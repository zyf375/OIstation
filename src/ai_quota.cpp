#include "../include/ai_quota.h"
#include "../include/common.h"
#include <map>
#include <mutex>
#include <ctime>
#include <cmath>
#include <cstdio>

namespace aiquota {

static const char* kCfgFile = "./data/ai_quota.json";
static const char* kUsageFile = "./data/ai_usage.json";
static std::mutex g_mu;

static std::string monthKey(time_t t) {
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    lt = *localtime(&t);
#endif
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d", lt.tm_year + 1900, lt.tm_mon + 1);
    return std::string(buf);
}

// ---------------- 配置 ----------------
Config load() {
    Config c;
    std::string raw = common::readFile(kCfgFile);
    if (!raw.empty()) {
        auto o = common::jsonParseObject(raw);
        if (o.count("windowHours")) c.windowHours = common::stringToInt(o["windowHours"]);
        if (o.count("defWindow")) c.defWindow = common::stringToLongLong(o["defWindow"]);
        if (o.count("defMonth")) c.defMonth = common::stringToLongLong(o["defMonth"]);
        if (o.count("defPerK")) c.defPerK = atof(o["defPerK"].c_str());
        if (o.count("prices")) {
            // 格式：model|perK;model2|perK2
            auto items = common::split(o["prices"], ';');
            for (size_t i = 0; i < items.size(); i++) {
                auto kv = common::split(items[i], '|');
                if (kv.size() != 2) continue;
                Price p;
                p.model = common::trim(kv[0]);
                p.perK = atof(common::trim(kv[1]).c_str());
                if (!p.model.empty() && p.perK > 0) c.prices.push_back(p);
            }
        }
    }
    if (c.windowHours < 1) c.windowHours = 5;
    if (c.windowHours > 168) c.windowHours = 168;
    if (c.defWindow < 0) c.defWindow = 0;
    if (c.defMonth < 0) c.defMonth = 0;
    if (c.defPerK <= 0) c.defPerK = 1.0;
    if (c.prices.empty()) {
        Price a; a.model = "deepseek-chat"; a.perK = 1.0; c.prices.push_back(a);
        Price b; b.model = "deepseek-reasoner"; b.perK = 2.0; c.prices.push_back(b);
    }
    return c;
}

void save(const Config& cfg) {
    std::map<std::string, std::string> m;
    m["windowHours"] = std::to_string(cfg.windowHours);
    m["defWindow"] = std::to_string(cfg.defWindow);
    m["defMonth"] = std::to_string(cfg.defMonth);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", cfg.defPerK);
    m["defPerK"] = buf;
    std::string prices;
    for (size_t i = 0; i < cfg.prices.size(); i++) {
        if (i) prices += ";";
        char pb[64];
        snprintf(pb, sizeof(pb), "%.4f", cfg.prices[i].perK);
        prices += cfg.prices[i].model + "|" + std::string(pb);
    }
    m["prices"] = prices;
    common::writeFile(kCfgFile, common::jsonStringify(m));
}

double priceOf(const std::string& model) {
    Config c = load();
    for (size_t i = 0; i < c.prices.size(); i++)
        if (c.prices[i].model == model) return c.prices[i].perK;
    return c.defPerK;
}

long long costOf(const std::string& model, long long tokens) {
    if (tokens <= 0) return 0;
    double price = priceOf(model);
    if (price <= 0) price = 1.0;
    double cost = (double)tokens * price / 1000.0;
    long long ci = (long long)ceil(cost);
    if (ci < 1) ci = 1;   // 每次对话至少 1 算力点
    return ci;
}

// ---------------- 用量 ----------------
namespace {

struct UsageRec {
    std::string userId;
    std::string month;
    long long monthUsed = 0;
    long long bonus = 0;              // 本月额外额度（商城/兑换码）
    long long extra = 0;              // 额外算力点余额（永久，用完即没）
    std::vector<std::pair<long long, long long> > events;  // (时间戳, 算力点)
};

std::vector<UsageRec> loadUsage() {
    std::vector<UsageRec> out;
    std::string raw = common::readFile(kUsageFile);
    if (raw.empty()) return out;
    auto lines = common::split(raw, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) continue;
        auto o = common::jsonParseObject(lines[i]);
        if (o.empty()) continue;
        UsageRec r;
        r.userId = o.count("userId") ? o["userId"] : "";
        r.month = o.count("month") ? o["month"] : "";
        r.monthUsed = o.count("monthUsed") ? common::stringToLongLong(o["monthUsed"]) : 0;
        r.bonus = o.count("bonus") ? common::stringToLongLong(o["bonus"]) : 0;
        r.extra = o.count("extra") ? common::stringToLongLong(o["extra"]) : 0;
        if (o.count("events")) {
            auto evs = common::split(o["events"], ',');
            for (size_t k = 0; k < evs.size(); k++) {
                auto kv = common::split(evs[k], ':');
                if (kv.size() != 2) continue;
                r.events.push_back(std::make_pair(common::stringToLongLong(kv[0]), common::stringToLongLong(kv[1])));
            }
        }
        out.push_back(r);
    }
    return out;
}

std::string usageToLine(const UsageRec& r) {
    std::map<std::string, std::string> m;
    m["userId"] = r.userId;
    m["month"] = r.month;
    m["monthUsed"] = std::to_string(r.monthUsed);
    m["bonus"] = std::to_string(r.bonus);
    m["extra"] = std::to_string(r.extra);
    std::string ev;
    for (size_t i = 0; i < r.events.size(); i++) {
        if (i) ev += ",";
        ev += std::to_string(r.events[i].first) + ":" + std::to_string(r.events[i].second);
    }
    m["events"] = ev;
    return common::jsonStringify(m);
}

void saveUsage(const std::vector<UsageRec>& all) {
    std::string out;
    for (size_t i = 0; i < all.size(); i++) out += usageToLine(all[i]) + "\n";
    common::writeFile(kUsageFile, out);
}

UsageRec* findRec(std::vector<UsageRec>& all, const std::string& userId) {
    for (size_t i = 0; i < all.size(); i++) if (all[i].userId == userId) return &all[i];
    return nullptr;
}

// 归一化：跨月清零、裁剪过期窗口事件
void normalize(UsageRec& r, int windowHours) {
    std::string cur = monthKey(time(NULL));
    if (r.month != cur) {
        r.month = cur;
        r.monthUsed = 0;
        r.bonus = 0;
        r.events.clear();
        // 注意：extra（商店购买的额外算力点）不随月份重置
        return;
    }
    long long cut = (long long)time(NULL) - (long long)windowHours * 3600;
    std::vector<std::pair<long long, long long> > keep;
    for (size_t i = 0; i < r.events.size(); i++)
        if (r.events[i].first >= cut) keep.push_back(r.events[i]);
    r.events = keep;
}

} // namespace

Info info(const std::string& userId, long long userWin, long long userMonth) {
    std::lock_guard<std::mutex> lk(g_mu);
    Config cfg = load();
    Info out;
    out.windowQuota = userWin > 0 ? userWin : cfg.defWindow;
    out.monthQuota = (userMonth > 0 ? userMonth : cfg.defMonth);
    auto all = loadUsage();
    UsageRec* r = findRec(all, userId);
    long long windowUsed = 0, monthUsed = 0, bonus = 0;
    if (r) {
        normalize(*r, cfg.windowHours);
        for (size_t i = 0; i < r->events.size(); i++) windowUsed += r->events[i].second;
        monthUsed = r->monthUsed;
        bonus = r->bonus;
    }
    out.windowUsed = windowUsed;
    out.monthUsed = monthUsed;
    out.monthQuota += bonus;   // 商城/兑换码增加的额度
    out.extra = r ? r->extra : 0;
    return out;
}

std::vector<Info> infoMany(const std::vector<InfoQuery>& qs) {
    std::lock_guard<std::mutex> lk(g_mu);
    Config cfg = load();
    std::vector<UsageRec> all = loadUsage();
    std::vector<Info> out;
    out.reserve(qs.size());
    for (size_t i = 0; i < qs.size(); i++) {
        Info o;
        o.windowQuota = qs[i].windowQuota > 0 ? qs[i].windowQuota : cfg.defWindow;
        o.monthQuota = qs[i].monthQuota > 0 ? qs[i].monthQuota : cfg.defMonth;
        UsageRec* r = findRec(all, qs[i].userId);
        if (r) {
            normalize(*r, cfg.windowHours);
            for (size_t k = 0; k < r->events.size(); k++) o.windowUsed += r->events[k].second;
            o.monthUsed = r->monthUsed;
            o.monthQuota += r->bonus;
            o.extra = r->extra;
        }
        out.push_back(o);
    }
    return out;
}

long long remaining(const Info& i) {
    long long w = i.windowQuota - i.windowUsed;
    long long m = i.monthQuota - i.monthUsed;
    if (w < 0) w = 0;
    if (m < 0) m = 0;
    long long normal = w < m ? w : m;
    return normal + i.extra;   // 额外算力点不受 5 小时/月度限制
}

std::string jsonOf(const Info& i) {
    std::map<std::string, std::string> m;
    m["windowQuota"] = std::to_string(i.windowQuota);
    m["windowUsed"] = std::to_string(i.windowUsed);
    m["monthQuota"] = std::to_string(i.monthQuota);
    m["monthUsed"] = std::to_string(i.monthUsed);
    m["extra"] = std::to_string(i.extra);
    m["remaining"] = std::to_string(remaining(i));
    return common::jsonStringify(m);
}

Info record(const std::string& userId, long long cost, long long userWin, long long userMonth) {
    if (cost < 0) cost = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config cfg = load();
        auto all = loadUsage();
        UsageRec* r = findRec(all, userId);
        if (!r) {
            UsageRec nr;
            nr.userId = userId;
            nr.month = monthKey(time(NULL));
            all.push_back(nr);
            r = &all[all.size() - 1];
        }
        normalize(*r, cfg.windowHours);
        if (cost > 0) {
            // 先扣免费额度（窗口 + 月度取更紧），不足部分扣商店购买的额外算力点
            long long wq = userWin > 0 ? userWin : cfg.defWindow;
            long long mq = (userMonth > 0 ? userMonth : cfg.defMonth) + r->bonus;
            long long wUsed = 0;
            for (size_t i = 0; i < r->events.size(); i++) wUsed += r->events[i].second;
            long long wLeft = wq - wUsed; if (wLeft < 0) wLeft = 0;
            long long mLeft = mq - r->monthUsed; if (mLeft < 0) mLeft = 0;
            long long normalLeft = wLeft < mLeft ? wLeft : mLeft;
            long long normalPart = cost < normalLeft ? cost : normalLeft;
            long long extraPart = cost - normalPart;
            if (extraPart > r->extra) extraPart = r->extra;
            if (normalPart > 0) {
                r->monthUsed += normalPart;
                r->events.push_back(std::make_pair((long long)time(NULL), normalPart));
            }
            r->extra -= extraPart;
            if (r->extra < 0) r->extra = 0;
        }
        saveUsage(all);
    }
    return info(userId, userWin, userMonth);
}

void addBonusMonth(const std::string& userId, long long amount) {
    if (userId.empty() || amount <= 0) return;
    std::lock_guard<std::mutex> lk(g_mu);
    Config cfg = load();
    auto all = loadUsage();
    UsageRec* r = findRec(all, userId);
    if (!r) {
        UsageRec nr;
        nr.userId = userId;
        nr.month = monthKey(time(NULL));
        all.push_back(nr);
        r = &all[all.size() - 1];
    }
    normalize(*r, cfg.windowHours);
    r->bonus += amount;
    saveUsage(all);
}

void addExtra(const std::string& userId, long long amount) {
    if (userId.empty() || amount <= 0) return;
    std::lock_guard<std::mutex> lk(g_mu);
    Config cfg = load();
    auto all = loadUsage();
    UsageRec* r = findRec(all, userId);
    if (!r) {
        UsageRec nr;
        nr.userId = userId;
        nr.month = monthKey(time(NULL));
        all.push_back(nr);
        r = &all[all.size() - 1];
    }
    normalize(*r, cfg.windowHours);
    r->extra += amount;
    saveUsage(all);
}

void resetMonth(const std::string& userId) {    std::lock_guard<std::mutex> lk(g_mu);
    Config cfg = load();
    auto all = loadUsage();
    UsageRec* r = findRec(all, userId);
    if (!r) return;
    normalize(*r, cfg.windowHours);
    r->monthUsed = 0;
    r->events.clear();
    saveUsage(all);
}

} // namespace aiquota

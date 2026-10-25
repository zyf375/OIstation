#include "../include/ai_plans.h"
#include "../include/models.h"
#include "../include/common.h"
#include <map>
#include <mutex>
#include <cstdio>

namespace aiplan {

static const char* kFile = "./data/ai_plans.json";
static const char* kSetFile = "./data/ai_plan_settings.json";
static std::mutex g_mu;

static std::string joinModels(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].empty()) continue;
        if (!out.empty()) out += ";";
        out += v[i];
    }
    return out;
}

static std::vector<std::string> splitModels(const std::string& s) {
    std::vector<std::string> out;
    auto items = common::split(s, ';');
    for (size_t i = 0; i < items.size(); i++) {
        std::string t = common::trim(items[i]);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

std::vector<Plan> load() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Plan> out;
    std::string raw = common::readFile(kFile);
    auto lines = common::split(raw, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = common::trim(lines[i]);
        if (line.empty()) continue;
        auto o = common::jsonParseObject(line);
        if (o.empty()) continue;
        Plan p;
        p.id = o.count("id") ? o["id"] : "";
        if (p.id.empty()) continue;
        p.name = o.count("name") ? o["name"] : p.id;
        p.desc = o.count("desc") ? o["desc"] : "";
        p.windowQuota = o.count("windowQuota") ? common::stringToLongLong(o["windowQuota"]) : 0;
        p.monthQuota = o.count("monthQuota") ? common::stringToLongLong(o["monthQuota"]) : 0;
        p.price = o.count("price") ? atof(o["price"].c_str()) : 0;
        if (o.count("models")) p.models = splitModels(o["models"]);
        out.push_back(p);
    }
    return out;
}

void save(const std::vector<Plan>& list) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string out;
    for (size_t i = 0; i < list.size(); i++) {
        std::map<std::string, std::string> m;
        m["id"] = list[i].id;
        m["name"] = list[i].name;
        m["desc"] = list[i].desc;
        m["windowQuota"] = std::to_string(list[i].windowQuota);
        m["monthQuota"] = std::to_string(list[i].monthQuota);
        char pb[40];
        snprintf(pb, sizeof(pb), "%.2f", list[i].price);
        m["price"] = pb;
        m["models"] = joinModels(list[i].models);
        out += common::jsonStringify(m) + "\n";
    }
    common::writeFile(kFile, out);
}

bool find(const std::string& id, Plan& out) {
    if (id.empty()) return false;
    std::vector<Plan> all = load();
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].id == id) { out = all[i]; return true; }
    }
    return false;
}

bool anyPlan() {
    return !load().empty();
}

bool allowedModel(const std::string& planId, const std::string& modelId) {
    Plan p;
    if (!find(planId, p)) return true;          // 没有套餐 = 不限制模型
    if (p.models.empty()) return true;
    for (size_t i = 0; i < p.models.size(); i++)
        if (p.models[i] == modelId) return true;
    return false;
}

bool userAllowedModel(const User& u, const std::string& modelId) {
    return allowedModel(u.aiPlanId, modelId);
}

bool applyPlan(User& u, const std::string& planId) {
    if (planId.empty()) {
        u.aiPlanId.clear();
        u.aiWindowQuota = 0;
        u.aiMonthQuota = 0;
        return true;
    }
    Plan p;
    if (!find(planId, p)) return false;
    u.aiPlanId = p.id;
    u.aiWindowQuota = p.windowQuota;
    u.aiMonthQuota = p.monthQuota;
    return true;
}

static std::map<std::string, std::string> loadSettings() {
    std::map<std::string, std::string> m;
    std::string raw = common::readFile(kSetFile);
    if (!raw.empty()) m = common::jsonParseObject(raw);
    return m;
}

std::string defaultPlanId() {
    auto o = loadSettings();
    if (o.count("defaultPlan")) return common::trim(o["defaultPlan"]);
    // 未配置时默认使用 free 套餐（存在的话）
    Plan p;
    if (find("free", p)) return "free";
    return "";
}

void setDefaultPlanId(const std::string& id) {
    auto m = loadSettings();
    m["defaultPlan"] = common::trim(id);
    common::writeFile(kSetFile, common::jsonStringify(m));
}

std::string testModelId() {
    auto m = loadSettings();
    if (m.count("testModel")) return common::trim(m["testModel"]);
    return "";
}

void setTestModelId(const std::string& id) {
    auto m = loadSettings();
    m["testModel"] = common::trim(id);
    common::writeFile(kSetFile, common::jsonStringify(m));
}

void ensureSeed() {
    if (anyPlan()) return;
    std::vector<Plan> all;
    Plan a; a.id = "free"; a.name = "免费版"; a.desc = "体验 OI 助手，5 小时 80 / 每月 600 算力点";
    a.windowQuota = 80; a.monthQuota = 600; all.push_back(a);
    Plan b; b.id = "member"; b.name = "会员版"; b.desc = "日常刷题与讲解，5 小时 400 / 每月 4000 算力点";
    b.windowQuota = 400; b.monthQuota = 4000; all.push_back(b);
    Plan c; c.id = "pro"; c.name = "专业版"; c.desc = "高强度训练，5 小时 1200 / 每月 15000 算力点";
    c.windowQuota = 1200; c.monthQuota = 15000; all.push_back(c);
    save(all);
}

} // namespace aiplan

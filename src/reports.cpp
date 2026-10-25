#include "../include/reports.h"
#include "../include/common.h"
#include <map>
#include <mutex>

namespace reports {

static const char* kFile = "./data/reports.json";
static std::mutex g_mu;

static std::string field(const std::map<std::string, std::string>& o, const std::string& k) {
    auto it = o.find(k);
    return it == o.end() ? std::string() : it->second;
}

static Report fromLine(const std::string& line) {
    Report r;
    auto o = common::jsonParseObject(line);
    if (o.empty()) return r;
    r.id = field(o, "id");
    r.targetType = field(o, "targetType");
    r.targetId = field(o, "targetId");
    r.postId = field(o, "postId");
    r.title = field(o, "title");
    r.snippet = field(o, "snippet");
    r.authorId = field(o, "authorId");
    r.authorName = field(o, "authorName");
    r.reason = field(o, "reason");
    r.detail = field(o, "detail");
    r.reporterId = field(o, "reporterId");
    r.reporterName = field(o, "reporterName");
    r.time = field(o, "time");
    r.status = field(o, "status");
    r.handledBy = field(o, "handledBy");
    r.handledTime = field(o, "handledTime");
    r.result = field(o, "result");
    if (r.status.empty()) r.status = "pending";
    return r;
}

static std::string toLine(const Report& r) {
    std::map<std::string, std::string> m;
    m["id"] = r.id;
    m["targetType"] = r.targetType;
    m["targetId"] = r.targetId;
    m["postId"] = r.postId;
    m["title"] = r.title;
    m["snippet"] = r.snippet;
    m["authorId"] = r.authorId;
    m["authorName"] = r.authorName;
    m["reason"] = r.reason;
    m["detail"] = r.detail;
    m["reporterId"] = r.reporterId;
    m["reporterName"] = r.reporterName;
    m["time"] = r.time;
    m["status"] = r.status;
    m["handledBy"] = r.handledBy;
    m["handledTime"] = r.handledTime;
    m["result"] = r.result;
    return common::jsonStringify(m);
}

std::vector<Report> load() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Report> out;
    std::string raw = common::readFile(kFile);
    auto lines = common::split(raw, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = common::trim(lines[i]);
        if (line.empty()) continue;
        Report r = fromLine(line);
        if (r.id.empty()) continue;
        out.push_back(r);
    }
    return out;
}

void save(const std::vector<Report>& list) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string out;
    for (size_t i = 0; i < list.size(); i++) out += toLine(list[i]) + "\n";
    common::writeFile(kFile, out);
}

bool add(const Report& r) {
    if (r.id.empty()) return false;
    std::vector<Report> all = load();
    all.push_back(r);
    save(all);
    return true;
}

bool find(const std::string& id, Report& out) {
    std::vector<Report> all = load();
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].id == id) { out = all[i]; return true; }
    }
    return false;
}

bool updateStatus(const std::string& id, const std::string& status,
                  const std::string& by, const std::string& result) {
    std::vector<Report> all = load();
    bool ok = false;
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].id != id) continue;
        all[i].status = status;
        all[i].handledBy = by;
        all[i].handledTime = common::getTimeStr();
        if (!result.empty()) all[i].result = result;
        ok = true;
    }
    if (ok) save(all);
    return ok;
}

int pendingCount() {
    std::vector<Report> all = load();
    int n = 0;
    for (size_t i = 0; i < all.size(); i++) if (all[i].status == "pending") n++;
    return n;
}

int pendingCountOf(const std::string& reporterId, const std::string& targetId) {
    std::vector<Report> all = load();
    int n = 0;
    for (size_t i = 0; i < all.size(); i++)
        if (all[i].status == "pending" && all[i].reporterId == reporterId && all[i].targetId == targetId) n++;
    return n;
}

void markTargetHandled(const std::string& targetId, const std::string& by, const std::string& result) {
    std::vector<Report> all = load();
    bool changed = false;
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].targetId != targetId || all[i].status != "pending") continue;
        all[i].status = "handled";
        all[i].handledBy = by;
        all[i].handledTime = common::getTimeStr();
        all[i].result = result;
        changed = true;
    }
    if (changed) save(all);
}

} // namespace reports

#include "../include/ai_providers.h"
#include "../include/common.h"
#include <map>
#include <mutex>

namespace aiprov {

static const char* kFile = "./data/ai_providers.json";
static std::mutex g_mu;

static Provider fromLine(const std::string& line) {
    Provider p;
    auto o = common::jsonParseObject(line);
    if (o.empty()) return p;
    p.id = o.count("id") ? o["id"] : "";
    p.name = o.count("name") ? o["name"] : p.id;
    p.kind = o.count("kind") ? o["kind"] : "openai";
    p.baseUrl = o.count("baseUrl") ? o["baseUrl"] : "";
    p.apiKey = o.count("apiKey") ? o["apiKey"] : "";
    p.apiMode = o.count("apiMode") ? common::trim(o["apiMode"]) : "chat";
    if (p.apiMode != "responses") p.apiMode = "chat";
    p.enabled = o.count("enabled") ? (o["enabled"] == "1" || o["enabled"] == "true") : true;
    p.freeOnly = o.count("freeOnly") ? (o["freeOnly"] == "1" || o["freeOnly"] == "true") : false;
    if (o.count("models")) p.models = parseModels(o["models"]);
    while (!p.baseUrl.empty() && p.baseUrl[p.baseUrl.size() - 1] == '/')
        p.baseUrl.erase(p.baseUrl.size() - 1);
    return p;
}

static std::string toLine(const Provider& p) {
    std::map<std::string, std::string> m;
    m["id"] = p.id;
    m["name"] = p.name;
    m["kind"] = p.kind;
    m["baseUrl"] = p.baseUrl;
    m["apiKey"] = p.apiKey;
    m["apiMode"] = p.apiMode.empty() ? "chat" : p.apiMode;
    m["enabled"] = p.enabled ? "1" : "0";
    m["freeOnly"] = p.freeOnly ? "1" : "0";
    m["models"] = modelsField(p.models);
    return common::jsonStringify(m);
}

std::vector<Provider> load() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Provider> out;
    std::string raw = common::readFile(kFile);
    auto lines = common::split(raw, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = common::trim(lines[i]);
        if (line.empty()) continue;
        Provider p = fromLine(line);
        if (p.id.empty() || p.baseUrl.empty()) continue;
        out.push_back(p);
    }
    return out;
}

void save(const std::vector<Provider>& list) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string out;
    for (size_t i = 0; i < list.size(); i++) out += toLine(list[i]) + "\n";
    common::writeFile(kFile, out);
}

bool find(const std::string& id, Provider& out) {
    std::vector<Provider> all = load();
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].id == id) { out = all[i]; return true; }
    }
    return false;
}

bool resolve(const std::string& modelId, Provider& p, ModelRef& m) {
    std::vector<Provider> all = load();
    for (size_t i = 0; i < all.size(); i++) {
        if (!all[i].enabled) continue;
        for (size_t k = 0; k < all[i].models.size(); k++) {
            if (all[i].models[k].id == modelId) {
                p = all[i];
                m = all[i].models[k];
                return true;
            }
        }
    }
    return false;
}

std::vector<ModelEntry> entries() {
    std::vector<ModelEntry> out;
    std::vector<Provider> all = load();
    for (size_t i = 0; i < all.size(); i++) {
        if (!all[i].enabled) continue;
        for (size_t k = 0; k < all[i].models.size(); k++) {
            ModelEntry e;
            e.id = all[i].models[k].id;
            e.name = all[i].models[k].name;
            e.providerId = all[i].id;
            e.providerName = all[i].name;
            e.freeModel = all[i].freeOnly;
            out.push_back(e);
        }
    }
    return out;
}

std::string maskKey(const std::string& key) {
    if (key.empty()) return "";
    if (key.size() <= 12) return "****";
    return key.substr(0, 6) + "****" + key.substr(key.size() - 4);
}

std::string modelsField(const std::vector<ModelRef>& models) {
    std::string out;
    for (size_t i = 0; i < models.size(); i++) {
        if (models[i].id.empty()) continue;
        if (!out.empty()) out += ";";
        out += models[i].id + "|" + (models[i].name.empty() ? models[i].id : models[i].name);
    }
    return out;
}

std::vector<ModelRef> parseModels(const std::string& field) {
    std::vector<ModelRef> out;
    auto items = common::split(field, ';');
    for (size_t i = 0; i < items.size(); i++) {
        std::string it = common::trim(items[i]);
        if (it.empty()) continue;
        size_t bar = it.find('|');
        ModelRef m;
        if (bar == std::string::npos) { m.id = it; m.name = it; }
        else { m.id = common::trim(it.substr(0, bar)); m.name = common::trim(it.substr(bar + 1)); }
        if (m.id.empty()) continue;
        if (m.name.empty()) m.name = m.id;
        out.push_back(m);
    }
    return out;
}

void ensureSeed() {
    std::vector<Provider> all = load();
    if (!all.empty()) return;
    Provider p;
    p.id = "deepseek";
    p.name = "DeepSeek 官方";
    p.kind = "deepseek";
    p.baseUrl = "https://api.deepseek.com";
    std::string raw = common::readFile("./data/ai.json");
    if (!raw.empty()) {
        auto o = common::jsonParseObject(raw);
        if (o.count("apiKey")) p.apiKey = common::trim(o["apiKey"]);
        if (o.count("baseUrl") && !o["baseUrl"].empty()) p.baseUrl = common::trim(o["baseUrl"]);
    }
    ModelRef a; a.id = "deepseek-flash"; a.name = "DeepSeek-V4.1-Flash"; p.models.push_back(a);
    ModelRef b; b.id = "deepseek-v4-pro"; b.name = "DeepSeek-V4-Pro"; p.models.push_back(b);
    all.push_back(p);
    save(all);
}

} // namespace aiprov

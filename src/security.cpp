#include "../include/security.h"
#include "../include/common.h"
#include <ctime>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <deque>
#include <set>
#include <map>
#include <mutex>

namespace security {

// ========== IP ban (permanent, manual) ==========
static std::set<std::string> g_bannedIps;
static std::mutex g_banMutex;
static const char* BAN_FILE = "./data/banned_ips.json";

void loadBans() {
    std::lock_guard<std::mutex> lock(g_banMutex);
    g_bannedIps.clear();
    std::string content = common::readFile(BAN_FILE);
    if (content.empty()) return;
    auto arr = common::jsonParseArray(content);
    for (size_t i = 0; i < arr.size(); ++i) {
        std::string ip = arr[i];
        ip.erase(std::remove(ip.begin(), ip.end(), '"'), ip.end());
        ip.erase(std::remove(ip.begin(), ip.end(), '\''), ip.end());
        if (!ip.empty()) g_bannedIps.insert(ip);
    }
}

void saveBans() {
    std::lock_guard<std::mutex> lock(g_banMutex);
    std::string data = "[";
    bool first = true;
    for (const auto& ip : g_bannedIps) {
        if (!first) data += ",";
        first = false;
        data += common::jsonStringify(ip);
    }
    data += "]";
    common::writeFile(BAN_FILE, data);
}

bool isIpBanned(const std::string& ip) {
    if (ip.empty()) return false;
    std::lock_guard<std::mutex> lock(g_banMutex);
    return g_bannedIps.find(ip) != g_bannedIps.end();
}

void banIp(const std::string& ip) {
    if (ip.empty()) return;
    { std::lock_guard<std::mutex> lock(g_banMutex); g_bannedIps.insert(ip); }
    saveBans();
}

void unbanIp(const std::string& ip) {
    { std::lock_guard<std::mutex> lock(g_banMutex); g_bannedIps.erase(ip); }
    saveBans();
}

std::vector<std::string> bannedIps() {
    std::lock_guard<std::mutex> lock(g_banMutex);
    std::vector<std::string> out;
    for (const auto& ip : g_bannedIps) out.push_back(ip);
    return out;
}

// ========== Security thresholds (admin-configurable) ==========
static SecConfig g_cfg;
static std::mutex g_cfgMutex;
static const char* OISEC_FILE = "./data/security.json";
static bool g_secLoaded = false;

static SecConfig loadSecConfigFromFile() {
    SecConfig c;
    std::string content = common::readFile(OISEC_FILE);
    if (content.empty()) return c;
    auto obj = common::jsonParseObject(content);
    if (obj.count("ratePerSec"))   c.ratePerSec    = common::stringToInt(obj["ratePerSec"]);
    if (obj.count("burstThreshold")) c.burstThreshold = common::stringToInt(obj["burstThreshold"]);
    if (obj.count("windowSec"))    c.windowSec     = common::stringToInt(obj["windowSec"]);
    if (obj.count("autoBanSec"))   c.autoBanSec    = common::stringToInt(obj["autoBanSec"]);
    if (obj.count("captchaMinSec")) c.captchaMinSec = common::stringToInt(obj["captchaMinSec"]);
    if (obj.count("captchaExpireSec")) c.captchaExpireSec = common::stringToInt(obj["captchaExpireSec"]);
    if (obj.count("captchaStrict")) c.captchaStrict = common::stringToInt(obj["captchaStrict"]);
    if (obj.count("captchaEnabled")) c.captchaEnabled = common::stringToInt(obj["captchaEnabled"]);
    if (obj.count("capLogin")) c.capLogin = common::stringToInt(obj["capLogin"]);
    if (obj.count("capRegister")) c.capRegister = common::stringToInt(obj["capRegister"]);
    if (obj.count("capSubmit")) c.capSubmit = common::stringToInt(obj["capSubmit"]);
    if (obj.count("capBlog")) c.capBlog = common::stringToInt(obj["capBlog"]);
    if (obj.count("capDiscuss")) c.capDiscuss = common::stringToInt(obj["capDiscuss"]);
    if (obj.count("capTask")) c.capTask = common::stringToInt(obj["capTask"]);
    if (obj.count("capShop")) c.capShop = common::stringToInt(obj["capShop"]);
    if (obj.count("capMessage")) c.capMessage = common::stringToInt(obj["capMessage"]);
    if (obj.count("capGroup")) c.capGroup = common::stringToInt(obj["capGroup"]);
    if (obj.count("capBenben")) c.capBenben = common::stringToInt(obj["capBenben"]);
    if (obj.count("maxBenben")) c.maxBenben = common::stringToInt(obj["maxBenben"]);
    if (obj.count("maxBlog")) c.maxBlog = common::stringToInt(obj["maxBlog"]);
    if (obj.count("maxBlogTitle")) c.maxBlogTitle = common::stringToInt(obj["maxBlogTitle"]);
    if (obj.count("maxDiscuss")) c.maxDiscuss = common::stringToInt(obj["maxDiscuss"]);
    if (obj.count("maxComment")) c.maxComment = common::stringToInt(obj["maxComment"]);
    if (obj.count("maxTaskTitle")) c.maxTaskTitle = common::stringToInt(obj["maxTaskTitle"]);
    if (obj.count("maxTaskDesc")) c.maxTaskDesc = common::stringToInt(obj["maxTaskDesc"]);
    if (obj.count("maxTaskMsg")) c.maxTaskMsg = common::stringToInt(obj["maxTaskMsg"]);
    if (obj.count("maxMessage")) c.maxMessage = common::stringToInt(obj["maxMessage"]);
    if (obj.count("maxGroupName")) c.maxGroupName = common::stringToInt(obj["maxGroupName"]);
    if (obj.count("maxBio")) c.maxBio = common::stringToInt(obj["maxBio"]);
    if (obj.count("maxUsername")) c.maxUsername = common::stringToInt(obj["maxUsername"]);
    if (obj.count("maxCode")) c.maxCode = common::stringToInt(obj["maxCode"]);
    if (obj.count("blogIntervalSec")) c.blogIntervalSec = common::stringToInt(obj["blogIntervalSec"]);
    if (obj.count("discussIntervalSec")) c.discussIntervalSec = common::stringToInt(obj["discussIntervalSec"]);
    if (obj.count("submitIntervalSec")) c.submitIntervalSec = common::stringToInt(obj["submitIntervalSec"]);
    if (obj.count("benbenIntervalSec")) c.benbenIntervalSec = common::stringToInt(obj["benbenIntervalSec"]);
    if (obj.count("registerIntervalSec")) c.registerIntervalSec = common::stringToInt(obj["registerIntervalSec"]);
    if (obj.count("maxProblemId")) c.maxProblemId = common::stringToInt(obj["maxProblemId"]);
    if (obj.count("maxProblemTitle")) c.maxProblemTitle = common::stringToInt(obj["maxProblemTitle"]);
    if (obj.count("maxProblemDesc")) c.maxProblemDesc = common::stringToInt(obj["maxProblemDesc"]);
    if (obj.count("maxProblemSample")) c.maxProblemSample = common::stringToInt(obj["maxProblemSample"]);
    if (obj.count("maxUserProblems")) c.maxUserProblems = common::stringToInt(obj["maxUserProblems"]);
    if (obj.count("maxUserProblemTime")) c.maxUserProblemTime = common::stringToInt(obj["maxUserProblemTime"]);
    if (obj.count("maxUserProblemMem")) c.maxUserProblemMem = common::stringToInt(obj["maxUserProblemMem"]);
    if (obj.count("maxCustomTestTime")) c.maxCustomTestTime = common::stringToInt(obj["maxCustomTestTime"]);
    if (obj.count("maxCustomTestMem")) c.maxCustomTestMem = common::stringToInt(obj["maxCustomTestMem"]);
    if (obj.count("maxCustomTestCode")) c.maxCustomTestCode = common::stringToInt(obj["maxCustomTestCode"]);
    if (obj.count("maxCustomTestData")) c.maxCustomTestData = common::stringToInt(obj["maxCustomTestData"]);
    if (obj.count("customTestIntervalSec")) c.customTestIntervalSec = common::stringToInt(obj["customTestIntervalSec"]);
    if (obj.count("hackRetestEnabled")) c.hackRetestEnabled = common::stringToInt(obj["hackRetestEnabled"]);
    if (obj.count("hackIntervalSec")) c.hackIntervalSec = common::stringToInt(obj["hackIntervalSec"]);
    if (obj.count("judgeThreads")) c.judgeThreads = common::stringToInt(obj["judgeThreads"]);
    if (obj.count("codeGuardEnabled")) c.codeGuardEnabled = common::stringToInt(obj["codeGuardEnabled"]);
    if (obj.count("guardHeaders")) c.guardHeaders = common::stringToInt(obj["guardHeaders"]);
    if (obj.count("guardProcess")) c.guardProcess = common::stringToInt(obj["guardProcess"]);
    if (obj.count("guardWinApi")) c.guardWinApi = common::stringToInt(obj["guardWinApi"]);
    if (obj.count("guardNetwork")) c.guardNetwork = common::stringToInt(obj["guardNetwork"]);
    if (obj.count("guardDll")) c.guardDll = common::stringToInt(obj["guardDll"]);
    if (obj.count("guardFileFuncs")) c.guardFileFuncs = common::stringToInt(obj["guardFileFuncs"]);
    if (obj.count("guardPathEscape")) c.guardPathEscape = common::stringToInt(obj["guardPathEscape"]);
    if (obj.count("guardCompilerExt")) c.guardCompilerExt = common::stringToInt(obj["guardCompilerExt"]);
    if (obj.count("guardExtraFuncs")) c.guardExtraFuncs = obj["guardExtraFuncs"];
    if (obj.count("guardAllowFuncs")) c.guardAllowFuncs = obj["guardAllowFuncs"];
    if (obj.count("guardExtraHeaders")) c.guardExtraHeaders = obj["guardExtraHeaders"];
    if (obj.count("gppPath")) c.gppPath = obj["gppPath"];
    if (c.ratePerSec < 1) c.ratePerSec = 30;
    if (c.burstThreshold < 2) c.burstThreshold = 100;
    if (c.windowSec < 1) c.windowSec = 5;
    if (c.autoBanSec < 1) c.autoBanSec = 30;
    if (c.captchaMinSec < 0) c.captchaMinSec = 0;
    if (c.captchaExpireSec < 30) c.captchaExpireSec = 300;
    if (c.captchaStrict < 0) c.captchaStrict = 0;
    if (c.captchaStrict > 2) c.captchaStrict = 2;
    if (c.captchaEnabled != 0 && c.captchaEnabled != 1) c.captchaEnabled = 1;
    if (c.capLogin != 0 && c.capLogin != 1) c.capLogin = 1;
    if (c.capRegister != 0 && c.capRegister != 1) c.capRegister = 1;
    if (c.capSubmit != 0 && c.capSubmit != 1) c.capSubmit = 1;
    if (c.capBlog != 0 && c.capBlog != 1) c.capBlog = 1;
    if (c.capDiscuss != 0 && c.capDiscuss != 1) c.capDiscuss = 1;
    if (c.capTask != 0 && c.capTask != 1) c.capTask = 1;
    if (c.capShop != 0 && c.capShop != 1) c.capShop = 1;
    if (c.capBenben != 0 && c.capBenben != 1) c.capBenben = 1;
    if (c.maxBenben < 1) c.maxBenben = 500;
    if (c.maxBlog < 1) c.maxBlog = 10000;
    if (c.maxBlogTitle < 1) c.maxBlogTitle = 100;
    if (c.maxDiscuss < 1) c.maxDiscuss = 10000;
    if (c.maxComment < 1) c.maxComment = 2000;
    if (c.maxTaskTitle < 1) c.maxTaskTitle = 100;
    if (c.maxTaskDesc < 1) c.maxTaskDesc = 5000;
    if (c.maxTaskMsg < 1) c.maxTaskMsg = 2000;
    if (c.maxMessage < 1) c.maxMessage = 2000;
    if (c.maxGroupName < 1) c.maxGroupName = 50;
    if (c.maxBio < 1) c.maxBio = 500;
    if (c.maxUsername < 1) c.maxUsername = 20;
    if (c.maxCode < 1) c.maxCode = 100000;
    if (c.blogIntervalSec < 0) c.blogIntervalSec = 0;
    if (c.discussIntervalSec < 0) c.discussIntervalSec = 0;
    if (c.maxProblemId < 1) c.maxProblemId = 20;
    if (c.maxProblemTitle < 1) c.maxProblemTitle = 100;
    if (c.maxProblemDesc < 1) c.maxProblemDesc = 20000;
    if (c.maxProblemSample < 1) c.maxProblemSample = 2000;
    if (c.maxUserProblems < 0) c.maxUserProblems = 0;
    if (c.maxUserProblemTime < 1) c.maxUserProblemTime = 2000;
    if (c.maxUserProblemMem < 1) c.maxUserProblemMem = 256;
    if (c.maxCustomTestTime < 1) c.maxCustomTestTime = 2000;
    if (c.maxCustomTestMem < 1) c.maxCustomTestMem = 256;
    if (c.gppPath.empty()) c.gppPath = "";
    if (c.maxCustomTestCode < 1) c.maxCustomTestCode = 50000;
    if (c.maxCustomTestData < 1) c.maxCustomTestData = 100000;
    if (c.customTestIntervalSec < 0) c.customTestIntervalSec = 0;
    if (c.hackRetestEnabled < 0) c.hackRetestEnabled = 0;
    if (c.hackRetestEnabled > 1) c.hackRetestEnabled = 1;
    if (c.hackIntervalSec < 0) c.hackIntervalSec = 0;
    if (c.judgeThreads < 1) c.judgeThreads = 1;
    if (c.judgeThreads > 32) c.judgeThreads = 32;
    if (c.codeGuardEnabled != 0 && c.codeGuardEnabled != 1) c.codeGuardEnabled = 1;
    if (c.guardHeaders != 0 && c.guardHeaders != 1) c.guardHeaders = 1;
    if (c.guardProcess != 0 && c.guardProcess != 1) c.guardProcess = 1;
    if (c.guardWinApi != 0 && c.guardWinApi != 1) c.guardWinApi = 1;
    if (c.guardNetwork != 0 && c.guardNetwork != 1) c.guardNetwork = 1;
    if (c.guardDll != 0 && c.guardDll != 1) c.guardDll = 1;
    if (c.guardFileFuncs != 0 && c.guardFileFuncs != 1) c.guardFileFuncs = 1;
    if (c.guardPathEscape != 0 && c.guardPathEscape != 1) c.guardPathEscape = 1;
    if (c.guardCompilerExt != 0 && c.guardCompilerExt != 1) c.guardCompilerExt = 1;
    return c;
}

void loadSecurityConfig() {
    std::lock_guard<std::mutex> lock(g_cfgMutex);
    g_cfg = loadSecConfigFromFile();
    g_secLoaded = true;
}

SecConfig getSecConfig() {
    std::lock_guard<std::mutex> lock(g_cfgMutex);
    if (!g_secLoaded) { g_cfg = loadSecConfigFromFile(); g_secLoaded = true; }
    return g_cfg;
}

void setSecConfig(const SecConfig& c) {
    SecConfig v = c;
    if (v.ratePerSec < 1) v.ratePerSec = 30;
    if (v.burstThreshold < 2) v.burstThreshold = 100;
    if (v.windowSec < 1) v.windowSec = 5;
    if (v.autoBanSec < 1) v.autoBanSec = 30;
    if (v.captchaMinSec < 0) v.captchaMinSec = 0;
    if (v.captchaExpireSec < 30) v.captchaExpireSec = 300;
    if (v.captchaStrict < 0) v.captchaStrict = 0;
    if (v.captchaStrict > 2) v.captchaStrict = 2;
    if (v.captchaEnabled != 0 && v.captchaEnabled != 1) v.captchaEnabled = 1;
    if (v.capLogin != 0 && v.capLogin != 1) v.capLogin = 1;
    if (v.capRegister != 0 && v.capRegister != 1) v.capRegister = 1;
    if (v.capSubmit != 0 && v.capSubmit != 1) v.capSubmit = 1;
    if (v.capBlog != 0 && v.capBlog != 1) v.capBlog = 1;
    if (v.capDiscuss != 0 && v.capDiscuss != 1) v.capDiscuss = 1;
    if (v.capTask != 0 && v.capTask != 1) v.capTask = 1;
    if (v.capShop != 0 && v.capShop != 1) v.capShop = 1;
    if (v.capMessage != 0 && v.capMessage != 1) v.capMessage = 0;
    if (v.capGroup != 0 && v.capGroup != 1) v.capGroup = 1;
    if (v.capBenben != 0 && v.capBenben != 1) v.capBenben = 1;
    if (v.maxBenben < 1) v.maxBenben = 500;
    if (v.maxBlog < 1) v.maxBlog = 10000;
    if (v.maxBlogTitle < 1) v.maxBlogTitle = 100;
    if (v.maxDiscuss < 1) v.maxDiscuss = 10000;
    if (v.maxComment < 1) v.maxComment = 2000;
    if (v.maxTaskTitle < 1) v.maxTaskTitle = 100;
    if (v.maxTaskDesc < 1) v.maxTaskDesc = 5000;
    if (v.maxTaskMsg < 1) v.maxTaskMsg = 2000;
    if (v.maxMessage < 1) v.maxMessage = 2000;
    if (v.maxGroupName < 1) v.maxGroupName = 50;
    if (v.maxBio < 1) v.maxBio = 500;
    if (v.maxUsername < 1) v.maxUsername = 20;
    if (v.maxCode < 1) v.maxCode = 100000;
    if (v.blogIntervalSec < 0) v.blogIntervalSec = 0;
    if (v.discussIntervalSec < 0) v.discussIntervalSec = 0;
    if (v.submitIntervalSec < 0) v.submitIntervalSec = 0;
    if (v.benbenIntervalSec < 0) v.benbenIntervalSec = 0;
    if (v.registerIntervalSec < 0) v.registerIntervalSec = 0;

    if (v.maxProblemId < 1) v.maxProblemId = 20;
    if (v.maxProblemTitle < 1) v.maxProblemTitle = 100;
    if (v.maxProblemDesc < 1) v.maxProblemDesc = 20000;
    if (v.maxProblemSample < 1) v.maxProblemSample = 2000;
    if (v.maxUserProblems < 0) v.maxUserProblems = 0;
    if (v.maxUserProblemTime < 1) v.maxUserProblemTime = 2000;
    if (v.maxUserProblemMem < 1) v.maxUserProblemMem = 256;
    if (v.maxCustomTestTime < 1) v.maxCustomTestTime = 2000;
    if (v.maxCustomTestMem < 1) v.maxCustomTestMem = 256;
    if (v.maxCustomTestCode < 1) v.maxCustomTestCode = 50000;
    if (v.maxCustomTestData < 1) v.maxCustomTestData = 100000;
    if (v.customTestIntervalSec < 0) v.customTestIntervalSec = 0;
    if (v.hackRetestEnabled < 0) v.hackRetestEnabled = 0;
    if (v.hackRetestEnabled > 1) v.hackRetestEnabled = 1;
    if (v.hackIntervalSec < 0) v.hackIntervalSec = 0;
    if (v.judgeThreads < 1) v.judgeThreads = 1;
    if (v.judgeThreads > 32) v.judgeThreads = 32;
    if (v.codeGuardEnabled != 0 && v.codeGuardEnabled != 1) v.codeGuardEnabled = 1;
    if (v.guardHeaders != 0 && v.guardHeaders != 1) v.guardHeaders = 1;
    if (v.guardProcess != 0 && v.guardProcess != 1) v.guardProcess = 1;
    if (v.guardWinApi != 0 && v.guardWinApi != 1) v.guardWinApi = 1;
    if (v.guardNetwork != 0 && v.guardNetwork != 1) v.guardNetwork = 1;
    if (v.guardDll != 0 && v.guardDll != 1) v.guardDll = 1;
    if (v.guardFileFuncs != 0 && v.guardFileFuncs != 1) v.guardFileFuncs = 1;
    if (v.guardPathEscape != 0 && v.guardPathEscape != 1) v.guardPathEscape = 1;
    if (v.guardCompilerExt != 0 && v.guardCompilerExt != 1) v.guardCompilerExt = 1;
    if (v.guardExtraFuncs.size() > 4000) v.guardExtraFuncs = v.guardExtraFuncs.substr(0, 4000);
    if (v.guardAllowFuncs.size() > 4000) v.guardAllowFuncs = v.guardAllowFuncs.substr(0, 4000);
    if (v.guardExtraHeaders.size() > 4000) v.guardExtraHeaders = v.guardExtraHeaders.substr(0, 4000);
    if (v.gppPath.empty()) v.gppPath = "";    { std::lock_guard<std::mutex> lock(g_cfgMutex); g_cfg = v; g_secLoaded = true; }
    std::string data = "{\"ratePerSec\":" + std::to_string(v.ratePerSec) +
                       ",\"burstThreshold\":" + std::to_string(v.burstThreshold) +
                       ",\"windowSec\":" + std::to_string(v.windowSec) +
                       ",\"autoBanSec\":" + std::to_string(v.autoBanSec) +
                       ",\"captchaMinSec\":" + std::to_string(v.captchaMinSec) +
                       ",\"captchaExpireSec\":" + std::to_string(v.captchaExpireSec) +
                       ",\"captchaStrict\":" + std::to_string(v.captchaStrict) +
                       ",\"captchaEnabled\":" + std::to_string(v.captchaEnabled) +
                       ",\"capLogin\":" + std::to_string(v.capLogin) +
                       ",\"capRegister\":" + std::to_string(v.capRegister) +
                       ",\"capSubmit\":" + std::to_string(v.capSubmit) +
                       ",\"capBlog\":" + std::to_string(v.capBlog) +
                       ",\"capDiscuss\":" + std::to_string(v.capDiscuss) +
                       ",\"capTask\":" + std::to_string(v.capTask) +
                       ",\"capShop\":" + std::to_string(v.capShop) + ",\"capMessage\":" + std::to_string(v.capMessage) + ",\"capGroup\":" + std::to_string(v.capGroup) +
                       ",\"capBenben\":" + std::to_string(v.capBenben) +
                       ",\"maxBenben\":" + std::to_string(v.maxBenben) +
                       ",\"maxBlog\":" + std::to_string(v.maxBlog) +
                       ",\"maxBlogTitle\":" + std::to_string(v.maxBlogTitle) +
                       ",\"maxDiscuss\":" + std::to_string(v.maxDiscuss) +
                       ",\"maxComment\":" + std::to_string(v.maxComment) +
                       ",\"maxTaskTitle\":" + std::to_string(v.maxTaskTitle) +
                       ",\"maxTaskDesc\":" + std::to_string(v.maxTaskDesc) +
                       ",\"maxTaskMsg\":" + std::to_string(v.maxTaskMsg) +
                       ",\"maxMessage\":" + std::to_string(v.maxMessage) +
                       ",\"maxGroupName\":" + std::to_string(v.maxGroupName) +
                       ",\"maxBio\":" + std::to_string(v.maxBio) +
                       ",\"maxUsername\":" + std::to_string(v.maxUsername) +
                       ",\"maxCode\":" + std::to_string(v.maxCode) +
                       ",\"blogIntervalSec\":" + std::to_string(v.blogIntervalSec) +
                       ",\"discussIntervalSec\":" + std::to_string(v.discussIntervalSec) +
                       ",\"submitIntervalSec\":" + std::to_string(v.submitIntervalSec) +
                       ",\"benbenIntervalSec\":" + std::to_string(v.benbenIntervalSec) +
                       ",\"registerIntervalSec\":" + std::to_string(v.registerIntervalSec) +
                       ",\"maxProblemId\":" + std::to_string(v.maxProblemId) +
                       ",\"maxProblemTitle\":" + std::to_string(v.maxProblemTitle) +
                       ",\"maxProblemDesc\":" + std::to_string(v.maxProblemDesc) +
                       ",\"maxProblemSample\":" + std::to_string(v.maxProblemSample) +
                       ",\"maxUserProblems\":" + std::to_string(v.maxUserProblems) +
                       ",\"maxUserProblemTime\":" + std::to_string(v.maxUserProblemTime) +
                       ",\"maxUserProblemMem\":" + std::to_string(v.maxUserProblemMem)
                       + ",\"maxCustomTestTime\":" + std::to_string(v.maxCustomTestTime)
                       + ",\"maxCustomTestMem\":" + std::to_string(v.maxCustomTestMem)
                       + ",\"maxCustomTestCode\":" + std::to_string(v.maxCustomTestCode)
                       + ",\"maxCustomTestData\":" + std::to_string(v.maxCustomTestData)
                       + ",\"customTestIntervalSec\":" + std::to_string(v.customTestIntervalSec)
                       + ",\"hackRetestEnabled\":" + std::to_string(v.hackRetestEnabled)
                       + ",\"hackIntervalSec\":" + std::to_string(v.hackIntervalSec)
                       + ",\"judgeThreads\":" + std::to_string(v.judgeThreads)
                       + ",\"codeGuardEnabled\":" + std::to_string(v.codeGuardEnabled)
                       + ",\"guardHeaders\":" + std::to_string(v.guardHeaders)
                       + ",\"guardProcess\":" + std::to_string(v.guardProcess)
                       + ",\"guardWinApi\":" + std::to_string(v.guardWinApi)
                       + ",\"guardNetwork\":" + std::to_string(v.guardNetwork)
                       + ",\"guardDll\":" + std::to_string(v.guardDll)
                       + ",\"guardFileFuncs\":" + std::to_string(v.guardFileFuncs)
                       + ",\"guardPathEscape\":" + std::to_string(v.guardPathEscape)
                       + ",\"guardCompilerExt\":" + std::to_string(v.guardCompilerExt)
                       + ",\"guardExtraFuncs\":\"" + common::jsonEscape(v.guardExtraFuncs) + "\""
                       + ",\"guardAllowFuncs\":\"" + common::jsonEscape(v.guardAllowFuncs) + "\""
                       + ",\"guardExtraHeaders\":\"" + common::jsonEscape(v.guardExtraHeaders) + "\""
                       + ",\"gppPath\":\"" + common::jsonEscape(v.gppPath) + "\"}";
    common::writeFile(OISEC_FILE, data);
}

// ========== Over-speed detection + auto-ban ==========
static std::map<std::string, std::deque<time_t> > g_hits;
static std::map<std::string, time_t> g_autoBanUntil; // ip -> expiry
static std::mutex g_rateMutex;

void pruneAutoBans() {
    time_t now = time(nullptr);
    for (auto it = g_autoBanUntil.begin(); it != g_autoBanUntil.end();) {
        if (now >= it->second) it = g_autoBanUntil.erase(it);
        else ++it;
    }
}

int gateRequest(const std::string& ip) {
    if (ip.empty()) return 0;
    {
        std::lock_guard<std::mutex> lock(g_banMutex);
        if (g_bannedIps.find(ip) != g_bannedIps.end()) return 403;
    }
    SecConfig cfg = getSecConfig();
    time_t now = time(nullptr);
    std::lock_guard<std::mutex> lock(g_rateMutex);
    pruneAutoBans();
    if (g_autoBanUntil.count(ip)) return 403;

    auto& dq = g_hits[ip];
    dq.erase(std::remove_if(dq.begin(), dq.end(),
        [&](time_t t){ return now - t >= cfg.windowSec; }), dq.end());

    // burst triggers auto-ban
    if ((int)dq.size() >= cfg.burstThreshold) {
        g_autoBanUntil[ip] = now + cfg.autoBanSec;
        dq.clear();
        return 403;
    }
    // per-second limit
    int cntLastSec = 0;
    for (auto& t : dq) if (now - t < 1) cntLastSec++;
    if (cntLastSec >= cfg.ratePerSec) return 429;

    dq.push_back(now);

    // prune memory to avoid unbounded growth
    if (g_hits.size() > 8000) {
        for (auto it = g_hits.begin(); it != g_hits.end();) {
            it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
                [&](time_t t){ return now - t >= cfg.windowSec; }), it->second.end());
            if (it->second.empty()) it = g_hits.erase(it);
            else ++it;
        }
    }
    return 0;
}

std::vector<FreqInfo> getFrequencyTop(int n) {
    SecConfig cfg = getSecConfig();
    time_t now = time(nullptr);
    std::vector<FreqInfo> out;
    {
        std::lock_guard<std::mutex> lock(g_rateMutex);
        for (auto& kv : g_hits) {
            int cnt = 0;
            for (auto& t : kv.second) if (now - t < cfg.windowSec) cnt++;
            if (cnt > 0) out.push_back({kv.first, cnt, cfg.windowSec > 0 ? cnt / cfg.windowSec : cnt});
        }
    }
    std::sort(out.begin(), out.end(),
        [](const FreqInfo& a, const FreqInfo& b){ return a.count > b.count; });
    if (n > 0 && (int)out.size() > n) out.resize(n);
    return out;
}

std::vector<BanInfo> getBansInfo() {
    std::vector<BanInfo> out;
    {
        std::lock_guard<std::mutex> lock(g_banMutex);
        for (const auto& ip : g_bannedIps) out.push_back({ip, false, 0});
    }
    time_t now = time(nullptr);
    {
        std::lock_guard<std::mutex> lock(g_rateMutex);
        pruneAutoBans();
        for (auto& kv : g_autoBanUntil) {
            out.push_back({kv.first, true, (long long)kv.second});
        }
    }
    return out;
}

// ========== Simple captcha ==========
static std::map<std::string, std::string> g_captcha; // token -> answer|created
static std::mutex g_captchaMutex;

static unsigned long long g_capCnt = 0;
static std::string createCaptchaEx(std::string& token, int difficulty) {
    srand((unsigned)(time(NULL) ^ ((unsigned long long)clock() << 32) ^ ((unsigned long long)rand() << 16) ^ (g_capCnt++)));
    int maxA = (difficulty >= 2) ? 99 : (difficulty == 1 ? 30 : 9);
    int a = rand() % maxA + 1;
    int b = rand() % maxA + 1;
    int op = rand() % (difficulty >= 2 ? 4 : 3); // 0:+ 1:- 2:* 3:/
    std::string question;
    int answer;
    if (op == 0) { question = std::to_string(a) + " + " + std::to_string(b); answer = a + b; }
    else if (op == 1) { if (a < b) std::swap(a, b); question = std::to_string(a) + " - " + std::to_string(b); answer = a - b; }
    else if (op == 2) { a = a % 9 + 1; b = b % 9 + 1; question = std::to_string(a) + " x " + std::to_string(b); answer = a * b; }
    else { int q2 = a % 9 + 1; int dv = b % 9 + 1; question = std::to_string(q2 * dv) + " / " + std::to_string(dv); answer = q2; }

    const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    do {
        token.clear();
        for (int i = 0; i < 16; i++) token += chars[rand() % 36];
    } while (g_captcha.count(token));

    {
        std::lock_guard<std::mutex> lock(g_captchaMutex);
        g_captcha[token] = std::to_string(answer) + "|" + std::to_string((long long)time(NULL));
        if (g_captcha.size() > 800) {
            auto it = g_captcha.begin();
            std::advance(it, 200);
            g_captcha.erase(g_captcha.begin(), it);
        }
    }
    return question + " = ?";
}

std::string createCaptcha(std::string& token) {
    return createCaptchaEx(token, getSecConfig().captchaStrict);
}

int assessRisk(const std::map<std::string, std::string>& headers) {
    int risk = 10;
    std::string ua, accept, lang, cookie;
    for (auto& kv : headers) {
        if (kv.first == "user-agent") ua = kv.second;
        else if (kv.first == "accept") accept = kv.second;
        else if (kv.first == "accept-language") lang = kv.second;
        else if (kv.first == "cookie") cookie = kv.second;
    }
    if (ua.find("Mozilla") == std::string::npos) risk += 35; else risk += 5;
    if (accept.find("text/html") == std::string::npos && accept.find("application/xhtml") == std::string::npos) risk += 20;
    if (lang.empty()) risk += 10;
    if (cookie.empty()) risk += 15;
    if (risk > 100) risk = 100;
    return risk;
}

bool verifyCaptcha(const std::string& token, const std::string& answer) {
    SecConfig cfg = getSecConfig();
    if (!cfg.captchaEnabled) return true; // captcha disabled by admin
    time_t now = time(nullptr);
    std::lock_guard<std::mutex> lock(g_captchaMutex);
    auto it = g_captcha.find(token);
    if (it == g_captcha.end()) return false;
    std::string stored = it->second;
    g_captcha.erase(it); // single-use

    size_t pipe = stored.find('|');
    if (pipe == std::string::npos) return false;
    std::string right = stored.substr(0, pipe);
    long long created = 0;
    try { created = std::stoll(stored.substr(pipe + 1)); } catch (...) { return false; }
    if (now - created > cfg.captchaExpireSec) return false;
    if (now - created < cfg.captchaMinSec) return false;
    return common::trim(answer) == right;
}

bool captchaFor(const std::string& feature) {
    SecConfig c = getSecConfig();
    if (!c.captchaEnabled) return false;
    if (feature == "login") return c.capLogin != 0;
    if (feature == "register") return c.capRegister != 0;
    if (feature == "submit") return c.capSubmit != 0;
    if (feature == "blog") return c.capBlog != 0;
    if (feature == "discuss") return c.capDiscuss != 0;
    if (feature == "task") return c.capTask != 0;
    if (feature == "shop") return c.capShop != 0;
    if (feature == "message") return c.capMessage != 0;
    if (feature == "group") return c.capGroup != 0;
    if (feature == "benben") return c.capBenben != 0;
    return true;
}

std::string captchaFieldsFor(const std::string& feature, const std::map<std::string, std::string>& headers) {
    if (!captchaFor(feature)) return "";
    return headers.empty() ? captchaFields() : captchaFields(headers);
}

bool verifyCaptchaFor(const std::string& token, const std::string& answer, const std::string& feature) {
    if (!captchaFor(feature)) return true;
    return verifyCaptcha(token, answer);
}

std::string captchaJsonFor(const std::string& feature) {
    if (!captchaFor(feature)) return "{\"required\":false,\"token\":\"\",\"question\":\"\"}";
    std::string token;
    std::string q = createCaptcha(token);
    std::map<std::string, std::string> m;
    m["required"] = "true";
    m["token"] = token;
    m["question"] = q;
    return common::jsonStringify(m);
}

std::string captchaFields() {
    if (!getSecConfig().captchaEnabled) return "";
    std::string token;
    std::string q = createCaptcha(token);
    return "<input type='hidden' name='captcha_token' value='" + token + "'/>"
           "<div class='form-group'><label>\u9a8c\u8bc1\u7801\uff1a" + common::htmlEscape(q) + "</label>"
           "<input name='captcha_answer' required/></div>";
}

std::string captchaFields(const std::map<std::string, std::string>& headers) {
    if (!getSecConfig().captchaEnabled) return "";
    int cfgStrict = getSecConfig().captchaStrict;
    int risk = assessRisk(headers);
    int diff = cfgStrict;
    if (risk >= 60) diff = (cfgStrict < 2) ? cfgStrict + 1 : 2;
    std::string token;
    std::string q = createCaptchaEx(token, diff);
    return "<input type='hidden' name='captcha_token' value='" + token + "'/>"
           "<div class='form-group'><label>\u9a8c\u8bc1\u7801\uff1a" + common::htmlEscape(q) + "</label>"
           "<input name='captcha_answer' required/></div>";
}

// ---------- Per-action throttle ----------
static std::map<std::string, long long> g_actionLast;
static std::mutex g_actionMutex;

bool throttleAction(const std::string& key, int minSec) {
    if (minSec <= 0) return false; // disabled
    long long now = (long long)time(nullptr);
    std::lock_guard<std::mutex> lock(g_actionMutex);
    auto it = g_actionLast.find(key);
    if (it != g_actionLast.end() && (now - it->second) < (long long)minSec) {
        return true; // blocked (too soon)
    }
    g_actionLast[key] = now;
    return false;
}

// ---------- IP user activity ----------
static std::map<std::string, std::vector<IpActivity>> g_ipActivity;
static std::mutex g_ipActMutex;
static const int IP_ACTIVITY_CAP = 20; // max records kept per IP

void recordIpActivity(const std::string& ip, const std::string& username) {
    if (ip.empty() || username.empty()) return;
    long long now = (long long)time(nullptr);
    std::lock_guard<std::mutex> lock(g_ipActMutex);
    auto& v = g_ipActivity[ip];
    // remove existing entry for this user (keep most recent time only)
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->username == username) { v.erase(it); break; }
    }
    v.push_back({username, now});
    if ((int)v.size() > IP_ACTIVITY_CAP) v.erase(v.begin(), v.end() - IP_ACTIVITY_CAP);
}

std::vector<IpActivity> ipActivity(const std::string& ip, int maxN) {
    std::lock_guard<std::mutex> lock(g_ipActMutex);
    std::vector<IpActivity> out;
    auto it = g_ipActivity.find(ip);
    if (it == g_ipActivity.end()) return out;
    // most recent last in storage -> reverse
    for (auto r = it->second.rbegin(); r != it->second.rend(); ++r) out.push_back(*r);
    if (maxN > 0 && (int)out.size() > maxN) out.resize(maxN);
    return out;
}

}

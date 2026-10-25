#include "../include/models.h"
#include "../include/ai_quota.h"
#include "../include/common.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <set>
#include <map>

// --------------------------      --------------------------
namespace {
// 从 JSON 行提取 id 字段值
std::string extractJsonId(const std::string& line) {
    size_t p = line.find("\"id\":\"");
    if (p == std::string::npos) return "";
    p += 6; // 跳过 \"id\":\"
    size_t e = line.find('"', p);
    if (e == std::string::npos) return "";
    return line.substr(p, e - p);
}
// 记录每个文件上次写入的 id 集合（区分“本进程已写过的旧条目”和“其它进程新增的条目”）
std::map<std::string, std::set<std::string> > g_lastWrite;
// 合并写入：先扫描磁盘上已有数据，保留“本进程上次写入之后、其它进程新增”的条目，避免互相覆盖丢失
std::string mergeWithDisk(const std::string& filePath, const std::vector<std::string>& memLines) {
    std::set<std::string>& lastWrite = g_lastWrite[filePath];
    std::set<std::string> memIds;
    std::string out;
    for (size_t i = 0; i < memLines.size(); ++i) {
        std::string id = extractJsonId(memLines[i]);
        if (!id.empty()) memIds.insert(id);
        out += memLines[i] + "\n";
    }
    std::string disk = common::readFile(filePath);
    std::istringstream ss(disk);
    std::string line;
    while (std::getline(ss, line)) {
        if (common::trim(line).empty()) continue;
        std::string id = extractJsonId(line);
        if (!id.empty() && memIds.count(id) == 0 && lastWrite.count(id) == 0) {
            out += line + "\n"; // 其它进程新增的数据，保留追加
        }
    }
    lastWrite = memIds;
    return out;
}
}

DataStore& DataStore::instance() {
    static DataStore ds;
    return ds;
}

std::string DataStore::getDataDir() const {
    return common::getExeDir() + "data\\";
}

// ============================================================
// JSON    л /     л 
// ============================================================

// ---------- User ----------
std::string DataStore::userToJson(const User& u) {
    std::map<std::string, std::string> m;
    m["id"] = u.id;
    m["username"] = u.username;
    m["password"] = u.password;
    m["role"] = std::to_string(u.role);
    m["registerTime"] = u.registerTime;
    m["checkinDays"] = std::to_string(u.checkinDays);
    m["lastCheckin"] = u.lastCheckin;
    m["avatar"] = u.avatar;
    m["bio"] = u.bio;
    m["rating"] = std::to_string(u.rating);
    m["maxRating"] = std::to_string(u.maxRating);
    m["ratedMatches"] = std::to_string(u.ratedMatches);
    m["tokens"] = std::to_string(u.tokens);
    m["experience"] = std::to_string(u.experience);
    m["gold"] = std::to_string(u.gold);
    m["aiWindowQuota"] = std::to_string(u.aiWindowQuota);
    m["aiMonthQuota"] = std::to_string(u.aiMonthQuota);
    m["aiPlanId"] = u.aiPlanId;
    m["adminPerms"] = common::jsonStringify(u.adminPerms);
    m["codePublic"] = u.codePublic ? "1" : "0";
    m["effCoeff"] = std::to_string(u.effCoeff);
    m["driveMB"] = std::to_string(u.driveMB);
    m["driveBonusMB"] = std::to_string(u.driveBonusMB);
    m["driveBonusUntil"] = u.driveBonusUntil;
    m["coeffBonusPct"] = std::to_string(u.coeffBonusPct);
    m["coeffUntil"] = u.coeffUntil;
    m["renameCards"] = std::to_string(u.renameCards);
    m["contacts"] = common::jsonStringify(u.contacts);
    m["ratingHistory"] = common::jsonStringify(u.ratingHistory);
    return common::jsonStringify(m);
}

User DataStore::userFromJson(const std::string& json) {
    User u;
    auto obj = common::jsonParseObject(json);
    u.id = obj.count("id") ? obj["id"] : "";
    u.username = obj.count("username") ? obj["username"] : "";
    u.password = obj.count("password") ? obj["password"] : "";
    u.role = obj.count("role") ? (UserRole)common::stringToInt(obj["role"]) : ROLE_USER;
    u.registerTime = obj.count("registerTime") ? obj["registerTime"] : "";
    u.checkinDays = obj.count("checkinDays") ? common::stringToInt(obj["checkinDays"]) : 0;
    u.lastCheckin = obj.count("lastCheckin") ? obj["lastCheckin"] : "";
    u.avatar = obj.count("avatar") ? obj["avatar"] : "";
    u.bio = obj.count("bio") ? obj["bio"] : "    ˺     ʲô  ûд";
    u.rating = obj.count("rating") ? common::stringToInt(obj["rating"]) : 1500;
    u.maxRating = obj.count("maxRating") ? common::stringToInt(obj["maxRating"]) : 1500;
    u.ratedMatches = obj.count("ratedMatches") ? common::stringToInt(obj["ratedMatches"]) : 0;
    u.tokens = obj.count("tokens") ? common::stringToInt(obj["tokens"]) : 0;
    u.experience = obj.count("experience") ? common::stringToInt(obj["experience"]) : 0;
    u.aiWindowQuota = obj.count("aiWindowQuota") ? common::stringToLongLong(obj["aiWindowQuota"]) : 0;
    u.aiMonthQuota = obj.count("aiMonthQuota") ? common::stringToLongLong(obj["aiMonthQuota"]) : 0;
    if (obj.count("aiPlanId")) u.aiPlanId = obj["aiPlanId"];
    if (obj.count("adminPerms")) u.adminPerms = common::jsonParseArray(obj["adminPerms"]);
    // 老用户没有该字段：默认公开，保持原有行为
    u.codePublic = obj.count("codePublic") ? (obj["codePublic"] == "1" || obj["codePublic"] == "true") : true;
    u.gold = obj.count("gold") ? common::stringToLongLong(obj["gold"]) : 100;
    if (u.gold < 0) u.gold = 0;
    if (u.gold > 9000000000000LL) u.gold = 9000000000000LL;
    u.effCoeff = obj.count("effCoeff") ? common::stringToInt(obj["effCoeff"]) : 100;
    u.driveMB = obj.count("driveMB") ? common::stringToInt(obj["driveMB"]) : 0;
    u.driveBonusMB = obj.count("driveBonusMB") ? common::stringToInt(obj["driveBonusMB"]) : 0;
    u.driveBonusUntil = obj.count("driveBonusUntil") ? obj["driveBonusUntil"] : "";
    u.coeffBonusPct = obj.count("coeffBonusPct") ? common::stringToInt(obj["coeffBonusPct"]) : 0;
    u.coeffUntil = obj.count("coeffUntil") ? obj["coeffUntil"] : "";
    u.renameCards = obj.count("renameCards") ? common::stringToInt(obj["renameCards"]) : 0;
    if (obj.count("contacts")) u.contacts = common::jsonParseArray(obj["contacts"]);
    if (obj.count("ratingHistory")) u.ratingHistory = common::jsonParseArray(obj["ratingHistory"]);
    return u;
}

// ---------- Problem ----------
std::string DataStore::problemToJson(const Problem& p) {
    std::map<std::string, std::string> m;
    m["id"] = p.id;
    m["title"] = p.title;
    m["description"] = p.description;
    m["inputDesc"] = p.inputDesc;
    m["outputDesc"] = p.outputDesc;
    m["samples"] = common::jsonStringify(p.samples);
    m["timeLimit"] = std::to_string(p.timeLimit);
    m["memLimit"] = std::to_string(p.memLimit);
    m["author"] = p.author;
    m["createTime"] = p.createTime;
    m["acCount"] = std::to_string(p.acCount);
    m["submitCount"] = std::to_string(p.submitCount);
    m["hidden"] = p.hidden ? "1" : "0";
    m["image"] = p.image;
    m["standardSolution"] = p.standardSolution;
    m["solutionClosed"] = p.solutionClosed ? "1" : "0";
    m["spj"] = p.spj;
    m["dataRange"] = p.dataRange;
    m["generator"] = p.generator;
    m["subtaskMode"] = p.subtaskMode ? "1" : "0";
    {
        std::string arr;
        for (size_t i = 0; i < p.subtasks.size(); i++) {
            if (i) arr += ",";
            arr += "{\"name\":" + common::jsonStringify(p.subtasks[i].name) +
                   ",\"score\":" + std::to_string(p.subtasks[i].score) +
                   ",\"tests\":" + common::jsonStringify(p.subtasks[i].tests) + "}";
        }
        m["subtasks"] = "[" + arr + "]";
    }
    m["comments"] = common::jsonStringify(p.comments);
    return common::jsonStringify(m);
}

Problem DataStore::problemFromJson(const std::string& json) {
    Problem p;
    auto obj = common::jsonParseObject(json);
    p.id = obj.count("id") ? obj["id"] : "";
    p.title = obj.count("title") ? obj["title"] : "";
    p.description = obj.count("description") ? obj["description"] : "";
    p.inputDesc = obj.count("inputDesc") ? obj["inputDesc"] : "";
    p.outputDesc = obj.count("outputDesc") ? obj["outputDesc"] : "";
    if (obj.count("samples")) {
        p.samples = common::jsonParseArray(obj["samples"]);
    }
    p.timeLimit = obj.count("timeLimit") ? common::stringToInt(obj["timeLimit"]) : 1000;
    p.memLimit = obj.count("memLimit") ? common::stringToInt(obj["memLimit"]) : 256;
    p.author = obj.count("author") ? obj["author"] : "";
    p.createTime = obj.count("createTime") ? obj["createTime"] : "";
    p.acCount = obj.count("acCount") ? common::stringToInt(obj["acCount"]) : 0;
    p.submitCount = obj.count("submitCount") ? common::stringToInt(obj["submitCount"]) : 0;
    p.hidden = obj.count("hidden") ? (obj["hidden"] == "1") : false;
    p.image = obj.count("image") ? obj["image"] : "";
    p.standardSolution = obj.count("standardSolution") ? obj["standardSolution"] : "";
    p.solutionClosed = obj.count("solutionClosed") ? (obj["solutionClosed"] == "1") : false;
    p.spj = obj.count("spj") ? obj["spj"] : "";
    p.dataRange = obj.count("dataRange") ? obj["dataRange"] : "";
    p.generator = obj.count("generator") ? obj["generator"] : "";
    p.subtaskMode = obj.count("subtaskMode") ? (obj["subtaskMode"] == "1") : false;
    if (obj.count("subtasks") && !obj["subtasks"].empty()) {
        std::vector<std::string> items = common::jsonParseArray(obj["subtasks"]);
        for (size_t i = 0; i < items.size(); i++) {
            std::map<std::string, std::string> sm = common::jsonParseObject(items[i]);
            if (sm.empty()) continue;
            Subtask st;
            st.name = sm.count("name") ? sm["name"] : ("子任务 " + std::to_string(i + 1));
            st.score = sm.count("score") ? common::stringToInt(sm["score"]) : 0;
            if (sm.count("tests")) st.tests = common::jsonParseArray(sm["tests"]);
            if (!st.tests.empty() || st.score > 0) p.subtasks.push_back(st);
        }
    }
    if (obj.count("comments")) p.comments = common::jsonParseArray(obj["comments"]);
    return p;
}

// ---------- Submission ----------
std::string DataStore::submissionToJson(const Submission& s) {
    std::map<std::string, std::string> m;
    m["id"] = s.id;
    m["problemId"] = s.problemId;
    m["userId"] = s.userId;
    m["code"] = s.code;
    m["language"] = s.language;
    m["status"] = std::to_string(s.status);
    m["usedTime"] = std::to_string(s.usedTime);
    m["usedMem"] = std::to_string(s.usedMem);
    m["submitTime"] = s.submitTime;
    m["judgeResult"] = s.judgeResult;
    m["hacked"] = s.hacked ? "1" : "0";
    m["hackedBy"] = s.hackedBy;
    m["hackInput"] = s.hackInput;
    m["hackTime"] = s.hackTime;
    m["locked"] = s.locked ? "1" : "0";
    m["contestId"] = s.contestId;
    m["score"] = std::to_string(s.score);
    return common::jsonStringify(m);
}

Submission DataStore::submissionFromJson(const std::string& json) {
    Submission s;
    auto obj = common::jsonParseObject(json);
    s.id = obj.count("id") ? obj["id"] : "";
    s.problemId = obj.count("problemId") ? obj["problemId"] : "";
    s.userId = obj.count("userId") ? obj["userId"] : "";
    s.code = obj.count("code") ? obj["code"] : "";
    s.language = obj.count("language") ? obj["language"] : "cpp";
    s.status = obj.count("status") ? (SubmitStatus)common::stringToInt(obj["status"]) : PENDING;
    s.usedTime = obj.count("usedTime") ? common::stringToInt(obj["usedTime"]) : 0;
    s.usedMem = obj.count("usedMem") ? common::stringToInt(obj["usedMem"]) : 0;
    s.submitTime = obj.count("submitTime") ? obj["submitTime"] : "";
    s.judgeResult = obj.count("judgeResult") ? obj["judgeResult"] : "";
    s.hacked = obj.count("hacked") ? (obj["hacked"] == "1") : false;
    s.hackedBy = obj.count("hackedBy") ? obj["hackedBy"] : "";
    s.hackInput = obj.count("hackInput") ? obj["hackInput"] : "";
    s.hackTime = obj.count("hackTime") ? obj["hackTime"] : "";
    s.locked = obj.count("locked") ? (obj["locked"] == "1") : false;
    s.contestId = obj.count("contestId") ? obj["contestId"] : "";
    s.score = obj.count("score") ? common::stringToInt(obj["score"]) : 0;
    return s;
}

// ---------- Post ----------
std::string DataStore::postToJson(const Post& p) {
    std::map<std::string, std::string> m;
    m["id"] = p.id;
    m["authorId"] = p.authorId;
    m["type"] = std::to_string(p.type);
    m["title"] = p.title;
    m["content"] = p.content;
    m["createTime"] = p.createTime;
    m["views"] = std::to_string(p.views);
    m["likes"] = std::to_string(p.likes);
    m["problemId"] = p.problemId;
    m["auditStatus"] = p.auditStatus;
    m["rejectReason"] = p.rejectReason;
    m["reviewerId"] = p.reviewerId;
    m["auditTime"] = p.auditTime;
    m["rewardPaid"] = p.rewardPaid ? "1" : "0";
    m["board"] = p.board;
    m["tags"] = common::jsonStringify(p.tags);
    m["pinned"] = p.pinned ? "1" : "0";
    m["essence"] = p.essence ? "1" : "0";
    m["locked"] = p.locked ? "1" : "0";
    m["lastReplyTime"] = p.lastReplyTime;
    m["favoritedBy"] = common::jsonStringify(p.favoritedBy);
    m["editTime"] = p.editTime;
    std::vector<std::string> commentStrs;
    for (const auto& c : p.comments) {
        std::map<std::string, std::string> cm;
        cm["id"] = c.id;
        cm["authorId"] = c.authorId;
        cm["content"] = c.content;
        cm["createTime"] = c.createTime;
        cm["parentId"] = c.parentId;
        cm["likedBy"] = common::jsonStringify(c.likedBy);
        commentStrs.push_back(common::jsonStringify(cm));
    }
    m["comments"] = common::jsonStringify(commentStrs);
    m["likedBy"] = common::jsonStringify(p.likedBy);
    m["pollTitle"] = p.pollTitle;
    m["pollOptions"] = common::jsonStringify(p.pollOptions);
    std::vector<std::string> pv;
    for (const auto& op : p.pollVotes) pv.push_back(common::jsonStringify(op));
    m["pollVotes"] = common::jsonStringify(pv);
    return common::jsonStringify(m);
}

Post DataStore::postFromJson(const std::string& json) {
    Post p;
    auto obj = common::jsonParseObject(json);
    p.id = obj.count("id") ? obj["id"] : "";
    p.authorId = obj.count("authorId") ? obj["authorId"] : "";
    p.type = obj.count("type") ? (PostType)common::stringToInt(obj["type"]) : BLOG;
    p.title = obj.count("title") ? obj["title"] : "";
    p.content = obj.count("content") ? obj["content"] : "";
    p.createTime = obj.count("createTime") ? obj["createTime"] : "";
    p.views = obj.count("views") ? common::stringToInt(obj["views"]) : 0;
    p.likes = obj.count("likes") ? common::stringToInt(obj["likes"]) : 0;
    p.problemId = obj.count("problemId") ? obj["problemId"] : "";
    p.auditStatus = obj.count("auditStatus") ? obj["auditStatus"] : "";
    p.rejectReason = obj.count("rejectReason") ? obj["rejectReason"] : "";
    p.reviewerId = obj.count("reviewerId") ? obj["reviewerId"] : "";
    p.auditTime = obj.count("auditTime") ? obj["auditTime"] : "";
    p.rewardPaid = obj.count("rewardPaid") ? (obj["rewardPaid"] == "1") : false;
    p.board = obj.count("board") && !obj["board"].empty() ? obj["board"] : "academic";
    if (obj.count("tags")) p.tags = common::jsonParseArray(obj["tags"]);
    p.pinned = obj.count("pinned") ? (obj["pinned"] == "1") : false;
    p.essence = obj.count("essence") ? (obj["essence"] == "1") : false;
    p.locked = obj.count("locked") ? (obj["locked"] == "1") : false;
    p.lastReplyTime = obj.count("lastReplyTime") ? obj["lastReplyTime"] : "";
    if (obj.count("favoritedBy")) p.favoritedBy = common::jsonParseArray(obj["favoritedBy"]);
    p.editTime = obj.count("editTime") ? obj["editTime"] : "";
    if (obj.count("likedBy")) p.likedBy = common::jsonParseArray(obj["likedBy"]);
    p.pollTitle = obj.count("pollTitle") ? obj["pollTitle"] : "";
    if (obj.count("pollOptions")) p.pollOptions = common::jsonParseArray(obj["pollOptions"]);
    if (obj.count("pollVotes")) {
        auto pvArr = common::jsonParseArray(obj["pollVotes"]);
        for (const auto& item : pvArr) p.pollVotes.push_back(common::jsonParseArray(item));
    }
    if (obj.count("comments")) {
        auto commentStrs = common::jsonParseArray(obj["comments"]);
        for (const auto& item : commentStrs) {
            auto cm = common::jsonParseObject(item);
            if (!cm.empty()) {
                Comment c;
                c.id = cm.count("id") ? cm["id"] : "";
                c.authorId = cm.count("authorId") ? cm["authorId"] : "";
                c.content = cm.count("content") ? cm["content"] : "";
                c.createTime = cm.count("createTime") ? cm["createTime"] : "";
                c.parentId = cm.count("parentId") ? cm["parentId"] : "";
                if (cm.count("likedBy")) c.likedBy = common::jsonParseArray(cm["likedBy"]);
                p.comments.push_back(c);
            }
        }
    }
    return p;
}

// ---------- Contest ----------
std::string DataStore::contestToJson(const Contest& c) {
    std::map<std::string, std::string> m;
    m["id"] = c.id;
    m["title"] = c.title;
    m["description"] = c.description;
    m["startTime"] = c.startTime;
    m["endTime"] = c.endTime;
    m["isRated"] = c.isRated ? "1" : "0";
    m["isFinished"] = c.isFinished ? "1" : "0";
    m["format"] = c.format;
    m["isTeam"] = c.isTeam ? "1" : "0";

    std::vector<std::string> teamStrs;
    for (const auto& t : c.teams) {
        std::map<std::string, std::string> tm;
        tm["id"] = t.id;
        tm["name"] = t.name;
        tm["leaderId"] = t.leaderId;
        tm["memberIds"] = common::jsonStringify(t.memberIds);
        tm["pendingInvites"] = common::jsonStringify(t.pendingInvites);
        tm["pendingApplications"] = common::jsonStringify(t.pendingApplications);
        teamStrs.push_back(common::jsonStringify(tm));
    }
    m["teams"] = common::jsonStringify(teamStrs);

    std::vector<std::string> commentStrs;
    for (const auto& cm : c.comments) {
        std::map<std::string, std::string> cmm;
        cmm["userId"] = cm.userId;
        cmm["username"] = cm.username;
        cmm["time"] = cm.time;
        cmm["content"] = cm.content;
        commentStrs.push_back(common::jsonStringify(cmm));
    }
    m["comments"] = common::jsonStringify(commentStrs);

    std::vector<std::string> problemStrs;
    for (const auto& p : c.problems) {
        std::map<std::string, std::string> pm;
        pm["problemId"] = p.problemId;
        pm["score"] = std::to_string(p.score);
        problemStrs.push_back(common::jsonStringify(pm));
    }
    m["problems"] = common::jsonStringify(problemStrs);
    m["participants"] = common::jsonStringify(c.participants);
    m["registrants"] = common::jsonStringify(c.registrants);

    std::vector<std::string> statusStrs;
    for (const auto& u : c.userProblemStatus) {
        for (const auto& p : u.second) {
            std::map<std::string, std::string> sm;
            sm["userId"] = u.first;
            sm["problemId"] = p.first;
            sm["status"] = std::to_string(p.second);
            statusStrs.push_back(common::jsonStringify(sm));
        }
    }
    m["userProblemStatus"] = common::jsonStringify(statusStrs);

    std::vector<std::string> changeStrs;
    for (const auto& kv : c.ratingChanges) {
        std::map<std::string, std::string> rm;
        rm["userId"] = kv.first;
        rm["delta"] = std::to_string(kv.second);
        changeStrs.push_back(common::jsonStringify(rm));
    }
    m["ratingChanges"] = common::jsonStringify(changeStrs);

    return common::jsonStringify(m);
}

Contest DataStore::contestFromJson(const std::string& json) {
    Contest c;
    auto obj = common::jsonParseObject(json);
    c.id = obj.count("id") ? obj["id"] : "";
    c.title = obj.count("title") ? obj["title"] : "";
    c.description = obj.count("description") ? obj["description"] : "";
    c.startTime = obj.count("startTime") ? obj["startTime"] : "";
    c.endTime = obj.count("endTime") ? obj["endTime"] : "";
    c.isRated = obj.count("isRated") ? (obj["isRated"] == "1") : false;
    c.isFinished = obj.count("isFinished") ? (obj["isFinished"] == "1") : false;
    c.format = obj.count("format") ? obj["format"] : "icpc";
    c.isTeam = obj.count("isTeam") ? (obj["isTeam"] == "1") : false;

    if (obj.count("teams")) {
        auto teamStrs = common::jsonParseArray(obj["teams"]);
        for (const auto& item : teamStrs) {
            auto tm = common::jsonParseObject(item);
            if (tm.empty()) continue;
            Team t;
            t.id = tm.count("id") ? tm["id"] : "";
            t.name = tm.count("name") ? tm["name"] : "";
            t.leaderId = tm.count("leaderId") ? tm["leaderId"] : "";
            if (tm.count("memberIds")) t.memberIds = common::jsonParseArray(tm["memberIds"]);
            if (tm.count("pendingInvites")) t.pendingInvites = common::jsonParseArray(tm["pendingInvites"]);
            if (tm.count("pendingApplications")) t.pendingApplications = common::jsonParseArray(tm["pendingApplications"]);
            c.teams.push_back(t);
        }
    }

    if (obj.count("comments")) {
        auto commentStrs = common::jsonParseArray(obj["comments"]);
        for (const auto& item : commentStrs) {
            auto cmm = common::jsonParseObject(item);
            if (cmm.empty()) continue;
            Contest::ContestComment cm;
            cm.userId = cmm.count("userId") ? cmm["userId"] : "";
            cm.username = cmm.count("username") ? cmm["username"] : "";
            cm.time = cmm.count("time") ? cmm["time"] : "";
            cm.content = cmm.count("content") ? cmm["content"] : "";
            c.comments.push_back(cm);
        }
    }

    if (obj.count("problems")) {
        auto problemStrs = common::jsonParseArray(obj["problems"]);
        for (const auto& item : problemStrs) {
            auto pm = common::jsonParseObject(item);
            if (!pm.empty()) {
                ContestProblem cp;
                cp.problemId = pm.count("problemId") ? pm["problemId"] : "";
                cp.score = pm.count("score") ? common::stringToInt(pm["score"]) : 0;
                c.problems.push_back(cp);
            }
        }
    }
    if (obj.count("participants")) {
        c.participants = common::jsonParseArray(obj["participants"]);
    }
    if (obj.count("registrants")) {
        c.registrants = common::jsonParseArray(obj["registrants"]);
    }

    if (obj.count("userProblemStatus")) {
        auto statusStrs = common::jsonParseArray(obj["userProblemStatus"]);
        for (const auto& item : statusStrs) {
            auto sm = common::jsonParseObject(item);
            if (!sm.empty()) {
                std::string userId = sm.count("userId") ? sm["userId"] : "";
                std::string problemId = sm.count("problemId") ? sm["problemId"] : "";
                int status = sm.count("status") ? common::stringToInt(sm["status"]) : 0;
                if (!userId.empty() && !problemId.empty()) {
                    c.userProblemStatus[userId][problemId] = (SubmitStatus)status;
                }
            }
        }
    }

    if (obj.count("ratingChanges")) {
        auto changeStrs = common::jsonParseArray(obj["ratingChanges"]);
        for (const auto& item : changeStrs) {
            auto rm = common::jsonParseObject(item);
            if (!rm.empty()) {
                std::string userId = rm.count("userId") ? rm["userId"] : "";
                int delta = rm.count("delta") ? common::stringToInt(rm["delta"]) : 0;
                if (!userId.empty()) {
                    c.ratingChanges[userId] = delta;
                }
            }
        }
    }
    return c;
}

// ---------- ProblemSet ----------
std::string DataStore::problemSetToJson(const ProblemSet& ps) {
    std::map<std::string, std::string> m;
    m["id"] = ps.id;
    m["name"] = ps.name;
    m["description"] = ps.description;
    m["authorId"] = ps.authorId;
    m["problemIds"] = common::jsonStringify(ps.problemIds);
    m["createTime"] = ps.createTime;
    return common::jsonStringify(m);
}

ProblemSet DataStore::problemSetFromJson(const std::string& json) {
    ProblemSet ps;
    auto obj = common::jsonParseObject(json);
    ps.id = obj.count("id") ? obj["id"] : "";
    ps.name = obj.count("name") ? obj["name"] : "";
    ps.description = obj.count("description") ? obj["description"] : "";
    ps.authorId = obj.count("authorId") ? obj["authorId"] : "";
    if (obj.count("problemIds")) {
        ps.problemIds = common::jsonParseArray(obj["problemIds"]);
    }
    ps.createTime = obj.count("createTime") ? obj["createTime"] : "";
    return ps;
}

// ---------- Message ----------
std::string DataStore::messageToJson(const Message& m) {
    std::map<std::string, std::string> obj;
    obj["id"] = m.id;
    obj["fromId"] = m.fromId;
    obj["toId"] = m.toId;
    obj["content"] = m.content;
    obj["sendTime"] = m.sendTime;
    obj["read"] = m.read ? "1" : "0";
    obj["readBy"] = common::jsonStringify(m.readBy);
    if (!m.file.empty()) {
        obj["file"] = m.file;
        obj["fileName"] = m.fileName;
        obj["fileSize"] = std::to_string(m.fileSize);
    }
    return common::jsonStringify(obj);
}

Message DataStore::messageFromJson(const std::string& json) {
    Message m;
    auto obj = common::jsonParseObject(json);
    m.id = obj.count("id") ? obj["id"] : "";
    m.fromId = obj.count("fromId") ? obj["fromId"] : "";
    m.toId = obj.count("toId") ? obj["toId"] : "";
    m.content = obj.count("content") ? obj["content"] : "";
    m.sendTime = obj.count("sendTime") ? obj["sendTime"] : "";
    m.read = obj.count("read") ? (obj["read"] == "1") : false;
    if (obj.count("readBy")) m.readBy = common::jsonParseArray(obj["readBy"]);
    if (obj.count("file")) m.file = obj["file"];
    if (obj.count("fileName")) m.fileName = obj["fileName"];
    if (obj.count("fileSize")) { try { m.fileSize = std::stoll(obj["fileSize"]); } catch (...) { m.fileSize = 0; } }
    return m;
}

// ---------- Announcement ----------
std::string DataStore::announcementToJson(const std::string& content) {
    std::map<std::string, std::string> m;
    m["content"] = content;
    return common::jsonStringify(m);
}

std::string DataStore::announcementFromJson(const std::string& json) {
    auto obj = common::jsonParseObject(json);
    return obj.count("content") ? obj["content"] : "";
}

// ============================================================
//    溯  
// ============================================================
void DataStore::saveUsers() {
    std::vector<std::string> _l;
    for (const auto& u : users) _l.push_back(userToJson(u));
    common::lockData();
    common::writeFile(getDataDir() + "users.json", mergeWithDisk(getDataDir() + "users.json", _l));
    common::unlockData();
}

void DataStore::saveProblems() {
    std::vector<std::string> _l;
    for (const auto& p : problems) _l.push_back(problemToJson(p));
    common::lockData();
    common::writeFile(getDataDir() + "problems.json", mergeWithDisk(getDataDir() + "problems.json", _l));
    common::unlockData();
}

void DataStore::saveSubmissions() {
    std::vector<std::string> _l;
    for (const auto& s : submissions) _l.push_back(submissionToJson(s));
    common::lockData();
    common::writeFile(getDataDir() + "submissions.json", mergeWithDisk(getDataDir() + "submissions.json", _l));
    common::unlockData();
}

void DataStore::savePosts() {
    std::vector<std::string> _l;
    for (const auto& p : posts) _l.push_back(postToJson(p));
    common::lockData();
    common::writeFile(getDataDir() + "posts.json", mergeWithDisk(getDataDir() + "posts.json", _l));
    common::unlockData();
}

void DataStore::saveContests() {
    std::vector<std::string> _l;
    for (const auto& c : contests) _l.push_back(contestToJson(c));
    common::lockData();
    common::writeFile(getDataDir() + "contests.json", mergeWithDisk(getDataDir() + "contests.json", _l));
    common::unlockData();
}

void DataStore::saveProblemSets() {
    std::vector<std::string> _l;
    for (const auto& ps : problemSets) _l.push_back(problemSetToJson(ps));
    common::lockData();
    common::writeFile(getDataDir() + "problemsets.json", mergeWithDisk(getDataDir() + "problemsets.json", _l));
    common::unlockData();
}

void DataStore::saveMessages() {
    std::vector<std::string> _l;
    for (const auto& m : messages) _l.push_back(messageToJson(m));
    common::lockData();
    common::writeFile(getDataDir() + "messages.json", mergeWithDisk(getDataDir() + "messages.json", _l));
    common::unlockData();
}

void DataStore::saveAnnouncement() {
    common::writeFile(getDataDir() + "announcement.json", announcementToJson(announcement));
}

void DataStore::saveShops() {
    std::vector<std::string> _l;
    for (const auto& s : shops) _l.push_back(shopToJson(s));
    common::lockData();
    common::writeFile(getDataDir() + "shops.json", mergeWithDisk(getDataDir() + "shops.json", _l));
    common::unlockData();
}

void DataStore::saveLanguages() {
    std::vector<std::string> _l;
    for (const auto& l : languages) _l.push_back(langToJson(l));
    common::lockData();
    common::writeFile(getDataDir() + "languages.json", mergeWithDisk(getDataDir() + "languages.json", _l));
    common::unlockData();
}

void DataStore::saveAll() {
    saveUsers();
    saveProblems();
    saveSubmissions();
    savePosts();
    saveContests();
    saveProblemSets();
    saveMessages();
    saveGroups();
    saveAnnouncement();
    saveShops();
    saveLanguages();
    saveTasks();
    saveOrders();
    saveDuels();}

// ============================================================
//    غ   
// ============================================================
void DataStore::loadAll() {
    std::string dataDir = getDataDir();
    common::createDirectory(dataDir);

    // idempotent load: clear all collections first
        users.clear();
    problems.clear();
    submissions.clear();
    posts.clear();
    contests.clear();
    problemSets.clear();
    messages.clear();
    groups.clear();
    shops.clear();
    languages.clear();
    tasks.clear();
    orders.clear();
    duels.clear();
    papers.clear();
    paperSubs.clear();

    // 1.  û 
    std::string userFile = dataDir + "users.json";
    if (common::fileExists(userFile)) {
        std::string content = common::readFile(userFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                users.push_back(userFromJson(line));
            }
        }
    }
    if (users.empty()) {
        User admin;
        admin.id = "1";
        admin.username = "admin";
        admin.password = common::passwordHash("admin123");
        admin.role = ROLE_ADMIN;
        admin.registerTime = common::getTimeStr();
        admin.checkinDays = 0;
        admin.lastCheckin = "";
        admin.avatar = "";
        admin.bio = "ϵͳ    Ա";
        admin.rating = 1500;
        admin.maxRating = 1500;
        admin.ratedMatches = 0;
        admin.tokens = 100;
        admin.experience = 0;
        admin.gold = 1000;
        users.push_back(admin);
        saveUsers();
    }

    // 明文密码一次性迁移为加盐哈希（幂等）
    {
        bool pwdDirty = false;
        for (size_t i = 0; i < users.size(); i++) {
            if (users[i].password.empty()) continue;
            if (common::passwordIsHashed(users[i].password)) continue;
            users[i].password = common::passwordHash(users[i].password);
            pwdDirty = true;
        }
        if (pwdDirty) saveUsers();
    }

    // 2.   Ŀ
    std::string problemFile = dataDir + "problems.json";
    if (common::fileExists(problemFile)) {
        std::string content = common::readFile(problemFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                problems.push_back(problemFromJson(line));
            }
        }
    }

    // 3.  ύ  ¼
    std::string subFile = dataDir + "submissions.json";
    if (common::fileExists(subFile)) {
        std::string content = common::readFile(subFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                submissions.push_back(submissionFromJson(line));
            }
        }
    }

    // 4.     
    std::string postFile = dataDir + "posts.json";
    if (common::fileExists(postFile)) {
        std::string content = common::readFile(postFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                posts.push_back(postFromJson(line));
            }
        }
    }

    // 5.     
    std::string contestFile = dataDir + "contests.json";
    if (common::fileExists(contestFile)) {
        std::string content = common::readFile(contestFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                contests.push_back(contestFromJson(line));
            }
        }
    }

    // 6.  ⵥ
    std::string setFile = dataDir + "problemsets.json";
    if (common::fileExists(setFile)) {
        std::string content = common::readFile(setFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                problemSets.push_back(problemSetFromJson(line));
            }
        }
    }

    // 7.   Ϣ
    std::string msgFile = dataDir + "messages.json";
    if (common::fileExists(msgFile)) {
        std::string content = common::readFile(msgFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                messages.push_back(messageFromJson(line));
            }
        }
    }

    // 8.     
    // coin shop load
    // groups
    std::string groupFile = dataDir + "groups.json";
    if (common::fileExists(groupFile)) {
        std::string content = common::readFile(groupFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) groups.push_back(groupFromJson(line));
        }
    }


    shops.clear();
    std::string shopFile = dataDir + "shops.json";
    if (common::fileExists(shopFile)) {
        std::string content = common::readFile(shopFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                shops.push_back(shopFromJson(line));
            }
        }
    }
    if (shops.empty()) {
        ShopItem it;
        it.id = "1";
        it.name = "VIP \u4f1a\u5458 7\u5929";
        it.description = "\u89e3\u9501\u5168\u90e8\u9898\u76ee\u4e0e\u7279\u6743\u529f\u80fd";
        it.price = 500;
        it.stock = -1;
        it.onSale = true;
        it.tokenReward = 0;
        it.icon = "\U0001f451";
        it.createTime = common::getTimeStr();
        shops.push_back(it);

        ShopItem t1;
        t1.id = "2";
        t1.name = "AI Token \u5305 \uff08600\uff09";
        t1.description = "\u83b7\u5f97 600 Token\uff0c\u53ef\u7528\u4e8e AI \u5bf9\u8bdd";
        t1.price = 300;
        t1.stock = -1;
        t1.onSale = true;
        t1.tokenReward = 600;
        t1.icon = "\U0001f916";
        t1.createTime = common::getTimeStr();
        shops.push_back(t1);

        ShopItem t2;
        t2.id = "3";
        t2.name = "AI Token \u5305 \uff083000\uff09";
        t2.description = "\u83b7\u5f97 3000 Token\uff0c\u53ef\u7528\u4e8e AI \u5bf9\u8bdd";
        t2.price = 1200;
        t2.stock = -1;
        t2.onSale = true;
        t2.tokenReward = 3000;
        t2.icon = "\U0001f916";
        t2.createTime = common::getTimeStr();
        shops.push_back(t2);

        saveShops();
    }

    // 编译语言配置加载
    languages.clear();
    std::string langFile = dataDir + "languages.json";
    if (common::fileExists(langFile)) {
        std::string content = common::readFile(langFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                languages.push_back(langFromJson(line));
            }
        }
    }
    if (languages.empty()) {
        // 默认语言：C++（编译型）
        LangConfig cpp;
        cpp.name = "C++";
        cpp.id = "cpp";
        cpp.ext = "cpp";
        cpp.compileCmd = "\"{gpp}\" -O2 -std=c++11 -static \"{src}\" -o \"{out}\"";
        cpp.runCmd = "";
        cpp.enabled = true;
        languages.push_back(cpp);
        // 默认语言：Python（解释型）
        LangConfig py;
        py.name = "Python";
        py.id = "python";
        py.ext = "py";
        py.compileCmd = "";
        py.runCmd = "python \"{src}\"";
        py.enabled = true;
        languages.push_back(py);
        saveLanguages();
    }

        // task bounty load
    tasks.clear();
    std::string taskFile = dataDir + "tasks.json";
    if (common::fileExists(taskFile)) {
        std::string content = common::readFile(taskFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                tasks.push_back(taskFromJson(line));
            }
        }
    }

    // -p U
    orders.clear();
    std::string orderFile = dataDir + "orders.json";
    if (common::fileExists(orderFile)) {
        std::string content = common::readFile(orderFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (!line.empty()) {
                orders.push_back(orderFromJson(line));
            }
        }
    }
    // 试卷
    papers.clear();
    paperSubs.clear();
    std::string paperFile = dataDir + "papers.json";
    if (common::fileExists(paperFile)) {
        std::string content = common::readFile(paperFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) if (!line.empty()) papers.push_back(paperFromJson(line));
    }
    std::string paperSubFile = dataDir + "paper_subs.json";
    if (common::fileExists(paperSubFile)) {
        std::string content = common::readFile(paperSubFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) if (!line.empty()) paperSubs.push_back(paperSubFromJson(line));
    }
    // 对决
    duels.clear();
    std::string duelFile = dataDir + "duels.json";
    if (common::fileExists(duelFile)) {
        std::string dcontent = common::readFile(duelFile);
        auto dlines = common::split(dcontent, '\n');
        for (const auto& line : dlines) {
            if (!line.empty()) duels.push_back(duelFromJson(line));
        }
    }

std::string annFile = dataDir + "announcement.json";
    if (common::fileExists(annFile)) {
        std::string content = common::readFile(annFile);
        if (!content.empty()) {
            announcement = announcementFromJson(content);
        }
    }
    if (announcement.empty()) {
        announcement = "一点都不欢迎来到OI小站<br>请关注作者luoguuid:1813501<br>OIstarionv1.4.1正在为您提供服务";
        saveAnnouncement();
    }

    std::cout << "[DataStore] Loaded " << users.size() << " users, "
              << problems.size() << " problems, "
              << submissions.size() << " submissions, "
              << posts.size() << " posts, "
              << contests.size() << " contests, "
              << problemSets.size() << " sets, "
              << messages.size() << " messages." << std::endl;
}

// ============================================================
//   ѯ    
// ============================================================
User* DataStore::findUserById(const std::string& id) {
    for (auto& u : users) if (u.id == id) return &u;
    return nullptr;
}

User* DataStore::findUserByName(const std::string& name) {
    for (auto& u : users) if (u.username == name) return &u;
    return nullptr;
}

Problem* DataStore::findProblemById(const std::string& id) {
    for (auto& p : problems) if (p.id == id) return &p;
    return nullptr;
}

Submission* DataStore::findSubmissionById(const std::string& id) {
    for (auto& s : submissions) if (s.id == id) return &s;
    return nullptr;
}

Post* DataStore::findPostById(const std::string& id) {
    for (auto& p : posts) if (p.id == id) return &p;
    return nullptr;
}

Comment* DataStore::findCommentInPost(const std::string& postId, const std::string& commentId) {
    Post* post = findPostById(postId);
    if (!post) return nullptr;
    for (auto& c : post->comments) {
        if (c.id == commentId) return &c;
    }
    return nullptr;
}

Contest* DataStore::findContestById(const std::string& id) {
    for (auto& c : contests) if (c.id == id) return &c;
    return nullptr;
}

ProblemSet* DataStore::findProblemSetById(const std::string& id) {
    for (auto& ps : problemSets) if (ps.id == id) return &ps;
    return nullptr;
}

Message* DataStore::findMessageById(const std::string& id) {
    for (auto& m : messages) if (m.id == id) return &m;
    return nullptr;
}

std::string DataStore::groupToJson(const GroupChat& g) {
    std::map<std::string, std::string> m;
    m["id"] = g.id;
    m["name"] = g.name;
    m["ownerId"] = g.ownerId;
    m["createTime"] = g.createTime;
    m["memberIds"] = common::jsonStringify(g.memberIds);
    return common::jsonStringify(m);
}

GroupChat DataStore::groupFromJson(const std::string& json) {
    GroupChat g;
    auto obj = common::jsonParseObject(json);
    g.id = obj.count("id") ? obj["id"] : "";
    g.name = obj.count("name") ? obj["name"] : "";
    g.ownerId = obj.count("ownerId") ? obj["ownerId"] : "";
    g.createTime = obj.count("createTime") ? obj["createTime"] : "";
    if (obj.count("memberIds")) g.memberIds = common::jsonParseArray(obj["memberIds"]);
    return g;
}

void DataStore::saveGroups() {
    std::vector<std::string> _l;
    for (const auto& g : groups) _l.push_back(groupToJson(g));
    common::lockData();
    common::writeFile(getDataDir() + "groups.json", mergeWithDisk(getDataDir() + "groups.json", _l));
    common::unlockData();
}

GroupChat* DataStore::findGroupById(const std::string& id) {
    for (auto& g : groups) if (g.id == id) return &g;
    return nullptr;
}

// ============================================================
// ͳ ƺ   
// ============================================================
int DataStore::getProblemACCount(const std::string& problemId) {
    int cnt = 0;
    for (const auto& s : submissions) {
        if (s.problemId == problemId && s.status == AC) cnt++;
    }
    return cnt;
}

int DataStore::getProblemSubmitCount(const std::string& problemId) {
    int cnt = 0;
    for (const auto& s : submissions) {
        if (s.problemId == problemId) cnt++;
    }
    return cnt;
}

int DataStore::getUserACCount(const std::string& userId) {
    int cnt = 0;
    for (const auto& s : submissions) {
        if (s.userId == userId && s.status == AC && !s.hacked) cnt++;
    }
    return cnt;
}

int DataStore::getUserSubmitCount(const std::string& userId) {
    int cnt = 0;
    for (const auto& s : submissions) {
        if (s.userId == userId) cnt++;
    }
    return cnt;
}

// ============================================================
// Token         
// ============================================================
bool DataStore::addTokens(const std::string& userId, int amount) {
    User* user = findUserById(userId);
    if (!user) return false;
    user->tokens += amount;
    saveUsers();
    // Token 已停用：不再折算 AI 额度，AI 只认套餐额度 + 商店购买的额外算力点
    (void)amount;
    return true;
}

bool DataStore::useTokens(const std::string& userId, int amount) {
    User* user = findUserById(userId);
    if (!user) return false;
    if (user->tokens < amount) return false;
    user->tokens -= amount;
    saveUsers();
    return true;
}

int DataStore::getTokens(const std::string& userId) {
    User* user = findUserById(userId);
    if (!user) return 0;
    return user->tokens;
}

// ============================================================
// Ȩ ޼  
// ============================================================
bool DataStore::canManageProblems(User* user) {
    if (!user) return false;
    return user->role == ROLE_ADMIN || user->role == ROLE_PROBLEM_MANAGER;
}

// ---------- ShopItem ----------
std::string DataStore::shopToJson(const ShopItem& s) {
    std::map<std::string, std::string> m;
    m["id"] = s.id;
    m["name"] = s.name;
    m["description"] = s.description;
    m["price"] = std::to_string(s.price);
    m["stock"] = std::to_string(s.stock);
    m["onSale"] = s.onSale ? "1" : "0";
    m["tokenReward"] = std::to_string(s.tokenReward);
    m["type"] = std::to_string(s.type);
    m["effectType"] = std::to_string(s.effectType);
    m["effectValue"] = std::to_string(s.effectValue);
    m["durationDays"] = std::to_string(s.durationDays);
    m["icon"] = s.icon;    m["icon"] = s.icon;
    m["createTime"] = s.createTime;
    return common::jsonStringify(m);
}

ShopItem DataStore::shopFromJson(const std::string& json) {
    ShopItem s;
    auto obj = common::jsonParseObject(json);
    s.id = obj.count("id") ? obj["id"] : "";
    s.name = obj.count("name") ? obj["name"] : "";
    s.description = obj.count("description") ? obj["description"] : "";
    s.price = obj.count("price") ? common::stringToInt(obj["price"]) : 0;
    s.stock = obj.count("stock") ? common::stringToInt(obj["stock"]) : -1;
    s.onSale = obj.count("onSale") ? (obj["onSale"] == "1") : true;
    s.tokenReward = obj.count("tokenReward") ? common::stringToInt(obj["tokenReward"]) : 0;
    s.type = obj.count("type") ? common::stringToInt(obj["type"]) : 0;
    s.effectType = obj.count("effectType") ? common::stringToInt(obj["effectType"]) : 0;
    s.effectValue = obj.count("effectValue") ? common::stringToInt(obj["effectValue"]) : 0;
    s.durationDays = obj.count("durationDays") ? common::stringToInt(obj["durationDays"]) : 0;
    s.icon = obj.count("icon") ? obj["icon"] : "";    s.icon = obj.count("icon") ? obj["icon"] : "";
    s.createTime = obj.count("createTime") ? obj["createTime"] : "";
    return s;
}

std::string DataStore::langToJson(const LangConfig& l) {
    std::map<std::string, std::string> m;
    m["name"] = l.name;
    m["id"] = l.id;
    m["ext"] = l.ext;
    m["compileCmd"] = l.compileCmd;
    m["runCmd"] = l.runCmd;
    m["enabled"] = l.enabled ? "1" : "0";
    return common::jsonStringify(m);
}

LangConfig DataStore::langFromJson(const std::string& json) {
    LangConfig l;
    auto obj = common::jsonParseObject(json);
    l.name = obj.count("name") ? obj["name"] : "";
    l.id = obj.count("id") ? obj["id"] : "";
    l.ext = obj.count("ext") ? obj["ext"] : "";
    l.compileCmd = obj.count("compileCmd") ? obj["compileCmd"] : "";
    l.runCmd = obj.count("runCmd") ? obj["runCmd"] : "";
    l.enabled = obj.count("enabled") ? (obj["enabled"] == "1") : true;
    return l;
}



std::string DataStore::orderToJson(const Order& o) {
    std::map<std::string, std::string> m;
    m["id"] = o.id;
    m["shopItemId"] = o.shopItemId;
    m["itemName"] = o.itemName;
    m["icon"] = o.icon;
    m["userId"] = o.userId;
    m["username"] = o.username;
    m["quantity"] = std::to_string(o.quantity);
    m["price"] = std::to_string(o.price);
    m["contact"] = o.contact;
    m["status"] = std::to_string(o.status);
    m["time"] = o.time;
    return common::jsonStringify(m);
}

Order DataStore::orderFromJson(const std::string& json) {
    Order o;
    auto obj = common::jsonParseObject(json);
    o.id = obj.count("id") ? obj["id"] : "";
    o.shopItemId = obj.count("shopItemId") ? obj["shopItemId"] : "";
    o.itemName = obj.count("itemName") ? obj["itemName"] : "";
    o.icon = obj.count("icon") ? obj["icon"] : "";
    o.userId = obj.count("userId") ? obj["userId"] : "";
    o.username = obj.count("username") ? obj["username"] : "";
    o.quantity = obj.count("quantity") ? common::stringToInt(obj["quantity"]) : 1;
    o.price = obj.count("price") ? common::stringToInt(obj["price"]) : 0;
    o.contact = obj.count("contact") ? obj["contact"] : "";
    o.status = obj.count("status") ? common::stringToInt(obj["status"]) : ORDER_PENDING;
    o.time = obj.count("time") ? obj["time"] : "";
    return o;
}

ShopItem* DataStore::findShopItemById(const std::string& id) {
    for (auto& s : shops) if (s.id == id) return &s;
    return nullptr;
}

// ---------- Duel 对决 ----------
static std::string duelSubJson(const DuelSubmission& s) {
    std::map<std::string, std::string> m;
    m["id"] = s.id; m["problemId"] = s.problemId; m["score"] = std::to_string(s.score);
    m["pass"] = std::to_string(s.pass); m["total"] = std::to_string(s.total);
    m["code"] = s.code; m["time"] = s.time; m["isHack"] = s.isHack ? "1" : "0"; m["hacked"] = s.hacked ? "1" : "0"; m["hackInfo"] = s.hackInfo;
    return common::jsonStringify(m);
}
static DuelSubmission duelSubFromJson(const std::string& j) {
    DuelSubmission s;
    auto o = common::jsonParseObject(j);
    s.id = o.count("id") ? o["id"] : "";
    s.problemId = o.count("problemId") ? o["problemId"] : "";
    s.score = o.count("score") ? common::stringToInt(o["score"]) : 0;
    s.pass = o.count("pass") ? common::stringToInt(o["pass"]) : 0;
    s.total = o.count("total") ? common::stringToInt(o["total"]) : 0;
    s.code = o.count("code") ? o["code"] : "";
    s.time = o.count("time") ? o["time"] : "";
    s.isHack = o.count("isHack") ? (o["isHack"] == "1") : false;
    s.hacked = o.count("hacked") ? (o["hacked"] == "1") : false;
    s.hackInfo = o.count("hackInfo") ? o["hackInfo"] : "";
    return s;
}

static std::string duelPlayerJson(const DuelPlayer& p) {
    std::map<std::string, std::string> m;
    m["userId"] = p.userId;
    m["hackCount"] = std::to_string(p.hackCount);
    std::map<std::string, std::string> bs, bp, tc, lc, bc, lo, ls, ha, lastC, sub;
    for (auto& kv : p.bestScore) bs[kv.first] = std::to_string(kv.second);
    for (auto& kv : p.bestPass) bp[kv.first] = std::to_string(kv.second);
    for (auto& kv : p.totalCases) tc[kv.first] = std::to_string(kv.second);
    for (auto& kv : p.lockedCode) lc[kv.first] = kv.second;
    for (auto& kv : p.bestCode) bc[kv.first] = kv.second;
    for (auto& kv : p.locked) lo[kv.first] = kv.second ? "1" : "0";
    for (auto& kv : p.lockedScore) ls[kv.first] = std::to_string(kv.second);
    for (auto& kv : p.hacked) ha[kv.first] = kv.second ? "1" : "0";
    for (auto& kv : p.lastCode) lastC[kv.first] = kv.second;
    for (auto& kv : p.submitLog) sub[kv.first] = common::jsonStringify(kv.second);
    m["bestScore"] = common::jsonStringify(bs);
    m["bestPass"] = common::jsonStringify(bp);
    m["totalCases"] = common::jsonStringify(tc);
    m["lockedCode"] = common::jsonStringify(lc);
    m["bestCode"] = common::jsonStringify(bc);
    m["locked"] = common::jsonStringify(lo);
    m["lockedScore"] = common::jsonStringify(ls);
    m["hacked"] = common::jsonStringify(ha);
    m["lastCode"] = common::jsonStringify(lastC);
    m["submitLog"] = common::jsonStringify(sub);
    std::vector<std::string> subs;
    for (auto& s : p.submissions) subs.push_back(duelSubJson(s));
    m["submissions"] = common::jsonStringify(subs);
    return common::jsonStringify(m);
}
static DuelPlayer duelPlayerFromJson(const std::string& j) {
    DuelPlayer p;
    auto obj = common::jsonParseObject(j);
    p.userId = obj.count("userId") ? obj["userId"] : "";
    p.hackCount = obj.count("hackCount") ? common::stringToInt(obj["hackCount"]) : 0;
    auto bs = common::jsonParseObject(obj.count("bestScore") ? obj["bestScore"] : "{}");
    for (auto& kv : bs) p.bestScore[kv.first] = common::stringToInt(kv.second);
    auto bp = common::jsonParseObject(obj.count("bestPass") ? obj["bestPass"] : "{}");
    for (auto& kv : bp) p.bestPass[kv.first] = common::stringToInt(kv.second);
    auto tc = common::jsonParseObject(obj.count("totalCases") ? obj["totalCases"] : "{}");
    for (auto& kv : tc) p.totalCases[kv.first] = common::stringToInt(kv.second);
    auto lc = common::jsonParseObject(obj.count("lockedCode") ? obj["lockedCode"] : "{}");
    for (auto& kv : lc) p.lockedCode[kv.first] = kv.second;
    auto bc = common::jsonParseObject(obj.count("bestCode") ? obj["bestCode"] : "{}");
    for (auto& kv : bc) p.bestCode[kv.first] = kv.second;
    auto lo = common::jsonParseObject(obj.count("locked") ? obj["locked"] : "{}");
    for (auto& kv : lo) p.locked[kv.first] = (kv.second == "1");
    auto ls = common::jsonParseObject(obj.count("lockedScore") ? obj["lockedScore"] : "{}");
    for (auto& kv : ls) p.lockedScore[kv.first] = common::stringToInt(kv.second);
    auto ha = common::jsonParseObject(obj.count("hacked") ? obj["hacked"] : "{}");
    for (auto& kv : ha) p.hacked[kv.first] = (kv.second == "1");
    auto lastC = common::jsonParseObject(obj.count("lastCode") ? obj["lastCode"] : "{}");
    for (auto& kv : lastC) p.lastCode[kv.first] = kv.second;
    auto sub = common::jsonParseObject(obj.count("submitLog") ? obj["submitLog"] : "{}");
    for (auto& kv : sub) p.submitLog[kv.first] = common::jsonParseArray(kv.second);
    auto subs = common::jsonParseArray(obj.count("submissions") ? obj["submissions"] : "[]");
    for (auto& sj : subs) p.submissions.push_back(duelSubFromJson(sj));
    return p;
}

std::string DataStore::duelToJson(const Duel& d) {
    std::map<std::string, std::string> m;
    m["id"] = d.id; m["status"] = d.status; m["mode"] = d.mode;
    m["playerA"] = d.playerA; m["playerB"] = d.playerB; m["winner"] = d.winner;
    m["codingEndMs"] = std::to_string(d.codingEndMs);
    m["hackEndMs"] = std::to_string(d.hackEndMs);
    m["hackMinutes"] = std::to_string(d.hackMinutes);
    m["createTimeMs"] = std::to_string(d.createTimeMs);
    m["language"] = d.language;
    std::vector<std::string> ps;
    for (auto& p : d.problems) {
        std::map<std::string, std::string> pm; pm["problemId"] = p.problemId; pm["points"] = std::to_string(p.points);
        ps.push_back(common::jsonStringify(pm));
    }
    m["problems"] = common::jsonStringify(ps);
    m["pa"] = duelPlayerJson(d.pa);
    m["pb"] = duelPlayerJson(d.pb);
    m["log"] = common::jsonStringify(d.log);
    return common::jsonStringify(m);
}
Duel DataStore::duelFromJson(const std::string& j) {
    Duel d;
    auto obj = common::jsonParseObject(j);
    d.id = obj.count("id") ? obj["id"] : "";
    d.status = obj.count("status") ? obj["status"] : "";
    d.mode = obj.count("mode") ? obj["mode"] : "1v1";
    d.playerA = obj.count("playerA") ? obj["playerA"] : "";
    d.playerB = obj.count("playerB") ? obj["playerB"] : "";
    d.winner = obj.count("winner") ? obj["winner"] : "";
    d.codingEndMs = obj.count("codingEndMs") ? common::stringToLongLong(obj["codingEndMs"]) : 0;
    d.hackEndMs = obj.count("hackEndMs") ? common::stringToLongLong(obj["hackEndMs"]) : 0;
    d.hackMinutes = obj.count("hackMinutes") ? common::stringToInt(obj["hackMinutes"]) : 5;
    d.createTimeMs = obj.count("createTimeMs") ? common::stringToLongLong(obj["createTimeMs"]) : 0;
    d.language = obj.count("language") ? obj["language"] : "cpp";
    auto ps = common::jsonParseArray(obj.count("problems") ? obj["problems"] : "[]");
    for (auto& pj : ps) {
        auto pm = common::jsonParseObject(pj);
        DuelProblem dp; dp.problemId = pm.count("problemId") ? pm["problemId"] : ""; dp.points = pm.count("points") ? common::stringToInt(pm["points"]) : 100;
        d.problems.push_back(dp);
    }
    d.pa = duelPlayerFromJson(obj.count("pa") ? obj["pa"] : "{}");
    d.pb = duelPlayerFromJson(obj.count("pb") ? obj["pb"] : "{}");
    auto lg = common::jsonParseArray(obj.count("log") ? obj["log"] : "[]");
    d.log = lg;
    return d;
}


LangConfig* DataStore::findLangConfig(const std::string& id) {
    for (auto& l : languages) if (l.id == id) return &l;
    return nullptr;
}


// ---------- TaskItem ----------
std::string DataStore::taskToJson(const TaskItem& t) {
    std::map<std::string, std::string> m;
    m["id"] = t.id;
    m["title"] = t.title;
    m["description"] = t.description;
    m["authorId"] = t.authorId;
    m["reward"] = std::to_string(t.reward);
    m["status"] = std::to_string(t.status);
    m["acceptorId"] = t.acceptorId;
    m["createTime"] = t.createTime;
    std::vector<std::string> hs;
    for (const auto& h : t.helpers) {
        std::map<std::string, std::string> hm;
        hm["userId"] = h.userId;
        hm["state"] = std::to_string(h.state);
        hm["submitTime"] = h.submitTime;
        hm["submitNote"] = h.submitNote;
        hs.push_back(common::jsonStringify(hm));
    }
    m["helpers"] = common::jsonStringify(hs);
    std::vector<std::string> ms;
    for (const auto& tm : t.taskMessages) {
        std::map<std::string, std::string> mm;
        mm["id"] = tm.id;
        mm["fromId"] = tm.fromId;
        mm["toId"] = tm.toId;
        mm["content"] = tm.content;
        mm["sendTime"] = tm.sendTime;
        ms.push_back(common::jsonStringify(mm));
    }
    m["messages"] = common::jsonStringify(ms);
    return common::jsonStringify(m);
}

TaskItem DataStore::taskFromJson(const std::string& json) {
    TaskItem t;
    auto obj = common::jsonParseObject(json);
    t.id = obj.count("id") ? obj["id"] : "";
    t.title = obj.count("title") ? obj["title"] : "";
    t.description = obj.count("description") ? obj["description"] : "";
    t.authorId = obj.count("authorId") ? obj["authorId"] : "";
    t.reward = obj.count("reward") ? common::stringToInt(obj["reward"]) : 0;
    t.status = obj.count("status") ? common::stringToInt(obj["status"]) : TASK_OPEN;
    t.acceptorId = obj.count("acceptorId") ? obj["acceptorId"] : "";
    t.createTime = obj.count("createTime") ? obj["createTime"] : "";
    if (obj.count("helpers")) {
        auto hs = common::jsonParseArray(obj["helpers"]);
        for (const auto& hj : hs) {
            auto hm = common::jsonParseObject(hj);
            TaskHelper h;
            h.userId = hm.count("userId") ? hm["userId"] : "";
            h.state = hm.count("state") ? common::stringToInt(hm["state"]) : TH_ACCEPTED;
            h.submitTime = hm.count("submitTime") ? hm["submitTime"] : "";
            h.submitNote = hm.count("submitNote") ? hm["submitNote"] : "";
            if (!h.userId.empty()) t.helpers.push_back(h);
        }
    }
    if (t.helpers.empty() && !t.acceptorId.empty()) {
        TaskHelper h;
        h.userId = t.acceptorId;
        h.state = TH_ACCEPTED;
        t.helpers.push_back(h);
    }
    if (obj.count("messages")) {
        auto ms = common::jsonParseArray(obj["messages"]);
        for (const auto& mj : ms) {
            auto mm = common::jsonParseObject(mj);
            TaskMessage tm;
            tm.id = mm.count("id") ? mm["id"] : "";
            tm.fromId = mm.count("fromId") ? mm["fromId"] : "";
            tm.toId = mm.count("toId") ? mm["toId"] : "";
            tm.content = mm.count("content") ? mm["content"] : "";
            tm.sendTime = mm.count("sendTime") ? mm["sendTime"] : "";
            if (!tm.id.empty()) t.taskMessages.push_back(tm);
        }
    }
    return t;
}

TaskItem* DataStore::findTaskById(const std::string& id) {
    for (auto& t : tasks) if (t.id == id) return &t;
    return nullptr;
}

Order* DataStore::findOrderById(const std::string& id) {
    for (auto& o : orders) if (o.id == id) return &o;
    return nullptr;
}

void DataStore::saveTasks() {
    std::vector<std::string> _l;
    for (const auto& t : tasks) _l.push_back(taskToJson(t));
    common::lockData();
    common::writeFile(getDataDir() + "tasks.json", mergeWithDisk(getDataDir() + "tasks.json", _l));
    common::unlockData();
}

void DataStore::saveOrders() {
    std::vector<std::string> _l;
    for (const auto& o : orders) _l.push_back(orderToJson(o));
    common::lockData();
    common::writeFile(getDataDir() + "orders.json", mergeWithDisk(getDataDir() + "orders.json", _l));
    common::unlockData();
}

Duel* DataStore::findDuelById(const std::string& id) {
    for (auto& d : duels) if (d.id == id) return &d;
    return nullptr;
}

void DataStore::saveDuels() {
    std::vector<std::string> _l;
    for (const auto& d : duels) _l.push_back(duelToJson(d));
    common::lockData();
    common::writeFile(getDataDir() + "duels.json", mergeWithDisk(getDataDir() + "duels.json", _l));
    common::unlockData();
}
// ---------- 试卷 ----------
Paper* DataStore::findPaperById(const std::string& id) {
    for (auto& p : papers) if (p.id == id) return &p;
    return nullptr;
}

void DataStore::savePapers() {
    std::vector<std::string> _l;
    for (const auto& p : papers) _l.push_back(paperToJson(p));
    common::lockData();
    common::writeFile(getDataDir() + "papers.json", mergeWithDisk(getDataDir() + "papers.json", _l));
    common::unlockData();
}

void DataStore::savePaperSubs() {
    std::vector<std::string> _l;
    for (const auto& ps : paperSubs) _l.push_back(paperSubToJson(ps));
    common::lockData();
    common::writeFile(getDataDir() + "paper_subs.json", mergeWithDisk(getDataDir() + "paper_subs.json", _l));
    common::unlockData();
}

std::string DataStore::paperToJson(const Paper& p) {
    std::map<std::string, std::string> m;
    m["id"] = p.id;
    m["title"] = p.title;
    m["description"] = p.description;
    m["authorId"] = p.authorId;
    m["createTime"] = p.createTime;
    m["startTime"] = p.startTime;
    m["endTime"] = p.endTime;
    m["hidden"] = p.hidden ? "1" : "0";
    m["timeLimit"] = std::to_string(p.timeLimit);
    std::vector<std::string> qs;
    for (const auto& q : p.questions) {
        std::map<std::string, std::string> qm;
        qm["type"] = q.type;
        qm["stem"] = q.stem;
        qm["options"] = common::jsonStringify(q.options);
        qm["multi"] = q.multi ? "1" : "0";
        qm["answer"] = q.answer;
        qm["score"] = std::to_string(q.score);
        qm["analysis"] = q.analysis;
        qs.push_back(common::jsonStringify(qm));
    }
    m["questions"] = common::jsonStringify(qs);
    return common::jsonStringify(m);
}

Paper DataStore::paperFromJson(const std::string& json) {
    Paper p;
    auto obj = common::jsonParseObject(json);
    p.id = obj.count("id") ? obj["id"] : "";
    p.title = obj.count("title") ? obj["title"] : "";
    p.description = obj.count("description") ? obj["description"] : "";
    p.authorId = obj.count("authorId") ? obj["authorId"] : "";
    p.createTime = obj.count("createTime") ? obj["createTime"] : "";
    p.startTime = obj.count("startTime") ? obj["startTime"] : "";
    p.endTime = obj.count("endTime") ? obj["endTime"] : "";
    p.hidden = obj.count("hidden") ? (obj["hidden"] == "1") : false;
    p.timeLimit = obj.count("timeLimit") ? common::stringToInt(obj["timeLimit"]) : 0;
    if (obj.count("questions")) {
        auto arr = common::jsonParseArray(obj["questions"]);
        for (const auto& item : arr) {
            auto qm = common::jsonParseObject(item);
            if (qm.empty()) continue;
            PaperQuestion q;
            q.type = qm.count("type") ? qm["type"] : "choice";
            q.stem = qm.count("stem") ? qm["stem"] : "";
            if (qm.count("options")) q.options = common::jsonParseArray(qm["options"]);
            q.multi = qm.count("multi") ? (qm["multi"] == "1") : false;
            q.answer = qm.count("answer") ? qm["answer"] : "";
            q.score = qm.count("score") ? common::stringToInt(qm["score"]) : 5;
            q.analysis = qm.count("analysis") ? qm["analysis"] : "";
            p.questions.push_back(q);
        }
    }
    return p;
}

std::string DataStore::paperSubToJson(const PaperSubmission& ps) {
    std::map<std::string, std::string> m;
    m["id"] = ps.id;
    m["paperId"] = ps.paperId;
    m["userId"] = ps.userId;
    m["username"] = ps.username;
    m["time"] = ps.time;
    m["total"] = std::to_string(ps.total);
    m["fullScore"] = std::to_string(ps.fullScore);
    m["objective"] = ps.objective ? "1" : "0";
    m["answers"] = common::jsonStringify(ps.answers);
    std::vector<std::string> sc;
    for (int v : ps.scores) sc.push_back(std::to_string(v));
    m["scores"] = common::jsonStringify(sc);
    return common::jsonStringify(m);
}

PaperSubmission DataStore::paperSubFromJson(const std::string& json) {
    PaperSubmission ps;
    auto obj = common::jsonParseObject(json);
    ps.id = obj.count("id") ? obj["id"] : "";
    ps.paperId = obj.count("paperId") ? obj["paperId"] : "";
    ps.userId = obj.count("userId") ? obj["userId"] : "";
    ps.username = obj.count("username") ? obj["username"] : "";
    ps.time = obj.count("time") ? obj["time"] : "";
    ps.total = obj.count("total") ? common::stringToInt(obj["total"]) : 0;
    ps.fullScore = obj.count("fullScore") ? common::stringToInt(obj["fullScore"]) : 0;
    ps.objective = obj.count("objective") ? (obj["objective"] == "1") : true;
    if (obj.count("answers")) ps.answers = common::jsonParseArray(obj["answers"]);
    if (obj.count("scores")) {
        for (const auto& v : common::jsonParseArray(obj["scores"])) ps.scores.push_back(common::stringToInt(v));
    }
    return ps;
}

bool DataStore::addGold(const std::string& userId, long long amount) {
    User* user = findUserById(userId);
    if (!user) return false;
    // 允许正数奖励，也允许负数扣钱（余额最低为 0，不为负）
    long long after = user->gold + amount;
    if (after > 9000000000000LL) after = 9000000000000LL;
    else if (after < 0) after = 0;
    user->gold = after;
    saveUsers();
    return true;
}

bool DataStore::useGold(const std::string& userId, long long amount) {
    User* user = findUserById(userId);
    if (!user) return false;
    if (user->gold < amount) return false;
    user->gold -= amount;
    saveUsers();
    return true;
}

long long DataStore::getGold(const std::string& userId) {
    User* user = findUserById(userId);
    if (!user) return 0;
    return user->gold;
}

bool DataStore::addExperience(const std::string& userId, int amount) {
    User* user = findUserById(userId);
    if (!user) return false;
    user->experience += amount;
    saveUsers();
    return true;
}

int DataStore::getExperience(const std::string& userId) {
    User* user = findUserById(userId);
    if (!user) return 0;
    return user->experience;
}

int DataStore::calcLevel(int exp) {
    int level = 1, need = 100, cum = 0;
    while (exp >= cum + need) {
        cum += need;
        need += 50;
        level++;
    }
    return level;
}

int DataStore::levelBaseExp(int level) {
    int need = 100, cum = 0;
    for (int i = 1; i < level; i++) { cum += need; need += 50; }
    return cum;
}

std::string DataStore::getLevelBadge(int level) {
    if (level >= 30) return "\U0001f3c6 \u4f20\u8bf4";
    if (level >= 20) return "\U0001f451 \u738b\u8005";
    if (level >= 15) return "\U0001f48e \u5927\u5e08";
    if (level >= 10) return "\U0001f948 \u9ad8\u624b";
    if (level >= 5)  return "\U0001f948 \u8fdb\u9636";
    return "\U0001f331 \u65b0\u624b";
}

#include "../include/admin_perm.h"
#include "../include/models.h"
#include "../include/common.h"
#include <cstring>

namespace adminperm {

static const char* kAll[] = {
    "users", "problems", "solutions", "content",
    "contests", "papers", "shop", "ai", "system"
};

std::vector<std::string> all() {
    std::vector<std::string> out;
    for (size_t i = 0; i < sizeof(kAll) / sizeof(kAll[0]); i++) out.push_back(kAll[i]);
    return out;
}

std::string label(const std::string& p) {
    if (p == "users") return "用户与权限";
    if (p == "problems") return "题目与评测";
    if (p == "solutions") return "题解审核";
    if (p == "content") return "社区内容";
    if (p == "contests") return "比赛与题单";
    if (p == "papers") return "试卷";
    if (p == "shop") return "商城与订单";
    if (p == "ai") return "AI 管理";
    if (p == "system") return "系统设置";
    return p;
}

std::string desc(const std::string& p) {
    if (p == "users") return "用户列表、发放金币/Token、分配套餐、权限管理";
    if (p == "problems") return "增删改题目、测试数据、重测";
    if (p == "solutions") return "审核/隐藏用户题解";
    if (p == "content") return "帖子、博客、公告与举报处理";
    if (p == "contests") return "比赛、题单管理";
    if (p == "papers") return "试卷组卷与批改管理";
    if (p == "shop") return "商品、订单管理";
    if (p == "ai") return "AI 提供方、模型、套餐、额度";
    if (p == "system") return "系统开关、语言、云盘、IP 封禁";
    return "";
}

bool has(const User* u, const std::string& perm) {
    if (!u) return false;
    if (u->role == ROLE_ADMIN) return true;
    for (size_t i = 0; i < u->adminPerms.size(); i++)
        if (u->adminPerms[i] == perm) return true;
    // 兼容旧角色：题目管理员
    if (u->role == ROLE_PROBLEM_MANAGER && perm == "problems") return true;
    return false;
}

bool any(const User* u) {
    if (!u) return false;
    if (u->role == ROLE_ADMIN || u->role == ROLE_PROBLEM_MANAGER) return true;
    return !u->adminPerms.empty();
}

static bool starts(const std::string& s, const char* p) {
    return s.compare(0, strlen(p), p) == 0;
}

bool pathAllowed(const User* u, const std::string& path) {
    if (!any(u)) return false;
    if (path == "/admin" || path == "/admin/") return true;

    if (starts(path, "/admin/permissions") || starts(path, "/admin/user/") ||
        starts(path, "/admin/users"))
        return has(u, "users");

    if (starts(path, "/admin/solution"))
        return has(u, "solutions");

    if (starts(path, "/admin/problem/solution-toggle"))
        return has(u, "solutions") || has(u, "problems");

    if (starts(path, "/admin/problem") || starts(path, "/admin/rejudge"))
        return has(u, "problems");

    if (starts(path, "/admin/post") || starts(path, "/admin/announcement") ||
        starts(path, "/admin/reports"))
        return has(u, "content");

    if (starts(path, "/admin/contest") || starts(path, "/admin/set/"))
        return has(u, "contests");

    if (starts(path, "/admin/paper"))
        return has(u, "papers");

    if (starts(path, "/admin/shop") || starts(path, "/admin/order"))
        return has(u, "shop");

    if (starts(path, "/admin/ai"))
        return has(u, "ai");

    if (starts(path, "/admin/system") || starts(path, "/admin/languages") ||
        starts(path, "/admin/drive") || starts(path, "/admin/ban"))
        return has(u, "system");

    // 未归类的新后台页面：只要有任何管理权限即可进入
    return any(u);
}

std::string summary(const User* u) {
    if (!u) return "";
    if (u->role == ROLE_ADMIN) return "全部权限";
    std::string out;
    std::vector<std::string> ps = all();
    for (size_t i = 0; i < ps.size(); i++) {
        if (!has(u, ps[i])) continue;
        if (!out.empty()) out += "、";
        out += label(ps[i]);
    }
    return out.empty() ? "无" : out;
}

} // namespace adminperm

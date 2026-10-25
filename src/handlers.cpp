#include "../include/handlers.h"
#include "../include/session.h"
#include "../include/judge.h"
#include "../include/common.h"
#include "../include/security.h"
#include "../include/papers.h"
#include "../include/forum.h"
#include "../include/ai_quota.h"
#include "../include/ai_pages.h"
#include "../include/ai_models.h"
#include "../include/ai_providers.h"
#include "../include/ai_plans.h"
#include "../include/ai_tools.h"
#include "../include/admin_perm.h"
#include "../include/reports.h"
#include <windows.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <thread>
#include <mutex>


// ---------- \u7248\u672c\u4fe1\u606f ----------
#define OISTATION_VERSION "1.18.0"

// ---------- 辅助函数 ----------
static int getUserDriveTotalMB(User* u);
static int getUserEffCoeff(User* u);
static std::string sanitizeRelPath(const std::string& p);
// 云盘/共享页通用小工具（定义在文件后部 Cloud Drive 段落）
static std::string drvQueryValue(const std::string& path, const std::string& key);

// 从 URL 查询串中取一个参数
static std::string queryParam(const std::string& path, const std::string& key) {
    size_t q = path.find('?');
    if (q == std::string::npos) return "";
    std::string query = path.substr(q + 1);
    auto parts = common::split(query, '&');
    for (size_t i = 0; i < parts.size(); i++) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        if (parts[i].substr(0, eq) == key) return common::urlDecode(parts[i].substr(eq + 1));
    }
    return "";
}

// 获取客户端IP（由 server.cpp 注入的 x-client-ip）
static std::string clientIpOf(const std::map<std::string, std::string>& headers) {
    auto it = headers.find("x-client-ip");
    if (it != headers.end() && !it->second.empty()) return it->second;
    auto it2 = headers.find("x-forwarded-for");
    if (it2 != headers.end()) { std::string v = common::trim(it2->second); size_t c = v.find(','); if (c != std::string::npos) v = v.substr(0, c); return v; }
    return "";
}

// 从 URL（含 query string）中提取指定查询参数值
static std::string parseQueryString(const std::string& path, const std::string& key) {
    std::string needle = key + "=";
    size_t p = path.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    size_t amp = path.find('&', p);
    std::string raw = path.substr(p, (amp == std::string::npos) ? std::string::npos : amp - p);
    return common::urlDecode(raw);
}

// 前向声明：系统通知（定义在私信区），供题解审核驳回等使用
static void pushSystemNotification(const std::string& userId, const std::string& content);

// ========== \u7cfb\u7edf\u5f00\u5173\u914d\u7f6e ==========
// ========== \u7cfb\u7edf\u5f00\u5173\u914d\u7f6e ==========
struct SystemConfig {
    bool allowRegister = true;
    bool allowBlog = true;
    bool allowDiscuss = true;
    bool allowMessages = true;
    bool allowSubmit = true;
    bool allowJudge = true;
    bool allowBenben = true;
};

static SystemConfig loadSystemConfig() {
    SystemConfig sc;
    std::string cfg = common::readFile("./data/system.json");
    if (!cfg.empty()) {
        auto o = common::jsonParseObject(cfg);
        if (o.count("allowRegister")) sc.allowRegister = (o["allowRegister"] == "1");
        if (o.count("allowBlog")) sc.allowBlog = (o["allowBlog"] == "1");
        if (o.count("allowDiscuss")) sc.allowDiscuss = (o["allowDiscuss"] == "1");
        if (o.count("allowMessages")) sc.allowMessages = (o["allowMessages"] == "1");
        if (o.count("allowSubmit")) sc.allowSubmit = (o["allowSubmit"] == "1");
        if (o.count("allowJudge")) sc.allowJudge = (o["allowJudge"] == "1");
        if (o.count("allowBenben")) sc.allowBenben = (o["allowBenben"] == "1");
    }
    return sc;
}

static void saveSystemConfig(const SystemConfig& sc) {
    std::map<std::string, std::string> m;
    m["allowRegister"] = sc.allowRegister ? "1" : "0";
    m["allowBlog"] = sc.allowBlog ? "1" : "0";
    m["allowDiscuss"] = sc.allowDiscuss ? "1" : "0";
    m["allowMessages"] = sc.allowMessages ? "1" : "0";
    m["allowSubmit"] = sc.allowSubmit ? "1" : "0";
    m["allowJudge"] = sc.allowJudge ? "1" : "0";
    m["allowBenben"] = sc.allowBenben ? "1" : "0";
    common::writeFile("./data/system.json", common::jsonStringify(m));
}

User* Handlers::getCurrentUser(const std::string& sessionId) {
    if (sessionId.empty()) return nullptr;
    Session* sess = SessionManager::instance().getSession(sessionId);
    if (!sess) return nullptr;
    return DataStore::instance().findUserById(sess->userId);
}

bool Handlers::isAdmin(User* user) {
    return user && adminperm::any(user);
}

bool Handlers::canManageProblems(User* user) {
    if (!user) return false;
    return adminperm::has(user, "problems");
}

bool Handlers::isProblemOwner(Problem* p, User* u) {
    return u && p && p->author == u->username;
}

std::string Handlers::problemQuickList() {
    std::string r;
    for (const auto& p : DataStore::instance().problems) {
        if (p.hidden) continue;
        r += "<span class='pitem' data-k='" + common::htmlEscape(p.id + " " + p.title) + "' style='display:inline-block;padding:3px 8px;margin:2px;background:#f0f5f1;border-radius:6px;'>"
             + common::htmlEscape(p.id) + " - " + common::htmlEscape(p.title) + "</span>";
    }
    return r;
}

std::string Handlers::getStatusText(SubmitStatus status) {
    switch(status) {
        case PENDING: return "等待评测";
        case JUDGING: return "评测中";
        case AC: return "Accepted";
        case WA: return "Wrong Answer";
        case TLE: return "Time Limit Exceeded";
        case MLE: return "Memory Limit Exceeded";
        case CE: return "Compile Error";
        case RE: return "Runtime Error";
        default: return "Unknown";
    }
}

std::string Handlers::getRatingBadge(int rating) {
    if (rating >= 3000) return "🏆 传奇";
    if (rating >= 2600) return "👑 王者";
    if (rating >= 2200) return "💎 大师";
    if (rating >= 1800) return "🥇 金牌";
    if (rating >= 1500) return "🥈 银牌";
    if (rating >= 1200) return "🥉 铜牌";
    return "📘 新手";
}

std::string Handlers::parseFormData(const std::string& body, const std::string& key) {
    auto parts = common::split(body, '&');
    for (const auto& part : parts) {
        auto kv = common::split(part, '=');
        if (kv.size() == 2 && kv[0] == key) {
            return common::urlDecode(kv[1]);
        }
    }
    return "";
}

std::string Handlers::renderPagination(const std::string& baseUrl, int currentPage, int totalPages) {
    if (totalPages <= 1) return "";
    std::string html = "<div style='display:flex;gap:5px;justify-content:center;margin:20px 0;flex-wrap:wrap;'>";
    
    if (currentPage > 1) {
        html += "<a href='" + baseUrl + "/1' class='btn btn-sm'>首页</a>";
        html += "<a href='" + baseUrl + "/" + std::to_string(currentPage - 1) + "' class='btn btn-sm'>‹</a>";
    }
    
    int startPage = std::max(1, currentPage - 2);
    int endPage = std::min(totalPages, currentPage + 2);
    
    for (int i = startPage; i <= endPage; ++i) {
        if (i == currentPage) {
            html += "<span class='btn btn-sm' style='background:#667eea;color:white;'>" + std::to_string(i) + "</span>";
        } else {
            html += "<a href='" + baseUrl + "/" + std::to_string(i) + "' class='btn btn-sm'>" + std::to_string(i) + "</a>";
        }
    }
    
    if (currentPage < totalPages) {
        html += "<a href='" + baseUrl + "/" + std::to_string(currentPage + 1) + "' class='btn btn-sm'>›</a>";
        html += "<a href='" + baseUrl + "/" + std::to_string(totalPages) + "' class='btn btn-sm'>末页</a>";
    }
    
    html += "</div>";
    return html;
}

// 分页（保留额外 query 参数，如 tab/q/mine）。params 形如 "tab=solution&q=abc"，不含?与&
std::string Handlers::renderPaginationKeepParams(const std::string& baseUrl, int currentPage, int totalPages,
                                                 const std::string& params) {
    std::string qs = params.empty() ? "" : ("?" + params);
    if (totalPages <= 1) return "";
    std::string html = "<div style='display:flex;gap:5px;justify-content:center;margin:20px 0;flex-wrap:wrap;'>";
    if (currentPage > 1) {
        html += "<a href='" + baseUrl + "/1" + qs + "' class='btn btn-sm'>首页</a>";
        html += "<a href='" + baseUrl + "/" + std::to_string(currentPage - 1) + qs + "' class='btn btn-sm'>‹</a>";
    }
    int startPage = std::max(1, currentPage - 2);
    int endPage = std::min(totalPages, currentPage + 2);
    for (int i = startPage; i <= endPage; ++i) {
        if (i == currentPage) {
            html += "<span class='btn btn-sm' style='background:#667eea;color:white;'>" + std::to_string(i) + "</span>";
        } else {
            html += "<a href='" + baseUrl + "/" + std::to_string(i) + qs + "' class='btn btn-sm'>" + std::to_string(i) + "</a>";
        }
    }
    if (currentPage < totalPages) {
        html += "<a href='" + baseUrl + "/" + std::to_string(currentPage + 1) + qs + "' class='btn btn-sm'>›</a>";
        html += "<a href='" + baseUrl + "/" + std::to_string(totalPages) + qs + "' class='btn btn-sm'>末页</a>";
    }
    html += "</div>";
    return html;
}
std::string Handlers::getNav(User* user) {
    // ===== 侧栏：大栏（学习/社区/AI/我的）+ 小栏（进入该栏或悬停时展开） =====
    std::string sb = R"(<aside class="side-nav"><div class="side-brand"><a href='/' class="brand">OI<span class="dot sname">小站</span></a></div><nav class="side-menu">)";
    struct NM { std::string label; std::string href; std::string icon; };
    auto grp = [&](const std::string& key, const std::string& icon, const std::string& name,
                   std::vector<NM> items) {
        sb += "<div class='side-group'>";
        sb += "<div class='side-grp' data-grp='" + key + "'><span class='si'>" + icon +
              "</span><span class='sl'>" + name + "</span><span class='sc'>&#9662;</span></div>";
        sb += "<div class='side-sub' data-sub='" + key + "'>";
        for (size_t i = 0; i < items.size(); i++) {
            sb += "<a class='side-link' href='" + items[i].href + "'><span class='si'>" + items[i].icon +
                  "</span><span class='sl'>" + items[i].label + "</span></a>\n";
        }
        sb += "</div></div>";
    };
    auto it = [](const char* label, const char* href, const char* icon) {
        NM n; n.label = label; n.href = href; n.icon = icon; return n;
    };

    std::vector<NM> learn;
    learn.push_back(it("题库", "/problems", "◆"));
    learn.push_back(it("题单", "/sets", "📖"));
    learn.push_back(it("试卷", "/papers", "📝"));
    learn.push_back(it("比赛", "/contests", "🏆"));
    learn.push_back(it("提交状态", "/status", "✓"));
    learn.push_back(it("试运行", "/run", "▶"));
    grp("learn", "🎓", "学习", learn);

    std::vector<NM> comm;
    comm.push_back(it("讨论区", "/forum", "💬"));
    comm.push_back(it("博客", "/blog", "✎"));
    comm.push_back(it("排行榜", "/rank", "🏅"));
    comm.push_back(it("对决", "/duel", "⚔"));
    grp("comm", "💬", "社区", comm);

    std::vector<NM> ai;
    ai.push_back(it("AI 工具", "/ai", "🤖"));
    ai.push_back(it("AI 对话", "/llm/chat", "🗨"));
    ai.push_back(it("OI-debug", "/ai/debug", "🐞"));
    ai.push_back(it("思路讲解", "/ai/explain", "🧠"));
    ai.push_back(it("额度用量", "/ai/usage", "📊"));
    ai.push_back(it("学习分析", "/ai/study", "📈"));
    grp("ai", "🤖", "AI", ai);

    std::vector<NM> me;
    if (user) {
        me.push_back(it("个人主页", ("/user/" + user->id).c_str(), "👤"));
        me.push_back(it("消息", "/messages", "✉"));
        me.push_back(it("我的收藏", "/my/favorites", "⭐"));
        me.push_back(it("任务", "/tasks", "📋"));
        me.push_back(it("云盘", "/drive", "☁"));
        me.push_back(it("商城", "/shop", "🛒"));
        me.push_back(it("设置", "/user/settings", "⚙"));
        if (isAdmin(user)) me.push_back(it("管理面板", "/admin", "🛠"));
    } else {
        me.push_back(it("登录 / 注册", "/login", "🔑"));
        me.push_back(it("排行榜", "/rank", "🏅"));
        me.push_back(it("商城", "/shop", "🛒"));
    }
    me.push_back(it("关于", "/about", "💡"));
    grp("me", "👤", "我的", me);

    sb += "</nav><div style='flex-shrink:0;display:flex;gap:6px;justify-content:center;padding:6px 8px 14px;'>"
          "<button id=\"themeBtn\" class=\"side-theme\" title=\"切换主题\">&#127795;</button>"
          "<button class=\"side-theme\" title=\"背景\" onclick=\"toggleBgSel()\">&#127912;</button></div></aside>";
    return sb;
}

std::string Handlers::getTopBar(User* user) {
    std::string html = "";
    html += "<div class='topstrip'>";
    if (isAdmin(user)) {
        html += "<form class='ts-admin-search' method='GET' action='/admin/users' title='在所有管理页快速查找用户'>"
                "<span>🔍</span><input name='q' placeholder='查找用户（用户名 / id）'/></form>";
    }
    html += "<div class='ts-right'>";
if (user) {
        // 未读统计：排除自己发给自己、以及发送者已注销的消息（否则会出现「看不到来源」的红点）
        int unreadCount = 0;
        DataStore& _navds = DataStore::instance();
        for (const auto& msg : _navds.messages) {
            if (msg.toId != user->id || msg.read) continue;
            if (msg.fromId == user->id) continue;
            if (msg.fromId != "system" && !_navds.findUserById(msg.fromId)) continue;
            unreadCount++;
        }
        html += "<a href='/messages' class='ts-btn' title='\u6d88\u606f'>\u2709";
        if (unreadCount > 0) html += "<span class='ts-badge'>" + std::to_string(unreadCount) + "</span>";
        html += "</a>";
        std::string avatarDisplay;
        if (!user->avatar.empty() && common::isBase64Image(user->avatar)) {
            avatarDisplay = "<img src='" + user->avatar + "' class='nav-avatar'/>";
        } else {
            avatarDisplay = "<span class='nav-avatar-txt'>" + std::string(1, toupper(user->username[0])) + "</span>";
        }
        std::string badge = Handlers::getRatingBadge(user->rating);
        html += "<a href='/user/" + user->id + "' class='user-pill'>" + avatarDisplay + "<span>" + common::htmlEscape(user->username) + " <span class='pill-tag'>" + badge + "</span></span></a>";
        int oiLv = DataStore::calcLevel(user->experience);
        html += "<span class='coin-tag'>&#129472;" + std::to_string(user->gold) + "</span>";
        html += "<span class='coin-tag'>Lv." + std::to_string(oiLv) + " " + DataStore::getLevelBadge(oiLv) + "</span>";
        if (isAdmin(user)) {
            int pendSol = 0;
            for (const auto& pp : DataStore::instance().posts)
                if (pp.type == SOLUTION && pp.auditStatus == "pending") pendSol++;
            html += "<a href='/admin' class='ts-btn' style='color:#d97706;'>&#9881; \u7ba1\u7406";
            if (pendSol > 0) html += "<span class='ts-badge' style='background:#e74c3c;'>" + std::to_string(pendSol) + "</span>";
            html += "</a>";
            if (pendSol > 0) html += "<a href='/admin/solutions?status=pending' class='ts-btn' style='color:#e74c3c;'>\u5f85\u5ba1\u6838\u9898\u89e3" + std::to_string(pendSol) + "</a>";
        }
        html += "<a href='/logout' class='ts-btn'>\u9000\u51fa</a>";
    } else {
        html += "<a href='/login' class='ghost-btn'>\u767b\u5f55</a>";
        html += "<a href='/register' class='btn btn-sm'>\u6ce8\u518c</a>";
    }

    html += "</div></div>";
    return html;
}


// 后台统一顶部导航：按权限过滤，避免每个页面各写一套入口
std::string Handlers::adminNavbar(User* user) {
    if (!isAdmin(user)) return "";
    struct NavItem { const char* label; const char* href; const char* perm; const char* icon; };
    static const NavItem items[] = {
        {"概览", "/admin", "", "\U0001f4ca"},
        {"用户", "/admin/users", "users", "\U0001f465"},
        {"题目", "/admin/problems", "problems", "\U0001f4da"},
        {"内容", "/admin/posts", "content", "\U0001f4c4"},
        {"举报", "/admin/reports", "content", "\U0001f6a9"},
        {"题解", "/admin/solutions", "solutions", "\U0001f9fe"},
        {"比赛", "/admin/contests", "contests", "\U0001f3c6"},
        {"试卷", "/admin/papers", "papers", "\U0001f4dd"},
        {"商城", "/admin/shop", "shop", "\U0001f6d2"},
        {"AI", "/admin/ai", "ai", "\U0001f916"},
        {"系统", "/admin/system", "system", "\U0001f6e1"}
    };
    int pendReports = reports::pendingCount();
    std::string h = "<div class='admin-nav'>";
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        std::string perm = items[i].perm;
        if (!perm.empty() && !adminperm::has(user, perm)) continue;
        h += "<a class='an-item' href='" + std::string(items[i].href) + "'>" + items[i].icon +
             " <span>" + items[i].label + "</span>";
        if (std::string(items[i].href) == "/admin/reports" && pendReports > 0)
            h += "<span class='an-badge'>" + std::to_string(pendReports) + "</span>";
        h += "</a>";
    }
    h += "</div>";
    return h;
}

std::string Handlers::renderTemplate(const std::string& title, const std::string& content, User* user) {
    // 页面外壳模板已抽离到 static/template.html（前后端分离）
    std::string tpl;
    if (common::fileExists("static/template.html")) {
        tpl = common::readFile("static/template.html");
    } else {
        tpl = "<!DOCTYPE html><html><head><meta charset='UTF-8'>\n"
              "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
              "<title>OI小站 - @@TITLE@@</title>\n"
              "<link rel='stylesheet' href='/static/style.css?v=" OISTATION_VERSION "'>\n"
              "<script src='/static/app.js'></script>\n"
              "</head><body>\n@@NAV@@\n"
              "<div class='container'>\n<h1>@@TITLE@@</h1>\n@@CONTENT@@\n</div>\n"
              "<footer class='site-footer'>\u00a9 2026 OI \u5c0f\u7ad9 v @@VERSION@@ \u00b7 \u57fa\u4e8e C++11 \u7684\u5728\u7ebf\u5224\u9898\u7cfb\u7edf</footer>\n"
              "</body></html>";
    }
    tpl = common::replaceAll(tpl, "@@TITLE@@", common::htmlEscape(title));
    tpl = common::replaceAll(tpl, "@@NAV@@", getNav(user));
    tpl = common::replaceAll(tpl, "@@TOPBAR@@", getTopBar(user));
    tpl = common::replaceAll(tpl, "@@VERSION@@", std::string(OISTATION_VERSION));
    tpl = common::replaceAll(tpl, "@@CONTENT@@", content); // 最后替换，避免正文被误替换
    return tpl;
}

// 登录/注册独立居中页（无侧栏）
std::string Handlers::renderAuthPage(const std::string& title, const std::string& subtitle,
                                     const std::string& content) {
    return "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
           "<title>OI小站 - " + common::htmlEscape(title) + "</title>"
           "<link rel='stylesheet' href='/static/style.css'>"
           "<script src='/static/app.js'></script>"
           "</head><body class='auth-body'><div class='auth-wrap'><div class='auth-card'>"
           "<a href='/' class='brand'>OI<span class='dot'>小站</span></a>"
           "<div class='auth-sub'>" + common::htmlEscape(subtitle) + "</div>"
           + content +
           "<div class='auth-foot'><a href='/' style='color:var(--muted);font-size:12.5px;'>&larr; 返回首页</a></div>"
           "</div></div></body></html>";
}

std::string Handlers::handleRoot(const std::string& method, const std::string& path,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers,
                                 std::map<std::string, std::string>& respHeaders,
                                 std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string content;
    
    content += "<div style='display:grid;grid-template-columns:1fr 300px;gap:20px;'>";
    content += "<div>";
    
    // 两大分区入口：学习 / 社区
    content += "<div class='zone-grid'>";
    content += "<a class='zone-card' href='/problems'><div class='zone-ic'>📚</div><div class='zone-tx'><div class='zone-t'>学习</div>"
               "<div class='zone-d'>题库 · 题单 · 试卷 · 比赛 · 提交评测</div></div></a>";
    content += "<a class='zone-card' href='/forum'><div class='zone-ic'>💬</div><div class='zone-tx'><div class='zone-t'>社区</div>"
               "<div class='zone-d'>讨论区 · 博客 · 排行榜 · 对决</div></div></a>";
    content += "</div>";

    // 统计信息
    content += "<div class='grid' style='grid-template-columns:repeat(auto-fill,minmax(180px,1fr));'>";
    content += "<div class='stat-box'><div class='stat-number'>" + 
               std::to_string(DataStore::instance().problems.size()) + "</div>"
               "<div class='stat-label'>📚 题目</div></div>";
    content += "<div class='stat-box'><div class='stat-number'>" + 
               std::to_string(DataStore::instance().users.size()) + "</div>"
               "<div class='stat-label'>👥 用户</div></div>";
    
    int acCount = 0;
    for (const auto& s : DataStore::instance().submissions) {
        if (s.status == AC) acCount++;
    }
    content += "<div class='stat-box'><div class='stat-number'>" + 
               std::to_string(acCount) + "</div>"
               "<div class='stat-label'>✅ AC</div></div>";
    content += "</div>";

    // 进行中的比赛
    content += "<h2>⚡ 进行中的比赛</h2>";
    {
        bool anyLive = false;
        time_t now = time(NULL);
        for (const auto& c : DataStore::instance().contests) {
            time_t cs = common::parseTime(c.startTime), ce = common::parseTime(c.endTime);
            if (now >= cs && now <= ce) {
                anyLive = true;
                content += "<div class='card' style='margin-bottom:8px;'><a href='/contest/" + c.id + "'>" + common::htmlEscape(c.title) + "</a> <span class='pill status-ac'>进行中</span></div>";
            }
        }
        if (!anyLive) content += "<div class='empty-td'>暂无进行中的比赛</div>";
    }

    // 最近通过
    content += "<h2>🏆 最近通过</h2><div class='card'><div style='display:flex;flex-direction:column;gap:8px;'>";
    {
        int rc = 0;
        for (auto it = DataStore::instance().submissions.rbegin(); it != DataStore::instance().submissions.rend() && rc < 8; ++it) {
            if (it->status != AC) continue;
            rc++;
            User* su = DataStore::instance().findUserById(it->userId);
            Problem* sp = DataStore::instance().findProblemById(it->problemId);
            if (!sp) continue;
            content += "<div style='display:flex;justify-content:space-between;gap:8px;font-size:13.5px;'>"
                "<span><a href='/user/" + it->userId + "' class='user-link'>" + common::htmlEscape(su ? su->username : "未知") + "</a> 通过了 <a href='/problem/" + sp->id + "'>" + common::htmlEscape(sp->title) + "</a></span>"
                "<span class='pill status-ac'>AC</span></div>";
        }
        if (rc == 0) content += "<div class='empty-td'>暂无记录</div>";
    }
    content += "</div></div>";

    // 打卡
    if (user) {
        content += "<h2>📅 每日打卡</h2>";
        content += "<div class='card'><p>连续打卡: <strong>" + std::to_string(user->checkinDays) + "</strong> 天</p>"
                   "<form method='POST' action='/checkin' style='display:inline;'>"
                   "<input type='submit' value='✅ 打卡' class='btn btn-success'/></form></div>";
    }
    
    // 最新犇犇
    content += "<h2>🐮 最新犇犇</h2>";
    int count = 0;
    bool benbenOn = loadSystemConfig().allowBenben;
    for (auto it = DataStore::instance().posts.rbegin(); 
         it != DataStore::instance().posts.rend() && count < 10; ++it) {
        if (it->type == BENBEN) {
            User* author = DataStore::instance().findUserById(it->authorId);
            content += "<div class='card'>"
                      "<div><strong>" + (author ? ("<a href='/user/" + author->id + "' class='user-link'>" + common::htmlEscape(author->username) + "</a>") : std::string("已注销")) + 
                      "</strong> <span style='color:#7f8c8d;font-size:14px;'>" + it->createTime + "</span></div>"
                      "<div style='margin-top:8px;'>" + common::htmlEscape(it->content) + "</div></div>";
            count++;
        }
    }
    
    if (count == 0) {
        content += benbenOn ? "<div class='alert alert-info'>还没有犇犇，快来发第一条吧！</div>"
                            : "<div class='alert alert-info'>犇犇功能当前已关闭。</div>";
    }
    
    if (user) {
        if (benbenOn) {
            content += "<div style='margin-top:20px;'>"
                       "<form method='POST' action='/benben'>"
                       "<div class='form-group'>"
                       "<input name='content' placeholder='发布一条犇犇...' style='width:70%;padding:8px;'/>"
                       + security::captchaFieldsFor("benben") +
                       "<input type='submit' value='发布' class='btn'/></div></form></div>";
        } else {
            content += "<div style='margin-top:20px;'><div class='alert alert-warning'>犇犇功能已由管理员关闭。</div></div>";
        }
    }
    
    // ========== 用户 AC 题目列表 ==========
    if (user) {
        content += "<h2 style='margin-top:30px;'>✅ 我 AC 的题目</h2>";
        std::vector<std::pair<std::string, std::string>> acProblems; // problemId -> submissionId
        for (const auto& sub : DataStore::instance().submissions) {
            if (sub.userId == user->id && sub.status == AC) {
                acProblems.push_back({sub.problemId, sub.id});
            }
        }
        // 去重（保留最新的提交）
        std::map<std::string, std::string> uniqueAC;
        for (auto it = acProblems.rbegin(); it != acProblems.rend(); ++it) {
            if (uniqueAC.find(it->first) == uniqueAC.end()) {
                uniqueAC[it->first] = it->second;
            }
        }
        if (uniqueAC.empty()) {
            content += "<div class='alert alert-info'>还没有 AC 的题目，加油！</div>";
        } else {
            content += "<div style='display:flex;flex-wrap:wrap;gap:10px;'>";
            for (const auto& kv : uniqueAC) {
                Problem* p = DataStore::instance().findProblemById(kv.first);
                if (p) {
                    content += "<a href='/submission/" + kv.second + "' class='ac-problem'>✅ " + common::htmlEscape(p->title) + "</a>";
                }
            }
            content += "</div>";
        }
    }
    
    content += "</div>";
    
    // 侧边栏
    content += "<div class='sidebar'>";
    content += "<div style='display:flex;justify-content:space-between;align-items:center;border-bottom:2px solid #667eea;padding-bottom:10px;margin-bottom:10px;'>";
    content += "<h3 style='margin:0;'>📢 公告</h3>";
    if (isAdmin(user)) {
        content += "<a href='/admin/announcement' class='btn btn-sm' style='font-size:12px;padding:2px 10px;'>✏️</a>";
    }
    content += "</div>";
    content += "<div style='font-size:14px;line-height:1.8;color:#555;'>" + DataStore::instance().announcement + "</div>";
    
    content += "<h3 style='margin-top:20px;border-bottom:2px solid #667eea;padding-bottom:10px;'>🔗 快速链接</h3>";
    content += "<div style='font-size:11px;font-weight:700;color:#98a4b3;letter-spacing:1px;margin:8px 0 6px;'>学习</div>";
    content += "<div style='display:flex;flex-direction:column;gap:8px;'>";
    content += "<a href='/problems' class='btn' style='text-align:center;'>📚 题库</a>";
    content += "<a href='/papers' class='btn' style='text-align:center;'>📝 试卷</a>";
    content += "<a href='/sets' class='btn' style='text-align:center;'>📖 题单</a>";
    content += "<a href='/contests' class='btn' style='text-align:center;'>🏆 比赛</a>";
    content += "</div>";
    content += "<div style='font-size:11px;font-weight:700;color:#98a4b3;letter-spacing:1px;margin:14px 0 6px;'>社区</div>";
    content += "<div style='display:flex;flex-direction:column;gap:8px;'>";
    content += "<a href='/forum' class='btn' style='text-align:center;'>💬 讨论区</a>";
    content += "<a href='/blog' class='btn' style='text-align:center;'>✎ 博客</a>";
    content += "<a href='/rank' class='btn' style='text-align:center;'>🏅 排行榜</a>";
    content += "</div>";
    if (user) {
        content += "<div style='font-size:11px;font-weight:700;color:#98a4b3;letter-spacing:1px;margin:14px 0 6px;'>我的</div>";
        content += "<div style='display:flex;flex-direction:column;gap:8px;'>";
        content += "<a href='/status' class='btn' style='text-align:center;'>📊 我的提交</a>";
        content += "<a href='/user/" + user->id + "' class='btn' style='text-align:center;'>👤 个人主页</a>";
        content += "<a href='/user/settings' class='btn' style='text-align:center;'>⚙️ 设置</a>";
        content += "</div>";
    }
    
    content += "</div></div>";
    
    return renderTemplate("首页", content, user);
}

// 由于代码量巨大，后续函数（登录、注册、题库分页、比赛分页等）将在下一条回复中继续提供
// 请回复"继续"获取完整的剩余代码

// ============================================================
// 登录
// ============================================================
std::string Handlers::handleLogin(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId) {
    if (method == "GET") {
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>用户名</label><input name='username' required/></div>"
                             "<div class='form-group'><label>密码</label><input type='password' name='password' required/></div>"
                             + security::captchaFieldsFor("login", headers) +
"<input type='submit' value='登录' class='btn btn-block' style='width:100%;'/> <a href='/register' class='btn btn-outline btn-block' style='margin-top:6px;'>注册新账号</a></form>";
        return renderAuthPage("登录", "登录 OI 小站，继续你的刷题之旅", content);
    } else {
        std::string username = parseFormData(body, "username");
        std::string password = parseFormData(body, "password");
        std::string captchaToken = parseFormData(body, "captcha_token");
        std::string captchaAnswer = parseFormData(body, "captcha_answer");
        
        if (!security::verifyCaptchaFor(captchaToken, captchaAnswer, "login")) {
            std::string content = "<div class='alert alert-error'>验证码错误或已过期</div><a href='/login' class='btn'>重新登录</a>";
            return renderTemplate("登录失败", content, nullptr);
        }
        
        User* user = DataStore::instance().findUserByName(username);
        if (user && common::passwordVerify(password, user->password)) {
            // 旧明文密码：登录成功后自动升级为加盐哈希
            if (!common::passwordIsHashed(user->password)) {
                user->password = common::passwordHash(password);
                DataStore::instance().saveUsers();
            }
            security::recordIpActivity(clientIpOf(headers), user->username);
            std::string sid = SessionManager::instance().createSession(user->id);
            respHeaders["Set-Cookie"] = "SESSION=" + sid + "; HttpOnly; Path=/";
            return "<script>window.location.href='/'</script>";
        }
        
        std::string content = "<div class='alert alert-error'>用户名或密码错误</div><a href='/login' class='btn'>重新登录</a>";
        return renderTemplate("登录失败", content, nullptr);
    }
}

// ============================================================
// 注册
// ============================================================
std::string Handlers::handleRegister(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    if (!loadSystemConfig().allowRegister) {
        std::string c = "<div class='alert alert-info'>\u6ce8\u518c\u5df2\u5173\u95ed\uff0c\u8bf7\u8054\u7cfb\u7ba1\u7406\u5458</div><a href='/login' class='btn'>\u53bb\u767b\u5f55</a>";
        return renderTemplate("\u6ce8\u518c\u5df2\u5173\u95ed", c, nullptr);
    }
    if (method == "GET") {
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>用户名 (至少3个字符)</label><input name='username' required minlength='3'/></div>"
                             "<div class='form-group'><label>密码 (至少6个字符)</label><input type='password' name='password' required minlength='6'/></div>"
                             "<div class='form-group'><label>确认密码</label><input type='password' name='confirm_password' required minlength='6'/></div>"
                             + security::captchaFieldsFor("register", headers) +
"<input type='submit' value='注册' class='btn btn-block' style='width:100%;'/> <a href='/login' class='btn btn-outline btn-block' style='margin-top:6px;'>已有账号？去登录</a></form>";
        return renderAuthPage("注册", "注册 OI 小站账号", content);
    } else {
        std::string username = parseFormData(body, "username");
        std::string password = parseFormData(body, "password");
        std::string confirmPassword = parseFormData(body, "confirm_password");
        std::string captchaToken = parseFormData(body, "captcha_token");
        std::string captchaAnswer = parseFormData(body, "captcha_answer");
        
        int _ul = security::getSecConfig().maxUsername;
        if ((int)username.length() > _ul) username = username.substr(0, _ul);
        
        if (!security::verifyCaptchaFor(captchaToken, captchaAnswer, "register")) {
            std::string content = "<div class='alert alert-error'>验证码错误或已过期</div><a href='/register' class='btn'>返回</a>";
            return renderTemplate("注册失败", content, nullptr);
        }
        
        if (security::throttleAction("register:" + clientIpOf(headers), security::getSecConfig().registerIntervalSec)) {
            std::string content = "<div class='alert alert-error'>该IP注册过于频繁，请稍后再试</div><a href='/register' class='btn'>返回</a>";
            return renderTemplate("注册失败", content, nullptr);
        }
        
        if (username.length() < 3 || password.length() < 6 || password != confirmPassword) {
            std::string error = password != confirmPassword ? "两次密码不一致" : "用户名至少3字符，密码至少6字符";
            std::string content = "<div class='alert alert-error'>" + error + "</div><a href='/register' class='btn'>返回</a>";
            return renderTemplate("注册失败", content, nullptr);
        }
        
        if (DataStore::instance().findUserByName(username)) {
            std::string content = "<div class='alert alert-error'>用户名已存在</div><a href='/register' class='btn'>返回</a>";
            return renderTemplate("注册失败", content, nullptr);
        }
        
        User newUser;
        newUser.id = common::genId();
        newUser.username = username;
        newUser.password = common::passwordHash(password);
        newUser.registerTime = common::getTimeStr();
        newUser.checkinDays = 0;
        newUser.lastCheckin = "";
        newUser.avatar = "";
        newUser.bio = "这个人很懒，什么都没写";
        newUser.role = DataStore::instance().users.empty() ? ROLE_ADMIN : ROLE_USER;
        newUser.rating = 1500;
        newUser.maxRating = 1500;
        newUser.ratedMatches = 0;
        newUser.experience = 0;
        newUser.gold = 100;
        // 新用户默认套餐（管理员可在套餐页设置）
        if (newUser.role != ROLE_ADMIN) {
            std::string dp = aiplan::defaultPlanId();
            if (!dp.empty()) aiplan::applyPlan(newUser, dp);
        }
        
        DataStore::instance().users.push_back(newUser);
        DataStore::instance().saveUsers();
        security::recordIpActivity(clientIpOf(headers), newUser.username);
        
        std::string roleMsg = newUser.role == ROLE_ADMIN ? " (管理员)" : "";
        std::string content = "<div class='alert alert-success'>注册成功！" + roleMsg + "</div><a href='/login' class='btn'>去登录</a>";
        return renderTemplate("注册成功", content, nullptr);
    }
}

// ============================================================
// 登出
// ============================================================
std::string Handlers::handleLogout(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId) {
    if (!sessionId.empty()) {
        SessionManager::instance().destroySession(sessionId);
        respHeaders["Set-Cookie"] = "SESSION=; Max-Age=0; Path=/";
    }
    return "<meta http-equiv='refresh' content='0;url=/'>";
}

// ============================================================
// 题库（支持分页）
// ============================================================
std::string Handlers::handleProblems(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    bool isManager = canManageProblems(user);

    int page = 1;
    int perPage = 20;
    bool searchMode = path.find("/problems/search") != std::string::npos;
    std::string kw;
    if (searchMode) {
        size_t qp = path.find("q=");
        if (qp != std::string::npos) {
            size_t amp = path.find('&', qp);
            std::string raw = path.substr(qp + 2, (amp == std::string::npos) ? std::string::npos : amp - (qp + 2));
            kw = common::urlDecode(raw);
        }
        size_t pp = path.find("page=");
        if (pp != std::string::npos) page = common::stringToInt(path.substr(pp + 5));
    } else if (path.find("/problems/page/") != std::string::npos) {
        size_t lastSlash = path.find_last_of('/');
        page = common::stringToInt(path.substr(lastSlash + 1));
    }
    if (page < 1) page = 1;

    std::string kwLower = kw;
    std::transform(kwLower.begin(), kwLower.end(), kwLower.begin(), ::tolower);

    std::vector<Problem*> visibleProblems;
    for (auto& p : DataStore::instance().problems) {
        if (p.hidden) continue;
        if (searchMode && !kw.empty()) {
            std::string idL = p.id; std::transform(idL.begin(), idL.end(), idL.begin(), ::tolower);
            std::string tl = p.title; std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
            if (idL.find(kwLower) == std::string::npos && tl.find(kwLower) == std::string::npos) continue;
        }
        visibleProblems.push_back(&p);
    }

    int total = (int)visibleProblems.size();
    int totalPages = (total + perPage - 1) / perPage;
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    int start = (page - 1) * perPage;
    int end = std::min(start + perPage, total);

    std::string content;
    // 搜索框
    content += "<div class='search-bar' style='margin-bottom:15px;'>"
        "<form method='get' action='/problems/search' style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;'>"
        "<input name='q' value='" + common::htmlEscape(kw) + "' placeholder='搜索题号 / 标题…' style='flex:1;min-width:200px;'/>"
        "<button class='btn'>&#128269; 搜索</button>"
        "<a href='/problems' class='btn btn-outline'>&#10006;</a>"
        "</form></div>";

    content += "<div class='action-bar' style='margin-bottom:15px;'>";
    if (user) content += "<a href='/problems/add' class='btn btn-success'>&#9997;&#65039; 出题</a> ";
    if (isManager) content += "<a href='/admin/problems/add' class='btn'>&#10133; 添加题目</a> ";
    content += "<a href='/myproblem' class='btn btn-outline'>&#128203; 我的题目</a> ";
    content += "<span style='margin-left:12px;color:#7f8c8d;'>共 " + std::to_string(total) + " 道题目"
        + (searchMode ? "（搜索: " + common::htmlEscape(kw) + "）" : "") + "</span></div>";

    content += "<table><thead><tr><th>#</th><th>标题</th><th>通过率</th><th>提交</th>";
    if (isManager) content += "<th>状态</th><th>操作</th>";
    content += "</tr></thead><tbody>";

    for (int i = start; i < end; ++i) {
        Problem* p = visibleProblems[i];
        int ac = DataStore::instance().getProblemACCount(p->id);
        int totalSub = DataStore::instance().getProblemSubmitCount(p->id);
        double rate = totalSub > 0 ? (double)ac / totalSub * 100 : 0;
        std::string rowClass = p->hidden ? " class='hidden-problem'" : "";
        content += "<tr" + rowClass + "><td><strong>" + p->id + "</strong></td>";
        content += "<td><a href='/problem/" + p->id + "'>" + common::htmlEscape(p->title) + "</a>";
        if (p->hidden) content += " <span style='color:#e74c3c;font-size:12px;'>(隐藏)</span>";
        content += "</td>";
        content += "<td>" + std::to_string(ac) + "/" + std::to_string(totalSub) + " (" + std::to_string((int)rate) + "%)</td>";
        content += "<td>" + std::to_string(totalSub) + "</td>";
        if (isManager) {
            std::string statusText = p->hidden ? "🔒 隐藏" : "👁️ 公开";
            content += "<td>" + statusText + "</td><td>";
            content += "<a href='/admin/problem/edit/" + p->id + "' class='btn btn-sm btn-outline'>✏️</a>";
            content += "<form method='POST' action='/admin/problem/toggle' style='display:inline;'><input type='hidden' name='problemId' value='" + p->id + "'/><input type='submit' value='" + (p->hidden ? "🔓 显示" : "🔒 隐藏") + "' class='btn btn-sm' style='background:" + (p->hidden ? "#27ae60" : "#e67e22") + ";'/></form>";
            content += "<form method='POST' action='/admin/problem/delete' style='display:inline;'><input type='hidden' name='problemId' value='" + p->id + "'/><input type='submit' value='🗑️' class='btn btn-sm btn-danger' onclick='return confirm(\"确定要删除吗？\")'/></form>";
            content += "</td>";
        }
        content += "</tr>";
    }
    content += "</tbody></table>";

    if (visibleProblems.empty()) {
        content = "<div class='search-bar' style='margin-bottom:15px;'><form method='get' action='/problems/search' style='display:flex;gap:8px;'><input name='q' value='" + common::htmlEscape(kw) + "' placeholder='搜索题号 / 标题…' style='flex:1;'/><button class='btn'>&#128269; 搜索</button></form></div>"
            + "<div class='empty-state'><span class='icon'>📚</span>" + (searchMode ? "未找到匹配的题目" : "暂无题目") + "</div>";
    }

    if (!searchMode) {
        content += renderPagination("/problems/page", page, totalPages);
    } else {
        std::string enc = common::urlEncode(kw);
        content += "<div class='pagination' style='margin-top:20px;display:flex;gap:6px;justify-content:center;flex-wrap:wrap;'>";
        if (page > 1) content += "<a class='btn btn-sm' href='/problems/search?q=" + enc + "&page=" + std::to_string(page - 1) + "'>&#8592; 上一页</a>";
        for (int pi = 1; pi <= totalPages; ++pi) {
            content += "<a class='btn btn-sm" + std::string(pi == page ? " cur" : "") + "' href='/problems/search?q=" + enc + "&page=" + std::to_string(pi) + "'>" + std::to_string(pi) + "</a>";
        }
        if (page < totalPages) content += "<a class='btn btn-sm' href='/problems/search?q=" + enc + "&page=" + std::to_string(page + 1) + "'>下一页 &#8594;</a>";
        content += "</div>";
    }
    return renderTemplate("题库", content, user);
}

// 题目详情 helper 前置声明
static void buildProblemStatistics(std::string& content, Problem* problem, User* user);
static void buildProblemSubmit(std::string& content, Problem* problem, User* user, const std::string& contestId);
static void buildProblemDiscuss(std::string& content, Problem* problem, User* user);
// ============================================================
// 题目详情
// ============================================================
std::string Handlers::handleProblemDetail(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string contestId;
    // 路径形如 /problem/<id> 或 /problem/<id>/submit|statistics|discuss
    std::string problemId, tab = "statement";
    {
        std::string seg = path;
        size_t q = seg.find('?');
        if (q != std::string::npos) seg = seg.substr(0, q);
        size_t slash = seg.find('/', 9); // 跳过 "/problem/"
        if (slash != std::string::npos) {
            problemId = seg.substr(9, slash - 9);
            std::string tail = seg.substr(slash + 1);
            if (tail == "submit" || tail == "statistics" || tail == "discuss") tab = tail;
        } else {
            problemId = seg.substr(9);
        }
        // 兼容 /problem/<id>?contest_id=xxx
        if (q != std::string::npos) {
            std::string query = path.substr(q + 1);
            size_t cq = query.find("contest_id=");
            if (cq != std::string::npos) contestId = query.substr(cq + 11);
        }
    }
    
    Problem* problem = DataStore::instance().findProblemById(problemId);
    if (!problem) {
        std::string content = "<div class='alert alert-error'>题目不存在</div><a href='/problems' class='btn'>返回题库</a>";
        return renderTemplate("错误", content, user);
    }
    
    // 检查隐藏题目权限
    if (problem->hidden) {
        bool canAccess = isAdmin(user);
        if (!canAccess && !contestId.empty() && user) {
            Contest* contest = DataStore::instance().findContestById(contestId);
            if (contest) {
                bool isParticipant = false;
                for (const auto& pid : contest->participants) {
                    if (pid == user->id) { isParticipant = true; break; }
                }
                bool hasProblem = false;
                for (const auto& cp : contest->problems) {
                    if (cp.problemId == problemId) { hasProblem = true; break; }
                }
                if (isParticipant && hasProblem) canAccess = true;
            }
        }
        if (!canAccess) {
            std::string content = "<div class='alert alert-error'>该题目已被隐藏，无法直接访问</div>"
                                 "<a href='/problems' class='btn'>返回题库</a>";
            return renderTemplate("题目不可见", content, user);
        }
    }
    
    // 开始构建内容
    std::string content = "";
    content += "<div style='margin-bottom:20px;'>";
    content += "<h2>" + common::htmlEscape(problem->title) + "</h2>";
    content += "<div style='color:#7f8c8d;font-size:14px;'>";
    content += "作者: " + common::htmlEscape(problem->author);
    content += " | 时间限制: " + std::to_string(problem->timeLimit) + "ms";
    content += " | 内存限制: " + std::to_string(problem->memLimit) + "MB";
    if (!problem->spj.empty()) content += " | <span class='pill' style='background:#f3eafd;color:#8e44ad;'>SPJ 特殊评测</span>";
    if (problem->hidden) content += " | <span style='color:#e67e22;'>🔒 隐藏</span>";
    if (!contestId.empty()) content += " | <a href='/contest/" + contestId + "'>← 返回比赛</a>";
    content += "</div></div>";

    if (isProblemOwner(problem, user) || canManageProblems(user)) {
        content += "<div style='margin-bottom:14px;display:flex;gap:8px;flex-wrap:wrap;'>"
            "<a href='/myproblem/edit/" + problem->id + "' class='btn btn-sm btn-outline'>&#9999;&#65039; 编辑</a>"
            "<a href='/myproblem/data/" + problem->id + "' class='btn btn-sm btn-outline'>&#128190; 数据</a>"
            "<form method='POST' action='/myproblem/toggle' style='display:inline;'><input type='hidden' name='problemId' value='" + problem->id + "'/><input type='submit' value='" + (problem->hidden ? "&#128275; 显示" : "&#128274; 隐藏") + "' class='btn btn-sm' style='background:" + (problem->hidden ? "#27ae60" : "#e67e22") + ";'/></form>"
            "<form method='POST' action='/myproblem/delete' style='display:inline;' onsubmit='return confirm(\"确定删除该题目？\")'><input type='hidden' name='problemId' value='" + problem->id + "'/><input type='submit' value='&#128465;&#65039; 删除' class='btn btn-sm btn-danger'/></form>"
            "</div>";
    }
    if (isAdmin(user)) {
        content += "<div style='margin-bottom:14px;'><form method='POST' action='/admin/problem/solution-toggle' style='display:inline;'><input type='hidden' name='problemId' value='" + problem->id + "'/><input type='submit' value='" + (problem->solutionClosed ? "🔓 开启题解入口" : "🔒 关闭题解入口") + "' class='btn btn-sm btn-outline'/></form>"
                   " <a href='/solutions?problem=" + problem->id + "' class='btn btn-sm btn-outline'>📝 查看/管理本题题解</a></div>";
    }
    content += "<div style='margin-bottom:14px;'><a href='/solutions?problem=" + problem->id + "' class='btn btn-sm btn-outline'>📝 题解</a>"
               + (user && !problem->solutionClosed ? " <a href='/solution/new/" + problem->id + "' class='btn btn-sm'>✏️ 写题解</a>" : std::string(""))
               + (problem->solutionClosed ? " <span style='color:#e67e22;font-size:13px;'>🔒 本题题解入口已关闭</span>" : "")
               + "</div>";
    std::string pb = "/problem/" + problem->id;
    std::string tb = (tab=="statement") ? "prob-tab active" : "prob-tab";
    std::string tab2 = (tab=="statistics") ? "prob-tab active" : "prob-tab";
    std::string tab3 = (tab=="submit") ? "prob-tab active" : "prob-tab";
    std::string tab4 = (tab=="discuss") ? "prob-tab active" : "prob-tab";
    content += "<div class='prob-tabs'>"
        "<a class='" + tb + "' href='" + pb + "'>\U0001f4d6 \u9898\u9762</a>"
        "<a class='" + tab2 + "' href='" + pb + "/statistics'>\U0001f4ca \u7edf\u8ba1</a>"
        "<a class='" + tab3 + "' href='" + pb + "/submit'>\U0001f4ac \u63d0\u4ea4</a>"
        "<a class='prob-tab' href='/run'>\U0001f9ea \u81ea\u5b9a\u4e49\u6d4b\u8bd5</a>"
        "<a class='" + tab4 + "' href='" + pb + "/discuss'>\U0001f4ac \u8ba8\u8bba</a>"
        "</div>";

    if (tab == "statistics") {
        buildProblemStatistics(content, problem, user);
        return renderTemplate("\u7edf\u8ba1 - " + problem->title, content, user);
    }
    if (tab == "submit") {
        buildProblemSubmit(content, problem, user, contestId);
        return renderTemplate("\u63d0\u4ea4 - " + problem->title, content, user);
    }
    if (tab == "discuss") {
        buildProblemDiscuss(content, problem, user);
        return renderTemplate("\u8ba8\u8bba - " + problem->title, content, user);
    }

    // ---------- 默认：题面 ----------
    // 题目图片
    if (!problem->image.empty() && common::isBase64Image(problem->image)) {
        content += "<div style='text-align:center;margin:10px 0;'><img src='" + problem->image + "' style='max-width:100%;max-height:400px;border-radius:8px;'/></div>";
    }
    content += "<div style='margin:20px 0;'><h3>\u9898\u76ee\u63cf\u8ff0</h3>";
    content += "<div class='md-body'>" + common::markdownToHtml(problem->description) + "</div></div>";
    if (!problem->inputDesc.empty()) {
        content += "<div style='margin:20px 0;'><h3>\u8f93\u5165\u683c\u5f0f</h3>";
        content += "<div class='md-body'>" + common::markdownToHtml(problem->inputDesc) + "</div></div>";
    }
    if (!problem->outputDesc.empty()) {
        content += "<div style='margin:20px 0;'><h3>\u8f93\u51fa\u683c\u5f0f</h3>";
        content += "<div class='md-body'>" + common::markdownToHtml(problem->outputDesc) + "</div></div>";
    }
    if (!problem->samples.empty()) {
        content += "<h3>\u6837\u4f8b</h3>";
        for (size_t i = 0; i + 1 < problem->samples.size(); i += 2) {
            content += "<div style='background:var(--surface-2);padding:12px;margin:10px 0;border-radius:8px;'>";
            content += "<strong>\u8f93\u5165:</strong><pre style='background:var(--surface);padding:10px;border:1px solid var(--border-soft);white-space:pre-wrap;border-radius:6px;'>" + common::htmlEscape(problem->samples[i]) + "</pre>";
            content += "<strong>\u8f93\u51fa:</strong><pre style='background:var(--surface);padding:10px;border:1px solid var(--border-soft);white-space:pre-wrap;border-radius:6px;'>" + common::htmlEscape(problem->samples[i+1]) + "</pre>";
            content += "</div>";
        }
    }
    return renderTemplate("\u9898\u76ee - " + problem->title, content, user);
}

// ---------- 题目统计板块 ----------
static void buildProblemStatistics(std::string& content, Problem* problem, User* user) {
    int acTotal = DataStore::instance().getProblemACCount(problem->id);
    int subTotal = 0;
    for (const auto& s : DataStore::instance().submissions)
        if (s.problemId == problem->id) subTotal++;
    std::string myState = "\u672a\u63d0\u4ea4";
    bool solved = false, tried = false;
    if (user) {
        for (const auto& s : DataStore::instance().submissions) {
            if (s.userId == user->id && s.problemId == problem->id) {
                tried = true;
                if (s.status == AC) { solved = true; break; }
            }
        }
        myState = solved ? "\u2705 \u5df2 AC" : (tried ? "\u274c \u5c1d\u8bd5\u4e2d" : "\u672a\u63d0\u4ea4");
    }
    std::string acPct;
    { double pct = subTotal>0 ? ((double)acTotal*100.0/subTotal) : 0.0; int tp=(int)(pct*10+0.5); acPct=std::to_string(tp/10)+"."+std::to_string(tp%10); }
    content += "<h3 style='border-bottom:1px solid var(--border-soft);padding-bottom:8px;margin-bottom:14px;'>\U0001f4ca \u7edf\u8ba1</h3>";
    std::string myCard;
    if (user) myCard = "<div class='stat-card'><span class='stat-num' style='font-size:15px;'>" + common::htmlEscape(myState) + "</span><span class='stat-lbl'>\U0001f464 \u6211\u7684\u72b6\u6001</span></div>";
    content += "<div class='stat-grid'>"
        "<div class='stat-card'><span class='stat-num'>" + std::to_string(acTotal) + "</span><span class='stat-lbl'>\u2705 \u63d0\u4ea4 AC \u603b\u6570</span></div>"
        "<div class='stat-card'><span class='stat-num'>" + std::to_string(subTotal) + "</span><span class='stat-lbl'>\u2757 \u5168\u90e8\u63d0\u4ea4</span></div>"
        "<div class='stat-card'><span class='stat-num'>" + acPct + "%</span><span class='stat-lbl'>\U0001f4c8 \u901a\u8fc7\u7387</span></div>"
        + myCard +
        "</div>";
    std::vector<const Submission*> subs;
    for (const auto& s : DataStore::instance().submissions)
        if (s.problemId == problem->id && s.contestId.empty()) subs.push_back(&s);
    std::sort(subs.begin(), subs.end(), [](const Submission* a, const Submission* b){ return a->submitTime > b->submitTime; });
    content += "<div style='margin-top:18px;'><div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;'><b>\U0001f4dd \u6700\u65b0\u63d0\u4ea4</b>"
        "<a href='/status?problem=" + problem->id + "' class='btn btn-sm btn-outline'>\u67e5\u770b\u5168\u90e8</a></div>";
    if (subs.empty()) content += "<div class='empty-state' style='padding:16px;'>\u6682\u65e0\u63d0\u4ea4</div>";
    else {
        content += "<table><thead><tr><th>\u7528\u6237</th><th>\u72b6\u6001</th><th>\u5206\u6570</th><th>\u8bed\u8a00</th><th>\u65f6\u95f4</th></tr></thead><tbody>";
        int shown = 0;
        for (auto it = subs.begin(); it != subs.end() && shown < 10; ++it, ++shown) {
            const Submission* s = *it;
            User* u = DataStore::instance().findUserById(s->userId);
            std::string st;
            switch(s->status){case AC:st="Accepted";break;case WA:st="Wrong Answer";break;case TLE:st="Time Limit Exceeded";break;case MLE:st="Memory Limit Exceeded";break;case CE:st="Compile Error";break;case RE:st="Runtime Error";break;case PENDING:st="Pending";break;case JUDGING:st="Judging";break;default:st="?";}
            std::string cls = s->status==AC?"ac":(s->status==PENDING?"pending":(s->status==CE?"ce":"wa"));
            content += "<tr><td><a href='/user/" + s->userId + "' class='user-link'>" + (u?common::htmlEscape(u->username):std::string("\u5df2\u6ce8\u9500")) + "</a></td>"
                "<td><span class='pill status-" + cls + "'>" + common::htmlEscape(st) + "</span></td>"
                "<td>" + std::to_string(s->score) + "</td><td>" + common::htmlEscape(s->language) + "</td>"
                "<td class='time-cell'>" + s->submitTime + "</td></tr>";
        }
        content += "</tbody></table>";
    }
    content += "</div>";
}

// ---------- 题目提交板块 ----------
static void buildProblemSubmit(std::string& content, Problem* problem, User* user, const std::string& contestId) {
    if (!user) {
        content += "<div class='alert alert-info'><a href='/login'>\u767b\u5f55</a> \u540e\u53ef\u4ee5\u63d0\u4ea4\u4ee3\u7801</div>";
        return;
    }
    std::string langOptions;
    for (const auto& l : DataStore::instance().languages) if (l.enabled) langOptions += "<option value='" + l.id + "'>" + common::htmlEscape(l.name) + "</option>";
    if (langOptions.empty()) langOptions = "<option value='cpp'>C++</option>";
    content += "<h3 style='margin-bottom:12px;'>\U0001f4ac \u63d0\u4ea4\u4ee3\u7801</h3>"
        "<form method='POST' action='/submit'>"
        "<div class='form-group'><label>\u8bed\u8a00</label><select name='language'>" + langOptions + "</select></div>"
        "<div class='form-group'><label>\u4ee3\u7801</label><textarea name='code' rows='16' placeholder='// \u5728\u6b64\u7c98\u8d34\u60a8\u7684\u4ee3\u7801' required></textarea></div>"
        "<input type='hidden' name='problemId' value='" + problem->id + "'/>"
        + (contestId.empty() ? "" : std::string("<input type='hidden' name='contestId' value='") + common::htmlEscape(contestId) + "'/>")
        + security::captchaFieldsFor("submit") +
        "<input type='submit' value='\u63d0\u4ea4' class='btn'/>"
        "</form>";
}

// ---------- 题目讨论板块（仅评论） ----------
static void buildProblemDiscuss(std::string& content, Problem* problem, User* user) {
    content += "<h3 style='margin-bottom:6px;'>\U0001f4ac \u8ba8\u8bba</h3>";
    if (user) {
        content += "<form method='POST' action='/problem/discuss' style='margin-bottom:16px;'>"
            "<input type='hidden' name='problemId' value='" + problem->id + "'/>"
            "<textarea name='content' rows='3' placeholder='\u53d1\u8868\u4f60\u7684\u770b\u6cd5\uff08\u4ec5\u8bc4\u8bba\uff09...' required style='width:100%;box-sizing:border-box;'></textarea>"
            + security::captchaFieldsFor("discuss") +
            "<div style='margin-top:8px;'><input type='submit' value='\u53d1\u5e03\u8bc4\u8bba' class='btn btn-sm'/></div></form>";
    } else {
        content += "<div class='alert alert-info' style='margin-bottom:16px;'><a href='/login'>\u767b\u5f55</a> \u540e\u53ef\u4ee5\u8bc4\u8bba</div>";
    }
    if (problem->comments.empty()) { content += "<div class='empty-state' style='padding:14px;'>\u8fd8\u6ca1\u6709\u8bc4\u8bba</div>"; return; }
    // 最新在前
    std::vector<std::string> cs = problem->comments;
    std::reverse(cs.begin(), cs.end());
    for (const auto& c : cs) {
        auto o = common::jsonParseObject(c);
        std::string uid = o.count("uid")?o["uid"]:"";
        std::string uname = o.count("user")?o["user"]:"\u5df2\u6ce8\u9500";
        std::string tm = o.count("time")?o["time"]:"";
        std::string ct = o.count("content")?o["content"]:"";
        content += "<div class='comment-card'>"
            "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;'>"
            "<b><a href='/user/" + uid + "' class='user-link'>" + common::htmlEscape(uname) + "</a></b>"
            "<span style='color:var(--muted);font-size:12px;'>" + common::htmlEscape(tm) + "</span></div>"
            "<div class='md-body' style='font-size:14px;'>" + common::markdownToHtml(ct) + "</div></div>";
    }
}
// ---------- 题目讨论评论提交 ----------
std::string Handlers::handleProblemDiscussPost(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string pid = common::trim(parseFormData(body, "problemId"));
    std::string content = common::trim(parseFormData(body, "content"));
    Problem* problem = DataStore::instance().findProblemById(pid);
    if (!problem) return "<script>alert('题目不存在');history.back()</script>";
    if (content.empty()) return "<script>alert('评论不能为空');history.back()</script>";
    if (content.size() > 1000) return "<script>alert('评论过长');history.back()</script>";
    if (!security::verifyCaptchaFor(parseFormData(body,"captcha_token"), parseFormData(body,"captcha_answer"), "discuss")) {
        return "<script>alert('验证码错误');history.back()</script>";
    }
    std::string now = common::getTimeStr();
    problem->comments.push_back(common::jsonStringify(std::map<std::string,std::string>{
        {"uid", user->id}, {"user", user->username}, {"time", now}, {"content", content}
    }));
    DataStore::instance().saveProblems();
    return "<script>window.location.href='/problem/" + pid + "/discuss'</script>";
}


std::string Handlers::handleSubmit(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId) {
    bool _ajax = false;
    {
        auto _xh = headers.find("x-requested-with");
        if (_xh != headers.end() && _xh->second == "fetch") _ajax = true;
    }
    if (_ajax) respHeaders["Content-Type"] = "application/json; charset=utf-8";
    if (!loadSystemConfig().allowSubmit) {
        if (_ajax) return "{\"ok\":false,\"msg\":\"提交已关闭\"}";
        std::string c = "<div class='alert alert-info'>\u63d0\u4ea4\u5df2\u5173\u95ed</div><a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a>";
        return renderTemplate("\u63d0\u4ea4\u5df2\u5173\u95ed", c, getCurrentUser(sessionId));
    }
    User* user = getCurrentUser(sessionId);
    if (!user) {
        if (_ajax) return "{\"ok\":false,\"msg\":\"请先登录\",\"login\":true}";
        return "<script>window.location.href='/login'</script>";
    }
    
    std::string problemId = parseFormData(body, "problemId");
    std::string language = parseFormData(body, "language");
    std::string code = parseFormData(body, "code");
    std::string contestId = parseFormData(body, "contestId");
    
    if (code.empty()) {
        if (_ajax) return "{\"ok\":false,\"msg\":\"代码不能为空\"}";
        std::string content = "<div class='alert alert-error'>代码不能为空</div><a href='/problem/" + problemId + "' class='btn'>返回</a>";
        return renderTemplate("提交失败", content, user);
    }
    if (!contestId.empty()) {
        for (auto& _s : DataStore::instance().submissions) {
            if (_s.userId == user->id && _s.problemId == problemId && _s.contestId == contestId && _s.locked) {
                if (_ajax) return "{\"ok\":false,\"msg\":\"该题已锁定，不能再提交\"}";
                std::string content = "<div class='alert alert-error'>该题已锁定，不能再提交</div><a href='/contest/" + contestId + "' class='btn'>返回</a>";
                return renderTemplate("提交失败", content, user);
            }
        }
    }
    int _cl = security::getSecConfig().maxCode;
    if ((int)code.length() > _cl) code = code.substr(0, _cl);
    
    if (!security::verifyCaptchaFor(parseFormData(body, "captcha_token"), parseFormData(body, "captcha_answer"), "submit")) {
        if (_ajax) return "{\"ok\":false,\"msg\":\"验证码错误或已过期\",\"captcha\":true}";
        std::string c = "<div class='alert alert-error'>\u9a8c\u8bc1\u7801\u9519\u8bef\u6216\u5df2\u8fc7\u671f</div><a href='/problem/" + problemId + "' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u63d0\u4ea4\u5931\u8d25", c, user);
    }
    if (security::throttleAction("submit:" + user->id, security::getSecConfig().submitIntervalSec)) {
        if (_ajax) return "{\"ok\":false,\"msg\":\"提交过于频繁，请稍后再试\"}";
        std::string c = "<div class='alert alert-error'>提交过于频繁，请稍后再试</div><a href='/problem/" + problemId + "' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u63d0\u4ea4\u5931\u8d25", c, user);
    }
    security::recordIpActivity(clientIpOf(headers), user->username);
    Submission sub;
    sub.id = common::genId();
    sub.problemId = problemId;
    sub.userId = user->id;
    sub.code = code;
    // 校验语言：不存在或未启用时回退到第一个启用的语言
    LangConfig* lang = DataStore::instance().findLangConfig(language);
    if (!lang || !lang->enabled) {
        lang = nullptr;
        for (auto& ll : DataStore::instance().languages) { if (ll.enabled) { lang = &ll; break; } }
    }
    sub.language = lang ? lang->id : (language.empty() ? "cpp" : language);
    sub.status = PENDING;
    sub.usedTime = 0;
    sub.usedMem = 0;
    sub.submitTime = common::getTimeStr();
    sub.judgeResult = "等待评测...";
    sub.contestId = contestId;
    
    DataStore::instance().submissions.push_back(sub);
    DataStore::instance().saveSubmissions();
    
    if (!loadSystemConfig().allowJudge) {
        sub.status = PENDING;
        sub.judgeResult = "\u8bc4\u6d4b\u529f\u80fd\u5df2\u5173\u95ed\uff0c\u7b49\u5f85\u7ba1\u7406\u5458\u5f00\u542f";
        DataStore::instance().saveSubmissions();
        if (_ajax) return "{\"ok\":true,\"id\":\"" + common::jsonEscape(sub.id) + "\",\"judge\":false,\"msg\":\"提交成功，但评测功能已关闭\"}";
        std::string c = "<div class='alert alert-info'>\u63d0\u4ea4\u6210\u529f\uff0c\u4f46\u8bc4\u6d4b\u529f\u80fd\u5df2\u5173\u95ed\uff0c\u4ee3\u7801\u5df2\u4fdd\u5b58</div><a href='/status' class='btn'>\u67e5\u770b\u63d0\u4ea4</a>";
        return renderTemplate("\u63d0\u4ea4\u6210\u529f", c, user);
    }
    std::string subId = sub.id;
    Judge::enqueue(subId);

    if (_ajax) return "{\"ok\":true,\"id\":\"" + common::jsonEscape(subId) + "\",\"judge\":true}";
    std::string content = "<div class='alert alert-success'>提交成功！正在后台评测...</div>"
                         "<a href='/status' class='btn'>查看状态</a> <a href='/problem/" + problemId + "' class='btn'>返回题目</a>";
    return renderTemplate("提交成功", content, user);
}

// ============================================================
// 状态（支持分页）
// ============================================================
std::string Handlers::handleStatus(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";

    // 解析查询参数（筛选：problem/user/status）
    std::string query;
    size_t qpos = path.find('?');
    if (qpos != std::string::npos) query = path.substr(qpos + 1);
    std::string fprob, fuser, fstatus;
    size_t pp = query.find("problem=");
    if (pp != std::string::npos) fprob = common::urlDecode(query.substr(pp + 8, query.find('&', pp) == std::string::npos ? std::string::npos : query.find('&', pp) - (pp + 8)));
    size_t pu = query.find("user=");
    if (pu != std::string::npos) fuser = common::urlDecode(query.substr(pu + 5, query.find('&', pu) == std::string::npos ? std::string::npos : query.find('&', pu) - (pu + 5)));
    size_t ps = query.find("status=");
    if (ps != std::string::npos) fstatus = common::urlDecode(query.substr(ps + 7, query.find('&', ps) == std::string::npos ? std::string::npos : query.find('&', ps) - (ps + 7)));
    size_t pPage = query.find("page=");
    int page = 1;
    if (pPage != std::string::npos) { page = common::stringToInt(query.substr(pPage + 5)); if (page < 1) page = 1; }
    if (path.find("/status/page/") != std::string::npos) {
        size_t lastSlash = path.find_last_of('/');
        page = common::stringToInt(path.substr(lastSlash + 1));
        if (page < 1) page = 1;
    }

    int perPage = 20;
    std::string fuserLower = fuser;
    std::transform(fuserLower.begin(), fuserLower.end(), fuserLower.begin(), ::tolower);
    std::string fprobLower = fprob;
    std::transform(fprobLower.begin(), fprobLower.end(), fprobLower.begin(), ::tolower);

    // UOJ机制：所有提交公开可见（任何登录用户可看全部）
    std::vector<const Submission*> visibleSubmissions;
    for (auto it = DataStore::instance().submissions.rbegin();
         it != DataStore::instance().submissions.rend(); ++it) {
        Problem* fp = DataStore::instance().findProblemById(it->problemId);
        User* fu = DataStore::instance().findUserById(it->userId);
        std::string pidLower = it->problemId;
        std::transform(pidLower.begin(), pidLower.end(), pidLower.begin(), ::tolower);
        std::string unameLower = fu ? fu->username : "";
        std::transform(unameLower.begin(), unameLower.end(), unameLower.begin(), ::tolower);
        if (!fprob.empty() && pidLower.find(fprobLower) == std::string::npos) continue;
        if (!fuser.empty() && unameLower.find(fuserLower) == std::string::npos) continue;
        if (!fstatus.empty()) {
            std::string st = getStatusText(it->status);
            std::string stl = st;
            std::transform(stl.begin(), stl.end(), stl.begin(), ::tolower);
            std::string fsl = fstatus;
            std::transform(fsl.begin(), fsl.end(), fsl.begin(), ::tolower);
            if (stl.find(fsl) == std::string::npos) continue;
        }
        // 比赛可见性：他人的比赛提交未锁定前不公开
        if (!it->contestId.empty() && it->userId != user->id && !it->locked) continue;
        visibleSubmissions.push_back(&(*it));
    }

    int total = visibleSubmissions.size();
    int totalPages = (total + perPage - 1) / perPage;
    if (page > totalPages) page = totalPages;
    if (page < 1) page = 1;
    int start = (page - 1) * perPage;
    int end = std::min(start + perPage, total);

    // 筛选表单
    std::string fq;
    if (!fprob.empty()) fq += "&problem=" + common::urlEncode(fprob);
    if (!fuser.empty()) fq += "&user=" + common::urlEncode(fuser);
    if (!fstatus.empty()) fq += "&status=" + common::urlEncode(fstatus);

    std::string content = "<form method='get' action='/status/search' style='display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px;'>"
     "<input name='problem' value='" + common::htmlEscape(fprob) + "' placeholder='题号/题名' style='padding:6px 10px;border:1px solid #d5d9e0;border-radius:6px;'/>"
     "<input name='user' value='" + common::htmlEscape(fuser) + "' placeholder='用户名' style='padding:6px 10px;border:1px solid #d5d9e0;border-radius:6px;'/>"
     "<select name='status' style='padding:6px 10px;border:1px solid #d5d9e0;border-radius:6px;'>"
     "<option value=''>全部状态</option>"
     "<option value='AC'" + (fstatus == "AC" ? " selected" : "") + ">AC</option>"
     "<option value='WA'" + (fstatus == "WA" ? " selected" : "") + ">WA</option>"
     "<option value='TLE'" + (fstatus == "TLE" ? " selected" : "") + ">TLE</option>"
     "<option value='MLE'" + (fstatus == "MLE" ? " selected" : "") + ">MLE</option>"
     "<option value='CE'" + (fstatus == "CE" ? " selected" : "") + ">CE</option>"
     "<option value='RE'" + (fstatus == "RE" ? " selected" : "") + ">RE</option>"
     "</select>"
     "<input type='submit' value='筛选' class='btn btn-sm'/>"
     "</form>";

    content += "<div style='margin-bottom:10px;'><span style='color:#7f8c8d;'>共 " + std::to_string(total) + " 条提交记录</span></div>";
    content += "<table><thead><tr><th>提交ID</th><th>题目</th><th>用户</th><th>状态</th><th>时间</th><th>内存</th><th>提交时间</th><th>操作</th></tr></thead><tbody>";
    if (total == 0) {
        content += "<tr><td colspan='8' style='text-align:center;color:#95a5a6;'>暂无提交记录</td></tr>";
    } else {
        for (int i = start; i < end; ++i) {
            const Submission* s = visibleSubmissions[i];
            Problem* p = DataStore::instance().findProblemById(s->problemId);
            User* u = DataStore::instance().findUserById(s->userId);
            std::string statusClass;
            switch (s->status) {
                case AC: statusClass = "badge-ac"; break;
                case WA: statusClass = "badge-wa"; break;
                case TLE: statusClass = "badge-tle"; break;
                case CE: statusClass = "badge-ce"; break;
                case RE: statusClass = "badge-re"; break;
                default: statusClass = "badge-pending";
            }
            std::string op;
            bool hidden = Handlers::isContestResultHidden(s, user);
            if (s->hacked) op = "<span style='color:#e74c3c;font-size:12px;'>&#128293;已Hack</span>";
            else if (!hidden && s->status == AC && (s->contestId.empty() || s->locked) && p && !p->standardSolution.empty() && s->userId != user->id)
                op = "<a href='/submission/hack/" + s->id + "' class='btn btn-sm' style='background:#e74c3c;'>&#128293;Hack</a>";
            content += "<tr><td><a href='/submission/" + s->id + "'>" + s->id.substr(0, 8) + "</a></td>"
                      "<td><a href='/problem/" + s->problemId + "'>" + (p ? common::htmlEscape(p->title) : "已删除") + "</a></td>"
                      "<td><a href='/user/" + s->userId + "'>" + (u ? common::htmlEscape(u->username) : "已注销") + "</a></td>"
                      "<td>" + (s->hacked ? "<span class='badge' style='background:#e74c3c;color:#fff;'>&#128293;Hacked</span>" : (hidden ? "<span class='badge badge-pending'>🔒 评测中</span>" : ("<span class='badge " + statusClass + "'>" + getStatusText(s->status) + "</span>"))) + "</td>"
                      "<td>" + (hidden ? "-" : std::to_string(s->usedTime) + "ms") + "</td><td>" + (hidden ? "-" : std::to_string(s->usedMem) + "MB") + "</td>"
                      "<td>" + s->submitTime + "</td><td>" + op + "</td></tr>";
        }
    }
    content += "</tbody></table>";

    // 筛选感知的分页
    if (totalPages > 1) {
        content += "<div class='pagination' style='margin-top:16px;justify-content:center;'>";
        if (page > 1) content += "<a class='btn btn-sm' href='/status/search?page=" + std::to_string(page - 1) + fq + "'>&#10094; 上一页</a>";
        int lo = std::max(1, page - 3), hi = std::min(totalPages, page + 3);
        for (int i = lo; i <= hi; ++i) {
            if (i == page) content += "<a class='btn btn-sm' style='background:var(--accent);color:#fff;'>" + std::to_string(i) + "</a>";
            else content += "<a class='btn btn-sm' href='/status/search?page=" + std::to_string(i) + fq + "'>" + std::to_string(i) + "</a>";
        }
        if (page < totalPages) content += "<a class='btn btn-sm' href='/status/search?page=" + std::to_string(page + 1) + fq + "'>下一页 &#10095;</a>";
        content += "</div>";
    }
    return renderTemplate("评测状态", content, user);
}

// ============================================================
// 提交详情
// ============================================================
std::string Handlers::handleSubmissionDetail(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    size_t lastSlash = path.find_last_of('/');
    std::string subId = path.substr(lastSlash + 1);
    Submission* sub = DataStore::instance().findSubmissionById(subId);
    
    if (!sub) {
        std::string content = "<div class='alert alert-error'>提交记录不存在</div><a href='/status' class='btn'>返回</a>";
        return renderTemplate("错误", content, user);
    }

    // UOJ机制：提交记录公开，任何登录用户可查看（含代码）
    
    Problem* problem = DataStore::instance().findProblemById(sub->problemId);
    User* author = DataStore::instance().findUserById(sub->userId);
    
    std::string statusClass;
    switch(sub->status) {
        case AC: statusClass = "badge-ac"; break;
        case WA: statusClass = "badge-wa"; break;
        case TLE: statusClass = "badge-tle"; break;
        case CE: statusClass = "badge-ce"; break;
        case RE: statusClass = "badge-re"; break;
        default: statusClass = "badge-pending";
    }
    
    // OI 赛制结果隐藏
    bool hidden = Handlers::isContestResultHidden(sub, user);
    
    std::string content = "<div class='card'><h2>提交详情</h2>"
                         "<p><strong>提交ID:</strong> " + sub->id + "</p>"
                         "<p><strong>题目:</strong> <a href='/problem/" + sub->problemId + "'>" + 
                         (problem ? common::htmlEscape(problem->title) : "已删除") + "</a></p>"
                         "<p><strong>用户:</strong> <a href='/user/" + sub->userId + "'>" + 
                         (author ? common::htmlEscape(author->username) : "已注销") + "</a></p>";
    if (hidden) {
        content += "<p><strong>状态:</strong> <span class='badge badge-pending'>🔒 评测中（OI赛制，赛后公开）</span></p>"
                   "<p><strong>耗时:</strong> -</p><p><strong>内存:</strong> -</p>";
    } else {
        content += "<p><strong>状态:</strong> <span class='badge " + statusClass + "'>" + 
                   getStatusText(sub->status) + "</span></p>"
                   "<p><strong>耗时:</strong> " + std::to_string(sub->usedTime) + "ms</p>"
                   "<p><strong>内存:</strong> " + std::to_string(sub->usedMem) + "MB</p>";
    }
    content += "<p><strong>提交时间:</strong> " + sub->submitTime + "</p></div>";
    if (sub->hacked) {
        content += "<div style='background:#fdf2f2;border:1px solid #f5c6c6;border-radius:6px;padding:12px;margin-top:10px;'>"
         "<p style='margin:0;color:#c0392b;'>&#128293; 该提交已被 <b>" + common::htmlEscape(sub->hackedBy) + "</b> Hack（" + common::htmlEscape(sub->hackTime) + "），本题得分已<b>归零</b>。</p>"
         "<div style='margin-top:8px;'><b>攻击数据：</b><pre style='white-space:pre-wrap;margin:6px 0 0;font-size:13px;'>" + common::htmlEscape(sub->hackInput) + "</pre></div></div>";
    }
    if (!sub->hacked && sub->status == AC && problem && !problem->standardSolution.empty() && sub->userId != user->id) {
        content += "<div style='margin:12px 0;'><a href='/submission/hack/" + sub->id + "' class='btn' style='background:#e74c3c;'>&#128293; 进入Hack工作台</a></div>";
    }
    
    if (!sub->judgeResult.empty() && !hidden) {
        content += "<h3>📊 评测详情</h3><div style='background:#f8f9fa;padding:15px;border-radius:6px;white-space:pre-wrap;font-family:monospace;font-size:14px;'>" + 
                  common::htmlEscape(sub->judgeResult) + "</div>";
    }
    
    // 代码可见性：作者本人与管理员始终可见；其他人取决于作者的「代码公开」设置
    bool codeVisible = true;
    if (author && !author->codePublic && sub->userId != user->id && !isAdmin(user)) codeVisible = false;
    content += "<h3 style='margin-top:20px;'>📝 提交代码</h3>";
    if (codeVisible) {
        content += "<div style='background:#1e1e1e;color:#d4d4d4;padding:15px;border-radius:6px;overflow-x:auto;'>"
                   "<pre style='margin:0;white-space:pre-wrap;font-family:Consolas,monospace;font-size:13px;color:#d4d4d4;'>" +
                   common::htmlEscape(sub->code) + "</pre></div>";
    } else {
        content += "<div class='alert alert-info'>🔒 该用户未公开提交代码，仅本人与管理员可查看。</div>";
    }
    
    if (isAdmin(user)) {
        content += "<form method='POST' action='/admin/rejudge-single' style='display:inline-block;'>"
                  "<input type='hidden' name='submissionId' value='" + sub->id + "'/>"
                  "<input type='submit' value='\u2699\ufe0f 单条重测' class='btn' style='background:#e67e22;'/></form> ";
    }
    content += "<div style='margin-top:20px;'><a href='/status' class='btn'>返回状态列表</a> <a href='/problem/" + sub->problemId + "' class='btn'>返回题目</a></div>";
    return renderTemplate("提交详情", content, user);
}

// ============================================================
// 博客（支持分页）
// ============================================================
std::string Handlers::handleBlog(const std::string& method, const std::string& path,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers,
                                 std::map<std::string, std::string>& respHeaders,
                                 std::string& sessionId) {
    if (!loadSystemConfig().allowBlog) {
        std::string c = "<div class='alert alert-info'>\u535a\u5ba2\u5df2\u5173\u95ed</div><a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a>";
        return renderTemplate("\u535a\u5ba2\u5df2\u5173\u95ed", c, getCurrentUser(sessionId));
    }
    User* user = getCurrentUser(sessionId);
    
    int page = 1;
    int perPage = 10;
    if (path.find("/blog/page/") != std::string::npos) {
        size_t lastSlash = path.find_last_of('/');
        std::string pageStr = path.substr(lastSlash + 1);
        page = common::stringToInt(pageStr);
        if (page < 1) page = 1;
    } else {
        std::string ps = parseQueryString(path, "page");
        if (!ps.empty()) { page = common::stringToInt(ps); if (page < 1) page = 1; }
    }
    
    // 筛选参数：搜索 / 只看我的 / 类型tab
    std::string kw = common::trim(parseQueryString(path, "q"));
    bool mineOnly = (parseQueryString(path, "mine") == "1");
    std::string tab = parseQueryString(path, "tab");
    if (tab != "blog" && tab != "solution") tab = "all"; // all / blog / solution
    
    std::vector<Post*> blogPosts;
    for (auto& p : DataStore::instance().posts) {
        if (tab == "solution") {
            if (p.type != SOLUTION) continue;
        } else if (tab == "blog") {
            if (p.type != BLOG) continue;
        } else {
            if (p.type != BLOG && p.type != SOLUTION) continue;
        }
        // SOLUTION 可见性：未审核通过的仅作者自己(及管理员)可见
        if (p.type == SOLUTION && p.auditStatus != "approved") {
            bool ownerView = (user && (p.authorId == user->id || isAdmin(user)));
            if (!ownerView) continue;
        }
        if (mineOnly) {
            if (!user || p.authorId != user->id) continue;
        }
        if (!kw.empty()) {
            std::string hay = p.title + " " + p.content;
            if (hay.find(kw) == std::string::npos) continue;
        }
        blogPosts.push_back(&p);
    }
    
    int total = blogPosts.size();
    int totalPages = (total + perPage - 1) / perPage;
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    if (page < 1) page = 1;
    
    int start = (page - 1) * perPage;
    int end = std::min(start + perPage, total);
    
    std::string content;
    content += "<div style='display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:12px;'>";
    if (user) content += "<a href='/blog/new' class='btn'>📝 写博客</a>";
    // 类型 tab
    std::string qs = !kw.empty() ? ("&q=" + common::urlEncode(kw)) : "";
    if (mineOnly) qs += "&mine=1";
    auto tabLink = [&](const std::string& t, const std::string& label){
        std::string href = "/blog?tab=" + t + qs;
        if (tab == t) return "<a class='btn btn-sm' style='background:#667eea;color:#fff;' href='"+href+"'>"+label+"</a>";
        return "<a class='btn btn-sm' href='"+href+"'>"+label+"</a>";
    };
    content += tabLink("all","全部");
    content += tabLink("blog","博客");
    content += tabLink("solution","题解");
    if (user) {
        std::string mhref = "/blog?mine=1&tab=" + tab + (!kw.empty() ? ("&q="+common::urlEncode(kw)) : "");
        std::string mstyle = mineOnly ? "background:#27ae60;color:#fff;" : "";
        content += "<a class='btn btn-sm' style='"+mstyle+"' href='"+mhref+"'>我的</a>";
    }
    content += "</div>";
    // 搜索框
    content += "<form action='/blog' method='get' style='margin-bottom:14px;'>"
               "<div style='display:flex;gap:8px;'>"
               "<input type='text' name='q' value='" + common::htmlEscape(kw) + "' placeholder='搜索标题或内容…' style='flex:1;'/>"
               "<input type='hidden' name='tab' value='" + tab + "'/>"
               + (mineOnly ? "<input type='hidden' name='mine' value='1'/>" : "") +
               "<button class='btn' type='submit'>搜索</button></div></form>";
    
    for (int i = end - 1; i >= start; --i) {
        Post* post = blogPosts[i];
        User* author = DataStore::instance().findUserById(post->authorId);
        std::string typeTag, auditTag;
        if (post->type == SOLUTION) {
            typeTag = "<span class='pill' style='background:#eaf5ff;color:#2980b9;border:1px solid #bcd8f0;'>题解</span> ";
            if (post->auditStatus == "approved") auditTag = "<span class='pill' style='background:#e8f8f0;color:#27ae60;'>已通过</span>";
            else if (post->auditStatus == "rejected") auditTag = "<span class='pill' style='background:#fdecea;color:#e74c3c;'>已驳回</span>";
            else auditTag = "<span class='pill' style='background:#fef9e7;color:#f39c12;'>待审核</span>";
        }
        std::string title = post->title;
        if (post->type == SOLUTION) {
            Problem* ppp = DataStore::instance().findProblemById(post->problemId);
            title += (ppp ? "（题目：" + ppp->title + "）" : "");
        }
        content += "<div class='card'>"
                  "<div class='card-title'>" + typeTag + auditTag +
                  " <a href='/post/" + post->id + "'>" + common::htmlEscape(title) + "</a></div>"
                  "<div class='card-meta'>作者: <a href='/user/" + post->authorId + "'>" + 
                  (author ? common::htmlEscape(author->username) : "已注销") + "</a> | " + post->createTime +
                  " | 阅读: " + std::to_string(post->views) + " | 点赞: " + std::to_string(post->likes) + "</div>"
                  "<div style='margin-top:8px;color:#555;'>" + common::htmlEscape(post->content.substr(0, 200)) + 
                  (post->content.length() > 200 ? "..." : "") + "</div></div>";
    }
    
    if (blogPosts.empty()) {
        content += "<div class='empty-state'><span class='icon'>📝</span>暂无内容</div>";
    }
    
    std::string pageBase = "/blog/page";
    content += renderPaginationKeepParams(pageBase, page, totalPages, "tab=" + tab + qs + (mineOnly ? "&mine=1" : ""));
    return renderTemplate("博客", content, user);
}

// ============================================================
// 写博客
// ============================================================
std::string Handlers::handleBlogNew(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    if (!loadSystemConfig().allowBlog) {
        std::string c = "<div class='alert alert-info'>\u535a\u5ba2\u5df2\u5173\u95ed</div><a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a>";
        return renderTemplate("\u535a\u5ba2\u5df2\u5173\u95ed", c, getCurrentUser(sessionId));
    }
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    if (method == "GET") {
        std::string content = "<form method='POST'><div class='form-group'><label>标题</label><input name='title' required/></div>"
                             "<div class='form-group'><label>内容 (支持Markdown)</label><textarea name='content' rows='20' required></textarea></div>"
                             + security::captchaFieldsFor("blog") +
                             "<input type='submit' value='发布' class='btn'/> <a href='/blog' class='btn'>取消</a></form>";
        return renderTemplate("写博客", content, user);
    } else {
        if (!security::verifyCaptchaFor(parseFormData(body, "captcha_token"), parseFormData(body, "captcha_answer"), "blog")) {
            std::string c = "<div class='alert alert-error'>\u9a8c\u8bc1\u7801\u9519\u8bef\u6216\u5df2\u8fc7\u671f</div><a href='/blog/new' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u53d1\u5e03\u5931\u8d25", c, user);
        }

        std::string title = parseFormData(body, "title");
        std::string contentText = parseFormData(body, "content");
        if (title.empty() || contentText.empty()) {
            std::string content = "<div class='alert alert-error'>标题和内容不能为空</div><a href='/blog/new' class='btn'>返回</a>";
            return renderTemplate("发布失败", content, user);
        }
        security::SecConfig _sc = security::getSecConfig();
        if ((int)title.length() > _sc.maxBlogTitle) title = title.substr(0, _sc.maxBlogTitle);
        if ((int)contentText.length() > _sc.maxBlog) contentText = contentText.substr(0, _sc.maxBlog);

        if (security::throttleAction("blog:" + user->id, _sc.blogIntervalSec)) {
            std::string c = "<div class='alert alert-error'>\u53d1\u5e03\u8fc7\u4e8e\u9891\u7e41\uff0c\u8bf7\u7a0d\u540e\u518d\u8bd5</div><a href='/blog/new' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u53d1\u5e03\u5931\u8d25", c, user);
        }
        security::recordIpActivity(clientIpOf(headers), user->username);
        
        Post newPost;
        newPost.id = common::genId();
        newPost.authorId = user->id;
        newPost.type = BLOG;
        newPost.title = title;
        newPost.content = contentText;
        newPost.createTime = common::getTimeStr();
        newPost.views = 0;
        newPost.likes = 0;
        std::string pollTitle = parseFormData(body, "pollTitle");
        std::string pollOptionsRaw = parseFormData(body, "pollOptions");
        if (!pollTitle.empty() && !pollOptionsRaw.empty()) {
            newPost.pollTitle = pollTitle;
            auto olines = common::split(pollOptionsRaw, '\n');
            for (auto& o : olines) {
                std::string ot = common::trim(o);
                if (!ot.empty()) newPost.pollOptions.push_back(ot);
            }
            newPost.pollVotes.resize(newPost.pollOptions.size());
        }
        
        DataStore::instance().posts.push_back(newPost);
        DataStore::instance().savePosts();
        
        std::string content = "<div class='alert alert-success'>博客发布成功！</div>"
                             "<a href='/blog' class='btn'>返回博客列表</a> <a href='/post/" + newPost.id + "' class='btn'>查看博客</a>";
        return renderTemplate("发布成功", content, user);
    }
}

// ============================================================
// 帖子详情、评论、点赞
// ============================================================
std::string Handlers::handlePostDetail(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string cleanPath = path;
    size_t pq = cleanPath.find('?');
    if (pq != std::string::npos) cleanPath = cleanPath.substr(0, pq);
    size_t lastSlash = cleanPath.find_last_of('/');
    std::string postId = cleanPath.substr(lastSlash + 1);
    Post* post = DataStore::instance().findPostById(postId);
    
    if (!post) {
        std::string content = "<div class='alert alert-error'>帖子不存在</div><a href='/blog' class='btn'>返回</a>";
        return renderTemplate("错误", content, user);
    }
    
    // 题解可见性：SOLUTION 未通过审核时仅作者与管理员可见（含驳回）
    bool isAdminNow = user && (canManageProblems(user) || isAdmin(user));
    if (post->type == SOLUTION && !post->auditStatus.empty() && post->auditStatus != "approved") {
        if (!user || (user->id != post->authorId && !isAdminNow)) {
            std::string content = "<div class='alert alert-warning'>该题解尚未通过审核，暂时无法查看</div><a href='/problems' class='btn'>返回题库</a>";
            return renderTemplate("题解", content, user);
        }
    }
    
    // 增加阅读数
    post->views++;
    DataStore::instance().savePosts();
    
    User* author = DataStore::instance().findUserById(post->authorId);
    std::string typeStr;
    switch(post->type) { 
        case BLOG: typeStr = "博客"; break; 
        case DISCUSSION: typeStr = "讨论"; break; 
        case BENBEN: typeStr = "犇犇"; break; 
        case SOLUTION: typeStr = "题解"; break; 
        default: typeStr = "未知";
    }
    
    std::string content = "";
    content += "<div style='margin-bottom:20px;'>";
    content += "<h2>" + common::htmlEscape(post->title) + "</h2>";
    content += "<div style='color:#7f8c8d;font-size:14px;'>";
    content += "类型: " + typeStr + " | 作者: <a href='/user/" + post->authorId + "'>" + 
              (author ? common::htmlEscape(author->username) : "已注销") + "</a> | " + post->createTime +
              " | 阅读: " + std::to_string(post->views) + " | 点赞: " + std::to_string(post->likes);
    // 题解关联题目 & 审核状态
    if (post->type == SOLUTION) {
        Problem* _solP = DataStore::instance().findProblemById(post->problemId);
        content += "<br/>所属题目: <a href='/problem/" + post->problemId + "'>" +
                  (_solP ? common::htmlEscape(_solP->title) : post->problemId) + "</a>";
        if (post->auditStatus == "pending") content += " | <span style='color:#e67e22;font-weight:600;'>⏳ 待审核</span>";
        else if (post->auditStatus == "rejected") content += " | <span style='color:#e74c3c;font-weight:600;'>❌ 已驳回</span>" + (post->rejectReason.empty()?"":"（" + common::htmlEscape(post->rejectReason) + "）");
        else if (post->auditStatus == "removed") content += " | <span style='color:#8e44ad;font-weight:600;'>🚫 已撤下</span>（已被管理员撤下，重新申请可再上架）";
        else content += " | <span style='color:#27ae60;font-weight:600;'>✅ 已通过</span>";
    }
    content += "</div></div>";
    
    // ========== 显示帖子内容 ==========
    // 使用 markdownToHtml 渲染（支持 Markdown），同时保留换行和特殊字符
    std::string renderedContent = common::markdownToHtml(post->content);
    if (renderedContent.empty()) {
        // 如果 markdownToHtml 返回空，直接显示纯文本（转义后）
        renderedContent = "<pre style='white-space:pre-wrap;font-family:inherit;'>" + common::htmlEscape(post->content) + "</pre>";
    }
    content += "<div class='post-content md-body'>" + renderedContent + "</div>";
    
    // 题解：作者操作（重新申请 / 返回题目）
    if (post->type == SOLUTION && user && (post->authorId == user->id || isAdminNow)) {
        if (post->auditStatus == "rejected" || post->auditStatus == "removed") {
            content += "<div style='margin:10px 0;'>"
                       "<form method='POST' action='/post/" + postId + "/resubmit' style='display:inline;'>"
                       "<input type='submit' value='🔄 重新申请审核' class='btn'/></form> "
                       "<span style='color:#7f8c8d;font-size:13px;'>" + (post->auditStatus=="rejected" ? std::string("被驳回后可修改内容再重新申请") : std::string("已撤下，重新申请后可再次上架审核")) + "</span></div>";
        } else if (post->auditStatus == "pending") {
            content += "<div style='margin:10px 0;'><span class='pill' style='background:#fef9e7;color:#f39c12;'>⏳ 已提交，等待管理员审核</span></div>";
        }
        content += "<div style='margin:10px 0;'><a href='/solutions?problem=" + common::urlEncode(post->problemId) + "' class='btn btn-sm btn-outline'>← 返回该题题解区</a></div>";
    }
    
    // 点赞按钮
        bool liked = false;
    if (user) { for (auto& uid : post->likedBy) if (uid == user->id) { liked = true; break; } }
    std::string likeLabel = liked ? ("\U0001f44d \u5df2\u70b9\u8d5e (" + std::to_string(post->likes) + ")") : ("\U0001f44d \u70b9\u8d5e (" + std::to_string(post->likes) + ")");
    content += "<div style='margin:15px 0;'>"
              "<form method='POST' action='/post/" + postId + "/like' style='display:inline;'>"
              "<input type='submit' value='" + likeLabel + "' class='btn'/></form>";

    // \u6295\u7968
    if (!post->pollTitle.empty() && !post->pollOptions.empty()) {
        int totalVotes = 0;
        for (auto& vv : post->pollVotes) totalVotes += (int)vv.size();
        bool voted = false;
        if (user) { for (auto& vv : post->pollVotes) for (auto& uid : vv) if (uid == user->id) voted = true; }
        content += "<div style='margin:20px 0;border:1px solid #e3e8ee;border-radius:10px;padding:16px;background:#fafbfc;'>";
        content += "<h4 style='margin:0 0 12px;'>\U0001f4ca " + common::htmlEscape(post->pollTitle) + "</h4>";
        if (voted || !user) {
            for (size_t i = 0; i < post->pollOptions.size(); i++) {
                int cnt = i < post->pollVotes.size() ? (int)post->pollVotes[i].size() : 0;
                int pct = totalVotes > 0 ? (int)((double)cnt / totalVotes * 100) : 0;
                content += "<div style='margin-bottom:10px;'>";
                content += "<div>" + common::htmlEscape(post->pollOptions[i]) + " <span style='color:#7f8c8d;'>(" + std::to_string(cnt) + " \u7968, " + std::to_string(pct) + "%)</span></div>";
                content += "<div style='background:#e8ecf1;border-radius:6px;height:16px;overflow:hidden;'><div style='height:100%;width:" + std::to_string(pct) + "%;background:#667eea;'></div></div>";
                content += "</div>";
            }
            content += "<div style='color:#7f8c8d;font-size:13px;'>\u5171 " + std::to_string(totalVotes) + " \u4eba\u53c2\u4e0e\u6295\u7968" + (voted ? "\uff08\u4f60\u5df2\u6295\u7968\uff09" : "") + "</div>";
        } else {
            content += "<form method='POST' action='/post/" + postId + "/vote'>";
            for (size_t i = 0; i < post->pollOptions.size(); i++) {
                content += "<label style='display:block;margin:6px 0;'><input type='radio' name='choice' value='" + std::to_string(i) + "'/> " + common::htmlEscape(post->pollOptions[i]) + "</label>";
            }
            content += "<input type='submit' value='\u6295\u7968' class='btn'/></form>";
        }
        content += "</div>";
    }
    content += "</div>";

    
    // 评论列表
    content += "<h3 style='margin-top:30px;'>评论 (" + std::to_string(post->comments.size()) + ")</h3>";
    for (const auto& comment : post->comments) {
        User* commenter = DataStore::instance().findUserById(comment.authorId);
        content += "<div class='comment' style='background:#f8f9fa;padding:12px 16px;border-radius:6px;margin:8px 0;border-left:3px solid #667eea;'>";
        content += "<div><strong><a href='/user/" + comment.authorId + "'>" + 
                  (commenter ? common::htmlEscape(commenter->username) : "已注销") + "</a></strong> "
                  "<span style='color:#7f8c8d;font-size:14px;'>" + comment.createTime + "</span></div>";
        content += "<div style='margin-top:5px;'>" + common::htmlEscape(comment.content) + "</div>";
        content += "</div>";
    }
    
    // 发表评论
    if (user) {
        content += "<div style='margin-top:20px;'><h4>发表评论</h4>"
                  "<form method='POST' action='/post/" + postId + "/comment'>"
                  "<div class='form-group'><textarea name='content' rows='4' placeholder='写下你的评论...' required></textarea></div>"
                  "<input type='submit' value='发表评论' class='btn'/></form></div>";
    } else {
        content += "<div class='alert alert-info' style='margin-top:20px;'><a href='/login'>登录</a> 后可以发表评论</div>";
    }
    
    return renderTemplate("帖子详情 - " + post->title, content, user);
}
// ============================================================
// 题解（独立列表 / 撰写 / 审核 / 开关入口）
// ============================================================
std::string Handlers::handleProblemSolutions(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string problemId = parseQueryString(path, "problem");
    if (problemId.empty() && path.rfind("/solutions/", 0) == 0) problemId = sanitizeRelPath(path.substr(11));
    Problem* problem = DataStore::instance().findProblemById(problemId);
    if (!problem) {
        std::string c = "<div class='alert alert-error'>题目不存在</div><a href='/problems' class='btn'>返回题库</a>";
        return renderTemplate("题解", c, user);
    }
    std::string content = "<div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
                          "<h2 style='margin:0;'>📝 题解 · " + common::htmlEscape(problem->title) + "</h2>"
                          "<div><a href='/problem/" + problem->id + "' class='btn btn-sm btn-outline'>← 返回题目</a></div></div>";
    // 顶部提示
    std::string statusNote;
    bool hasMine = false; std::string mineId;
    if (problem->solutionClosed) statusNote = "<div class='alert alert-info'>🔒 本题题解已关闭（管理员已禁用）</div>";
    if (user) {
        for (const auto& pp : DataStore::instance().posts) {
            if (pp.type == SOLUTION && pp.problemId == problem->id && pp.authorId == user->id) { hasMine = true; mineId = pp.id; break; }
        }
    }
    std::string writeBtn;
    if (user && !problem->solutionClosed) {
        if (hasMine) writeBtn = "<a href='/post/" + mineId + "' class='btn btn-sm'>✏️ 查看我的题解</a>";
        else writeBtn = "<a href='/solution/new/" + problem->id + "' class='btn btn-sm'>✏️ 写题解</a>";
    } else if (!user) {
        writeBtn = "<a href='/login' class='btn btn-sm'>登录后写题解</a>";
    }
    content += "<div style='margin:12px 0;display:flex;gap:8px;flex-wrap:wrap;'>" + writeBtn + statusNote + "</div>";

    std::string listHtml;
    int shown = 0;
    for (const auto& pp : DataStore::instance().posts) {
        if (pp.type != SOLUTION || pp.problemId != problem->id) continue;
        // 题解区仅展示：已审核通过 或 自己的（含待审/驳回草稿）
        bool visible = (pp.auditStatus == "approved") || (user && pp.authorId == user->id);
        if (!visible) continue;
        User* au = DataStore::instance().findUserById(pp.authorId);
        std::string badge;
        if (pp.auditStatus == "pending") badge = "<span style='color:#e67e22;font-size:12px;'>⏳待审核</span>";
        else if (pp.auditStatus == "approved") badge = "<span style='color:#27ae60;font-size:12px;'>✅已通过</span>";
        else if (pp.auditStatus == "removed") badge = "<span style='color:#8e44ad;font-size:12px;'>🚫已撤下</span>";
        else badge = "<span style='color:#e74c3c;font-size:12px;'>❌已驳回</span>";
        listHtml += "<div style='border:1px solid var(--border);border-radius:10px;padding:12px 16px;margin:10px 0;'>"
                    "<div style='font-weight:600;'><a href='/post/" + pp.id + "'>" + common::htmlEscape(pp.title) + "</a> " + badge + "</div>"
                    "<div style='color:var(--muted);font-size:13px;margin-top:4px;'>by <a href='/user/" + pp.authorId + "'>" + (au ? common::htmlEscape(au->username) : "已注销") + "</a> · " + pp.createTime + " · 点赞 " + std::to_string(pp.likes) + "</div></div>";
        shown++;
    }
    if (shown == 0) {
        content += std::string("<div class='empty-state'>暂无题解") + (hasMine ? "（你的题解见上方入口）" : "") + "</div>";
    } else {
        content += "<h3>全部题解（" + std::to_string(shown) + "）</h3>" + listHtml;
    }
    return renderTemplate("题解 - " + problem->title, content, user);
}

std::string Handlers::handleSolutionNew(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string problemId = sanitizeRelPath(path.substr(std::string("/solution/new/").size()));
    Problem* problem = DataStore::instance().findProblemById(problemId);
    if (!problem) {
        std::string c = "<div class='alert alert-error'>题目不存在</div><a href='/problems' class='btn'>返回题库</a>";
        return renderTemplate("写题解", c, user);
    }
    if (problem->solutionClosed) {
        std::string c = "<div class='alert alert-error'>本题题解入口已关闭</div><a href='/solutions?problem=" + problem->id + "' class='btn'>返回</a>";
        return renderTemplate("写题解", c, user);
    }
    // 一人一题一篇
    bool hasMine = false;
    for (const auto& pp : DataStore::instance().posts)
        if (pp.type == SOLUTION && pp.problemId == problem->id && pp.authorId == user->id) { hasMine = true; break; }
    if (hasMine) {
        std::string c = "<div class='alert alert-warning'>你已为本题写过题解，一人一题限一篇。</div><a href='/solutions?problem=" + problem->id + "' class='btn'>返回题解列表</a>";
        return renderTemplate("写题解", c, user);
    }
    if (method == "GET") {
        std::string content = "<div style='margin-bottom:10px;'><a href='/solutions?problem=" + problem->id + "' class='btn btn-sm btn-outline'>← 返回题解列表</a></div>";
        content += "<h2>✏️ 为《" + common::htmlEscape(problem->title) + "》写题解</h2>"
                   "<div class='alert alert-info'>题解支持 Markdown（标题、加粗、表格、公式 <code>$...$</code>、图片 <code>![](url)</code>、颜色 <code>[color=red]文字[/color]</code>）。提交后需管理员审核，通过后 +50 金币。</div>"
                   "<form method='POST'>"
                   "<div class='form-group'><label>标题</label><input name='title' placeholder='题解标题' required/></div>"
                   "<div class='form-group'><label>题解内容</label><textarea name='content' rows='18' placeholder='在此用 Markdown 撰写题解...' required></textarea></div>"
                   "<input type='submit' value='提交题解（待审核）' class='btn'/> <a href='/solutions?problem=" + problem->id + "' class='btn'>取消</a></form>";
        return renderTemplate("写题解", content, user);
    }
    // POST
    std::string title = parseFormData(body, "title");
    std::string contentText = parseFormData(body, "content");
    if (title.empty() || contentText.empty()) {
        std::string c = "<div class='alert alert-error'>标题和内容不能为空</div><a href='/solution/new/" + problem->id + "' class='btn'>返回</a>";
        return renderTemplate("写题解", c, user);
    }
    security::SecConfig _sc = security::getSecConfig();
    if ((int)title.length() > _sc.maxBlogTitle) title = title.substr(0, _sc.maxBlogTitle);
    int _ml = _sc.maxDiscuss;
    if ((int)contentText.length() > _ml) contentText = contentText.substr(0, _ml);
    if (security::throttleAction("solution:" + user->id, _sc.discussIntervalSec)) {
        std::string c = "<div class='alert alert-error'>发布过于频繁，请稍后再试</div><a href='/solution/new/" + problem->id + "' class='btn'>返回</a>";
        return renderTemplate("写题解", c, user);
    }
    Post np;
    np.id = common::genId();
    np.authorId = user->id;
    np.type = SOLUTION;
    np.title = title;
    np.content = contentText;
    np.createTime = common::getTimeStr();
    np.views = 0; np.likes = 0;
    np.problemId = problem->id;
    np.auditStatus = "pending";
    DataStore::instance().posts.push_back(np);
    DataStore::instance().savePosts();
    std::string c = "<div class='alert alert-success'>题解已提交，等待管理员审核。</div>"
                    "<a href='/post/" + np.id + "' class='btn'>查看我的题解</a> <a href='/solutions?problem=" + problem->id + "' class='btn'>返回题解列表</a>";
    return renderTemplate("提交成功", c, user);
}

std::string Handlers::handleAdminSolutions(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user || !isAdmin(user)) return "<script>window.location.href='/login'</script>";
    std::string statusFilter = parseQueryString(path, "status");
    std::string content = adminNavbar(user);
    content += "<h2>🛂 题解审核</h2>";
    content += "<div style='margin:10px 0;display:flex;gap:8px;flex-wrap:wrap;'>"
               "<a href='/admin/solutions' class='btn btn-sm btn-outline'>全部</a> "
               "<a href='/admin/solutions?status=pending' class='btn btn-sm btn-outline'>⏳ 待审核</a> "
               "<a href='/admin/solutions?status=approved' class='btn btn-sm btn-outline'>✅ 已通过</a> "
               "<a href='/admin/solutions?status=rejected' class='btn btn-sm btn-outline'>❌ 已驳回</a></div>";
    std::string rows;
    int cnt = 0;
    for (const auto& pp : DataStore::instance().posts) {
        if (pp.type != SOLUTION) continue;
        if (!statusFilter.empty() && pp.auditStatus != statusFilter) continue;
        cnt++;
        User* au = DataStore::instance().findUserById(pp.authorId);
        Problem* pr = DataStore::instance().findProblemById(pp.problemId);
        std::string preview = common::htmlEscape(pp.content.substr(0, 80));
        rows += "<tr><td><a href='/post/" + pp.id + "'>" + common::htmlEscape(pp.title) + "</a><div style='color:var(--muted);font-size:12px;'>" + preview + (pp.content.size()>80?"...":"") + "</div></td>"
                "<td><a href='/problem/" + pp.problemId + "'>" + (pr ? common::htmlEscape(pr->title) : pp.problemId) + "</a></td>"
                "<td>" + (au ? common::htmlEscape(au->username) : "已注销") + "</td>"
                "<td>" + pp.createTime + "</td>"
                "<td>" + (pp.auditStatus=="pending"?"<span style='color:#e67e22;'>⏳待审核</span>":pp.auditStatus=="approved"?"<span style='color:#27ae60;'>✅已通过</span>":pp.auditStatus=="removed"?"<span style='color:#8e44ad;'>🚫已撤下</span>":"<span style='color:#e74c3c;'>❌已驳回</span>") + "</td>"
                "<td><a href='/post/" + pp.id + "' class='btn btn-sm btn-outline'>查看</a> ";
        if (pp.auditStatus == "approved") {
            rows += "<form method='POST' action='/admin/solution/audit' style='display:inline-flex;gap:4px;'>"
                    "<input type='hidden' name='postId' value='" + pp.id + "'/>"
                    "<input type='hidden' name='action' value='remove'/>"
                    "<input type='submit' value='撤下' class='btn btn-sm' style='background:#8e44ad;color:#fff;'/></form> ";
        } else if (pp.auditStatus == "removed") {
            rows += "<span style='color:#7f8c8d;font-size:12px;'>已撤下，作者重新申请后可再审</span> ";
        } else {
            rows += "<form method='POST' action='/admin/solution/audit' style='display:inline-flex;gap:4px;'>"
                    "<input type='hidden' name='postId' value='" + pp.id + "'/>"
                    "<input type='hidden' name='action' value='approve'/>"
                    "<input type='submit' value='通过' class='btn btn-sm' style='background:#27ae60;'/></form> "
                    "<form method='POST' action='/admin/solution/audit' style='display:inline-flex;gap:4px;'>"
                    "<input type='hidden' name='postId' value='" + pp.id + "'/>"
                    "<input type='hidden' name='action' value='reject'/>"
                    "<input name='reason' placeholder='驳回原因(可选)' style='width:90px;padding:4px;border-radius:6px;border:1px solid #ddd;'/>"
                    "<input type='submit' value='驳回' class='btn btn-sm' style='background:#e74c3c;'/></form>";
        }
        rows += "</td></tr>";
    }
    if (cnt == 0) rows = "<tr><td colspan='6'>暂无题解</td></tr>";
    content += "<table><thead><tr><th>题解</th><th>题目</th><th>作者</th><th>时间</th><th>状态</th><th>操作</th></tr></thead><tbody>" + rows + "</tbody></table>";
    content += "<div style='margin-top:14px;'><a href='/admin' class='btn'>返回管理面板</a></div>";
    return renderTemplate("题解审核", content, user);
}

std::string Handlers::handleAdminSolutionAudit(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user || !isAdmin(user)) return "<script>window.location.href='/login'</script>";
    std::string postId = parseFormData(body, "postId");
    std::string action = parseFormData(body, "action");
    std::string reason = parseFormData(body, "reason");
    Post* post = DataStore::instance().findPostById(postId);
    if (!post || post->type != SOLUTION) return "<script>window.location.href='/admin/solutions'</script>";
    if (action == "approve") {
        // 撤下(removed)状态下不能直接通过，须作者重新申请(pending)后再审
        bool canApprove = (post->auditStatus == "pending" || post->auditStatus == "rejected");
        if (canApprove) {
            post->auditStatus = "approved";
            post->reviewerId = user->id;
            post->auditTime = common::getTimeStr();
            // 仅在未发过奖励时 +50（防撤下再装上重复发）
            if (!post->rewardPaid) {
                DataStore::instance().addGold(post->authorId, 50);
                post->rewardPaid = true;
            }
        }
    } else if (action == "remove") {
        // 撤下已通过的题解：approved -> removed（金币不回退，但不可再被直接通过）
        if (post->auditStatus == "approved") {
            post->auditStatus = "removed";
            post->reviewerId = user->id;
            post->auditTime = common::getTimeStr();
        }
    } else if (action == "reject") {
        bool wasApproved = (post->auditStatus == "approved");
        post->auditStatus = "rejected";
        post->reviewerId = user->id;
        post->auditTime = common::getTimeStr();
        post->rejectReason = reason.substr(0, 200);
        if (!wasApproved) {
            // 系统通知作者：被驳回
            Problem* pp = DataStore::instance().findProblemById(post->problemId);
            std::string notif = "你的题解《" + post->title + "》被管理员驳回";
            if (!pp) notif += "（相关题目已删除）";
            else notif += "（题目：" + pp->title + "）";
            if (!post->rejectReason.empty()) notif += "。原因：" + post->rejectReason;
            notif += "。可修改后重新提交审核。";
            pushSystemNotification(post->authorId, notif);
        }
    }
    DataStore::instance().savePosts();
    return "<script>window.location.href='/admin/solutions?status=" + common::urlEncode(post->auditStatus) + "'</script>";
}

std::string Handlers::handleProblemToggleSolution(const std::string& method, const std::string& path,
                                                  const std::string& body,
                                                  const std::map<std::string, std::string>& headers,
                                                  std::map<std::string, std::string>& respHeaders,
                                                  std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user || !isAdmin(user)) return "<script>window.location.href='/login'</script>";
    std::string problemId = parseFormData(body, "problemId");
    Problem* problem = DataStore::instance().findProblemById(problemId);
    if (problem) { problem->solutionClosed = !problem->solutionClosed; DataStore::instance().saveProblems(); }
    return "<script>window.location.href='/problem/" + problemId + "'</script>";
}
// ============================================================
// 题解：重新申请审核（作者在被驳回后可重新提交）
// ============================================================
std::string Handlers::handleSolutionResubmit(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string postId;
    size_t firstSlash = path.find('/', 1);
    if (firstSlash != std::string::npos) {
        size_t secondSlash = path.find('/', firstSlash + 1);
        if (secondSlash != std::string::npos) postId = path.substr(firstSlash + 1, secondSlash - firstSlash - 1);
    }
    Post* post = DataStore::instance().findPostById(postId);
    if (!post || post->type != SOLUTION) return "<script>window.location.href='/blog'</script>";
    if (post->authorId != user->id && !isAdmin(user)) return "<script>window.location.href='/post/" + postId + "'</script>";
    if (post->auditStatus == "approved") return "<script>window.location.href='/post/" + postId + "'</script>";
    post->auditStatus = "pending";
    post->rejectReason.clear();
    post->reviewerId.clear();
    post->auditTime.clear();
    DataStore::instance().savePosts();
    return "<script>window.location.href='/post/" + postId + "'</script>";
}
// ============================================================
// 评论
// ============================================================
std::string Handlers::handlePostComment(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    std::string postId;
    size_t firstSlash = path.find('/', 1);
    if (firstSlash != std::string::npos) {
        size_t secondSlash = path.find('/', firstSlash + 1);
        if (secondSlash != std::string::npos) {
            postId = path.substr(firstSlash + 1, secondSlash - firstSlash - 1);
        }
    }
    
    Post* post = DataStore::instance().findPostById(postId);
    if (!post) return "<script>window.location.href='/blog'</script>";
    
    std::string content = parseFormData(body, "content");
    if (content.empty()) return "<script>window.location.href='/post/" + postId + "'</script>";
    int _ml = security::getSecConfig().maxComment;
    if ((int)content.length() > _ml) content = content.substr(0, _ml);
    
    Comment comment;
    comment.id = common::genId();
    comment.authorId = user->id;
    comment.content = content;
    comment.createTime = common::getTimeStr();
    post->comments.push_back(comment);
    DataStore::instance().savePosts();
    
    return "<script>window.location.href='/post/" + postId + "'</script>";
}

// ============================================================
// 点赞
// ============================================================
std::string Handlers::handlePostLike(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    std::string postId;
    size_t firstSlash = path.find('/', 1);
    if (firstSlash != std::string::npos) {
        size_t secondSlash = path.find('/', firstSlash + 1);
        if (secondSlash != std::string::npos) {
            postId = path.substr(firstSlash + 1, secondSlash - firstSlash - 1);
        }
    }
    
    Post* post = DataStore::instance().findPostById(postId);
    if (post) {
        bool has = false;
        for (auto& uid : post->likedBy) if (uid == user->id) has = true;
        if (has) {
            std::vector<std::string> nv;
            for (auto& uid : post->likedBy) if (uid != user->id) nv.push_back(uid);
            post->likedBy = nv;
            if (post->likes > 0) post->likes--;
        } else {
            post->likedBy.push_back(user->id);
            post->likes++;
        }
        DataStore::instance().savePosts();
    }
    return "<script>window.location.href='/post/" + postId + "'</script>";
}

// ============================================================
// 讨论区（支持分页）
// ============================================================

std::string Handlers::handlePostVote(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string postId;
    size_t firstSlash = path.find('/', 1);
    if (firstSlash != std::string::npos) {
        size_t secondSlash = path.find('/', firstSlash + 1);
        if (secondSlash != std::string::npos) {
            postId = path.substr(firstSlash + 1, secondSlash - firstSlash - 1);
        }
    }
    Post* post = DataStore::instance().findPostById(postId);
    if (!post) return "<script>window.location.href='/blog'</script>";
    if (post->pollOptions.empty()) return "<script>window.location.href='/post/" + postId + "'</script>";
    bool voted = false;
    for (auto& vv : post->pollVotes) for (auto& uid : vv) if (uid == user->id) voted = true;
    if (voted) return "<script>window.location.href='/post/" + postId + "'</script>";
    int choice = common::stringToInt(parseFormData(body, "choice"));
    if (choice < 0 || choice >= (int)post->pollOptions.size()) return "<script>window.location.href='/post/" + postId + "'</script>";
    if ((int)post->pollVotes.size() <= choice) post->pollVotes.resize(post->pollOptions.size());
    post->pollVotes[choice].push_back(user->id);
    DataStore::instance().savePosts();
    return "<script>window.location.href='/post/" + postId + "'</script>";
}

std::string Handlers::handleDiscuss(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    if (!loadSystemConfig().allowDiscuss) {
        std::string c = "<div class='alert alert-info'>\u8ba8\u8bba\u533a\u5df2\u5173\u95ed</div><a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a>";
        return renderTemplate("\u8ba8\u8bba\u533a\u5df2\u5173\u95ed", c, getCurrentUser(sessionId));
    }
    User* user = getCurrentUser(sessionId);
    
    int page = 1;
    int perPage = 15;
    if (path.find("/discuss/page/") != std::string::npos) {
        size_t lastSlash = path.find_last_of('/');
        std::string pageStr = path.substr(lastSlash + 1);
        page = common::stringToInt(pageStr);
        if (page < 1) page = 1;
    } else {
        std::string ps = parseQueryString(path, "page");
        if (!ps.empty()) { page = common::stringToInt(ps); if (page < 1) page = 1; }
    }
    
    std::string kw = common::trim(parseQueryString(path, "q"));
    bool mineOnly = (parseQueryString(path, "mine") == "1");
    
    std::vector<Post*> discussPosts;
    for (auto& p : DataStore::instance().posts) {
        if (p.type != DISCUSSION) continue;
        if (mineOnly) { if (!user || p.authorId != user->id) continue; }
        if (!kw.empty()) {
            std::string hay = p.title + " " + p.content;
            if (hay.find(kw) == std::string::npos) continue;
        }
        discussPosts.push_back(&p);
    }
    
    int total = discussPosts.size();
    int totalPages = (total + perPage - 1) / perPage;
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    if (page < 1) page = 1;
    
    int start = (page - 1) * perPage;
    int end = std::min(start + perPage, total);
    
    std::string content;
    content += "<div style='display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:12px;'>";
    if (user) content += "<a href='/discuss/new' class='btn'>💬 发帖</a>";
    if (user) {
        std::string mhref = "/discuss?mine=1" + (!kw.empty() ? ("&q=" + common::urlEncode(kw)) : "");
        std::string mstyle = mineOnly ? "background:#27ae60;color:#fff;" : "";
        content += "<a class='btn btn-sm' style='"+mstyle+"' href='"+mhref+"'>我的</a>";
        if (mineOnly) content += "<a class='btn btn-sm' href='/discuss'>全部</a>";
    }
    content += "</div>";
    content += "<form action='/discuss' method='get' style='margin-bottom:14px;'>"
               "<div style='display:flex;gap:8px;'>"
               "<input type='text' name='q' value='" + common::htmlEscape(kw) + "' placeholder='搜索标题或内容…' style='flex:1;'/>"
               + (mineOnly ? "<input type='hidden' name='mine' value='1'/>" : "") +
               "<button class='btn' type='submit'>搜索</button></div></form>";
    
    for (int i = end - 1; i >= start; --i) {
        Post* post = discussPosts[i];
        User* author = DataStore::instance().findUserById(post->authorId);
        content += "<div class='card'>"
                  "<div class='card-title'><a href='/post/" + post->id + "'>" + common::htmlEscape(post->title) + "</a></div>"
                  "<div class='card-meta'>作者: <a href='/user/" + post->authorId + "'>" + 
                  (author ? common::htmlEscape(author->username) : "已注销") + "</a> | " + post->createTime +
                  " | 回复: " + std::to_string(post->comments.size()) + "</div>"
                  "<div style='margin-top:8px;color:#555;'>" + common::htmlEscape(post->content.substr(0, 150)) + 
                  (post->content.length() > 150 ? "..." : "") + "</div></div>";
    }
    
    if (discussPosts.empty()) {
        content += "<div class='empty-state'><span class='icon'>💬</span>暂无讨论帖</div>";
    }
    
    std::string dParams = mineOnly ? "mine=1" : "";
    if (!kw.empty()) { if (!dParams.empty()) dParams += "&"; dParams += "q=" + common::urlEncode(kw); }
    content += renderPaginationKeepParams("/discuss/page", page, totalPages, dParams);
    return renderTemplate("讨论区", content, user);
}

// ============================================================
// 发讨论帖
// ============================================================
std::string Handlers::handleDiscussNew(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    if (!loadSystemConfig().allowDiscuss) {
        std::string c = "<div class='alert alert-info'>\u8ba8\u8bba\u533a\u5df2\u5173\u95ed</div><a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a>";
        return renderTemplate("\u8ba8\u8bba\u533a\u5df2\u5173\u95ed", c, getCurrentUser(sessionId));
    }
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    if (method == "GET") {
        std::string content = "<form method='POST'><div class='form-group'><label>标题</label><input name='title' required/></div>"
                             "<div class='form-group'><label>内容</label><textarea name='content' rows='15' required></textarea></div>"
                             + security::captchaFieldsFor("discuss") +
                             "<div class='form-group'><label>\u6295\u7968\u6807\u9898\uff08\u53ef\u9009\uff09</label><input name='pollTitle'/></div>"
                             "<div class='form-group'><label>\u6295\u7968\u9009\u9879\uff08\u6bcf\u884c\u4e00\u4e2a\uff0c\u53ef\u9009\uff09</label><textarea name='pollOptions' rows='4'></textarea></div>"
                             "<input type='submit' value='发布讨论' class='btn'/> <a href='/discuss' class='btn'>取消</a></form>";
        return renderTemplate("发布讨论", content, user);
    } else {
        if (!security::verifyCaptchaFor(parseFormData(body, "captcha_token"), parseFormData(body, "captcha_answer"), "discuss")) {
            std::string c = "<div class='alert alert-error'>\u9a8c\u8bc1\u7801\u9519\u8bef\u6216\u5df2\u8fc7\u671f</div><a href='/discuss/new' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u53d1\u5e03\u5931\u8d25", c, user);
        }

        std::string title = parseFormData(body, "title");
        std::string contentText = parseFormData(body, "content");
        if (title.empty() || contentText.empty()) {
            std::string content = "<div class='alert alert-error'>标题和内容不能为空</div><a href='/discuss/new' class='btn'>返回</a>";
            return renderTemplate("发布失败", content, user);
        }
        security::SecConfig _sc = security::getSecConfig();
        if ((int)title.length() > _sc.maxBlogTitle) title = title.substr(0, _sc.maxBlogTitle);
        if ((int)contentText.length() > _sc.maxDiscuss) contentText = contentText.substr(0, _sc.maxDiscuss);

        if (security::throttleAction("discuss:" + user->id, _sc.discussIntervalSec)) {
            std::string c = "<div class='alert alert-error'>\u53d1\u5e03\u8fc7\u4e8e\u9891\u7e41\uff0c\u8bf7\u7a0d\u540e\u518d\u8bd5</div><a href='/discuss/new' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u53d1\u5e03\u5931\u8d25", c, user);
        }
        security::recordIpActivity(clientIpOf(headers), user->username);
        
        Post newPost;
        newPost.id = common::genId();
        newPost.authorId = user->id;
        newPost.type = DISCUSSION;
        newPost.title = title;
        newPost.content = contentText;
        newPost.createTime = common::getTimeStr();
        newPost.views = 0;
        newPost.likes = 0;
        std::string pollTitle = parseFormData(body, "pollTitle");
        std::string pollOptionsRaw = parseFormData(body, "pollOptions");
        if (!pollTitle.empty() && !pollOptionsRaw.empty()) {
            newPost.pollTitle = pollTitle;
            auto olines = common::split(pollOptionsRaw, '\n');
            for (auto& o : olines) {
                std::string ot = common::trim(o);
                if (!ot.empty()) newPost.pollOptions.push_back(ot);
            }
            newPost.pollVotes.resize(newPost.pollOptions.size());
        }
        
        DataStore::instance().posts.push_back(newPost);
        DataStore::instance().savePosts();
        
        std::string content = "<div class='alert alert-success'>讨论发布成功！</div>"
                             "<a href='/discuss' class='btn'>返回讨论区</a> <a href='/post/" + newPost.id + "' class='btn'>查看帖子</a>";
        return renderTemplate("发布成功", content, user);
    }
}

// ============================================================
// 犇犇
// ============================================================
std::string Handlers::handleBenben(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    if (!loadSystemConfig().allowBenben) {
        std::string error = "<div class='alert alert-error'>犇犇已关闭，暂时无法发布</div><a href='/' class='btn'>返回</a>";
        return renderTemplate("发布失败", error, user);
    }
    if (!security::verifyCaptchaFor(parseFormData(body, "captcha_token"), parseFormData(body, "captcha_answer"), "benben")) {
        std::string error = "<div class='alert alert-error'>验证码错误或已过期</div><a href='/' class='btn'>返回</a>";
        return renderTemplate("发布失败", error, user);
    }
    if (security::throttleAction("benben:" + user->id, security::getSecConfig().benbenIntervalSec)) {
        std::string error = "<div class='alert alert-error'>发布过于频繁，请稍后再试</div><a href='/' class='btn'>返回</a>";
        return renderTemplate("发布失败", error, user);
    }
    security::recordIpActivity(clientIpOf(headers), user->username);
    
    std::string content = parseFormData(body, "content");
    if (content.empty()) {
        std::string error = "<div class='alert alert-error'>内容不能为空</div><a href='/' class='btn'>返回</a>";
        return renderTemplate("发布失败", error, user);
    }
    int maxLen = security::getSecConfig().maxBenben;
    if ((int)content.length() > maxLen) content = content.substr(0, maxLen);
    
    Post newPost;
    newPost.id = common::genId();
    newPost.authorId = user->id;
    newPost.type = BENBEN;
    newPost.title = "";
    newPost.content = content;
    newPost.createTime = common::getTimeStr();
    newPost.views = 0;
    newPost.likes = 0;
    
    DataStore::instance().posts.push_back(newPost);
    DataStore::instance().savePosts();
    return "<script>window.location.href='/'</script>";
}

// ============================================================
// 打卡
// ============================================================
std::string Handlers::handleCheckin(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    std::string today = common::getDateStr();
    if (user->lastCheckin == today) {
        std::string content = "<div class='alert alert-info'>今天已经打卡过了！</div><a href='/' class='btn'>返回首页</a>";
        return renderTemplate("打卡", content, user);
    }
    
    if (!user->lastCheckin.empty()) {
        time_t t = time(NULL);
        struct tm* tm = localtime(&t);
        tm->tm_mday -= 1;
        mktime(tm);
        char buf[16];
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        std::string yesterday = std::string(buf);
        user->checkinDays = (user->lastCheckin == yesterday) ? user->checkinDays + 1 : 1;
    } else {
        user->checkinDays = 1;
    }
    
    user->lastCheckin = today;
    DataStore::instance().saveUsers();
    
    std::string content = "<div class='alert alert-success'>🎉 打卡成功！连续打卡 " + std::to_string(user->checkinDays) + " 天</div><a href='/' class='btn'>返回首页</a>";
    int goldReward = 30 * getUserEffCoeff(user) / 100;
    int expReward = 20 * getUserEffCoeff(user) / 100;
    DataStore::instance().addGold(user->id, goldReward);
    DataStore::instance().addExperience(user->id, expReward);
    content += "<div style='margin:10px 0;'><b>🪙 金币 +" + std::to_string(goldReward) + "</b> | <b>⚡ 经验 +" + std::to_string(expReward) + "</b></div>";
    return renderTemplate("打卡成功", content, user);
}

// ============================================================
// 比赛列表（支持分页）
// ============================================================
std::string Handlers::handleContests(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    
    std::string filter;
    if (path.find("/contests/upcoming") != std::string::npos) filter = "upcoming";
    else if (path.find("/contests/ongoing") != std::string::npos) filter = "ongoing";
    else if (path.find("/contests/past") != std::string::npos) filter = "past";
    else if (path.find("/contests/all") != std::string::npos) filter = "all";
    else filter = "auto"; // /contests 默认：有进行中自动进进行中，否则进未开始

    if (filter == "auto") {
        filter = "upcoming";
        time_t now = time(NULL);
        for (auto& c : DataStore::instance().contests) {
            time_t st = common::parseTime(c.startTime);
            time_t en = common::parseTime(c.endTime);
            if (now >= st && now <= en) { filter = "ongoing"; break; }
        }
    }
    
    int page = 1;
    int perPage = 10;
    if (path.find("/contests/page/") != std::string::npos) {
        size_t lastSlash = path.find_last_of('/');
        std::string pageStr = path.substr(lastSlash + 1);
        page = common::stringToInt(pageStr);
        if (page < 1) page = 1;
    }
    
    std::vector<Contest*> filteredContests;
    for (auto& c : DataStore::instance().contests) {
        time_t now = time(NULL);
        time_t start = common::parseTime(c.startTime);
        time_t end = common::parseTime(c.endTime);
        std::string status;
        if (now > end) status = "past";
        else if (now >= start) status = "ongoing";
        else status = "upcoming";
        
        if (filter == "all" || filter == status) {
            filteredContests.push_back(&c);
        }
    }
    
    int total = filteredContests.size();
    int totalPages = (total + perPage - 1) / perPage;
    if (page > totalPages) page = totalPages;
    if (page < 1) page = 1;
    
    int start = (page - 1) * perPage;
    int end = std::min(start + perPage, total);
    
    std::string content;
    if (isAdmin(user)) content += "<a href='/admin/contest/add' class='btn'>➕ 创建比赛</a><br/><br/>";
    
    content += "<div style='display:flex;gap:10px;margin-bottom:15px;flex-wrap:wrap;'>";
    content += "<a href='/contests/all' class='btn" + std::string(filter == "all" ? "" : "-outline") + "'>全部</a>";
    content += "<a href='/contests/upcoming' class='btn" + std::string(filter == "upcoming" ? "" : "-outline") + "'>未开始</a>";
    content += "<a href='/contests/ongoing' class='btn" + std::string(filter == "ongoing" ? "" : "-outline") + "'>进行中</a>";
    content += "<a href='/contests/past' class='btn" + std::string(filter == "past" ? "" : "-outline") + "'>已结束</a>";
    content += "</div>";
    
    bool hasContests = false;
    for (int i = start; i < end; ++i) {
        Contest* contest = filteredContests[i];
        time_t now = time(NULL);
        time_t startTime = common::parseTime(contest->startTime);
        time_t endTime = common::parseTime(contest->endTime);
        
        std::string status;
        std::string statusColor;
        if (now > endTime) {
            status = "已结束";
            statusColor = "#e74c3c";
        } else if (now >= startTime) {
            status = "进行中";
            statusColor = "#2ecc71";
        } else {
            status = "未开始";
            statusColor = "#f39c12";
        }
        
        hasContests = true;
        std::string ratedTag = contest->isRated ? "⭐" : "📝";
        std::string ratedText = contest->isRated ? "Rated" : "Unrated";
        std::string ratedColor = contest->isRated ? "#f39c12" : "#7f8c8d";
        
        content += "<div class='card'>"
                  "<div class='card-title'><a href='/contest/" + contest->id + "'>" + 
                  common::htmlEscape(contest->title) + "</a> "
                  "<span style='font-size:14px;color:" + ratedColor + ";'>" + ratedTag + " " + ratedText + "</span></div>"
                  "<div class='card-meta'>"
                  "🕐 开始: " + contest->startTime + " | 结束: " + contest->endTime +
                  " | 📚 题目: " + std::to_string(contest->problems.size()) + 
                  " | 👥 参赛: " + std::to_string(contest->participants.size()) +
                  " | <span style='color:" + statusColor + ";font-weight:bold;'>" + status + "</span>"
                  "</div>"
                  "<div style='margin-top:8px;'>" + common::htmlEscape(contest->description.substr(0, 100)) + 
                  (contest->description.length() > 100 ? "..." : "") + "</div>"
                  "</div>";
    }
    
    if (!hasContests) {
        content += "<div class='empty-state'><span class='icon'>🏆</span>暂无比赛</div>";
    }
    
    content += renderPagination("/contests/page", page, totalPages);
    return renderTemplate("比赛列表", content, user);
}

// ============================================================
// Rating 计算
// ============================================================
void Handlers::calculateRatingChanges(Contest& contest) {
    if (contest.isFinished) return;
    if (!contest.isRated) return;
    if (contest.participants.size() < 2) return;

    // 1. 计算每个参赛者的得分（按题目 AC 得分累加）
    std::map<std::string, int> scores;
    for (const auto& pid : contest.participants) {
        scores[pid] = 0;
        for (const auto& cp : contest.problems) {
            for (const auto& sub : DataStore::instance().submissions) {
                if (sub.userId == pid && sub.problemId == cp.problemId && sub.status == AC) {
                    scores[pid] += cp.score;
                    break;
                }
            }
        }
    }

    // 2. 排序确定实际排名
    std::vector<std::pair<std::string, int>> sorted;
    for (const auto& kv : scores) sorted.push_back(kv);
    std::sort(sorted.begin(), sorted.end(),
              [&](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                  if (a.second != b.second) return a.second > b.second;
                  // 罚时统计（简化）
                  int pa = 0, pb = 0;
                  for (const auto& sub : DataStore::instance().submissions) {
                      if (sub.userId == a.first) pa++;
                      if (sub.userId == b.first) pb++;
                  }
                  return pa < pb;
              });

    int n = sorted.size();
    int K = 320;  // 基础 K 因子

    // 对每个选手，计算与所有其他选手的 Elo 期望得分
    for (int i = 0; i < n; ++i) {
        const std::string& userId = sorted[i].first;
        User* user = DataStore::instance().findUserById(userId);
        if (!user) continue;

        double expectedScore = 0.0;
        double actualScore = 0.0;

        // 实际得分：赢的对手数（排名更靠前）
        // 排名 i，赢了 i 个对手（排名在 i 之前）？实际上排名靠前的选手（i 小）赢了所有排名在后的？我们使用标准：实际得分 = 击败的对手数（即比自己排名靠后的对手数）
        // 但我们可以更简单地：实际得分为 (n - 1 - i)，即他击败了排名在他之后的选手数
        // 但若考虑分数不相等的情况，更精确的是按得分比较，但通常排名已按得分排序，可认为排名即表现。
        // 这里用排名：实际得分为 (n - 1 - i)，即他击败了 (n-1-i) 个对手（假定得分高于所有排在他后面的选手）
        actualScore = (double)(n - 1 - i);

        // 期望得分：与所有其他选手比较
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            const std::string& otherId = sorted[j].first;
            User* other = DataStore::instance().findUserById(otherId);
            if (!other) continue;
            double diff = (double)(other->rating - user->rating) / 400.0;
            double winProb = 1.0 / (1.0 + std::pow(10.0, diff));
            expectedScore += winProb;
        }

        // 变化量 = K * (实际得分 - 期望得分) / (n-1)
        int delta = (int)(K * (actualScore - expectedScore) / (n - 1));

        // 限制范围
        if (delta > 50) delta = 50;
        if (delta < -50) delta = -50;

        // 新用户加倍
        if (user->ratedMatches < 5) {
            delta = (int)(delta * 1.5);
            if (delta > 75) delta = 75;
            if (delta < -75) delta = -75;
        }

        user->rating += delta;
        if (user->rating < 100) user->rating = 100;
        if (user->rating > 4000) user->rating = 4000;
        if (user->rating > user->maxRating) user->maxRating = user->rating;
        user->ratedMatches++;
        // 记录 Rating 变化历史（用于个人曲线可视化）
        user->ratingHistory.push_back(common::getTimeStr() + "|" + std::to_string(delta)
                                      + "|" + std::to_string(user->rating) + "|" + contest.title);
        if ((int)user->ratingHistory.size() > 500)
            user->ratingHistory.erase(user->ratingHistory.begin(), user->ratingHistory.begin() + (user->ratingHistory.size() - 500));

        contest.ratingChanges[userId] = delta;
    }

    contest.isFinished = true;
    DataStore::instance().saveUsers();
    DataStore::instance().saveContests();
}

// ============================================================
// 比赛详情
// ============================================================
std::string Handlers::handleContestDetail(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);

    // 剥离 query，解析 tab 参数（/contest/<id>?tab=tasks|ranking|submissions|feedback|rating）
    std::string rawPath = path;
    std::string query;
    size_t qp = rawPath.find('?');
    if (qp != std::string::npos) { query = rawPath.substr(qp + 1); rawPath = rawPath.substr(0, qp); }
    std::string tab = "tasks";
    {
        size_t tp = query.find("tab=");
        if (tp != std::string::npos) {
            size_t te = query.find('&', tp);
            std::string v = query.substr(tp + 4, te == std::string::npos ? std::string::npos : te - tp - 4);
            if (v == "ranking" || v == "submissions" || v == "feedback" || v == "rating") tab = v;
        }
    }

    size_t lastSlash = rawPath.find_last_of('/');
    std::string contestId = rawPath.substr(lastSlash + 1);
    Contest* contest = DataStore::instance().findContestById(contestId);
    
    if (!contest) {
        std::string content = "<div class='alert alert-error'>比赛不存在</div><a href='/contests' class='btn'>返回</a>";
        return renderTemplate("错误", content, user);
    }
    
    time_t now = time(NULL);
    time_t startTime = common::parseTime(contest->startTime);
    time_t endTime = common::parseTime(contest->endTime);
    
    bool isStarted = (now >= startTime);
    bool isEnded = (now > endTime);
    bool isAdminUser = isAdmin(user);
    
    if (!isStarted && !isAdminUser) {
        std::string content = "<div class='alert alert-info' style='padding:20px;text-align:center;'>"
                             "<h2>⏳ 比赛尚未开始</h2>"
                             "<p style='font-size:16px;margin:15px 0;'>开始时间: <strong>" + contest->startTime + "</strong></p>"
                             "<a href='/contests' class='btn' style='margin-top:15px;'>返回比赛列表</a>"
                             "</div>";
        return renderTemplate("比赛未开始 - " + contest->title, content, user);
    }
    
    if (isEnded && contest->isRated && !contest->isFinished) {
        Handlers::calculateRatingChanges(*contest);
        DataStore::instance().saveContests();
    }
    
    std::string statusText;
    std::string statusColor;
    if (isEnded) {
        statusText = "🏁 已结束";
        statusColor = "#e74c3c";
    } else if (isStarted) {
        statusText = "🟢 进行中";
        statusColor = "#2ecc71";
    } else {
        statusText = "⏳ 未开始";
        statusColor = "#f39c12";
    }
    
    std::string ratedTag = contest->isRated ? "⭐ Rated" : "📝 Unrated";
    std::string ratedColor = contest->isRated ? "#f39c12" : "#7f8c8d";
    std::string fmtTag = (contest->format == "oi") ? "📐 OI" : (contest->format == "ioi" ? "🪙 IOI" : "🏆 ICPC");
    std::string fmtColor = (contest->format == "oi") ? "#8e44ad" : (contest->format == "ioi" ? "#16a085" : "#2980b9");
    
    std::string content = "";
    content += "<h2>" + common::htmlEscape(contest->title) + "</h2>";
    content += "<div style='color:#7f8c8d;margin-bottom:20px;'>";
    content += "开始: " + contest->startTime + " | 结束: " + contest->endTime;
    content += " | <span style='color:" + statusColor + ";font-weight:bold;'>" + statusText + "</span>";
    content += " | <span style='color:" + ratedColor + ";font-weight:bold;'>" + ratedTag + "</span>";
    content += " | <span style='color:" + fmtColor + ";font-weight:bold;'>" + fmtTag + "</span>";
    if (contest->isTeam) content += " | <span style='color:#c0392b;font-weight:bold;'>👥 组队赛</span>";
    content += "</div>";
    content += "<div style='margin-bottom:20px;padding:15px;background:#f8f9fa;border-radius:6px;'>" 
             + common::htmlEscape(contest->description) + "</div>";

    // ---------- Tab 导航条 ----------
    {
        std::string tabs[5] = { "tasks", "ranking", "submissions", "feedback", "rating" };
        std::string labels[5] = { "📚 Tasks", "🏆 排行榜", "🧾 提交记录", "🔔 赛时反馈", "📊 Rating变化" };
        std::string nav = "<div class='contest-tabs' style='display:flex;gap:6px;flex-wrap:wrap;margin:16px 0 20px;border-bottom:2px solid var(--border-soft);padding-bottom:10px;'>";
        for (int i = 0; i < 5; i++) {
            bool act = (tab == tabs[i]);
            nav += "<a href='/contest/" + contest->id + "?tab=" + tabs[i] + "' class='contest-tab" + (act ? " active" : "") + "' style='padding:7px 14px;border-radius:8px;text-decoration:none;font-size:14px;font-weight:" + (act?"700":"500") + ";background:" + (act?"var(--accent)":"transparent") + ";color:" + (act?"#fff":"var(--text)") + ";'>" + labels[i] + "</a>";
        }
        nav += "</div>";
        content += nav;
    }

    // ---------- 组队管理面板（组队赛时显示） ----------
    if (contest->isTeam) {
        content += buildTeamPanel(contest, user);
    }
    
    // 题目列表（含 AC 状态）
    if (tab == "tasks" && (isStarted || isAdminUser)) {
        content += "<h3>📚 题目列表</h3>";
        if (contest->problems.empty()) {
            content += "<div class='alert alert-info'>暂无题目</div>";
        } else {
            content += "<table><thead><tr><th>#</th><th>题目</th><th>分数</th>";
            if (user) content += "<th>状态</th>";
            content += "</tr></thead><tbody>";
            
            int idx = 1;
            for (size_t i = 0; i < contest->problems.size(); ++i) {
                const ContestProblem& cp = contest->problems[i];
                Problem* p = DataStore::instance().findProblemById(cp.problemId);
                std::string title = p ? common::htmlEscape(p->title) : "已删除";
                std::string link = "/problem/" + cp.problemId + "?contest_id=" + contest->id;
                
                content += "<tr><td>" + std::to_string(idx++) + "</td>"
                          "<td><a href='" + link + "'>" + title + "</a></td>"
                          "<td>" + std::to_string(cp.score) + "</td>";
                
                if (user) {
                    std::string acSubId = "";
                    for (auto it = DataStore::instance().submissions.rbegin(); 
                         it != DataStore::instance().submissions.rend(); ++it) {
                        if (it->userId == user->id && it->problemId == cp.problemId && it->status == AC) {
                            acSubId = it->id;
                            break;
                        }
                    }
                    if (!acSubId.empty()) {
                        content += "<td><a href='/submission/" + acSubId + "' style='color:#2ecc71;font-weight:bold;text-decoration:none;'>✅ AC</a></td>";
                    } else {
                        content += "<td style='color:#95a5a6;'>⬜ 未AC</td>";
                    }
                }
                content += "</tr>";
            }
            content += "</tbody></table>";
        }
    } else if (tab == "tasks") {
        content += "<div class='alert alert-info'>📢 比赛开始后显示题目</div>";
    }
    
    // 锁定题目 / Hack面板（比赛进行中，参与者可锁自己的AC题）
    if (tab == "tasks" && isStarted && !isEnded && user) {
        bool isPart = false;
        for (size_t i = 0; i < contest->participants.size(); ++i) if (contest->participants[i] == user->id) { isPart = true; break; }
        if (isPart) {
            bool hasAC = false;
            std::string lk = "<div class='card' style='margin-top:20px;'><h3>&#128274; 锁定题目 &amp; Hack</h3>"
                             "<div style='color:#7f8c8d;font-size:13px;margin-bottom:8px;'>锁题后不可再提交，且你的提交公开可被Hack；锁题前他人看不到你的比赛提交。</div>"
                             "<table><thead><tr><th>题目</th><th>我的状态</th><th>操作</th></tr></thead><tbody>";
            for (size_t i = 0; i < contest->problems.size(); ++i) {
                const ContestProblem& cp = contest->problems[i];
                bool myLocked = false, myAC = false; std::string mySubId;
                for (auto it = DataStore::instance().submissions.rbegin(); it != DataStore::instance().submissions.rend(); ++it) {
                    if (it->userId == user->id && it->problemId == cp.problemId && it->contestId == contestId) {
                        if (it->locked) myLocked = true;
                        if (it->status == AC) { myAC = true; if (mySubId.empty()) mySubId = it->id; }
                    }
                }
                if (!myAC) continue;
                hasAC = true;
                std::string st, act;
                if (myLocked) {
                    st = "<span style='color:#e67e22;'>&#128274; 已锁定（公开，可被Hack）</span>";
                    act = "<a href='/submission/" + mySubId + "' class='btn btn-sm btn-outline'>查看</a>";
                } else {
                    st = "<span style='color:#27ae60;'>&#9989; AC（未锁定）</span>";
                    act = "<form method='POST' action='/contest/lock' style='display:inline;'>"
                          "<input type='hidden' name='contestId' value='" + contestId + "'/>"
                          "<input type='hidden' name='problemId' value='" + cp.problemId + "'/>"
                          "<input type='submit' value='&#128274; 锁定' class='btn btn-sm' style='background:#e67e22;'/></form>";
                }
                lk += "<tr><td><a href='/problem/" + cp.problemId + "?contest=" + contestId + "'>" + common::htmlEscape(cp.problemId) + "</a></td><td>" + st + "</td><td>" + act + "</td></tr>";
            }
            if (!hasAC) lk += "<tr><td colspan='3' style='text-align:center;color:#95a5a6;'>你还没有AC任何题目</td></tr>";
            lk += "</tbody></table></div>";
            content += lk;
        }
    }

    // 参赛按钮
    if (tab == "tasks" && !isEnded && user) {
        bool isParticipant = false;
        for (size_t i = 0; i < contest->participants.size(); ++i) {
            if (contest->participants[i] == user->id) { isParticipant = true; break; }
        }
        if (!isParticipant && isStarted) {
            content += "<form method='POST' action='/contest/" + contestId + "/join' style='margin-top:20px;'>"
                      "<input type='submit' value='参加比赛' class='btn'/></form>";
        } else if (isParticipant && isStarted) {
            content += "<div class='alert alert-success' style='margin-top:20px;'>✅ 你已参加此比赛</div>";
        } else if (!isStarted) {
            content += "<div class='alert alert-info' style='margin-top:20px;'>⏳ 比赛尚未开始，请等待</div>";
        }
    } else if (tab == "tasks" && isEnded) {
        content += "<div class='alert alert-info' style='margin-top:20px;'>🏁 比赛已结束</div>";
    } else if (tab == "tasks" && !user) {
        content += "<div class='alert alert-info' style='margin-top:20px;'><a href='/login'>登录</a> 后可以参加比赛</div>";
    }
    
    // 排名
    if (tab == "ranking" && (isStarted || isEnded)) {
        content += buildContestRanking(contest);
    }
    
    // 提交记录（Tasks 之外的 tab）
    if (tab == "submissions") {
        content += "<h3>🧾 比赛提交记录</h3>";
        bool found = false;
        // 收集本场比赛提交（倒序，最新在前）
        std::vector<const Submission*> subs;
        for (auto it = DataStore::instance().submissions.rbegin(); it != DataStore::instance().submissions.rend(); ++it) {
            if (it->contestId == contestId) {
                // 可见性：本人自己的提交始终可见；他人提交需已锁定或比赛已结束
                if (user && it->userId == user->id) { subs.push_back(&(*it)); continue; }
                if (isEnded) { subs.push_back(&(*it)); continue; }
                if (it->locked) { subs.push_back(&(*it)); continue; }
            }
        }
        if (subs.empty()) {
            content += "<div class='alert alert-info'>暂无提交记录</div>";
        } else {
            content += "<table><thead><tr><th>时间</th><th>用户</th><th>题目</th><th>状态</th><th>分数</th></tr></thead><tbody>";
            int shown = 0;
            for (size_t i = 0; i < subs.size() && shown < 200; i++) {
                const Submission* s = subs[i];
                User* u = DataStore::instance().findUserById(s->userId);
                Problem* pp = DataStore::instance().findProblemById(s->problemId);
                std::string st;
                // 比赛详情页：显示真实结果（OI 赛时也公开，供 Hack）
                switch (s->status) {
                    case AC: st = "✅ AC"; break;
                    case WA: st = "❌ WA"; break;
                    case CE: st = "💥 CE"; break;
                    case TLE: st = "⏱ TLE"; break;
                    case MLE: st = "📈 MLE"; break;
                    case RE: st = "🚨 RE"; break;
                    default: st = "❓"; break;
                }
                std::string stColor = s->status == AC ? "#2ecc71" : "#e74c3c";
                content += "<tr><td>" + common::htmlEscape(s->submitTime) + "</td>"
                          "<td><a href='/user/" + s->userId + "'>" + (u ? common::htmlEscape(u->username) : "已注销") + "</a></td>"
                          "<td><a href='/problem/" + s->problemId + "?contest_id=" + contestId + "'>" + (pp ? common::htmlEscape(pp->title) : s->problemId) + "</a></td>"
                          "<td style='color:" + stColor + ";font-weight:600;'>" + st + "</td>"
                          "<td>" + std::to_string(s->score) + "</td></tr>";
                shown++;
            }
            content += "</tbody></table>";
            if (subs.size() > 200) content += "<div style='color:#7f8c8d;margin-top:8px;'>仅显示最近 200 条</div>";
        }
    }
    
    // 赛时反馈（每道题我的 AC / 尝试状态）
    if (tab == "feedback") {
        content += "<h3>🔔 赛时反馈</h3>";
        if (!user) {
            content += "<div class='alert alert-info'><a href='/login'>登录</a> 后查看你的赛时反馈</div>";
        } else if (!isStarted) {
            content += "<div class='alert alert-info'>比赛尚未开始</div>";
        } else {
            // 我的每道题最优提交；组队赛时统计全队
            std::vector<std::string> myUids;
            if (contest->isTeam) {
                for (auto& t : contest->teams) {
                    bool mine = (t.leaderId == user->id);
                    for (auto& m : t.memberIds) if (m == user->id) mine = true;
                    if (mine) {
                        myUids.push_back(t.leaderId);
                        for (auto& m : t.memberIds) myUids.push_back(m);
                        break;
                    }
                }
                if (myUids.empty()) myUids.push_back(user->id);
            } else {
                myUids.push_back(user->id);
            }
            std::string who = contest->isTeam ? "全队各题状态" : "我的各题状态";
            content += "<div class='card'><h4>" + who + "</h4>";
            content += "<table><thead><tr><th>题目</th><th>最优状态</th><th>得分</th><th>提交次数</th><th>操作</th></tr></thead><tbody>";
            bool any = false;
            for (size_t i = 0; i < contest->problems.size(); ++i) {
                const ContestProblem& cp = contest->problems[i];
                Problem* pp = DataStore::instance().findProblemById(cp.problemId);
                int best = -1, cnt = 0; std::string bestId;
                for (auto it = DataStore::instance().submissions.rbegin(); it != DataStore::instance().submissions.rend(); ++it) {
                    bool mine = false;
                    for (auto& uid : myUids) if (it->userId == uid) { mine = true; break; }
                    if (mine && it->problemId == cp.problemId && it->contestId == contestId) {
                        cnt++;
                        if (bestId.empty()) bestId = it->id;
                        int sc = it->status == AC ? 100 : it->score;
                        if (sc > best) best = sc;
                    }
                }
                any = true;
                std::string st;
                if (best < 0) st = "<span style='color:#95a5a6;'>未提交</span>";
                else if (best >= 100) st = "<span style='color:#2ecc71;font-weight:600;'>✅ AC</span>";
                else st = "<span style='color:#e67e22;font-weight:600;'>正在评测 / 部分分 " + std::to_string(best) + "</span>";
                content += "<tr><td><a href='/problem/" + cp.problemId + "?contest_id=" + contestId + "'>" + (pp ? common::htmlEscape(pp->title) : cp.problemId) + "</a></td>"
                          "<td>" + st + "</td><td>" + (best < 0 ? "-" : std::to_string(best)) + "</td>"
                          "<td>" + std::to_string(cnt) + "</td>";
                if (!bestId.empty()) content += "<td><a href='/submission/" + bestId + "' class='btn btn-sm btn-outline'>查看</a></td>";
                else content += "<td></td>";
                content += "</tr>";
            }
            if (!any) content += "<tr><td colspan='5' style='text-align:center;color:#95a5a6;'>暂无题目</td></tr>";
            content += "</tbody></table></div>";

            // 赛时反馈留言区
            content += "<div class='card' style='margin-top:18px;'><h4>💬 赛时留言</h4>";
            if (contest->comments.empty()) {
                content += "<div style='color:#95a5a6;font-size:14px;margin-bottom:10px;'>暂无留言，抢个沙发～</div>";
            } else {
                content += "<div style='max-height:280px;overflow-y:auto;margin-bottom:12px;'>";
                for (auto it = contest->comments.rbegin(); it != contest->comments.rend(); ++it) {
                    content += "<div style='padding:9px 4px;border-bottom:1px solid #f1f3f5;'>"
                              "<div style='font-size:13px;'><a href='/user/" + it->userId + "' style='color:#2980b9;font-weight:600;'>" + common::htmlEscape(it->username) + "</a> <span style='color:#95a5a6;font-size:12px;'>" + common::htmlEscape(it->time) + "</span></div>"
                              "<div style='margin-top:3px;'>" + common::htmlEscape(it->content) + "</div></div>";
                }
                content += "</div>";
            }
            content += "<form method='POST' action='/contest/" + contestId + "/comment' style='display:flex;gap:8px;align-items:center;'>"
                      "<input name='content' placeholder='说点什么…（比赛期间可讨论）' maxlength='500' style='flex:1;padding:8px;border:1px solid #ddd;border-radius:6px;'/>"
                      "<input type='submit' value='发送' class='btn btn-sm'/></form></div>";
        }
    }
    
    // Rating 变化
    if (tab == "rating" && contest->isFinished && contest->isRated) {
        content += "<h3 style='margin-top:30px;'>📊 Rating 变化</h3>";
        if (contest->ratingChanges.empty()) {
            content += "<div class='alert alert-info'>暂无变化</div>";
        } else {
            content += "<table><thead><tr><th>用户</th><th>变化</th><th>新 Rating</th></tr></thead><tbody>";
            for (std::map<std::string, int>::const_iterator it = contest->ratingChanges.begin(); 
                 it != contest->ratingChanges.end(); ++it) {
                const std::string& userId = it->first;
                int delta = it->second;
                User* u = DataStore::instance().findUserById(userId);
                if (u) {
                    std::string color = delta >= 0 ? "#2ecc71" : "#e74c3c";
                    content += "<tr><td><a href='/user/" + u->id + "'>" + common::htmlEscape(u->username) + "</a></td>"
                              "<td style='color:" + color + ";font-weight:bold;'>" + (delta >= 0 ? "+" : "") + std::to_string(delta) + "</td>"
                              "<td>" + std::to_string(u->rating) + "</td></tr>";
                }
            }
            content += "</tbody></table>";
        }
    }
    
    if (isAdminUser && !isStarted) {
        content += "<div class='alert alert-warning' style='margin-top:20px;'>🔑 <strong>管理员预览模式</strong> - 普通用户无法查看未开始的比赛内容</div>";
    }
    
    return renderTemplate("比赛详情 - " + contest->title, content, user);
}

// ============================================================
// 参加比赛
// ============================================================
std::string Handlers::handleContestJoin(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    size_t firstSlash = path.find('/', 1);
    if (firstSlash != std::string::npos) {
        size_t secondSlash = path.find('/', firstSlash + 1);
        if (secondSlash != std::string::npos) {
            std::string contestId = path.substr(firstSlash + 1, secondSlash - firstSlash - 1);
            Contest* contest = DataStore::instance().findContestById(contestId);
            if (contest) {
                time_t now = time(NULL);
                time_t startTime = common::parseTime(contest->startTime);
                if (now < startTime) {
                    std::string content = "<div class='alert alert-error'>比赛尚未开始，无法参加</div>"
                                         "<a href='/contest/" + contestId + "' class='btn'>返回</a>";
                    return renderTemplate("无法参加", content, user);
                }
                for (size_t i = 0; i < contest->participants.size(); ++i) {
                    if (contest->participants[i] == user->id) {
                        return "<script>window.location.href='/contest/" + contestId + "'</script>";
                    }
                }
                contest->participants.push_back(user->id);
                DataStore::instance().saveContests();
                return "<script>window.location.href='/contest/" + contestId + "'</script>";
            }
        }
    }
    return "<script>window.location.href='/contests'</script>";
}

// ============================================================
// 比赛排名构建（支持 ICPC/OI/IOI 赛制与组队赛）
// ============================================================
std::string Handlers::buildContestRanking(Contest* contest) {
    std::string content = "<h3 style='margin-top:30px;'>🏆 比赛排名</h3>";
    
    std::string format = contest->format.empty() ? "icpc" : contest->format;
    bool isICPC = (format == "icpc");
    bool isOI = (format == "oi");
    bool isIOI = (format == "ioi");
    bool isTeam = contest->isTeam;
    
    // ---------- 计算每个“实体”（个人 or 队伍）每道题的状态 ----------
    // 实体 key：个人为 userId，队伍为 "team:" + teamId
    struct EProg {
        int attempts = 0;
        bool solved = false;
        int acTimeMin = 0;   // AC 时刻距开始分钟数
        int maxScore = 0;    // 各题最高分（OI/IOI）
    };
    std::map<std::string, std::map<std::string, EProg> > progMap; // entity -> problemId -> prog
    std::map<std::string, std::string> entityName; // entity -> 显示名
    std::map<std::string, std::vector<std::string> > entityMembers; // 队伍成员列表
    
    time_t startTime = common::parseTime(contest->startTime);
    if (startTime == 0) startTime = time(NULL);
    
    // 收集实体
    std::vector<std::string> entities;
    if (isTeam) {
        for (auto& t : contest->teams) {
            std::string key = "team:" + t.id;
            entities.push_back(key);
            entityName[key] = t.name;
            std::vector<std::string> mems;
            mems.push_back(t.leaderId);
            for (auto& m : t.memberIds) mems.push_back(m);
            entityMembers[key] = mems;
        }
        if (entities.empty()) { content += "<div class='alert alert-info'>暂无队伍</div>"; return content; }
    } else {
        for (auto& pid : contest->participants) {
            entities.push_back(pid);
            User* u = DataStore::instance().findUserById(pid);
            entityName[pid] = u ? u->username : "已注销";
        }
        if (entities.empty()) { content += "<div class='alert alert-info'>暂无参赛者</div>"; return content; }
    }
    
    // 计算每题每个实体的状态
    for (auto& cp : contest->problems) {
        for (auto& ent : entities) {
            EProg ep;
            // 该实体对应的所有 userId
            std::vector<std::string> uids;
            if (isTeam) uids = entityMembers[ent];
            else uids.push_back(ent);
            
            std::string firstACSub;
            for (auto& sub : DataStore::instance().submissions) {
                if (sub.problemId != cp.problemId) continue;
                if (sub.contestId != contest->id) continue; // 只统计本场比赛提交
                // 是否属于该实体
                bool mine = false;
                for (auto& uid : uids) if (sub.userId == uid) { mine = true; break; }
                if (!mine) continue;
                ep.attempts++;
                int sc = sub.status == AC ? 100 : sub.score;
                if (sc > ep.maxScore) ep.maxScore = sc;
                if (sub.status == AC && !ep.solved) {
                    ep.solved = true;
                    time_t st = common::parseTime(sub.submitTime);
                    if (st >= startTime) ep.acTimeMin = (int)((st - startTime) / 60);
                    if (firstACSub.empty()) firstACSub = sub.id;
                }
            }
            progMap[ent][cp.problemId] = ep;
        }
    }
    
    // ---------- 汇总得分/罚时/AC数，用于排序 ----------
    std::map<std::string, int> totalScore, totalAC, totalPenalty, totalAcTime;
    for (auto& ent : entities) {
        totalScore[ent] = 0; totalAC[ent] = 0; totalPenalty[ent] = 0; totalAcTime[ent] = 0;
        for (auto& cp : contest->problems) {
            EProg& ep = progMap[ent][cp.problemId];
            if (ep.solved) {
                totalAC[ent]++;
                totalAcTime[ent] += ep.acTimeMin;
                if (isICPC) {
                    // ICPC：得分按题分（1 题得满分），罚时 = AC时刻 + 20×此前非AC提交
                    totalScore[ent] += cp.score;
                    totalPenalty[ent] += ep.acTimeMin + (ep.attempts - 1) * 20;
                } else {
                    // OI/IOI：AC 记满分；未 AC 取最高部分分
                    totalScore[ent] += cp.score;
                }
            } else if (!isICPC) {
                // OI/IOI 非AC题取部分分（按通过用例比例）
                int sc = ep.maxScore;
                totalScore[ent] += (int)((long long)cp.score * sc / 100);
            }
        }
    }
    
    // ---------- 排序 ----------
    std::vector<std::string> order = entities;
    if (isICPC) {
        std::sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
            if (totalAC[a] != totalAC[b]) return totalAC[a] > totalAC[b];
            if (totalPenalty[a] != totalPenalty[b]) return totalPenalty[a] < totalPenalty[b];
            return a < b;
        });
    } else {
        std::sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
            if (totalScore[a] != totalScore[b]) return totalScore[a] > totalScore[b];
            if (totalAcTime[a] != totalAcTime[b]) return totalAcTime[a] < totalAcTime[b];
            return a < b;
        });
    }
    
    // ---------- 渲染（洛谷风格：每题一列） ----------
    content += std::string("<table style='border-collapse:collapse;font-size:13px;'><thead><tr>")
             + "<th>排名</th><th>" + (isTeam ? "队伍 / 成员" : "用户") + "</th>";
    // 每题一列
    for (auto& cp : contest->problems) {
        content += "<th title='" + common::htmlEscape(cp.problemId) + "'>" + common::htmlEscape(cp.problemId) + "</th>";
    }
    // 汇总列
    content += isICPC ? "<th>AC</th><th>罚时</th></tr></thead><tbody>"
                      : "<th>总分</th></tr></thead><tbody>";
    
    int rank = 1;
    for (auto& ent : order) {
        std::string rankClass = "";
        if (rank == 1) rankClass = "rank-1";
        else if (rank == 2) rankClass = "rank-2";
        else if (rank == 3) rankClass = "rank-3";
        content += "<tr class='" + rankClass + "'><td style='text-align:center;'><strong>" + std::to_string(rank++) + "</strong></td>";
        // 名字/队伍列
        if (isTeam) {
            content += "<td><div style='font-weight:600;'>" + common::htmlEscape(entityName[ent]) + "</div>"
                      "<div style='font-size:12px;color:#777;'>";
            bool first = true;
            for (auto& uid : entityMembers[ent]) {
                User* u = DataStore::instance().findUserById(uid);
                if (!first) content += ", ";
                content += (u ? common::htmlEscape(u->username) : "已注销");
                first = false;
            }
            content += "</div></td>";
        } else {
            User* u = DataStore::instance().findUserById(ent);
            content += "<td><a href='/user/" + ent + "'>" + (u ? common::htmlEscape(u->username) : "已注销") + "</a></td>";
        }
        // 每题单元格
        for (auto& cp : contest->problems) {
            EProg& ep = progMap[ent][cp.problemId];
            std::string cell;
            std::string bg;
            std::string tip = "提交 " + std::to_string(ep.attempts) + " 次" + (ep.solved ? ", AC@" + std::to_string(ep.acTimeMin) + "min" : "");
            if (ep.solved) {
                // AC
                bg = "#eafaf1";
                if (isICPC) cell = "<span style='color:#27ae60;font-weight:700;'>AC</span>";
                else cell = "<span style='color:#27ae60;font-weight:700;'>" + std::to_string(cp.score) + "</span>";
            } else if (ep.attempts > 0) {
                // 已提交未AC
                if (isICPC) { bg = "#fdecea"; cell = "<span style='color:#e74c3c;font-weight:600;'>-" + std::to_string(ep.attempts) + "</span>"; }
                else {
                    int sc = (int)((long long)cp.score * ep.maxScore / 100);
                    bg = "#fef9e7"; cell = "<span style='color:#f39c12;font-weight:600;'>" + std::to_string(sc) + "</span>";
                }
            } else {
                cell = "<span style='color:#c8ccd1;'>-</span>";
            }
            content += "<td title='" + tip + "' style='text-align:center;background:" + bg + ";'>" + cell + "</td>";
        }
        // 汇总列
        if (isICPC) {
            content += "<td style='text-align:center;'><strong>" + std::to_string(totalAC[ent]) + "</strong></td>"
                      "<td style='text-align:center;'>" + std::to_string(totalPenalty[ent]) + "</td>";
        } else {
            content += "<td style='text-align:center;'><strong>" + std::to_string(totalScore[ent]) + "</strong></td>";
        }
        content += "</tr>";
    }
    content += "</tbody></table>";
    return content;
}


// ============================================================
// 组队赛队伍面板（显示我的队伍/所有队伍，提供建队/加入/邀请等）
// ============================================================
std::string Handlers::buildTeamPanel(Contest* contest, User* user) {
    std::string content = "<div class='card' style='margin:16px 0;'>";
    content += "<h3>👥 组队</h3>";
    
    // 我的队伍
    if (user) {
        Team* myTeam = nullptr;
        for (auto& t : contest->teams) {
            if (t.leaderId == user->id) { myTeam = &t; break; }
            for (auto& m : t.memberIds) if (m == user->id) { myTeam = &t; break; }
            if (myTeam) break;
        }
        
        if (myTeam) {
            bool isLeader = (myTeam->leaderId == user->id);
            content += "<div style='padding:12px;background:#f0f9ff;border:1px solid #c9e2f2;border-radius:8px;margin-bottom:14px;'>";
            content += "<b>我的队伍:</b> " + common::htmlEscape(myTeam->name);
            content += isLeader ? " <span style='color:#c0392b;'>（我是队长）</span>" : "";
            content += "<div style='margin-top:6px;font-size:13px;color:#555;'>队员: ";
            User* ld = DataStore::instance().findUserById(myTeam->leaderId);
            content += (ld ? common::htmlEscape(ld->username) : "已注销");
            for (auto& m : myTeam->memberIds) {
                User* mu = DataStore::instance().findUserById(m);
                content += "、" + (mu ? common::htmlEscape(mu->username) : "已注销");
            }
            content += "</div>";
            if (isLeader) {
                // 队长：邀请、审批、解散
                if (!myTeam->pendingApplications.empty()) {
                    content += "<div style='margin-top:10px;'><b>待审批申请:</b>";
                    for (auto& ap : myTeam->pendingApplications) {
                        User* au = DataStore::instance().findUserById(ap);
                        content += "<div style='margin:4px 0;'>" + (au ? common::htmlEscape(au->username) : "已注销") +
                                  " <form method='POST' action='/contest/" + contest->id + "/team/approve' style='display:inline;'>"
                                  "<input type='hidden' name='applyId' value='" + ap + "'/>"
                                  "<input type='hidden' name='action' value='accept'/>"
                                  "<input type='submit' value='同意' class='btn btn-sm' style='background:#27ae60;'/></form> "
                                  "<form method='POST' action='/contest/" + contest->id + "/team/approve' style='display:inline;'>"
                                  "<input type='hidden' name='applyId' value='" + ap + "'/>"
                                  "<input type='hidden' name='action' value='reject'/>"
                                  "<input type='submit' value='拒绝' class='btn btn-sm' style='background:#e74c3c;'/></form></div>";
                    }
                    content += "</div>";
                }
                content += "<form method='POST' action='/contest/" + contest->id + "/team/invite' style='display:flex;gap:8px;margin-top:10px;align-items:center;'>"
                          "<input name='username' placeholder='邀请用户(用户名)' style='flex:1;padding:7px;border:1px solid #ddd;border-radius:6px;'/>"
                          "<input type='submit' value='邀请' class='btn btn-sm'/></form>";
                content += "<form method='POST' action='/contest/" + contest->id + "/team/leave' style='display:inline;margin-top:6px;'>"
                          "<input type='submit' value='解散队伍' class='btn btn-sm' style='background:#e74c3c;' onclick='return confirm(\"确定解散队伍？\")'/></form>";
            } else {
                content += "<form method='POST' action='/contest/" + contest->id + "/team/leave' style='display:inline;margin-top:6px;'>"
                          "<input type='submit' value='退出队伍' class='btn btn-sm' style='background:#e67e22;'/></form>";
            }
            content += "</div>";
        } else {
            content += "<div style='padding:10px;background:#fff8e6;border:1px solid #f2e0b6;border-radius:8px;margin-bottom:14px;font-size:14px;'>"
                      "你还没有加入队伍。";
            bool hasApply = false, hasInvite = false;
            for (auto& t : contest->teams) {
                for (auto& pi : t.pendingInvites) if (pi == user->id) hasInvite = true;
                for (auto& pa : t.pendingApplications) if (pa == user->id) hasApply = true;
            }
            if (hasInvite) content += " <span style='color:#2980b9;'>（你有一个待确认的邀请，见下方队伍列表）</span>";
            if (hasApply) content += " <span style='color:#c0392b;'>（你已申请加入，等待队长审批）</span>";
            content += "</div>";
            content += "<form method='POST' action='/contest/" + contest->id + "/team/create' style='display:flex;gap:8px;align-items:center;margin-bottom:14px;'>"
                      "<input name='teamName' placeholder='创建队伍（输入队名）' style='flex:1;padding:7px;border:1px solid #ddd;border-radius:6px;'/>"
                      "<input type='submit' value='创建队伍' class='btn btn-sm'/></form>";
        }
    }
    
    // 队伍列表
    if (contest->teams.empty()) {
        content += "<div style='color:#95a5a6;font-size:14px;'>暂无队伍，快来创建第一支队伍吧！</div>";
    } else {
        content += "<table><thead><tr><th>队伍</th><th>队长</th><th>成员</th><th>操作</th></tr></thead><tbody>";
        for (auto& t : contest->teams) {
            User* ld = DataStore::instance().findUserById(t.leaderId);
            std::string members = "";
            for (auto& m : t.memberIds) {
                User* mu = DataStore::instance().findUserById(m);
                members += (mu ? common::htmlEscape(mu->username) : "已注销") + " ";
            }
            content += "<tr><td><b>" + common::htmlEscape(t.name) + "</b></td>"
                      "<td>" + (ld ? common::htmlEscape(ld->username) : "已注销") + "</td>"
                      "<td>" + (members.empty() ? "-" : members) + "</td><td>";
            // 操作按钮
            if (user) {
                bool inTeam = (t.leaderId == user->id);
                for (auto& m : t.memberIds) if (m == user->id) inTeam = true;
                bool isInvited = false;
                for (auto& pi : t.pendingInvites) if (pi == user->id) isInvited = true;
                bool hasApplied = false;
                for (auto& pa : t.pendingApplications) if (pa == user->id) hasApplied = true;
                if (!inTeam) {
                    if (isInvited) {
                        content += "<form method='POST' action='/contest/" + contest->id + "/team/join' style='display:inline;'>"
                                  "<input type='hidden' name='teamId' value='" + t.id + "'/>"
                                  "<input type='submit' value='接受邀请' class='btn btn-sm' style='background:#27ae60;'/></form>";
                    } else if (hasApplied) {
                        content += "<span style='color:#95a5a6;'>已申请</span>";
                    } else {
                        content += "<form method='POST' action='/contest/" + contest->id + "/team/join' style='display:inline;'>"
                                  "<input type='hidden' name='teamId' value='" + t.id + "'/>"
                                  "<input type='submit' value='申请加入' class='btn btn-sm'/></form>";
                    }
                } else {
                    content += "<span style='color:#27ae60;'>已加入</span>";
                }
            }
            content += "</td></tr>";
        }
        content += "</tbody></table>";
    }
    content += "</div>";
    return content;
}

// ============================================================
// 组队操作（创建/加入/邀请/审批/退出）
// ============================================================
std::string Handlers::handleContestTeam(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    // 解析 /contest/<id>/team/<action>
    size_t slash1 = path.find('/', 1);
    size_t slash2 = path.find('/', slash1 + 1);
    size_t slash3 = path.find('/', slash2 + 1);
    std::string contestId = path.substr(slash1 + 1, slash2 - slash1 - 1);
    std::string action = path.substr(slash3 + 1);
    Contest* contest = DataStore::instance().findContestById(contestId);
    if (!contest || !contest->isTeam) return "<script>window.location.href='/contest/" + contestId + "'</script>";
    
    auto save = [&]() { DataStore::instance().saveContests(); };
    
    // 判断用户是否已在某队伍
    auto findUserTeam = [&]() -> Team* {
        for (auto& t : contest->teams) {
            if (t.leaderId == user->id) return &t;
            for (auto& m : t.memberIds) if (m == user->id) return &t;
        }
        return nullptr;
    };
    
    if (action == "create") {
        std::string name = common::trim(parseFormData(body, "teamName"));
        if (name.empty()) return "<script>alert('队名不能为空');window.location.href='/contest/" + contestId + "'</script>";
        if (findUserTeam()) return "<script>alert('你已在一个队伍中');window.location.href='/contest/" + contestId + "'</script>";
        Team t;
        t.id = common::genId();
        t.name = name.substr(0, 50);
        t.leaderId = user->id;
        contest->teams.push_back(t);
        save();
        return "<script>window.location.href='/contest/" + contestId + "'</script>";
    }
    if (action == "invite") {
        Team* my = findUserTeam();
        if (!my || my->leaderId != user->id) return "<script>alert('只有队长可以邀请');window.location.href='/contest/" + contestId + "'</script>";
        std::string username = common::trim(parseFormData(body, "username"));
        User* target = nullptr;
        for (auto& u : DataStore::instance().users) if (u.username == username) { target = &u; break; }
        if (!target) return "<script>alert('用户不存在');window.location.href='/contest/" + contestId + "'</script>";
        if (target->id == user->id) return "<script>alert('不能邀请自己');window.location.href='/contest/" + contestId + "'</script>";
        // 检查是否已在某队伍
        for (auto& t : contest->teams) {
            if (t.leaderId == target->id) return "<script>alert('对方已在队伍中');window.location.href='/contest/" + contestId + "'</script>";
            for (auto& m : t.memberIds) if (m == target->id) return "<script>alert('对方已在队伍中');window.location.href='/contest/" + contestId + "'</script>";
        }
        // 去重
        bool exists = false;
        for (auto& pi : my->pendingInvites) if (pi == target->id) exists = true;
        if (!exists) my->pendingInvites.push_back(target->id);
        save();
        return "<script>window.location.href='/contest/" + contestId + "'</script>";
    }
    if (action == "join") {
        std::string teamId = parseFormData(body, "teamId");
        if (findUserTeam()) return "<script>alert('你已在一个队伍中');window.location.href='/contest/" + contestId + "'</script>";
        Team* t = nullptr;
        for (auto& tt : contest->teams) if (tt.id == teamId) { t = &tt; break; }
        if (!t) return "<script>window.location.href='/contest/" + contestId + "'</script>";
        // 若是被邀请则直接加入；否则发申请
        bool invited = false;
        for (size_t i = 0; i < t->pendingInvites.size(); i++) {
            if (t->pendingInvites[i] == user->id) { t->pendingInvites.erase(t->pendingInvites.begin() + i); invited = true; break; }
        }
        if (invited) {
            t->memberIds.push_back(user->id);
        } else {
            // 申请加入
            bool applied = false;
            for (auto& pa : t->pendingApplications) if (pa == user->id) applied = true;
            if (!applied) t->pendingApplications.push_back(user->id);
        }
        save();
        return "<script>window.location.href='/contest/" + contestId + "'</script>";
    }
    if (action == "approve") {
        Team* my = findUserTeam();
        if (!my || my->leaderId != user->id) return "<script>alert('只有队长可以审批');window.location.href='/contest/" + contestId + "'</script>";
        std::string applyId = parseFormData(body, "applyId");
        std::string act = parseFormData(body, "action");
        for (size_t i = 0; i < my->pendingApplications.size(); i++) {
            if (my->pendingApplications[i] == applyId) {
                if (act == "accept") my->memberIds.push_back(applyId);
                my->pendingApplications.erase(my->pendingApplications.begin() + i);
                break;
            }
        }
        save();
        return "<script>window.location.href='/contest/" + contestId + "'</script>";
    }
    if (action == "leave") {
        Team* my = findUserTeam();
        if (!my) return "<script>window.location.href='/contest/" + contestId + "'</script>";
        if (my->leaderId == user->id) {
            // 队长解散
            for (auto it = contest->teams.begin(); it != contest->teams.end(); ++it) {
                if (it->id == my->id) { contest->teams.erase(it); break; }
            }
        } else {
            for (size_t i = 0; i < my->memberIds.size(); i++) {
                if (my->memberIds[i] == user->id) { my->memberIds.erase(my->memberIds.begin() + i); break; }
            }
        }
        save();
        return "<script>window.location.href='/contest/" + contestId + "'</script>";
    }
    return "<script>window.location.href='/contest/" + contestId + "'</script>";
}

// ============================================================
// 比赛赛时反馈留言
// ============================================================
std::string Handlers::handleContestComment(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    size_t slash1 = path.find('/', 1);
    size_t slash2 = path.find('/', slash1 + 1);
    std::string contestId = path.substr(slash1 + 1, slash2 - slash1 - 1);
    Contest* contest = DataStore::instance().findContestById(contestId);
    if (!contest) return "<script>window.location.href='/contests'</script>";
    
    std::string content = common::trim(parseFormData(body, "content"));
    if (!content.empty()) {
        if (content.length() > 500) content = content.substr(0, 500);
        Contest::ContestComment cm;
        cm.userId = user->id;
        cm.username = user->username;
        cm.time = common::getTimeStr();
        cm.content = content;
        contest->comments.push_back(cm);
        // 限制留言数量，防止内存膨胀
        if (contest->comments.size() > 500) {
            contest->comments.erase(contest->comments.begin(), contest->comments.begin() + (contest->comments.size() - 500));
        }
        DataStore::instance().saveContests();
    }
    return "<script>window.location.href='/contest/" + contestId + "?tab=feedback'</script>";
}

// ============================================================
// OI 赛制：提交结果是否应对当前查看者隐藏
// （OI 比赛进行中，非本人/非管理员隐藏评测结果，赛后统一公开）
// ============================================================
bool Handlers::isContestResultHidden(const Submission* sub, User* viewer) {
    if (!sub || sub->contestId.empty()) return false;
    Contest* contest = DataStore::instance().findContestById(sub->contestId);
    if (!contest) return false;
    if (contest->format != "oi") return false;
    // 比赛结束后公开
    time_t now = time(NULL);
    time_t endTime = common::parseTime(contest->endTime);
    if (endTime != 0 && now > endTime) return false;
    // 本人可见自己的结果
    if (viewer && sub->userId == viewer->id) return false;
    // 管理员可见
    if (viewer && isAdmin(viewer)) return false;
    return true;
}

// ============================================================
// 题单列表
// ============================================================
std::string Handlers::handleProblemSets(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string content;
    if (isAdmin(user)) content += "<a href='/admin/set/add' class='btn'>➕ 创建题单</a><br/><br/>";
    
    for (const auto& ps : DataStore::instance().problemSets) {
        User* author = DataStore::instance().findUserById(ps.authorId);
        content += "<div class='card'><div class='card-title'><a href='/set/" + ps.id + "'>" + 
                  common::htmlEscape(ps.name) + "</a></div>"
                  "<div class='card-meta'>作者: <a href='/user/" + ps.authorId + "'>" + 
                  (author ? common::htmlEscape(author->username) : "已注销") + "</a> | " + ps.createTime +
                  " | 题目: " + std::to_string(ps.problemIds.size()) + "</div>"
                  "<div style='margin-top:8px;'>" + common::htmlEscape(ps.description.substr(0, 100)) + 
                  (ps.description.length() > 100 ? "..." : "") + "</div></div>";
    }
    return renderTemplate("题单列表", content, user);
}

// ============================================================
// 题单详情
// ============================================================
std::string Handlers::handleProblemSetDetail(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    size_t lastSlash = path.find_last_of('/');
    std::string setId = path.substr(lastSlash + 1);
    ProblemSet* ps = DataStore::instance().findProblemSetById(setId);
    
    if (!ps) {
        std::string content = "<div class='alert alert-error'>题单不存在</div><a href='/sets' class='btn'>返回</a>";
        return renderTemplate("错误", content, user);
    }
    
    User* author = DataStore::instance().findUserById(ps->authorId);
    std::string content = "<h2>" + common::htmlEscape(ps->name) + "</h2>"
                         "<div style='color:#7f8c8d;margin-bottom:20px;'>作者: <a href='/user/" + ps->authorId + "'>" + 
                         (author ? common::htmlEscape(author->username) : "已注销") + "</a> | " + ps->createTime + "</div>"
                         "<div style='margin-bottom:20px;'>" + common::htmlEscape(ps->description) + "</div>"
                         "<h3>📚 题目列表</h3><table><thead><tr><th>#</th><th>题目</th><th>通过率</th></tr></thead><tbody>";
    
    int idx = 1;
    for (const auto& pid : ps->problemIds) {
        Problem* p = DataStore::instance().findProblemById(pid);
        if (p) {
            int ac = DataStore::instance().getProblemACCount(pid);
            int total = DataStore::instance().getProblemSubmitCount(pid);
            double rate = total > 0 ? (double)ac / total * 100 : 0;
            content += "<tr><td>" + std::to_string(idx++) + "</td>"
                      "<td><a href='/problem/" + pid + "'>" + common::htmlEscape(p->title) + "</a></td>"
                      "<td>" + std::to_string(ac) + "/" + std::to_string(total) + " (" + std::to_string((int)rate) + "%)</td></tr>";
        }
    }
    content += "</tbody></table>";
    return renderTemplate("题单详情", content, user);
}

// ============================================================
// 私信
// ============================================================
// ============================================================
// 系统会话
// ============================================================
static const std::string kSystemId = "system";

static void pushSystemNotification(const std::string& userId, const std::string& content) {
    Message m;
    m.id = common::genId();
    m.fromId = kSystemId;
    m.toId = userId;
    m.content = content;
    m.sendTime = common::getTimeStr();
    m.read = false;
    DataStore::instance().messages.push_back(m);
    DataStore::instance().saveMessages();
}

// 聊天文本：URL 自动成链 + @用户名 高亮（其余全部转义）
static std::string chatTextHtml(const std::string& s) {
    std::string out;
    size_t i = 0, n = s.size();
    while (i < n) {
        // 服务端生成的 <a href='...'>文字</a>（系统通知）：只放行站内/外链 URL，文字仍转义
        if (s.compare(i, 9, "<a href='") == 0) {
            size_t q = s.find("'>", i + 9);
            size_t e = s.find("</a>", i);
            if (q != std::string::npos && e != std::string::npos && q < e) {
                std::string url = s.substr(i + 9, q - i - 9);
                bool okUrl = url.compare(0, 1, "/") == 0 || url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
                if (okUrl) {
                    out += "<a href='" + common::htmlEscape(url) + "' target='_blank' rel='noopener'>" +
                           common::htmlEscape(s.substr(q + 2, e - q - 2)) + "</a>";
                    i = e + 4;
                    continue;
                }
            }
        }
        if (s[i] == '@') {
            size_t k = i + 1;
            std::string nm;
            while (k < n && nm.size() < 60) {
                unsigned char c = (unsigned char)s[k];
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
                if (c < 0x80) { if (!(isalnum(c) || c == '_' || c == '.' || c == '-')) break; }
                else if (s.compare(k, 3, "\xEF\xBC\x8C") == 0 || s.compare(k, 3, "\xE3\x80\x82") == 0 ||
                         s.compare(k, 3, "\xEF\xBC\x9A") == 0 || s.compare(k, 3, "\xEF\xBC\x9B") == 0 ||
                         s.compare(k, 3, "\xEF\xBC\x81") == 0 || s.compare(k, 3, "\xEF\xBC\x9F") == 0) break;
                nm += s[k]; k++;
            }
            if (!nm.empty()) {
                out += "<a class='mention' href='/user/" + common::urlEncode(nm) + "'>@" + common::htmlEscape(nm) + "</a>";
                i = k; continue;
            }
        }
        if (s.compare(i, 7, "http://") == 0 || s.compare(i, 8, "https://") == 0) {
            size_t k = i;
            while (k < n && s[k] != ' ' && s[k] != '\n' && s[k] != '\r' && s[k] != '\t') k++;
            std::string url = s.substr(i, k - i);
            out += "<a href='" + common::htmlEscape(url) + "' target='_blank' rel='noopener'>" + common::htmlEscape(url) + "</a>";
            i = k; continue;
        }
        out += common::htmlEscape(std::string(1, s[i]));
        i++;
    }
    return out;
}

static std::string chatHumanSize(long long n) {
    char b[32];
    if (n >= 1024 * 1024) snprintf(b, sizeof(b), "%.1f MB", n / 1048576.0);
    else if (n >= 1024) snprintf(b, sizeof(b), "%.1f KB", n / 1024.0);
    else snprintf(b, sizeof(b), "%lld B", n);
    return std::string(b);
}

static std::string chatFileChip(const Message& m) {
    if (m.file.empty()) return "";
    std::string key = m.file;
    if (key.compare(0, 4, "msg/") == 0) key = key.substr(4);
    std::string nm = m.fileName.empty() ? key : m.fileName;
    std::string lower = nm;
    for (size_t i = 0; i < lower.size(); i++) lower[i] = (char)tolower((unsigned char)lower[i]);
    bool img = lower.size() > 4 && (lower.rfind(".png") == lower.size() - 4 || lower.rfind(".jpg") == lower.size() - 4 ||
                                    lower.rfind(".jpeg") == lower.size() - 5 || lower.rfind(".gif") == lower.size() - 4 ||
                                    lower.rfind(".webp") == lower.size() - 5);
    std::string h = "<a class='msg-file" + std::string(img ? " img" : "") + "' href='/messages/file/" +
                    common::urlEncode(key) + "' target='_blank' rel='noopener'>";
    if (img) h += "<img src='/messages/file/" + common::urlEncode(key) + "' alt='" + common::htmlEscape(nm) + "'/>";
    h += "<span class='mf-ic'>📎</span><span class='mf-name'>" + common::htmlEscape(nm) + "</span>";
    if (m.fileSize > 0) h += "<span class='mf-size'>" + chatHumanSize(m.fileSize) + "</span>";
    h += "</a>";
    return h;
}

// ---------- 私信 UI 小工具 ----------
// 取 UTF-8 首字符（头像占位，避免把中文截成半个字节）
static std::string firstUtf8Char(const std::string& s) {
    if (s.empty()) return "?";
    unsigned char c = (unsigned char)s[0];
    int len = 1;
    if (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    if ((size_t)len > s.size()) len = 1;
    return s.substr(0, len);
}
// 会话列表里的短时间：今天显示 HH:MM，其它显示 MM-DD
static std::string shortTimeLabel(const std::string& t) {
    if (t.size() < 16) return t;
    if (t.substr(0, 10) == common::getDateStr()) return t.substr(11, 5);
    return t.substr(5, 5);
}
// 头像 HTML（有图用图，否则用首字符/emoji）
static std::string convAvatar(const User* u, const std::string& fallbackEmoji) {
    if (u && !u->avatar.empty() && common::isBase64Image(u->avatar))
        return "<img src='" + u->avatar + "' alt=''/>";
    if (!fallbackEmoji.empty()) return "<span class='conv-av-t'>" + fallbackEmoji + "</span>";
    return "<span class='conv-av-t'>" + common::htmlEscape(firstUtf8Char(u ? u->username : std::string("?"))) + "</span>";
}
// 「正在输入」内存表（5 秒有效）
static std::mutex g_typingMutex;
static std::map<std::string, std::map<std::string, time_t> > g_typing;

// 收集某会话里正在输入的人（6 秒内有效）
static std::string typingNamesFor(const std::string& conv, const std::string& me, DataStore& ds) {
    std::string out;
    std::lock_guard<std::mutex> lk(g_typingMutex);
    time_t now = time(0);
    std::map<std::string, std::map<std::string, time_t> >::iterator it = g_typing.find(conv);
    if (it == g_typing.end()) return out;
    for (std::map<std::string, time_t>::iterator k = it->second.begin(); k != it->second.end(); ++k) {
        if (k->first == me) continue;
        if (now - k->second > 6) continue;
        User* u = ds.findUserById(k->first);
        if (!out.empty()) out += "、";
        out += u ? u->username : "有人";
    }
    return out;
}


static std::string chatBubbleMsg(const Message& m, bool mine, const std::string& fromName, int peerCount = -1) {
    std::string out = "<div class='bubble " + std::string(mine ? "mine" : "theirs") + "' id='m" + common::htmlEscape(m.id) + "'>";
    if (!mine && !fromName.empty())
        out += "<div class='bubble-from'>" + common::htmlEscape(fromName) + "</div>";
    if (!m.content.empty()) out += "<div class='bubble-text'>" + chatTextHtml(m.content) + "</div>";
    out += chatFileChip(m);
    out += "<div class='bubble-time'>" + m.sendTime;
    if (mine) {
        std::string readTxt;
        bool isGroupMsg = (!m.toId.empty() && m.toId[0] == 'g');
        if (isGroupMsg) {
            int total = peerCount > 0 ? peerCount - 1 : (int)m.readBy.size() - 1;
            int done = (int)m.readBy.size() - 1;
            if (total < 0) total = 0;
            if (done < 0) done = 0;
            if (done > total) done = total;
            readTxt = "已读 " + std::to_string(done) + "/" + std::to_string(total);
        } else {
            readTxt = m.read ? "已读" : "未读";
        }
        out += "<span class='bubble-read" + std::string((!isGroupMsg && m.read) ? " read" : "") + "' data-id='" + common::htmlEscape(m.id) + "'>" +
               readTxt + "</span>";
    }
    if (mine) out += "<button type='button' class='msg-del' data-id='" + common::htmlEscape(m.id) + "' title='撤回'>撤回</button>";
    out += "</div></div>";
    return out;
}

static std::string chatBubble(const std::string& t, const std::string& c, bool mine) {
    Message m;
    m.content = c; m.sendTime = t;
    return chatBubbleMsg(m, mine, "");
}
static std::string chatGroupBubble(const std::string& t, const std::string& name, const std::string& c, bool mine) {
    Message m;
    m.content = c; m.sendTime = t;
    return chatBubbleMsg(m, mine, name);
}


std::string Handlers::handleContactAdd(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    if (!loadSystemConfig().allowMessages) return "<script>window.location.href='/messages'</script>";
    std::string name = common::trim(parseFormData(body, "username"));
    DataStore& ds = DataStore::instance();
    User* target = ds.findUserByName(name);
    if (!target) target = ds.findUserById(name);
    if (!target || target->id == user->id) return "<script>window.location.href='/messages'</script>";
    bool exists = false;
    for (auto& cid : user->contacts) if (cid == target->id) exists = true;
    if (!exists) { user->contacts.push_back(target->id); ds.saveUsers(); }
    return "<script>window.location.href='/messages/chat/" + target->id + "'</script>";
}

std::string Handlers::handleMessages(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    if (!loadSystemConfig().allowMessages) {
        std::string c = "<div class='alert alert-info'>\u79c1\u4fe1\u5df2\u5173\u95ed</div><a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a>";
        return renderTemplate("\u79c1\u4fe1\u5df2\u5173\u95ed", c, getCurrentUser(sessionId));
    }
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    DataStore& ds = DataStore::instance();

    std::string active;       // \u4e2a\u4eba\u5bf9\u8bdd\u5bf9\u65b9 id
    std::string activeGroup;  // \u7fa4\u804a id
    if (path == "/messages" || path == "/messages/") active = kSystemId;
    else if (path == "/messages/system") active = kSystemId;
    else if (path.rfind("/messages/chat/group/", 0) == 0) activeGroup = path.substr(std::string("/messages/chat/group/").size());
    else if (path.rfind("/messages/chat/", 0) == 0) active = path.substr(std::string("/messages/chat/").size());

    // ---------- 侧栏数据（统一来源，避免联系人与会话重复出现） ----------
    int sysUnread = 0;
    std::map<std::string, int> convUnread, groupUnread;
    std::map<std::string, std::string> partnerName, groupName, lastTime, lastText, groupLast, groupLastText;
    bool changedRead = false;
    for (size_t gi = 0; gi < ds.groups.size(); gi++) {
        GroupChat& g = ds.groups[gi];
        bool isMember = false;
        for (size_t k = 0; k < g.memberIds.size(); k++) if (g.memberIds[k] == user->id) isMember = true;
        if (isMember) groupName[g.id] = g.name;
    }
    for (size_t i = 0; i < ds.messages.size(); i++) {
        Message& m = ds.messages[i];
        std::string preview = m.content;
        if (preview.empty() && !m.fileName.empty()) preview = "\U0001f4ce " + m.fileName;
        if (preview.size() > 36) {
            preview = preview.substr(0, 36);
            while (!preview.empty() && ((unsigned char)preview[preview.size() - 1] & 0xC0) == 0x80) preview.erase(preview.size() - 1);
            preview += "\u2026";
        }
        if (m.toId.size() >= 2 && m.toId[0] == 'g') {
            if (!groupName.count(m.toId.substr(1))) continue;
            groupLast[m.toId] = m.sendTime;
            groupLastText[m.toId] = preview;
            bool hasRead = false;
            for (size_t k = 0; k < m.readBy.size(); k++) if (m.readBy[k] == user->id) hasRead = true;
            if (m.fromId != user->id && !hasRead) groupUnread[m.toId]++;
            continue;
        }
        if (m.toId == user->id && m.fromId == kSystemId) { if (!m.read) sysUnread++; continue; }
        // 自己发给自己（备忘）：不计入会话，也不该永远显示未读
        if (m.fromId == user->id && m.toId == user->id) {
            if (!m.read) { m.read = true; changedRead = true; }
            continue;
        }
        std::string peer;
        if (m.fromId == user->id) peer = m.toId;
        else if (m.toId == user->id) peer = m.fromId;
        else continue;
        if (peer.empty()) continue;
        User* pu = ds.findUserById(peer);
        if (!pu) {
            // 发送者已注销：读不到也回不了，自动标记已读，避免出现「找不到来源」的未读
            if (m.toId == user->id && !m.read) { m.read = true; changedRead = true; }
            continue;
        }
        partnerName[peer] = pu->username;
        lastTime[peer] = m.sendTime;
        lastText[peer] = preview;
        if (m.toId == user->id && !m.read) convUnread[peer]++;
    }
    if (changedRead) ds.saveMessages();

    struct ConvRow {
        std::string key, href, av, name, preview, time;
        int unread;
        bool active;
    };
    std::vector<ConvRow> convRows, groupRows, contactRows;
    for (std::map<std::string, std::string>::iterator it = lastTime.begin(); it != lastTime.end(); ++it) {
        User* pu = ds.findUserById(it->first);
        if (!pu) continue;
        ConvRow r;
        r.key = it->first;
        r.href = "/messages/chat/" + it->first;
        r.av = convAvatar(pu, "");
        r.name = common::htmlEscape(pu->username);
        r.preview = common::htmlEscape(lastText[it->first]);
        r.time = shortTimeLabel(it->second);
        r.unread = convUnread.count(it->first) ? convUnread[it->first] : 0;
        r.active = (active == it->first);
        convRows.push_back(r);
    }
    std::sort(convRows.begin(), convRows.end(), [&](const ConvRow& a, const ConvRow& b) {
        return lastTime[a.key] > lastTime[b.key];
    });
    for (std::map<std::string, std::string>::iterator it = groupLast.begin(); it != groupLast.end(); ++it) {
        std::string gid = it->first.substr(1);
        GroupChat* gg = ds.findGroupById(gid);
        if (!gg) continue;
        ConvRow r;
        r.key = it->first;
        r.href = "/messages/chat/group/" + gid;
        r.av = convAvatar(NULL, "\U0001f465");
        r.name = common::htmlEscape(gg->name) + " <span class='conv-sub'>" + std::to_string(gg->memberIds.size()) + "</span>";
        r.preview = common::htmlEscape(groupLastText[it->first]);
        r.time = shortTimeLabel(it->second);
        r.unread = groupUnread.count(it->first) ? groupUnread[it->first] : 0;
        r.active = (activeGroup == gid);
        groupRows.push_back(r);
    }
    std::sort(groupRows.begin(), groupRows.end(), [&](const ConvRow& a, const ConvRow& b) {
        return groupLast[a.key] > groupLast[b.key];
    });
    for (size_t i = 0; i < user->contacts.size(); i++) {
        std::string cid = user->contacts[i];
        if (cid.empty() || cid == user->id || lastTime.count(cid)) continue;
        User* cu = ds.findUserById(cid);
        if (!cu) continue;
        ConvRow r;
        r.key = cid;
        r.href = "/messages/chat/" + cid;
        r.av = convAvatar(cu, "");
        r.name = common::htmlEscape(cu->username);
        r.preview = "\U0001f44b \u70b9\u5f00\u53d1\u6d88\u606f";
        r.time = "";
        r.unread = 0;
        r.active = false;
        contactRows.push_back(r);
    }

    int totalUnread = sysUnread;
    for (std::map<std::string, int>::iterator it = convUnread.begin(); it != convUnread.end(); ++it) totalUnread += it->second;
    for (std::map<std::string, int>::iterator it = groupUnread.begin(); it != groupUnread.end(); ++it) totalUnread += it->second;

    auto rowHtml = [](const ConvRow& r) {
        std::string h = "<a class='conv" + std::string(r.active ? " active" : "") + "' href='" + r.href + "'>";
        h += "<span class='conv-av'>" + r.av + "</span>";
        h += "<span class='conv-main'><span class='conv-name'>" + r.name + "</span>";
        h += "<span class='conv-last'>" + (r.preview.empty() ? std::string("&nbsp;") : r.preview) + "</span></span>";
        h += "<span class='conv-right'><span class='conv-time'>" + r.time + "</span>";
        if (r.unread > 0) h += "<span class='conv-badge'>" + (r.unread > 99 ? std::string("99+") : std::to_string(r.unread)) + "</span>";
        h += "</span></a>";
        return h;
    };

    std::string sb;
    sb += "<div class='cs-head'><span>\u79c1\u4fe1</span>";
    if (totalUnread > 0) sb += "<span class='cs-head-badge'>" + std::to_string(totalUnread) + " \u672a\u8bfb</span>";
    sb += "</div>";
    sb += "<div class='cs-acts'>"
          "<a class='cs-btn' href='/messages/group/new'>\U0001f465 \u5efa\u7fa4</a>"
          "<a class='cs-btn' href='/messages/search'>\U0001f50d \u627e\u7528\u6237</a></div>";
    sb += "<form class='cs-add' method='POST' action='/messages/contact/add'>"
          "<input type='text' name='username' placeholder='\u6dfb\u52a0\u8054\u7cfb\u4eba' autocomplete='off'/>"
          "<button type='submit' title='\u6dfb\u52a0'>\uff0b</button></form>";
    if (!groupRows.empty()) {
        sb += "<div class='cs-sec'>\u7fa4\u804a \u00b7 " + std::to_string(groupRows.size()) + "</div>";
        for (size_t i = 0; i < groupRows.size(); i++) sb += rowHtml(groupRows[i]);
    }
    if (!convRows.empty()) {
        sb += "<div class='cs-sec'>\u4f1a\u8bdd</div>";
        for (size_t i = 0; i < convRows.size(); i++) sb += rowHtml(convRows[i]);
    }
    {
        ConvRow r;
        r.key = kSystemId;
        r.href = "/messages/system";
        r.av = convAvatar(NULL, "\U0001f4e2");
        r.name = "\u7cfb\u7edf\u901a\u77e5";
        r.preview = "\u516c\u544a\u4e0e\u63d0\u9192";
        r.time = "";
        r.unread = sysUnread;
        r.active = (active == kSystemId);
        sb += "<div class='cs-sec'>\u7cfb\u7edf</div>" + rowHtml(r);
    }
    if (!contactRows.empty()) {
        sb += "<div class='cs-sec'>\u8054\u7cfb\u4eba \u00b7 \u8fd8\u6ca1\u804a\u8fc7</div>";
        for (size_t i = 0; i < contactRows.size(); i++) sb += rowHtml(contactRows[i]);
    }

    std::string title, bubbles;
    bool showInput = false;
    std::string headerBtn, headerPanel;
    if (!activeGroup.empty()) {
        GroupChat* g = ds.findGroupById(activeGroup);
        if (!g) { title = "群聊不存在"; bubbles = "<div class='chat-empty'>群聊不存在</div>"; }
        else {
            title = "\U0001f465 " + common::htmlEscape(g->name);
            int gsize = (int)g->memberIds.size();
            bool isMember = false; for (auto& m : g->memberIds) if (m == user->id) { isMember = true; break; }
            if (!isMember) { bubbles = "<div class='chat-empty'>你不在该群聊</div>"; }
            else {
                bool changed = false;
                std::string gid = "g" + activeGroup;
                for (auto& m : ds.messages) {
                    if (m.toId != gid) continue;
                    bool mine = (m.fromId == user->id);
                    if (!mine) {
                        bool hasRead = false; for (auto& u : m.readBy) if (u == user->id) hasRead = true;
                        if (!hasRead) { m.readBy.push_back(user->id); changed = true; }
                    }
                    User* from = ds.findUserById(m.fromId);
                    std::string fromName = from ? common::htmlEscape(from->username) : "已注销";
                    bubbles += chatBubbleMsg(m, mine, fromName, gsize);
                }
                if (changed) ds.saveMessages();
                if (bubbles.empty()) bubbles = "<div class='chat-empty'>群里还没有消息，打个招呼吧 \U0001f44b</div>";

                // 成员 / 活跃面板
                std::map<std::string, std::string> memLast;
                for (size_t i = 0; i < ds.messages.size(); i++)
                    if (ds.messages[i].toId == gid && !ds.messages[i].fromId.empty()) memLast[ds.messages[i].fromId] = ds.messages[i].sendTime;
                std::string memRows;
                int onlineCount = 0;
                for (size_t i = 0; i < g->memberIds.size(); i++) {
                    const std::string& mid = g->memberIds[i];
                    User* mm = ds.findUserById(mid);
                    bool online = SessionManager::instance().hasActiveSession(mid);
                    if (online) onlineCount++;
                    std::string nm = mm ? common::htmlEscape(mm->username) : "已注销";
                    std::string last = memLast.count(mid) ? shortTimeLabel(memLast[mid]) : "";
                    memRows += "<div class='gm-row'><span class='conv-av gm-av'>" + convAvatar(mm, "") + "</span>"
                               "<span class='gm-name'>" + nm +
                               (mid == g->ownerId ? " <span class='gm-tag'>群主</span>" : "") +
                               (mid == user->id ? " <span class='gm-tag me'>我</span>" : "") + "</span>"
                               "<span class='gm-state'>" + (online ? "<i class='on'></i>在线" : "<i></i>离线") + "</span>"
                               "<span class='gm-last'>" + (last.empty() ? std::string("—") : last + " 发言") + "</span></div>";
                }
                User* ownerU = ds.findUserById(g->ownerId);
                headerBtn = "<button type='button' class='chat-hbtn' id='grpToggle'>\U0001f465 " +
                            std::to_string(gsize) + " 人 · " + std::to_string(onlineCount) + " 在线</button>";
                headerPanel = "<div class='grp-panel' id='grpPanel' hidden>"
                    "<div class='grp-info'>群主 " + (ownerU ? common::htmlEscape(ownerU->username) : "已注销") +
                    " · 创建于 " + common::htmlEscape(g->createTime.empty() ? std::string("—") : g->createTime.substr(0, 10)) +
                    " · 成员 " + std::to_string(gsize) + " 人</div>"
                    "<div class='grp-mem'>" + memRows + "</div>"
                    "<div class='grp-acts'>"
                    "<form method='POST' action='/messages/group/" + activeGroup + "/add' class='grp-add'>"
                    "<input type='text' name='username' placeholder='添加成员（用户名）' autocomplete='off'/>"
                    "<button class='btn btn-sm'>添加</button></form>"
                    "<form method='POST' action='/messages/group/" + activeGroup + "/leave' onsubmit=\"return confirm('确定退出该群聊？')\">"
                    "<button class='btn btn-sm btn-danger'>退出群聊</button></form>"
                    "</div></div>";
                showInput = true;
            }
        }
    } else if (active == kSystemId) {

        title = "\U0001f4e2 \u7cfb\u7edf\u901a\u77e5";
        bool changed = false;
        for (auto& m : ds.messages) {
            if (m.toId != user->id || m.fromId != kSystemId) continue;
            if (!m.read) { m.read = true; changed = true; }
            bubbles += chatBubble(m.sendTime, m.content, false);
        }
        if (changed) ds.saveMessages();
        if (bubbles.empty()) bubbles = "<div class='chat-empty'>\u6682\u65e0\u7cfb\u7edf\u901a\u77e5</div>";
    } else if (!active.empty()) {
        User* p = ds.findUserById(active);
        if (!p) return "<script>window.location.href='/messages'</script>";
        title = common::htmlEscape(p->username);
        {
            bool online = SessionManager::instance().hasActiveSession(p->id);
            headerBtn = "<span class='chat-presence" + std::string(online ? " on" : "") + "'>" +
                        (online ? "\u25cf \u5728\u7ebf" : "\u25cb \u79bb\u7ebf") + "</span>"
                        "<a class='chat-hbtn' href='/user/" + p->id + "'>\u8d44\u6599</a>";
        }
        bool changed = false;
        for (auto& m : ds.messages) {
            bool rel = (m.fromId == user->id && m.toId == active) || (m.fromId == active && m.toId == user->id);
            if (!rel) continue;
            bool mine = (m.fromId == user->id);
            if (!mine && !m.read) { m.read = true; changed = true; }
            bubbles += chatBubbleMsg(m, mine, "");
        }
        if (changed) ds.saveMessages();
        if (bubbles.empty()) bubbles = "<div class='chat-empty'>\u5f00\u59cb\u804a\u5929\u5427</div>";
        showInput = true;
    } else {
        title = "\u6d88\u606f";
        bubbles = "<div class='chat-empty'>\u8bf7\u9009\u62e9\u5de6\u4fa7\u4f1a\u8bdd</div>";
    }

    std::string content;
    content += "<div class='chat-layout'>";
    content += "<div class='chat-sidebar'>" + sb + "</div>";
    content += "<div class='chat-main'>";
    content += "<div class='chat-header'><span class='chat-title'>" + title + "</span>" + headerBtn + "</div>";
    content += headerPanel;
    content += "<div class='chat-body'>" + bubbles + "</div>";
    content += "<div class='chat-typing' id='typingHint'></div>";
    if (showInput) {
        std::string target = activeGroup.empty() ? active : ("g" + activeGroup);
        content += "<form class='chat-input' id='msgForm' method='POST' action='/message/send'>"
                   "<input type='hidden' name='toUser' value='" + common::htmlEscape(target) + "'/>"
                   "<input type='hidden' name='fileKey' id='fk'/>"
                   "<input type='hidden' name='fileName' id='fn'/>"
                   "<input type='hidden' name='fileSize' id='fsz'/>"
                   "<label class='chat-file-btn' title='发送文件 / 图片（≤20MB）'>📎"
                   "<input type='file' id='chatFile' hidden multiple/></label>"
                   "<input type='text' name='content' id='chatText' placeholder='输入消息…（Enter 发送，Shift+Enter 换行）' autocomplete='off'/>"
                   "<button class='btn' id='chatSend'>发送</button>"
                   "</form>";
    } else {
        content += "<div class='chat-input' style='justify-content:center;color:#999;font-size:13px;'>系统通知仅可查看</div>";
    }
    content += "</div></div>";

    content += "<style>"
        ".chat-layout{display:flex;height:72vh;border:1px solid var(--border,#e3e8ee);border-radius:14px;overflow:hidden;background:var(--surface,#fff)}"
        ".chat-sidebar{width:254px;border-right:1px solid #eef1f5;background:var(--surface-2,#f7fafd);overflow-y:auto;overflow-x:hidden;flex-shrink:0}"
        ".cs-head{display:flex;justify-content:space-between;align-items:center;padding:12px 14px 8px;font-weight:700;font-size:14px;color:var(--text-strong,#2c3e50)}"
        ".cs-head-badge{font-size:11.5px;background:#e0553f;color:#fff;border-radius:99px;padding:1px 8px;font-weight:600}"
        ".cs-acts{display:flex;gap:6px;padding:0 12px 8px}"
        ".cs-btn{flex:1;text-align:center;font-size:12.5px;padding:5px 6px;border-radius:8px;background:#fff;border:1px solid #e3e8ee;color:#3a86c8;text-decoration:none;font-weight:600}"
        ".cs-btn:hover{background:#eef4fb}"
        ".cs-add{display:flex;gap:6px;padding:0 12px 10px}"
        ".cs-add input{flex:1;min-width:0;padding:6px 9px;border:1px solid #e3e8ee;border-radius:8px;font-size:12.5px;outline:none;box-sizing:border-box}"
        ".cs-add button{border:none;background:#3a86c8;color:#fff;border-radius:8px;width:30px;cursor:pointer;font-size:15px;line-height:1}"
        ".cs-sec{padding:10px 14px 5px;font-size:11.5px;color:#9aa4b2;font-weight:600;letter-spacing:.4px}"
        ".conv{display:flex;align-items:center;gap:9px;padding:8px 12px;margin:1px 8px;border-radius:10px;color:#2c3e50;text-decoration:none;min-height:48px;box-sizing:border-box}"
        ".conv:hover{background:#eef4fb}"
        ".conv.active{background:#e4effc;color:#2f6fb0}"
        ".conv-av{width:34px;height:34px;border-radius:50%;flex-shrink:0;display:flex;align-items:center;justify-content:center;background:linear-gradient(135deg,#8fbbe6,#5a97d0);color:#fff;font-size:13px;font-weight:700;overflow:hidden}"
        ".conv-av img{width:100%;height:100%;object-fit:cover}"
        ".conv-av-t{font-size:13px;line-height:1}"
        ".conv-main{flex:1;min-width:0;display:flex;flex-direction:column;gap:2px}"
        ".conv-name{font-size:13.5px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
        ".conv-sub{font-size:11px;color:#9aa4b2;font-weight:500}"
        ".conv-last{font-size:11.8px;color:#93a0ae;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
        ".conv-right{display:flex;flex-direction:column;align-items:flex-end;gap:4px;flex-shrink:0;min-width:34px}"
        ".conv-time{font-size:10.8px;color:#a8b3c0;white-space:nowrap}"
        ".conv-badge{background:#e0553f;color:#fff;border-radius:99px;padding:1px 6px;font-size:11px;font-weight:600;line-height:1.5}"
        ".chat-main{flex:1;display:flex;flex-direction:column;min-width:0;position:relative}"
        ".chat-header{display:flex;align-items:center;gap:10px;padding:11px 16px;border-bottom:1px solid #eef1f5;font-weight:600;background:#fff}"
        ".chat-title{font-size:14.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".chat-hbtn{margin-left:auto;border:1px solid #e3e8ee;background:#fff;border-radius:99px;padding:4px 12px;font-size:12px;color:#4a90d9;cursor:pointer;font-weight:600;white-space:nowrap}"
        ".chat-hbtn:hover{background:#eef4fb}"
        ".grp-panel{border-bottom:1px solid #eef1f5;background:#fbfdff;max-height:38vh;overflow:auto}"
        ".grp-info{padding:8px 16px;font-size:12px;color:#8792a0;border-bottom:1px dashed #e8eef5}"
        ".grp-mem{padding:4px 8px}"
        ".gm-row{display:flex;align-items:center;gap:9px;padding:6px 8px;border-radius:8px;font-size:13px}"
        ".gm-row:hover{background:#eef4fb}"
        ".gm-av{width:26px;height:26px;font-size:11px}"
        ".gm-name{flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#2c3e50}"
        ".gm-tag{font-size:10.5px;background:#eef4fb;color:#4a90d9;border-radius:99px;padding:1px 6px;font-weight:600}"
        ".gm-tag.me{background:#e8f6ef;color:#2f7a55}"
        ".gm-state{font-size:11.5px;color:#93a0ae;display:flex;align-items:center;gap:4px}"
        ".gm-state i{width:6px;height:6px;border-radius:50%;background:#c6cfda;display:inline-block}"
        ".gm-state i.on{background:#3ba272}"
        ".gm-last{font-size:11px;color:#a8b3c0;min-width:64px;text-align:right}"
        ".grp-acts{display:flex;gap:8px;align-items:center;padding:8px 12px;border-top:1px dashed #e8eef5;flex-wrap:wrap}"
        ".grp-add{display:flex;gap:6px;flex:1;min-width:180px}"
        ".grp-add input{flex:1;padding:5px 9px;border:1px solid #e3e8ee;border-radius:8px;font-size:12.5px;outline:none}"
        ".chat-typing{font-size:12px;color:#8a93a1;padding:2px 16px 0;line-height:1.6}"
        ".chat-typing:empty{display:none}"
        ".chat-body{flex:1;overflow-y:auto;padding:16px;background:var(--surface-2,#f7f9fb)}"
        ".bubble{max-width:76%;margin-bottom:10px;padding:8px 12px;border-radius:14px;font-size:14px;line-height:1.55;word-break:break-word;box-shadow:0 1px 2px rgba(20,40,70,.05)}"
        ".bubble.mine{margin-left:auto;background:#3a86c8;color:#fff;border-bottom-right-radius:4px}"
        ".bubble.theirs{background:#fff;color:#2c3e50;border:1px solid #eaedf2;border-bottom-left-radius:4px}"
        ".bubble-text{white-space:pre-wrap}"
        ".bubble-from{font-size:11.5px;color:#8a93a1;margin-bottom:3px}"
        ".bubble-time{font-size:11px;color:#b8c0cc;margin-top:5px;display:flex;gap:8px;justify-content:flex-end;align-items:center}"
        ".bubble.theirs .bubble-time{justify-content:flex-start}"
        ".bubble.mine .bubble-time{color:rgba(255,255,255,.8)}"
        ".msg-del{border:none;background:none;color:inherit;font-size:11px;cursor:pointer;opacity:.55;padding:0}"
        ".msg-del:hover{opacity:1;text-decoration:underline}"
        ".msg-file{display:inline-flex;align-items:center;gap:6px;margin-top:6px;padding:6px 10px;border-radius:9px;background:rgba(0,0,0,.06);color:inherit;text-decoration:none;font-size:12.5px;max-width:100%}"
        ".bubble.mine .msg-file{background:rgba(255,255,255,.18);color:#fff}"
        ".msg-file.img{display:block;padding:0;background:none}"
        ".msg-file.img img{max-width:260px;max-height:220px;border-radius:10px;display:block}"
        ".msg-file.img .mf-ic,.msg-file.img .mf-size{display:none}"
        ".mf-name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".mf-size{font-size:11px;opacity:.7}"
        ".bubble a.mention{color:#2f6fb0;font-weight:600;text-decoration:none}"
        ".bubble.mine a.mention{color:#fff;text-decoration:underline}"
        ".chat-input{display:flex;gap:8px;align-items:center;padding:10px 12px;border-top:1px solid #eef1f5;background:#fff}"
        ".chat-input input[type=text]{flex:1;border:1px solid #e0e5ec;border-radius:10px;padding:9px 12px;font-size:14px;outline:none}"
        ".chat-input input[type=text]:focus{border-color:#3a86c8}"
        ".chat-file-btn{cursor:pointer;font-size:17px;line-height:1;padding:4px 6px;border-radius:8px}"
        ".chat-file-btn:hover{background:#eef4fb}"
        ".chat-empty{text-align:center;color:#a0a8b4;padding:40px 0}"
        ".chat-att{display:flex;gap:6px;align-items:center;font-size:12px;color:#2f6fb0;background:#eef4fb;border-radius:99px;padding:3px 9px}"
        "@media(max-width:760px){.chat-sidebar{width:170px}.bubble{max-width:86%}.conv-last,.conv-time{display:none}}"
        "</style>";

    content += R"JS(<style>
.bubble-read{font-size:11px;color:rgba(255,255,255,.75);}
.bubble-read.read{color:#bfe6cf;}
.bubble.theirs .bubble-read{display:none;}
.date-sep{text-align:center;color:#a6aebb;font-size:11.5px;margin:12px 0 8px;}
.date-sep span{background:#e9eef5;border-radius:99px;padding:2px 10px;}
.newmsg-pill{position:absolute;left:50%;transform:translateX(-50%);bottom:70px;background:#3a86c8;color:#fff;
  border:none;border-radius:99px;padding:6px 14px;font-size:12.5px;cursor:pointer;box-shadow:0 6px 18px rgba(20,60,110,.25);z-index:5}
.chat-main{position:relative;}
.att-queue{display:flex;flex-wrap:wrap;gap:6px;padding:0 12px 6px;}
.att-chip{display:flex;align-items:center;gap:6px;font-size:12px;background:#eef4fb;color:#2f6fb0;
  border-radius:99px;padding:3px 10px;}
.att-chip b{cursor:pointer;opacity:.6}
.att-chip b:hover{opacity:1}
.att-chip .pct{color:#7f8c8d}
</style>
<script>
(function(){
  var body=document.querySelector('.chat-body');
  var main=document.querySelector('.chat-main');
  var form=document.getElementById('msgForm');
  var sysUnreadEl=null;
  function convId(){
    if(!form) return document.body.getAttribute('data-conv')||'';
    var el=form.querySelector('[name=toUser]');
    return el?el.value:'';
  }
  function toast(m,kind){
    var box=document.getElementById('msgToast');
    if(!box){ box=document.createElement('div'); box.id='msgToast';
      box.style.cssText='position:fixed;right:18px;bottom:18px;z-index:999;display:flex;flex-direction:column;gap:8px;';
      document.body.appendChild(box); }
    var d=document.createElement('div');
    d.textContent=m;
    d.style.cssText='background:'+(kind==='err'?'#b34638':'#2f3640')+';color:#fff;padding:9px 14px;border-radius:9px;font-size:13px;box-shadow:0 8px 24px rgba(0,0,0,.18)';
    box.appendChild(d);
    setTimeout(function(){ d.style.opacity='0'; d.style.transition='opacity .3s'; setTimeout(function(){ d.remove(); },320); },2600);
  }
  function todayOf(t){ return String(t||'').slice(0,10); }
  function lastDate(){ var ns=body?body.querySelectorAll('.date-sep'):[]; if(!ns.length) return ''; return ns[ns.length-1].getAttribute('data-d'); }
  function addDateIfNeeded(time){
    if(!body) return;
    var d=todayOf(time);
    if(!d||d===lastDate()) return;
    var now=new Date();
    var pad=function(n){return n<10?'0'+n:''+n;};
    var today=now.getFullYear()+'-'+pad(now.getMonth()+1)+'-'+pad(now.getDate());
    var label=d===today?'今天':d;
    var div=document.createElement('div');
    div.className='date-sep'; div.setAttribute('data-d',d);
    div.innerHTML='<span>'+label+'</span>';
    body.appendChild(div);
  }
  function lastId(){
    if(!body) return '';
    var bs=body.querySelectorAll('.bubble[id]');
    if(!bs.length) return '';
    return bs[bs.length-1].id.replace(/^m/,'');
  }
  function atBottom(){
    if(!body) return true;
    return (body.scrollHeight-body.scrollTop-body.clientHeight)<90;
  }
  function showPill(){
    if(!main||document.getElementById('newPill')) return;
    var b=document.createElement('button');
    b.id='newPill'; b.className='newmsg-pill'; b.textContent='↓ 有新消息';
    b.onclick=function(){ body.scrollTop=body.scrollHeight; b.remove(); };
    main.appendChild(b);
  }
  function markReads(list){
    (list||[]).forEach(function(id){
      var el=document.querySelector('.bubble-read[data-id="'+id+'"]');
      if(el){ el.textContent='已读'; el.classList.add('read'); }
    });
  }
  function updateBadges(unread,total){
    document.querySelectorAll('a.conv').forEach(function(a){
      var href=a.getAttribute('href')||'';
      var key='', badge=a.querySelector('.conv-badge');
      if(href.indexOf('/messages/chat/group/')>=0) key='g'+href.split('/').pop();
      else if(href.indexOf('/messages/chat/')>=0) key=href.split('/').pop();
      else if(href.indexOf('/messages/system')>=0) key='system';
      var n=unread&&unread[key]?unread[key]:0;
      if(n>0){
        if(!badge){
          badge=document.createElement('span'); badge.className='conv-badge';
          var right=a.querySelector('.conv-right');
          (right||a).appendChild(badge);
        }
        badge.textContent=n>99?'99+':n;
      } else if(badge) badge.remove();
    });
    if(total>0){ document.title='('+total+') 私信 - OI小站'; }
    else if(document.title.indexOf(')')===0) document.title='私信 - OI小站';
  }
  var firstPoll=true, busy=false;
  async function poll(){
    if(busy) return; busy=true;
    try{
      var c=convId();
      var r=await fetch('/messages/poll?conv='+encodeURIComponent(c)+'&after='+encodeURIComponent(lastId()),{headers:{'X-Requested-With':'fetch'},credentials:'same-origin'});
      var j=await r.json();
      if(j&&j.ok){
        if(j.bubbles && body){
          var tmp=document.createElement('div'); tmp.innerHTML=j.bubbles;
          var wasBottom=atBottom();
          var appended=0;
          Array.prototype.forEach.call(tmp.children,function(node){
            var bid=node.id?node.id.replace(/^m/,'') : '';
            if(bid && document.getElementById('m'+bid)) return;
            addDateIfNeeded(node.querySelector('.bubble-time')?node.querySelector('.bubble-time').textContent.slice(0,10):'');
            body.appendChild(node);
            if(node.classList&&node.classList.contains('bubble')) node.style.opacity='0.01';
            appended++;
          });
          Array.prototype.forEach.call(body.querySelectorAll('.bubble'),function(b){ b.style.opacity=''; });
          if(appended){
            if(wasBottom){ body.scrollTop=body.scrollHeight; }
            else if(!firstPoll){ showPill(); if(j.total>0) toast('收到新消息'); }
            bindDel();
          }
        }
        markReads(j.reads);
        updateBadges(j.unread, parseInt(j.total||'0',10));
        var hint=document.getElementById('typingHint');
        if(hint) hint.textContent = j.typing ? (j.typing+' 正在输入…') : '';
      }
    }catch(e){}
    busy=false; firstPoll=false;
  }

  // 已读标记与撤回
  function bindDel(){
    document.querySelectorAll('.msg-del').forEach(function(b){
      if(b.__bound) return; b.__bound=1;
      b.onclick=async function(){
        if(!confirm('撤回这条消息？')) return;
        var fd=new URLSearchParams(); fd.set('messageId', b.getAttribute('data-id')); fd.set('ajax','1');
        try{ var r=await fetch('/message/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Requested-With':'fetch'},body:fd.toString()});
          var j=await r.json(); if(j.ok){ var n=b.closest('.bubble'); if(n) n.remove(); } else toast(j.error||'删除失败','err');
        }catch(e){ toast('删除失败','err'); }
      };
    });
  }
  bindDel();
  // 首次进入：按顺序重建日期分隔线（避免出现顺序错乱）
  (function rebuildSeps(){
    if(!body) return;
    body.querySelectorAll('.date-sep').forEach(function(n){ n.remove(); });
    var last='';
    Array.prototype.slice.call(body.children).forEach(function(node){
      if(!node.classList||!node.classList.contains('bubble')) return;
      var t=node.querySelector('.bubble-time');
      var d=t?t.textContent.trim().slice(0,10):'';
      if(!d||d===last) return;
      last=d;
      var now=new Date(); var pad=function(n){return n<10?'0'+n:''+n;};
      var today=now.getFullYear()+'-'+pad(now.getMonth()+1)+'-'+pad(now.getDate());
      var div=document.createElement('div');
      div.className='date-sep'; div.setAttribute('data-d',d);
      div.innerHTML='<span>'+(d===today?'今天':d)+'</span>';
      body.insertBefore(div,node);
    });
  })();
  if(body) body.scrollTop=body.scrollHeight;
  setTimeout(function(){ if(body) body.scrollTop=body.scrollHeight; }, 400);  // 图片/附件加载后再落到底部
  poll();
  setInterval(poll,3000);

  if(!form){ return; }
  var text=document.getElementById('chatText');
  var fileIn=document.getElementById('chatFile');
  var sendBtn=document.getElementById('chatSend');
  var queue=[]; // {key,name,size}
  var queueBox=document.createElement('div');
  queueBox.className='att-queue'; queueBox.id='attQueue';
  form.parentNode.insertBefore(queueBox, form);

  function renderQueue(){
    queueBox.innerHTML='';
    queue.forEach(function(a,i){
      var d=document.createElement('span'); d.className='att-chip';
      d.innerHTML='📎 '+a.name.replace(/</g,'&lt;')+(a.uploading?' <span class="pct">'+a.pct+'%</span>':'')+' <b>✕</b>';
      d.querySelector('b').onclick=function(){ queue.splice(i,1); renderQueue(); };
      queueBox.appendChild(d);
    });
  }
  function uploadOne(f){
    return new Promise(function(resolve){
      if(f.size>20*1024*1024){ toast('单个文件不能超过 20MB','err'); return resolve(null); }
      var item={name:f.name,size:f.size,uploading:true,pct:0};
      queue.push(item); renderQueue();
      var fd=new FormData(); fd.append('file',f);
      var xhr=new XMLHttpRequest();
      xhr.open('POST','/messages/upload');
      xhr.setRequestHeader('X-Requested-With','fetch');
      xhr.upload.onprogress=function(e){ if(e.lengthComputable){ item.pct=Math.round(e.loaded/e.total*100); renderQueue(); } };
      xhr.onload=function(){
        item.uploading=false;
        try{ var j=JSON.parse(xhr.responseText);
          if(!j.ok){ toast(j.error||'上传失败','err'); queue=queue.filter(function(x){return x!==item;}); }
          else { item.key=j.key; item.name=j.name; item.size=j.size; }
        }catch(e){ toast('上传失败','err'); queue=queue.filter(function(x){return x!==item;}); }
        renderQueue(); resolve(item.key?item:null);
      };
      xhr.onerror=function(){ toast('上传失败','err'); queue=queue.filter(function(x){return x!==item;}); renderQueue(); resolve(null); };
      xhr.send(fd);
    });
  }
  fileIn.onchange=async function(){
    var fs=Array.prototype.slice.call(fileIn.files||[]);
    fileIn.value='';
    for(var i=0;i<fs.length;i++) await uploadOne(fs[i]);
  };

  async function sendPayload(data){
    var r=await fetch('/message/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Requested-With':'fetch'},body:data.toString()});
    return await r.json();
  }
  form.onsubmit=async function(e){
    e.preventDefault();
    var v=text.value.trim();
    if(!v && !queue.length) return;
    if(sendBtn.disabled) return;
    sendBtn.disabled=true;
    var c=convId();
    var okAll=true;
    try{
      var atts=queue.filter(function(a){return a.key;});
      // 兼容旧式隐藏域写法（fileKey/fileName/fileSize）
      if(!atts.length){
        var hk=document.getElementById('fk'); if(hk && hk.value) atts=[{key:hk.value,name:document.getElementById('fn').value,size:document.getElementById('fsz').value}];
      }
      var first=atts[0];
      if(v || first){
        var d1=new URLSearchParams(); d1.set('toUser',c); d1.set('content',v);
        if(first){ d1.set('fileKey',first.key); d1.set('fileName',first.name); d1.set('fileSize',first.size); }
        var j1=await sendPayload(d1);
        if(j1.ok){ body.insertAdjacentHTML('beforeend', j1.bubble); }
        else { okAll=false; toast(j1.error||'发送失败','err'); }
        if(okAll && first) atts=atts.slice(1);
      }
      if(okAll){
        for(var i=0;i<atts.length;i++){
          var a=atts[i];
          var d2=new URLSearchParams(); d2.set('toUser',c); d2.set('content','');
          d2.set('fileKey',a.key); d2.set('fileName',a.name); d2.set('fileSize',a.size);
          var j2=await sendPayload(d2);
          if(j2.ok){ body.insertAdjacentHTML('beforeend', j2.bubble); }
          else { okAll=false; toast(j2.error||'附件发送失败','err'); }
        }
      }
    }catch(err){ okAll=false; toast('发送失败，请重试','err'); }
    if(okAll){
      text.value=''; queue=[]; renderQueue();
      var hk2=document.getElementById('fk'); if(hk2){ hk2.value=''; document.getElementById('fn').value=''; document.getElementById('fsz').value=''; }
    }
    body.scrollTop=body.scrollHeight;
    bindDel();
    sendBtn.disabled=false; text.focus();
    poll();
  };
  text.addEventListener('keydown', function(e){
    if(e.key==='Enter' && !e.shiftKey && !e.isComposing){ e.preventDefault(); form.requestSubmit?form.requestSubmit():form.onsubmit(e); }
  });
  // 拖入文件
  ['dragover','drop'].forEach(function(ev){
    body.addEventListener(ev, function(e){ e.preventDefault();
      if(ev==='drop' && e.dataTransfer.files) Array.prototype.slice.call(e.dataTransfer.files).forEach(function(f){ uploadOne(f); });
    });
  });
  // 粘贴图片
  text.addEventListener('paste', function(e){
    var items=(e.clipboardData||{}).items||[];
    for(var i=0;i<items.length;i++){
      if(items[i].type.indexOf('image')===0){
        var f=items[i].getAsFile();
        if(f) uploadOne(new File([f],'截图_'+Date.now()+'.png',{type:f.type}));
      }
    }
  });
  // 群成员面板
  var gt=document.getElementById('grpToggle'), gp=document.getElementById('grpPanel');
  if(gt&&gp){ gt.onclick=function(){ gp.hidden=!gp.hidden; gt.classList.toggle('on', !gp.hidden); }; }
  // 正在输入上报（2.5 秒节流）
  var lastTyping=0;
  text.addEventListener('input', function(){
    var now=Date.now();
    if(!text.value || now-lastTyping<2500) return;
    lastTyping=now;
    var d=new URLSearchParams(); d.set('conv', convId());
    fetch('/messages/typing',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Requested-With':'fetch'},body:d.toString(),credentials:'same-origin'}).catch(function(){});
  });
  text.focus();
})();
</script>)JS";
    return renderTemplate("私信", content, user);
}

std::string Handlers::handleMessagesTyping(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId) {
    (void)method; (void)path; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false}";
    std::string conv = parseFormData(body, "conv");
    if (conv.empty() || conv.size() > 90) return "{\"ok\":false}";
    std::lock_guard<std::mutex> lk(g_typingMutex);
    time_t now = time(0);
    std::map<std::string, time_t>& m = g_typing[conv];
    m[user->id] = now;
    for (std::map<std::string, time_t>::iterator it = m.begin(); it != m.end();) {
        if (now - it->second > 30) it = m.erase(it); else ++it;
    }
    return "{\"ok\":true}";
}

std::string Handlers::handleMessagesPoll(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    (void)method; (void)body; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false,\"error\":\"login\"}";
    if (!loadSystemConfig().allowMessages) return "{\"ok\":false,\"error\":\"closed\"}";
    DataStore& ds = DataStore::instance();
    std::string conv = drvQueryValue(path, "conv");
    std::string after = drvQueryValue(path, "after");

    bool isGroup = (conv.size() >= 2 && conv[0] == 'g');
    int groupSize = 0;
    if (isGroup) {
        GroupChat* gp = ds.findGroupById(conv.substr(1));
        if (gp) groupSize = (int)gp->memberIds.size();
    }
    bool changed = false;
    std::string bubbles, readsJson = "[";
    bool firstRead = true;
    size_t start = 0;
    if (!after.empty()) {
        bool found = false;
        for (size_t i = 0; i < ds.messages.size(); i++)
            if (ds.messages[i].id == after) { start = i + 1; found = true; break; }
        if (!found) start = 0;
    }
    for (size_t i = start; i < ds.messages.size(); i++) {
        Message& m = ds.messages[i];
        bool rel = false;
        if (conv == kSystemId) rel = (m.toId == user->id && m.fromId == kSystemId);
        else if (isGroup) rel = (m.toId == conv);
        else if (!conv.empty()) rel = ((m.fromId == user->id && m.toId == conv) || (m.fromId == conv && m.toId == user->id));
        if (!rel) continue;
        bool mine = (m.fromId == user->id);
        if (!mine) {
            if (isGroup) {
                bool hasRead = false;
                for (size_t k = 0; k < m.readBy.size(); k++) if (m.readBy[k] == user->id) hasRead = true;
                if (!hasRead) { m.readBy.push_back(user->id); changed = true; }
            } else if (!m.read) { m.read = true; changed = true; }
        }
        std::string fromName;
        if (isGroup && !mine) {
            User* fu = ds.findUserById(m.fromId);
            fromName = fu ? common::htmlEscape(fu->username) : "已注销";
        }
        bubbles += chatBubbleMsg(m, mine, fromName, isGroup ? groupSize : -1);
    }
    // 我发出的消息是否已被对方读过（用于「已读」标记）
    if (!isGroup && !conv.empty() && conv != kSystemId) {
        for (size_t i = 0; i < ds.messages.size(); i++) {
            Message& m = ds.messages[i];
            if (m.fromId == user->id && m.toId == conv && m.read) {
                if (!firstRead) readsJson += ",";
                firstRead = false;
                readsJson += "\"" + common::jsonEscape(m.id) + "\"";
            }
        }
    }
    readsJson += "]";
    if (changed) ds.saveMessages();

    // 未读统计（侧栏）
    std::map<std::string, int> unread;
    std::map<std::string, std::string> gname;
    for (size_t i = 0; i < ds.groups.size(); i++) {
        GroupChat& g = ds.groups[i];
        for (size_t k = 0; k < g.memberIds.size(); k++)
            if (g.memberIds[k] == user->id) { gname["g" + g.id] = g.name; break; }
    }
    int total = 0;
    for (size_t i = 0; i < ds.messages.size(); i++) {
        Message& m = ds.messages[i];
        if (m.toId == user->id && m.fromId != user->id) {
            if (m.fromId == kSystemId) { if (!m.read) { unread[kSystemId]++; total++; } }
            else if (!ds.findUserById(m.fromId)) {
                // 发送者已注销：自动置读，避免统计到「找不到来源」的未读
                if (!m.read) { m.read = true; changed = true; }
            }
            else if (!m.read) { unread[m.fromId]++; total++; }
        } else if (m.toId.size() >= 2 && m.toId[0] == 'g' && gname.count(m.toId) && m.fromId != user->id) {
            bool hasRead = false;
            for (size_t k = 0; k < m.readBy.size(); k++) if (m.readBy[k] == user->id) hasRead = true;
            if (!hasRead) { unread[m.toId]++; total++; }
        }
    }
    std::string unreadJson = "{";
    bool firstU = true;
    for (std::map<std::string, int>::iterator it = unread.begin(); it != unread.end(); ++it) {
        if (!firstU) unreadJson += ",";
        firstU = false;
        unreadJson += "\"" + common::jsonEscape(it->first) + "\":" + std::to_string(it->second);
    }
    unreadJson += "}";

    std::string lastId;
    for (size_t i = ds.messages.size(); i > 0; i--) {
        Message& m = ds.messages[i - 1];
        bool rel = false;
        if (conv == kSystemId) rel = (m.toId == user->id && m.fromId == kSystemId);
        else if (isGroup) rel = (m.toId == conv);
        else if (!conv.empty()) rel = ((m.fromId == user->id && m.toId == conv) || (m.fromId == conv && m.toId == user->id));
        if (rel) { lastId = m.id; break; }
    }

    std::map<std::string, std::string> o;
    o["ok"] = "true";
    o["bubbles"] = bubbles;
    o["lastId"] = lastId;
    o["total"] = std::to_string(total);
    o["typing"] = typingNamesFor(conv, user->id, ds);
    o["unread"] = unreadJson;
    // reads 手动拼接（jsonStringify(map) 会转义，这里直接放已构造好的数组）
    std::string out = common::jsonStringify(o);
    if (!out.empty() && out[out.size() - 1] == '}') out.erase(out.size() - 1);
    out += ",\"reads\":" + readsJson + "}";
    return out;
}

std::string Handlers::handleSendMessage(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    (void)method; (void)path;
    bool ajax = false;
    if (headers.count("x-requested-with") && headers.at("x-requested-with") == "fetch") ajax = true;
    if (!loadSystemConfig().allowMessages) {
        std::string c = "<div class='alert alert-info'>私信已关闭</div><a href='/' class='btn'>返回首页</a>";
        if (ajax) return "{\"ok\":false,\"error\":\"私信已关闭\"}";
        return renderTemplate("私信已关闭", c, getCurrentUser(sessionId));
    }
    User* user = getCurrentUser(sessionId);
    if (!user) {
        if (ajax) return "{\"ok\":false,\"error\":\"请先登录\"}";
        return "<script>window.location.href='/login'</script>";
    }
    DataStore& ds = DataStore::instance();
    std::string toUser = parseFormData(body, "toUser");
    std::string content = common::trim(parseFormData(body, "content"));
    std::string fileKey = common::trim(parseFormData(body, "fileKey"));
    std::string fileName = parseFormData(body, "fileName");
    long long fileSize = 0;
    { std::string fs0 = parseFormData(body, "fileSize"); if (!fs0.empty()) { try { fileSize = std::stoll(fs0); } catch (...) {} } }
    if (toUser.empty() || (content.empty() && fileKey.empty())) {
        if (ajax) return "{\"ok\":false,\"error\":\"消息为空\"}";
        return "<script>window.location.href='/messages'</script>";
    }
    int _ml = security::getSecConfig().maxMessage;
    if ((int)content.length() > _ml) content = content.substr(0, _ml);

    // 频率限制（替代原来每条消息都要验证码）
    if (security::throttleAction("msg:" + user->id, 1)) {
        if (ajax) return "{\"ok\":false,\"error\":\"发送太快了，歇一秒再发\"}";
        return "<script>window.location.href='/messages'</script>";
    }

    // 附件 key 只允许安全字符
    if (!fileKey.empty()) {
        bool okKey = fileKey.size() < 200;
        for (size_t i = 0; okKey && i < fileKey.size(); i++) {
            char c = fileKey[i];
            if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) okKey = false;
        }
        if (!okKey) fileKey.clear();
    }

    std::string convId;
    Message msg;
    msg.id = common::genId();
    msg.fromId = user->id;
    msg.content = content;
    msg.sendTime = common::getTimeStr();
    msg.read = false;
    if (!fileKey.empty()) {
        msg.file = "msg/" + fileKey;
        msg.fileName = fileName.empty() ? fileKey : fileName;
        msg.fileSize = fileSize;
    }

    if (toUser.size() >= 2 && toUser[0] == 'g') {
        std::string gid = toUser.substr(1);
        GroupChat* g = ds.findGroupById(gid);
        if (!g) {
            if (ajax) return "{\"ok\":false,\"error\":\"群聊不存在\"}";
            return "<script>window.location.href='/messages'</script>";
        }
        bool isMember = false; for (auto& mm : g->memberIds) if (mm == user->id) { isMember = true; break; }
        if (!isMember) {
            if (ajax) return "{\"ok\":false,\"error\":\"你不在该群聊\"}";
            return "<script>window.location.href='/messages'</script>";
        }
        msg.toId = "g" + gid;
        msg.readBy.push_back(user->id);
        ds.messages.push_back(msg);
        ds.saveMessages();
        convId = "/messages/chat/group/" + gid;
    } else {
        User* toObj = ds.findUserByName(toUser);
        if (!toObj) toObj = ds.findUserById(toUser);
        if (!toObj) {
            if (ajax) return "{\"ok\":false,\"error\":\"用户不存在\"}";
            return "<script>alert('用户不存在');window.location.href='/messages'</script>";
        }
        msg.toId = toObj->id;
        if (toObj->id == user->id) msg.read = true;  // 备忘消息不产生「永远未读」
        ds.messages.push_back(msg);
        ds.saveMessages();
        convId = "/messages/chat/" + toObj->id;
    }

    if (ajax) {
        std::string bubble = chatBubbleMsg(msg, true, "");
        std::map<std::string, std::string> o;
        o["ok"] = "true";
        o["id"] = msg.id;
        o["time"] = msg.sendTime;
        o["bubble"] = bubble;
        return common::jsonStringify(o);
    }
    return "<script>window.location.href='" + convId + "'</script>";
}

std::string Handlers::handleDeleteMessage(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    (void)method; (void)headers; (void)respHeaders;
    bool ajax = (body.find("ajax=1") != std::string::npos);
    User* user = getCurrentUser(sessionId);
    if (!user) return ajax ? "{\"ok\":false,\"error\":\"请先登录\"}" : "<script>window.location.href='/login'</script>";
    std::string msgId = parseFormData(body, "messageId");
    std::string back = parseFormData(body, "back");
    if (msgId.empty()) {
        size_t lastSlash = path.find_last_of('/');
        msgId = path.substr(lastSlash + 1);
    }
    auto& messages = DataStore::instance().messages;
    bool removed = false;
    for (auto it = messages.begin(); it != messages.end(); ++it) {
        // 允许发送者撤回、接收者删除
        if (it->id == msgId && (it->toId == user->id || it->fromId == user->id)) {
            messages.erase(it);
            DataStore::instance().saveMessages();
            removed = true;
            break;
        }
    }
    if (ajax) return removed ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"消息不存在\"}";
    if (back.empty()) back = "/messages";
    return "<script>window.location.href='" + back + "'</script>";
}

std::string Handlers::handleGroupNew(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    if (method == "GET") {
        std::string c = "<form method='POST'>"
            "<div class='form-group'><label>\u7fa4\u804a\u540d\u79f0</label><input name='name' required/></div>"
            "<div class='form-group'><label>\u6210\u5458\u7528\u6237\u540d\uff08\u9017\u53f7\u5206\u9694\uff0c\u53ef\u9009\uff09</label><input name='members'/></div>"
            + security::captchaFieldsFor("group") +
            "<input type='submit' value='\u521b\u5efa' class='btn'/> <a href='/messages' class='btn'>\u53d6\u6d88</a></form>";
        return renderTemplate("\u521b\u5efa\u7fa4\u804a", c, user);
    }
    std::string name = parseFormData(body, "name");
    std::string members = parseFormData(body, "members");
    if (name.empty()) { std::string c = "<div class='alert alert-error'>\u7fa4\u804a\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a</div><a href='/messages/group/new' class='btn'>\u8fd4\u56de</a>"; return renderTemplate("\u521b\u5efa\u5931\u8d25", c, user); }
    int _gl = security::getSecConfig().maxGroupName;
    if ((int)name.length() > _gl) name = name.substr(0, _gl);
    if (!security::verifyCaptchaFor(parseFormData(body, "captcha_token"), parseFormData(body, "captcha_answer"), "group")) {
        std::string c = "<div class='alert alert-error'>\u9a8c\u8bc1\u7801\u9519\u8bef\u6216\u5df2\u8fc7\u671f</div><a href='/messages/group/new' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u521b\u5efa\u7fa4\u804a\u5931\u8d25", c, user);
    }
    DataStore& ds = DataStore::instance();
    GroupChat g;
    g.id = common::genId();
    g.name = name;
    g.ownerId = user->id;
    g.createTime = common::getTimeStr();
    g.memberIds.push_back(user->id);
    auto parts = common::split(members, ',');
    for (auto& p : parts) {
        std::string nm = common::trim(p);
        if (nm.empty()) continue;
        User* u = ds.findUserByName(nm);
        if (!u) u = ds.findUserById(nm);
        if (!u || u->id == user->id) continue;
        bool dup = false; for (auto& m : g.memberIds) if (m == u->id) dup = true;
        if (!dup) g.memberIds.push_back(u->id);
    }
    ds.groups.push_back(g);
    ds.saveGroups();
    return "<script>window.location.href='/messages/chat/group/" + g.id + "'</script>";
}

std::string Handlers::handleGroupAction(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    size_t p1 = path.find("/messages/group/");
    std::string rest = path.substr(p1 + std::string("/messages/group/").size());
    size_t sl = rest.find('/');
    std::string gid = rest.substr(0, sl);
    std::string action = (sl == std::string::npos) ? "" : rest.substr(sl + 1);
    DataStore& ds = DataStore::instance();
    GroupChat* g = ds.findGroupById(gid);
    if (!g) return "<script>window.location.href='/messages'</script>";
    bool isMember = false; for (auto& m : g->memberIds) if (m == user->id) { isMember = true; break; }
    if (action == "leave") {
        std::vector<std::string> nv;
        for (auto& m : g->memberIds) if (m != user->id) nv.push_back(m);
        g->memberIds = nv; ds.saveGroups();
        return "<script>window.location.href='/messages'</script>";
    }
    if (action == "add") {
        if (!isMember) return "<script>window.location.href='/messages/chat/group/" + gid + "'</script>";
        std::string nm = parseFormData(body, "username");
        User* u = ds.findUserByName(nm);
        if (!u) u = ds.findUserById(nm);
        if (u) {
            bool dup = false; for (auto& m : g->memberIds) if (m == u->id) dup = true;
            if (!dup) g->memberIds.push_back(u->id);
        }
        ds.saveGroups();
        return "<script>window.location.href='/messages/chat/group/" + gid + "'</script>";
    }
    return "<script>window.location.href='/messages/chat/group/" + gid + "'</script>";
}

std::string Handlers::handleRename(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    if (method == "GET") {
        std::string content = "<div class='card' style='max-width:480px;margin:0 auto;padding:22px;'>"
            "<h3>\U0001f451 \u4f7f\u7528\u6539\u540d\u5361</h3>"
            "<div style='margin:10px 0;color:#7f8c8d;'>\u5269\u4f59\u6539\u540d\u5361\uff1a<b>" + std::to_string(user->renameCards) + " \u5f20</b></div>"
            "<form method='POST'>"
            "<div class='form-group'><label>\u65b0\u7528\u6237\u540d</label><input name='newUsername' required/></div>"
            "<input type='submit' value='\u786e\u8ba4\u6539\u540d' class='btn' " + (user->renameCards <= 0 ? "disabled" : "") + "/> <a href='/user/" + user->id + "' class='btn'>\u53d6\u6d88</a>"
            "</form></div>";
        return renderTemplate("\u6539\u540d", content, user);
    }
    std::string newName = common::trim(parseFormData(body, "newUsername"));
    if (newName.empty() || newName.length() < 3) {
        std::string c = "<div class='alert alert-error'>\u7528\u6237\u540d\u81f3\u5c113\u4e2a\u5b57\u7b26</div><a href='/user/rename' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u6539\u540d\u5931\u8d25", c, user);
    }
    if (newName == user->username) {
        std::string c = "<div class='alert alert-error'>\u65b0\u7528\u6237\u540d\u4e0e\u5f53\u524d\u76f8\u540c</div><a href='/user/rename' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u6539\u540d\u5931\u8d25", c, user);
    }
    if (DataStore::instance().findUserByName(newName)) {
        std::string c = "<div class='alert alert-error'>\u8be5\u7528\u6237\u540d\u5df2\u88ab\u5360\u7528</div><a href='/user/rename' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u6539\u540d\u5931\u8d25", c, user);
    }
    if (user->renameCards <= 0) {
        std::string c = "<div class='alert alert-error'>\u4f60\u6ca1\u6709\u6539\u540d\u5361</div><a href='/shop' class='btn'>\u53bb\u5546\u57ce</a>";
        return renderTemplate("\u6539\u540d\u5931\u8d25", c, user);
    }
    user->renameCards--;
    std::string oldName = user->username;
    user->username = newName;
    DataStore::instance().saveUsers();
    std::string c = "<div class='alert alert-success'>\u6539\u540d\u6210\u529f\uff1a" + common::htmlEscape(oldName) + " \u2192 " + common::htmlEscape(newName) + "</div><a href='/user/" + user->id + "' class='btn'>\u67e5\u770b\u4e3b\u9875</a>";
    return renderTemplate("\u6539\u540d\u6210\u529f", c, user);
}

std::string Handlers::handleUserProfile(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* currentUser = getCurrentUser(sessionId);

    // 解析查询参数（个人主页分页）与路径
    std::string path2 = path;
    size_t qpos = path2.find('?');
    std::string query;
    if (qpos != std::string::npos) { query = path2.substr(qpos + 1); path2 = path2.substr(0, qpos); }
    int page = 1;
    size_t pq = query.find("page=");
    if (pq != std::string::npos) page = common::stringToInt(query.substr(pq + 5));
    if (page < 1) page = 1;

    size_t lastSlash = path2.find_last_of('/');
    std::string userId = (lastSlash != std::string::npos) ? path2.substr(lastSlash + 1) : "";

    if (userId.empty() || userId == "user") {
        if (currentUser) return "<script>window.location.href='/user/" + currentUser->id + "'</script>";
        else return "<script>window.location.href='/login'</script>";
    }

    User* user = DataStore::instance().findUserById(userId);
    if (!user) user = DataStore::instance().findUserByName(userId);
    if (!user) {
        std::string content = "<div class='alert alert-error'>用户不存在</div><a href='/' class='btn'>返回首页</a>";
        return renderTemplate("用户不存在", content, currentUser);
    }

    int acCount = DataStore::instance().getUserACCount(user->id);
    int submitCount = DataStore::instance().getUserSubmitCount(user->id);

    std::string avatarHtml;
    if (!user->avatar.empty() && common::isBase64Image(user->avatar)) {
        avatarHtml = "<img src='" + user->avatar + "' class='profile-avatar-img'/>";
    } else {
        avatarHtml = "<div class='profile-avatar'>" + std::string(1, toupper(user->username[0])) + "</div>";
    }

    std::string badge = Handlers::getRatingBadge(user->rating);
    std::string roleStr;
    if (user->role == ROLE_ADMIN) roleStr = "<span class='role-badge admin'>\u2b50 \u7ba1\u7406\u5458</span>";
    else if (user->role == ROLE_PROBLEM_MANAGER) roleStr = "<span class='role-badge pm'>\U0001f4da \u9898\u76ee\u7ba1\u7406\u5458</span>";
    else roleStr = "<span class='role-badge user'>\u7528\u6237</span>";

    std::string content = "";
    // 顶部：头像 + 信息
    content += "<div class='profile-hero'>" + avatarHtml;
    content += "<div class='profile-info'>";
    content += "<h2 class='profile-name'>" + common::htmlEscape(user->username) + " <span class='badge rating'>" + badge + "</span></h2>";
    content += "<div class='profile-role'>" + roleStr + "</div>";
    content += "<div class='profile-meta'>\u6ce8\u518c\u4e8e " + user->registerTime;
    content += " \u00b7 \u6ce8\u518c\u4e8e " + user->registerTime + "</div>";
    if (!user->bio.empty()) {
        content += "<div class='profile-bio'>" + common::htmlEscape(user->bio) + "</div>";
    }
    if (currentUser && currentUser->id == user->id) {
        content += "<div class='profile-actions'>"
                  "<a href='/user/edit' class='btn btn-sm'>\u270f\ufe0f \u7f16\u8f91\u8d44\u6599</a>"
                  "<a href='/user/settings' class='btn btn-sm'>\u2699\ufe0f \u8bbe\u7f6e</a>"
                  "<a href='/myproblem' class='btn btn-sm'>&#128203; \u6211\u7684\u9898\u76ee</a>"
                  "</div>";
    }
    content += "</div></div>";

    // 统计卡片
    content += "<div class='stat-grid'>";
    content += "<div class='stat-card'><span class='stat-num'>" + std::to_string(acCount) + "</span><span class='stat-lbl'>\u2705 AC \u6570\u91cf</span></div>";
    content += "<div class='stat-card'><span class='stat-num'>" + std::to_string(submitCount) + "</span><span class='stat-lbl'>\U0001f4c8 \u63d0\u4ea4\u603b\u6570</span></div>";
    content += "<div class='stat-card'><span class='stat-num'>" + std::to_string(user->rating) + "</span><span class='stat-lbl'>\U0001f3c6 Rating</span></div>";
    content += "<div class='stat-card'><span class='stat-num'>" + std::to_string(user->maxRating) + "</span><span class='stat-lbl'>\U0001f947 \u6700\u9ad8 Rating</span></div>";
    content += "<div class='stat-card'><span class='stat-num'>" + std::to_string(user->checkinDays) + "</span><span class='stat-lbl'>\U0001f4c5 \u8fde\u7eed\u6253\u5361</span></div>";
    content += "<div class='stat-card'><span class='stat-num'>" + std::to_string(user->gold) + "</span><span class='stat-lbl'>\U0001fa99 \u91d1\u5e01</span></div>";
    content += "</div>";

    // Rating 变化曲线（由历史绘制 SVG）
    if (user->ratingHistory.size() >= 1) {
        std::vector<int> rh;
        for (const auto& e : user->ratingHistory) {
            std::vector<std::string> parts = common::split(e, '|');
            if (parts.size() >= 3) rh.push_back(common::stringToInt(parts[2]));
        }
        if (!rh.empty()) {
            int minV = *std::min_element(rh.begin(), rh.end()), maxV = *std::max_element(rh.begin(), rh.end());
            if (maxV - minV < 100) { maxV += 50; minV -= 50; }
            int W = 940, H = 220, pad = 26;
            auto px = [&](int idx){ return pad + (int)((double)idx * (W - 2*pad) / (double)std::max(1,(int)rh.size()-1)); };
            auto py = [&](int v){ return (H - pad) - (int)((double)(v - minV) * (H - 2*pad - 10) / (double)std::max(1,(maxV-minV))); };
            std::string pts, area;
            for (size_t k = 0; k < rh.size(); k++) {
                int x = px((int)k), y = py(rh[k]);
                if (k) { pts += " "; area += " "; }
                pts += std::to_string(x) + "," + std::to_string(y);
                area += std::to_string(x) + "," + std::to_string(y);
            }
            std::string last = std::to_string(rh.back());
            content += "<div class='card' style='margin-top:18px;'><div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
                "<h3 style='margin:0;'>\U0001f4c8 Rating \u53d8\u5316\u66f2\u7ebf</h3>"
                "<span style='color:var(--muted);font-size:12.5px;'>\u5f53\u524d " + last + " \u00b7 \u5386\u53f2 " + std::to_string((int)rh.size()) + " \u6b21\u8bb0\u5f55</span></div>"
                "<svg viewBox='0 0 " + std::to_string(W) + " " + std::to_string(H) + "' style='width:100%;height:auto;margin-top:10px;'>"
                "<polygon points='" + area + " " + std::to_string(W-pad) + "," + std::to_string(H-pad) + " " + std::to_string(pad) + "," + std::to_string(H-pad) + "' fill='var(--brand-soft)'/>"
                "<polyline points='" + pts + "' fill='none' stroke='var(--brand)' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'/>";
            for (size_t k = 0; k < rh.size(); k++)
                if (k == rh.size()-1 || rh.size() <= 30)
                    content += "<circle cx='" + std::to_string(px((int)k)) + "' cy='" + std::to_string(py(rh[k])) + "' r='3' fill='var(--brand)'/>";
            content += "</svg></div>";
        }
    }

    // 我的卡片（道具）
    if (currentUser && currentUser->id == user->id) {
        content += "<h3 style='margin-top:24px;'>\U0001f451 \u6211\u7684\u5361\u7247</h3>";
        content += "<div class='item-card-row'>";
        content += "<div class='item-card'><span class='item-emoji'>\U0001f451</span><span>\u6539\u540d\u5361 x" + std::to_string(currentUser->renameCards) + "</span> <a href='/user/rename' class='btn btn-sm btn-outline'>\u4f7f\u7528</a></div>";
        if (!currentUser->driveBonusUntil.empty() && !common::isTimeExpired(currentUser->driveBonusUntil))
            content += "<div class='item-card'><span class='item-emoji'>\U0001f4be</span><span>\u4e91\u76d8\u589e\u5bb9 +" + std::to_string(currentUser->driveBonusMB) + "MB\uff08\u81f3 " + common::htmlEscape(currentUser->driveBonusUntil) + "\uff09</span></div>";
        if (!currentUser->coeffUntil.empty() && !common::isTimeExpired(currentUser->coeffUntil))
            content += "<div class='item-card'><span class='item-emoji'>\U0001f4c8</span><span>\u6536\u76ca\u7cfb\u6570 +" + std::to_string(currentUser->coeffBonusPct) + "%\uff08\u81f3 " + common::htmlEscape(currentUser->coeffUntil) + "\uff09</span></div>";
        content += "</div>";
    }

    // TA 的内容（博客 + 题解）
    {
        std::vector<Post*> uPosts;
        for (auto& pp : DataStore::instance().posts) {
            if (pp.authorId != user->id) continue;
            bool isBlog = (pp.type == BLOG);
            bool isSol = (pp.type == SOLUTION);
            if (!isBlog && !isSol) continue;
            if (isSol && pp.auditStatus != "approved") {
                bool ownerView = (currentUser && (currentUser->id == user->id || isAdmin(currentUser)));
                if (!ownerView) continue;
            }
            uPosts.push_back(&pp);
        }
        // 按时间倒序（新在前）
        std::sort(uPosts.begin(), uPosts.end(), [](Post* a, Post* b){ return a->createTime > b->createTime; });
        std::string who = (currentUser && currentUser->id == user->id) ? "我的" : "TA 的";
        bool isSelf = (currentUser && currentUser->id == user->id);
        content += "<h3 style='margin-top:26px;'>\U0001f4d6 " + who + "内容（博客/题解）</h3>";
        if (uPosts.empty()) {
            content += "<div class='empty-state' style='padding:14px;'>\U0001f4d6 暂无博客或题解</div>";
        } else {
            int _cnt = std::min((int)uPosts.size(), 8);
            for (int _k = 0; _k < _cnt; ++_k) {
                Post* pp = uPosts[_k];
                std::string tag;
                if (pp->type == SOLUTION) {
                    tag = "<span class='pill' style='background:#eaf5ff;color:#2980b9;'>题解</span>";
                    if (pp->auditStatus == "rejected") tag += " <span class='pill' style='background:#fdecea;color:#e74c3c;'>已驳回</span>";
                    else if (pp->auditStatus != "approved") tag += " <span class='pill' style='background:#fef9e7;color:#f39c12;'>待审核</span>";
                } else {
                    tag = "<span class='pill' style='background:#f0f3f7;color:#555;'>博客</span>";
                }
                std::string ttl = pp->title;
                if (pp->type == SOLUTION) {
                    Problem* _pp = DataStore::instance().findProblemById(pp->problemId);
                    ttl += (_pp ? "（" + _pp->title + "）" : "");
                }
                content += "<div class='card' style='padding:10px 14px;margin-bottom:8px;'>"
                           "<div>" + tag + " <a href='/post/" + pp->id + "'>" + common::htmlEscape(ttl) + "</a></div>"
                           "<div class='card-meta' style='margin-top:3px;'>" + pp->createTime +
                           " | \U0001f4ad " + std::to_string(pp->comments.size()) +
                           " | \u2764\ufe0f " + std::to_string(pp->likes) + "</div></div>";
            }
            if ((int)uPosts.size() > _cnt && isSelf)
                content += "<a class='btn btn-sm' href='/blog?mine=1'>\u67e5\u770b\u66f4\u591a</a>";
        }
    }

    // 提交记录（分页）
    content += "<h3 style='margin-top:26px;'>\U0001f4ca \u63d0\u4ea4\u8bb0\u5f55</h3>";
    int perPage = 15;
    std::vector<const Submission*> mySubs;
    for (auto it = DataStore::instance().submissions.rbegin(); it != DataStore::instance().submissions.rend(); ++it) {
        if (it->userId == user->id) mySubs.push_back(&(*it));
    }
    int total = (int)mySubs.size();
    int totalPages = (total + perPage - 1) / perPage;
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    int st = (page - 1) * perPage;
    int en = std::min(total, st + perPage);

    content += "<table><thead><tr><th>\u9898\u76ee</th><th>\u72b6\u6001</th><th>\u8bed\u8a00</th><th>\u65f6\u95f4</th></tr></thead><tbody>";
    if (total == 0) {
        content += "<tr><td colspan='4' class='empty-td'>\u6682\u65e0\u63d0\u4ea4\u8bb0\u5f55</td></tr>";
    } else {
        for (int i = st; i < en; ++i) {
            const Submission* it = mySubs[i];
            Problem* p = DataStore::instance().findProblemById(it->problemId);
            std::string statusClass;
            switch(it->status) {
                case AC: statusClass = "status-ac"; break;
                case WA: statusClass = "status-wa"; break;
                case TLE: statusClass = "status-tle"; break;
                case CE: statusClass = "status-ce"; break;
                case RE: statusClass = "status-re"; break;
                default: statusClass = "status-pending";
            }
            content += "<tr><td><a href='/problem/" + it->problemId + "'>" +
                      (p ? common::htmlEscape(p->title) : "\u5df2\u5220\u9664") + "</a></td>"
                      "<td><a class='pill " + statusClass + "' href='/submission/" + it->id + "'>" + getStatusText(it->status) + "</a></td>"
                      "<td>" + it->language + "</td>"
                      "<td class='time-cell'>" + it->submitTime + "</td></tr>";
        }
    }
    content += "</tbody></table>";

    // 分页控件
    if (totalPages > 1) {
        std::string base = "/user/" + userId + "?page=";
        content += "<div class='pagination'>";
        if (page > 1) content += "<a href='" + base + "1' class='btn btn-sm'>\u9996\u9875</a><a href='" + base + std::to_string(page-1) + "' class='btn btn-sm'>\u2039</a>";
        for (int i = 1; i <= totalPages; ++i) {
            if (i == page) content += "<span class='btn btn-sm cur'>" + std::to_string(i) + "</span>";
            else content += "<a href='" + base + std::to_string(i) + "' class='btn btn-sm'>" + std::to_string(i) + "</a>";
        }
        if (page < totalPages) content += "<a href='" + base + std::to_string(page+1) + "' class='btn btn-sm'>\u203a</a><a href='" + base + std::to_string(totalPages) + "' class='btn btn-sm'>\u672b\u9875</a>";
        content += "</div>";
    }

    return renderTemplate(user->username + " \u7684\u4e2a\u4eba\u4e3b\u9875", content, currentUser);
}
static std::string extractBoundary(const std::map<std::string, std::string>& headers) {
    std::string ctype = headers.count("content-type") ? headers.at("content-type") : "";
    size_t bp = ctype.find("boundary=");
    if (bp == std::string::npos) return "";
    std::string b = common::trim(ctype.substr(bp + 9));
    if (!b.empty() && b[0] == '"' && b[b.size() - 1] == '"') b = b.substr(1, b.size() - 2);
    return b;
}

static void parseMultipart(const std::string& body, const std::string& boundary,
                           std::map<std::string, std::string>& fields,
                           std::string& fileMime, std::string& fileData) {
    if (boundary.empty()) return;
    std::string delim = "--" + boundary;
    size_t i = 0;
    while (true) {
        i = body.find(delim, i);
        if (i == std::string::npos) break;
        i += delim.size();
        if (body.compare(i, 2, "--") == 0) break; // 结束
        size_t lineEnd = body.find("\r\n", i);
        if (lineEnd == std::string::npos) break;
        i = lineEnd + 2;
        size_t hEnd = body.find("\r\n\r\n", i);
        if (hEnd == std::string::npos) break;
        std::string head = body.substr(i, hEnd - i);
        i = hEnd + 4;
        size_t vEnd = body.find("\r\n" + delim, i);
        if (vEnd == std::string::npos) vEnd = body.find(delim, i);
        std::string value = body.substr(i, (vEnd == std::string::npos) ? body.size() - i : vEnd - i);
        std::string name;
        size_t np = head.find("name=\"");
        if (np != std::string::npos) {
            size_t ne = head.find("\"", np + 6);
            name = (ne == std::string::npos) ? "" : head.substr(np + 6, ne - np - 6);
        }
        if (head.find("filename=\"") != std::string::npos) {
            fileData = value;
            size_t ct = head.find("Content-Type:");
            if (ct != std::string::npos) fileMime = common::trim(head.substr(ct + 13));
        } else if (!name.empty()) {
            fields[name] = common::urlDecode(value);
        }
        if (vEnd == std::string::npos) break;
        i = vEnd + 2;
    }
}


// ============================================================
// helper: multi-file multipart parser + drive/testdata utilities
// ============================================================
struct UploadedFile {
    std::string name;
    std::string filename;
    std::string mime;
    std::string data;
};

static void parseMultipartMulti(const std::string& body, const std::string& boundary,
                                std::map<std::string, std::string>& fields,
                                std::vector<UploadedFile>& files) {
    if (boundary.empty()) return;
    std::string delim = "--" + boundary;
    size_t i = 0;
    while (true) {
        i = body.find(delim, i);
        if (i == std::string::npos) break;
        i += delim.size();
        if (body.compare(i, 2, "--") == 0) break;
        size_t lineEnd = body.find("\r\n", i);
        if (lineEnd == std::string::npos) break;
        i = lineEnd + 2;
        size_t hEnd = body.find("\r\n\r\n", i);
        if (hEnd == std::string::npos) break;
        std::string head = body.substr(i, hEnd - i);
        i = hEnd + 4;
        size_t vEnd = body.find("\r\n" + delim, i);
        if (vEnd == std::string::npos) vEnd = body.find(delim, i);
        std::string value = body.substr(i, (vEnd == std::string::npos) ? body.size() - i : vEnd - i);
        std::string name;
        size_t np = head.find("name=\"");
        if (np != std::string::npos) {
            size_t ne = head.find("\"", np + 6);
            name = (ne == std::string::npos) ? "" : head.substr(np + 6, ne - np - 6);
        }
        std::string filename;
        size_t fp = head.find("filename=\"");
        if (fp != std::string::npos) {
            size_t fe = head.find("\"", fp + 10);
            filename = (fe == std::string::npos) ? "" : head.substr(fp + 10, fe - fp - 10);
        }
        if (!filename.empty()) {
            UploadedFile f;
            f.name = name;
            f.filename = filename;
            f.data = value;
            size_t ct = head.find("Content-Type:");
            f.mime = (ct != std::string::npos) ? common::trim(head.substr(ct + 13)) : "";
            files.push_back(f);
        } else if (!name.empty()) {
            fields[name] = common::urlDecode(value);
        }
        if (vEnd == std::string::npos) break;
        i = vEnd + 2;
    }
}

static std::string sanitizeFilename(const std::string& name) {
    std::string base = name;
    size_t s1 = base.find_last_of('/');
    size_t s2 = base.find_last_of('\\');
    size_t p = 0;
    if (s1 != std::string::npos) p = s1 + 1;
    if (s2 != std::string::npos && s2 + 1 > p) p = s2 + 1;
    base = base.substr(p);
    std::string out;
    for (size_t k = 0; k < base.size(); ++k) {
        char c = base[k];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') continue;
        out += c;
    }
    if (out.empty() || out == "." || out == "..") return "file";
    return out;
}
//保留文件夹结构的安全相对路径（用'/'分隔）；非法返回空
static std::string sanitizeRelName(const std::string& name) {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i <= name.size(); ++i) {
        char c = (i < name.size()) ? name[i] : '/';
        if (c == '/' || c == '\\') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            if ((unsigned char)c < 32 || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|') return "";
            cur += c;
        }
    }
    std::string r;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (parts[k] == "." || parts[k] == "..") return "";
        if (!r.empty()) r += "/";
        r += parts[k];
    }
    return r;
}


static long long getFileSizeBytes(const std::string& path) {
    std::ifstream ifs(path.c_str(), std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return 0;
    return (long long)ifs.tellg();
}

static std::string humanSize(long long sz) {
    std::ostringstream os;
    if (sz < 1024) os << sz << " B";
    else if (sz < 1024 * 1024) os << std::fixed << std::setprecision(1) << (sz / 1024.0) << " KB";
    else if (sz < 1024LL * 1024 * 1024) os << std::fixed << std::setprecision(2) << (sz / (1024.0 * 1024.0)) << " MB";
    else os << std::fixed << std::setprecision(2) << (sz / (1024.0 * 1024.0 * 1024.0)) << " GB";
    return os.str();
}

static std::vector<std::string> listDirFiles(const std::string& dir) {
    std::vector<std::string> out;
    std::string search = dir + "*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string name = fd.cFileName;
        if (name != "." && name != "..") out.push_back(name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

struct DirEntry { std::string name; bool isDir; };

static std::vector<DirEntry> listDirEntries(const std::string& dir) {
    std::vector<DirEntry> out;
    std::string search = dir + "*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string name = fd.cFileName;
        if (name != "." && name != "..") { DirEntry e; e.name = name; e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0; out.push_back(e); }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

// 规范化相对路径（去掉 .. 与绝对路径成分）
static std::string sanitizeRelPath(const std::string& p) {
    std::string out;
    for (char ch : p) {
        if (ch=='\\'||ch==':'||ch=='*'||ch=='?'||ch=='"'||ch=='<'||ch=='>'||ch=='|'||ch=='\0'||ch=='\r'||ch=='\n') continue;
        out += ch;
    }
    while (!out.empty() && out[0]=='/') out.erase(0,1);
    while (!out.empty() && out.back()=='/') out.pop_back();
    if (out.find("..") != std::string::npos) return "";
    return out;
}

// 递归删除目录
static void deletePathRecursive(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        std::string search = path + "/*";
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(search.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::string n = fd.cFileName;
                if (n != "." && n != "..") deletePathRecursive(path + "/" + n);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryA(path.c_str());
    } else {
        DeleteFileA(path.c_str());
    }
}

static long long dirSizeBytes(const std::string& dir) {
    long long total = 0;
    auto entries = listDirEntries(dir);
    for (auto& e : entries) {
        if (e.isDir) total += dirSizeBytes(dir + e.name + "/");
        else total += getFileSizeBytes(dir + e.name);
    }
    return total;
}

std::string Handlers::handleAdminProblemImport(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!canManageProblems(user)) return "<script>window.location.href='/'</script>";
    DataStore& ds = DataStore::instance();
    if (method == "GET") {
        std::string content = "<div class='alert alert-info'>\u652f\u6301\u4e0a\u4f20 JSON \u6587\u4ef6\u6279\u91cf\u5bfc\u5165\u9898\u76ee\u3002\u683c\u5f0f\u4e3a\u6570\u7ec4\uff0c\u6bcf\u4e2a\u5143\u7d20\u53ef\u542b title/description/inputDesc/outputDesc/samples/timeLimit/memLimit/hidden\u3002</div>"
                              "<form method='POST' enctype='multipart/form-data'>"
                              "<div class='form-group'><label>\u9009\u62e9\u9898\u76ee JSON \u6587\u4ef6</label><input type='file' name='file' accept='.json,application/json' required/></div>"
                              "<input type='submit' value='\u5bfc\u5165' class='btn'/> <a href='/admin/problems' class='btn'>\u53d6\u6d88</a></form>";
        return renderTemplate("\u5bfc\u5165\u9898\u76ee", content, user);
    }
    std::map<std::string, std::string> fields;
    std::string fileMime, fileData;
    parseMultipart(body, extractBoundary(headers), fields, fileMime, fileData);
    if (fileData.empty()) {
        std::string c = "<div class='alert alert-error'>\u672a\u8bfb\u53d6\u5230\u6587\u4ef6</div><a href='/admin/problems/import' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u5bfc\u5165\u5931\u8d25", c, user);
    }
    if (fileData.size() > 5 * 1024 * 1024) {
        std::string c = "<div class='alert alert-error'>\u6587\u4ef6\u8fc7\u5927\uff08\u9700\u5c0f\u4e8e 5MB\uff09</div><a href='/admin/problems/import' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u5bfc\u5165\u5931\u8d25", c, user);
    }
    auto items = common::jsonParseArray(fileData);
    int added = 0, skipped = 0;
    for (const auto& it : items) {
        auto o = common::jsonParseObject(it);
        std::string title = o.count("title") ? o["title"] : "";
        if (title.empty()) { skipped++; continue; }
        Problem p;
        p.id = common::genId();
        p.title = title;
        p.description = o.count("description") ? o["description"] : "";
        p.inputDesc = o.count("inputDesc") ? o["inputDesc"] : "";
        p.outputDesc = o.count("outputDesc") ? o["outputDesc"] : "";
        if (o.count("samples")) p.samples = common::jsonParseArray(o["samples"]);
        p.timeLimit = o.count("timeLimit") ? common::stringToInt(o["timeLimit"]) : 1000;
        p.memLimit = o.count("memLimit") ? common::stringToInt(o["memLimit"]) : 256;
        p.author = user->username;
        p.createTime = common::getTimeStr();
        p.acCount = 0;
        p.submitCount = 0;
        p.hidden = o.count("hidden") ? (o["hidden"] == "1" || o["hidden"] == "true") : false;
        p.image = "";
        ds.problems.push_back(p);
        added++;
    }
    ds.saveProblems();
    std::string c = "<div class='alert alert-success'>\u5bfc\u5165\u5b8c\u6210\uff1a\u6210\u529f " + std::to_string(added) + " \u9898"
                    + (skipped > 0 ? "\uff0c\u8df3\u8fc7 " + std::to_string(skipped) + " \u9898\uff08\u7f3a\u5c11\u6807\u9898\uff09" : "")
                    + "</div><a href='/admin/problems' class='btn'>\u8fd4\u56de\u9898\u76ee\u7ba1\u7406</a>";
    return renderTemplate("\u5bfc\u5165\u5b8c\u6210", c, user);
}

std::string Handlers::handleAdminProblems(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!canManageProblems(user)) return "<script>window.location.href='/'</script>";

    int page = 1;
    int perPage = 20;
    bool searchMode = path.find("/admin/problems/search") != std::string::npos;
    std::string kw;
    if (searchMode) {
        size_t qp = path.find("q=");
        if (qp != std::string::npos) {
            size_t amp = path.find('&', qp);
            kw = common::urlDecode(path.substr(qp + 2, (amp == std::string::npos) ? std::string::npos : amp - (qp + 2)));
        }
        size_t pp = path.find("page=");
        if (pp != std::string::npos) page = common::stringToInt(path.substr(pp + 5));
    } else if (path.find("/admin/problems/page/") != std::string::npos) {
        size_t lastSlash = path.find_last_of('/');
        page = common::stringToInt(path.substr(lastSlash + 1));
    }
    if (page < 1) page = 1;

    std::string kwLower = kw;
    std::transform(kwLower.begin(), kwLower.end(), kwLower.begin(), ::tolower);

    std::vector<Problem*> list;
    for (auto& p : DataStore::instance().problems) {
        if (searchMode && !kw.empty()) {
            std::string idL = p.id; std::transform(idL.begin(), idL.end(), idL.begin(), ::tolower);
            std::string tl = p.title; std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
            std::string au = p.author; std::transform(au.begin(), au.end(), au.begin(), ::tolower);
            if (idL.find(kwLower) == std::string::npos && tl.find(kwLower) == std::string::npos && au.find(kwLower) == std::string::npos) continue;
        }
        list.push_back(&p);
    }

    int total = (int)list.size();
    int totalPages = (total + perPage - 1) / perPage;
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    int start = (page - 1) * perPage;
    int end = std::min(start + perPage, total);

    std::string content = adminNavbar(user);
    content += "<div class='action-bar' style='margin-bottom:12px;'><a href='/admin/problems/add' class='btn'>&#10133; 添加题目</a> <a href='/admin/problems/import' class='btn'>&#128229; 导入题目</a></div>";
    content += "<div class='search-bar' style='margin-bottom:12px;'><form method='get' action='/admin/problems/search' style='display:flex;gap:8px;'><input name='q' value='" + common::htmlEscape(kw) + "' placeholder='搜索ID / 标题 / 作者…' style='flex:1;'/><button class='btn'>&#128269; 搜索</button><a href='/admin/problems' class='btn btn-outline'>&#10006;</a></form></div>";

    content += "<table><thead><tr><th>ID</th><th>标题</th><th>作者</th><th>通过/提交</th><th>状态</th><th>操作</th></tr></thead><tbody>";
    for (int i = start; i < end; ++i) {
        Problem* p = list[i];
        int ac = DataStore::instance().getProblemACCount(p->id);
        int totalS = DataStore::instance().getProblemSubmitCount(p->id);
        std::string rowClass = p->hidden ? " class='hidden-problem'" : "";
        content += "<tr" + rowClass + "><td><strong>" + p->id + "</strong></td>"
                  "<td><a href='/problem/" + p->id + "'>" + common::htmlEscape(p->title) + "</a></td>"
                  "<td>" + common::htmlEscape(p->author) + "</td>"
                  "<td>" + std::to_string(ac) + "/" + std::to_string(totalS) + "</td>"
                  "<td>" + (p->hidden ? "🔒 隐藏" : "👁️ 公开") + "</td>"
                  "<td>"
                  "<a href='/admin/problem/edit/" + p->id + "' class='btn btn-sm btn-outline'>✏️</a>"
                  "<a href='/admin/problem/data/" + p->id + "' class='btn btn-sm btn-outline'>&#128193;</a>"
                  "<form method='POST' action='/admin/problem/toggle' style='display:inline;'><input type='hidden' name='problemId' value='" + p->id + "'/><input type='submit' value='" + (p->hidden ? "🔓 显示" : "🔒 隐藏") + "' class='btn btn-sm' style='background:" + (p->hidden ? "#27ae60" : "#e67e22") + ";'/></form>"
                  "<form method='POST' action='/admin/problem/delete' style='display:inline;'><input type='hidden' name='problemId' value='" + p->id + "'/><input type='submit' value='🗑️' class='btn btn-sm btn-danger' onclick='return confirm(\"确定要删除吗？\")'/></form>"
                  "</td></tr>";
    }
    content += "</tbody></table>";
    if (list.empty()) content = "<div class='search-bar' style='margin-bottom:12px;'><form method='get' action='/admin/problems/search' style='display:flex;gap:8px;'><input name='q' value='" + common::htmlEscape(kw) + "' placeholder='搜索ID / 标题 / 作者…' style='flex:1;'/><button class='btn'>&#128269; 搜索</button></form></div>" + "<div class='empty-state'><span class='icon'>📚</span>" + (searchMode ? "未找到匹配题目" : "暂无题目") + "</div>";

    if (!searchMode) {
        content += renderPagination("/admin/problems/page", page, totalPages);
    } else {
        std::string enc = common::urlEncode(kw);
        content += "<div class='pagination' style='margin-top:20px;display:flex;gap:6px;justify-content:center;flex-wrap:wrap;'>";
        if (page > 1) content += "<a class='btn btn-sm' href='/admin/problems/search?q=" + enc + "&page=" + std::to_string(page - 1) + "'>&#8592; 上一页</a>";
        for (int pi = 1; pi <= totalPages; ++pi) content += "<a class='btn btn-sm" + std::string(pi == page ? " cur" : "") + "' href='/admin/problems/search?q=" + enc + "&page=" + std::to_string(pi) + "'>" + std::to_string(pi) + "</a>";
        if (page < totalPages) content += "<a class='btn btn-sm' href='/admin/problems/search?q=" + enc + "&page=" + std::to_string(page + 1) + "'>下一页 &#8594;</a>";
        content += "</div>";
    }
    return renderTemplate("题目管理", content, user);
}
// ============================================================
// 用户设置（修改密码）
// ============================================================

// ============================================================
// 管理员 - 编译语言管理（编辑编译命令 / 添加语言）
// ============================================================
std::string Handlers::handleAdminLanguages(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";

    if (method == "POST") {
        std::string action = parseFormData(body, "action");
        if (action == "add") {
            LangConfig l;
            l.name = parseFormData(body, "name");
            l.id = parseFormData(body, "id");
            l.ext = parseFormData(body, "ext");
            l.compileCmd = parseFormData(body, "compileCmd");
            l.runCmd = parseFormData(body, "runCmd");
            l.enabled = parseFormData(body, "enabled") == "1";
            if (!l.name.empty() && !l.id.empty() && !DataStore::instance().findLangConfig(l.id)) {
                DataStore::instance().languages.push_back(l);
                DataStore::instance().saveLanguages();
            }
        } else if (action == "update") {
            std::string id = parseFormData(body, "id");
            LangConfig* l = DataStore::instance().findLangConfig(id);
            if (l) {
                l->name = parseFormData(body, "name");
                l->ext = parseFormData(body, "ext");
                l->compileCmd = parseFormData(body, "compileCmd");
                l->runCmd = parseFormData(body, "runCmd");
                l->enabled = parseFormData(body, "enabled") == "1";
                DataStore::instance().saveLanguages();
            }
        } else if (action == "delete") {
            std::string id = parseFormData(body, "id");
            auto& vec = DataStore::instance().languages;
            for (auto it = vec.begin(); it != vec.end(); ++it) {
                if (it->id == id) { vec.erase(it); break; }
            }
            DataStore::instance().saveLanguages();
        } else if (action == "toggle") {
            std::string id = parseFormData(body, "id");
            LangConfig* l = DataStore::instance().findLangConfig(id);
            if (l) { l->enabled = !l->enabled; DataStore::instance().saveLanguages(); }
        } else if (action == "setgpp") {
            security::SecConfig _c = security::getSecConfig();
            _c.gppPath = parseFormData(body, "gppPath");
            security::setSecConfig(_c);
        }
        return "<script>window.location.href='/admin/languages'</script>";
    }

    // ---- GET：列表 + 添加表单 ----
    std::string content = "<div class='alert alert-info'>&#128296; 编译语言管理：可编辑已有语言的<b>编译命令</b>/<b>运行命令</b>，也可<b>添加新语言</b>。<br/>占位符：<code>{gpp}</code>=编译器路径，<code>{src}</code>=源文件，<code>{out}</code>=输出exe。<br/><b>编译型</b>：填编译命令、运行命令留空；<b>解释型</b>（如 Python）：编译命令留空、填运行命令（如 <code>python {src}</code>）。</div>";

    std::string cfgGpp = security::getSecConfig().gppPath;
    std::string shownGpp = cfgGpp.empty() ? common::autoFindGpp() : cfgGpp;
    content += "<div class='card'><h3>&#128295; 编译器路径</h3>"
      "<form method='POST'><input type='hidden' name='action' value='setgpp'/>"
      "<div class='form-group'><label>g++ / 编译器可执行文件路径</label>"
      "<input name='gppPath' value='" + common::htmlEscape(shownGpp) + "' placeholder='例如 D:\\MinGW64\\bin\\g++.exe（留空=自动搜索）' style='width:100%;'/></div>"
      "<div style='margin-top:8px;'><input type='submit' value='&#128190; 保存编译器路径' class='btn'/></div></form>"
      "<div style='margin-top:10px;' class='alert alert-info'>当前显示为" + std::string(cfgGpp.empty() ? "自动找到的" : "已配置的") + "路径；留空保存即恢复自动搜索。</div>"
      "</div>";

    content += "<div class='card'><h3>&#10133; 添加语言</h3><form method='POST'>"
      "<input type='hidden' name='action' value='add'/>"
      "<div class='form-group'><label>\u540d\u79f0</label><input name='name' required placeholder='\u5982 Java'/></div>"
      "<div class='form-group'><label>ID\uff08\u63d0\u4ea4\u65f6\u4f7f\u7528\uff09</label><input name='id' required placeholder='\u5982 java'/></div>"
      "<div class='form-group'><label>\u6e90\u6587\u4ef6\u6269\u5c55\u540d\uff08\u4e0d\u542b\u70b9\uff09</label><input name='ext' required placeholder='\u5982 java'/></div>"
      "<div class='form-group'><label>\u7f16\u8bd1\u547d\u4ee4\uff08\u89e3\u91ca\u578b\u7559\u7a7a\uff09</label><input name='compileCmd' placeholder='javac {src}'/></div>"
      "<div class='form-group'><label>\u8fd0\u884c\u547d\u4ee4\uff08\u7f16\u8bd1\u578b\u7559\u7a7a\uff09</label><input name='runCmd' placeholder='java -cp {src}'/></div>"
      "<label style='display:flex;align-items:center;gap:8px;'><input type='checkbox' name='enabled' value='1' checked/> \u542f\u7528</label>"
      "<div style='margin-top:12px;'><input type='submit' value='&#9989; \u6dfb\u52a0' class='btn'/></div></form></div>";

    content += "<h3 style='margin-top:24px;'>\U0001f4cb \u8bed\u8a00\u5217\u8868\uff08" + std::to_string(DataStore::instance().languages.size()) + "\uff09</h3>";
    for (const auto& l : DataStore::instance().languages) {
        content += "<div class='card'>";
        content += "<div style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>"
                   "<strong>" + common::htmlEscape(l.name) + "</strong> "
                   "<span class='pill-tag'>" + common::htmlEscape(l.id) + "</span> "
                   "<span class='pill-tag'>." + common::htmlEscape(l.ext) + "</span> "
                   + (l.enabled ? "<span class='badge badge-ac'>\u542f\u7528</span>" : "<span class='badge badge-pending'>\u7981\u7528</span>") +
                   "<span style='margin-left:auto;display:flex;gap:6px;'>"
                   "<form method='POST' style='display:inline;'><input type='hidden' name='action' value='toggle'/><input type='hidden' name='id' value='" + l.id + "'/><button class='btn btn-sm btn-outline'>" + (l.enabled ? "\u7981\u7528" : "\u542f\u7528") + "</button></form>"
                   "<form method='POST' style='display:inline;' onsubmit='return confirm(\"\u786e\u5b9a\u5220\u9664\u8be5\u8bed\u8a00\uff1f\")'><input type='hidden' name='action' value='delete'/><input type='hidden' name='id' value='" + l.id + "'/><button class='btn btn-sm btn-danger'>\u5220\u9664</button></form>"
                   "</span></div>";
        content += "<form method='POST' style='margin-top:12px;'>"
                   "<input type='hidden' name='action' value='update'/><input type='hidden' name='id' value='" + l.id + "'/>"
                   "<div class='form-group'><label>\u540d\u79f0</label><input name='name' value='" + common::htmlEscape(l.name) + "'/></div>"
                   "<div class='form-group'><label>\u6e90\u6587\u4ef6\u6269\u5c55\u540d</label><input name='ext' value='" + common::htmlEscape(l.ext) + "'/></div>"
                   "<div class='form-group'><label>\u7f16\u8bd1\u547d\u4ee4</label><input name='compileCmd' value='" + common::htmlEscape(l.compileCmd) + "'/></div>"
                   "<div class='form-group'><label>\u8fd0\u884c\u547d\u4ee4</label><input name='runCmd' value='" + common::htmlEscape(l.runCmd) + "'/></div>"
                   "<label style='display:flex;align-items:center;gap:8px;'><input type='checkbox' name='enabled' value='1' " + (l.enabled ? "checked" : "") + "/> \u542f\u7528</label>"
                   "<div style='margin-top:12px;'><input type='submit' value='\U0001f4dd \u4fdd\u5b58' class='btn'/></div></form>";
        content += "</div>";
    }
    return renderTemplate("\u8bed\u8a00\u7ba1\u7406", content, user);
}

std::string Handlers::handleUserSettings(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    if (method == "GET") {
        std::string content = "<h2>账户设置</h2>"
                             "<form method='POST'>"
                             "<div class='form-group'><label>当前密码</label><input type='password' name='current_password' required/></div>"
                             "<div class='form-group'><label>新密码 (至少6个字符)</label><input type='password' name='new_password' required minlength='6'/></div>"
                             "<div class='form-group'><label>确认新密码</label><input type='password' name='confirm_password' required minlength='6'/></div>"
                             "<input type='submit' value='修改密码' class='btn'/> <a href='/user/" + user->id + "' class='btn'>取消</a>"
                             "</form>";
        return renderTemplate("账户设置", content, user);
    } else {
        std::string currentPassword = parseFormData(body, "current_password");
        std::string newPassword = parseFormData(body, "new_password");
        std::string confirmPassword = parseFormData(body, "confirm_password");
        
        if (user->password != currentPassword) {
            std::string content = "<div class='alert alert-error'>当前密码错误</div><a href='/user/settings' class='btn'>返回</a>";
            return renderTemplate("修改失败", content, user);
        }
        if (newPassword.length() < 6 || newPassword != confirmPassword) {
            std::string error = newPassword.length() < 6 ? "新密码至少6个字符" : "两次密码输入不一致";
            std::string content = "<div class='alert alert-error'>" + error + "</div><a href='/user/settings' class='btn'>返回</a>";
            return renderTemplate("修改失败", content, user);
        }
        
        user->password = newPassword;
        DataStore::instance().saveUsers();
        
        std::string content = "<div class='alert alert-success'>密码修改成功！</div>"
                             "<a href='/user/" + user->id + "' class='btn'>返回个人主页</a>";
        return renderTemplate("修改成功", content, user);
    }
}

// ============================================================
// 用户编辑资料
// ============================================================
std::string Handlers::handleUserEdit(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    
    if (method == "GET") {
        std::string content = "<form method='POST' enctype='multipart/form-data'>"
                             "<div class='form-group'><label>用户名</label>"
                             "<input name='username' value='" + common::htmlEscape(user->username) + "' required/></div>"
                             "<div class='form-group'><label>个人简介</label>"
                             "<textarea name='bio' rows='5'>" + common::htmlEscape(user->bio) + "</textarea></div>"
                             "<div class='form-group'><label>提交代码</label>"
                             "<label style='display:flex;gap:8px;align-items:center;font-weight:400;'>"
                             "<input type='checkbox' name='codePublic' value='1'" + std::string(user->codePublic ? " checked" : "") + "/>"
                             " 对其他人公开我的提交代码</label>"
                             "<div style='color:var(--muted);font-size:12.5px;margin-top:4px;'>取消勾选后，别人打开你的提交只能看到状态与用时，代码区域会隐藏；你自己与管理员始终可以查看。</div></div>"
                             "<div class='form-group'><label>头像 (上传图片)</label>"
                             "<input type='file' name='avatar' accept='image/*'/></div>"
                             "<input type='submit' value='保存修改' class='btn'/> <a href='/user/" + user->id + "' class='btn'>取消</a></form>";
        return renderTemplate("编辑个人资料", content, user);
    } else {
        std::map<std::string, std::string> fields;
        std::string fileMime, fileData;
        parseMultipart(body, extractBoundary(headers), fields, fileMime, fileData);
        std::string newUsername = fields.count("username") ? fields["username"] : user->username;
        std::string newBio = fields.count("bio") ? fields["bio"] : "";
        security::SecConfig _sc = security::getSecConfig();
        if ((int)newUsername.length() > _sc.maxUsername) newUsername = newUsername.substr(0, _sc.maxUsername);
        if ((int)newBio.length() > _sc.maxBio) newBio = newBio.substr(0, _sc.maxBio);
        if (newUsername != user->username) {
            User* existing = DataStore::instance().findUserByName(newUsername);
            if (existing && existing->id != user->id) {
                std::string content = "<div class='alert alert-error'>\u7528\u6237\u540d\u5df2\u88ab\u5360\u7528</div><a href='/user/edit' class='btn'>\u8fd4\u56de</a>";
                return renderTemplate("\u7f16\u8f91\u5931\u8d25", content, user);
            }
            user->username = newUsername;
        }
        if (!newBio.empty()) user->bio = newBio;
        // 代码公开开关：勾选=公开（默认），未勾选=仅自己与管理员可见
        user->codePublic = (fields.count("codePublic") && !fields["codePublic"].empty() && fields["codePublic"] != "0");
        if (!fileData.empty()) {
            if (fileData.size() > 2 * 1024 * 1024) {
                std::string content = "<div class='alert alert-error'>\u5934\u50cf\u56fe\u7247\u8fc7\u5927\uff08\u9700\u5c0f\u4e8e 2MB\uff09</div><a href='/user/edit' class='btn'>\u8fd4\u56de</a>";
                return renderTemplate("\u7f16\u8f91\u5931\u8d25", content, user);
            }
            std::string mime = fileMime.empty() ? "image/png" : fileMime;
            user->avatar = "data:" + mime + ";base64," + common::base64Encode(fileData);
        }
        DataStore::instance().saveUsers();
        std::string content = "<div class='alert alert-success'>个人资料更新成功！</div><a href='/user/" + user->id + "' class='btn'>查看个人主页</a>";
        return renderTemplate("编辑成功", content, user);
    }
}
// ============================================================
// 管理员面板
// ============================================================
static std::string adminCardHTML(const std::string& title, const std::string& desc, const std::string& href, const std::string& badge) {
    // 按首个空格拆：前=图标(emoji)，后=标题文本
    size_t sp = title.find(' ');
    std::string icon = (sp == std::string::npos) ? title : title.substr(0, sp);
    std::string rest = (sp == std::string::npos) ? "" : title.substr(sp + 1);
    std::string out = "<a class='acard' href='" + href + "'>";
    if (!badge.empty()) out += "<span class='a-badge'>" + badge + "</span>";
    out += "<div class='a-ic'>" + icon + "</div>";
    out += "<div class='a-body'><div class='a-title'>" + rest + "</div>";
    out += "<div class='a-desc'>" + desc + "</div></div></a>";
    return out;
}

std::string Handlers::handleAdminSystem(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    if (method == "POST") {
        std::string _act = parseFormData(body, "action");
        if (_act == "ban") {
            std::string ip = common::trim(parseFormData(body, "ip"));
            if (!ip.empty()) security::banIp(ip);
            std::string c = "<div class='alert alert-success'>\u64cd\u4f5c\u6210\u529f</div><a href='/admin/system' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u7cfb\u7edf\u5b89\u5168", c, user);
        }
        if (_act == "unban") {
            std::string ip2 = common::trim(parseFormData(body, "ip2"));
            if (!ip2.empty()) security::unbanIp(ip2);
            std::string c = "<div class='alert alert-success'>\u64cd\u4f5c\u6210\u529f</div><a href='/admin/system' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u7cfb\u7edf\u5b89\u5168", c, user);
        }
        if (parseFormData(body, "sec_only") == "1") {
            security::SecConfig _sec;
            _sec.gppPath = security::getSecConfig().gppPath; // 另一表单维护，这里保持
            _sec.ratePerSec = common::stringToInt(parseFormData(body, "ratePerSec"));
            _sec.burstThreshold = common::stringToInt(parseFormData(body, "burstThreshold"));
            _sec.windowSec = common::stringToInt(parseFormData(body, "windowSec"));
            _sec.autoBanSec = common::stringToInt(parseFormData(body, "autoBanSec"));
            _sec.captchaMinSec = common::stringToInt(parseFormData(body, "captchaMinSec"));
            _sec.captchaExpireSec = common::stringToInt(parseFormData(body, "captchaExpireSec"));
            _sec.captchaStrict = common::stringToInt(parseFormData(body, "captchaStrict"));
            _sec.captchaEnabled = parseFormData(body, "captchaEnabled") == "1" ? 1 : 0;
            _sec.capLogin = parseFormData(body, "capLogin") == "1" ? 1 : 0;
            _sec.capRegister = parseFormData(body, "capRegister") == "1" ? 1 : 0;
            _sec.capSubmit = parseFormData(body, "capSubmit") == "1" ? 1 : 0;
            _sec.capBlog = parseFormData(body, "capBlog") == "1" ? 1 : 0;
            _sec.capDiscuss = parseFormData(body, "capDiscuss") == "1" ? 1 : 0;
            _sec.capTask = parseFormData(body, "capTask") == "1" ? 1 : 0;
            _sec.capShop = parseFormData(body, "capShop") == "1" ? 1 : 0;
            _sec.capMessage = parseFormData(body, "capMessage") == "1" ? 1 : 0;
            _sec.capGroup = parseFormData(body, "capGroup") == "1" ? 1 : 0;
            _sec.capBenben = parseFormData(body, "capBenben") == "1" ? 1 : 0;
            _sec.maxBenben = common::stringToInt(parseFormData(body, "maxBenben"));
            _sec.maxBlog = common::stringToInt(parseFormData(body, "maxBlog"));
            _sec.maxBlogTitle = common::stringToInt(parseFormData(body, "maxBlogTitle"));
            _sec.maxDiscuss = common::stringToInt(parseFormData(body, "maxDiscuss"));
            _sec.maxComment = common::stringToInt(parseFormData(body, "maxComment"));
            _sec.maxTaskTitle = common::stringToInt(parseFormData(body, "maxTaskTitle"));
            _sec.maxTaskDesc = common::stringToInt(parseFormData(body, "maxTaskDesc"));
            _sec.maxTaskMsg = common::stringToInt(parseFormData(body, "maxTaskMsg"));
            _sec.maxMessage = common::stringToInt(parseFormData(body, "maxMessage"));
            _sec.maxGroupName = common::stringToInt(parseFormData(body, "maxGroupName"));
            _sec.maxBio = common::stringToInt(parseFormData(body, "maxBio"));
            _sec.maxUsername = common::stringToInt(parseFormData(body, "maxUsername"));
            _sec.maxCode = common::stringToInt(parseFormData(body, "maxCode"));
            _sec.maxProblemId = common::stringToInt(parseFormData(body, "maxProblemId"));
            _sec.maxProblemTitle = common::stringToInt(parseFormData(body, "maxProblemTitle"));
            _sec.maxProblemDesc = common::stringToInt(parseFormData(body, "maxProblemDesc"));
            _sec.maxProblemSample = common::stringToInt(parseFormData(body, "maxProblemSample"));
            _sec.maxUserProblems = common::stringToInt(parseFormData(body, "maxUserProblems"));
            _sec.maxUserProblemTime = common::stringToInt(parseFormData(body, "maxUserProblemTime"));
            _sec.maxUserProblemMem = common::stringToInt(parseFormData(body, "maxUserProblemMem"));
            _sec.maxCustomTestTime = common::stringToInt(parseFormData(body, "maxCustomTestTime"));
            _sec.maxCustomTestMem = common::stringToInt(parseFormData(body, "maxCustomTestMem"));
            _sec.maxCustomTestCode = common::stringToInt(parseFormData(body, "maxCustomTestCode"));
            _sec.maxCustomTestData = common::stringToInt(parseFormData(body, "maxCustomTestData"));
            _sec.customTestIntervalSec = common::stringToInt(parseFormData(body, "customTestIntervalSec"));
            _sec.hackRetestEnabled = parseFormData(body, "hackRetestEnabled") == "1" ? 1 : 0;
            _sec.hackIntervalSec = common::stringToInt(parseFormData(body, "hackIntervalSec"));
            _sec.judgeThreads = common::stringToInt(parseFormData(body, "judgeThreads"));
            _sec.codeGuardEnabled = parseFormData(body, "codeGuardEnabled") == "1" ? 1 : 0;
            _sec.guardHeaders = parseFormData(body, "guardHeaders") == "1" ? 1 : 0;
            _sec.guardProcess = parseFormData(body, "guardProcess") == "1" ? 1 : 0;
            _sec.guardWinApi = parseFormData(body, "guardWinApi") == "1" ? 1 : 0;
            _sec.guardNetwork = parseFormData(body, "guardNetwork") == "1" ? 1 : 0;
            _sec.guardDll = parseFormData(body, "guardDll") == "1" ? 1 : 0;
            _sec.guardFileFuncs = parseFormData(body, "guardFileFuncs") == "1" ? 1 : 0;
            _sec.guardPathEscape = parseFormData(body, "guardPathEscape") == "1" ? 1 : 0;
            _sec.guardCompilerExt = parseFormData(body, "guardCompilerExt") == "1" ? 1 : 0;
            _sec.guardExtraFuncs = common::trim(parseFormData(body, "guardExtraFuncs"));
            _sec.guardAllowFuncs = common::trim(parseFormData(body, "guardAllowFuncs"));
            _sec.guardExtraHeaders = common::trim(parseFormData(body, "guardExtraHeaders"));
            _sec.blogIntervalSec = common::stringToInt(parseFormData(body, "blogIntervalSec"));
            _sec.discussIntervalSec = common::stringToInt(parseFormData(body, "discussIntervalSec"));
            _sec.submitIntervalSec = common::stringToInt(parseFormData(body, "submitIntervalSec"));
            _sec.benbenIntervalSec = common::stringToInt(parseFormData(body, "benbenIntervalSec"));
            _sec.registerIntervalSec = common::stringToInt(parseFormData(body, "registerIntervalSec"));
            security::setSecConfig(_sec);
            std::string _c = "<div class='alert alert-success'>\u8fc7\u901f\u9608\u503c\u5df2\u4fdd\u5b58</div><a href='/admin/system' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u7cfb\u7edf\u5b89\u5168", _c, user);
        }
        SystemConfig sc;
        sc.allowRegister = parseFormData(body, "allowRegister") == "1";
        sc.allowBlog = parseFormData(body, "allowBlog") == "1";
        sc.allowDiscuss = parseFormData(body, "allowDiscuss") == "1";
        sc.allowMessages = parseFormData(body, "allowMessages") == "1";
        sc.allowSubmit = parseFormData(body, "allowSubmit") == "1";
        sc.allowJudge = parseFormData(body, "allowJudge") == "1";
        sc.allowBenben = parseFormData(body, "allowBenben") == "1";
        saveSystemConfig(sc);
        std::string c = "<div class='alert alert-success'>\u8bbe\u7f6e\u5df2\u4fdd\u5b58</div><a href='/admin/system' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u7cfb\u7edf\u5b89\u5168", c, user);
    }
    SystemConfig sc = loadSystemConfig();
    std::string content = adminNavbar(user);
    content += "<h3>\U0001f6e1\ufe0f \u7cfb\u7edf\u5b89\u5168</h3>";
    content += "<div class='alert alert-info'>\u5173\u95ed\u67d0\u9879\u540e\uff0c\u8be5\u529f\u80fd\u7684\u5165\u53e3\u5c06\u4e3a\u666e\u901a\u7528\u6237\u663e\u793a\u201c\u5df2\u5173\u95ed\u201d\u63d0\u793a</div>";
    content += "<form method='POST'>";
    auto cb = [&content](const char* name, bool on, const char* label) {
        content += "<label style='display:block;margin:8px 0;cursor:pointer;'><input type='checkbox' name='" + std::string(name) + "' value='1' " + (on ? "checked" : "") + "/> " + std::string(label) + "</label>";
    };
    cb("allowRegister", sc.allowRegister, "\u2705 \u5f00\u653e\u6ce8\u518c");
    cb("allowBlog", sc.allowBlog, "\U0001f4dd \u5f00\u653e\u535a\u5ba2");
    cb("allowDiscuss", sc.allowDiscuss, "\U0001f4ac \u5f00\u653e\u8ba8\u8bba\u533a");
    cb("allowMessages", sc.allowMessages, "\U0001f4e9 \u5f00\u653e\u79c1\u4fe1");
    cb("allowSubmit", sc.allowSubmit, "\U0001f4c4 \u5f00\u653e\u63d0\u4ea4");
    cb("allowJudge", sc.allowJudge, "\u2699\ufe0f \u5f00\u653e\u8bc4\u6d4b\u53ca\u7f16\u8bd1");
    cb("allowBenben", sc.allowBenben, "\U0001f42e \u5f00\u653e\u728d\u728d");
    content += "<input type='submit' value='\u4fdd\u5b58\u8bbe\u7f6e' class='btn'/> <a href='/admin' class='btn'>\u8fd4\u56de</a></form>";
    security::SecConfig sec = security::getSecConfig();
    content += "<h3 style='margin:26px 0 10px;'>\u2699\ufe0f \u5b89\u5168\u8bbe\u7f6e</h3>";
    content += "<div class='card' style='padding:16px;'><form method='POST'>";
    content += "<input type='hidden' name='sec_only' value='1'/>";
    content += "<h4 style='margin:0 0 6px;'>\U0001f6e1\ufe0f \u8fc7\u901f\u9632\u62a4\u9608\u503c</h4>";
    content += "<div class='form-group'><label>\u6bcf\u79d2\u8bf7\u6c42\u4e0a\u9650\uff08\u4e2a\uff09</label><input type='number' name='ratePerSec' value='" + std::to_string(sec.ratePerSec) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\u89e6\u53d1\u81ea\u52a8\u5c01\u7981\u7684\u8bf7\u6c42\u6570\uff08\u7a97\u53e3\u5185\uff09</label><input type='number' name='burstThreshold' value='" + std::to_string(sec.burstThreshold) + "' min='2'/></div>";
    content += "<div class='form-group'><label>\u9891\u7387\u7edf\u8ba1\u7a97\u53e3\uff08\u79d2\uff09</label><input type='number' name='windowSec' value='" + std::to_string(sec.windowSec) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\u81ea\u52a8\u5c01\u7981\u65f6\u957f\uff08\u79d2\uff09</label><input type='number' name='autoBanSec' value='" + std::to_string(sec.autoBanSec) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\u9a8c\u8bc1\u7801\u6700\u77ed\u95f4\u9694\uff08\u79d2\uff09</label><input type='number' name='captchaMinSec' value='" + std::to_string(sec.captchaMinSec) + "' min='0'/></div>";
    content += "<div class='form-group'><label>\u9a8c\u8bc1\u7801\u6709\u6548\u671f\uff08\u79d2\uff09</label><input type='number' name='captchaExpireSec' value='" + std::to_string(sec.captchaExpireSec) + "' min='30'/></div>";
    content += "<div class='form-group'><label>\u9a8c\u8bc1\u7801\u5f3a\u5ea6\uff080\u5f31/1\u6b63\u5e38/2\u5f3a\uff09</label><input type='number' name='captchaStrict' value='" + std::to_string(sec.captchaStrict) + "' min='0' max='2'/></div>";
    content += std::string("<div class='form-group'><label style='cursor:pointer;'><input type='checkbox' name='captchaEnabled' value='1' ") + (sec.captchaEnabled ? "checked" : "") + "/> \u2705 \u542f\u7528\u9a8c\u8bc1\u7801</label></div>";
    content += "<h4 style='margin:14px 0 6px;'>\U0001f4a1 \u6309\u529f\u80fd\u5f00\u5173\u9a8c\u8bc1\u7801</h4>";
    cb("capLogin", sec.capLogin != 0, "\U0001f511 \u767b\u5f55");
    cb("capRegister", sec.capRegister != 0, "\U0001f4dd \u6ce8\u518c");
    cb("capSubmit", sec.capSubmit != 0, "\U0001f4c4 \u63d0\u4ea4\u4ee3\u7801");
    cb("capBlog", sec.capBlog != 0, "\U0001f4dd \u535a\u5ba2\u53d1\u5e03");
    cb("capDiscuss", sec.capDiscuss != 0, "\U0001f4ac \u8ba8\u8bba\u53d1\u5e03");
    cb("capTask", sec.capTask != 0, "\U0001f4e6 \u4efb\u52a1\u53d1\u5e03");
    cb("capShop", sec.capShop != 0, "\U0001f6d2 \u5546\u57ce\u4e0b\u5355");
    cb("capMessage", sec.capMessage != 0, "\U0001f4ac \u79c1\u4fe1\u53d1\u9001");
    cb("capGroup", sec.capGroup != 0, "\U0001f465 \u521b\u5efa\u7fa4\u804a");
    cb("capBenben", sec.capBenben != 0, "\U0001f42e \u53d1\u728d\u728d");

    // ---------- \u8868\u5355\u957f\u5ea6\u9650\u5236 ----------
    content += "<h4 style='margin:16px 0 6px;'>\U0001f4cf \u8868\u5355\u957f\u5ea6\u9650\u5236\uff08\u5b57\u7b26\uff09</h4>";
    content += "<div class='alert alert-info'>\u8bbe\u5b9a\u5404\u79cd\u63d0\u4ea4\u5185\u5bb9\u7684\u6700\u5927\u957f\u5ea6\uff0c\u8d85\u8fc7\u90e8\u5206\u5c06\u81ea\u52a8\u622a\u65ad</div>";
    content += "<div class='form-group'><label>\U0001f42e \u728d\u728d\u5185\u5bb9</label><input type='number' name='maxBenben' value='" + std::to_string(sec.maxBenben) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4dd \u535a\u5ba2\u6807\u9898 / \u8ba8\u8bba\u6807\u9898</label><input type='number' name='maxBlogTitle' value='" + std::to_string(sec.maxBlogTitle) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4dd \u535a\u5ba2\u5185\u5bb9</label><input type='number' name='maxBlog' value='" + std::to_string(sec.maxBlog) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4ac \u8ba8\u8bba\u5185\u5bb9</label><input type='number' name='maxDiscuss' value='" + std::to_string(sec.maxDiscuss) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4ad \u8bc4\u8bba</label><input type='number' name='maxComment' value='" + std::to_string(sec.maxComment) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4e6 \u4efb\u52a1\u6807\u9898</label><input type='number' name='maxTaskTitle' value='" + std::to_string(sec.maxTaskTitle) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4e6 \u4efb\u52a1\u63cf\u8ff0</label><input type='number' name='maxTaskDesc' value='" + std::to_string(sec.maxTaskDesc) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4e6 \u4efb\u52a1\u79c1\u5bc6\u6d88\u606f</label><input type='number' name='maxTaskMsg' value='" + std::to_string(sec.maxTaskMsg) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4e9 \u79c1\u4fe1 / \u7fa4\u804a\u6d88\u606f</label><input type='number' name='maxMessage' value='" + std::to_string(sec.maxMessage) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f465 \u7fa4\u804a\u540d\u79f0</label><input type='number' name='maxGroupName' value='" + std::to_string(sec.maxGroupName) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f464 \u7528\u6237\u540d</label><input type='number' name='maxUsername' value='" + std::to_string(sec.maxUsername) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4dd \u4e2a\u4eba\u7b80\u4ecb</label><input type='number' name='maxBio' value='" + std::to_string(sec.maxBio) + "' min='1'/></div>";
    content += "<div class='form-group'><label>\U0001f4c4 \u63d0\u4ea4\u4ee3\u7801\u957f\u5ea6</label><input type='number' name='maxCode' value='" + std::to_string(sec.maxCode) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#128273; 用户出题-题号长度</label><input type='number' name='maxProblemId' value='" + std::to_string(sec.maxProblemId) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#127942; 用户出题-题目名称长度</label><input type='number' name='maxProblemTitle' value='" + std::to_string(sec.maxProblemTitle) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#128220; 用户出题-题面(描述)长度</label><input type='number' name='maxProblemDesc' value='" + std::to_string(sec.maxProblemDesc) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#128202; 用户出题-样例长度</label><input type='number' name='maxProblemSample' value='" + std::to_string(sec.maxProblemSample) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#128230; 用户出题-每用户最多出题数(0=不限)</label><input type='number' name='maxUserProblems' value='" + std::to_string(sec.maxUserProblems) + "' min='0'/></div>";
    content += "<div class='form-group'><label>&#9200; 用户出题-时间限制上限(ms)</label><input type='number' name='maxUserProblemTime' value='" + std::to_string(sec.maxUserProblemTime) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#128190; 用户出题-内存限制上限(MB)</label><input type='number' name='maxUserProblemMem' value='" + std::to_string(sec.maxUserProblemMem) + "' min='1'/></div>";
        content += "<div class='form-group'><label>&#9200; 自定义测试-时间限制上限(ms)</label><input type='number' name='maxCustomTestTime' value='" + std::to_string(sec.maxCustomTestTime) + "' min='1'/></div>";
        content += "<div class='form-group'><label>&#128190; 自定义测试-内存限制上限(MB)</label><input type='number' name='maxCustomTestMem' value='" + std::to_string(sec.maxCustomTestMem) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#128221; 自定义测试-代码长度</label><input type='number' name='maxCustomTestCode' value='" + std::to_string(sec.maxCustomTestCode) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#128225; 自定义测试-输入数据长度</label><input type='number' name='maxCustomTestData' value='" + std::to_string(sec.maxCustomTestData) + "' min='1'/></div>";
    content += "<div class='form-group'><label>&#9200; 自定义测试-提交间隔(秒,0=不限)</label><input type='number' name='customTestIntervalSec' value='" + std::to_string(sec.customTestIntervalSec) + "' min='0'/></div>";
            content += "<div class='form-group'><label>&#128293; Hack重测（标程通过后加入正式数据并重测所有AC提交）</label> <input type='checkbox' name='hackRetestEnabled' value='1'" + std::string(sec.hackRetestEnabled ? " checked" : "") + "/></div>";
            content += "<div class='form-group'><label>&#128293; Hack提交间隔(秒,0=不限)</label><input type='number' name='hackIntervalSec' value='" + std::to_string(sec.hackIntervalSec) + "' min='0'/></div>";
            content += "<div class='form-group'><label>&#128187; 评测线程数</label><input type='number' name='judgeThreads' value='" + std::to_string(sec.judgeThreads) + "' min='1' max='32'/></div>";

    // ---------- 编译安全（提交代码静态检查） ----------
    content += "<h4 style='margin:20px 0 6px;'>&#129514; 编译安全（提交代码静态检查）</h4>";
    content += "<div class='alert alert-info'>编译前先扫描源码，命中规则直接返回 CE（不启动编译器）。字符串与注释内容不参与函数检测，不会误伤；白名单优先级最高。</div>";
    content += "<div class='form-group'><label style='cursor:pointer;'><input type='checkbox' name='codeGuardEnabled' value='1'" + std::string(sec.codeGuardEnabled ? " checked" : "") + "/> &#9989; 启用编译安全总开关（关闭后只编译、不检查）</label></div>";
    cb("guardHeaders", sec.guardHeaders != 0, "&#128683; 危险头文件（windows.h / sys/ / winsock …）");
    cb("guardProcess", sec.guardProcess != 0, "&#128683; 进程与系统调用（system / fork / exec / getenv …）");
    cb("guardWinApi", sec.guardWinApi != 0, "&#128683; Windows API（CreateProcess / VirtualAlloc / 注册表 …）");
    cb("guardNetwork", sec.guardNetwork != 0, "&#128683; 网络（socket / connect / send …）");
    cb("guardDll", sec.guardDll != 0, "&#128683; 动态加载（dlopen / LoadLibrary …）");
    cb("guardFileFuncs", sec.guardFileFuncs != 0, "&#128683; 文件读写（freopen / fopen / ifstream / ofstream …）");
    cb("guardPathEscape", sec.guardPathEscape != 0, "&#128683; 沙箱外路径（C:\\ / ../ / /etc …）");
    cb("guardCompilerExt", sec.guardCompilerExt != 0, "&#128683; 编译器扩展（#pragma comment / __declspec / asm）");
    content += "<div class='form-group'><label>&#10133; 额外禁止的函数 / 标识符</label>"
               "<textarea name='guardExtraFuncs' rows='3' placeholder='例如：freopen, system, getenv（逗号、空格或换行分隔）'>" + common::htmlEscape(sec.guardExtraFuncs) + "</textarea></div>";
    content += "<div class='form-group'><label>&#10133; 额外禁止的头文件</label>"
               "<textarea name='guardExtraHeaders' rows='2' placeholder='例如：curl, openssl（逗号 / 空格 / 换行分隔）'>" + common::htmlEscape(sec.guardExtraHeaders) + "</textarea></div>";
    content += "<div class='form-group'><label>&#9989; 白名单（放行的函数，优先于全部禁止规则）</label>"
               "<textarea name='guardAllowFuncs' rows='2' placeholder='例如：freopen（填了就不再拦截）'>" + common::htmlEscape(sec.guardAllowFuncs) + "</textarea></div>";

    // ---------- \u53d1\u5e03\u9891\u7387\u9650\u5236 ----------
    content += "<h4 style='margin:16px 0 6px;'>\u23f0 \u53d1\u5e03\u9891\u7387\u9650\u5236\uff08\u79d2\uff09</h4>";
    content += "<div class='alert alert-info'>\u8bbe\u5b9a\u5404\u529f\u80fd\u53d1\u5e03\u7684\u6700\u5c0f\u95f4\u9694\uff0c0=\u5173\u95ed\u9650\u5236</div>";
    content += "<div class='form-group'><label>\U0001f4dd \u535a\u5ba2\u53d1\u5e03\u95f4\u9694</label><input type='number' name='blogIntervalSec' value='" + std::to_string(sec.blogIntervalSec) + "' min='0'/></div>";
    content += "<div class='form-group'><label>\U0001f4ac \u8ba8\u8bba\u53d1\u5e03\u95f4\u9694</label><input type='number' name='discussIntervalSec' value='" + std::to_string(sec.discussIntervalSec) + "' min='0'/></div>";
    content += "<div class='form-group'><label>\U0001f4e4 \u63d0\u4ea4\u4ee3\u7801\u95f4\u9694</label><input type='number' name='submitIntervalSec' value='" + std::to_string(sec.submitIntervalSec) + "' min='0'/></div>";
    content += "<div class='form-group'><label>\U0001f424 \u72c7\u72c7\u53d1\u5e03\u95f4\u9694</label><input type='number' name='benbenIntervalSec' value='" + std::to_string(sec.benbenIntervalSec) + "' min='0'/></div>";
    content += "<div class='form-group'><label>\U0001f465 \u540c\u4e00IP\u6ce8\u518c\u95f4\u9694</label><input type='number' name='registerIntervalSec' value='" + std::to_string(sec.registerIntervalSec) + "' min='0'/></div>";
    content += "<input type='submit' value='\u4fdd\u5b58\u5b89\u5168\u8bbe\u7f6e' class='btn'/></form></div>";

    // ---------- IP \u5c01\u7981\u7ba1\u7406 ----------
    content += "<h3 style='margin:26px 0 10px;'>\U0001f6ab IP \u5c01\u7981\u7ba1\u7406</h3>";
    content += "<div class='card' style='padding:16px;margin-bottom:18px;'>"
               "<form method='POST' style='display:flex;gap:10px;align-items:flex-end;flex-wrap:wrap;'>"
               "<input type='hidden' name='action' value='ban'/>"
               "<div class='form-group' style='margin:0;'><label>\u5c01\u7981 IP</label>"
               "<input name='ip' placeholder='\u8f93\u5165\u8981\u5c01\u7981\u7684 IP' required/></div>"
               "<input type='submit' value='\u5c01\u7981' class='btn btn-danger'/></form></div>";
    content += "<h3 style='margin:20px 0 10px;'>\U0001f4c8 \u8bbf\u95ee\u9891\u7387\u53ef\u89c6\u5316</h3>";
    content += "<div class='card' style='padding:16px;'>";
    auto freq = security::getFrequencyTop(15);
    if (freq.empty()) {
        content += "<div class='empty-state'>\u6682\u65e0\u6d3b\u8dc3\u8bf7\u6c42</div>";
    } else {
        int maxc = 1; for (auto& f : freq) if (f.count > maxc) maxc = f.count;
        content += "<table><thead><tr><th>IP</th><th>\u9891\u7387(\u6b21/\u7a97\u53e3)</th><th>\u901f\u5ea6(\u6b21/\u79d2)</th><th>\u53ef\u89c6\u5316</th></tr></thead><tbody>";
        for (auto& f : freq) {
            int w = (int)((long long)f.count * 100 / maxc);
            content += "<tr><td><code>" + common::htmlEscape(f.ip) + "</code></td><td>" + std::to_string(f.count) + "</td><td>" + std::to_string(f.rate) + "</td>"
                       "<td style='width:220px;'><div style='height:14px;background:#eef1f5;border-radius:6px;'><div style='height:14px;width:" + std::to_string(w) + "%;background:#e67e22;border-radius:6px;'></div></div></td></tr>";
        }
        content += "</tbody></table>";
    }
    content += "</div>";
    content += "<h3 style='margin:20px 0 10px;'>\U0001f6ab \u5f53\u524d\u5c01\u7981\u5217\u8868</h3>";
    content += "<div class='card' style='padding:16px;'>";
    auto binfo = security::getBansInfo();
    if (binfo.empty()) {
        content += "<div class='empty-state'><span class='icon'>\U0001f331</span>\u6682\u65e0\u88ab\u5c01\u7981\u7684 IP</div>";
    } else {
        content += "<table><thead><tr><th>IP</th><th>\u7c7b\u578b</th><th>\u5269\u4f59\u65f6\u95f4</th><th>\u6700\u8fd1\u7528\u6237\u6d3b\u52a8</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
        for (auto& b : binfo) {
            std::string type = b.autoBan ? "\u81ea\u52a8\u5c01\u7981" : "\u624b\u52a8\u5c01\u7981";
            std::string remain = "-";
            if (b.autoBan) { long long left = b.until - (long long)time(nullptr); if (left < 0) left = 0; remain = std::to_string(left) + " s"; }
            long long nowAct = (long long)time(nullptr);
            std::string act = "-";
            auto acts = security::ipActivity(b.ip, 6);
            if (!acts.empty()) {
                std::string joined;
                for (size_t ai = 0; ai < acts.size(); ++ai) {
                    long long ago = nowAct - acts[ai].time; if (ago < 0) ago = 0;
                    std::string agoStr = (ago < 60) ? (std::to_string(ago) + "s") : (ago < 3600 ? (std::to_string(ago/60) + "m") : (ago < 86400 ? (std::to_string(ago/3600) + "h") : (std::to_string(ago/86400) + "d")));
                    if (ai > 0) joined += ", ";
                    joined += common::htmlEscape(acts[ai].username) + " <span style='color:#888;font-size:12px;'>" + agoStr + "</span>";
                }
                act = joined;
            }
            content += "<tr><td><code>" + common::htmlEscape(b.ip) + "</code></td><td>" + type + "</td><td>" + remain + "</td><td style='font-size:13px;'>" + act + "</td><td>"
                       "<form method='POST' style='display:inline;'><input type='hidden' name='action' value='unban'/><input type='hidden' name='ip2' value='" + common::htmlEscape(b.ip) + "'/><input type='submit' value='\u89e3\u5c01' class='btn btn-sm btn-outline'/></form></td></tr>";
        }
        content += "</tbody></table>";
    }
    content += "</div>";
    return renderTemplate("\u7cfb\u7edf\u5b89\u5168", content, user);
}

std::string Handlers::handleAdmin(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) {
        std::string content = "<div class='alert alert-error'>\u6743\u9650\u4e0d\u8db3\uff0c\u9700\u8981\u7ba1\u7406\u5458\u6743\u9650</div><a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a>";
        return renderTemplate("\u6743\u9650\u4e0d\u8db3", content, user);
    }
    int acCount = 0;
    for (const auto& s : DataStore::instance().submissions) if (s.status == AC) acCount++;
    int pendingOrders = 0;
    for (const auto& o : DataStore::instance().orders) if (o.status == ORDER_PENDING) pendingOrders++;
    int pendReports = reports::pendingCount();
    int pendSol = 0;
    for (const auto& pp : DataStore::instance().posts)
        if (pp.type == SOLUTION && pp.auditStatus == "pending") pendSol++;
    std::string content;
    content += "<div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
               "<h2 style='margin:0;'>\u7ba1\u7406\u9762\u677f</h2>"
               "<a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a></div>";
    content += "<div class='grid' style='margin-top:14px;'>"
               "<div class='dash-stat'><div class='ds-num'>" + std::to_string(DataStore::instance().users.size()) + "</div><div class='ds-lbl'>\U0001f465 \u7528\u6237</div><div class='ds-bar'></div></div>"
               "<div class='dash-stat'><div class='ds-num'>" + std::to_string(DataStore::instance().problems.size()) + "</div><div class='ds-lbl'>\U0001f4da \u9898\u76ee</div><div class='ds-bar'></div></div>"
               "<div class='dash-stat'><div class='ds-num'>" + std::to_string(DataStore::instance().submissions.size()) + "</div><div class='ds-lbl'>\U0001f4ca \u63d0\u4ea4</div><div class='ds-bar'></div></div>"
               "<div class='dash-stat'><div class='ds-num'>" + std::to_string(acCount) + "</div><div class='ds-lbl'>\u2705 AC</div><div class='ds-bar'></div></div>"
               "<div class='dash-stat'><div class='ds-num'>" + std::to_string(DataStore::instance().shops.size()) + "</div><div class='ds-lbl'>\U0001f6d2 \u5546\u54c1</div><div class='ds-bar'></div></div>"
               "<div class='dash-stat'><div class='ds-num'>" + std::to_string(DataStore::instance().orders.size()) + "</div><div class='ds-lbl'>\U0001f4e6 \u8ba2\u5355</div><div class='ds-bar'></div></div></div>";

    // 待办速览
    content += "<div class='todo-strip'>"
               "<a class='todo-item" + std::string(pendReports > 0 ? " hot" : "") + "' href='/admin/reports'>\U0001f6a9 待处理举报 <b>" + std::to_string(pendReports) + "</b></a>"
               "<a class='todo-item" + std::string(pendSol > 0 ? " hot" : "") + "' href='/admin/solutions?status=pending'>\U0001f9fe 待审题解 <b>" + std::to_string(pendSol) + "</b></a>"
               "<a class='todo-item" + std::string(pendingOrders > 0 ? " hot" : "") + "' href='/admin/orders'>\U0001f4e6 待发货订单 <b>" + std::to_string(pendingOrders) + "</b></a>"
               "</div>";

    content += "<h3 style='margin:24px 0 10px;'>\U0001f465 \u7528\u6237\u4e0e\u6743\u9650</h3>";
    content += "<div class='admin-cards'>";
    content += adminCardHTML("\U0001f465 \u7528\u6237\u7ba1\u7406", "\u67e5\u770b\u7528\u6237\u3001\u53d1\u653e\u91d1\u5e01\u3001\u5206\u914d\u5957\u9910\u4e0e\u6743\u9650", "/admin/users", "");
    content += adminCardHTML("\U0001f511 \u6743\u9650\u7ba1\u7406", "\u89d2\u8272\u4e0e\u6743\u9650\u5206\u914d", "/admin/permissions", "");
    content += "</div>";

    content += "<h3 style='margin:24px 0 10px;'>\U0001f4da \u5185\u5bb9\u4e0e\u7ade\u8d5b</h3>";
    content += "<div class='admin-cards'>";
    content += adminCardHTML("\U0001f4da \u9898\u76ee\u7ba1\u7406", "\u65b0\u589e/\u7f16\u8f91/\u5220\u9664\u9898\u76ee\u3001\u663e\u85cf\u63a7\u5236", "/admin/problems", "");
        content += adminCardHTML("\U0001f502 \u9898\u76ee\u91cd\u6d4b", "\u91cd\u6d4b\u7279\u5b9a\u9898\u76ee\u63d0\u4ea4\uff0c\u53ef\u8bbe\u5206\u6570\u533a\u95f4", "/admin/rejudge", "");
    content += adminCardHTML("\U0001f4dd \u5185\u5bb9\u7ba1\u7406", "\u5e16\u5b50/\u535a\u5ba2/\u8ba8\u8bba\u5e16\u7ba1\u7406", "/admin/posts", "");
    content += adminCardHTML("\U0001f6a9 \u4e3e\u62a5\u5904\u7406", "\u7528\u6237\u4e3e\u62a5\u7684\u5e16\u5b50\u4e0e\u56de\u590d\uff0c\u53ef\u4e00\u952e\u5220\u9664", "/admin/reports", pendReports > 0 ? (std::to_string(pendReports) + " \u5f85\u5904\u7406") : "");
    content += adminCardHTML("\U0001f9fe \u9898\u89e3\u5ba1\u6838", "\u5ba1\u6838\u7528\u6237\u63d0\u4ea4\u7684\u9898\u89e3", "/admin/solutions", pendSol > 0 ? (std::to_string(pendSol) + " \u5f85\u5ba1\u6838") : "");
    content += adminCardHTML("\U0001f4dd \u8bd5\u5377\u7ba1\u7406", "\u521b\u5efa\u9009\u62e9/\u586b\u7a7a\u8bd5\u5377\u3001\u5f55\u5165\u7b54\u6848\u4e0e\u89e3\u6790", "/admin/papers", "");
    content += adminCardHTML("\U0001f3c6 \u6bd4\u8d5b\u7ba1\u7406", "\u521b\u5efa\u4e0e\u7f16\u8f91\u7ade\u8d5b", "/admin/contests", "");
    content += adminCardHTML("\U0001f4e2 \u516c\u544a\u7ba1\u7406", "\u7f16\u8f91\u9996\u9875\u516c\u544a", "/admin/announcement", "");
    content += "</div>";

    content += "<h3 style='margin:24px 0 10px;'>\U0001f6d2 \u5546\u57ce\u4e0e\u7cfb\u7edf</h3>";
    content += "<div class='admin-cards'>";
    content += adminCardHTML("\U0001f6d2 \u5546\u54c1\u7ba1\u7406", "\u589e\u5220\u6539\u5546\u54c1\u3001\u4e0a\u67b6/\u4e0b\u67b6\u3001\u8bbe\u5b9a\u865a\u62df/\u5b9e\u4f53", "/admin/shop", "");
    content += adminCardHTML("\U0001f4e6 \u8d2d\u4e70\u8bb0\u5f55", "\u67e5\u770b\u8ba2\u5355\u3001\u5904\u7406\u5b9e\u4f53\u5546\u54c1\u53d1\u8d27", "/admin/orders", pendingOrders > 0 ? (std::to_string(pendingOrders) + " \u5f85\u5904\u7406") : "");
    content += adminCardHTML("\U0001f916 AI \u7ba1\u7406", "\u6a21\u578b / \u5957\u9910 / \u989d\u5ea6 / \u5151\u6362\u7801", "/admin/ai", "");
    content += adminCardHTML("\U0001f527 \u7f16\u8bd1\u8bed\u8a00\u7ba1\u7406", "\u7f16\u8f91\u7f16\u8bd1\u547d\u4ee4\u3001\u6dfb\u52a0\u8bed\u8a00", "/admin/languages", "");
    content += adminCardHTML("\u2601\ufe0f \u4e91\u76d8\u8bbe\u7f6e", "\u8bbe\u5b9a\u4e91\u76d8\u4e0a\u4f20\u4e0a\u9650", "/admin/drive", "");
    content += adminCardHTML("\U0001f6e1\ufe0f \u7cfb\u7edf\u5b89\u5168", "\u7cfb\u7edf\u5f00\u5173\u3001\u8fc7\u901f\u9632\u62a4\u9608\u503c\u3001\u9a8c\u8bc1\u7801\u914d\u7f6e\u3001IP \u5c01\u7981\u4e0e\u9891\u7387\u53ef\u89c6\u5316", "/admin/system", "");
    content += "</div>";

    content += "<style>"
        ".admin-cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:14px;}"
        ".acard{display:flex;flex-direction:column;gap:6px;background:#fff;border:1px solid #e8ecf1;border-radius:10px;padding:16px;text-decoration:none;color:#2c3e50;transition:.15s;}"
        ".acard:hover{box-shadow:0 4px 14px rgba(0,0,0,.08);transform:translateY(-2px);}"
        ".acard .a-title{font-size:15px;font-weight:600;}"
        ".acard .a-desc{font-size:12px;color:#7f8c8d;line-height:1.5;}"
        ".acard .a-badge{align-self:flex-start;background:#e74c3c;color:#fff;border-radius:12px;padding:2px 10px;font-size:12px;}"
        "</style>";
    return renderTemplate("\u7ba1\u7406\u9762\u677f", content, user);
}

std::string Handlers::handleAdminOrders(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    int pending = 0;
    for (const auto& o : DataStore::instance().orders) if (o.status == ORDER_PENDING) pending++;
    std::string content = "<div class='action-bar' style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
                          "<h2>\U0001f4e6 \u8d2d\u4e70\u8bb0\u5f55</h2>"
                          "<a href='/admin/shop' class='btn'>\u5546\u54c1\u7ba1\u7406</a></div>";
    content += "<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:14px;margin-bottom:16px;'>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(DataStore::instance().orders.size()) + "</div><div class='stat-label'>\U0001f4e6 \u603b\u8ba2\u5355</div></div>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(pending) + "</div><div class='stat-label'>\u23f3 \u5f85\u5904\u7406</div></div></div>";
    content += "<div style='margin-bottom:12px;'>"
               "<button class='btn btn-sm' onclick='filterOrders(-1)'>\u5168\u90e8</button> "
               "<button class='btn btn-sm' onclick='filterOrders(0)'>\u5f85\u5904\u7406</button> "
               "<button class='btn btn-sm' onclick='filterOrders(1)'>\u5df2\u53d1\u8d27</button> "
               "<button class='btn btn-sm' onclick='filterOrders(2)'>\u5df2\u53d6\u6d88</button></div>";
    if (DataStore::instance().orders.empty()) {
        content += "<div class='empty-state'><span class='icon'>\U0001f6d2</span>\u6682\u65e0\u8d2d\u4e70\u8bb0\u5f55</div>";
    } else {
        content += "<table id='orderTable'><thead><tr><th>\u65f6\u95f4</th><th>\u7528\u6237</th><th>\u5546\u54c1</th><th>\u6570\u91cf</th><th>\u603b\u4ef7</th><th>\u6536\u8d27\u4fe1\u606f</th><th>\u72b6\u6001</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
        for (auto it = DataStore::instance().orders.rbegin(); it != DataStore::instance().orders.rend(); ++it) {
            const Order& o = *it;
            std::string st, stColor;
            int stv = o.status;
            if (stv == ORDER_PENDING) { st = "\u5f85\u5904\u7406"; stColor = "#f39c12"; }
            else if (stv == ORDER_FULFILLED) { st = "\u5df2\u53d1\u8d27"; stColor = "#27ae60"; }
            else { st = "\u5df2\u53d6\u6d88"; stColor = "#95a5a6"; }
            content += "<tr data-status='" + std::to_string(stv) + "'>"
                       "<td style='font-size:13px;'>" + o.time + "</td>"
                       "<td><a href='/user/" + o.userId + "'>" + common::htmlEscape(o.username) + "</a></td>"
                       "<td>" + o.icon + " " + common::htmlEscape(o.itemName) + "</td>"
                       "<td>x" + std::to_string(o.quantity) + "</td>"
                       "<td style='color:#e67e22;font-weight:bold;'>\U0001fa99 " + std::to_string(o.price) + "</td>"
                       "<td style='font-size:13px;max-width:220px;word-break:break-word;'>" + common::htmlEscape(o.contact) + "</td>"
                       "<td><span class='badge' style='background:" + stColor + "'>" + st + "</span></td><td>";
            if (stv == ORDER_PENDING) {
                content += "<form method='POST' action='/admin/order/update' style='display:inline;'><input type='hidden' name='id' value='" + o.id + "'/><input type='hidden' name='action' value='fulfill'/><button class='btn btn-sm btn-success'>\u53d1\u8d27/\u5b8c\u6210</button></form> ";
                content += "<form method='POST' action='/admin/order/update' style='display:inline;'><input type='hidden' name='id' value='" + o.id + "'/><input type='hidden' name='action' value='cancel'/><button class='btn btn-sm btn-danger'>\u53d6\u6d88</button></form>";
            } else {
                content += "<span style='color:#b0b8c4;font-size:13px;'>-</span>";
            }
            content += "</td></tr>";
        }
        content += "</tbody></table>";
        content += "<script>"
                   "function filterOrders(v){var rows=document.querySelectorAll('#orderTable tbody tr');rows.forEach(function(r){if(v===-1||r.getAttribute('data-status')==String(v)){r.style.display='';}else{r.style.display='none';}});}"
                   "</script>";
    }
    return renderTemplate("\u8d2d\u4e70\u8bb0\u5f55", content, user);
}

std::string Handlers::handleAdminOrderUpdate(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string id = parseFormData(body, "id");
    std::string action = parseFormData(body, "action");
    Order* o = DataStore::instance().findOrderById(id);
    if (o) {
        if (action == "fulfill" && o->status == ORDER_PENDING) {
            o->status = ORDER_FULFILLED;
            pushSystemNotification(o->userId, "\U0001f4e6 \u4f60\u7684\u5b9e\u4f53\u5546\u54c1\u8ba2\u5355\u300a" + o->itemName + "\u300b\u5df2\u53d1\u8d27/\u5b8c\u6210");
        } else if (action == "cancel" && o->status == ORDER_PENDING) {
            o->status = ORDER_CANCELLED;
            DataStore::instance().addGold(o->userId, o->price);
            pushSystemNotification(o->userId, "\u274c \u4f60\u7684\u5b9e\u4f53\u5546\u54c1\u8ba2\u5355\u300a" + o->itemName + "\u300b\u5df2\u88ab\u53d6\u6d88\uff0c\u5df2\u9000\u56de " + std::to_string(o->price) + " \u91d1\u5e01");
        }
        DataStore::instance().saveOrders();
    }
    return "<script>window.location.href='/admin/orders'</script>";
}

std::string Handlers::handleAdminAI(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";

    if (method == "POST") {
        std::string action = parseFormData(body, "action");
        aiquota::Config cfg = aiquota::load();
        if (action == "global") {
            int wh = common::stringToInt(parseFormData(body, "windowHours"));
            if (wh < 1) wh = 5;
            if (wh > 168) wh = 168;
            cfg.windowHours = wh;
            long long dw = common::stringToLongLong(parseFormData(body, "defWindow"));
            long long dm = common::stringToLongLong(parseFormData(body, "defMonth"));
            cfg.defWindow = dw > 0 ? dw : 0;
            cfg.defMonth = dm > 0 ? dm : 0;
            double pk = atof(parseFormData(body, "defPerK").c_str());
            if (pk > 0) cfg.defPerK = pk;
            aiquota::save(cfg);
        } else if (action == "price") {
            std::string model = common::trim(parseFormData(body, "model"));
            double perK = atof(parseFormData(body, "perK").c_str());
            if (!model.empty() && perK > 0) {
                bool found = false;
                for (size_t i = 0; i < cfg.prices.size(); i++)
                    if (cfg.prices[i].model == model) { cfg.prices[i].perK = perK; found = true; }
                if (!found) { aiquota::Price p; p.model = model; p.perK = perK; cfg.prices.push_back(p); }
                aiquota::save(cfg);
            }
        } else if (action == "rmprice") {
            std::string model = common::trim(parseFormData(body, "model"));
            std::vector<aiquota::Price> keep;
            for (size_t i = 0; i < cfg.prices.size(); i++)
                if (cfg.prices[i].model != model) keep.push_back(cfg.prices[i]);
            cfg.prices = keep;
            aiquota::save(cfg);
        } else if (action == "userquota") {
            std::string uid = parseFormData(body, "userId");
            User* t = DataStore::instance().findUserById(uid);
            if (t) {
                long long w = common::stringToLongLong(parseFormData(body, "window"));
                long long m = common::stringToLongLong(parseFormData(body, "month"));
                t->aiWindowQuota = w > 0 ? w : 0;
                t->aiMonthQuota = m > 0 ? m : 0;
                t->aiPlanId.clear();
                DataStore::instance().saveUsers();
            }
        } else if (action == "plan") {
            std::string uid = parseFormData(body, "userId");
            std::string pid = common::trim(parseFormData(body, "planId"));
            User* t = DataStore::instance().findUserById(uid);
            if (t) {
                aiplan::applyPlan(*t, pid);
                DataStore::instance().saveUsers();
            }
        } else if (action == "bonus") {
            std::string uid = parseFormData(body, "userId");
            long long amt = common::stringToLongLong(parseFormData(body, "amount"));
            if (amt > 0) aiquota::addBonusMonth(uid, amt);
        } else if (action == "reset") {
            aiquota::resetMonth(parseFormData(body, "userId"));
        }
        return "<script>window.location.href='/admin/ai'</script>";
    }

    aiquota::Config cfg = aiquota::load();
    std::string content;
    content += adminNavbar(user);
    content += "<div class='action-bar' style='display:flex;justify-content:flex-end;align-items:center;gap:8px;margin-bottom:10px;'>"
               "<a href='/admin/ai/providers' class='btn btn-sm'>🔌 提供方与模型</a>"
               "<a href='/admin/ai/plans' class='btn btn-sm'>💳 套餐</a>"
               "<a href='/llm' class='btn'>打开 AI 对话</a></div>";
    content += "<div class='alert alert-info'>计费单位：<b>算力点</b>。双重额度：滚动的 "
               + std::to_string(cfg.windowHours) + " 小时窗口额度 + 自然月额度，<b>任一用尽立刻停止输出</b>。"
               "单价 = 算力点 / 千 token，可按模型单独设置。</div>";

    // ---------- 中转状态 & 测试模型 ----------
    {
        std::vector<aiprov::ModelEntry> ents = aiprov::entries();
        int provCount = 0;
        std::vector<aiprov::Provider> provs = aiprov::load();
        for (size_t i = 0; i < provs.size(); i++) if (provs[i].enabled) provCount++;
        std::string testModel = aiplan::testModelId();
        if (testModel.empty()) {
            for (size_t i = 0; i < ents.size(); i++) {
                if (ents[i].providerId == "openrouter" && ents[i].freeModel) { testModel = ents[i].id; break; }
            }
            if (testModel.empty() && !ents.empty()) testModel = ents[0].id;
        }
        std::string opts;
        for (size_t i = 0; i < ents.size(); i++) {
            opts += "<option value='" + common::htmlEscape(ents[i].id) + "'" +
                    std::string(ents[i].id == testModel ? " selected" : "") + ">" +
                    common::htmlEscape(ents[i].name + " · " + ents[i].providerName) + "</option>";
        }
        content += "<div class='card'><h3>🛰 中转状态与自检</h3>"
                   "<div class='relay-grid'>"
                   "<div><span class='relay-k'>中转模式</span><b>服务端中转</b>：浏览器只请求本站，Key 不入前端</div>"
                   "<div><span class='relay-k'>启用提供方</span><b>" + std::to_string(provCount) + "</b> 家</div>"
                   "<div><span class='relay-k'>可用模型</span><b>" + std::to_string(ents.size()) + "</b> 个</div>"
                   "<div><span class='relay-k'>测试模型</span><b>" + common::htmlEscape(testModel.empty() ? "未配置" : testModel) + "</b></div>"
                   "</div>";
        if (!ents.empty()) {
            content += "<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:10px;'>"
                       "<select id='aiTestModel' style='flex:1;min-width:260px;max-width:420px;'>" + opts + "</select>"
                       "<button class='btn btn-sm' type='button' onclick='aiRelayTest()'>▶ 跑一次中转测试</button>"
                       "<span id='aiTestMsg' style='font-size:12.5px;color:var(--muted);'>用所选模型真发一次对话，验证 Key / 中转 / 计费链路</span></div>"
                       "<div id='aiTestOut' style='display:none;margin-top:10px;padding:10px 12px;border-radius:8px;background:var(--surface-2);font-size:13px;line-height:1.6;'></div>"
                       "<script>"
                       "async function aiRelayTest(){var m=document.getElementById('aiTestModel').value;"
                       "var msg=document.getElementById('aiTestMsg');var out=document.getElementById('aiTestOut');"
                       "out.style.display='block';out.textContent='正在请求 '+m+' …';msg.textContent='请求中…';"
                       "try{var r=await fetch('/admin/ai/test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({model:m,prompt:'只回一句话：中转正常'})});"
                       "var j=await r.json();"
                       "if(j.ok){out.innerHTML='<b style=\"color:#27ae60\">✅ 中转正常</b> · '+j.provider+' · '+j.latencyMs+' ms · '+(j.promptTokens+j.completionTokens)+' tokens<br/>'+j.html;msg.textContent='测试通过';}"
                       "else{out.innerHTML='<b style=\"color:#e74c3c\">❌ 失败</b><br/>'+j.error;msg.textContent='测试失败';}}"
                       "catch(e){out.textContent='❌ 请求异常：'+e;}}"
                       "</script>";
        } else {
            content += "<div class='card-meta' style='margin-top:10px;'>还没有已启用的模型，先去 <a href='/admin/ai/providers'>提供方与模型</a> 扫描并勾选。</div>";
        }
        content += "<div class='card-meta' style='margin-top:10px;'>所有上游请求都从服务器发出（WinHTTP），模型列表只下发 id/名称；API Key 仅保存在 <code>data/ai_providers.json</code>。</div></div>";
    }

    // ---------- 全局额度 & 单价 ----------
    char pk[64];
    snprintf(pk, sizeof(pk), "%.4g", cfg.defPerK);
    content += "<div class='card'><h3>⚙️ 全局额度与默认单价</h3><form method='POST'>"
               "<input type='hidden' name='action' value='global'/>"
               "<div style='display:flex;gap:12px;flex-wrap:wrap;'>"
               "<div class='form-group' style='flex:1;min-width:150px;'><label>窗口时长（小时）</label>"
               "<input name='windowHours' type='number' min='1' max='168' value='" + std::to_string(cfg.windowHours) + "'/></div>"
               "<div class='form-group' style='flex:1;min-width:150px;'><label>默认窗口额度（算力点）</label>"
               "<input name='defWindow' type='number' min='0' value='" + std::to_string(cfg.defWindow) + "'/></div>"
               "<div class='form-group' style='flex:1;min-width:150px;'><label>默认月额度（算力点）</label>"
               "<input name='defMonth' type='number' min='0' value='" + std::to_string(cfg.defMonth) + "'/></div>"
               "<div class='form-group' style='flex:1;min-width:150px;'><label>默认单价（算力点/千token）</label>"
               "<input name='defPerK' type='number' step='0.01' min='0.01' value='" + std::string(pk) + "'/></div>"
               "</div><button class='btn' type='submit'>保存全局配置</button>"
               "<div class='card-meta' style='margin-top:8px;'>用户额度填 0 表示使用此处的默认值。</div></form></div>";

    // ---------- 已启用模型（直接读配置，零网络请求，页面秒开） ----------
    {
        std::vector<aiprov::ModelEntry> ents = aiprov::entries();
        std::string chips;
        for (size_t i = 0; i < ents.size() && i < 40; i++) {
            chips += "<span class='pill' style='background:var(--surface-2);color:var(--text);margin:3px 0;'>" +
                     common::htmlEscape(ents[i].id) + "</span> ";
        }
        if (ents.empty()) chips = "<span style='color:#c0392b;'>还没有启用任何模型</span>";
        content += "<div class='card'><h3 style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>🧩 已启用模型"
                   "<span style='flex:1;'></span>"
                   "<a class='btn btn-sm' href='/admin/ai/providers'>扫描 / 选择模型 →</a></h3>"
                   "<div style='line-height:2;'>" + chips + "</div>"
                   "<div class='card-meta' style='margin-top:8px;'>扫描按提供方分开进行、支持搜索，在「提供方与模型」页的小窗里操作；本页不再触发任何上游请求。</div></div>";
    }
    content += "<div class='card'><h3>💲 模型单价</h3><table style='width:100%;'><thead><tr>"
               "<th>模型</th><th>单价（算力点/千token）</th><th>操作</th></tr></thead><tbody>";
    for (size_t i = 0; i < cfg.prices.size(); i++) {
        char p2[64];
        snprintf(p2, sizeof(p2), "%.4g", cfg.prices[i].perK);
        content += "<tr><td>" + common::htmlEscape(cfg.prices[i].model) + "</td><td>" + std::string(p2) + "</td>"
                   "<td><form method='POST' style='display:inline;'>"
                   "<input type='hidden' name='action' value='rmprice'/>"
                   "<input type='hidden' name='model' value='" + common::htmlEscape(cfg.prices[i].model) + "'/>"
                   "<input type='submit' class='btn btn-sm btn-danger' value='删除'/></form></td></tr>";
    }
    content += "</tbody></table><form method='POST' style='display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end;margin-top:12px;'>"
               "<input type='hidden' name='action' value='price'/>"
               "<div class='form-group' style='margin:0;flex:1;min-width:180px;'><label>模型名</label>"
               "<input name='model' placeholder='如 deepseek-chat' required/></div>"
               "<div class='form-group' style='margin:0;width:180px;'><label>单价</label>"
               "<input name='perK' type='number' step='0.01' min='0.01' value='1' required/></div>"
               "<button class='btn' type='submit'>添加 / 修改</button></form></div>";

    // ---------- 用户额度分配（支持搜索 / 风险筛选 / 分页 / 套餐） ----------
    std::string q = common::trim(queryParam(path, "q"));
    std::string qlow = q;
    for (size_t i = 0; i < qlow.size(); i++) qlow[i] = (char)tolower((unsigned char)qlow[i]);
    bool riskOnly = queryParam(path, "risk") == "1";
    int page = common::stringToInt(queryParam(path, "page"));
    if (page < 1) page = 1;

    std::map<std::string, long long> calls24, costAll;
    {
        std::vector<aitools::LogRow> logs = aitools::readUsageLog();
        long long now = (long long)time(NULL);
        for (size_t i = 0; i < logs.size(); i++) {
            if (logs[i].ts >= now - 86400) calls24[logs[i].userId]++;
            costAll[logs[i].userId] += logs[i].cost;
        }
    }
    std::vector<aiplan::Plan> plans = aiplan::load();
    if (plans.empty()) { aiplan::ensureSeed(); plans = aiplan::load(); }

    // 批量取额度：配置与用量各只读一次，避免用户多时逐行读盘
    std::vector<User*> matched;
    std::vector<aiquota::InfoQuery> iqs;
    for (size_t i = 0; i < DataStore::instance().users.size(); i++) {
        User& u = DataStore::instance().users[i];
        if (!q.empty()) {
            std::string name = u.username;
            for (size_t k = 0; k < name.size(); k++) name[k] = (char)tolower((unsigned char)name[k]);
            if (name.find(qlow) == std::string::npos && u.id != q) continue;
        }
        matched.push_back(&u);
        aiquota::InfoQuery iq;
        iq.userId = u.id;
        iq.windowQuota = u.aiWindowQuota;
        iq.monthQuota = u.aiMonthQuota;
        iqs.push_back(iq);
    }
    std::vector<aiquota::Info> infos = aiquota::infoMany(iqs);

    struct RowView { User* u; aiquota::Info qi; long long c24; long long cost; bool risk; std::string riskText; };
    std::vector<RowView> view;
    for (size_t i = 0; i < matched.size(); i++) {
        User& u = *matched[i];
        RowView rv;
        rv.u = &u;
        rv.qi = (i < infos.size()) ? infos[i] : aiquota::Info();
        rv.c24 = calls24.count(u.id) ? calls24[u.id] : 0;
        rv.cost = costAll.count(u.id) ? costAll[u.id] : 0;
        rv.risk = false;
        long long wLeft = rv.qi.windowQuota - rv.qi.windowUsed;
        long long mLeft = rv.qi.monthQuota - rv.qi.monthUsed;
        if (rv.qi.windowQuota > 0 && wLeft * 100 <= rv.qi.windowQuota * 10) { rv.risk = true; rv.riskText += "窗口将尽 "; }
        if (rv.qi.monthQuota > 0 && mLeft * 100 <= rv.qi.monthQuota * 10) { rv.risk = true; rv.riskText += "月额度将尽 "; }
        if (rv.c24 >= 40) { rv.risk = true; rv.riskText += "24h 高频 "; }
        if (riskOnly && !rv.risk) continue;
        view.push_back(rv);
    }
    int PAGE_SIZE = 30;
    int totalPages = (int)((view.size() + PAGE_SIZE - 1) / PAGE_SIZE);
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    int start = (page - 1) * PAGE_SIZE;
    int end = std::min((int)view.size(), start + PAGE_SIZE);

    std::string qEnc = common::urlEncode(q);
    content += "<div class='card'><h3 style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>👥 用户额度与套餐"
               "<span class='spacer' style='flex:1;'></span>"
               "<a class='btn btn-sm' href='/admin/ai/providers'>提供方</a>"
               "<a class='btn btn-sm' href='/admin/ai/plans'>套餐</a></h3>";
    content += "<form method='GET' action='/admin/ai' style='display:flex;gap:8px;flex-wrap:wrap;align-items:flex-end;margin-bottom:10px;'>"
               "<div class='form-group' style='margin:0;flex:1;min-width:180px;'><label>搜索用户（用户名 / id）</label>"
               "<input name='q' value='" + common::htmlEscape(q) + "' placeholder='输入用户名或 id'/></div>"
               "<label class='ai-chk' style='padding-bottom:8px;'><input type='checkbox' name='risk' value='1'" + std::string(riskOnly ? " checked" : "") + "/> 只看风险用户</label>"
               "<button class='btn btn-sm' type='submit'>筛选</button>"
               "<span style='color:var(--muted);font-size:12px;margin-left:auto;'>共 " + std::to_string(view.size()) + " 人 · 第 " + std::to_string(page) + "/" + std::to_string(totalPages) + " 页</span>"
               "</form>";
    content += "<div class='card-meta' style='margin-bottom:10px;'>用户额度由<b>套餐</b>决定：直接在左侧下拉切换套餐即可；「加额度」只临时增加本月余额，「重置」清零本月用量。风险标记：窗口/月额度剩余≤10% 或 24 小时调用 ≥40 次。</div>";
    content += "<table style='width:100%;font-size:13px;'><thead><tr>"
               "<th style='width:130px;'>用户</th><th style='width:150px;'>套餐</th><th style='width:100px;'>窗口</th>"
               "<th style='width:100px;'>本月</th><th style='width:80px;'>额外</th><th style='width:70px;'>24h</th><th style='width:90px;'>风险</th><th>额度分配与操作</th></tr></thead><tbody>";
    for (int i = start; i < end; i++) {
        User& u = *view[i].u;
        aiquota::Info qi = view[i].qi;
        std::string wColor = (qi.windowQuota - qi.windowUsed) <= 0 ? "#e74c3c" : (view[i].risk ? "#e6a23c" : "#27ae60");
        std::string mColor = (qi.monthQuota - qi.monthUsed) <= 0 ? "#e74c3c" : "#27ae60";
        std::string planName = "默认";
        for (size_t k = 0; k < plans.size(); k++) if (plans[k].id == u.aiPlanId) planName = plans[k].name;
        content += "<tr><td><a href='/user/" + u.id + "'>" + common::htmlEscape(u.username) + "</a>"
                   "<div style='color:var(--muted);font-size:11.5px;'>#" + u.id + "</div></td>"
                   "<td><form method='POST' style='display:flex;gap:4px;align-items:center;'>"
                   "<input type='hidden' name='userId' value='" + u.id + "'/>"
                   "<select name='planId' style='width:96px;' onchange=\"this.form.querySelector('button').click()\">"
                   "<option value=''>默认</option>";
        for (size_t k = 0; k < plans.size(); k++) {
            content += "<option value='" + common::htmlEscape(plans[k].id) + "'" +
                       std::string(plans[k].id == u.aiPlanId ? " selected" : "") + ">" + common::htmlEscape(plans[k].name) + "</option>";
        }
        content += "</select><button class='btn btn-sm' name='action' value='plan' type='submit' style='display:none;'>保存</button>"
                   "</form><div style='color:var(--muted);font-size:11.5px;'>" + common::htmlEscape(planName) + "</div></td>"
                   "<td style='color:" + wColor + ";'>" + std::to_string(qi.windowUsed) + " / " + std::to_string(qi.windowQuota) + "</td>"
                   "<td style='color:" + mColor + ";'>" + std::to_string(qi.monthUsed) + " / " + std::to_string(qi.monthQuota) + "</td>"
                   "<td style='color:#8e44ad;'>🎁 " + std::to_string(qi.extra) + "</td>"
                   "<td>" + std::to_string(view[i].c24) + "</td>"
                   "<td>" + (view[i].risk ? ("<span style='color:#e0553f;font-size:12px;'>⚠ " + common::htmlEscape(view[i].riskText) + "</span>") : "<span style='color:#95a5a6;'>正常</span>") + "</td>"
                   "<td><form method='POST' style='display:flex;gap:6px;align-items:center;flex-wrap:wrap;'>"
                   "<input type='hidden' name='userId' value='" + u.id + "'/>"
                   "<input name='amount' type='number' min='1' value='100' style='width:70px;' title='临时增加本月额度（不改套餐）'/>"
                   "<button class='btn btn-sm btn-outline' name='action' value='bonus' type='submit'>加额度</button>"
                   "<button class='btn btn-sm btn-outline' name='action' value='reset' type='submit' onclick=\"return confirm('清零本月用量？')\">重置用量</button>"
                   "</form></td></tr>";
    }
    if (view.empty()) content += "<tr><td colspan='8' class='empty-td'>没有匹配的用户</td></tr>";
    content += "</tbody></table>";
    if (totalPages > 1) {
        content += "<div style='display:flex;gap:8px;justify-content:center;margin-top:12px;'>";
        for (int p2 = 1; p2 <= totalPages && p2 <= 20; p2++) {
            content += "<a class='btn btn-sm" + std::string(p2 == page ? "" : " btn-outline") + "' href='/admin/ai?q=" + qEnc +
                       (riskOnly ? "&risk=1" : "") + "&page=" + std::to_string(p2) + "'>" + std::to_string(p2) + "</a>";
        }
        content += "</div>";
    }
    content += "</div>";

    // ---------- 兑换码 ----------
    content += "<div class='card' style='padding:20px;'><h3>🔑 生成兑换码（算力点）</h3>"
               "<div style='display:flex;gap:12px;flex-wrap:wrap;align-items:flex-end;'>"
               "<div class='form-group' style='flex:1;min-width:140px;margin:0;'><label>每个码算力点</label><input id='aiAmount' type='number' value='200' min='1'/></div>"
               "<div class='form-group' style='flex:1;min-width:140px;margin:0;'><label>有效期（天）</label><input id='aiDays' type='number' value='30' min='1'/></div>"
               "<button onclick='genAICode()' class='btn'>生成兑换码</button></div>"
               "<p class='card-meta' id='aiGenMsg' style='margin-top:12px;color:#27ae60;'></p>"
               "<div class='card-meta'>兑换码与商店商品都直接充入<b>额外算力点</b>（不随月份重置，用完即没）；管理员「加额度」才会增加本月额度。</div></div>";

    content += "<div class='card' style='padding:20px;'><h3>📋 兑换码列表</h3>"
               "<table id='codeTable' style='font-size:13px;'><thead><tr><th>兑换码</th><th>算力点</th><th>状态</th><th>使用人</th><th>创建时间</th><th>有效期至</th></tr></thead><tbody></tbody></table>"
               "<div style='display:flex;justify-content:space-between;align-items:center;margin-top:12px;flex-wrap:wrap;gap:8px;'>"
               "<span id='codeInfo' style='color:#7f8c8d;'>加载中…</span>"
               "<div><button onclick='codePage(-1)' class='btn btn-sm'>上一页</button> <span id='codePageInfo' style='margin:0 10px;'>第 1/1 页</span> <button onclick='codePage(1)' class='btn btn-sm'>下一页</button></div></div></div>";

    content += "<script>"
               "async function genAICode(){"
               "const amt=parseInt(document.getElementById('aiAmount').value)||200;"
               "const days=parseInt(document.getElementById('aiDays').value)||30;"
               "const r=await fetch('/llm/api/generate-code',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({amount:amt,days:days})});"
               "const j=await r.json();const m=document.getElementById('aiGenMsg');"
               "if(j.code){m.innerHTML='✅ 生成成功：<b>'+j.code+'</b>（'+j.amount+' 算力点，有效期 '+days+' 天）';loadCodes();}"
               "else{m.innerHTML='❌ '+(j.error||'生成失败');}}"
               "let allCodes=[],codePageCur=1,codePageSize=10;"
               "async function loadCodes(){try{const r=await fetch('/llm/api/list-codes');const d=await r.json();allCodes=Array.isArray(d)?d:(d.codes||[]);}catch(e){allCodes=[];}codePageCur=1;renderCodes();}"
               "function renderCodes(){"
               "const tb=document.querySelector('#codeTable tbody');tb.innerHTML='';"
               "const total=allCodes.length;const pages=Math.max(1,Math.ceil(total/codePageSize));"
               "if(codePageCur>pages)codePageCur=pages;"
               "const start=(codePageCur-1)*codePageSize;const page=allCodes.slice(start,start+codePageSize);"
               "page.forEach(c=>{const tr=document.createElement('tr');"
               "tr.innerHTML='<td style=\"font-family:monospace\">'+c.code+'</td><td>'+c.amount+'</td>'"
               "+'<td>'+(c.used==='1'?'<span style=\"color:#95a5a6\">已使用</span>':'<span style=\"color:#27ae60\">未使用</span>')+'</td>'"
               "+'<td>'+(c.usedBy||'-')+'</td><td>'+(c.createdAt||'-')+'</td><td>'+(c.expiresAt||'-')+'</td>';"
               "tb.appendChild(tr);});"
               "if(page.length===0){tb.innerHTML='<tr><td colspan=\"6\" style=\"text-align:center;color:#95a5a6\">暂无兑换码</td></tr>';}"
               "document.getElementById('codeInfo').textContent='共 '+total+' 个兑换码';"
               "document.getElementById('codePageInfo').textContent='第 '+codePageCur+'/'+pages+' 页';}"
               "function codePage(d){const pages=Math.max(1,Math.ceil(allCodes.length/codePageSize));codePageCur+=d;if(codePageCur<1)codePageCur=1;if(codePageCur>pages)codePageCur=pages;renderCodes();}"
               "loadCodes();"
               "</script>";
    return renderTemplate("AI 额度管理", content, user);
}

// ============================================================
// 全站排行榜
// ============================================================
std::string Handlers::handleRank(const std::string& method, const std::string& path,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers,
                                 std::map<std::string, std::string>& respHeaders,
                                 std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string tab = "rating"; // rating | solve
    if (path.find("tab=solve") != std::string::npos) tab = "solve";
    else if (path.find("tab=rating") != std::string::npos) tab = "rating";

    struct Row { std::string id, uname; int rating, maxRating, solve, gold; std::string role, badge; };
    std::vector<Row> rows;
    for (const auto& u : DataStore::instance().users) {
        Row r;
        r.id = u.id; r.uname = u.username; r.rating = u.rating; r.maxRating = u.maxRating;
        r.solve = DataStore::instance().getUserACCount(u.id);
        r.gold = u.gold; r.role = u.role == ROLE_ADMIN ? "admin" : (u.role == ROLE_PROBLEM_MANAGER ? "pm" : "user");
        r.badge = Handlers::getRatingBadge(u.rating);
        rows.push_back(r);
    }
    if (tab == "rating") {
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b){
            if (a.rating != b.rating) return a.rating > b.rating;
            return a.maxRating > b.maxRating;
        });
    } else {
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b){
            if (a.solve != b.solve) return a.solve > b.solve;
            return a.rating > b.rating;
        });
    }

    std::string content = "<div class='prob-tabs' style='border-bottom:none;margin:6px 0 0;'>"
        "<a href='/rank?tab=rating' class='prob-tab" + std::string(tab=="rating"?" active":"") + "'>\u2b50 Rating \u699c</a>"
        "<a href='/rank?tab=solve' class='prob-tab" + std::string(tab=="solve"?" active":"") + "'>\u2705 \u5237\u9898\u699c</a>"
        "</div><div style='color:var(--muted);font-size:12.5px;margin:4px 0 10px;'>\u5171 " + std::to_string(rows.size()) + " \u4f4d\u73a9\u5bb6</div>";
    content += "<table><thead><tr><th style='width:60px'>#</th><th>\u73a9\u5bb6</th><th>Rating</th><th>\u6700\u9ad8 Rating</th><th>AC</th><th>\u91d1\u5e01</th></tr></thead><tbody>";
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        std::string rankCls = (i==0)?"rank-1":((i==1)?"rank-2":((i==2)?"rank-3":""));
        std::string tag = (i==0)?(std::string("\U0001f947")):((i==1)?(std::string("\U0001f948")):((i==2)?(std::string("\U0001f949")):(std::to_string(i+1))));
        content += "<tr><td style='font-size:16px;'>" + tag + "</td>"
            "<td><a href='/user/" + r.id + "' class='user-link' style='font-weight:700;'>" + common::htmlEscape(r.uname) + "</a>"
            + "<span class='role-badge " + r.role + "' style='margin-left:6px;'>" + (r.role=="admin"?"\u7ba1\u7406\u5458":(r.role=="pm"?"\u9898\u76ee\u7ba1":"\u73a9\u5bb6")) + "</span>"
            + "<span style='font-size:11px;background:var(--brand-soft);color:var(--brand);padding:1px 8px;border-radius:10px;margin-left:6px;'>" + r.badge + "</span></td>"
            + "<td style='font-weight:800;color:var(--brand);'>" + std::to_string(r.rating) + "</td>"
            + "<td>" + std::to_string(r.maxRating) + "</td>"
            + "<td style='font-weight:700;color:var(--success);'>" + std::to_string(r.solve) + "</td>"
            + "<td>\U0001fa99" + std::to_string(r.gold) + "</td></tr>";
    }
    if (rows.empty()) content += "<tr><td colspan='6' class='empty-td'>\u6682\u65e0\u73a9\u5bb6</td></tr>";
    content += "</tbody></table>";
    return renderTemplate("\u6392\u884c\u699c", content, user);
}

// ============================================================
// 管理员 - 用户管理
// ============================================================
std::string Handlers::handleAdminUsers(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    int totalGold=0; long long totalExtra=0;
    for (const auto& uu : DataStore::instance().users) {
        totalGold += uu.gold;
        aiquota::Info qi = aiquota::info(uu.id, uu.aiWindowQuota, uu.aiMonthQuota);
        totalExtra += qi.extra;
    }
    std::string q = common::trim(queryParam(path, "q"));
    std::string ql = q;
    for (size_t i = 0; i < ql.size(); i++) ql[i] = (char)tolower((unsigned char)ql[i]);
    int page = common::stringToInt(queryParam(path, "page"));
    if (page < 1) page = 1;

    std::vector<aiplan::Plan> plans = aiplan::load();
    if (plans.empty()) { aiplan::ensureSeed(); plans = aiplan::load(); }

    std::vector<User*> list;
    for (size_t i = 0; i < DataStore::instance().users.size(); i++) {
        User& u = DataStore::instance().users[i];
        if (!q.empty()) {
            std::string name = u.username;
            for (size_t k = 0; k < name.size(); k++) name[k] = (char)tolower((unsigned char)name[k]);
            if (name.find(ql) == std::string::npos && u.id.find(q) == std::string::npos) continue;
        }
        list.push_back(&u);
    }
    int PAGE_SIZE = 30;
    int totalPages = (int)((list.size() + PAGE_SIZE - 1) / PAGE_SIZE);
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    int start = (page - 1) * PAGE_SIZE;
    int end = std::min((int)list.size(), start + PAGE_SIZE);

    std::string content = adminNavbar(user);
    content += "<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;margin-bottom:18px;'>"
              "<div class='stat-box'><div class='stat-number'>" + std::to_string(DataStore::instance().users.size()) + "</div><div class='stat-label'>👥 用户总数</div></div>"
              "<div class='stat-box'><div class='stat-number'>🪙 " + std::to_string(totalGold) + "</div><div class='stat-label'>金币总量</div></div>"
              "<div class='stat-box'><div class='stat-number'>🎁 " + std::to_string(totalExtra) + "</div><div class='stat-label'>额外算力点余额</div></div></div>";
    content += "<form method='GET' action='/admin/users' style='display:flex;gap:8px;align-items:flex-end;margin-bottom:12px;flex-wrap:wrap;'>"
               "<div class='form-group' style='margin:0;flex:1;min-width:200px;'><label>查找用户（用户名 / id）</label>"
               "<input name='q' value='" + common::htmlEscape(q) + "' placeholder='输入用户名或 id'/></div>"
               "<button class='btn btn-sm' type='submit'>搜索</button>"
               "<span style='color:var(--muted);font-size:12px;margin-left:auto;'>共 " + std::to_string(list.size()) + " 人 · 第 " +
               std::to_string(page) + "/" + std::to_string(totalPages) + " 页</span></form>";
    content += "<table><thead><tr><th>ID</th><th>用户名</th><th>角色 / 权限</th><th>AI 套餐</th><th>Rating</th><th>注册时间</th><th>打卡</th><th>AC</th><th>金币</th><th>操作</th></tr></thead><tbody>";
    for (int idx = start; idx < end; idx++) {
        User& u = *list[idx];
        int acCount = DataStore::instance().getUserACCount(u.id);
        std::string roleStr;
        switch(u.role) {
            case ROLE_ADMIN: roleStr = "⭐ 管理员"; break;
            case ROLE_PROBLEM_MANAGER: roleStr = "📚 题目管理员"; break;
            default: roleStr = adminperm::any(&u) ? ("🛡 " + common::htmlEscape(adminperm::summary(&u))) : "用户";
        }
        std::string badge = Handlers::getRatingBadge(u.rating);
        content += "<tr><td>" + u.id + "</td><td><a href='/user/" + u.id + "'>" + common::htmlEscape(u.username) + "</a>"
                  + " <span style='font-size:11px;background:#eee;padding:1px 6px;border-radius:10px;'>" + badge + "</span></td>"
                  + "<td><a href='/admin/permissions?q=" + common::urlEncode(u.username) + "' style='color:inherit;'>" + roleStr + "</a></td>"
                  + "<td><form method='POST' action='/admin/user/plan' style='display:flex;gap:4px;align-items:center;margin:0;'>"
                  + "<input type='hidden' name='userId' value='" + u.id + "'/>"
                  + "<select name='planId' style='width:110px;'>"
                  + "<option value=''" + std::string(u.aiPlanId.empty() ? " selected" : "") + ">默认</option>";
        for (size_t k = 0; k < plans.size(); k++) {
            content += "<option value='" + common::htmlEscape(plans[k].id) + "'" +
                       std::string(plans[k].id == u.aiPlanId ? " selected" : "") + ">" + common::htmlEscape(plans[k].name) + "</option>";
        }
        content += std::string("</select><button class='btn btn-sm' type='submit'>改</button></form></td>")
                  + "<td>" + std::to_string(u.rating) + "</td>"
                  + "<td>" + u.registerTime + "</td><td>" + std::to_string(u.checkinDays) + "天</td><td>" + std::to_string(acCount) + "</td><td>🪙" + std::to_string(u.gold) + "</td><td>";
        if (u.id != user->id) {
            content += "<form method='POST' action='/admin/user/delete' style='display:inline;'>"
                      "<input type='hidden' name='userId' value='" + u.id + "'/>"
                      "<input type='submit' value='删除' class='btn btn-danger btn-sm' onclick='return confirm(\"确定要删除用户吗？\")'/></form>";
        }
        content += "<form method='POST' action='/admin/user/gold' style='display:inline-flex;align-items:center;gap:4px;margin-left:4px;'>"
                   "<input type='hidden' name='username' value='" + common::htmlEscape(u.username) + "'/>"
                   "<input type='number' name='amount' min='-1000000' max='1000000' value='100' title='正数=发金币，负数=扣金币（余额最低为0）' style='width:66px;padding:4px;border:1px solid #ddd;border-radius:4px;'/>"
                   "<input type='hidden' name='note' value=''/>"
                   "<input type='submit' value='金币' class='btn btn-sm btn-outline' onclick='return confirm(\"确认给该用户发放金币？\")'/></form>";
        content += "<form method='POST' action='/admin/user/password' style='display:inline-flex;align-items:center;gap:4px;margin-left:4px;'>"
                   "<input type='hidden' name='userId' value='" + u.id + "'/>"
                   "<input type='password' name='newPassword' placeholder='新密码' minlength='6' autocomplete='new-password' style='width:92px;padding:4px;border:1px solid #ddd;border-radius:4px;'/>"
                   "<input type='submit' value='改密码' class='btn btn-sm btn-outline' onclick='return confirm(\"确认重置该用户的密码？\")'/></form>";
        content += "</td></tr>";
    }
    if (list.empty()) content += "<tr><td colspan='10' class='empty-td'>没有匹配的用户</td></tr>";
    content += "</tbody></table>";
    if (totalPages > 1) {
        content += "<div style='display:flex;gap:6px;justify-content:center;margin-top:12px;flex-wrap:wrap;'>";
        for (int p2 = 1; p2 <= totalPages && p2 <= 30; p2++)
            content += "<a class='btn btn-sm" + std::string(p2 == page ? "" : " btn-outline") + "' href='/admin/users?q=" +
                       common::urlEncode(q) + "&page=" + std::to_string(p2) + "'>" + std::to_string(p2) + "</a>";
        content += "</div>";
    }
    return renderTemplate("用户管理", content, user);
}

// ============================================================
// 管理员 - 重置用户密码
// ============================================================
std::string Handlers::handleAdminUserPassword(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId) {
    (void)method; (void)path; (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!adminperm::has(user, "users")) return "<script>window.location.href='/'</script>";
    std::string uid = common::trim(parseFormData(body, "userId"));
    std::string np = common::trim(parseFormData(body, "newPassword"));
    User* target = DataStore::instance().findUserById(uid);
    if (!target) return "<script>alert('用户不存在');window.location.href='/admin/users'</script>";
    if (np.size() < 6) return "<script>alert('新密码至少 6 个字符');window.location.href='/admin/users'</script>";
    target->password = common::passwordHash(np);
    DataStore::instance().saveUsers();
    return "<script>alert('已重置 " + common::jsonEscape(target->username) +
           " 的密码（加密存储）');window.location.href='/admin/users'</script>";
}

// ============================================================
// 管理员 - 删除用户
// ============================================================
std::string Handlers::handleAdminDeleteUser(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    std::string userId = parseFormData(body, "userId");
    if (userId == user->id) {
        std::string content = "<div class='alert alert-error'>不能删除自己</div><a href='/admin/users' class='btn'>返回</a>";
        return renderTemplate("删除失败", content, user);
    }
    
    User* target = DataStore::instance().findUserById(userId);
    if (!target) {
        std::string content = "<div class='alert alert-error'>用户不存在</div><a href='/admin/users' class='btn'>返回</a>";
        return renderTemplate("删除失败", content, user);
    }
    
    auto& users = DataStore::instance().users;
    for (auto it = users.begin(); it != users.end(); ++it) {
        if (it->id == userId) { users.erase(it); break; }
    }
    DataStore::instance().saveUsers();
    
    std::string content = "<div class='alert alert-success'>用户删除成功！</div><a href='/admin/users' class='btn'>返回</a>";
    return renderTemplate("删除成功", content, user);
}

// ============================================================
// 管理员 - 权限管理
// ============================================================
std::string Handlers::handleAdminPermissions(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    if (method == "GET") {
        std::string content = "<h2>权限管理</h2>"
                             "<p>授予用户题目管理员权限（可管理题目、隐藏/显示题目）</p>"
                             "<form method='POST'>"
                             "<div class='form-group'><label>用户名</label>"
                             "<input name='username' placeholder='输入用户名' required/></div>"
                             "<div class='form-group'><label>权限</label>"
                             "<select name='role'>"
                             "<option value='2'>题目管理员</option>"
                             "<option value='0'>普通用户</option>"
                             "</select></div>"
                             "<input type='submit' value='更新权限' class='btn'/></form>";
        
        content += "<h3 style='margin-top:30px;'>当前权限列表</h3><table>"
                  "<thead><tr><th>用户名</th><th>角色</th></tr></thead><tbody>";
        for (const auto& u : DataStore::instance().users) {
            std::string roleStr;
            switch(u.role) {
                case ROLE_ADMIN: roleStr = "⭐ 管理员"; break;
                case ROLE_PROBLEM_MANAGER: roleStr = "📚 题目管理员"; break;
                default: roleStr = "用户";
            }
            content += "<tr><td>" + common::htmlEscape(u.username) + "</td><td>" + roleStr + "</td></tr>";
        }
        content += "</tbody></table>";
        return renderTemplate("权限管理", content, user);
    } else {
        std::string username = parseFormData(body, "username");
        int role = common::stringToInt(parseFormData(body, "role"));
        
        User* target = DataStore::instance().findUserByName(username);
        if (!target) {
            std::string content = "<div class='alert alert-error'>用户不存在</div><a href='/admin/permissions' class='btn'>返回</a>";
            return renderTemplate("操作失败", content, user);
        }
        if (target->id == user->id) {
            std::string content = "<div class='alert alert-error'>不能修改自己的权限</div><a href='/admin/permissions' class='btn'>返回</a>";
            return renderTemplate("操作失败", content, user);
        }
        
        target->role = (UserRole)role;
        DataStore::instance().saveUsers();
        
        std::string content = "<div class='alert alert-success'>权限更新成功！</div><a href='/admin/permissions' class='btn'>返回</a>";
        return renderTemplate("操作成功", content, user);
    }
}

// ============================================================
// 管理员 - 细粒度权限管理
// ============================================================
std::string Handlers::handleAdminPerms(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!adminperm::has(user, "users")) return "<script>window.location.href='/'</script>";

    std::vector<std::string> perms = adminperm::all();
    if (method == "POST") {
        std::string action = parseFormData(body, "action");
        std::string uid = parseFormData(body, "userId");
        User* t = DataStore::instance().findUserById(uid);
        bool amSuper = (user->role == ROLE_ADMIN);
        if (t) {
            if (action == "savePerms") {
                if (t->role != ROLE_ADMIN || amSuper) {
                    std::string raw = parseFormData(body, "perms");
                    t->adminPerms.clear();
                    auto parts = common::split(raw, ',');
                    for (size_t i = 0; i < parts.size(); i++) {
                        std::string p = common::trim(parts[i]);
                        bool ok = false;
                        for (size_t k = 0; k < perms.size(); k++) if (perms[k] == p) ok = true;
                        if (ok) t->adminPerms.push_back(p);
                    }
                    DataStore::instance().saveUsers();
                }
            } else if (action == "role") {
                int role = common::stringToInt(parseFormData(body, "role"));
                bool escalate = (role == 1 || t->role == ROLE_ADMIN);
                if (t->id != user->id && (!escalate || amSuper)) {
                    t->role = (role == 2) ? ROLE_PROBLEM_MANAGER : (role == 1 ? ROLE_ADMIN : ROLE_USER);
                    DataStore::instance().saveUsers();
                }
            }
        }
        return "<script>window.location.href='/admin/permissions'</script>";
    }

    std::string q = common::trim(queryParam(path, "q"));
    std::string ql = q;
    for (size_t i = 0; i < ql.size(); i++) ql[i] = (char)tolower((unsigned char)ql[i]);

    std::string c;
    c += "<div class='action-bar' style='display:flex;justify-content:flex-end;gap:8px;margin-bottom:10px;'>"
         "<a class='btn btn-sm' href='/admin/users'>用户管理 →</a></div>";
    c += "<div class='alert alert-info'>权限按模块拆分：拥有某模块权限即可进入对应后台页面，<b>ROLE_ADMIN</b> 始终拥有全部权限。"
         "只勾选需要的模块，避免一个账号掌握所有后台能力。</div>";
    c += "<form method='GET' action='/admin/permissions' style='display:flex;gap:8px;align-items:flex-end;margin-bottom:12px;'>"
         "<div class='form-group' style='margin:0;flex:1;max-width:360px;'><label>查找用户（用户名 / id）</label>"
         "<input name='q' value='" + common::htmlEscape(q) + "' placeholder='输入用户名或 id'/></div>"
         "<button class='btn btn-sm' type='submit'>搜索</button></form>";

    c += "<div class='card'><h3>🛡 权限分配</h3><table style='width:100%;font-size:13px;'><thead><tr>"
         "<th style='width:170px;'>用户</th><th style='width:150px;'>角色</th>";
    for (size_t i = 0; i < perms.size(); i++)
        c += "<th style='text-align:center;' title='" + common::htmlEscape(adminperm::desc(perms[i])) + "'>" +
             common::htmlEscape(adminperm::label(perms[i])) + "</th>";
    c += "<th style='width:90px;'>操作</th></tr></thead><tbody>";

    int shown = 0;
    for (size_t i = 0; i < DataStore::instance().users.size(); i++) {
        User& u = DataStore::instance().users[i];
        if (!q.empty()) {
            std::string name = u.username;
            for (size_t k = 0; k < name.size(); k++) name[k] = (char)tolower((unsigned char)name[k]);
            if (name.find(ql) == std::string::npos && u.id.find(q) == std::string::npos) continue;
        }
        if (shown++ >= 80) break;
        bool isSuper = (u.role == ROLE_ADMIN);
        c += "<tr><td><a href='/user/" + u.id + "'>" + common::htmlEscape(u.username) + "</a>"
             "<div style='color:var(--muted);font-size:11.5px;'>#" + u.id + "</div></td>";
        c += "<td><form method='POST' style='margin:0;'>"
             "<input type='hidden' name='action' value='role'/><input type='hidden' name='userId' value='" + u.id + "'/>"
             "<select name='role' style='width:120px;' onchange=\"this.form.querySelector('button').click()\" " +
             std::string(u.id == user->id ? "disabled" : "") + ">"
             "<option value='0'" + std::string(u.role == ROLE_USER ? " selected" : "") + ">普通用户</option>"
             "<option value='2'" + std::string(u.role == ROLE_PROBLEM_MANAGER ? " selected" : "") + ">题目管理员</option>"
             "<option value='1'" + std::string(u.role == ROLE_ADMIN ? " selected" : "") + ">超级管理员</option>"
             "</select><button type='submit' style='display:none;'></button></form></td>";
        // 权限复选框
        c += "<td colspan='" + std::to_string(perms.size()) + "' style='padding:4px 8px;'>"
             "<form method='POST' class='permForm' style='margin:0;'><input type='hidden' name='action' value='savePerms'/>"
             "<input type='hidden' name='userId' value='" + u.id + "'/>"
             "<input type='hidden' name='perms' class='permField'/>"
             "<div style='display:flex;gap:14px;flex-wrap:wrap;'>";
        for (size_t k = 0; k < perms.size(); k++) {
            bool on = isSuper || adminperm::has(&u, perms[k]);
            c += "<label style='display:flex;align-items:center;gap:5px;font-size:12px;' title='" +
                 common::htmlEscape(adminperm::desc(perms[k])) + "'>"
                 "<input type='checkbox' class='permBox' value='" + perms[k] + "'" + (on ? " checked" : "") +
                 std::string(isSuper ? " disabled" : "") + "/>" + common::htmlEscape(adminperm::label(perms[k])) + "</label>";
        }
        c += "</div></form></td><td>";
        if (!isSuper) c += "<button class='btn btn-sm' onclick='saveRow(this)'>保存</button>";
        else c += "<span style='color:var(--muted);font-size:12px;'>全部</span>";
        c += "</td></tr>";
    }
    if (shown == 0) c += "<tr><td colspan='" + std::to_string(perms.size() + 3) + "' class='empty-td'>没有匹配的用户</td></tr>";
    c += "</tbody></table></div>";
    c += "<script>"
         "function saveRow(btn){var tr=btn.closest('tr');var f=tr.querySelector('.permForm');"
         "var arr=[];tr.querySelectorAll('.permBox').forEach(function(x){if(x.checked&&!x.disabled)arr.push(x.value);});"
         "f.querySelector('.permField').value=arr.join(',');f.submit();}"
         "</script>";
    return renderTemplate("权限管理", c, user);
}

// ============================================================
// 管理员 - IP 封禁管理
// ============================================================
std::string Handlers::handleAdminBan(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    return "<script>window.location.href='/admin/system'</script>";
}

// ============================================================
// 管理员 - 公告管理
// ============================================================
std::string Handlers::handleAdminAnnouncement(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";

    if (method == "GET") {
        std::string content = "<form method='POST'>"
                             "<div class='form-group'>"
                             "<label>公告内容（支持HTML标签，如 &lt;br&gt; 换行）</label>"
                             "<textarea name='content' rows='10' style='font-size:14px;'>" 
                             + common::htmlEscape(DataStore::instance().announcement) + "</textarea>"
                             "</div>"
                             "<div class='form-group'>"
                             "<label>预览</label>"
                             "<div style='background:#f8f9fa;padding:15px;border-radius:6px;border:1px solid #ddd;'>"
                             + DataStore::instance().announcement + "</div>"
                             "</div>"
                             "<input type='submit' value='保存公告' class='btn'/> <a href='/admin' class='btn'>取消</a>"
                             "</form>";
        return renderTemplate("编辑公告", content, user);
    } else {
        std::string content = parseFormData(body, "content");
        DataStore::instance().announcement = content;
        DataStore::instance().saveAnnouncement();
        
        std::string result = "<div class='alert alert-success'>公告更新成功！</div>"
                            "<a href='/admin' class='btn'>返回管理面板</a> <a href='/' class='btn'>查看首页</a>";
        return renderTemplate("公告更新成功", result, user);
    }
}

// ============================================================
// 管理员 - 切换题目可见性
// ============================================================
std::string Handlers::handleAdminToggleProblem(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string problemId = parseFormData(body, "problemId");
    Problem* problem = DataStore::instance().findProblemById(problemId);
    if (!problem) {
        std::string content = "<div class='alert alert-error'>题目不存在</div><a href='/admin' class='btn'>返回</a>";
        return renderTemplate("操作失败", content, user);
    }
    if (!canManageProblems(user) && !isProblemOwner(problem, user)) return "<script>window.location.href='/'</script>";
    
    problem->hidden = !problem->hidden;
    DataStore::instance().saveProblems();
    return "<script>window.location.href='/problems'</script>";
}

// ============================================================
// 管理员 - 添加题目
// ============================================================

// ============================================================
// 用户出题（任何登录用户，时间/内存限制设有上限）
// ============================================================

// ============================================================
// 我的题目 - 用户管理自己创建的题目(改/删/显隐)
// ============================================================
std::string Handlers::handleMyProblems(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";

    std::string content = "<div class='action-bar' style='margin-bottom:15px;'>"
        "<a href='/problems/add' class='btn btn-success'>&#9997;&#65039; 出题</a> "
        "<a href='/problems' class='btn'>&#128218; 返回题库</a></div>";

    bool any = false;
    for (const auto& pp : DataStore::instance().problems) {
        if (pp.author != user->username) continue;
        any = true;
        int ac = DataStore::instance().getProblemACCount(pp.id);
        int totalSub = DataStore::instance().getProblemSubmitCount(pp.id);
        content += "<div class='card'>"
            "<div style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>"
            "<strong><a href='/problem/" + pp.id + "'>" + common::htmlEscape(pp.title) + "</a></strong> "
            "<span class='pill-tag'>" + pp.id + "</span> "
            + (pp.hidden ? "<span class='badge badge-pending'>隐藏</span>" : "<span class='badge badge-ac'>公开</span>") +
            "<span style='margin-left:auto;display:flex;gap:8px;'>"
            "<a href='/myproblem/edit/" + pp.id + "' class='btn btn-sm btn-outline'>&#9999;&#65039; 编辑</a>"
            "<form method='POST' action='/myproblem/toggle' style='display:inline;'><input type='hidden' name='problemId' value='" + pp.id + "'/><input type='submit' value='" + (pp.hidden ? "&#128275; 显示" : "&#128274; 隐藏") + "' class='btn btn-sm' style='background:" + (pp.hidden ? "#27ae60" : "#e67e22") + ";'/></form>"
            "<form method='POST' action='/myproblem/delete' style='display:inline;' onsubmit='return confirm(\"确定删除该题目？删除后不可恢复\")'><input type='hidden' name='problemId' value='" + pp.id + "'/><input type='submit' value='&#128465;&#65039; 删除' class='btn btn-sm' style='background:#e74c3c;'/></form>"
            "</span></div>"
            "<div style='margin-top:8px;color:#7f8c8d;font-size:13px;'>AC " + std::to_string(ac) + " / 提交 " + std::to_string(totalSub) + "</div>"
            "</div>";
    }
    if (!any) {
        content += "<div class='empty-state'><p>你还没有创建题目。</p><a href='/problems/add' class='btn btn-success'>&#9997;&#65039; 现在出题</a></div>";
    }
    return renderTemplate("我的题目", content, user);
}


// ============ 1v1 对决 ============
static long long duelNowMs() { return (long long)GetTickCount64(); }

static DuelPlayer* duelPlayer(Duel& d, const std::string& uid) {
    if (d.playerA == uid) return &d.pa;
    if (d.playerB == uid) return &d.pb;
    return NULL;
}
static int duelProblemPoints(Duel& d, const std::string& pid) {
    for (auto& p : d.problems) if (p.problemId == pid) return p.points;
    return 100;
}
static std::string duelPhase(Duel& d) {
    if (d.status == "waiting") return "waiting";
    if (d.status == "finished") return "finished";
    long long now = duelNowMs();
    if (now < d.codingEndMs) return "coding";
    if (now < d.hackEndMs) return "hack";
    return "finished";
}
// 某题的得分 = 该题未被Hack的提交中的最高分（只归零被Hack的那条）
static int duelProblemScore(Duel& d, DuelPlayer& p, const std::string& pid) {
    int best = 0;
    for (auto& s : p.submissions) {
        if (s.problemId == pid && !s.hacked && s.score > best) best = s.score;
    }
    return best;
}
static int duelTotalScore(Duel& d, DuelPlayer& p) {
    int s = 0;
    for (auto& dp : d.problems) s += duelProblemScore(d, p, dp.problemId);
    return s;
}
static void duelFinalizeIfNeeded(Duel& d) {
    if (d.status == "finished") return;
    long long now = duelNowMs();
    if (d.status == "coding" && now >= d.codingEndMs) {
        // 自动锁定未锁定的题
        for (auto& dp : d.problems) {
            if (!d.pa.locked[dp.problemId]) { d.pa.locked[dp.problemId] = true; d.pa.lockedScore[dp.problemId] = d.pa.bestScore[dp.problemId]; d.pa.lockedCode[dp.problemId] = d.pa.bestCode[dp.problemId].empty() ? d.pa.lastCode[dp.problemId] : d.pa.bestCode[dp.problemId]; }
            if (!d.pb.locked[dp.problemId]) { d.pb.locked[dp.problemId] = true; d.pb.lockedScore[dp.problemId] = d.pb.bestScore[dp.problemId]; d.pb.lockedCode[dp.problemId] = d.pb.bestCode[dp.problemId].empty() ? d.pb.lastCode[dp.problemId] : d.pb.bestCode[dp.problemId]; }
        }
        d.status = "hack";
        d.hackEndMs = d.codingEndMs + (long long)(d.hackMinutes > 0 ? d.hackMinutes : 5) * 60 * 1000;
        d.log.push_back("编码阶段结束，进入 Hack 阶段（5分钟）");
    }
    if (d.status == "hack" && now >= d.hackEndMs) {
        int sa = duelTotalScore(d, d.pa), sb = duelTotalScore(d, d.pb);
        d.status = "finished";
        if (sa > sb) d.winner = d.playerA;
        else if (sb > sa) d.winner = d.playerB;
        else if (d.pa.hackCount > d.pb.hackCount) d.winner = d.playerA;
        else if (d.pb.hackCount > d.pa.hackCount) d.winner = d.playerB;
        else d.winner = "";
        d.log.push_back("对决结束，A得分 " + std::to_string(sa) + "，B得分 " + std::to_string(sb)
            + "（Hack A:" + std::to_string(d.pa.hackCount) + " / B:" + std::to_string(d.pb.hackCount) + "）");
        // 金币奖励
        if (!d.winner.empty()) {
            DataStore::instance().addGold(d.winner, 50);
            std::string loser = (d.winner == d.playerA) ? d.playerB : d.playerA;
            DataStore::instance().addGold(loser, 10);
        } else {
            DataStore::instance().addGold(d.playerA, 25);
            DataStore::instance().addGold(d.playerB, 25);
        }
        DataStore::instance().saveUsers();
    }
}

std::string Handlers::handleDuelLobby(const std::string& method, const std::string& path,
 const std::string& body,
 const std::map<std::string, std::string>& headers,
 std::map<std::string, std::string>& respHeaders,
 std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string content = "<div class='alert alert-info'>&#9878;&#65039;1v1 实时对决：邀请对手，限定时间内解题，锁定后进入 Hack 阶段攻击对手题目。</div>";
    // 创建表单
    std::string ps;
    for (const auto& p : DataStore::instance().problems) {
        if (p.hidden) continue;
        ps += "<option value='" + p.id + "=100'>" + common::htmlEscape(p.id + " - " + p.title) + " (100分)</option>";
    }
    content += "<div class='card'><h3>&#10133;创建对决</h3><form method='POST' action='/duel/create'>"
     "<div class='form-group'><label>&#128100;对手用户名</label><input name='opponent' required placeholder='对方用户名'/></div>"
     "<div class='form-group'><label>&#128218;题目（手动：题号=分值,题号=分值；或留空用下方随机）</label><input name='problems' placeholder='P1=100,P2=50'/></div>"
     "<div class='form-group'><label>&#127922;随机选题（道，填0则不随机）</label><input name='randomCount' type='number' value='0' min='0' max='20'/></div>"
     "<div class='form-group'><label>&#9200;编码时长（分钟）</label><input name='minutes' type='number' value='15' min='1' max='120'/></div>"
     "<div class='form-group'><label>&#128293;Hack阶段时长（分钟）</label><input name='hackMinutes' type='number' value='5' min='1' max='30'/></div>"
     "<div style='margin-top:8px;'><input type='submit' value='&#9878;&#65039;发起对决' class='btn'/></div></form></div>";

    // 我的对决：进行中 + 历史
    std::string ongoingList, historyList;
    for (auto& d : DataStore::instance().duels) {
        if (d.playerA != user->id && d.playerB != user->id) continue;
        duelFinalizeIfNeeded(d);
        User* ua = DataStore::instance().findUserById(d.playerA);
        User* ub = DataStore::instance().findUserById(d.playerB);
        std::string opp = (d.playerA == user->id) ? (ub ? ub->username : "?") : (ua ? ua->username : "?");
        std::string st = d.status;
        std::string color = "#9db2ac";
        if (d.status == "coding") { st = "编码中"; color = "#27ae60"; }
        else if (d.status == "hack") { st = "Hack中"; color = "#e67e22"; }
        else if (d.status == "finished") {
            if (d.winner == user->id) { st = "胜利"; color = "#27ae60"; }
            else if (d.winner.empty()) { st = "平局"; color = "#7f8c8d"; }
            else { st = "失败"; color = "#e74c3c"; }
        }
        std::string card = "<div class='card' style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>"
         "<strong><a href='/duel/" + d.id + "'>与 " + common::htmlEscape(opp) + " 的对决</a></strong>"
         "<span class='pill' style='background:" + color + ";'>" + st + "</span>"
         "<span style='margin-left:auto;color:#7f8c8d;font-size:13px;'>" + std::to_string((int)d.problems.size()) + "题</span></div>";
        if (d.status == "finished") historyList += card;
        else ongoingList += card;
    }
    content += "<h3 style='margin-top:20px;'>&#128308;进行中的对决</h3>";
    if (ongoingList.empty()) content += "<div class='empty-td'>暂无进行中的对决</div>";
    else content += ongoingList;
    content += "<h3 style='margin-top:20px;'>&#128337;&#65039;历史对决</h3>";
    if (historyList.empty()) content += "<div class='empty-td'>暂无历史对决</div>";
    else content += historyList;
    return renderTemplate("1v1对决", content, user);
}

std::string Handlers::handleDuelDetail(const std::string& method, const std::string& path,
 const std::string& body,
 const std::map<std::string, std::string>& headers,
 std::map<std::string, std::string>& respHeaders,
 std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    size_t ls = path.find_last_of('/');
    std::string did = path.substr(ls + 1);
    std::string selPid;
    std::string query;
    {
        size_t _q = did.find('?');
        if (_q != std::string::npos) { query = did.substr(_q + 1); did = did.substr(0, _q); }
        size_t pq = query.find("p=");
        if (pq != std::string::npos) selPid = query.substr(pq + 2);
    }
    Duel* d = DataStore::instance().findDuelById(did);
    if (!d) { std::string c = "<div class='alert alert-error'>对决不存在</div><a href='/duel' class='btn'>返回</a>"; return renderTemplate("对决", c, user); }
    Problem* curP = DataStore::instance().findProblemById(selPid);
    if (!curP && !d->problems.empty()) curP = DataStore::instance().findProblemById(d->problems[0].problemId);
    if (d->playerA != user->id && d->playerB != user->id) return "<script>window.location.href='/duel'</script>";
    duelFinalizeIfNeeded(*d);
    DataStore::instance().saveDuels();

    User* ua = DataStore::instance().findUserById(d->playerA);
    User* ub = DataStore::instance().findUserById(d->playerB);
    bool isA = (d->playerA == user->id);
    DuelPlayer& me = isA ? d->pa : d->pb;
    DuelPlayer& foe = isA ? d->pb : d->pa;
    std::string phase = duelPhase(*d);

    std::string pname = (phase == "coding" ? "编码阶段" : (phase == "hack" ? "Hack阶段" : "已结束"));
    std::string pcolor = (phase == "coding" ? "#27ae60" : (phase == "hack" ? "#e67e22" : "#7f8c8d"));
    std::string content = "<div style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:14px;'>"
     "<a href='/duel' class='btn btn-sm btn-outline'>&#11013; 回到对决主页</a>"
     "<h2 style='margin:0;'>&#9878;&#65039; " + common::htmlEscape(ua ? ua->username : "?") + " VS " + common::htmlEscape(ub ? ub->username : "?") + "</h2>"
     "<span class='pill' style='background:" + pcolor + ";'>" + pname + "</span>"
     "<span style='margin-left:auto;'>我 <b>" + std::to_string(duelTotalScore(*d, me)) + "</b> 分 &nbsp; 对手 <b>" + std::to_string(duelTotalScore(*d, foe)) + "</b> 分</span></div>";

    // 阶段倒计时
    if (phase == "coding") content += "<div class='alert alert-info'>编码倒计时：" + std::to_string(std::max(0LL, (d->codingEndMs - duelNowMs()) / 1000)) + " 秒，锁定即冻结该题</div>";
    else if (phase == "hack") content += "<div class='alert alert-warning'>Hack倒计时：" + std::to_string(std::max(0LL, (d->hackEndMs - duelNowMs()) / 1000)) + " 秒，用输入攻击对手已锁定的题目（崩溃/超时/超内存即归零）</div>";

    // 题目选择栏
    content += "<div style='display:flex;gap:6px;flex-wrap:wrap;margin-bottom:12px;'>";
    for (size_t _pi = 0; _pi < d->problems.size(); ++_pi) {
        auto& dp = d->problems[_pi];
        bool active = (curP && dp.problemId == curP->id);
        std::string tag = "P" + std::to_string((int)_pi + 1);
        content += "<a href='/duel/" + d->id + "?p=" + dp.problemId + "' class='btn btn-sm " + (active ? "" : "btn-outline") + "'>&#128218; " + tag + " · " + std::to_string(dp.points) + "分"
         + (me.locked[dp.problemId] ? " &#128274;" : "")
         + (foe.hacked[dp.problemId] ? " &#128293;" : "") + "</a>";
    }
    content += "</div>";

    // 实时排行（双方总分 + 分题）
    content += "<div class='card'><h4>&#127942;实时排行</h4><table><thead><tr><th>玩家</th>";
    for (size_t _pi = 0; _pi < d->problems.size(); ++_pi) content += "<th>P" + std::to_string((int)_pi + 1) + "</th>";
    content += "<th>总分</th><th>Hack</th></tr></thead><tbody>";
    content += "<tr><td>" + common::htmlEscape(ua ? ua->username : "?") + (isA ? "（我）" : "") + "</td>";
    for (auto& dp : d->problems) content += "<td>" + std::to_string(duelProblemScore(*d, me, dp.problemId)) + (duelProblemScore(*d, me, dp.problemId) == 0 && !me.submissions.empty() ? "" : "") + "</td>";
    content += "<td><b>" + std::to_string(duelTotalScore(*d, me)) + "</b></td><td>" + std::to_string(me.hackCount) + "</td></tr>";
    content += "<tr><td>" + common::htmlEscape(ub ? ub->username : "?") + (!isA ? "（我）" : "") + "</td>";
    for (auto& dp : d->problems) content += "<td>" + std::to_string(duelProblemScore(*d, foe, dp.problemId)) + "</td>";
    content += "<td><b>" + std::to_string(duelTotalScore(*d, foe)) + "</b></td><td>" + std::to_string(foe.hackCount) + "</td></tr>";
    content += "</tbody></table></div>";

    // 当前题目工作区（一页一题）
    if (curP) {
        int pts = 100;
        for (auto& dp : d->problems) if (dp.problemId == curP->id) pts = dp.points;
        bool myLocked = me.locked[curP->id];
        std::string stmt = common::htmlEscape(curP->description);
        if (!curP->inputDesc.empty()) stmt += "<div style='margin-top:8px;'><b>输入：</b>" + common::htmlEscape(curP->inputDesc) + "</div>";
        if (!curP->outputDesc.empty()) stmt += "<div style='margin-top:8px;'><b>输出：</b>" + common::htmlEscape(curP->outputDesc) + "</div>";
        content += "<div class='card'><h3>" + common::htmlEscape(curP->title) + " <span class='pill-tag'>" + curP->id + "</span> <span class='pill-tag'>&#11088;" + std::to_string(pts) + "分</span></h3>";
        content += "<div style='font-size:14px;line-height:1.8;color:#333;white-space:pre-wrap;'>" + stmt + "</div>";
        if (phase == "coding" && !myLocked) {
            content += "<form method='POST' action='/duel/action' style='margin-top:14px;'>"
             "<input type='hidden' name='action' value='submit'/><input type='hidden' name='duelId' value='" + d->id + "'/><input type='hidden' name='problemId' value='" + curP->id + "'/>"
             "<div class='form-group'><textarea name='code' rows='10' placeholder='粘贴你的代码'></textarea></div>"
             "<div style='display:flex;gap:8px;'><input type='submit' value='&#128190;提交（可多次，取最高分）' class='btn'/></div></form>";
            if (!me.lastCode[curP->id].empty()) {
                content += "<form method='POST' action='/duel/action' style='margin-top:6px;' onsubmit='return confirm(\"确定锁定该题？锁定后不可再提交或修改\")'>"
                 "<input type='hidden' name='action' value='lock'/><input type='hidden' name='duelId' value='" + d->id + "'/><input type='hidden' name='problemId' value='" + curP->id + "'/>"
                 "<input type='submit' value='&#128274;锁定当前最佳（得分 " + std::to_string(me.bestScore[curP->id]) + "）' class='btn' style='background:#e67e22;'/></form>";
            }
        } else if (myLocked) {
            content += "<div style='margin-top:12px;padding:10px;background:#f8f9fa;border-radius:6px;'>&#128274; 已锁定，得分 " + std::to_string(me.lockedScore[curP->id]) + " 分（不可再提交）</div>";
        }
        content += "</div>";
    }

    // 提交记录 + Hack工作台（可筛选；未锁定对手提交在对决结束前不公开）
    bool duelEnded = (d->status == "finished");
    std::string recScope = (query.find("r=all") != std::string::npos) ? "all" : "cur";
    content += "<div class='card'><h4>&#128221;提交记录 &amp; Hack</h4>"
     "<div style='display:flex;gap:8px;margin-bottom:8px;flex-wrap:wrap;'>"
     "<a href='/duel/" + d->id + "?p=" + (curP?curP->id:"") + "' class='btn btn-sm " + (recScope=="cur"?"":"btn-outline") + "'>当前题</a>"
     "<a href='/duel/" + d->id + "?p=" + (curP?curP->id:"") + "&r=all' class='btn btn-sm " + (recScope=="all"?"":"btn-outline") + "'>全部题目</a>"
     "</div>"
     "<div style='overflow-x:auto;'><table style='width:100%;'><thead><tr><th>题目</th><th>玩家</th><th>记录</th><th>状态 / 操作</th></tr></thead><tbody>";
    bool anyRec = false;
    bool oppAllVisible = (duelEnded || phase == "hack"); // 对手记录：Hack阶段及结束后全公开
    // 我的提交（含我的Hack记录）
    for (auto& s : me.submissions) {
        if (recScope == "cur" && (!curP || s.problemId != curP->id)) continue;
        anyRec = true;
        content += "<tr><td><a href='/duel/" + d->id + "?p=" + s.problemId + "'>" + common::htmlEscape(s.problemId) + "</a></td><td>我</td>";
        if (s.isHack) content += "<td class='sub-rec' style='color:#e74c3c;'>" + common::htmlEscape(s.hackInfo) + "</td><td></td></tr>";
        else content += "<td class='sub-rec'>" + std::to_string(s.pass) + "/" + std::to_string(s.total) + "(" + std::to_string(s.score) + ")</td>"
         "<td>" + (me.locked[s.problemId] ? "<span style='color:#e67e22;'>&#128274;</span>" : "") + "</td></tr>";
    }
    // 对手提交
    for (auto& s : foe.submissions) {
        if (recScope == "cur" && (!curP || s.problemId != curP->id)) continue;
        if (s.isHack) {
            if (!oppAllVisible) continue;
            anyRec = true;
            content += "<tr><td><a href='/duel/" + d->id + "?p=" + s.problemId + "'>" + common::htmlEscape(s.problemId) + "</a></td><td>对手</td>"
             "<td class='sub-rec' style='color:#e74c3c;'>" + common::htmlEscape(s.hackInfo) + "</td><td></td></tr>";
            continue;
        }
        bool visible = oppAllVisible || foe.locked[s.problemId];
        if (!visible) continue;
        anyRec = true;
        std::string actions;
        if (phase == "hack" && !s.hacked) {
            actions = "<details style='display:inline-block;margin-left:8px;'><summary style='cursor:pointer;color:#e74c3c;font-size:12px;'>&#128293;Hack</summary>"
             "<form method='POST' action='/duel/action' style='margin-top:6px;'>"
             "<input type='hidden' name='action' value='hack'/><input type='hidden' name='duelId' value='" + d->id + "'/><input type='hidden' name='problemId' value='" + s.problemId + "'/><input type='hidden' name='submissionId' value='" + s.id + "'/>"
             "<textarea name='input' rows='2' placeholder='攻击数据' style='width:180px;'></textarea><input type='submit' value='攻击' class='btn btn-sm' style='background:#e74c3c;'/></form></details>";
        }
        content += "<tr><td><a href='/duel/" + d->id + "?p=" + s.problemId + "'>" + common::htmlEscape(s.problemId) + "</a></td><td>对手</td>"
         "<td class='sub-rec'>" + (s.hacked ? "<s style='color:#e74c3c;'>" : "") + std::to_string(s.pass) + "/" + std::to_string(s.total) + "(" + std::to_string(s.score) + ")" + (s.hacked ? "</s> &#128293;已Hack" : "") + "</td>"
         "<td>" + (s.hacked ? "<span style='color:#e74c3c;font-size:12px;'>已Hack</span>" : (foe.locked[s.problemId] ? "<span style='color:#e67e22;'>&#128274;</span>" : "<span style='color:#7f8c8d;font-size:12px;'>未锁定</span>")) + actions + "</td></tr>";
    }
    if (!anyRec) content += "<tr><td colspan='4' class='empty-td'>" + std::string("暂无提交记录") + (oppAllVisible ? "" : "（对手未锁定的题目记录在Hack阶段才公开）") + "</td></tr>";
    content += "</tbody></table></div></div>";

    // 结束结果
    if (d->status == "finished") {
        std::string win;
        if (d->winner == user->id) win = "<div class='alert alert-success'>&#127942; 你赢了！(+50金币)</div>";
        else if (d->winner.empty()) win = "<div class='alert alert-info'>&#129309; 平局 (+25金币)</div>";
        else win = "<div class='alert alert-error'>&#128591; 你输了 (+10金币)</div>";
        content += win;
        content += "<div class='card'><h3>&#127942;分题结果</h3><table><thead><tr><th>题目</th><th>分值</th><th>你的得分</th><th>对手得分</th><th>Hack情况</th></tr></thead><tbody>";
        for (auto& dp : d->problems) {
            content += "<tr><td>" + common::htmlEscape(dp.problemId) + "</td><td>" + std::to_string(dp.points) + "</td>"
             + "<td>" + std::to_string(me.lockedScore[dp.problemId]) + "</td>"
             + "<td>" + std::to_string(foe.lockedScore[dp.problemId]) + "</td><td>";
            if (me.hacked[dp.problemId]) content += "<span style='color:#e74c3c;'>我的被Hack</span>";
            else if (foe.hacked[dp.problemId]) content += "<span style='color:#27ae60;'>Hack对手成功</span>";
            else content += "<span style='color:#7f8c8d;'>正常</span>";
            content += "</td></tr>";
        }
        content += "</tbody></table></div>";
        content += "<a href='/duel' class='btn'>返回大厅</a>";
    }
    // 日志
    content += "<div class='card'><h4>&#128161;活动记录</h4><div style='font-size:13px;color:#555;'>";
    for (const auto& lg : d->log) content += "<div>" + common::htmlEscape(lg) + "</div>";
    content += "</div></div>";
    return renderTemplate("对决", content, user);
}

std::string Handlers::handleDuelAction(const std::string& method, const std::string& path,
 const std::string& body,
 const std::map<std::string, std::string>& headers,
 std::map<std::string, std::string>& respHeaders,
 std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";

    if (path.find("/duel/create") != std::string::npos) {
        std::string opponentName = parseFormData(body, "opponent");
        std::string problemsStr = parseFormData(body, "problems");
        int randomCount = common::stringToInt(parseFormData(body, "randomCount"));
        int minutes = common::stringToInt(parseFormData(body, "minutes"));
        if (minutes < 1) minutes = 15;
        User* foe = DataStore::instance().findUserByName(opponentName);
        std::string c;
        if (!foe || foe->id == user->id) { c = "<div class='alert alert-error'>对手不存在或不能是自己</div><a href='/duel' class='btn'>返回</a>"; return renderTemplate("创建对决", c, user); }
        std::vector<DuelProblem> dps;
        if (randomCount > 0) {
            // 随机选题
            std::vector<Problem*> pool;
            for (auto& p : DataStore::instance().problems) if (!p.hidden) pool.push_back(&p);
            static bool seeded = false;
            if (!seeded) { srand((unsigned)time(NULL) ^ (unsigned)GetTickCount()); seeded = true; }
            for (int i = (int)pool.size() - 1; i > 0; --i) { int j = rand() % (i + 1); std::swap(pool[i], pool[j]); }
            if (randomCount > (int)pool.size()) randomCount = (int)pool.size();
            for (int i = 0; i < randomCount; ++i) { DuelProblem dp; dp.problemId = pool[i]->id; dp.points = 100; dps.push_back(dp); }
        } else {
            auto ids = common::split(problemsStr, ',');
            for (auto& t : ids) {
                std::string tok = common::trim(t); int pts = 100; size_t eq = tok.find('=');
                if (eq != std::string::npos) { pts = common::stringToInt(tok.substr(eq + 1)); if (pts < 1) pts = 1; tok = tok.substr(0, eq); }
                tok = common::trim(tok);
                if (!tok.empty() && DataStore::instance().findProblemById(tok)) { DuelProblem dp; dp.problemId = tok; dp.points = pts; dps.push_back(dp); }
            }
        }
        if (dps.empty()) { c = "<div class='alert alert-error'>没有有效题目</div><a href='/duel' class='btn'>返回</a>"; return renderTemplate("创建对决", c, user); }
        Duel d;
        d.id = "d" + common::genId();
        while (DataStore::instance().findDuelById(d.id)) d.id = "d" + common::genId();
        d.status = "coding";
        d.mode = "1v1";
        d.playerA = user->id; d.playerB = foe->id;
        d.problems = dps;
        d.codingEndMs = duelNowMs() + (long long)minutes * 60000;
        int hMin = common::stringToInt(parseFormData(body, "hackMinutes"));
        if (hMin < 1) hMin = 5;
        d.hackMinutes = hMin;
        d.createTimeMs = duelNowMs();
        d.pa.userId = d.playerA; d.pb.userId = d.playerB;
        d.log.push_back("对决开始，编码 " + std::to_string(minutes) + " 分钟，Hack " + std::to_string(hMin) + " 分钟");
        DataStore::instance().duels.push_back(d);
        DataStore::instance().saveDuels();
        return "<script>window.location.href='/duel/" + d.id + "'</script>";
    }

    // /duel/action
    std::string action = parseFormData(body, "action");
    std::string duelId = parseFormData(body, "duelId");
    Duel* d = DataStore::instance().findDuelById(duelId);
    if (!d) return "<script>window.location.href='/duel'</script>";
    duelFinalizeIfNeeded(*d);
    DataStore::instance().saveDuels();

    auto redirect = [&](const std::string& msg) {
        std::string c = "<div class='alert alert-info'>" + msg + "</div><a href='/duel/" + d->id + "' class='btn'>返回对决</a>";
        return renderTemplate("对决操作", c, user);
    };

    if (action == "submit" || action == "lock") {
        std::string pid = parseFormData(body, "problemId");
        std::string code = parseFormData(body, "code");
        if (duelPhase(*d) != "coding") return redirect("当前不是编码阶段");
        DuelPlayer* p = duelPlayer(*d, user->id);
        if (!p) return "<script>window.location.href='/duel'</script>";
        if (p->locked[pid]) return redirect("该题已锁定，无法再提交或修改");
        if (action == "lock") {
            // 锁定：冻结当前最佳得分对应的代码（无得分时冻结最后一次提交），与自动锁定一致
            if (p->bestCode[pid].empty() && p->lastCode[pid].empty()) return redirect("请先提交代码再锁定");
            p->locked[pid] = true;
            p->lockedScore[pid] = p->bestScore[pid];
            p->lockedCode[pid] = p->bestCode[pid].empty() ? p->lastCode[pid] : p->bestCode[pid];
            d->log.push_back(std::string(d->playerA == user->id ? "A" : "B") + " 锁定了 " + pid + "（" + std::to_string(p->bestScore[pid]) + "分）");
            DataStore::instance().saveDuels();
            return redirect("已锁定该题，得分 " + std::to_string(p->bestScore[pid]) + "，不可再修改");
        }
        // 提交：评测并更新最佳
        Problem* prob = DataStore::instance().findProblemById(pid);
        int passed = 0, total = 0; std::string detail;
        if (!Judge::duelScore(d->language, code, prob, passed, total, detail)) return redirect(common::htmlEscape(detail));
        int points = duelProblemPoints(*d, pid);
        int sc = (total > 0) ? (int)((long long)points * passed / total) : 0;
        if (sc > p->bestScore[pid]) { p->bestScore[pid] = sc; p->bestPass[pid] = passed; p->totalCases[pid] = total; p->bestCode[pid] = code; }
        p->lastCode[pid] = code;
        p->submitLog[pid].push_back(std::to_string(passed) + "/" + std::to_string(total) + "(" + std::to_string(sc) + ")");
        DuelSubmission ds;
        ds.id = "s" + common::genId();
        ds.problemId = pid; ds.score = sc; ds.pass = passed; ds.total = total;
        ds.code = code; ds.time = common::getTimeStr();
        p->submissions.push_back(ds);
        d->log.push_back(std::string(d->playerA == user->id ? "A" : "B") + " 提交了 " + pid + "（" + std::to_string(sc) + "分）");
        DataStore::instance().saveDuels();
        return redirect(std::string("提交成功，当前得分 ") + std::to_string(sc));
    }
    else if (action == "hack") {
        std::string pid = parseFormData(body, "problemId");
        std::string subId = parseFormData(body, "submissionId");
        std::string input = parseFormData(body, "input");
        if (duelPhase(*d) != "hack") return redirect("当前不是Hack阶段");
        DuelPlayer* p = duelPlayer(*d, user->id);
        DuelPlayer* tgt = duelPlayer(*d, (d->playerA == user->id) ? d->playerB : d->playerA);
        if (!p || !tgt) return "<script>window.location.href='/duel'</script>";
        if (tgt->hacked[pid]) return redirect("该题已被Hack归零");
        // 定位攻击目标：默认锁定代码，也可攻击对手任意一条提交记录
        std::string targetCode;
        DuelSubmission* tgtSub = NULL;
        if (!subId.empty()) {
            for (auto& s : tgt->submissions) {
                if (s.id == subId && !s.isHack && !s.code.empty()) { targetCode = s.code; tgtSub = &s; break; }
            }
            if (targetCode.empty()) return redirect("未找到该提交记录");
        } else {
            if (!tgt->locked[pid] || tgt->lockedCode[pid].empty()) return redirect("该题不可Hack（未锁定或无代码）");
            targetCode = tgt->lockedCode[pid];
        }
        // Hack数据正确性校验：用标程跑攻击输入，标程无法正常处理则该输入不合法
        Problem* _hp = DataStore::instance().findProblemById(pid);
        if (_hp && !_hp->standardSolution.empty()) {
            std::string _so, _sv; int _st = 0, _sm = 0; std::string _se;
            if (!Judge::customRun("cpp", _hp->standardSolution, input, 2000, 256, _so, _sv, _st, _sm, _se) || _sv != "AC") {
                d->log.push_back(std::string(d->playerA == user->id ? "A" : "B") + " 尝试 Hack " + pid + " 但攻击数据不合法（标程无法正常处理该输入）");
                DataStore::instance().saveDuels();
                return redirect("Hack失败：攻击数据不合法（标程无法正常处理该输入）");
            }
        }
        std::string output, verdict; int ut = 0, um = 0; std::string em;
        if (!Judge::customRun(d->language, targetCode, input, 2000, 256, output, verdict, ut, um, em)) return redirect(std::string("Hack失败：") + common::htmlEscape(em));
        // 记录Hack为一条提交记录
        DuelSubmission h;
        h.id = "h" + common::genId();
        h.problemId = pid; h.isHack = true; h.time = common::getTimeStr();
        if (verdict != "AC") {
            if (tgtSub) { tgtSub->hacked = true; }
            else { tgt->hacked[pid] = true; }
            p->hackCount++;
            h.hackInfo = "&#128293;Hack成功：" + pid + "（" + verdict + "，用时" + std::to_string(ut) + "ms，内存" + std::to_string(um) + "MB）";
            p->submissions.push_back(h);
            d->log.push_back(std::string(d->playerA == user->id ? "A" : "B") + " Hack 成功！" + pid + " 归零（" + verdict + "，用时" + std::to_string(ut) + "ms，内存" + std::to_string(um) + "MB）");
            DataStore::instance().saveDuels();
            return redirect(std::string("&#128293;Hack成功！") + pid + " 已归零");
        } else {
            h.hackInfo = "Hack失败：" + pid + "（程序存活，用时" + std::to_string(ut) + "ms）";
            p->submissions.push_back(h);
            d->log.push_back(std::string(d->playerA == user->id ? "A" : "B") + " Hack 失败（程序存活，用时" + std::to_string(ut) + "ms）");
            DataStore::instance().saveDuels();
            return redirect("Hack失败：程序正常运行");
        }
    }
    return "<script>window.location.href='/duel/" + d->id + "'</script>";
}

// ============================================================
// 提交Hack（全局：有标程的题，任意提交可被Hack，成功则该题对目标用户归零）
// ============================================================
std::string Handlers::handleSubmissionHack(const std::string& method, const std::string& path,
 const std::string& body,
 const std::map<std::string, std::string>& headers,
 std::map<std::string, std::string>& respHeaders,
 std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    security::SecConfig _sec = security::getSecConfig();
    if (_sec.hackIntervalSec > 0 && security::throttleAction("hack:" + user->id, _sec.hackIntervalSec)) {
        std::string c = "<div class='alert alert-warning'>Hack过于频繁，请 " + std::to_string(_sec.hackIntervalSec) + " 秒后再试</div><a href='/status' class='btn'>返回</a>";
        return renderTemplate("Hack", c, user);
    }
    std::string subId, input, expected;
    std::string ct = headers.count("content-type") ? headers.at("content-type") : "";
    if (ct.find("multipart/") != std::string::npos) {
        std::map<std::string, std::string> fields;
        std::vector<UploadedFile> files;
        parseMultipartMulti(body, extractBoundary(headers), fields, files);
        subId = fields["submissionId"]; input = fields["input"]; expected = fields["expected"];
        for (auto& f : files) {
            if (f.name == "inputFile" && !f.data.empty()) input = f.data;
            else if (f.name == "outputFile" && !f.data.empty()) expected = f.data;
        }
    } else {
        subId = parseFormData(body, "submissionId");
        input = parseFormData(body, "input");
        expected = parseFormData(body, "expected");
    }
    Submission* sub = DataStore::instance().findSubmissionById(subId);
    if (!sub) { std::string c = "<div class='alert alert-error'>提交不存在</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack", c, user); }
    if (sub->userId == user->id) { std::string c = "<div class='alert alert-error'>不能Hack自己的提交</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack", c, user); }
    if (sub->hacked) { std::string c = "<div class='alert alert-error'>该提交已被Hack</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack", c, user); }
    if (sub->status != AC) { std::string c = "<div class='alert alert-error'>只有AC的提交才能被Hack</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack", c, user); }
    if (!sub->contestId.empty() && !sub->locked) { std::string c = "<div class='alert alert-error'>比赛提交需先锁定题目才能被Hack</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack", c, user); }
    Problem* p = DataStore::instance().findProblemById(sub->problemId);
    if (!p || p->standardSolution.empty()) { std::string c = "<div class='alert alert-error'>该题没有标程，无法Hack</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack", c, user); }
    if (common::trim(input).empty()) { std::string c = "<div class='alert alert-error'>请输入攻击数据</div><a href='/submission/hack/" + sub->id + "' class='btn'>返回Hack工作台</a>"; return renderTemplate("Hack", c, user); }
    if (common::trim(expected).empty()) { std::string c = "<div class='alert alert-error'>请输入期望输出（标准输出）</div><a href='/submission/hack/" + sub->id + "' class='btn'>返回Hack工作台</a>"; return renderTemplate("Hack", c, user); }
    // 标程先验证攻击数据 + 得到标准输出
    std::string so, sv; int st = 0, sm = 0; std::string se;
    if (!Judge::customRun("cpp", p->standardSolution, input, 2000, 256, so, sv, st, sm, se)) {
        std::string c = "<div class='alert alert-error'>标程评测失败：" + common::htmlEscape(se) + "</div><a href='/submission/hack/" + sub->id + "' class='btn'>返回Hack工作台</a>";
        return renderTemplate("Hack", c, user);
    }
    if (sv != "AC") { std::string c = "<div class='alert alert-warning'>攻击数据不合法（标程无法正常处理该输入：" + common::htmlEscape(sv) + "）</div><a href='/submission/hack/" + sub->id + "' class='btn'>返回Hack工作台</a>"; return renderTemplate("Hack", c, user); }
    std::string correctOut = common::trim(so);
    if (!common::trim(expected).empty() && common::trim(expected) != correctOut) {
        std::string c = "<div class='alert alert-warning'>Hack失败：标程都无法返回你期望的输出（期望输出与标程不一致），本次攻击不成立</div><a href='/submission/hack/" + sub->id + "' class='btn'>返回Hack工作台</a>";
        return renderTemplate("Hack", c, user);
    }
    // Hack数据通过标程检测 -> 加入正式测试数据
    {
        std::string pdir = "./problems/" + p->id + "/";
        std::string hackName = "hack_" + common::genId();
        common::writeFile(pdir + hackName + ".in", input);
        common::writeFile(pdir + hackName + ".out", correctOut + "\n");
    }
    // 管理员开关：重测所有AC提交
        // 管理员开关：重测所有AC提交（入评测池并行）
    if (security::getSecConfig().hackRetestEnabled) {
        int queued = 0;
        for (auto& s2 : DataStore::instance().submissions) {
            if (s2.problemId == p->id && s2.status == AC && !s2.hacked) { Judge::enqueue(s2.id); queued++; }
        }
        if (queued > 0) {
            Judge::waitIdle();
            Judge::recomputeAC(p->id);
        }
    }
    // 运行目标代码
    std::string output, verdict; int ut = 0, um = 0; std::string em;
    if (!Judge::customRun(sub->language, sub->code, input, 2000, 256, output, verdict, ut, um, em)) {
        std::string c = "<div class='alert alert-error'>评测失败：" + common::htmlEscape(em) + "</div><a href='/submission/hack/" + sub->id + "' class='btn'>返回Hack工作台</a>";
        return renderTemplate("Hack", c, user);
    }
    // 判定：崩溃/超时/超内存 或 输出与标准输出不一致 -> Hack成功
    bool hackSuccess = false; std::string reason;
    if (verdict != "AC") { hackSuccess = true; reason = verdict + "（程序崩溃/超时/超内存）"; }
    else if (common::trim(output) != correctOut) { hackSuccess = true; reason = "答案错误（输出与标程不一致）"; }
    if (!hackSuccess) {
        std::string c = "<div class='alert alert-info'>Hack失败：程序正常运行且输出与标程一致（" + std::to_string(ut) + "ms）</div><a href='/submission/hack/" + sub->id + "' class='btn'>返回Hack工作台</a>";
        return renderTemplate("Hack", c, user);
    }
    // Hack成功：该题对该用户全部归零
    int removed = 0;
    for (auto& s : DataStore::instance().submissions) {
        if (s.userId == sub->userId && s.problemId == sub->problemId && s.status == AC && !s.hacked) removed++;
    }
    if (p) p->acCount = (p->acCount > removed) ? (p->acCount - removed) : 0;
    for (auto& s : DataStore::instance().submissions) {
        if (s.userId == sub->userId && s.problemId == sub->problemId) {
            s.hacked = true; s.hackedBy = user->username; s.hackInput = input; s.hackTime = common::getTimeStr();
        }
    }
    DataStore::instance().saveSubmissions();
    DataStore::instance().saveProblems();
    // 奖励Hacker +30金币
    DataStore::instance().addGold(user->id, 30);
    User* tu = DataStore::instance().findUserById(sub->userId);
    std::string c = "<div class='alert alert-success'>&#128293; Hack成功！奖励 <b>+30金币</b>！</div>"
     "<p>题目 <b>" + common::htmlEscape(sub->problemId) + "</b> 对 <b>" + common::htmlEscape(tu ? tu->username : "?") + "</b> 的得分已<b>归零</b>（" + common::htmlEscape(reason) + "，用时" + std::to_string(ut) + "ms）</p>"
     "<div style='background:#f8f9fa;border-radius:6px;padding:10px;margin-bottom:12px;'><b>你的攻击数据：</b><pre style='white-space:pre-wrap;margin:6px 0 0;'>" + common::htmlEscape(input) + "</pre></div>"
     "<a href='/status' class='btn'>返回提交记录</a> <a href='/submission/" + sub->id + "' class='btn'>查看提交</a>";
    return renderTemplate("Hack结果", c, user);
}

// ============================================================
// Hack工作台（独立页面，从提交详情进入）
// ============================================================
std::string Handlers::handleSubmissionHackPage(const std::string& method, const std::string& path,
 const std::string& body,
 const std::map<std::string, std::string>& headers,
 std::map<std::string, std::string>& respHeaders,
 std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string subId = path.substr(path.find_last_of('/') + 1);
    Submission* sub = DataStore::instance().findSubmissionById(subId);
    if (!sub) { std::string c = "<div class='alert alert-error'>提交不存在</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack工作台", c, user); }
    if (sub->hacked) { std::string c = "<div class='alert alert-error'>该提交已被Hack，本题已归零</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack工作台", c, user); }
    if (sub->status != AC) { std::string c = "<div class='alert alert-error'>只有AC的提交才能被Hack</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack工作台", c, user); }
    if (!sub->contestId.empty() && !sub->locked) { std::string c = "<div class='alert alert-error'>比赛提交需先锁定题目才能被Hack</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack工作台", c, user); }
    Problem* p = DataStore::instance().findProblemById(sub->problemId);
    if (!p || p->standardSolution.empty()) { std::string c = "<div class='alert alert-error'>该题没有标程，无法Hack</div><a href='/status' class='btn'>返回</a>"; return renderTemplate("Hack工作台", c, user); }
    User* au = DataStore::instance().findUserById(sub->userId);
    bool auCodePublic = au ? au->codePublic : true;
    std::string codeBlock = auCodePublic
        ? "<div style='background:#1e1e1e;color:#d4d4d4;padding:10px;border-radius:6px;max-height:220px;overflow:auto;font-size:12px;'><pre style='margin:0;white-space:pre-wrap;'>" + common::htmlEscape(sub->code) + "</pre></div>"
        : "<div class='alert alert-info'>🔒 该用户未公开提交代码，此处不显示源码。你仍可以提交攻击数据来测试它的行为。</div>";
    std::string content = "<div class='card'><h2>&#128293; Hack 工作台</h2>"
     "<p style='font-size:13.5px;'>目标提交 <a href='/submission/" + sub->id + "'>" + sub->id.substr(0, 8) + "</a> &nbsp;|&nbsp; 提交者 <a href='/user/" + sub->userId + "'>" + common::htmlEscape(au ? au->username : "?") + "</a> &nbsp;|&nbsp; 题目 <a href='/problem/" + p->id + "'>" + common::htmlEscape(p->title) + "</a></p>"
     + codeBlock +
     "<div style='margin-top:10px;' class='alert alert-info'>&#128161; 攻击数据必须能被<b>标程</b>正常处理；Hack成功条件：目标程序<b>崩溃/超时/超内存</b>，或<b>输出与标程不一致</b>。</div>"
     "<form method='POST' action='/submission/hack' enctype='multipart/form-data' onsubmit='var b=this.querySelector(\"input[type=submit]\"); b.disabled=true; b.value=\"评测中，请稍候...\";'>"
     "<input type='hidden' name='submissionId' value='" + sub->id + "'/>"
     "<div class='form-group'><label>&#128225; 标准输入（攻击数据）</label><textarea name='input' rows='6' placeholder='在此输入攻击数据'></textarea></div>"
     "<div class='form-group'><label>&#128196; 或上传输入文件</label><input type='file' name='inputFile'/></div>"
     "<div class='form-group'><label>&#9989; 标准输出（必填，用于判定）</label><textarea name='expected' rows='3' placeholder='期望输出（必填）' required></textarea></div>"
     "<div class='form-group'><label>&#128196; 或上传输出文件</label><input type='file' name='outputFile'/></div>"
     "<div style='display:flex;gap:8px;'><input type='submit' value='&#128293; 发起Hack' class='btn' style='background:#e74c3c;'/> <a href='/status' class='btn'>返回提交记录</a></div>"
     "</form></div>";
    return renderTemplate("Hack工作台", content, user);
}

// ============================================================
// 比赛锁题（锁题后不可再提交，且公开可被Hack）
// ============================================================
std::string Handlers::handleContestLock(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string contestId = parseFormData(body, "contestId");
    std::string problemId = parseFormData(body, "problemId");
    Contest* c = DataStore::instance().findContestById(contestId);
    if (!c) { std::string cc = "<div class='alert alert-error'>比赛不存在</div><a href='/contests' class='btn'>返回</a>"; return renderTemplate("锁定失败", cc, user); }
    bool hasLocked = false;
    Submission* best = NULL;
    for (auto& s : DataStore::instance().submissions) {
        if (s.userId == user->id && s.problemId == problemId && s.contestId == contestId) {
            if (s.locked) hasLocked = true;
            if (s.status == AC && (!best || s.usedTime < best->usedTime)) best = &s;
        }
    }
    if (hasLocked) { std::string cc = "<div class='alert alert-error'>该题已锁定</div><a href='/contest/" + contestId + "' class='btn'>返回</a>"; return renderTemplate("锁定失败", cc, user); }
    if (!best) { std::string cc = "<div class='alert alert-error'>该题还没有AC提交，无法锁定</div><a href='/contest/" + contestId + "' class='btn'>返回</a>"; return renderTemplate("锁定失败", cc, user); }
    best->locked = true;
    DataStore::instance().saveSubmissions();
    std::string cc = "<div class='alert alert-success'>&#128274; 已锁定该题！之后不可再提交，且你的提交将公开，他人可查看并Hack。</div><a href='/contest/" + contestId + "' class='btn'>返回比赛</a>";
    return renderTemplate("锁定成功", cc, user);
}

// ============================================================
// 管理员-题目重测（可设分数区间，只重测该区间内分数的提交）
// ============================================================
std::string Handlers::handleAdminRejudge(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    if (method == "POST") {
        std::string problemId = common::trim(parseFormData(body, "problemId"));
        int mn = common::stringToInt(parseFormData(body, "minScore"));
        int mx = common::stringToInt(parseFormData(body, "maxScore"));
        std::string st = common::trim(parseFormData(body, "startTime"));
        std::string et = common::trim(parseFormData(body, "endTime"));
        Problem* p = DataStore::instance().findProblemById(problemId);
        if (!p) { std::string c = "<div class='alert alert-error'>题目不存在</div><a href='/admin/rejudge' class='btn'>返回</a>"; return renderTemplate("重测", c, user); }
        int total = 0, hackedTotal = 0;
        std::map<std::string, std::string> _before;
        for (auto& s : DataStore::instance().submissions) {
            if (s.problemId != problemId) continue;
            if (mn > 0 && s.score < mn) continue;
            if (mx > 0 && s.score > mx) continue;
            if (!st.empty() && s.submitTime < st) continue;
            if (!et.empty() && s.submitTime > et) continue;
            total++;
            if (s.hacked) hackedTotal++;
            _before[s.id] = std::to_string((int)s.status) + ":" + std::to_string(s.score);
            Judge::enqueue(s.id);
        }
        if (total > 0) {
            Judge::waitIdle();          // 等评测池并行评测完成
            Judge::recomputeAC(problemId); // 重算题目AC数
        }
        int changed = 0, hackedRecovered = 0;
        for (auto& s : DataStore::instance().submissions) {
            if (s.problemId != problemId) continue;
            auto _it = _before.find(s.id);
            if (_it != _before.end() && _it->second != (std::to_string((int)s.status) + ":" + std::to_string(s.score))) changed++;
            if (s.hacked && s.status == AC) { s.hacked = false; s.hackedBy.clear(); s.hackInput.clear(); s.hackTime.clear(); hackedRecovered++; }
        }
        DataStore::instance().saveSubmissions();
        int ac = p->acCount;
        std::string c = "<div class='alert alert-success'>&#9989; 重测完成（评测池并行评测）</div>"
            "<div style='margin:12px0;'>加入评测队列 <b>" + std::to_string(total) + "</b> 条，结果变化 <b>" + std::to_string(changed) + "</b> 条。</div>"
            "<div style='color:#7f8c8d;font-size:13px;'>其中被Hack的重测 <b>" + std::to_string(hackedTotal) + "</b> 条，恢复AC <b>" + std::to_string(hackedRecovered) + "</b> 条。</div>"
            "<div style='color:#7f8c8d;font-size:13px;'>题目AC数已重新计算：<b>" + std::to_string(ac) + "</b></div>"
            "<div style='margin-top:16px;'><a href='/admin/rejudge' class='btn'>继续重测</a> <a href='/problem/" + problemId + "' class='btn'>查看题目</a></div>";
    }
    // GET：表单
    std::string options;
    for (auto& p : DataStore::instance().problems) {
        options += "<option value='" + p.id + "'>" + common::htmlEscape(p.id + " - " + p.title) + "</option>";
    }
    std::string c = "<div class='alert alert-info'>&#128260; 重测特定题目的提交。可设<strong>分数区间</strong>（0-100，按通过用例占比计算），只重测该区间内分数的提交；留空则重测该题全部提交。</div>"
        "<form method='POST' action='/admin/rejudge' class='card'>"
        "<div class='form-group'><label>题目</label><select name='problemId' required>" + options + "</select></div>"
        "<div style='display:flex;gap:10px;'><div class='form-group'><label>最小分数</label><input type='number' name='minScore' value='0' min='0' max='100'/></div>"
        "<div class='form-group'><label>最大分数</label><input type='number' name='maxScore' value='0' min='0' max='100'/></div></div>"
        "<div style='display:flex;gap:10px;'><div class='form-group'><label>开始时间</label><input name='startTime' placeholder='2026-01-01 00:00:00'/></div>"
        "<div class='form-group'><label>结束时间</label><input name='endTime' placeholder='2026-12-31 23:59:59'/></div></div>"
        "<div style='margin-top:8px;'><input type='submit' value='&#128260; 开始重测' class='btn'/></div></form>";
    return renderTemplate("题目重测", c, user);
}

std::string Handlers::handleAdminRejudgeSingle(const std::string& method, const std::string& path,
 const std::string& body,
 const std::map<std::string,std::string>& headers,
 std::map<std::string,std::string>& respHeaders,
 std::string& sessionId) {
 User* user = getCurrentUser(sessionId);
 if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
 std::string subId = parseFormData(body, "submissionId");
 Submission* s = DataStore::instance().findSubmissionById(subId);
 if (!s) {
  std::string c = "<div class='alert alert-error'>提交不存在</div><a href='/status' class='btn'>返回</a>";
  return renderTemplate("重测", c, user);
 }
 Judge::enqueue(s->id);
 Judge::waitIdle();
 Judge::recomputeAC(s->problemId);
 std::string c = "<div class='alert alert-success'>&#128257;该提交已重测完成</div>"
  "<a href='/submission/" + s->id + "' class='btn'>返回提交</a><a href='/status' class='btn'>提交记录</a>";
 return renderTemplate("重测完成", c, user);
}

std::string Handlers::handleUserAddProblem(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    security::SecConfig sec = security::getSecConfig();
    int MAX_TIME = sec.maxUserProblemTime >0 ? sec.maxUserProblemTime : 2000;
    int MAX_MEM  = sec.maxUserProblemMem  >0 ? sec.maxUserProblemMem  : 256;

    if (method == "GET") {
        std::string content = "<div class='alert alert-info'>&#128161; 提示：时间上限 <b>" + std::to_string(MAX_TIME)
                              + " ms</b>、内存上限 <b>" + std::to_string(MAX_MEM) + " MB</b>、题号≤" + std::to_string(sec.maxProblemId)
                              + "、标题≤" + std::to_string(sec.maxProblemTitle) + "、题面≤" + std::to_string(sec.maxProblemDesc)
                              + "、样例≤" + std::to_string(sec.maxProblemSample);
        if (sec.maxUserProblems >0) content += "、出题数≤" + std::to_string(sec.maxUserProblems);
        content += "。</div>";
        content += "<form method='POST'>"
          "<div class='form-group'><label>&#128278; 题号（可留空自动生成）</label><input name='id' maxlength='" + std::to_string(sec.maxProblemId) + "' placeholder='如 P1000'/></div>"
          "<div class='form-group'><label>&#128278; 标题</label><input name='title' required maxlength='" + std::to_string(sec.maxProblemTitle) + "' placeholder='题目标题'/></div>"
          "<div class='form-group'><label>&#128220; 题目描述</label><textarea name='description' rows='8' required maxlength='" + std::to_string(sec.maxProblemDesc) + "'></textarea></div>"
          "<div class='form-group'><label>&#128209; 输入格式</label><textarea name='inputDesc' rows='4'></textarea></div>"
          "<div class='form-group'><label>&#128209; 输出格式</label><textarea name='outputDesc' rows='4'></textarea></div>"
          "<div class='form-group'><label>&#128202; 样例 (输入和输出用 || 分隔，多组用 | 分隔)</label><textarea name='samples' rows='4' maxlength='" + std::to_string(sec.maxProblemSample) + "' placeholder='输入1||输出1|输入2||输出2'></textarea></div>"
          "<div class='form-group'><label>&#9200; 时间限制 (ms，上限 " + std::to_string(MAX_TIME) + ")</label><input name='timeLimit' type='number' value='1000' min='1' max='" + std::to_string(MAX_TIME) + "' required/></div>"
          "<div class='form-group'><label>&#128190; 内存限制 (MB，上限 " + std::to_string(MAX_MEM) + ")</label><input name='memLimit' type='number' value='128' min='1' max='" + std::to_string(MAX_MEM) + "' required/></div>"
          "<div style='display:flex;gap:10px;'><input type='submit' value='&#128226; 发布题目' class='btn'/> <a href='/problems' class='btn'>返回题库</a></div></form>";
        return renderTemplate("出题", content, user);
    } else {
        std::string title = parseFormData(body, "title");
        std::string description = parseFormData(body, "description");
        std::string inputDesc = parseFormData(body, "inputDesc");
        std::string outputDesc = parseFormData(body, "outputDesc");
        std::string samplesStr = parseFormData(body, "samples");
        std::string customId = parseFormData(body, "id");
        int timeLimit = common::stringToInt(parseFormData(body, "timeLimit"));
        int memLimit = common::stringToInt(parseFormData(body, "memLimit"));

        if (title.empty() || description.empty()) {
            std::string c = "<div class='alert alert-error'>标题和描述不能为空</div><a href='/problems/add' class='btn'>返回</a>";
            return renderTemplate("出题失败", c, user);
        }
        if ((int)title.size() > sec.maxProblemTitle || (int)description.size() > sec.maxProblemDesc
            || (int)samplesStr.size() > sec.maxProblemSample) {
            std::string c = "<div class='alert alert-error'>超出长度限制：标题≤" + std::to_string(sec.maxProblemTitle)
                            + "、题面≤" + std::to_string(sec.maxProblemDesc) + "、样例≤" + std::to_string(sec.maxProblemSample) + "</div>"
                            "<a href='/problems/add' class='btn'>返回</a>";
            return renderTemplate("出题失败", c, user);
        }
        if (sec.maxUserProblems >0) {
            int cnt =0;
            for (const auto& p : DataStore::instance().problems)
                if (p.author == user->username) cnt++;
            if (cnt >= sec.maxUserProblems) {
                std::string c = "<div class='alert alert-error'>已达到出题数量上限 " + std::to_string(sec.maxUserProblems) + " 道</div><a href='/problems' class='btn'>返回题库</a>";
                return renderTemplate("出题失败", c, user);
            }
        }
        if (timeLimit <= 0) timeLimit = 1000;
        if (timeLimit > MAX_TIME) timeLimit = MAX_TIME;
        if (memLimit <= 0) memLimit = 128;
        if (memLimit > MAX_MEM) memLimit = MAX_MEM;

        std::string id;
        if (!customId.empty()) {
            bool okId = true;
            if ((int)customId.size() > sec.maxProblemId) okId = false;
            for (char ch : customId) {
                if (!(isalnum(ch) || ch=='_' || ch=='-')) { okId = false; break; }
            }
            if (okId && DataStore::instance().findProblemById(customId)) okId = false;
            if (okId) id = customId;
            else {
                std::string c = "<div class='alert alert-error'>题号非法或已存在（限字母/数字/下划线/横线，≤" + std::to_string(sec.maxProblemId) + "）</div><a href='/problems/add' class='btn'>返回</a>";
                return renderTemplate("出题失败", c, user);
            }
        }
        if (id.empty()) {
            id = "u" + common::genId();
            while (DataStore::instance().findProblemById(id)) id = "u" + common::genId();
        }

        Problem problem;
        problem.id = id;
        problem.title = title;
        problem.description = description;
        problem.inputDesc = inputDesc;
        problem.outputDesc = outputDesc;
        if (!samplesStr.empty()) problem.samples = common::split(samplesStr, '|');
        problem.timeLimit = timeLimit;
        problem.memLimit = memLimit;
        problem.author = user->username;
        problem.createTime = common::getTimeStr();
        problem.acCount = 0;
        problem.submitCount = 0;
        problem.hidden = false;
        problem.image = "";
        DataStore::instance().problems.push_back(problem);
        DataStore::instance().saveProblems();
        std::string problemDir = std::string("./problems/") + id + "/";
        common::createDirectory(problemDir);
        std::string c = "<div class='alert alert-success'>&#9989; 题目发布成功！</div>"
                        "<p>请将测试数据放入: <code>" + problemDir + "</code></p>"
                        "<a href='/problem/" + id + "' class='btn'>&#128279; 查看题目</a> <a href='/problems' class='btn'>返回题库</a>";
        return renderTemplate("出题成功", c, user);
    }
}



// ============================================================
// IDE 模式自测：编辑器 + 题目样例 + 多测试点一次编译
// ============================================================
std::string Handlers::handleIdeTest(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";

    security::SecConfig _sec = security::getSecConfig();
    int MAX_TIME = _sec.maxCustomTestTime > 0 ? _sec.maxCustomTestTime : 2000;
    int MAX_MEM  = _sec.maxCustomTestMem  > 0 ? _sec.maxCustomTestMem  : 256;
    int MAX_CODE = _sec.maxCustomTestCode > 0 ? _sec.maxCustomTestCode : 50000;

    std::string initialPid = queryParam(path, "pid");
    std::string langOptions;
    std::string firstLang;
    for (size_t i = 0; i < DataStore::instance().languages.size(); i++) {
        const LangConfig& l = DataStore::instance().languages[i];
        if (!l.enabled) continue;
        if (firstLang.empty()) firstLang = l.id;
        langOptions += "<option value='" + common::htmlEscape(l.id) + "'>" + common::htmlEscape(l.name) + "</option>";
    }

    std::string c;
    c += "<link rel='stylesheet' href='/static/vendor/codemirror/codemirror.min.css'/>";
    c += "<script src='/static/vendor/codemirror/codemirror.min.js'></script>";
    c += "<script src='/static/vendor/codemirror/mode/clike/clike.min.js'></script>";
    c += "<script src='/static/vendor/codemirror/mode/python/python.min.js'></script>";
    c += "<script src='/static/vendor/codemirror/addon/edit/matchbrackets.min.js'></script>";
    c += "<script src='/static/vendor/codemirror/addon/edit/closebrackets.min.js'></script>";
    c += R"CSS(<style>
.ide-wrap{margin-top:-4px}
.ide-top{display:flex;gap:10px;align-items:center;flex-wrap:wrap;background:var(--card,#fff);border:1px solid var(--border,#e3e9f0);border-radius:14px;padding:10px 12px;margin-bottom:12px}
.ide-top select,.ide-top input{border:1px solid var(--border,#e3e9f0);background:var(--surface,#fff);color:var(--text,#2f3640);border-radius:9px;padding:6px 9px;font-size:13px;height:34px}
.ide-top select#pidSel{min-width:220px;max-width:320px}
.ide-top .grow{flex:1}
.ide-top .mini{width:88px}
.ide-body{display:flex;gap:12px;align-items:stretch;min-height:calc(100vh - 230px)}
.ide-left,.ide-right{background:var(--card,#fff);border:1px solid var(--border,#e3e9f0);border-radius:14px;display:flex;flex-direction:column;min-width:0}
.ide-left{flex:1.35}
.ide-right{flex:1;max-width:520px}
.ide-head{display:flex;align-items:center;gap:8px;padding:10px 12px;border-bottom:1px solid var(--border-soft,#edf1f6);font-weight:700;color:var(--strong,#1c232b);font-size:13.5px}
.ide-head .spacer{flex:1}
.ide-editor{flex:1;min-height:520px}
.CodeMirror{height:100%;font-size:13.5px;font-family:Consolas,"Cascadia Mono",Menlo,monospace;border-radius:0 0 14px 14px}
.cm-s-default .CodeMirror-gutters{background:#f7fafd;border-right:1px solid #eef2f6}
.case-bar{display:flex;gap:6px;padding:8px 10px;border-bottom:1px solid var(--border-soft,#edf1f6);flex-wrap:wrap}
.case-pill{border:1px solid var(--border,#e3e9f0);background:var(--surface-2,#f7fafd);border-radius:8px;padding:3px 10px;font-size:12.5px;cursor:pointer;display:flex;gap:6px;align-items:center}
.case-pill.on{border-color:#3a86c8;color:#3a86c8;background:rgba(58,134,200,.08);font-weight:700}
.case-pill .dot{width:7px;height:7px;border-radius:50%;background:#cbd5e1;display:inline-block}
.case-pill .dot.AC{background:#27ae60}.case-pill .dot.WA{background:#e74c3c}.case-pill .dot.RE,.case-pill .dot.CE{background:#8e44ad}.case-pill .dot.TLE{background:#e67e22}
.case-panel{padding:10px 12px;overflow:auto;flex:1;min-height:300px}
.case-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.case-grid label{font-size:12px;color:var(--muted,#8792a0);margin-bottom:4px;display:block}
.case-grid textarea{width:100%;font-family:Consolas,monospace;font-size:12.5px;border:1px solid var(--border,#e3e9f0);border-radius:9px;padding:8px;resize:vertical;background:var(--surface-2,#f7fafd);color:var(--text,#2f3640);min-height:110px}
.case-io{margin-top:12px;border-top:1px dashed var(--border,#e3e9f0);padding-top:10px}
.case-io h5{margin:0 0 6px;font-size:12.5px;color:var(--strong,#1c232b)}
.case-io pre{margin:0;background:#f7fafd;border:1px solid var(--border-soft,#edf1f6);border-radius:9px;padding:9px;font-size:12.5px;overflow:auto;max-height:180px;white-space:pre-wrap;word-break:break-all}
.verdict{display:inline-flex;align-items:center;gap:6px;border-radius:99px;padding:3px 10px;font-size:12.5px;font-weight:700}
.v-AC{background:rgba(39,174,96,.12);color:#27ae60}.v-WA{background:rgba(231,76,60,.12);color:#e74c3c}
.v-TLE{background:rgba(230,126,34,.12);color:#e67e22}.v-RE,.v-CE{background:rgba(142,68,173,.12);color:#8e44ad}
.v-NA{background:#eef2f6;color:#8792a0}
.drv-cur{font-size:12px;color:var(--muted,#8792a0);max-width:220px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;border:1px dashed var(--border,#e3e9f0);border-radius:8px;padding:3px 8px}
.drv-cur.on{color:#3a86c8;border-style:solid;background:rgba(58,134,200,.06);font-weight:600}
.ide-modal{position:fixed;inset:0;z-index:9999;display:flex;align-items:center;justify-content:center}
.ide-modal-mask{position:absolute;inset:0;background:rgba(12,22,34,.38)}
.ide-modal-box{position:relative;background:var(--card,#fff);border:1px solid var(--border,#e3e9f0);border-radius:16px;box-shadow:0 18px 50px rgba(20,40,70,.22);width:min(620px,92vw);max-height:82vh;display:flex;flex-direction:column;overflow:hidden}
.ide-modal-head{display:flex;align-items:center;gap:8px;padding:12px 14px;border-bottom:1px solid var(--border-soft,#edf1f6);font-weight:700}
.ide-modal-head .grow{flex:1}
.ide-modal-path{padding:6px 14px;font-size:12px;color:var(--muted,#8792a0);font-family:Consolas,monospace;border-bottom:1px dashed var(--border-soft,#edf1f6)}
.ide-modal-list{overflow:auto;padding:6px 8px 12px}
.ide-fl{display:flex;align-items:center;gap:8px;padding:8px 10px;border-radius:9px;cursor:pointer;font-size:13.5px}
.ide-fl:hover{background:var(--hover,#eef4fb)}
.ide-fl .ic{width:20px;text-align:center}
.ide-fl .nm{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.ide-fl .sz{font-size:12px;color:var(--muted,#8792a0)}
.ide-sub input{width:100%;border:1px solid var(--border,#e3e9f0);border-radius:9px;padding:8px 10px;font-size:14px;margin-top:6px}
.ide-sub label{font-size:13px;color:var(--strong,#1c232b)}
@media(max-width:900px){.ide-body{flex-direction:column}.ide-right{max-width:none}}
</style>)CSS";
    c += "<div class='ide-wrap'>";
    c += "<div class='ide-top'>"
         "<select id='pidSel'><option value=''>— 选择题目（可测样例）—</option></select>"
         "<select id='langSel'>" + langOptions + "</select>"
         "<input id='tlInp' class='mini' type='number' min='1' max='" + std::to_string(MAX_TIME) + "' value='2000' title='时间限制 ms'/>"
         "<input id='mlInp' class='mini' type='number' min='1' max='" + std::to_string(MAX_MEM) + "' value='256' title='内存限制 MB'/>"
         "<span class='grow'></span>"
         "<span class='drv-cur' id='drvCur' title='当前云盘文件'>未关联云盘文件</span>"
         "<button class='btn btn-sm btn-outline' id='drvOpen' title='从云盘打开文件'>📂</button>"
         "<button class='btn btn-sm btn-outline' id='drvSave' title='保存到云盘（Ctrl+S）'>💾</button>"
         "<button class='btn btn-sm' id='submitBtn' title='把当前代码提交到题目'>🚀 提交</button>"
         "<button class='btn' id='runBtn'>▶ 运行全部</button>"
         "<span style='color:var(--muted);font-size:12px;'>Ctrl+Enter</span></div>";
    c += "<div class='ide-body'>";
    c += "<div class='ide-left'><div class='ide-head'><span>📝 代码</span><span class='spacer'></span>"
         "<button class='btn btn-sm btn-outline' id='fmtBtn' title='整理缩进（简易）'>整理缩进</button>"
         "<button class='btn btn-sm btn-outline' id='resetBtn'>恢复模板</button></div>"
         "<div class='ide-editor'><textarea id='code'></textarea></div></div>";
    c += "<div class='ide-right'><div class='ide-head'><span>🧪 测试点</span><span class='spacer'></span>"
         "<button class='btn btn-sm' id='addCase'>＋ 自定义</button></div>"
         "<div class='case-bar' id='caseBar'></div><div class='case-panel' id='casePanel'></div></div>";
    c += "</div></div>";

    std::string initJs = "var INIT_PID=" + common::jsonStringify(initialPid) + ";var MAX_CODE=" + std::to_string(MAX_CODE) + ";var FIRST_LANG=" + common::jsonStringify(firstLang) + ";var INIT_FILE=" + common::jsonStringify(queryParam(path, "file")) + ";";
    c += "<script>" + initJs + R"JS(
(function(){
  var cm = CodeMirror.fromTextArea(document.getElementById('code'), {
    lineNumbers:true, mode:'text/x-c++src', indentUnit:4, tabSize:4,
    matchBrackets:true, autoCloseBrackets:true, lineWrapping:false,
    extraKeys:{"Ctrl-Enter":function(){runAll();}, "Ctrl-S":function(){saveToDrive();}, "Tab":function(c){c.replaceSelection('    ');}}
  });
  cm.setSize('100%','100%');
  var cases=[{input:'',expected:''}], cur=0, last=[], busy=false;
  var TEMPLATES={
    cpp:'#include <bits/stdc++.h>\nusing namespace std;\n\nint main(){\n    ios::sync_with_stdio(false);\n    cin.tie(nullptr);\n\n    return 0;\n}\n',
    c:'#include <stdio.h>\n\nint main(){\n    \n    return 0;\n}\n',
    python:'import sys\n\ndef main():\n    data = sys.stdin.read().split()\n    \n\nif __name__ == "__main__":\n    main()\n',
    java:'import java.util.*;\n\npublic class Main {\n    public static void main(String[] args) {\n        Scanner sc = new Scanner(System.in);\n        \n    }\n}\n'
  };
  var CODE_KEY='oi_ide_code_', DRAFT_KEY='oi_ide_cases_';
  function lang(){return document.getElementById('langSel').value;}
  function esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
  function modeOf(l){ if(l==='python') return 'text/x-python'; if(l==='java'||l==='cpp'||l==='c') return 'text/x-c++src'; return 'text/plain'; }
  function codeKey(){ var p=document.getElementById('pidSel').value||'custom'; return CODE_KEY+p+'_'+lang(); }
  function saveCode(){ try{localStorage.setItem(codeKey(),cm.getValue());}catch(e){} }
  function loadCode(){ try{var v=localStorage.getItem(codeKey()); if(v!=null&&v!==''){cm.setValue(v);return;} }catch(e){} var t=TEMPLATES[lang()]; cm.setValue(t||''); }
  cm.on('change',saveCode);

  function renderCases(){
    var bar=document.getElementById('caseBar'); bar.innerHTML='';
    cases.forEach(function(c,i){
      var st=(last[i]&&last[i].status)||'';
      var b=document.createElement('button'); b.className='case-pill'+(i===cur?' on':''); b.type='button';
      b.innerHTML='<span class="dot '+st+'"></span>#'+(i+1)+(st?' '+st:'');
      b.onclick=function(){cur=i;renderAll();};
      bar.appendChild(b);
    });
    var b2=document.createElement('button'); b2.className='case-pill'; b2.type='button'; b2.textContent='＋'; b2.onclick=addCase; bar.appendChild(b2);
  }
  function renderPanel(){
    var c=cases[cur]||{input:'',expected:''}; var r=last[cur];
    var h='<div class="case-grid"><div><label>输入</label><textarea id="cin" placeholder="程序输入">'+esc(c.input)+'</textarea></div>'
        +'<div><label>期望输出（可选）</label><textarea id="cexp" placeholder="填了会自动判断 AC/WA">'+esc(c.expected)+'</textarea></div></div>'
        +'<div style="display:flex;gap:8px;margin-top:8px;align-items:center;">'
        +'<button class="btn btn-sm btn-danger" id="delCase" type="button">删除此测试点</button>'
        +'<span style="color:var(--muted);font-size:12px;">共 '+cases.length+' 个测试点</span></div>';
    if(r){
      h+='<div class="case-io"><h5>本次结果 <span class="verdict v-'+(r.status||'NA')+'">'+(r.status||'-')+'</span>'
        +' <span style="color:var(--muted);font-weight:400;font-size:12px;">'+r.time+'ms · '+r.mem+'MB</span></h5>'
        +'<pre>'+esc(r.output||'(无输出)')+'</pre>';
      if(r.error) h+='<h5 style="margin-top:8px;">错误信息</h5><pre>'+esc(r.error)+'</pre>';
      if(c.expected.trim()!=='') h+='<h5 style="margin-top:8px;">期望输出</h5><pre>'+esc(c.expected)+'</pre>';
      h+='</div>';
    }
    var p=document.getElementById('casePanel'); p.innerHTML=h;
    document.getElementById('cin').oninput=function(){cases[cur].input=this.value;saveCases();};
    document.getElementById('cexp').oninput=function(){cases[cur].expected=this.value;saveCases();};
    var del=document.getElementById('delCase');
    if(del) del.onclick=function(){ if(cases.length<=1){cases=[{input:'',expected:''}];cur=0;} else{cases.splice(cur,1);if(cur>=cases.length)cur=cases.length-1;} last=[];saveCases();renderAll(); };
  }
  function renderAll(){renderCases();renderPanel();}
  function addCase(){cases.push({input:'',expected:''});cur=cases.length-1;last=[];saveCases();renderAll();}
  function saveCases(){try{localStorage.setItem(DRAFT_KEY+(document.getElementById('pidSel').value||'custom'),JSON.stringify(cases));}catch(e){}}
  function loadCases(){try{var v=localStorage.getItem(DRAFT_KEY+(document.getElementById('pidSel').value||'custom'));if(v){var a=JSON.parse(v);if(a&&a.length){cases=a;cur=0;last=[];return true;}}}catch(e){}return false;}

  function statusOf(verdict, output, expected){
    if(expected && expected.trim()!=='') return (output||'').trim()===expected.trim() ? 'AC' : 'WA';
    return verdict||'NA';
  }

  async function runAll(){
    if(busy) return;
    var code=cm.getValue();
    if(!code.trim()){alert('代码是空的');return;}
    if(code.length>MAX_CODE){alert('代码太长');return;}
    busy=true; var btn=document.getElementById('runBtn'); btn.disabled=true; btn.textContent='运行中…';
    last=[];
    try{
      var resp=await fetch('/api/custom-test',{method:'POST',headers:{'Content-Type':'application/json'},
        body:JSON.stringify({language:lang(),code:code,timeLimit:parseInt(document.getElementById('tlInp').value)||2000,
          memLimit:parseInt(document.getElementById('mlInp').value)||256,
          cases:cases.map(function(c){return {input:c.input};})})});
      var j=await resp.json();
      if(j.error){ alert(j.error); }
      else{
        var rs=j.results||[];
        last=cases.map(function(c,i){
          var r=rs[i]||{output:'',verdict:'NA',time:0,mem:0,error:''};
          return {output:r.output,verdict:r.verdict,time:r.time,mem:r.mem,error:r.error,status:statusOf(r.verdict,r.output,c.expected)};
        });
      }
    }catch(e){ alert('请求失败：'+e.message); }
    busy=false; btn.disabled=false; btn.textContent='▶ 运行全部';
    renderAll();
  }
  document.getElementById('runBtn').onclick=runAll;
  document.getElementById('addCase').onclick=addCase;
  document.getElementById('resetBtn').onclick=function(){ cm.setValue(TEMPLATES[lang()]||''); };
  document.getElementById('fmtBtn').onclick=function(){
    // 简易缩进整理：统一 tab -> 4 空格、去掉行尾空白
    var lines=cm.getValue().split('\n').map(function(l){return l.replace(/\t/g,'    ').replace(/\s+$/,'');});
    cm.setValue(lines.join('\n'));
  };
  document.getElementById('langSel').onchange=function(){cm.setOption('mode',modeOf(lang()));loadCode();};
  document.getElementById('pidSel').onchange=async function(){
    var id=this.value;
    if(!id){cases=[{input:'',expected:''}];cur=0;last=[];saveCases();loadCode();renderAll();return;}
    try{
      var r=await fetch('/api/problem-samples?id='+encodeURIComponent(id)); var d=await r.json();
      if(d && d.samples){
        var arr=[]; for(var i=0;i<d.samples.length;i+=2) arr.push({input:d.samples[i]||'',expected:d.samples[i+1]||''});
        cases=arr.length?arr:[{input:'',expected:''}];
        cur=0;last=[];
        if(!loadCases()) saveCases();
        if(d.timeLimit) document.getElementById('tlInp').value=d.timeLimit;
        if(d.memLimit) document.getElementById('mlInp').value=d.memLimit;
        renderAll();
      }
    }catch(e){}
    loadCode();
  };

  // ================= 云盘 & 提交（IDE 与云盘、提交打通） =================
  var CUR_FILE='', CUR_REV='';
  function setCurFile(p, rev){
    CUR_FILE=p||''; if(rev!==undefined) CUR_REV=rev||'';
    var el=document.getElementById('drvCur');
    if(!el) return;
    if(CUR_FILE){ el.textContent='📄 '+CUR_FILE; el.title='云盘：'+CUR_FILE; el.classList.add('on'); }
    else { el.textContent='未关联云盘文件'; el.title=''; el.classList.remove('on'); }
  }
  function fileIcon(kind,isDir){
    if(isDir) return '📁';
    if(kind==='image') return '🖼️';
    if(kind==='text') return '📄';
    return '📦';
  }
  function closeModal(){ var m=document.getElementById('ideModal'); if(m) m.remove(); }
  function openDriveAt(dir){
    fetch('/ide/drive.json?dir='+encodeURIComponent(dir||''),{credentials:'same-origin'}).then(function(r){return r.json();}).then(function(j){
      if(!j.ok){ alert(j.msg||'读取云盘失败'); return; }
      renderDrive(j);
    }).catch(function(){ alert('网络错误'); });
  }
  function renderDrive(j){
    var dir=j.dir||'', items=j.items||[];
    var box=document.getElementById('ideModal');
    if(!box){ box=document.createElement('div'); box.id='ideModal'; box.className='ide-modal'; document.body.appendChild(box); }
    items.sort(function(a,b){ if(a.isDir!==b.isDir) return a.isDir?-1:1; return a.name.localeCompare(b.name); });
    var list='';
    items.forEach(function(it){
      var sz = it.isDir ? '' : (it.size>=1048576 ? (it.size/1048576).toFixed(1)+' MB' : (it.size>=1024 ? (it.size/1024).toFixed(1)+' KB' : it.size+' B'));
      list+='<div class="ide-fl" data-p="'+esc(it.path)+'" data-d="'+(it.isDir?1:0)+'">'+
        '<span class="ic">'+fileIcon(it.kind,it.isDir)+'</span><span class="nm">'+esc(it.name)+'</span>'+
        '<span class="sz">'+sz+'</span></div>';
    });
    if(!items.length) list='<div class="ide-fl" style="cursor:default;color:var(--muted)">这个文件夹是空的</div>';
    box.innerHTML='<div class="ide-modal-mask" data-x="1"></div>'+
      '<div class="ide-modal-box"><div class="ide-modal-head"><span>📂 从云盘打开</span><span class="grow"></span>'+
      '<button class="btn btn-sm btn-outline" id="drvUpBtn">↑ 上级</button>'+
      '<button class="btn btn-sm" data-x="1">✕</button></div>'+
      '<div class="ide-modal-path">云盘 / '+(dir?esc(dir):'')+'</div>'+
      '<div class="ide-modal-list">'+list+'</div></div>';
    Array.prototype.forEach.call(box.querySelectorAll('[data-x]'),function(el){ el.onclick=closeModal; });
    box.querySelector('#drvUpBtn').onclick=function(){
      if(!dir) return;
      var p=dir.lastIndexOf('/'); openDriveAt(p<0?'':dir.slice(0,p));
    };
    Array.prototype.forEach.call(box.querySelectorAll('.ide-fl[data-p]'), function(el){
      el.onclick=function(){
        var p=el.getAttribute('data-p');
        if(el.getAttribute('data-d')==='1'){ openDriveAt(p); return; }
        loadDriveFile(p);
      };
    });
  }
  function loadDriveFile(p){
    fetch('/ide/read.json?p='+encodeURIComponent(p),{credentials:'same-origin'}).then(function(r){return r.json();}).then(function(j){
      if(!j.ok){ alert(j.msg||'打开失败'); return; }
      closeModal();
      cm.setValue(j.content);
      setCurFile(j.path, j.rev);
      var ext=(j.ext||'').toLowerCase();
      if(ext==='py') cm.setOption('mode','text/x-python');
      else if(['cpp','c','h','hpp','cc','java','js','ts','css','json'].indexOf(ext)>=0) cm.setOption('mode','text/x-c++src');
      else cm.setOption('mode','text/plain');
      var target = ext==='py'?'python':(ext==='c'?'c':(ext==='java'?'java':'cpp'));
      var ls=document.getElementById('langSel');
      for(var i=0;i<ls.options.length;i++) if(ls.options[i].value===target){ ls.value=target; break; }
      if(j.truncated) alert('文件超过 1MB，只打开了前 1MB');
    }).catch(function(){ alert('网络错误'); });
  }
  function saveToDrive(){
    var code=cm.getValue();
    if(!CUR_FILE){
      var p=prompt('保存到云盘的文件路径（例如 solution.cpp）：','solution.cpp');
      if(!p) return false;
      CUR_FILE=p.replace(/^\/+/, '');
    }
    var fd=new URLSearchParams({path:CUR_FILE,code:code});
    fetch('/ide/save',{method:'POST',credentials:'same-origin',
      headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:fd})
      .then(function(r){return r.json();}).then(function(j){
        if(!j.ok){ alert(j.msg||'保存失败'); return; }
        setCurFile(j.path, j.rev);
        alert('已保存到云盘：'+j.path+'（'+j.size+' 字节）');
      }).catch(function(){ alert('网络错误'); });
    return false;
  }
  function submitCode(){
    var pid=document.getElementById('pidSel').value;
    if(!pid){ alert('请先在顶部选择要提交的题目'); return; }
    var code=cm.getValue();
    if(!code.trim()){ alert('代码是空的'); return; }
    fetch('/api/captcha?feature=submit',{credentials:'same-origin'}).then(function(r){return r.json();}).then(function(cap){
      var mask=document.createElement('div'); mask.id='ideModal'; mask.className='ide-modal';
      mask.innerHTML='<div class="ide-modal-mask" data-x="1"></div><div class="ide-modal-box">'+
        '<div class="ide-modal-head"><span>🚀 提交到题目 '+esc(pid)+'</span><span class="grow"></span><button class="btn btn-sm" data-x="1">✕</button></div>'+
        '<div class="ide-modal-list" style="padding:14px">'+
        (cap.required ? '<div class="ide-sub"><label>验证码：'+esc(cap.question)+'</label><input id="capAns" autocomplete="off" placeholder="输入答案后提交"/></div>' : '')+
        '<div style="display:flex;gap:8px;margin-top:14px;align-items:center;">'+
        '<button class="btn" id="doSubmitBtn">确认提交</button>'+
        '<span style="font-size:12px;color:var(--muted)">'+esc(lang())+' · '+code.length+' 字符'+(CUR_FILE?' · 来自 '+esc(CUR_FILE):'')+'</span></div>'+
        '<div id="subMsg" style="margin-top:10px;font-size:13px;"></div></div></div>';
      document.body.appendChild(mask);
      Array.prototype.forEach.call(mask.querySelectorAll('[data-x]'),function(el){ el.onclick=function(){ mask.remove(); }; });
      var ans=document.getElementById('capAns');
      if(ans) ans.focus();
      document.getElementById('doSubmitBtn').onclick=function(){
        var btn=this; btn.disabled=true; btn.textContent='提交中…';
        var fd=new URLSearchParams({problemId:pid,language:lang(),code:code,
          captcha_token:cap.token||'',captcha_answer:ans?ans.value:''});
        fetch('/submit',{method:'POST',credentials:'same-origin',
          headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','X-Requested-With':'fetch'},
          body:fd}).then(function(r){return r.json();}).then(function(j){
          var m=document.getElementById('subMsg');
          if(!j.ok){ btn.disabled=false; btn.textContent='确认提交';
            if(j.captcha){ m.innerHTML='<span style="color:#e0553f">验证码错误，请关闭后重新提交</span>'; }
            else if(j.login){ location.href='/login'; }
            else m.innerHTML='<span style="color:#e0553f">'+esc(j.msg||'提交失败')+'</span>';
            return; }
          btn.textContent='已提交 ✓'; btn.disabled=true;
          m.innerHTML='<div style="color:#27ae60;font-weight:700">提交成功'+(j.judge?'，正在后台评测…':'（评测已关闭）')+'</div>'+
            '<a class="btn btn-sm" style="margin-top:8px" href="/submission/'+encodeURIComponent(j.id)+'">查看提交 →</a>';
        }).catch(function(){ btn.disabled=false; btn.textContent='确认提交'; document.getElementById('subMsg').textContent='网络错误'; });
      };
    }).catch(function(){ alert('无法获取验证码'); });
  }
  document.getElementById('drvOpen').onclick=function(){ openDriveAt(''); };
  document.getElementById('drvSave').onclick=saveToDrive;
  document.getElementById('submitBtn').onclick=submitCode;

  async function init(){
    if(window.innerWidth<900) cm.setSize('100%','420px');
    var sel=document.getElementById('pidSel');
    try{
      var r=await fetch('/api/problem-samples?list=1'); var d=await r.json();
      (d.problems||[]).forEach(function(p){ var o=document.createElement('option'); o.value=p.id;o.textContent=p.id+' · '+p.title+(p.samples>0?' ('+p.samples+' 组样例)':''); sel.appendChild(o); });
    }catch(e){}
    if(typeof INIT_FILE!=='undefined' && INIT_FILE){ setCurFile(INIT_FILE); loadDriveFile(INIT_FILE); renderAll(); return; }
    if(INIT_PID){ sel.value=INIT_PID; if(sel.value===INIT_PID){ await sel.onchange.call(sel); } }
    else { loadCode(); renderAll(); }
  }
  init();
})();
)JS";
    c += "</script>";
    return renderTemplate("自定义测试（IDE）", c, user);
}

std::string Handlers::handleCustomTest(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    (void)path;
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";

    security::SecConfig _sec = security::getSecConfig();
    int CUSTOM_MAX_TIME = _sec.maxCustomTestTime > 0 ? _sec.maxCustomTestTime : 2000;
    int CUSTOM_MAX_MEM  = _sec.maxCustomTestMem  > 0 ? _sec.maxCustomTestMem  : 256;
    int CUSTOM_MAX_CODE = _sec.maxCustomTestCode > 0 ? _sec.maxCustomTestCode : 50000;
    int CUSTOM_MAX_DATA = _sec.maxCustomTestData > 0 ? _sec.maxCustomTestData : 100000;
    int CUSTOM_INTERVAL = _sec.customTestIntervalSec > 0 ? _sec.customTestIntervalSec : 0;

    std::string language = parseFormData(body, "language");
    std::string code = parseFormData(body, "code");
    std::string input = parseFormData(body, "input");
    std::string expected = parseFormData(body, "expected");
    std::string timeLimitS = parseFormData(body, "timeLimit");
    std::string memLimitS = parseFormData(body, "memLimit");

    // 语言下拉
    std::string langOptions;
    for (const auto& l : DataStore::instance().languages) {
        if (!l.enabled) continue;
        langOptions += "<option value='" + l.id + "'" + (language == l.id ? " selected" : "") + ">"
                       + common::htmlEscape(l.name) + "</option>";
    }

    // 表单（保留已填内容，结果内联显示在同一页，不跳转）
    std::string c = "<div class='alert alert-info'>&#128269; 自定义测试：粘贴代码并输入测试数据，点运行，结果直接显示在下方。期望输出可选，填了会对比是否通过。上限：时间 <b>" + std::to_string(CUSTOM_MAX_TIME) + "ms</b>、内存 <b>" + std::to_string(CUSTOM_MAX_MEM) + "MB</b>、代码 <b>" + std::to_string(CUSTOM_MAX_CODE) + "</b>、输入 <b>" + std::to_string(CUSTOM_MAX_DATA) + "</b>" + (CUSTOM_INTERVAL > 0 ? ("、提交间隔 <b>" + std::to_string(CUSTOM_INTERVAL) + "s</b>") : "") + "。</div>"
        "<form method='POST' id='runForm'>"
        "<div class='form-group'><label>&#128278; 语言</label><select name='language'>" + langOptions + "</select></div>"
        "<div class='form-group'><label>&#128220; 代码</label><textarea name='code' rows='16' required placeholder='粘贴你的代码' maxlength='" + std::to_string(CUSTOM_MAX_CODE) + "'>" + common::htmlEscape(code) + "</textarea></div>"
        "<div class='form-group'><label>&#128225; 自定义输入</label><textarea name='input' rows='6' placeholder='输入测试数据' maxlength='" + std::to_string(CUSTOM_MAX_DATA) + "'>" + common::htmlEscape(input) + "</textarea></div>"
        "<div class='form-group'><label>&#9989; 期望输出（可选）</label><textarea name='expected' rows='6' placeholder='填了会对比是否通过'>" + common::htmlEscape(expected) + "</textarea></div>"
        "<div style='display:flex;gap:12px;flex-wrap:wrap;'><div class='form-group'><label>&#9200; 时间限制(ms，上限 " + std::to_string(CUSTOM_MAX_TIME) + ")</label><input name='timeLimit' type='number' value='" + common::htmlEscape(timeLimitS.empty() ? std::string("2000") : timeLimitS) + "' min='1' max='" + std::to_string(CUSTOM_MAX_TIME) + "'/></div>"
        "<div class='form-group'><label>&#128190; 内存限制(MB，上限 " + std::to_string(CUSTOM_MAX_MEM) + ")</label><input name='memLimit' type='number' value='" + common::htmlEscape(memLimitS.empty() ? std::string("256") : memLimitS) + "' min='1' max='" + std::to_string(CUSTOM_MAX_MEM) + "'/></div></div>"
        "<div style='margin-top:8px;'><input type='submit' value='&#9654;&#65039; 运行' class='btn'/></div></form>";

    // 结果（仅 POST 时内联显示）
    if (method == "POST") {
        if (code.empty()) {
            c += "<div class='result-card' id='runResult'><div class='alert alert-error'>代码不能为空</div></div>";
        } else if (CUSTOM_INTERVAL > 0 && security::throttleAction("customtest:" + user->id, CUSTOM_INTERVAL)) {
            c += "<div class='result-card' id='runResult'><div class='alert alert-error'>操作过于频繁，请 " + std::to_string(CUSTOM_INTERVAL) + " 秒后再试</div></div>";
        } else if ((int)code.size() > CUSTOM_MAX_CODE || (int)input.size() > CUSTOM_MAX_DATA) {
            c += "<div class='result-card' id='runResult'><div class='alert alert-error'>超出长度限制：代码≤" + std::to_string(CUSTOM_MAX_CODE) + "、输入数据≤" + std::to_string(CUSTOM_MAX_DATA) + "</div></div>";
        } else {
            int timeLimit = common::stringToInt(timeLimitS);
            int memLimit = common::stringToInt(memLimitS);
            if (timeLimit > CUSTOM_MAX_TIME) timeLimit = CUSTOM_MAX_TIME;
            if (memLimit > CUSTOM_MAX_MEM) memLimit = CUSTOM_MAX_MEM;
            std::string output, verdict, errorMsg;
            int usedTime = 0, usedMem = 0;
            if (!Judge::customRun(language, code, input, timeLimit, memLimit, output, verdict, usedTime, usedMem, errorMsg)) {
                c += "<div class='result-card' id='runResult'><div class='alert alert-error'>" + common::htmlEscape(errorMsg) + "</div></div>";
            } else {
                std::string finalVerdict = verdict;
                bool correct = true;
                if (!expected.empty()) {
                    correct = (common::trim(output) == common::trim(expected));
                    if (verdict == "AC") finalVerdict = correct ? "AC（与期望一致）" : "WA（与期望不一致）";
                }
                std::string cls = (verdict == "AC") ? "ac" : "wa";
                if (verdict == "TLE" || verdict == "MLE") cls = "tle";
                c += "<div class='result-card' id='runResult'>"
                    "<div style='display:flex;gap:12px;align-items:center;flex-wrap:wrap;'>"
                    "<span class='pill status-" + cls + "'>" + common::htmlEscape(finalVerdict) + "</span>"
                    "<span>用时 <b>" + std::to_string(usedTime) + "ms</b></span>"
                    "<span>内存 <b>" + std::to_string(usedMem) + "MB</b></span></div>"
                    "<h3>输出</h3><pre class='out-box'>" + common::htmlEscape(output) + "</pre>";
                if (!expected.empty()) c += "<h3>期望输出</h3><pre class='out-box'>" + common::htmlEscape(expected) + "</pre>";
                c += "</div>";
            }
        }
        // 自动滚动到结果
        c += "<script>if(document.getElementById('runResult')){document.getElementById('runResult').scrollIntoView({behavior:'smooth',block:'start'});}</script>";
    }

    return renderTemplate("自定义测试", c, user);
}

// ============================================================
// IDE 自测 JSON API
// ============================================================
std::string Handlers::handleProblemSamples(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId) {
    (void)method; (void)body; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"error\":\"Unauthorized\"}";

    DataStore& ds = DataStore::instance();
    std::string list = queryParam(path, "list");
    if (list == "1") {
        std::string out = "{\"problems\":[";
        int n = 0;
        for (size_t i = 0; i < ds.problems.size(); i++) {
            Problem& p = ds.problems[i];
            if (p.hidden && !isAdmin(user)) continue;
            if (n) out += ",";
            out += "{\"id\":" + common::jsonStringify(p.id) + ",\"title\":" + common::jsonStringify(p.title) +
                   ",\"samples\":" + std::to_string(p.samples.size() / 2) +
                   ",\"timeLimit\":" + std::to_string(p.timeLimit) + ",\"memLimit\":" + std::to_string(p.memLimit) + "}";
            n++;
        }
        out += "]}";
        return out;
    }
    std::string id = queryParam(path, "id");
    Problem* p = id.empty() ? NULL : ds.findProblemById(id);
    if (!p || (p->hidden && !isAdmin(user))) return "{\"error\":\"题目不存在\"}";
    std::string out = "{\"id\":" + common::jsonStringify(p->id) + ",\"title\":" + common::jsonStringify(p->title) +
                      ",\"timeLimit\":" + std::to_string(p->timeLimit) + ",\"memLimit\":" + std::to_string(p->memLimit) +
                      ",\"samples\":[";
    for (size_t i = 0; i < p->samples.size(); i++) {
        if (i) out += ",";
        out += common::jsonStringify(p->samples[i]);
    }
    out += "]}";
    return out;
}

std::string Handlers::handleCustomTestApi(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    (void)method; (void)path; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"error\":\"请先登录\"}";

    security::SecConfig _sec = security::getSecConfig();
    int MAX_TIME = _sec.maxCustomTestTime > 0 ? _sec.maxCustomTestTime : 2000;
    int MAX_MEM  = _sec.maxCustomTestMem  > 0 ? _sec.maxCustomTestMem  : 256;
    int MAX_CODE = _sec.maxCustomTestCode > 0 ? _sec.maxCustomTestCode : 50000;
    int MAX_DATA = _sec.maxCustomTestData > 0 ? _sec.maxCustomTestData : 100000;
    int INTERVAL = _sec.customTestIntervalSec > 0 ? _sec.customTestIntervalSec : 0;

    auto req = common::jsonParseObject(body);
    std::string language = req.count("language") ? req["language"] : "cpp";
    std::string code = req.count("code") ? req["code"] : "";
    std::string casesRaw = req.count("cases") ? req["cases"] : "[]";
    int timeLimit = req.count("timeLimit") ? common::stringToInt(req["timeLimit"]) : 2000;
    int memLimit = req.count("memLimit") ? common::stringToInt(req["memLimit"]) : 256;
    if (timeLimit <= 0) timeLimit = 2000;
    if (memLimit <= 0) memLimit = 256;
    if (timeLimit > MAX_TIME) timeLimit = MAX_TIME;
    if (memLimit > MAX_MEM) memLimit = MAX_MEM;

    if (code.empty()) return "{\"error\":\"代码不能为空\"}";
    if ((int)code.size() > MAX_CODE) return "{\"error\":\"代码超过长度上限\"}";
    if (INTERVAL > 0 && security::throttleAction("customtest:" + user->id, INTERVAL))
        return "{\"error\":\"操作过于频繁，请 " + std::to_string(INTERVAL) + " 秒后再试\"}";

    std::vector<std::string> inputs;
    auto items = common::jsonParseArray(casesRaw);
    for (size_t i = 0; i < items.size() && i < 10; i++) {
        auto o = common::jsonParseObject(items[i]);
        std::string in = o.count("input") ? o["input"] : "";
        if ((int)in.size() > MAX_DATA) return "{\"error\":\"输入数据超过长度上限\"}";
        inputs.push_back(in);
    }
    if (inputs.empty()) inputs.push_back("");

    std::vector<Judge::CustomCaseResult> results;
    std::string err;
    if (!Judge::customRunMulti(language, code, inputs, timeLimit, memLimit, results, err)) {
        return "{\"error\":" + common::jsonStringify(err) + "}";
    }
    if (!common::isValidUtf8(err)) err = common::toUtf8FromSystem(err);
    std::string out = "{\"ok\":true,\"results\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i) out += ",";
        std::string o = results[i].output;
        std::string e = results[i].error;
        if (!common::isValidUtf8(o)) o = common::toUtf8FromSystem(o);
        if (!common::isValidUtf8(e)) e = common::toUtf8FromSystem(e);
        out += "{\"verdict\":" + common::jsonStringify(results[i].verdict) +
               ",\"output\":" + common::jsonStringify(o) +
               ",\"error\":" + common::jsonStringify(e) +
               ",\"time\":" + std::to_string(results[i].time) +
               ",\"mem\":" + std::to_string(results[i].mem) + "}";
    }
    out += "]}";
    return out;
}

std::string Handlers::handleAdminAddProblem(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!canManageProblems(user)) return "<script>window.location.href='/'</script>";
    
    if (method == "GET") {
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>题目ID (如: 1000)</label><input name='id' required/></div>"
                             "<div class='form-group'><label>标题</label><input name='title' required/></div>"
                             "<div class='form-group'><label>题目描述</label><textarea name='description' rows='8' required></textarea></div>"
                             "<div class='form-group'><label>输入格式</label><textarea name='inputDesc' rows='4'></textarea></div>"
                             "<div class='form-group'><label>输出格式</label><textarea name='outputDesc' rows='4'></textarea></div>"
                             "<div class='form-group'><label>数据范围 / 约束（可空，供生成器与 AI 使用）</label><textarea name='dataRange' rows='4'></textarea></div>"
                             "<div class='form-group'><label>样例 (输入和输出用 || 分隔，多组用 | 分隔)</label>"
                             "<textarea name='samples' rows='4' placeholder='输入1||输出1|输入2||输出2'></textarea></div>"
                             "<div class='form-group'><label>时间限制 (ms)</label><input name='timeLimit' type='number' value='1000' required/></div>"
                             "<div class='form-group'><label>内存限制 (MB)</label><input name='memLimit' type='number' value='256' required/></div>"
                             "<div class='form-group'><label>是否隐藏</label>"
                             "<select name='hidden'><option value='0'>公开</option><option value='1'>隐藏</option></select></div>"
                             "<div class='form-group'><label>特殊评测 SPJ（可空）</label>"
                             "<textarea name='spj' rows='10' style='font-family:Consolas,monospace;font-size:13px;' placeholder='argc: argv[1]=输入 argv[2]=标准输出 argv[3]=选手输出；return 0 通过'></textarea>"
                             "<div class='card-meta'>填写后本题使用 SPJ；留空为标准答案比对。</div></div>"
                             "<input type='submit' value='添加题目' class='btn'/> <a href='/admin' class='btn'>取消</a></form>";
        return renderTemplate("添加题目", content, user);
    } else {
        std::string id = parseFormData(body, "id");
        std::string title = parseFormData(body, "title");
        std::string description = parseFormData(body, "description");
        std::string inputDesc = parseFormData(body, "inputDesc");
        std::string outputDesc = parseFormData(body, "outputDesc");
        std::string samplesStr = parseFormData(body, "samples");
        int timeLimit = common::stringToInt(parseFormData(body, "timeLimit"));
        int memLimit = common::stringToInt(parseFormData(body, "memLimit"));
        bool hidden = parseFormData(body, "hidden") == "1";
        
        if (id.empty() || title.empty() || description.empty() || DataStore::instance().findProblemById(id)) {
            std::string error = DataStore::instance().findProblemById(id) ? "题目ID已存在" : "题目ID、标题和描述不能为空";
            std::string content = "<div class='alert alert-error'>" + error + "</div><a href='/admin/problems/add' class='btn'>返回</a>";
            return renderTemplate("添加失败", content, user);
        }
        
        Problem problem;
        problem.id = id;
        problem.title = title;
        problem.description = description;
        problem.inputDesc = inputDesc;
        problem.outputDesc = outputDesc;
        problem.dataRange = parseFormData(body, "dataRange");
        if (!samplesStr.empty()) problem.samples = common::split(samplesStr, '|');
        problem.timeLimit = timeLimit > 0 ? timeLimit : 1000;
        problem.memLimit = memLimit > 0 ? memLimit : 256;
        problem.author = user->username;
        problem.createTime = common::getTimeStr();
        problem.acCount = 0;
        problem.submitCount = 0;
        problem.hidden = hidden;
        problem.image = "";
        problem.spj = parseFormData(body, "spj");
        
        DataStore::instance().problems.push_back(problem);
        DataStore::instance().saveProblems();
        
        std::string problemDir = std::string("./problems/") + id + "/";
        common::createDirectory(problemDir);
        
        std::string content = "<div class='alert alert-success'>题目添加成功！</div>"
                             "<p>请将测试数据放入: " + problemDir + "</p>"
                             "<a href='/admin' class='btn'>返回管理面板</a> <a href='/problem/" + id + "' class='btn'>查看题目</a>";
        return renderTemplate("添加成功", content, user);
    }
}

// ============================================================
// 管理员 - 修改题目
// ============================================================
std::string Handlers::handleAdminEditProblem(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    size_t lastSlash = path.find_last_of('/');
    std::string problemId = path.substr(lastSlash + 1);
    Problem* problem = DataStore::instance().findProblemById(problemId);
    if (!problem) {
        std::string content = "<div class='alert alert-error'>题目不存在</div><a href='/admin' class='btn'>返回</a>";
        return renderTemplate("错误", content, user);
    }
    bool isOwner = isProblemOwner(problem, user);
    if (!canManageProblems(user) && !isOwner) return "<script>window.location.href='/'</script>";
    
    if (method == "GET") {
        std::string samplesStr;
        for (size_t i = 0; i < problem->samples.size(); ++i) {
            samplesStr += problem->samples[i];
            if (i + 1 < problem->samples.size()) samplesStr += "|";
        }
        
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>题目ID</label><input name='id' value='" + common::htmlEscape(problem->id) + "' readonly/></div>"
                             "<div class='form-group'><label>标题</label><input name='title' value='" + common::htmlEscape(problem->title) + "' required/></div>"
                             "<div class='form-group'><label>题目描述</label><textarea name='description' rows='8' required>" + common::htmlEscape(problem->description) + "</textarea></div>"
                             "<div class='form-group'><label>输入格式</label><textarea name='inputDesc' rows='4'>" + common::htmlEscape(problem->inputDesc) + "</textarea></div>"
                             "<div class='form-group'><label>输出格式</label><textarea name='outputDesc' rows='4'>" + common::htmlEscape(problem->outputDesc) + "</textarea></div>"
                             "<div class='form-group'><label>数据范围 / 约束（供数据生成器与 AI 使用）</label><textarea name='dataRange' rows='4' placeholder='如：1 ≤ n ≤ 10^5，1 ≤ a_i ≤ 10^9；保证图连通'>" + common::htmlEscape(problem->dataRange) + "</textarea></div>"
                             "<div class='form-group'><label>样例</label><textarea name='samples' rows='4'>" + common::htmlEscape(samplesStr) + "</textarea></div>"
                             "<div class='form-group'><label>时间限制 (ms)</label><input name='timeLimit' type='number' value='" + std::to_string(problem->timeLimit) + "' required/></div>"
                             "<div class='form-group'><label>内存限制 (MB)</label><input name='memLimit' type='number' value='" + std::to_string(problem->memLimit) + "' required/></div>"
                             "<div class='form-group'><label>是否隐藏</label>"
                             "<select name='hidden'>"
                             "<option value='0'" + (!problem->hidden ? " selected" : "") + ">公开</option>"
                             "<option value='1'" + (problem->hidden ? " selected" : "") + ">隐藏</option>"
                             "</select></div>"
                             "<div class='form-group'><label>特殊评测 SPJ（留空=标准答案比对）</label>"
                             "<textarea name='spj' rows='12' style='font-family:Consolas,monospace;font-size:13px;' placeholder='int main(int argc,char* argv[]){ /* argv[1]=输入 argv[2]=标准输出 argv[3]=选手输出；return 0 表示通过 */ }'>" + common::htmlEscape(problem->spj) + "</textarea>"
                             "<div class='card-meta'>填写后本题使用 SPJ 判题：程序读到三个文件路径，退出码 0 = 通过，非 0 = 答案错误；stdout 第一行会显示给选手。</div></div>"
                             "<input type='submit' value='保存修改' class='btn'/> <a href='/problem/" + problemId + "' class='btn'>取消</a></form>";
        return renderTemplate("修改题目 - " + problem->title, content, user);
    } else {
        std::string title = parseFormData(body, "title");
        std::string description = parseFormData(body, "description");
        std::string inputDesc = parseFormData(body, "inputDesc");
        std::string outputDesc = parseFormData(body, "outputDesc");
        std::string samplesStr = parseFormData(body, "samples");
        int timeLimit = common::stringToInt(parseFormData(body, "timeLimit"));
        int memLimit = common::stringToInt(parseFormData(body, "memLimit"));
        bool hidden = parseFormData(body, "hidden") == "1";
        std::string spjCode = parseFormData(body, "spj");
        
        if (title.empty() || description.empty()) {
            std::string content = "<div class='alert alert-error'>标题和描述不能为空</div><a href='/admin/problem/edit/" + problemId + "' class='btn'>返回</a>";
            return renderTemplate("修改失败", content, user);
        }
        // 普通用户(题目作者)修改时也强制限制
        if (!canManageProblems(user)) {
            security::SecConfig _sec = security::getSecConfig();
            if ((int)title.size() > _sec.maxProblemTitle || (int)description.size() > _sec.maxProblemDesc
                || (int)samplesStr.size() > _sec.maxProblemSample) {
                std::string c = "<div class='alert alert-error'>超出长度限制：标题≤" + std::to_string(_sec.maxProblemTitle)
                    + "、题面≤" + std::to_string(_sec.maxProblemDesc) + "、样例≤" + std::to_string(_sec.maxProblemSample)
                    + "</div><a href='/problem/" + problemId + "' class='btn'>返回</a>";
                return renderTemplate("修改失败", c, user);
            }
            if (timeLimit > 0 && timeLimit > _sec.maxUserProblemTime) timeLimit = _sec.maxUserProblemTime;
            if (memLimit > 0 && memLimit > _sec.maxUserProblemMem) memLimit = _sec.maxUserProblemMem;
        }
        
        problem->title = title;
        problem->description = description;
        problem->inputDesc = inputDesc;
        problem->outputDesc = outputDesc;
        if (!samplesStr.empty()) problem->samples = common::split(samplesStr, '|');
        problem->timeLimit = timeLimit > 0 ? timeLimit : 1000;
        problem->memLimit = memLimit > 0 ? memLimit : 256;
        problem->hidden = hidden;
        problem->spj = spjCode;
        
        DataStore::instance().saveProblems();
        std::string content = "<div class='alert alert-success'>题目修改成功！" + std::string(spjCode.empty() ? "（标准评测）" : "（SPJ 特殊评测已启用）") + "</div><a href='/admin' class='btn'>返回管理面板</a> <a href='/problem/" + problemId + "' class='btn'>查看题目</a>";
        return renderTemplate("修改成功", content, user);
    }
}

// ============================================================
// 管理员 - 删除题目
// ============================================================
std::string Handlers::handleAdminDeleteProblem(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string problemId = parseFormData(body, "problemId");
    if (problemId.empty()) {
        size_t lastSlash = path.find_last_of('/');
        problemId = path.substr(lastSlash + 1);
    }
    
    Problem* problem = DataStore::instance().findProblemById(problemId);
    if (!problem) {
        std::string content = "<div class='alert alert-error'>题目不存在</div><a href='/admin' class='btn'>返回</a>";
        return renderTemplate("删除失败", content, user);
    }
    if (!canManageProblems(user) && !isProblemOwner(problem, user)) return "<script>window.location.href='/'</script>";
    
    auto& problems = DataStore::instance().problems;
    for (auto it = problems.begin(); it != problems.end(); ++it) {
        if (it->id == problemId) { problems.erase(it); break; }
    }
    DataStore::instance().saveProblems();
    
    std::string problemDir = std::string("./problems/") + problemId + "/";
    common::removeDirectory(problemDir);
    
    std::string content = "<div class='alert alert-success'>题目删除成功！</div><a href='/admin' class='btn'>返回管理面板</a>";
    return renderTemplate("删除成功", content, user);
}

// ============================================================
// 管理员 - 内容管理
// ============================================================
std::string Handlers::handleAdminPosts(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    std::string content = adminNavbar(user);
    content += "<table><thead><tr><th>ID</th><th>类型</th><th>标题</th><th>作者</th><th>时间</th><th>操作</th></tr></thead><tbody>";
    for (const auto& p : DataStore::instance().posts) {
        User* author = DataStore::instance().findUserById(p.authorId);
        std::string typeStr;
        switch(p.type) { case BLOG: typeStr = "博客"; break; case DISCUSSION: typeStr = "讨论"; break; case BENBEN: typeStr = "犇犇"; break; }
        
        content += "<tr><td>" + p.id.substr(0, 8) + "</td><td>" + typeStr + "</td>"
                  "<td><a href='/post/" + p.id + "'>" + common::htmlEscape(p.title.empty() ? "(无标题)" : p.title) + "</a></td>"
                  "<td><a href='/user/" + p.authorId + "'>" + (author ? common::htmlEscape(author->username) : "已注销") + "</a></td>"
                  "<td>" + p.createTime + "</td>"
                  "<td><a href='/admin/post/edit/" + p.id + "' class='btn btn-sm btn-outline'>✏️</a>"
                  "<form method='POST' action='/admin/post/delete' style='display:inline;'>"
                  "<input type='hidden' name='postId' value='" + p.id + "'/>"
                  "<input type='submit' value='🗑️' class='btn btn-sm btn-danger' onclick='return confirm(\"确定要删除吗？\")'/></form></td></tr>";
    }
    content += "</tbody></table>";
    return renderTemplate("内容管理", content, user);
}

// ============================================================
// 管理员 - 删除帖子
// ============================================================
std::string Handlers::handleAdminDeletePost(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    std::string postId = parseFormData(body, "postId");
    if (postId.empty()) {
        size_t lastSlash = path.find_last_of('/');
        postId = path.substr(lastSlash + 1);
    }
    
    Post* post = DataStore::instance().findPostById(postId);
    if (!post) {
        std::string content = "<div class='alert alert-error'>帖子不存在</div><a href='/admin/posts' class='btn'>返回</a>";
        return renderTemplate("删除失败", content, user);
    }
    
    auto& posts = DataStore::instance().posts;
    for (auto it = posts.begin(); it != posts.end(); ++it) {
        if (it->id == postId) { posts.erase(it); break; }
    }
    DataStore::instance().savePosts();
    
    std::string content = "<div class='alert alert-success'>帖子删除成功！</div><a href='/admin/posts' class='btn'>返回</a>";
    return renderTemplate("删除成功", content, user);
}

// ============================================================
// 管理员 - 修改帖子
// ============================================================
std::string Handlers::handleAdminEditPost(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    size_t lastSlash = path.find_last_of('/');
    std::string postId = path.substr(lastSlash + 1);
    Post* post = DataStore::instance().findPostById(postId);
    if (!post) {
        std::string content = "<div class='alert alert-error'>帖子不存在</div><a href='/admin' class='btn'>返回</a>";
        return renderTemplate("错误", content, user);
    }
    
    if (method == "GET") {
        std::string typeStr;
        switch(post->type) { case BLOG: typeStr = "blog"; break; case DISCUSSION: typeStr = "discuss"; break; case BENBEN: typeStr = "benben"; break; }
        
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>标题</label><input name='title' value='" + common::htmlEscape(post->title) + "'/></div>"
                             "<div class='form-group'><label>内容</label><textarea name='content' rows='15'>" + common::htmlEscape(post->content) + "</textarea></div>"
                             "<div class='form-group'><label>类型</label><select name='type'>"
                             "<option value='blog'" + (typeStr == "blog" ? " selected" : "") + ">博客</option>"
                             "<option value='discuss'" + (typeStr == "discuss" ? " selected" : "") + ">讨论</option>"
                             "<option value='benben'" + (typeStr == "benben" ? " selected" : "") + ">犇犇</option></select></div>"
                             "<input type='submit' value='保存修改' class='btn'/> <a href='/admin' class='btn'>取消</a></form>";
        return renderTemplate("修改帖子", content, user);
    } else {
        std::string title = parseFormData(body, "title");
        std::string contentText = parseFormData(body, "content");
        std::string typeStr = parseFormData(body, "type");
        
        if (contentText.empty()) {
            std::string content = "<div class='alert alert-error'>内容不能为空</div><a href='/admin/post/edit/" + postId + "' class='btn'>返回</a>";
            return renderTemplate("修改失败", content, user);
        }
        
        post->title = title;
        post->content = contentText;
        if (typeStr == "discuss") post->type = DISCUSSION;
        else if (typeStr == "benben") post->type = BENBEN;
        else post->type = BLOG;
        
        DataStore::instance().savePosts();
        std::string content = "<div class='alert alert-success'>帖子修改成功！</div><a href='/admin' class='btn'>返回管理面板</a> <a href='/post/" + postId + "' class='btn'>查看帖子</a>";
        return renderTemplate("修改成功", content, user);
    }
}

// ============================================================
// 管理员 - 创建比赛
// ============================================================
std::string Handlers::handleAdminAddContest(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    if (method == "GET") {
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>比赛标题</label><input name='title' required/></div>"
                             "<div class='form-group'><label>描述</label><textarea name='description' rows='4'></textarea></div>"
                             "<div class='form-group'><label>开始时间</label><input name='startTime' placeholder='YYYY-MM-DD HH:MM:SS' required/></div>"
                             "<div class='form-group'><label>结束时间</label><input name='endTime' placeholder='YYYY-MM-DD HH:MM:SS' required/></div>"
                             "<div class='form-group'><label>题目列表 (用逗号分隔，可写 题号=分值，如 P1=100,P2=50)</label><input name='problems'/></div>"
                             "<details style='margin:6px 0 10px;'><summary>&#128218; 题库速查（点击展开）</summary>"
                             "<input id='plistAdd' oninput='var q=this.value.toLowerCase();document.querySelectorAll(\"#plistAddBox .pitem\").forEach(function(e){e.style.display=e.getAttribute(\"data-k\").toLowerCase().indexOf(q)>=0?\"\":\"none\";});' placeholder='筛选题号/标题…' style='width:100%;margin:6px 0;'/>"
                             "<div id='plistAddBox' style='max-height:180px;overflow:auto;border:1px solid #e9ecef;border-radius:6px;padding:6px;font-size:13px;'>"
                             + Handlers::problemQuickList() +
                             "</div></details>"
                             "<div class='form-group'>"
                             "<label>是否 Rated 比赛</label>"
                             "<select name='isRated'>"
                             "<option value='1'>⭐ Rated (影响积分)</option>"
                             "<option value='0'>📝 Unrated (娱乐赛)</option>"
                             "</select>"
                             "</div>"
                             "<div class='form-group'>"
                             "<label>赛制</label>"
                             "<select name='format'>"
                             "<option value='icpc'>🏆 ICPC (AC数降序 + 罚时升序，实时反馈)</option>"
                             "<option value='oi'>📐 OI (提交后即时评测但不公开，赛后统一公开，总分排名)</option>"
                             "<option value='ioi'>🪙 IOI (即时公开，总分排名)</option>"
                             "</select>"
                             "</div>"
                             "<div class='form-group'>"
                             "<label>组队赛</label>"
                             "<select name='isTeam'>"
                             "<option value='0'>个人赛</option>"
                             "<option value='1'>👥 组队赛 (队长建队 + 邀请/申请加入，按队伍排名)</option>"
                             "</select>"
                             "</div>"
                             "<input type='submit' value='创建比赛' class='btn'/> <a href='/admin' class='btn'>取消</a></form>";
        return renderTemplate("创建比赛", content, user);
    } else {
        std::string title = parseFormData(body, "title");
        std::string description = parseFormData(body, "description");
        std::string startTime = parseFormData(body, "startTime");
        std::string endTime = parseFormData(body, "endTime");
        std::string problemsStr = parseFormData(body, "problems");
        bool isRated = parseFormData(body, "isRated") == "1";
        std::string format = parseFormData(body, "format");
        bool isTeam = parseFormData(body, "isTeam") == "1";
        
        if (format != "oi" && format != "ioi") format = "icpc";
        
        if (title.empty() || startTime.empty() || endTime.empty()) {
            std::string content = "<div class='alert alert-error'>标题、开始时间和结束时间不能为空</div><a href='/admin/contest/add' class='btn'>返回</a>";
            return renderTemplate("创建失败", content, user);
        }
        
        Contest contest;
        contest.id = common::genId();
        contest.title = title;
        contest.description = description;
        contest.startTime = startTime;
        contest.endTime = endTime;
        contest.isRated = isRated;
        contest.isFinished = false;
        contest.format = format;
        contest.isTeam = isTeam;
        
        if (!problemsStr.empty()) {
            auto ids = common::split(problemsStr, ',');
            for (const auto& id : ids) {
                std::string tok = common::trim(id);
                int sc = 100;
                size_t eq = tok.find('=');
                if (eq != std::string::npos) { sc = common::stringToInt(tok.substr(eq + 1)); if (sc < 1) sc = 1; tok = tok.substr(0, eq); }
                std::string tid = common::trim(tok);
                if (DataStore::instance().findProblemById(tid)) {
                    ContestProblem cp;
                    cp.problemId = tid;
                    cp.score = sc;
                    contest.problems.push_back(cp);
                }
            }
        }
        
        DataStore::instance().contests.push_back(contest);
        DataStore::instance().saveContests();
        
        std::string content = "<div class='alert alert-success'>比赛创建成功！</div>"
                             "<a href='/admin' class='btn'>返回管理面板</a> <a href='/contest/" + contest.id + "' class='btn'>查看比赛</a>";
        return renderTemplate("创建成功", content, user);
    }
}

// ============================================================
// 管理员 - 修改比赛
// ============================================================
std::string Handlers::handleAdminEditContest(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    size_t lastSlash = path.find_last_of('/');
    std::string contestId = path.substr(lastSlash + 1);
    Contest* contest = DataStore::instance().findContestById(contestId);
    if (!contest) {
        std::string content = "<div class='alert alert-error'>比赛不存在</div><a href='/admin' class='btn'>返回</a>";
        return renderTemplate("错误", content, user);
    }
    
    if (method == "GET") {
        std::string problemsStr;
        for (size_t i = 0; i < contest->problems.size(); ++i) {
            problemsStr += contest->problems[i].problemId;
            if (i + 1 < contest->problems.size()) problemsStr += ",";
        }
        
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>比赛标题</label><input name='title' value='" + common::htmlEscape(contest->title) + "' required/></div>"
                             "<div class='form-group'><label>描述</label><textarea name='description' rows='4'>" + common::htmlEscape(contest->description) + "</textarea></div>"
                             "<div class='form-group'><label>开始时间</label><input name='startTime' value='" + contest->startTime + "' required/></div>"
                             "<div class='form-group'><label>结束时间</label><input name='endTime' value='" + contest->endTime + "' required/></div>"
                             "<div class='form-group'><label>题目列表 (用逗号分隔，可写 题号=分值，如 P1=100,P2=50)</label><input name='problems' value='" + common::htmlEscape(problemsStr) + "'/></div>"
                             "<details style='margin:6px 0 10px;'><summary>&#128218; 题库速查（点击展开）</summary>"
                             "<input id='plistEdit' oninput='var q=this.value.toLowerCase();document.querySelectorAll(\"#plistEditBox .pitem\").forEach(function(e){e.style.display=e.getAttribute(\"data-k\").toLowerCase().indexOf(q)>=0?\"\":\"none\";});' placeholder='筛选题号/标题…' style='width:100%;margin:6px 0;'/>"
                             "<div id='plistEditBox' style='max-height:180px;overflow:auto;border:1px solid #e9ecef;border-radius:6px;padding:6px;font-size:13px;'>" + Handlers::problemQuickList() + "</div></details>"
                             "<div class='form-group'>"
                             "<label>是否 Rated 比赛</label>"
                             "<select name='isRated'>"
                             "<option value='1'" + (contest->isRated ? " selected" : "") + ">⭐ Rated (影响积分)</option>"
                             "<option value='0'" + (!contest->isRated ? " selected" : "") + ">📝 Unrated (娱乐赛)</option>"
                             "</select>"
                             "</div>"
                             "<div class='form-group'>"
                             "<label>赛制</label>"
                             "<select name='format'>"
                             "<option value='icpc'" + (contest->format == "icpc" ? " selected" : "") + ">🏆 ICPC (AC数降序 + 罚时升序)</option>"
                             "<option value='oi'" + (contest->format == "oi" ? " selected" : "") + ">📐 OI (即时评测不公开，赛后统一公开)</option>"
                             "<option value='ioi'" + (contest->format == "ioi" ? " selected" : "") + ">🪙 IOI (即时公开，总分排名)</option>"
                             "</select>"
                             "</div>"
                             "<div class='form-group'>"
                             "<label>组队赛</label>"
                             "<select name='isTeam'>"
                             "<option value='0'" + (!contest->isTeam ? " selected" : "") + ">个人赛</option>"
                             "<option value='1'" + (contest->isTeam ? " selected" : "") + ">👥 组队赛</option>"
                             "</select>"
                             "</div>"
                             "<input type='submit' value='保存修改' class='btn'/> <a href='/admin' class='btn'>取消</a></form>";
        return renderTemplate("修改比赛", content, user);
    } else {
        std::string title = parseFormData(body, "title");
        std::string description = parseFormData(body, "description");
        std::string startTime = parseFormData(body, "startTime");
        std::string endTime = parseFormData(body, "endTime");
        std::string problemsStr = parseFormData(body, "problems");
        bool isRated = parseFormData(body, "isRated") == "1";
        std::string format = parseFormData(body, "format");
        bool isTeam = parseFormData(body, "isTeam") == "1";
        if (format != "oi" && format != "ioi") format = "icpc";
        
        if (title.empty() || startTime.empty() || endTime.empty()) {
            std::string content = "<div class='alert alert-error'>标题、开始时间和结束时间不能为空</div><a href='/admin/contest/edit/" + contestId + "' class='btn'>返回</a>";
            return renderTemplate("修改失败", content, user);
        }
        
        contest->title = title;
        contest->description = description;
        contest->startTime = startTime;
        contest->endTime = endTime;
        contest->isRated = isRated;
        contest->format = format;
        contest->isTeam = isTeam;
        contest->problems.clear();
        
        if (!problemsStr.empty()) {
            auto ids = common::split(problemsStr, ',');
            for (const auto& id : ids) {
                std::string tok = common::trim(id);
                int sc = 100;
                size_t eq = tok.find('=');
                if (eq != std::string::npos) { sc = common::stringToInt(tok.substr(eq + 1)); if (sc < 1) sc = 1; tok = tok.substr(0, eq); }
                std::string tid = common::trim(tok);
                if (DataStore::instance().findProblemById(tid)) {
                    ContestProblem cp;
                    cp.problemId = tid;
                    cp.score = sc;
                    contest->problems.push_back(cp);
                }
            }
        }
        
        DataStore::instance().saveContests();
        std::string content = "<div class='alert alert-success'>比赛修改成功！</div>"
                             "<a href='/admin' class='btn'>返回管理面板</a> <a href='/contest/" + contestId + "' class='btn'>查看比赛</a>";
        return renderTemplate("修改成功", content, user);
    }
}

// ============================================================
// 管理员 - 比赛管理
// ============================================================
std::string Handlers::handleAdminContests(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    std::string content = adminNavbar(user);
    content += "<div class='action-bar'><a href='/admin/contest/add' class='btn'>➕ 创建比赛</a></div>";
    if (DataStore::instance().contests.empty()) {
        content += "<div class='empty-state'><span class='icon'>🏆</span>暂无比赛</div>";
    } else {
        content += "<table><thead><tr><th>ID</th><th>标题</th><th>开始时间</th><th>结束时间</th><th>题目数</th><th>Rated</th><th>操作</th></tr></thead><tbody>";
        for (const auto& c : DataStore::instance().contests) {
            content += "<tr><td>" + c.id.substr(0, 8) + "</td>"
                      "<td><a href='/contest/" + c.id + "'>" + common::htmlEscape(c.title) + "</a></td>"
                      "<td>" + c.startTime + "</td><td>" + c.endTime + "</td>"
                      "<td>" + std::to_string(c.problems.size()) + "</td>"
                      "<td>" + (c.isRated ? "⭐" : "📝") + "</td>"
                      "<td><a href='/admin/contest/edit/" + c.id + "' class='btn btn-sm btn-outline'>✏️</a></td></tr>";
        }
        content += "</tbody></table>";
    }
    return renderTemplate("比赛管理", content, user);
}

// ============================================================
// 管理员 - 创建题单
// ============================================================
std::string Handlers::handleAdminAddProblemSet(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    
    if (method == "GET") {
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>题单名称</label><input name='name' required/></div>"
                             "<div class='form-group'><label>描述</label><textarea name='description' rows='4'></textarea></div>"
                             "<div class='form-group'><label>题目列表 (用逗号分隔题目ID)</label><input name='problems' required/></div>"
                             "<input type='submit' value='创建题单' class='btn'/> <a href='/admin' class='btn'>取消</a></form>";
        return renderTemplate("创建题单", content, user);
    } else {
        std::string name = parseFormData(body, "name");
        std::string description = parseFormData(body, "description");
        std::string problemsStr = parseFormData(body, "problems");
        
        if (name.empty() || problemsStr.empty()) {
            std::string content = "<div class='alert alert-error'>名称和题目列表不能为空</div><a href='/admin/set/add' class='btn'>返回</a>";
            return renderTemplate("创建失败", content, user);
        }
        
        ProblemSet ps;
        ps.id = common::genId();
        ps.name = name;
        ps.description = description;
        ps.authorId = user->id;
        ps.createTime = common::getTimeStr();
        
        auto ids = common::split(problemsStr, ',');
        for (const auto& id : ids) {
            std::string tid = common::trim(id);
            if (DataStore::instance().findProblemById(tid)) {
                ps.problemIds.push_back(tid);
            }
        }
        
        if (ps.problemIds.empty()) {
            std::string content = "<div class='alert alert-error'>没有有效的题目</div><a href='/admin/set/add' class='btn'>返回</a>";
            return renderTemplate("创建失败", content, user);
        }
        
        DataStore::instance().problemSets.push_back(ps);
        DataStore::instance().saveProblemSets();
        
        std::string content = "<div class='alert alert-success'>题单创建成功！</div>"
                             "<a href='/admin' class='btn'>返回管理面板</a> <a href='/set/" + ps.id + "' class='btn'>查看题单</a>";
        return renderTemplate("创建成功", content, user);
    }
}

// ============================================================
// 路由注册
// ============================================================

// ============================================================
// Cloud Drive (\u4e91\u76d8)
// ============================================================

static int getDriveMB(const std::string& key, int def) {
    std::string cfg = common::readFile("./data/drive.json");
    if (!cfg.empty()) {
        auto o = common::jsonParseObject(cfg);
        if (o.count(key)) {
            int v = common::stringToInt(o[key]);
            if (v >= 1) return v;
        }
    }
    return def;
}
static int getDriveMaxMB() { return getDriveMB("maxFileMB", 10); }
static int getDriveTotalMB() { return getDriveMB("maxTotalMB", 100); }
static int getUserDriveTotalMB(User* u) {
    int base = (u && u->driveMB > 0) ? u->driveMB : getDriveTotalMB();
    if (u && !u->driveBonusUntil.empty() && !common::isTimeExpired(u->driveBonusUntil))
        base += u->driveBonusMB;
    return base;
}

static int getUserEffCoeff(User* u) {
    int c = u ? u->effCoeff : 100;
    if (u && !u->coeffUntil.empty() && !common::isTimeExpired(u->coeffUntil))
        c += u->coeffBonusPct;
    return c;
}

static void setDriveConfig(int maxFileMB, int maxTotalMB) {
    if (maxFileMB < 1) maxFileMB = 1;
    if (maxTotalMB < 1) maxTotalMB = 1;
    common::writeFile("./data/drive.json", "{\"maxFileMB\":" + std::to_string(maxFileMB) + ",\"maxTotalMB\":" + std::to_string(maxTotalMB) + "}");
}


// ---------- \u4e91\u76d8\u5171\u4eab ----------
static std::vector<std::map<std::string, std::string> > loadShares() {
    std::vector<std::map<std::string, std::string> > out;
    std::string s = common::readFile("./data/shares.json");
    std::vector<std::string> lines = common::split(s, '\n');
    for (auto& l : lines) {
        l = common::trim(l);
        if (l.empty()) continue;
        auto o = common::jsonParseObject(l);
        if (!o["token"].empty()) out.push_back(o);
    }
    return out;
}
static void saveShares(const std::vector<std::map<std::string, std::string> >& shares) {
    std::string s;
    for (const auto& o : shares) s += common::jsonStringify(o) + "\n";
    common::writeFile("./data/shares.json", s);
}
static std::string genShareToken() {
    std::string t;
    srand((unsigned)time(0) ^ (unsigned)(long long)&t);
    static const char* hexc = "0123456789abcdef";
    for (int i = 0; i < 24; i++) t += hexc[rand() % 16];
    return t;
}

// ---------- 共享文件夹协作：编辑锁 ----------
// 一把锁 = 某个共享 token 下的一个相对路径。默认 10 分钟，保存/续期会刷新。
// 锁只是"礼貌性互斥"：拿不到锁的人不会被强制踢出，但保存会被拒绝，避免互相覆盖。
struct DrvShareLock {
    std::string token, path, userId, userName;
    long long expire = 0;
};
static const int DRV_LOCK_SEC = 600;
static std::mutex g_drvLockMutex;

static std::vector<DrvShareLock> drvReadLocksUnlocked() {
    std::vector<DrvShareLock> out;
    std::string raw = common::readFile("./data/drive_locks.json");
    std::vector<std::string> lines = common::split(raw, '\n');
    long long now = (long long)time(NULL);
    for (size_t i = 0; i < lines.size(); i++) {
        std::string l = common::trim(lines[i]);
        if (l.empty()) continue;
        auto o = common::jsonParseObject(l);
        DrvShareLock k;
        if (o.count("token")) k.token = o["token"];
        if (o.count("path")) k.path = o["path"];
        if (o.count("userId")) k.userId = o["userId"];
        if (o.count("userName")) k.userName = o["userName"];
        if (o.count("expire")) k.expire = common::stringToLongLong(o["expire"]);
        if (!k.token.empty() && !k.path.empty() && k.expire > now) out.push_back(k);
    }
    return out;
}

static void drvWriteLocksUnlocked(const std::vector<DrvShareLock>& locks) {
    std::string s;
    for (size_t i = 0; i < locks.size(); i++) {
        std::map<std::string, std::string> m;
        m["token"] = locks[i].token;
        m["path"] = locks[i].path;
        m["userId"] = locks[i].userId;
        m["userName"] = locks[i].userName;
        m["expire"] = std::to_string(locks[i].expire);
        s += common::jsonStringify(m) + "\n";
    }
    common::writeFile("./data/drive_locks.json", s);
}

// 返回下标（找不到 = -1）。不要返回指针：push_back 会使指针失效。
static int drvFindLockIdx(std::vector<DrvShareLock>& locks, const std::string& token, const std::string& p) {
    for (size_t i = 0; i < locks.size(); i++)
        if (locks[i].token == token && locks[i].path == p) return (int)i;
    return -1;
}

// 共享权限：没有 perm 字段的一律按只读处理（兼容旧数据）
static bool drvShareCanEdit(const std::map<std::string, std::string>& sh) {
    std::map<std::string, std::string>::const_iterator it = sh.find("perm");
    return it != sh.end() && it->second == "edit";
}

// 文件版本号（大小 + 最后写入时间），用于编辑时的并发冲突检测
static std::string drvFileRev(const std::string& full) {
    WIN32_FILE_ATTRIBUTE_DATA ad;
    if (!GetFileAttributesExA(full.c_str(), GetFileExInfoStandard, &ad)) return "";
    long long sz = ((long long)ad.nFileSizeHigh << 32) | (long long)ad.nFileSizeLow;
    return std::to_string(sz) + "-" + std::to_string((long long)ad.ftLastWriteTime.dwLowDateTime);
}

std::string Handlers::handleDriveShare(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string action = parseFormData(body, "action");
    std::string fname = sanitizeRelPath(parseFormData(body, "file"));
    // 是否允许拿到链接的登录用户协作编辑（文件夹才有意义）
    bool _allowEdit = (parseFormData(body, "allowEdit") == "1");
    std::string dir = "./files/" + user->id + "/";
    bool ajax = headers.count("x-requested-with") && headers.at("x-requested-with") == "fetch";
    if (ajax) respHeaders["Content-Type"] = "application/json; charset=utf-8";
    if (action == "share") {
        DWORD _sattr = GetFileAttributesA((dir + fname).c_str());
        if (fname.empty() || _sattr == INVALID_FILE_ATTRIBUTES) {
            if (ajax) return "{\"ok\":false,\"error\":\"路径不存在\"}";
            return "<div class='alert alert-error'>\u6587\u4ef6\u4e0d\u5b58\u5728</div><a href='/drive' class='btn'>\u8fd4\u56de</a>";
        }
        bool _isDir = (_sattr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        auto shares = loadShares();
        for (auto& sh : shares) if (sh["userId"] == user->id && sh["filename"] == fname) {
            // 已分享过：顺带更新协作权限（只有文件夹支持编辑）
            std::string wantPerm = (_isDir && _allowEdit) ? "edit" : "view";
            if (!sh.count("perm") || sh["perm"] != wantPerm) {
                sh["perm"] = wantPerm;
                saveShares(shares);
            }
            if (ajax) return "{\"ok\":true,\"token\":\"" + common::jsonEscape(sh["token"]) + "\",\"isDir\":" + std::string(_isDir ? "true" : "false") + "}";
            return "<script>window.location.href='/drive'</script>";
        }
        std::map<std::string, std::string> entry;
        entry["token"] = genShareToken();
        entry["userId"] = user->id;
        entry["filename"] = fname;
        entry["isDir"] = _isDir ? "1" : "0";
        entry["perm"] = (_isDir && _allowEdit) ? "edit" : "view";
        entry["time"] = common::getCurrentTimeStr();
        shares.push_back(entry);
        saveShares(shares);
        if (ajax) return "{\"ok\":true,\"token\":\"" + common::jsonEscape(entry["token"]) + "\",\"isDir\":" + std::string(_isDir ? "true" : "false") + "}";
    } else if (action == "unshare") {
        auto shares = loadShares();
        std::vector<std::map<std::string, std::string> > keep;
        for (auto& sh : shares) if (!(sh["userId"] == user->id && sh["filename"] == fname)) keep.push_back(sh);
        saveShares(keep);
        if (ajax) return "{\"ok\":true}";
    }
    return "<script>window.location.href='/drive'</script>";
}
// Cloud Drive v2 —— 文件资源管理器级云盘
// 列表 / 搜索 / 排序 / 多选 / 预览(Range) / 下载到指定位置 / 文件夹打包下载
// 移动 / 重命名 / 分享 / 上传(拖拽, 文件夹) / 配额
// ============================================================

struct DrvEntry {
    std::string name;
    bool isDir;
    long long size;
    std::string mtime;
};

static std::vector<DrvEntry> listDriveEntries(const std::string& dir) {
    std::vector<DrvEntry> out;
    std::string search = dir + "*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        DrvEntry e;
        e.name = name;
        e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = ((long long)fd.nFileSizeHigh << 32) | (long long)fd.nFileSizeLow;
        SYSTEMTIME st;
        FILETIME ftLocal;
        FileTimeToLocalFileTime(&fd.ftLastWriteTime, &ftLocal);
        FileTimeToSystemTime(&ftLocal, &st);
        char buf[40];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute);
        e.mtime = buf;
        if (e.isDir) e.size = 0;
        out.push_back(e);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

static std::string drvLowerExt(const std::string& name) {
    std::string ext = common::getFileExtension(name);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

static bool drvExtIn(const std::string& ext, const char** arr, int n) {
    for (int i = 0; i < n; i++) if (ext == arr[i]) return true;
    return false;
}

static std::string driveKindOf(const std::string& name, bool isDir) {
    if (isDir) return "dir";
    std::string e = drvLowerExt(name);
    static const char* kImg[] = {"png","jpg","jpeg","gif","webp","bmp","svg","ico","avif"};
    static const char* kVid[] = {"mp4","webm","mov","mkv","avi","m4v","ogv"};
    static const char* kAud[] = {"mp3","wav","ogg","flac","m4a","aac","opus"};
    static const char* kArc[] = {"zip","rar","7z","tar","gz","bz2","xz"};
    static const char* kTxt[] = {"txt","md","log","cpp","c","h","hpp","cc","py","js","ts","json","css","xml","csv","yml","yaml","ini","conf","sh","bat","ps1","java","go","rs","php","rb","sql","out","ans","in","tex","gitignore","env"};
    static const char* kOff[] = {"doc","docx","xls","xlsx","ppt","pptx"};
    if (drvExtIn(e, kImg, 9)) return "image";
    if (drvExtIn(e, kVid, 7)) return "video";
    if (drvExtIn(e, kAud, 7)) return "audio";
    if (e == "pdf") return "pdf";
    if (drvExtIn(e, kArc, 6)) return "archive";
    if (drvExtIn(e, kOff, 6)) return "office";
    if (drvExtIn(e, kTxt, 34)) return "text";
    if (e == "html" || e == "htm") return "text";
    return "other";
}

static std::string driveMimeOf(const std::string& name) {
    std::string e = drvLowerExt(name);
    if (e == "png") return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    if (e == "gif") return "image/gif";
    if (e == "webp") return "image/webp";
    if (e == "bmp") return "image/bmp";
    if (e == "svg") return "image/svg+xml";
    if (e == "ico") return "image/x-icon";
    if (e == "avif") return "image/avif";
    if (e == "mp4" || e == "m4v") return "video/mp4";
    if (e == "webm") return "video/webm";
    if (e == "ogv") return "video/ogg";
    if (e == "mov") return "video/quicktime";
    if (e == "mkv") return "video/x-matroska";
    if (e == "avi") return "video/x-msvideo";
    if (e == "mp3") return "audio/mpeg";
    if (e == "wav") return "audio/wav";
    if (e == "ogg" || e == "opus") return "audio/ogg";
    if (e == "flac") return "audio/flac";
    if (e == "m4a" || e == "aac") return "audio/mp4";
    if (e == "pdf") return "application/pdf";
    if (e == "zip") return "application/zip";
    if (e == "md" || e == "txt" || e == "log") return "text/plain; charset=utf-8";
    if (driveKindOf(name, false) == "text") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// ---------- 简易 ZIP 打包（store，不压缩；足够下载文件夹用） ----------
struct DrvCrcTable {
    unsigned int t[256];
    DrvCrcTable() {
        for (unsigned int i = 0; i < 256; i++) {
            unsigned int c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
    }
};

static unsigned int drvCrc32(const std::string& data) {
    static DrvCrcTable tab;
    unsigned int crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < data.size(); i++)
        crc = tab.t[(crc ^ (unsigned char)data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void drvPut16(std::string& o, unsigned int v) {
    o += (char)(v & 0xFF);
    o += (char)((v >> 8) & 0xFF);
}
static void drvPut32(std::string& o, unsigned int v) {
    o += (char)(v & 0xFF);
    o += (char)((v >> 8) & 0xFF);
    o += (char)((v >> 16) & 0xFF);
    o += (char)((v >> 24) & 0xFF);
}

struct DrvZipRec {
    std::string name;
    unsigned int crc, csize, usize, off;
};

static void drvZipTime(unsigned int& dosTime, unsigned int& dosDate) {
    time_t now = time(0);
    struct tm* lt = localtime(&now);
    dosTime = (unsigned int)((lt->tm_hour << 11) | (lt->tm_min << 5) | (lt->tm_sec / 2));
    dosDate = (unsigned int)(((lt->tm_year - 80) << 9) | ((lt->tm_mon + 1) << 5) | lt->tm_mday);
}

static void drvZipAdd(std::string& out, std::vector<DrvZipRec>& recs,
                      const std::string& entryName, const std::string& data, bool isDir) {
    unsigned int dosTime, dosDate;
    drvZipTime(dosTime, dosDate);
    std::string name = entryName;
    if (isDir && (name.empty() || name[name.size() - 1] != '/')) name += "/";
    DrvZipRec r;
    r.name = name;
    r.crc = isDir ? 0 : drvCrc32(data);
    r.csize = (unsigned int)data.size();
    r.usize = (unsigned int)data.size();
    r.off = (unsigned int)out.size();
    drvPut32(out, 0x04034b50u);
    drvPut16(out, 20);              // version needed
    drvPut16(out, 0x0800);          // UTF-8 flag
    drvPut16(out, 0);               // store
    drvPut16(out, dosTime);
    drvPut16(out, dosDate);
    drvPut32(out, r.crc);
    drvPut32(out, r.csize);
    drvPut32(out, r.usize);
    drvPut16(out, (unsigned int)name.size());
    drvPut16(out, 0);
    out += name;
    if (!isDir && !data.empty()) out += data;
    recs.push_back(r);
}

static void drvZipEnd(std::string& out, const std::vector<DrvZipRec>& recs) {
    unsigned int dosTime, dosDate;
    drvZipTime(dosTime, dosDate);
    unsigned int cdStart = (unsigned int)out.size();
    for (size_t i = 0; i < recs.size(); i++) {
        const DrvZipRec& r = recs[i];
        drvPut32(out, 0x02014b50u);
        drvPut16(out, 20);
        drvPut16(out, 20);
        drvPut16(out, 0x0800);
        drvPut16(out, 0);
        drvPut16(out, dosTime);
        drvPut16(out, dosDate);
        drvPut32(out, r.crc);
        drvPut32(out, r.csize);
        drvPut32(out, r.usize);
        drvPut16(out, (unsigned int)r.name.size());
        drvPut16(out, 0);
        drvPut16(out, 0);
        drvPut16(out, 0);
        drvPut16(out, 0);
        drvPut32(out, 0);
        drvPut32(out, r.off);
        out += r.name;
    }
    unsigned int cdSize = (unsigned int)out.size() - cdStart;
    drvPut32(out, 0x06054b50u);
    drvPut16(out, 0);
    drvPut16(out, 0);
    drvPut16(out, (unsigned int)recs.size());
    drvPut16(out, (unsigned int)recs.size());
    drvPut32(out, cdSize);
    drvPut32(out, cdStart);
    drvPut16(out, 0);
}

static void drvZipWalk(std::string& out, std::vector<DrvZipRec>& recs,
                       const std::string& fsPath, const std::string& zipPath) {
    auto entries = listDriveEntries(fsPath);
    if (entries.empty()) {
        drvZipAdd(out, recs, zipPath, "", true);
        return;
    }
    for (size_t i = 0; i < entries.size(); i++) {
        const DrvEntry& e = entries[i];
        std::string zp = zipPath.empty() ? e.name : (zipPath + "/" + e.name);
        if (e.isDir) {
            drvZipWalk(out, recs, fsPath + e.name + "/", zp);
        } else {
            std::string data = common::readFile(fsPath + e.name);
            drvZipAdd(out, recs, zp, data, false);
        }
    }
}

// 收集某个相对路径（文件或目录）进 zip
static void drvZipCollect(std::string& out, std::vector<DrvZipRec>& recs,
                          const std::string& rootDir, const std::string& rel) {
    std::string fs = rootDir + rel;
    DWORD attrs = GetFileAttributesA(fs.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        std::string base = rel.substr(rel.find_last_of('/') + 1);
        std::vector<DrvEntry> sub = listDriveEntries(fs + "/");
        if (sub.empty()) {
            drvZipAdd(out, recs, base, "", true);
        } else {
            drvZipWalk(out, recs, fs + "/", base);
        }
    } else {
        std::string data = common::readFile(fs);
        drvZipAdd(out, recs, rel.substr(rel.find_last_of('/') + 1), data, false);
    }
}

// 解析换行/逗号分隔的路径列表
static std::vector<std::string> drvSplitPaths(const std::string& raw) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i <= raw.size(); i++) {
        char c = (i < raw.size()) ? raw[i] : '\n';
        if (c == '\n' || c == '\r') {
            std::string p = sanitizeRelPath(common::trim(cur));
            if (!p.empty()) out.push_back(p);
            cur.clear();
        } else cur += c;
    }
    return out;
}

static std::string drvBaseName(const std::string& rel) {
    size_t p = rel.find_last_of('/');
    return (p == std::string::npos) ? rel : rel.substr(p + 1);
}

// 查询串解析：/drive/xxx?a=1&b=2
static std::string drvQueryValue(const std::string& path, const std::string& key) {
    size_t q = path.find('?');
    if (q == std::string::npos) return "";
    std::string qs = path.substr(q + 1);
    std::vector<std::string> parts = common::split(qs, '&');
    for (size_t i = 0; i < parts.size(); i++) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        if (parts[i].substr(0, eq) == key) return common::urlDecode(parts[i].substr(eq + 1));
    }
    return "";
}

static bool drvCopyRecursive(const std::string& src, const std::string& dst) {
    DWORD attr = GetFileAttributesA(src.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        if (GetFileAttributesA(dst.c_str()) == INVALID_FILE_ATTRIBUTES)
            CreateDirectoryA(dst.c_str(), NULL);
        std::vector<DrvEntry> es = listDriveEntries(src + "/");
        for (size_t i = 0; i < es.size(); i++)
            if (!drvCopyRecursive(src + "/" + es[i].name, dst + "/" + es[i].name)) return false;
        return true;
    }
    std::string data = common::readFile(src);
    return common::writeFile(dst, data);
}

// 目录递归统计（文件数 / 目录数 / 总字节）
static void drvStatDir(const std::string& dir, long long& bytes, int& files, int& dirs) {
    std::vector<DrvEntry> es = listDriveEntries(dir);
    for (size_t i = 0; i < es.size(); i++) {
        if (es[i].isDir) { dirs++; drvStatDir(dir + es[i].name + "/", bytes, files, dirs); }
        else { files++; bytes += es[i].size; }
    }
}

static std::string drvUniqueTarget(const std::string& targetDir, const std::string& baseName) {
    std::string dst = targetDir + baseName;
    if (GetFileAttributesA(dst.c_str()) == INVALID_FILE_ATTRIBUTES) return dst;
    std::string stem = baseName, ext;
    size_t dot = baseName.find_last_of('.');
    if (dot != std::string::npos && dot > 0) { stem = baseName.substr(0, dot); ext = baseName.substr(dot); }
    for (int n = 2; n < 500; n++) {
        std::string cand = targetDir + stem + " (" + std::to_string(n) + ")" + ext;
        if (GetFileAttributesA(cand.c_str()) == INVALID_FILE_ATTRIBUTES) return cand;
    }
    return dst;
}

// ---------- 共享快捷方式（保存到我的云盘） ----------
static std::vector<std::map<std::string, std::string> > loadShortcuts() {
    std::vector<std::map<std::string, std::string> > out;
    std::string s = common::readFile("./data/drive_shortcuts.json");
    std::vector<std::string> lines = common::split(s, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        std::string l = common::trim(lines[i]);
        if (l.empty()) continue;
        auto o = common::jsonParseObject(l);
        if (!o["token"].empty()) out.push_back(o);
    }
    return out;
}
static void saveShortcuts(const std::vector<std::map<std::string, std::string> >& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) s += common::jsonStringify(v[i]) + "\n";
    common::writeFile("./data/drive_shortcuts.json", s);
}

std::string Handlers::handleShare(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId) {
    // /share/<token>            文件：保持旧行为（图片内联，便于外链引用）；文件夹：只读浏览页
    // /share/<token>/raw?p=x    内联预览 / 下载单个文件
    // /share/<token>/zip?p=x    打包下载文件夹
    // /share/<token>/shortcut   添加到我的云盘（快捷方式）
    std::string pure = path;
    size_t q = pure.find('?');
    if (q != std::string::npos) pure = pure.substr(0, q);
    std::string rest = pure.substr(std::string("/share/").size());
    std::string token = rest, op;
    size_t sl = rest.find('/');
    if (sl != std::string::npos) { token = rest.substr(0, sl); op = rest.substr(sl + 1); }

    auto shares = loadShares();
    std::string userId, fname, sharePerm = "view";
    for (size_t i = 0; i < shares.size(); i++)
        if (shares[i]["token"] == token) {
            userId = shares[i]["userId"];
            fname = sanitizeRelPath(shares[i]["filename"]);
            if (shares[i].count("perm")) sharePerm = shares[i]["perm"];
            break;
        }
    if (userId.empty() || fname.empty())
        return "<div class='alert alert-warning'>分享链接无效或已取消</div><a href='/' class='btn'>返回首页</a>";

    std::string ownerBase = "./files/" + userId + "/";
    std::string sharedFull = ownerBase + fname;
    DWORD sharedAttr = GetFileAttributesA(sharedFull.c_str());
    if (sharedAttr == INVALID_FILE_ATTRIBUTES)
        return "<div class='alert alert-warning'>内容已不存在（可能被删除或移动）</div><a href='/' class='btn'>返回首页</a>";
    bool sharedIsDir = (sharedAttr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    User* me = getCurrentUser(sessionId);
    User* owner = DataStore::instance().findUserById(userId);
    std::string ownerName = owner ? common::htmlEscape(owner->username) : "已注销用户";
    bool isOwner = (me && me->id == userId);

    // ---------- 快捷方式 ----------
    if (method == "POST" && op == "shortcut") {
        if (!me) return "<script>window.location.href='/login'</script>";
        std::string action = parseFormData(body, "action");
        bool added = false;
        std::vector<std::map<std::string, std::string> > sc = loadShortcuts();
        if (action == "remove") {
            std::vector<std::map<std::string, std::string> > keep;
            for (size_t i = 0; i < sc.size(); i++)
                if (!(sc[i]["userId"] == me->id && sc[i]["token"] == token)) keep.push_back(sc[i]);
            saveShortcuts(keep);
        } else {
            bool exists = false;
            for (size_t i = 0; i < sc.size(); i++)
                if (sc[i]["userId"] == me->id && sc[i]["token"] == token) exists = true;
            if (!exists) {
                std::map<std::string, std::string> e;
                e["userId"] = me->id;
                e["token"] = token;
                e["ownerId"] = userId;
                e["path"] = fname;
                e["name"] = drvBaseName(fname);
                e["isDir"] = sharedIsDir ? "1" : "0";
                e["time"] = common::getCurrentTimeStr();
                sc.push_back(e);
                saveShortcuts(sc);
            }
            added = true;
        }
        std::string msg = added ? "已添加到我的云盘（快捷方式）" : "已移除快捷方式";
        std::string c = "<div class='alert alert-success'>" + msg + "</div>"
                        "<a href='/drive' class='btn'>打开我的云盘</a> <a href='/share/" + token + "' class='btn btn-outline'>返回共享内容</a>";
        return renderTemplate("共享", c, me);
    }

    // ---------- 协作编辑（锁定 / 保存 / 新建 / 删除 / 锁列表） ----------
    // 只有 perm=edit 的文件夹分享才开放；锁是"礼貌性互斥"，保存时校验，避免互相覆盖。
    std::string collabRoot = sharedIsDir ? (sharedFull + "/") : "";
    auto jsonFail = [&](const std::string& msg) {
        respHeaders["Content-Type"] = "application/json; charset=utf-8";
        return "{\"ok\":false,\"msg\":" + common::jsonStringify(msg) + "}";
    };
    bool shareEditable = sharedIsDir && sharePerm == "edit" && me != NULL;

    if (op == "locks") {
        respHeaders["Content-Type"] = "application/json; charset=utf-8";
        std::lock_guard<std::mutex> lk(g_drvLockMutex);
        std::vector<DrvShareLock> locks = drvReadLocksUnlocked();
        std::vector<DrvShareLock> keepLocks;
        std::string arr = "[";
        bool firstLock = true;
        long long nowTs = (long long)time(NULL);
        for (size_t i = 0; i < locks.size(); i++) {
            bool sameToken = (locks[i].token == token);
            // 顺手清理：文件已不存在的锁不再展示，避免删了文件后“幽灵锁”
            bool exists = sameToken && sharedIsDir &&
                GetFileAttributesA((sharedFull + "/" + locks[i].path).c_str()) != INVALID_FILE_ATTRIBUTES;
            if (sameToken && !exists) continue;
            keepLocks.push_back(locks[i]);
            if (!sameToken) continue;
            if (!firstLock) arr += ",";
            firstLock = false;
            arr += "{\"path\":" + common::jsonStringify(locks[i].path) +
                   ",\"user\":" + common::jsonStringify(locks[i].userName) +
                   ",\"mine\":" + std::string(me && me->id == locks[i].userId ? "true" : "false") +
                   ",\"expire\":" + std::to_string(locks[i].expire) +
                   ",\"left\":" + std::to_string(locks[i].expire - nowTs) + "}";
        }
        if (keepLocks.size() != locks.size()) drvWriteLocksUnlocked(keepLocks);
        arr += "]";
        return "{\"ok\":true,\"locks\":" + arr + "}";
    }

    if (op == "lock" || op == "unlock" || op == "save" || op == "newfile" || op == "delete") {
        respHeaders["Content-Type"] = "application/json; charset=utf-8";
        if (!me) return jsonFail("请先登录");
        if (!shareEditable) return jsonFail("这个分享没有开启协作编辑（只读）");
        if (method != "POST") return jsonFail("需要 POST 请求");
        std::string rel = sanitizeRelPath(parseFormData(body, "p"));
        long long nowTs = (long long)time(NULL);
        std::string myName = common::htmlEscape(me->username);
        (void)myName;

        if (op == "lock" || op == "unlock") {
            if (rel.empty()) return jsonFail("缺少文件路径");
            std::string full = collabRoot + rel;
            bool isDirTarget = (GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES &&
                                (GetFileAttributesA(full.c_str()) & FILE_ATTRIBUTE_DIRECTORY));
            if (op == "lock") {
                if (GetFileAttributesA(full.c_str()) == INVALID_FILE_ATTRIBUTES)
                    return jsonFail("文件不存在");
                if (isDirTarget) return jsonFail("文件夹不能加锁");
            }
            std::lock_guard<std::mutex> lk(g_drvLockMutex);
            std::vector<DrvShareLock> locks = drvReadLocksUnlocked();
            int idx = drvFindLockIdx(locks, token, rel);
            if (op == "unlock") {
                if (idx >= 0 && (locks[idx].userId == me->id || me->id == userId)) {
                    locks.erase(locks.begin() + idx);
                    drvWriteLocksUnlocked(locks);
                    return "{\"ok\":true}";
                }
                return "{\"ok\":true,\"msg\":\"没有你的锁\"}";
            }
            if (idx >= 0 && locks[idx].userId != me->id) {
                return "{\"ok\":false,\"locked\":true,\"holder\":" +
                       common::jsonStringify(locks[idx].userName) + ",\"left\":" +
                       std::to_string(locks[idx].expire - nowTs) + "}";
            }
            DrvShareLock k;
            k.token = token; k.path = rel; k.userId = me->id;
            k.userName = me->username; k.expire = nowTs + DRV_LOCK_SEC;
            if (idx >= 0) locks[idx] = k; else locks.push_back(k);
            drvWriteLocksUnlocked(locks);
            return "{\"ok\":true,\"expire\":" + std::to_string(k.expire) + ",\"left\":" +
                   std::to_string(DRV_LOCK_SEC) + "}";
        }

        if (op == "newfile") {
            std::string name = sanitizeFilename(parseFormData(body, "name"));
            std::string joined = rel.empty() ? name : (rel + "/" + name);
            std::string nrel = sanitizeRelPath(joined);
            if (nrel.empty()) return jsonFail("文件名不合法");
            std::string full = collabRoot + nrel;
            if (GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES)
                return jsonFail("同名文件或文件夹已存在");
            size_t sp = full.find_last_of("/\\");
            if (sp != std::string::npos) common::createDirectory(full.substr(0, sp));
            std::string body0 = parseFormData(body, "content");
            if (!common::writeFile(full, body0)) return jsonFail("创建失败（可能没有写入权限）");
            std::lock_guard<std::mutex> lk(g_drvLockMutex);
            std::vector<DrvShareLock> locks = drvReadLocksUnlocked();
            int idx = drvFindLockIdx(locks, token, nrel);
            DrvShareLock k;
            k.token = token; k.path = nrel; k.userId = me->id;
            k.userName = me->username; k.expire = nowTs + DRV_LOCK_SEC;
            if (idx >= 0) locks[idx] = k; else locks.push_back(k);
            drvWriteLocksUnlocked(locks);
            return "{\"ok\":true,\"path\":" + common::jsonStringify(nrel) + ",\"rev\":" +
                   common::jsonStringify(drvFileRev(full)) + "}";
        }

        if (op == "delete") {
            if (rel.empty()) return jsonFail("缺少文件路径");
            std::string full = collabRoot + rel;
            DWORD at = GetFileAttributesA(full.c_str());
            if (at == INVALID_FILE_ATTRIBUTES) return jsonFail("文件不存在");
            if (at & FILE_ATTRIBUTE_DIRECTORY) return jsonFail("暂不支持在共享页删除文件夹");
            {
                std::lock_guard<std::mutex> lk(g_drvLockMutex);
                std::vector<DrvShareLock> locks = drvReadLocksUnlocked();
                int idx = drvFindLockIdx(locks, token, rel);
                if (idx >= 0 && locks[idx].userId != me->id)
                    return "{\"ok\":false,\"locked\":true,\"holder\":" +
                           common::jsonStringify(locks[idx].userName) + "}";
                if (!DeleteFileA(full.c_str())) return jsonFail("删除失败（文件可能正被占用）");
                if (idx >= 0) { locks.erase(locks.begin() + idx); drvWriteLocksUnlocked(locks); }
            }
            return "{\"ok\":true}";
        }

        // save
        if (rel.empty()) return jsonFail("缺少文件路径");
        std::string full = collabRoot + rel;
        DWORD at = GetFileAttributesA(full.c_str());
        if (at == INVALID_FILE_ATTRIBUTES) return jsonFail("文件不存在（可能已被删除）");
        if (at & FILE_ATTRIBUTE_DIRECTORY) return jsonFail("不能把内容写进文件夹");
        if (driveKindOf(rel, false) != "text") return jsonFail("只支持编辑文本 / 代码类文件");
        std::string content = parseFormData(body, "content");
        if (content.size() > 2 * 1024 * 1024) return jsonFail("内容过大（2MB 上限）");
        std::string rev = parseFormData(body, "rev");
        std::string curRev = drvFileRev(full);
        {
            std::lock_guard<std::mutex> lk(g_drvLockMutex);
            std::vector<DrvShareLock> locks = drvReadLocksUnlocked();
            int idx = drvFindLockIdx(locks, token, rel);
            if (idx >= 0 && locks[idx].userId != me->id)
                return "{\"ok\":false,\"locked\":true,\"holder\":" +
                       common::jsonStringify(locks[idx].userName) + ",\"left\":" +
                       std::to_string(locks[idx].expire - nowTs) + "}";
            if (!rev.empty() && rev != curRev) {
                return "{\"ok\":false,\"conflict\":true,\"rev\":" + common::jsonStringify(curRev) +
                       ",\"content\":" + common::jsonStringify(common::readFile(full)) + "}";
            }
            if (!common::writeFile(full, content)) return jsonFail("写入失败");
            DrvShareLock k;
            k.token = token; k.path = rel; k.userId = me->id;
            k.userName = me->username; k.expire = nowTs + DRV_LOCK_SEC;
            if (idx >= 0) locks[idx] = k; else locks.push_back(k);
            drvWriteLocksUnlocked(locks);
        }
        return "{\"ok\":true,\"rev\":" + common::jsonStringify(drvFileRev(full)) +
               ",\"size\":" + std::to_string(content.size()) + "}";
    }

    if (op == "edit") {
        if (!me) return "<script>window.location.href='/login'</script>";
        std::string rel = sanitizeRelPath(drvQueryValue(path, "p"));
        if (!shareEditable)
            return renderTemplate("只读分享",
                "<div class='alert alert-warning'>这个分享是只读的：只有分享者开启「允许协作编辑」后才能修改文件。</div>"
                "<a class='btn' href='/share/" + token + "'>返回共享内容</a>", me);
        std::string full = collabRoot + rel;
        DWORD eat = GetFileAttributesA(full.c_str());
        if (rel.empty() || eat == INVALID_FILE_ATTRIBUTES || (eat & FILE_ATTRIBUTE_DIRECTORY))
            return renderTemplate("文件不存在", "<div class='alert alert-error'>文件不存在</div>"
                                  "<a class='btn' href='/share/" + token + "'>返回</a>", me);
        if (driveKindOf(rel, false) != "text")
            return renderTemplate("无法编辑", "<div class='alert alert-warning'>这个文件不是文本类型，无法在线编辑。</div>"
                                  "<a class='btn' href='/share/" + token + "'>返回</a>", me);
        std::string content = common::readFile(full);
        if (!common::isValidUtf8(content)) content = common::toUtf8FromSystem(content);
        if (content.size() > 2 * 1024 * 1024) content = content.substr(0, 2 * 1024 * 1024);
        std::string rev = drvFileRev(full);
        std::string parentRel;
        size_t psl = rel.find_last_of('/');
        if (psl != std::string::npos) parentRel = rel.substr(0, psl);

        std::string c;
        c += "<style>"
             ".sh-ed{display:flex;flex-direction:column;gap:10px}"
             ".sh-ed-top{display:flex;align-items:center;gap:8px;flex-wrap:wrap;background:var(--card,#fff);"
             "border:1px solid var(--border,#e3e9f0);border-radius:14px;padding:9px 12px;font-size:13.5px}"
             ".sh-ed-top b{color:var(--strong,#1c232b);font-weight:700}"
             ".sh-ed-spacer{flex:1}"
             ".sh-lock{font-size:12.5px;color:#8792a0;display:inline-flex;align-items:center;gap:6px}"
             ".sh-lock.ok{color:#27ae60}.sh-lock.err{color:#e0553f}"
             ".sh-ed textarea{width:100%;min-height:calc(100vh - 300px);font-family:Consolas,'Cascadia Mono',Menlo,monospace;"
             "font-size:13.5px;line-height:1.6;border:1px solid var(--border,#e3e9f0);border-radius:14px;padding:14px 16px;"
             "resize:vertical;background:var(--surface,#fff);color:var(--text,#2f3640);tab-size:4}"
             ".sh-ed textarea[readonly]{background:#f7fafd;color:#8792a0}"
             ".sh-ed-status{font-size:12.5px;color:var(--muted,#8792a0);display:flex;gap:12px;flex-wrap:wrap}"
             ".sh-tip{font-size:12px;color:var(--muted,#8792a0)}"
             "</style>";
        c += "<div class='sh-ed'>";
        c += "<div class='sh-ed-top'>"
             "<a class='btn btn-sm btn-outline' href='/share/" + token + (parentRel.empty() ? "" : ("?p=" + common::urlEncode(parentRel))) + "'>&larr; 返回</a>"
             "<b>&#128221; " + common::htmlEscape(rel) + "</b>"
             "<span class='sh-tip'>保存后其他协作者刷新即可看到</span>"
             "<span class='sh-ed-spacer'></span>"
             "<span class='sh-lock' id='lockInfo'>获取编辑锁…</span>"
             "<button class='btn btn-sm' id='saveBtn'>保存 (Ctrl+S)</button>"
             "<button class='btn btn-sm btn-outline' id='unlockBtn'>释放锁</button>"
             "</div>";
        c += "<textarea id='ed' spellcheck='false'>" + common::htmlEscape(content) + "</textarea>";
        c += "<div class='sh-ed-status'><span id='st'>已加载 · " + std::to_string(content.size()) +
             " 字节</span><span id='stSaved'></span></div>";
        c += "</div>";

        std::string js = R"JS(
(function(){
  var TOKEN=__TOKEN__, P=__PATH__, REV=__REV__;
  var ed=document.getElementById('ed'), info=document.getElementById('lockInfo');
  var st=document.getElementById('st'), stSaved=document.getElementById('stSaved');
  var lockedByOther=false;
  function setInfo(t,cls){ info.textContent=t; info.className='sh-lock'+(cls?(' '+cls):''); }
  function post(op,data){
    var fd=new URLSearchParams(data);
    return fetch('/share/'+TOKEN+'/'+op,{method:'POST',credentials:'same-origin',
      headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','X-Requested-With':'fetch'},
      body:fd}).then(function(r){ return r.json(); });
  }
  function acquire(){
    return post('lock',{p:P}).then(function(j){
      if(j.ok){ setInfo('🔒 你已锁定此文件（10 分钟无操作自动释放，保存会续期）','ok'); ed.readOnly=false; return true; }
      if(j.locked){ lockedByOther=true; ed.readOnly=true;
        setInfo('🔒 '+j.holder+' 正在编辑（约 '+(j.left>=60?(Math.ceil(j.left/60)+' 分钟'):(j.left+' 秒'))+'后释放）','err');
        st.textContent='当前只读：请等对方释放锁，或让分享者协调';
        return false; }
      setInfo('⚠ '+(j.msg||'无法锁定'),'err'); return false;
    }).catch(function(){ setInfo('⚠ 网络错误','err'); return false; });
  }
  function save(){
    if(lockedByOther){ alert('文件正被其他人锁定，无法保存'); return; }
    stSaved.textContent='保存中…';
    post('save',{p:P,content:ed.value,rev:REV}).then(function(j){
      if(j.conflict){
        if(confirm('文件已被其他人修改过（版本已变化）。\n仍要用你当前的内容覆盖吗？覆盖后对方的修改会丢失。')){
          post('save',{p:P,content:ed.value,rev:''}).then(function(j2){
            if(!j2.ok){ stSaved.textContent=''; alert(j2.msg||'保存失败'); return; }
            REV=j2.rev; stSaved.textContent='已保存 '+new Date().toLocaleTimeString();
            setInfo('🔒 已锁定并保存','ok');
          });
        } else { stSaved.textContent=''; }
        return;
      }
      if(!j.ok){ stSaved.textContent=''; alert(j.msg||'保存失败'); return; }
      REV=j.rev;
      stSaved.textContent='已保存 '+new Date().toLocaleTimeString()+' · '+j.size+' 字节';
      setInfo('🔒 已锁定并保存','ok');
    }).catch(function(){ stSaved.textContent=''; alert('网络错误'); });
  }
  document.getElementById('saveBtn').onclick=save;
  document.getElementById('unlockBtn').onclick=function(){
    post('unlock',{p:P}).then(function(){ setInfo('🔓 已释放锁','ok'); });
  };
  document.addEventListener('keydown',function(e){
    if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='s'){ e.preventDefault(); save(); }
  });
  ed.addEventListener('input',function(){ stSaved.textContent='未保存的修改'; });
  ed.addEventListener('keydown',function(e){
    if(e.key==='Tab'){ e.preventDefault();
      var s=ed.selectionStart, t=ed.selectionEnd, v=ed.value;
      ed.value=v.slice(0,s)+'    '+v.slice(t); ed.selectionStart=ed.selectionEnd=s+4;
    }
  });
  acquire();
})();
)JS";
        js = common::replaceAll(js, "__TOKEN__", common::jsonStringify(token));
        js = common::replaceAll(js, "__PATH__", common::jsonStringify(rel));
        js = common::replaceAll(js, "__REV__", common::jsonStringify(rev));
        c += "<script>" + js + "</script>";
        return renderTemplate("编辑 · " + drvBaseName(rel), c, me);
    }

    // ---------- 文件级：内联 / 下载 ----------
    if (op == "raw" && !sharedIsDir) {
        std::string mime = driveMimeOf(fname);
        respHeaders["Content-Type"] = mime;
        if (mime == "image/svg+xml") respHeaders["Content-Security-Policy"] = "sandbox; default-src 'none'";
        if (drvQueryValue(path, "download") == "1" || mime == "application/octet-stream") {
            respHeaders["Content-Disposition"] = "attachment; filename=\"" + sanitizeFilename(drvBaseName(fname)) + "\"";
        } else {
            respHeaders["Content-Disposition"] = "inline";
        }
        return common::readFile(sharedFull);
    }

    // ---------- 目录级 ----------
    std::string sub = sanitizeRelPath(drvQueryValue(path, "p"));
    std::string rootDir = sharedIsDir ? (sharedFull + "/") : "";
    if (!sub.empty()) {
        if (!sharedIsDir) sub.clear();
        else if (sub.find("..") != std::string::npos) sub.clear();
    }
    std::string curDir = sharedIsDir ? (rootDir + (sub.empty() ? "" : sub + "/")) : "";
    // 校验：浏览必须是目录；raw / zip 允许文件或目录
    if (sharedIsDir && !sub.empty()) {
        std::string targetFull = rootDir + sub;
        DWORD ta = GetFileAttributesA(targetFull.c_str());
        if (ta == INVALID_FILE_ATTRIBUTES || ((op.empty()) && !(ta & FILE_ATTRIBUTE_DIRECTORY)))
            return "<div class='alert alert-warning'>路径不存在</div><a href='/share/" + token + "' class='btn'>回到共享根目录</a>";
    }

    if (op == "raw") {
        std::string full = rootDir + sub;
        if (GetFileAttributesA(full.c_str()) == INVALID_FILE_ATTRIBUTES)
            return "<div class='alert alert-error'>文件不存在</div>";
        std::string base = drvBaseName(sub);
        std::string mime = driveMimeOf(base);
        respHeaders["Content-Type"] = mime;
        if (mime == "image/svg+xml") respHeaders["Content-Security-Policy"] = "sandbox; default-src 'none'";
        if (drvQueryValue(path, "download") == "1" || mime == "application/octet-stream")
            respHeaders["Content-Disposition"] = "attachment; filename=\"" + sanitizeFilename(base) + "\"";
        else
            respHeaders["Content-Disposition"] = "inline";
        return common::readFile(full);
    }
    if (op == "zip") {
        std::string zipTarget = sub.empty() ? fname : sub;
        std::string zipName = drvBaseName(zipTarget) + ".zip";
        std::string out;
        std::vector<DrvZipRec> recs;
        if (sharedIsDir && sub.empty()) {
            drvZipWalk(out, recs, rootDir, drvBaseName(fname));
        } else {
            drvZipCollect(out, recs, rootDir, sub);
        }
        if (recs.empty()) { respHeaders["Content-Type"] = "text/plain; charset=utf-8"; return "没有可打包的内容"; }
        drvZipEnd(out, recs);
        respHeaders["Content-Type"] = "application/zip";
        respHeaders["Content-Disposition"] = "attachment; filename=\"" + sanitizeFilename(zipName) + "\"; filename*=UTF-8''" + common::urlEncode(zipName);
        return out;
    }

    // 共享的是单个文件：保持旧的内联/附件行为（外部 Markdown 可直接引用）
    if (!sharedIsDir && sub.empty() && op.empty()) {
        std::string base = drvBaseName(fname);
        std::string mime = common::getImageMimeType(sharedFull);
        if (mime.find("image/") == 0) {
            respHeaders["Content-Type"] = mime;
            respHeaders["Content-Disposition"] = "inline";
            return common::readFile(sharedFull);
        }
        respHeaders["Content-Type"] = driveMimeOf(base);
        if (respHeaders["Content-Type"] == "application/octet-stream")
            respHeaders["Content-Disposition"] = "attachment; filename=\"" + sanitizeFilename(base) + "\"";
        else
            respHeaders["Content-Disposition"] = "inline";
        return common::readFile(sharedFull);
    }

    // ---------- 浏览页 ----------
    std::vector<DrvEntry> entries = sharedIsDir ? listDriveEntries(curDir) : std::vector<DrvEntry>();
    long long totalBytes = 0; int fileCount = 0, dirCount = 0;
    if (sharedIsDir) drvStatDir(rootDir, totalBytes, fileCount, dirCount);
    else totalBytes = getFileSizeBytes(sharedFull); fileCount = 1;

    // 已添加到我的云盘？
    bool hasShortcut = false;
    if (me) {
        std::vector<std::map<std::string, std::string> > sc = loadShortcuts();
        for (size_t i = 0; i < sc.size(); i++)
            if (sc[i]["userId"] == me->id && sc[i]["token"] == token) hasShortcut = true;
    }

    std::string crumb = "<a class='drv-crumb-item' href='/share/" + token + "'>" +
                        (sharedIsDir ? "&#128194; " : "&#128196; ") + common::htmlEscape(drvBaseName(fname)) + "</a>";    std::string cur;
    std::vector<std::string> segs = common::split(sub, '/');
    for (size_t i = 0; i < segs.size(); i++) {
        if (segs[i].empty()) continue;
        if (!cur.empty()) cur += "/";
        cur += segs[i];
        crumb += "<span class='drv-crumb-sep'>/</span><a class='drv-crumb-item' href='/share/" + token + "?p=" + common::urlEncode(cur) + "'>" + common::htmlEscape(segs[i]) + "</a>";
    }

    // 协作状态：可编辑 + 当前锁（谁在编哪个文件）
    bool editable = sharedIsDir && me && sharePerm == "edit";
    std::vector<DrvShareLock> browseLocks;
    if (sharedIsDir) {
        std::lock_guard<std::mutex> lk(g_drvLockMutex);
        browseLocks = drvReadLocksUnlocked();
    }
    auto lockHolderOf = [&](const std::string& p) -> std::string {
        for (size_t i = 0; i < browseLocks.size(); i++)
            if (browseLocks[i].token == token && browseLocks[i].path == p) {
                if (GetFileAttributesA((sharedFull + "/" + p).c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                return browseLocks[i].userName;
            }
        return "";
    };

    std::string c = "<div class='drv'>";
    c += "<div class='drv-quota'><div class='drv-quota-top'><span>&#128279; 共享文件夹 · " + common::htmlEscape(drvBaseName(fname)) + "</span>"
         "<span class='drv-quota-num'>来自 " + ownerName + " · " + humanSize(totalBytes) + "</span></div>"
         "<div class='drv-quota-bar'><i style='width:100%'></i></div></div>";
    c += "<div class='drv-crumb'>" + crumb + "</div>";
    c += "<div class='drv-bar'>";
    c += "<a class='btn btn-sm' href='/share/" + token + "/zip" + (sub.empty() ? "" : "?p=" + common::urlEncode(sub)) + "'>\u2193 打包下载" + (sub.empty() ? "全部" : "此文件夹") + "</a>";
    if (!me) {
        c += "<a class='btn btn-sm btn-outline' href='/login'>登录后可添加到我的云盘</a>";
    } else if (!isOwner) {
        c += "<form method='POST' action='/share/" + token + "/shortcut' style='display:inline;'>"
             "<input type='hidden' name='action' value='" + std::string(hasShortcut ? "remove" : "add") + "'/>"
             "<input type='submit' value='" + std::string(hasShortcut ? "\u2716 \u79fb\u9664\u5feb\u6377\u65b9\u5f0f" : "\U0001f4cc \u6dfb\u52a0\u5230\u6211\u7684\u4e91\u76d8") + "' class='btn btn-sm " +
             std::string(hasShortcut ? "btn-danger" : "") + "'/></form>";
    } else {
        c += "<a class='btn btn-sm btn-outline' href='/drive'>这是你自己的分享 · 去云盘管理</a>";
    }
    c += "<span class='drv-spacer'></span>";
    if (editable) {
        c += "<button class='btn btn-sm' onclick='shNewFile()'>＋ 新建文件</button>"
             "<span class='drv-picker'>\U0001f513 协作编辑已开启 · 编辑前需先锁定文件</span>";
    } else {
        c += "<span class='drv-picker'>\U0001f512 只读分享 · 其他人无法修改内容</span>";
    }
    c += "</div>";

    if (entries.empty()) {
        c += "<div class='drv-empty'>这个文件夹是空的</div>";
    } else {
        c += "<div class='drv-list list'>";
        for (size_t i = 0; i < entries.size(); i++) {
            const DrvEntry& e = entries[i];
            std::string childRel = sub.empty() ? e.name : (sub + "/" + e.name);
            if (e.isDir) {
                c += "<div class='drv-row'><span class='drv-check'></span><span class='drv-ico'>\U0001f4c1</span>"
                     "<span class='drv-name'><a href='/share/" + token + "?p=" + common::urlEncode(childRel) + "' style='color:inherit;text-decoration:none;'>" + common::htmlEscape(e.name) + "</a></span>"
                     "<span class='drv-size'>—</span><span class='drv-time'>" + common::htmlEscape(e.mtime) + "</span>"
                     "<span class='drv-acts'><a class='btn btn-sm btn-outline' href='/share/" + token + "?p=" + common::urlEncode(childRel) + "'>打开</a>"
                     "<a class='btn btn-sm btn-outline' href='/share/" + token + "/zip?p=" + common::urlEncode(childRel) + "'>\u2193</a></span></div>";
            } else {
                std::string kind = driveKindOf(e.name, false);
                std::string ic = kind == "image" ? "\U0001f5bc\ufe0f" : (kind == "video" ? "\U0001f3ac" : (kind == "audio" ? "\U0001f3b5" : (kind == "pdf" ? "\U0001f4d5" : (kind == "text" ? "\U0001f4c4" : "\U0001f4e6"))));
                std::string rawU = "/share/" + token + "/raw?p=" + common::urlEncode(childRel);
                std::string lockBy = lockHolderOf(childRel);
                std::string lockBadge = lockBy.empty() ? "" :
                    ("<span class='drv-lockbadge' title='已被锁定'>\U0001f512 " + common::htmlEscape(lockBy) + "</span>");
                std::string editBtn = "";
                if (editable && kind == "text")
                    editBtn = "<a class='btn btn-sm' href='/share/" + token + "/edit?p=" + common::urlEncode(childRel) + "'>✏ 编辑</a>";
                c += "<div class='drv-row'><span class='drv-check'></span><span class='drv-ico'>" + ic + "</span>"
                     "<span class='drv-name'><a href='" + rawU + "' target='_blank' style='color:inherit;text-decoration:none;'>" + common::htmlEscape(e.name) + "</a>" + lockBadge + "</span>"
                     "<span class='drv-size'>" + humanSize(e.size) + "</span><span class='drv-time'>" + common::htmlEscape(e.mtime) + "</span>"
                     "<span class='drv-acts'>" + editBtn + "<a class='btn btn-sm btn-outline' href='" + rawU + "' target='_blank'>预览</a>"
                     "<a class='btn btn-sm btn-outline' href='" + rawU + "&download=1'>\u2193</a></span></div>";
            }
        }
        c += "</div>";
    }
    c += "<div class='drv-count'>\U0001f4c1 文件夹 " + std::to_string(dirCount) + " 个 · \U0001f4c4 文件 " + std::to_string(fileCount) + " 个</div>";
    if (editable) {
        c += "<style>.drv-lockbadge{display:inline-block;margin-left:8px;font-size:12px;color:#e67e22;background:rgba(230,126,34,.10);border:1px solid rgba(230,126,34,.35);border-radius:999px;padding:1px 8px}</style>";
        std::string js = R"JS(
function shNewFile(){
  var n=prompt('新建文件名（含扩展名，例如 main.cpp / notes.md）：','');
  if(!n) return;
  var fd=new URLSearchParams({p:__SUB__,name:n,content:''});
  fetch('/share/'+__TOKEN__+'/newfile',{method:'POST',credentials:'same-origin',
    headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','X-Requested-With':'fetch'},
    body:fd}).then(function(r){return r.json();}).then(function(j){
      if(!j.ok){ alert(j.msg||'创建失败'); return; }
      location.href='/share/'+__TOKEN__+'/edit?p='+encodeURIComponent(j.path);
    }).catch(function(){ alert('网络错误'); });
}
)JS";
        js = common::replaceAll(js, "__SUB__", common::jsonStringify(sub));
        js = common::replaceAll(js, "__TOKEN__", common::jsonStringify(token));
        c += "<script>" + js + "</script>";
    }
    c += "</div>";
    return renderTemplate("共享内容", c, me);
}

// ============================================================
// 多模型提供方管理
// ============================================================
std::string Handlers::handleAdminAIProviders(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";

    if (method == "POST") {
        std::string action = parseFormData(body, "action");
        std::vector<aiprov::Provider> all = aiprov::load();
        if (action == "add") {
            aiprov::Provider p;
            p.id = common::trim(parseFormData(body, "id"));
            p.name = common::trim(parseFormData(body, "name"));
            p.kind = common::trim(parseFormData(body, "kind"));
            p.apiMode = common::trim(parseFormData(body, "apiMode"));
            if (p.apiMode != "responses") p.apiMode = "chat";
            p.baseUrl = common::trim(parseFormData(body, "baseUrl"));
            p.apiKey = common::trim(parseFormData(body, "apiKey"));
            p.freeOnly = parseFormData(body, "freeOnly") == "1";
            p.enabled = true;
            if (p.id.empty()) p.id = "p" + std::to_string((long long)time(NULL));
            for (size_t i = 0; i < p.id.size(); i++) {
                char c = p.id[i];
                if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) p.id[i] = '_';
            }
            while (!p.baseUrl.empty() && p.baseUrl[p.baseUrl.size() - 1] == '/') p.baseUrl.erase(p.baseUrl.size() - 1);
            if (p.name.empty()) p.name = p.id;
            if (p.kind.empty()) p.kind = "openai";
            bool exists = false;
            for (size_t i = 0; i < all.size(); i++) if (all[i].id == p.id) exists = true;
            if (!exists && !p.baseUrl.empty()) {
                all.push_back(p);
                aiprov::save(all);
                // 添加后直接进入扫描页，询问要启用哪些模型
                return "<script>window.location.href='/admin/ai/providers?scan=" + common::urlEncode(p.id) + "'</script>";
            }
            return "<script>window.location.href='/admin/ai/providers'</script>";
        }
        if (action == "save") {
            std::string id = parseFormData(body, "id");
            for (size_t i = 0; i < all.size(); i++) {
                if (all[i].id != id) continue;
                all[i].name = common::trim(parseFormData(body, "name"));
                std::string base = common::trim(parseFormData(body, "baseUrl"));
                while (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
                if (!base.empty()) all[i].baseUrl = base;
                std::string key = common::trim(parseFormData(body, "apiKey"));
                if (!key.empty() && key.find("****") == std::string::npos) all[i].apiKey = key;
                std::string kind = common::trim(parseFormData(body, "kind"));
                if (!kind.empty()) all[i].kind = kind;
                std::string mode = common::trim(parseFormData(body, "apiMode"));
                all[i].apiMode = (mode == "responses") ? "responses" : "chat";
                all[i].enabled = parseFormData(body, "enabled") == "1";
                all[i].freeOnly = parseFormData(body, "freeOnly") == "1";
            }
            aiprov::save(all);
            return "<script>window.location.href='/admin/ai/providers'</script>";
        }
        if (action == "savemodels") {
            std::string id = parseFormData(body, "id");
            std::string field = parseFormData(body, "models");
            for (size_t i = 0; i < all.size(); i++)
                if (all[i].id == id) all[i].models = aiprov::parseModels(field);
            aiprov::save(all);
            return "<script>window.location.href='/admin/ai/providers'</script>";
        }
        if (action == "delete") {
            std::string id = parseFormData(body, "id");
            std::vector<aiprov::Provider> keep;
            for (size_t i = 0; i < all.size(); i++) if (all[i].id != id) keep.push_back(all[i]);
            aiprov::save(keep);
            return "<script>window.location.href='/admin/ai/providers'</script>";
        }
        if (action == "toggle") {
            std::string id = parseFormData(body, "id");
            for (size_t i = 0; i < all.size(); i++) if (all[i].id == id) all[i].enabled = !all[i].enabled;
            aiprov::save(all);
            return "<script>window.location.href='/admin/ai/providers'</script>";
        }
        return "<script>window.location.href='/admin/ai/providers'</script>";
    }

    aiprov::ensureSeed();
    std::vector<aiprov::Provider> all = aiprov::load();
    std::string scanId = common::trim(queryParam(path, "scan"));
    std::string c;
    c += adminNavbar(user);
    c += "<div class='action-bar' style='display:flex;gap:8px;justify-content:flex-end;margin-bottom:10px;'>"
         "<a class='btn btn-sm' href='/admin/ai'>← 返回 AI 管理</a>"
         "<a class='btn btn-sm' href='/admin/ai/plans'>套餐管理 →</a></div>";
    c += "<div class='alert alert-info'>支持多家 OpenAI 兼容服务（DeepSeek / OpenRouter / 自建中转）。"
         "<b>API Key 只保存在服务端</b>，页面只显示掩码；<b>扫描按提供方分开进行</b>，在小窗里搜索并勾选模型。"
         "<b>freeOnly</b> 的提供方只会留下免费模型。</div>";

    // 提供方列表
    c += "<div class='card'><h3>🔌 提供方</h3><table style='width:100%;font-size:13.5px;'><thead><tr>"
         "<th>名称</th><th>Base URL</th><th>API Key</th><th>模型</th><th>状态</th><th style='width:260px;'>操作</th>"
         "</tr></thead><tbody>";
    for (size_t i = 0; i < all.size(); i++) {
        const aiprov::Provider& p = all[i];
        c += "<tr><td><b>" + common::htmlEscape(p.name) + "</b><div style='color:var(--muted);font-size:12px;'>" +
             common::htmlEscape(p.id) + " · " + common::htmlEscape(p.kind) +
             (p.freeOnly ? " · 仅免费" : "") + "</div></td>"
             "<td style='max-width:240px;word-break:break-all;'>" + common::htmlEscape(p.baseUrl) + "</td>"
             "<td style='font-family:monospace;'>" + common::htmlEscape(aiprov::maskKey(p.apiKey)) + "</td>"
             "<td><span id='cnt-" + common::htmlEscape(p.id) + "'>" + std::to_string(p.models.size()) + " 个</span></td>"
             "<td>" + std::string(p.enabled ? "<span style='color:#27ae60;'>启用</span>" : "<span style='color:#95a5a6;'>停用</span>") + "</td>"
             "<td><button type='button' class='btn btn-sm' onclick=\"openScan('" + common::htmlEscape(p.id) + "','" +
             common::htmlEscape(p.name) + "')\">🔎 扫描选模型</button> "
             "<form method='POST' style='display:inline;'><input type='hidden' name='id' value='" + common::htmlEscape(p.id) + "'/>"
             "<button class='btn btn-sm btn-outline' name='action' value='toggle' type='submit'>" + std::string(p.enabled ? "停用" : "启用") + "</button></form> "
             "<form method='POST' style='display:inline;' onsubmit=\"return confirm('确定删除这个提供方？')\">"
             "<input type='hidden' name='id' value='" + common::htmlEscape(p.id) + "'/>"
             "<button class='btn btn-sm btn-danger' name='action' value='delete' type='submit'>删除</button></form></td></tr>";
    }
    if (all.empty()) c += "<tr><td colspan='6' class='empty-td'>还没有提供方</td></tr>";
    c += "</tbody></table>";
    // 已启用的模型（直接读配置，不请求上游）
    for (size_t i = 0; i < all.size(); i++) {
        const aiprov::Provider& p = all[i];
        if (p.models.empty()) continue;
        c += "<div style='margin-top:10px;padding-top:10px;border-top:1px dashed var(--border);font-size:12.5px;'>"
             "<b>" + common::htmlEscape(p.name) + "</b> 已启用：";
        for (size_t k = 0; k < p.models.size(); k++) {
            c += "<span class='pill' style='background:var(--surface-2);color:var(--text);margin:2px;'>" +
                 common::htmlEscape(p.models[k].id) + "</span> ";
        }
        c += "</div>";
    }
    c += "</div>";

    // 编辑 + 新增
    for (size_t i = 0; i < all.size(); i++) {
        const aiprov::Provider& p = all[i];
        std::string curKind = p.kind.empty() ? "openai" : p.kind;
        const char* kinds[][2] = {{"openai", "OpenAI 兼容"}, {"deepseek", "DeepSeek 官方"},
                                  {"openrouter", "OpenRouter"}, {"openai-compatible", "其他中转"}};
        std::string kindOpts;
        bool kindHit = false;
        for (int k = 0; k < 4; k++) {
            bool sel = (curKind == kinds[k][0]);
            if (sel) kindHit = true;
            kindOpts += std::string("<option value='") + kinds[k][0] + "'" + (sel ? " selected" : "") + ">" + kinds[k][1] + "</option>";
        }
        if (!kindHit) kindOpts = "<option value='" + common::htmlEscape(curKind) + "' selected>" + common::htmlEscape(curKind) + "</option>" + kindOpts;
        bool isResp = (p.apiMode == "responses");
        c += "<details class='card' style='padding:14px 18px;'><summary style='cursor:pointer;font-weight:700;'>✏️ 编辑 " +
             common::htmlEscape(p.name) + " <span style='color:var(--muted);font-size:12px;font-weight:400;'>" +
             (isResp ? "Responses API" : "Chat Completions") + "</span></summary><form method='POST' style='margin-top:12px;'>"
             "<input type='hidden' name='action' value='save'/><input type='hidden' name='id' value='" + common::htmlEscape(p.id) + "'/>"
             "<div style='display:flex;gap:12px;flex-wrap:wrap;'>"
             "<div class='form-group' style='flex:1;min-width:180px;'><label>名称</label><input name='name' value='" + common::htmlEscape(p.name) + "'/></div>"
             "<div class='form-group' style='flex:1;min-width:150px;'><label>类型</label><select name='kind'>" + kindOpts + "</select></div>"
             "<div class='form-group' style='flex:2;min-width:240px;'><label>Base URL</label><input name='baseUrl' value='" + common::htmlEscape(p.baseUrl) + "'/></div>"
             "<div class='form-group' style='flex:1;min-width:200px;'><label>API Key（留空不变）</label><input name='apiKey' placeholder='" + common::htmlEscape(aiprov::maskKey(p.apiKey)) + "' autocomplete='off'/></div>"
             "</div><div style='display:flex;gap:12px;align-items:center;flex-wrap:wrap;'>"
             "<div class='form-group' style='min-width:240px;margin:0;'><label>接口协议</label><select name='apiMode'>"
             "<option value='chat'" + std::string(isResp ? "" : " selected") + ">Chat Completions（/chat/completions）</option>"
             "<option value='responses'" + std::string(isResp ? " selected" : "") + ">Responses API（/responses）</option>"
             "</select></div>"
             "<label class='ai-chk'><input type='checkbox' name='enabled' value='1'" + std::string(p.enabled ? " checked" : "") + "/> 启用</label>"
             "<label class='ai-chk'><input type='checkbox' name='freeOnly' value='1'" + std::string(p.freeOnly ? " checked" : "") + "/> 仅免费模型</label>"
             "<button class='btn' type='submit'>保存</button></div>"
             "<div class='card-meta' style='margin-top:6px;'>Responses 模式目前用于纯对话（工具调用仍建议用 Chat Completions）。</div></form></details>";
    }

    c += "<div class='card'><h3>➕ 添加提供方</h3><form method='POST'>"
         "<input type='hidden' name='action' value='add'/>"
         "<div style='display:flex;gap:12px;flex-wrap:wrap;'>"
         "<div class='form-group' style='flex:1;min-width:140px;'><label>标识（英文/数字）</label><input name='id' placeholder='如 openrouter2'/></div>"
         "<div class='form-group' style='flex:1;min-width:160px;'><label>显示名称</label><input name='name' placeholder='如 某某中转'/></div>"
         "<div class='form-group' style='flex:1;min-width:150px;'><label>类型</label><select name='kind'>"
         "<option value='openai' selected>OpenAI 兼容</option>"
         "<option value='deepseek'>DeepSeek 官方</option>"
         "<option value='openrouter'>OpenRouter</option>"
         "<option value='openai-compatible'>其他中转</option>"
         "</select></div>"
         "<div class='form-group' style='flex:1;min-width:210px;'><label>接口协议</label><select name='apiMode'>"
         "<option value='chat' selected>Chat Completions（/chat/completions）</option>"
         "<option value='responses'>Responses API（/responses）</option>"
         "</select></div>"
         "<div class='form-group' style='flex:2;min-width:260px;'><label>Base URL（含 /v1）</label><input name='baseUrl' placeholder='https://api.example.com/v1' required/></div>"
         "<div class='form-group' style='flex:2;min-width:240px;'><label>API Key（只存服务端）</label><input name='apiKey' type='password' autocomplete='off'/></div>"
         "</div><div style='display:flex;gap:16px;align-items:center;'>"
         "<label class='ai-chk'><input type='checkbox' name='freeOnly' value='1'/> 仅免费模型</label>"
         "<button class='btn' type='submit'>添加</button></div></form>"
         "<div class='card-meta' style='margin-top:8px;'>添加后点「扫描选模型」，在小窗里搜索并勾选允许用户使用的模型。Key 永远不会下发到浏览器。</div></div>";

    // ---------- 扫描小窗（按提供方分开扫，支持搜索） ----------
    c += "<div class='modal-mask' id='scanMask'>"
         "<div class='modal-win'>"
         "<div class='modal-head'><b id='scanTitle'>扫描模型</b>"
         "<button type='button' class='modal-x' onclick='closeScan()'>✕</button></div>"
         "<div class='modal-tools'>"
         "<input id='scanQ' placeholder='搜索模型 id / 名称…' oninput='renderScan()'/>"
         "<button class='btn btn-sm' type='button' onclick='pickAll(1)'>全选</button>"
         "<button class='btn btn-sm' type='button' onclick='pickAll(0)'>全不选</button>"
         "<button class='btn btn-sm btn-outline' type='button' onclick='loadScan(1)'>🔄 重新扫描</button>"
         "</div>"
         "<div class='modal-info' id='scanInfo'>选择提供方后开始扫描</div>"
         "<div class='modal-list' id='scanList'></div>"
         "<div class='modal-foot'><span id='scanCount' style='color:var(--muted);font-size:12.5px;'></span>"
         "<span style='flex:1;'></span>"
         "<button class='btn btn-sm btn-outline' type='button' onclick='closeScan()'>取消</button>"
         "<button class='btn btn-sm' type='button' id='scanSave' onclick='saveScan()'>💾 保存启用列表</button>"
         "</div></div></div>";

    c += "<script>var AUTO_SCAN='" + common::jsonEscape(scanId) + "';</script>";
    c += R"SCAN(<script>
var SCAN={id:'',name:'',models:[],sel:{}};
function escHtml(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
function openScan(id,name){
  SCAN.id=id;SCAN.name=name||id;SCAN.models=[];SCAN.sel={};
  document.getElementById('scanMask').style.display='flex';
  document.getElementById('scanTitle').textContent='扫描模型 · '+SCAN.name;
  document.getElementById('scanQ').value='';
  document.getElementById('scanCount').textContent='';
  document.getElementById('scanInfo').textContent='扫描中…';
  document.getElementById('scanList').innerHTML="<div style='padding:20px;text-align:center;color:#7f8c8d;'>正在请求上游 /models …</div>";
  loadScan(1);
}
function closeScan(){document.getElementById('scanMask').style.display='none';}
function loadScan(refresh){
  var list=document.getElementById('scanList'),info=document.getElementById('scanInfo');
  if(refresh){list.innerHTML="<div style='padding:20px;text-align:center;color:#7f8c8d;'>正在请求上游 /models …</div>";}
  fetch('/admin/ai/providers/scan.json?id='+encodeURIComponent(SCAN.id)+(refresh?'&refresh=1':''),{credentials:'same-origin'})
    .then(function(r){return r.json();})
    .then(function(j){
      if(!j.ok){info.innerHTML='<span style="color:#e74c3c">'+escHtml(j.error||'扫描失败')+'</span>';list.innerHTML='';return;}
      SCAN.models=j.models||[];SCAN.sel={};
      (j.selected||[]).forEach(function(x){SCAN.sel[x]=1;});
      var free=SCAN.models.filter(function(m){return m.free;}).length;
      info.innerHTML='共 <b>'+SCAN.models.length+'</b> 个模型 · 免费 '+free+' · 已启用 <b>'+(j.selected||[]).length+'</b> 个'+(j.error?' · <span style="color:#e6a23c">'+escHtml(j.error)+'</span>':'');
      if(j.pruned){
        var rem=(j.removed||[]).join('、');
        info.innerHTML+=' · <span style="color:#e6a23c" title="'+escHtml(rem)+'">已自动移除 '+j.pruned+' 个扫不到的模型</span>';
        var cnt=document.getElementById('cnt-'+SCAN.id);
        if(cnt) cnt.textContent=(j.selected||[]).length+' 个';
      }
      renderScan();
    })
    .catch(function(e){info.innerHTML='<span style="color:#e74c3c">请求失败：'+escHtml(e)+'</span>';});
}
function renderScan(){
  var q=document.getElementById('scanQ').value.trim().toLowerCase();
  var list=document.getElementById('scanList');var h='';var shown=0;
  for(var i=0;i<SCAN.models.length;i++){
    var m=SCAN.models[i];
    if(q && (m.id+' '+m.name).toLowerCase().indexOf(q)<0) continue;
    shown++;
    var price=(m.price>0)?('$'+m.price.toFixed(2)+'/M'):'';
    h+="<label class='scan-row'><input type='checkbox' data-id='"+escHtml(m.id)+"' "+(SCAN.sel[m.id]?'checked':'')+" onchange='pick(this)'/>"
     +"<span class='scan-name'>"+escHtml(m.id)+"</span>"
     +(m.free?"<span class='pill' style='color:#27ae60;'>免费</span>":"")
     +(price?"<span class='scan-price'>"+price+"</span>":"")
     +"</label>";
  }
  if(!shown) h="<div style='padding:20px;text-align:center;color:#7f8c8d;'>没有匹配的模型</div>";
  list.innerHTML=h;
  document.getElementById('scanCount').textContent='已勾选 '+Object.keys(SCAN.sel).length+' 个';
}
function pick(el){var id=el.getAttribute('data-id');if(el.checked)SCAN.sel[id]=1;else delete SCAN.sel[id];document.getElementById('scanCount').textContent='已勾选 '+Object.keys(SCAN.sel).length+' 个';}
function pickAll(v){var boxes=document.querySelectorAll('#scanList input[type=checkbox]');for(var i=0;i<boxes.length;i++){boxes[i].checked=!!v;pick(boxes[i]);}}
function saveScan(){
  var names={};SCAN.models.forEach(function(m){names[m.id]=m.name;});
  var parts=[];Object.keys(SCAN.sel).forEach(function(id){parts.push(id+'|'+(names[id]||id));});
  var btn=document.getElementById('scanSave');btn.disabled=true;btn.textContent='保存中…';
  fetch('/admin/ai/providers/models',{method:'POST',headers:{'Content-Type':'application/json'},credentials:'same-origin',body:JSON.stringify({id:SCAN.id,models:parts.join(';')})})
    .then(function(r){return r.json();})
    .then(function(j){btn.disabled=false;btn.textContent='💾 保存启用列表';
      if(j.ok){var e=document.getElementById('cnt-'+SCAN.id);if(e)e.textContent=j.count+' 个';closeScan();location.reload();}
      else alert(j.error||'保存失败');})
    .catch(function(e){btn.disabled=false;btn.textContent='💾 保存启用列表';alert('保存失败：'+e);});
}
if(typeof AUTO_SCAN!=='undefined'&&AUTO_SCAN) openScan(AUTO_SCAN,'');
</script>)SCAN";

    return renderTemplate("AI 提供方", c, user);
}

// ============================================================
// AI 提供方 · 小窗扫描（JSON，分开扫）
// ============================================================
std::string Handlers::handleAdminProviderScanApi(const std::string& method, const std::string& path,
                                                 const std::string& body,
                                                 const std::map<std::string, std::string>& headers,
                                                 std::map<std::string, std::string>& respHeaders,
                                                 std::string& sessionId) {
    (void)method; (void)body; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!adminperm::has(user, "ai")) return "{\"ok\":false,\"error\":\"权限不足\"}";
    std::string id = common::trim(queryParam(path, "id"));
    bool refresh = queryParam(path, "refresh") == "1";
    if (id.empty()) return "{\"ok\":false,\"error\":\"缺少提供方参数\"}";
    std::string err;
    std::vector<AiScannedModel> ms = aiScanProvider(id, refresh, &err);

    // 重扫（refresh=1）时，自动移除上游已经扫不到的已启用模型
    std::string removedJson = "[]";
    int pruned = 0;
    aiprov::Provider p;
    bool provFound = aiprov::find(id, p);
    if (provFound && refresh && !ms.empty() && !p.models.empty()) {
        std::vector<aiprov::ModelRef> keep;
        std::string removedArr;
        for (size_t i = 0; i < p.models.size(); i++) {
            bool alive = false;
            for (size_t k = 0; k < ms.size(); k++) {
                if (ms[k].id == p.models[i].id) { alive = true; break; }
            }
            if (alive) {
                keep.push_back(p.models[i]);
            } else {
                pruned++;
                if (!removedArr.empty()) removedArr += ",";
                removedArr += common::jsonStringify(p.models[i].id);
            }
        }
        if (pruned > 0) {
            p.models = keep;
            std::vector<aiprov::Provider> all = aiprov::load();
            for (size_t i = 0; i < all.size(); i++) {
                if (all[i].id == id) all[i].models = keep;
            }
            aiprov::save(all);
            // 自检模型如果被移除，清空设置，避免指向不存在的模型
            std::string tm = aiplan::testModelId();
            if (!tm.empty()) {
                bool still = false;
                for (size_t i = 0; i < keep.size(); i++) if (keep[i].id == tm) still = true;
                if (!still) aiplan::setTestModelId("");
            }
            removedJson = "[" + removedArr + "]";
        }
    }

    std::string selected = "[]";
    if (provFound) {
        std::string arr;
        for (size_t i = 0; i < p.models.size(); i++) {
            if (i) arr += ",";
            arr += common::jsonStringify(p.models[i].id);
        }
        selected = "[" + arr + "]";
    }
    std::string modelsJson;
    for (size_t i = 0; i < ms.size(); i++) {
        char pb[40];
        if (ms[i].promptPrice < 0) snprintf(pb, sizeof(pb), "-1");
        else snprintf(pb, sizeof(pb), "%.4f", ms[i].promptPrice);
        std::string nm = ms[i].name.empty() ? ms[i].id : ms[i].name;
        std::string safeNm = common::replaceAll(nm, "|", "/");
        if (i) modelsJson += ",";
        modelsJson += "{\"id\":" + common::jsonStringify(ms[i].id) +
                      ",\"name\":" + common::jsonStringify(safeNm) +
                      ",\"free\":" + std::string(ms[i].freeModel ? "true" : "false") +
                      ",\"price\":" + std::string(pb) + "}";
    }
    return "{\"ok\":true,\"models\":[" + modelsJson + "],\"selected\":" + selected +
           ",\"pruned\":" + std::to_string(pruned) + ",\"removed\":" + removedJson +
           ",\"error\":" + common::jsonStringify(err) + "}";
}

// 保存某提供方启用的模型（JSON：{"id":"openrouter","models":"id|name;..."}）
std::string Handlers::handleAdminProviderModels(const std::string& method, const std::string& path,
                                                const std::string& body,
                                                const std::map<std::string, std::string>& headers,
                                                std::map<std::string, std::string>& respHeaders,
                                                std::string& sessionId) {
    (void)method; (void)path; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!adminperm::has(user, "ai")) return "{\"ok\":false,\"error\":\"权限不足\"}";
    auto o = common::jsonParseObject(body);
    std::string id = o.count("id") ? common::trim(o["id"]) : "";
    std::string field = o.count("models") ? o["models"] : "";
    if (id.empty()) return "{\"ok\":false,\"error\":\"缺少提供方\"}";
    std::vector<aiprov::Provider> all = aiprov::load();
    bool found = false;
    int count = 0;
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].id != id) continue;
        all[i].models = aiprov::parseModels(field);
        count = (int)all[i].models.size();
        found = true;
    }
    if (!found) return "{\"ok\":false,\"error\":\"提供方不存在\"}";
    aiprov::save(all);
    return "{\"ok\":true,\"count\":" + std::to_string(count) + "}";
}

// 中转自检（POST JSON：{"model":"...","prompt":"..."}）
std::string Handlers::handleAdminAiTest(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    (void)method; (void)path; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!adminperm::has(user, "ai")) return "{\"ok\":false,\"error\":\"权限不足\"}";
    auto o = common::jsonParseObject(body);
    std::string model = o.count("model") ? common::trim(o["model"]) : "";
    std::string prompt = o.count("prompt") ? o["prompt"] : "";
    if (model.empty()) model = aiplan::testModelId();
    if (model.empty()) return "{\"ok\":false,\"error\":\"还没有可测试的模型，请先在提供方页扫描并启用\"}";
    AiTestResult r = aiTestChat(model, prompt);
    if (r.ok) aiplan::setTestModelId(model);
    std::string html = common::htmlEscape(r.text);
    html = common::replaceAll(html, "\n", "<br/>");
    if (html.size() > 1200) html = html.substr(0, 1200) + "…";
    return "{\"ok\":" + std::string(r.ok ? "true" : "false") +
           ",\"model\":" + common::jsonStringify(model) +
           ",\"provider\":" + common::jsonStringify(r.providerName) +
           ",\"error\":" + common::jsonStringify(r.ok ? std::string() : (r.error.empty() ? std::string("未知错误") : r.error)) +
           ",\"html\":" + common::jsonStringify(html) +
           ",\"latencyMs\":" + std::to_string(r.latencyMs) +
           ",\"promptTokens\":" + std::to_string(r.promptTokens) +
           ",\"completionTokens\":" + std::to_string(r.completionTokens) + "}";
}

// ============================================================
// 社区举报：提交
// ============================================================
std::string Handlers::handleReport(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId) {
    (void)method; (void)path; (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string targetType = common::trim(parseFormData(body, "targetType"));
    if (targetType.empty()) targetType = "post";
    std::string targetId = common::trim(parseFormData(body, "targetId"));
    std::string postId = common::trim(parseFormData(body, "postId"));
    std::string reason = common::trim(parseFormData(body, "reason"));
    std::string detail = common::trim(parseFormData(body, "detail"));
    if (postId.empty()) postId = targetId;
    Post* post = DataStore::instance().findPostById(postId);
    if (!post) return "<script>window.location.href='/forum'</script>";

    std::string authorId = post->authorId;
    std::string content = post->content;
    std::string title = post->title;
    if (targetType == "comment") {
        bool found = false;
        for (size_t i = 0; i < post->comments.size(); i++) {
            if (post->comments[i].id != targetId) continue;
            authorId = post->comments[i].authorId;
            content = post->comments[i].content;
            found = true;
            break;
        }
        if (!found) return "<script>window.location.href='/post/" + postId + "'</script>";
        title = "回复：" + post->title;
    }
    if (authorId == user->id) return "<script>window.location.href='/post/" + postId + "'</script>";
    static const char* kReasons[] = {"广告/垃圾信息", "辱骂攻击", "无意义内容", "抄袭/侵权", "政治敏感", "其他"};
    bool reasonOk = false;
    for (int i = 0; i < 6; i++) if (reason == kReasons[i]) reasonOk = true;
    if (!reasonOk) reason = "其他";
    if (detail.size() > 300) detail = detail.substr(0, 300);
    if (security::throttleAction("report:" + user->id, 20))
        return "<script>window.location.href='/post/" + postId + "?reported=slow'</script>";
    if (reports::pendingCountOf(user->id, targetId) > 0)
        return "<script>window.location.href='/post/" + postId + "?reported=dup'</script>";

    std::string plain = common::trim(content);
    if (plain.size() > 140) plain = plain.substr(0, 140) + "…";
    reports::Report r;
    r.id = common::genId();
    r.targetType = (targetType == "comment") ? "comment" : "post";
    r.targetId = targetId;
    r.postId = postId;
    r.title = title;
    r.snippet = plain;
    r.authorId = authorId;
    User* au = DataStore::instance().findUserById(authorId);
    r.authorName = au ? au->username : "已注销";
    r.reason = reason;
    r.detail = detail;
    r.reporterId = user->id;
    r.reporterName = user->username;
    r.time = common::getTimeStr();
    r.status = "pending";
    reports::add(r);
    return "<script>window.location.href='/post/" + postId + "?reported=1" + (targetType == "comment" ? ("#c" + targetId) : std::string()) + "'</script>";
}

// ============================================================
// 后台：举报处理
// ============================================================
std::string Handlers::handleAdminReports(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId) {
    (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!adminperm::has(user, "content")) return "<script>window.location.href='/'</script>";
    std::string status = common::trim(queryParam(path, "status"));

    if (method == "POST") {
        std::string action = parseFormData(body, "action");
        std::string id = common::trim(parseFormData(body, "id"));
        reports::Report r;
        if (!reports::find(id, r)) return "<script>window.location.href='/admin/reports'</script>";
        if (action == "handle") {
            reports::updateStatus(id, "handled", user->username, "已处理");
        } else if (action == "ignore") {
            reports::updateStatus(id, "ignored", user->username, "已忽略");
        } else if (action == "delete") {
            if (r.targetType == "post") {
                auto& posts = DataStore::instance().posts;
                for (auto it = posts.begin(); it != posts.end(); ++it) {
                    if (it->id == r.targetId) { posts.erase(it); break; }
                }
                DataStore::instance().savePosts();
            } else {
                Post* post = DataStore::instance().findPostById(r.postId);
                if (post) {
                    for (auto it = post->comments.begin(); it != post->comments.end(); ++it) {
                        if (it->id == r.targetId) { post->comments.erase(it); break; }
                    }
                    DataStore::instance().savePosts();
                }
            }
            reports::markTargetHandled(r.targetId, user->username, "内容已删除");
        }
        return "<script>window.location.href='/admin/reports" +
               (status.empty() ? std::string() : ("?status=" + common::urlEncode(status))) + "'</script>";
    }

    std::vector<reports::Report> all = reports::load();
    int cntPending = 0, cntHandled = 0, cntIgnored = 0;
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].status == "pending") cntPending++;
        else if (all[i].status == "handled") cntHandled++;
        else cntIgnored++;
    }
    std::string q = common::trim(queryParam(path, "q"));
    std::string ql = q;
    for (size_t i = 0; i < ql.size(); i++) ql[i] = (char)tolower((unsigned char)ql[i]);

    std::vector<reports::Report> list;
    for (size_t i = all.size(); i > 0; i--) {
        const reports::Report& r = all[i - 1];
        if (!status.empty() && status != "all" && r.status != status) continue;
        if (status.empty() && r.status != "pending") continue;
        if (!q.empty()) {
            std::string hay = r.title + " " + r.authorName + " " + r.reporterName + " " + r.reason + " " + r.snippet;
            for (size_t k = 0; k < hay.size(); k++) hay[k] = (char)tolower((unsigned char)hay[k]);
            if (hay.find(ql) == std::string::npos) continue;
        }
        list.push_back(r);
    }
    if (status.empty()) status = "pending";
    int page = common::stringToInt(queryParam(path, "page"));
    if (page < 1) page = 1;
    const int PAGE_SIZE = 20;
    int totalPages = (int)((list.size() + PAGE_SIZE - 1) / PAGE_SIZE);
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    int start = (page - 1) * PAGE_SIZE;
    int end = std::min((int)list.size(), start + PAGE_SIZE);

    std::string c = adminNavbar(user);
    c += "<div class='action-bar' style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
         "<h2 style='margin:0;'>\U0001f6a9 举报处理</h2>"
         "<span style='color:var(--muted);font-size:13px;'>待处理 " + std::to_string(cntPending) +
         " · 已处理 " + std::to_string(cntHandled) + " · 已忽略 " + std::to_string(cntIgnored) + "</span></div>";
    c += "<div class='alert alert-info' style='margin-top:10px;'>同一目标的多条举报会合并提醒；删除内容会把该目标的待处理举报全部标记为已处理。</div>";
    c += "<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:12px 0;'>";
    auto tab = [&](const char* key, const std::string& label, int n) {
        std::string href = std::string("/admin/reports?status=") + key;
        if (!q.empty()) href += "&q=" + common::urlEncode(q);
        c += "<a class='btn btn-sm" + std::string(status == key ? "" : " btn-outline") + "' href='" + href + "'>" +
             label + (n > 0 ? (" <b>" + std::to_string(n) + "</b>") : "") + "</a>";
    };
    tab("pending", "待处理", cntPending);
    tab("handled", "已处理", 0);
    tab("ignored", "已忽略", 0);
    tab("all", "全部", 0);
    c += "<form method='GET' action='/admin/reports' style='display:flex;gap:6px;margin-left:auto;'>"
         "<input type='hidden' name='status' value='" + status + "'/>"
         "<input name='q' value='" + common::htmlEscape(q) + "' placeholder='搜索标题/用户/原因…'/>"
         "<button class='btn btn-sm' type='submit'>搜索</button></form></div>";

    c += "<table style='width:100%;font-size:13px;'><thead><tr>"
         "<th style='width:70px;'>类型</th><th>被举报内容</th><th style='width:110px;'>原因</th>"
         "<th style='width:110px;'>举报人</th><th style='width:130px;'>时间</th><th style='width:90px;'>状态</th>"
         "<th style='width:230px;'>操作</th></tr></thead><tbody>";
    for (int i = start; i < end; i++) {
        const reports::Report& r = list[i];
        std::string statusTxt = "<span style='color:#e6a23c;'>待处理</span>";
        if (r.status == "handled") statusTxt = "<span style='color:#27ae60;'>已处理</span>";
        else if (r.status == "ignored") statusTxt = "<span style='color:#95a5a6;'>已忽略</span>";
        c += "<tr><td>" + std::string(r.targetType == "comment" ? "回复" : "主题") + "</td>"
             "<td><a href='/post/" + r.postId + (r.targetType == "comment" ? ("#c" + r.targetId) : "") + "'>" +
             common::htmlEscape(r.title.empty() ? "(无标题)" : r.title) + "</a>"
             "<div style='color:var(--muted);font-size:12px;'>" + common::htmlEscape(r.snippet) +
             "<br/>作者：" + common::htmlEscape(r.authorName) +
             (r.detail.empty() ? std::string() : (" · 补充：" + common::htmlEscape(r.detail))) + "</div></td>"
             "<td>" + common::htmlEscape(r.reason) + "</td>"
             "<td>" + common::htmlEscape(r.reporterName) + "</td>"
             "<td style='font-size:12px;'>" + common::htmlEscape(r.time) + "</td>"
             "<td>" + statusTxt + "</td><td>";
        if (r.status == "pending") {
            c += "<form method='POST' style='display:inline;'><input type='hidden' name='id' value='" + r.id + "'/>"
                 "<button class='btn btn-sm' name='action' value='handle' type='submit'>已处理</button> "
                 "<button class='btn btn-sm btn-outline' name='action' value='ignore' type='submit'>忽略</button> "
                 "<button class='btn btn-sm btn-danger' name='action' value='delete' type='submit' onclick=\"return confirm('删除被举报内容？')\">删除内容</button></form>";
        } else {
            c += "<span style='color:var(--muted);font-size:12px;'>" + common::htmlEscape(r.handledBy) +
                 (r.handledTime.empty() ? std::string() : (" · " + common::htmlEscape(r.handledTime))) +
                 (r.result.empty() ? std::string() : (" · " + common::htmlEscape(r.result))) + "</span>";
        }
        c += "</td></tr>";
    }
    if (list.empty()) c += "<tr><td colspan='7' class='empty-td'>没有举报记录</td></tr>";
    c += "</tbody></table>";
    if (totalPages > 1) {
        c += "<div style='display:flex;gap:8px;justify-content:center;margin-top:12px;'>";
        for (int p2 = 1; p2 <= totalPages && p2 <= 20; p2++) {
            c += "<a class='btn btn-sm" + std::string(p2 == page ? "" : " btn-outline") +
                 "' href='/admin/reports?status=" + status + (q.empty() ? "" : ("&q=" + common::urlEncode(q))) +
                 "&page=" + std::to_string(p2) + "'>" + std::to_string(p2) + "</a>";
        }
        c += "</div>";
    }
    return renderTemplate("举报处理", c, user);
}

// ============================================================
// 用户套餐管理
// ============================================================
std::string Handlers::handleAdminAIPlans(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId) {
    (void)path; (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    aiplan::ensureSeed();
    std::vector<aiplan::Plan> plans = aiplan::load();

    if (method == "POST") {
        std::string action = parseFormData(body, "action");
        std::string id = common::trim(parseFormData(body, "id"));
        if (action == "save") {
            for (size_t i = 0; i < plans.size(); i++) {
                if (plans[i].id != id) continue;
                plans[i].name = common::trim(parseFormData(body, "name"));
                plans[i].desc = common::trim(parseFormData(body, "desc"));
                plans[i].windowQuota = common::stringToLongLong(parseFormData(body, "windowQuota"));
                plans[i].monthQuota = common::stringToLongLong(parseFormData(body, "monthQuota"));
                if (plans[i].windowQuota < 0) plans[i].windowQuota = 0;
                if (plans[i].monthQuota < 0) plans[i].monthQuota = 0;
                plans[i].price = atof(parseFormData(body, "price").c_str());
                if (plans[i].price < 0) plans[i].price = 0;
                std::string mf = parseFormData(body, "models");
                plans[i].models.clear();
                auto parts = common::split(mf, ';');
                for (size_t k = 0; k < parts.size(); k++) {
                    std::string t = common::trim(parts[k]);
                    if (!t.empty()) plans[i].models.push_back(t);
                }
            }
            aiplan::save(plans);
            // 同步所有使用该套餐的用户额度
            DataStore& ds = DataStore::instance();
            for (size_t i = 0; i < ds.users.size(); i++)
                if (ds.users[i].aiPlanId == id) aiplan::applyPlan(ds.users[i], id);
            ds.saveUsers();
        } else if (action == "add") {
            aiplan::Plan p;
            p.id = common::trim(parseFormData(body, "id"));
            for (size_t i = 0; i < p.id.size(); i++) {
                char c = p.id[i];
                if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) p.id[i] = '_';
            }
            if (p.id.empty()) p.id = "plan" + std::to_string((long long)time(NULL));
            p.name = common::trim(parseFormData(body, "name"));
            if (p.name.empty()) p.name = p.id;
            p.desc = common::trim(parseFormData(body, "desc"));
            p.windowQuota = common::stringToLongLong(parseFormData(body, "windowQuota"));
            p.monthQuota = common::stringToLongLong(parseFormData(body, "monthQuota"));
            p.price = atof(parseFormData(body, "price").c_str());
            if (p.price < 0) p.price = 0;
            bool exists = false;
            for (size_t i = 0; i < plans.size(); i++) if (plans[i].id == p.id) exists = true;
            if (!exists) { plans.push_back(p); aiplan::save(plans); }
        } else if (action == "delete") {
            std::vector<aiplan::Plan> keep;
            for (size_t i = 0; i < plans.size(); i++) if (plans[i].id != id) keep.push_back(plans[i]);
            aiplan::save(keep);
        } else if (action == "setdefault") {
            aiplan::setDefaultPlanId(parseFormData(body, "planId"));
        } else if (action == "applydefault") {
            std::string dp = aiplan::defaultPlanId();
            DataStore& ds = DataStore::instance();
            if (!dp.empty()) {
                for (size_t i = 0; i < ds.users.size(); i++)
                    if (ds.users[i].aiPlanId.empty()) aiplan::applyPlan(ds.users[i], dp);
                ds.saveUsers();
            }
        }
        return "<script>window.location.href='/admin/ai/plans'</script>";
    }

    // 每个套餐的会员数
    std::map<std::string, int> count;
    for (size_t i = 0; i < DataStore::instance().users.size(); i++)
        count[DataStore::instance().users[i].aiPlanId]++;
    std::string dpId = aiplan::defaultPlanId();
    int noPlan = count.count("") ? count[""] : 0;

    std::string c = "<div class='action-bar' style='display:flex;gap:8px;justify-content:flex-end;margin-bottom:10px;'>"
                    "<a class='btn btn-sm' href='/admin/ai'>← 返回 AI 管理</a>"
                    "<a class='btn btn-sm' href='/admin/ai/providers'>提供方管理 →</a></div>";
    c += "<div class='alert alert-info'>套餐决定 <b>5 小时窗口额度 / 月度额度 / 可用模型</b>。"
         "保存后会同步所有该套餐用户的额度；「可用模型」留空 = 全部模型，多个用分号隔开。</div>";

    // 默认套餐
    c += "<div class='card'><h3>🎁 新用户默认套餐</h3>"
         "<div class='card-meta' style='margin-bottom:10px;'>新注册用户自动使用该套餐；也可以一键应用到所有尚未分配套餐的用户（当前 "
         + std::to_string(noPlan) + " 人）。</div>"
         "<form method='POST' style='display:flex;gap:8px;align-items:flex-end;flex-wrap:wrap;'>"
         "<input type='hidden' name='action' value='setdefault'/>"
         "<div class='form-group' style='margin:0;min-width:180px;'><label>默认套餐</label><select name='planId'>";
    c += "<option value=''" + std::string(dpId.empty() ? " selected" : "") + ">不自动分配（用全局默认额度）</option>";
    for (size_t i = 0; i < plans.size(); i++) {
        c += "<option value='" + common::htmlEscape(plans[i].id) + "'" + std::string(plans[i].id == dpId ? " selected" : "") + ">"
             + common::htmlEscape(plans[i].name) + "（" + std::to_string(plans[i].windowQuota) + " / " + std::to_string(plans[i].monthQuota) + "）</option>";
    }
    c += "</select></div><button class='btn' type='submit'>保存默认套餐</button>"
         "<button class='btn btn-outline' name='action' value='applydefault' type='submit' onclick=\"return confirm('把所有未分配套餐的用户切换为默认套餐？')\">应用到未分配用户</button>"
         "</form></div>";
    for (size_t i = 0; i < plans.size(); i++) {
        const aiplan::Plan& p = plans[i];
        char prbuf[32];
        snprintf(prbuf, sizeof(prbuf), "%.2f", p.price);
        std::string priceStr(prbuf);
        std::string mf;
        for (size_t k = 0; k < p.models.size(); k++) {
            if (!mf.empty()) mf += ";";
            mf += p.models[k];
        }
        c += "<div class='card'><form method='POST'><input type='hidden' name='action' value='save'/>"
             "<input type='hidden' name='id' value='" + common::htmlEscape(p.id) + "'/>"
             "<div style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:10px;'>"
             "<h3 style='margin:0;'>" + common::htmlEscape(p.name) + "</h3>"
             "<span class='pill'>id: " + common::htmlEscape(p.id) + "</span>"
             "<span class='pill'>👥 " + std::to_string(count[p.id]) + " 人</span></div>"
             "<div style='display:flex;gap:12px;flex-wrap:wrap;'>"
             "<div class='form-group' style='flex:1;min-width:150px;'><label>名称</label><input name='name' value='" + common::htmlEscape(p.name) + "'/></div>"
             "<div class='form-group' style='flex:1;min-width:110px;'><label>5 小时额度</label><input name='windowQuota' type='number' min='0' value='" + std::to_string(p.windowQuota) + "'/></div>"
             "<div class='form-group' style='flex:1;min-width:110px;'><label>月额度</label><input name='monthQuota' type='number' min='0' value='" + std::to_string(p.monthQuota) + "'/></div>"
             "<div class='form-group' style='flex:1;min-width:100px;'><label>价格（元/月）</label><input name='price' type='number' step='0.01' min='0' value='" + priceStr + "'/></div>"
             "<div class='form-group' style='flex:3;min-width:260px;'><label>可用模型（空=全部）</label><input name='models' value='" + common::htmlEscape(mf) + "' placeholder='deepseek-flash;google/gemma-4-31b-it:free'/></div>"
             "</div><div class='form-group'><label>说明</label><input name='desc' value='" + common::htmlEscape(p.desc) + "'/></div>"
             "<div style='display:flex;gap:8px;'><button class='btn' type='submit'>保存</button>"
             "<button class='btn btn-sm btn-danger' name='action' value='delete' type='submit' onclick=\"return confirm('删除套餐？（用户会回到默认额度）')\">删除</button></div>"
             "</form></div>";
    }
    c += "<div class='card'><h3>➕ 新增套餐</h3><form method='POST'><input type='hidden' name='action' value='add'/>"
         "<div style='display:flex;gap:12px;flex-wrap:wrap;'>"
         "<div class='form-group' style='flex:1;min-width:130px;'><label>标识</label><input name='id' placeholder='vip'/></div>"
         "<div class='form-group' style='flex:1;min-width:150px;'><label>名称</label><input name='name' placeholder='尊享版'/></div>"
         "<div class='form-group' style='flex:1;min-width:110px;'><label>5 小时额度</label><input name='windowQuota' type='number' value='200'/></div>"
         "<div class='form-group' style='flex:1;min-width:110px;'><label>月额度</label><input name='monthQuota' type='number' value='2000'/></div>"
         "<div class='form-group' style='flex:1;min-width:100px;'><label>价格（元/月，0=不展示）</label><input name='price' type='number' step='0.01' min='0' value='0'/></div>"
         "<div class='form-group' style='flex:2;min-width:240px;'><label>说明</label><input name='desc'/></div>"
         "</div><button class='btn' type='submit'>新增</button></form></div>";
    return renderTemplate("AI 套餐", c, user);
}

// ============================================================
std::string Handlers::handleDrive(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rel;
    if (path.rfind("/drive/folder/", 0) == 0) rel = sanitizeRelPath(common::urlDecode(path.substr(13)));
    std::string baseDir = "./files/" + user->id + "/";
    std::string dir = baseDir + (rel.empty() ? "" : rel + "/");
    common::createDirectory(dir);
    auto entries = listDriveEntries(dir);
    long long usedTotal = dirSizeBytes(baseDir);
    long long quotaBytes = (long long)getUserDriveTotalMB(user) * 1024 * 1024;

    // ---------- 面包屑 ----------
    std::string crumb = "<a href='/drive' class='drv-crumb-item'>&#127968; 我的云盘</a>";
    std::string cur;
    {
        std::vector<std::string> segs = common::split(rel, '/');
        for (size_t i = 0; i < segs.size(); i++) {
            if (segs[i].empty()) continue;
            if (!cur.empty()) cur += "/";
            cur += segs[i];
            crumb += "<span class='drv-crumb-sep'>/</span><a class='drv-crumb-item' href='/drive/folder/" + common::urlEncode(cur) + "'>" + common::htmlEscape(segs[i]) + "</a>";
        }
    }

    // ---------- 列表数据 ----------
    auto sharesAll = loadShares();
    std::map<std::string, std::string> tokenByFile;
    for (size_t i = 0; i < sharesAll.size(); i++)
        if (sharesAll[i]["userId"] == user->id) tokenByFile[sharesAll[i]["filename"]] = sharesAll[i]["token"];

    std::string itemsJson = "[";
    bool firstItem = true;
    for (size_t i = 0; i < entries.size(); i++) {
        const DrvEntry& e = entries[i];
        std::string relPath = (rel.empty() ? "" : rel + "/") + e.name;
        std::string kind = driveKindOf(e.name, e.isDir);
        std::string sh;
        if (!e.isDir) {
            std::map<std::string, std::string>::iterator it = tokenByFile.find(relPath);
            if (it != tokenByFile.end()) sh = it->second;
        }
        if (!firstItem) itemsJson += ",";
        firstItem = false;
        itemsJson += "{\"n\":\"" + common::jsonEscape(e.name) + "\""
                     ",\"p\":\"" + common::jsonEscape(relPath) + "\""
                     ",\"d\":" + (e.isDir ? "true" : "false") +
                     ",\"s\":" + std::to_string(e.size) +
                     ",\"t\":\"" + common::jsonEscape(e.mtime) + "\""
                     ",\"k\":\"" + kind + "\""
                     ",\"sh\":\"" + common::jsonEscape(sh) + "\"}";
    }
    itemsJson += "]";

    // 共享给我的快捷方式（别人分享的文件夹/文件）
    std::string scJson = "[";
    {
        std::vector<std::map<std::string, std::string> > scs = loadShortcuts();
        bool firstSc = true;
        for (size_t i = 0; i < scs.size(); i++) {
            if (scs[i]["userId"] != user->id) continue;
            std::string tk = scs[i]["token"];
            bool alive = false;
            std::string ownerId, spath;
            for (size_t k = 0; k < sharesAll.size(); k++)
                if (sharesAll[k]["token"] == tk) { ownerId = sharesAll[k]["userId"]; spath = sharesAll[k]["filename"]; alive = true; break; }
            if (!alive) continue;
            if (GetFileAttributesA(("./files/" + ownerId + "/" + spath).c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            User* ow = DataStore::instance().findUserById(ownerId);
            if (!firstSc) scJson += ",";
            firstSc = false;
            scJson += "{\"token\":\"" + common::jsonEscape(tk) + "\",\"name\":\"" + common::jsonEscape(drvBaseName(spath)) +
                      "\",\"owner\":\"" + common::jsonEscape(ow ? ow->username : "已注销") + "\"}";
        }
    }
    scJson += "]";

    int ndir = 0, nfile = 0;
    for (size_t i = 0; i < entries.size(); i++) { if (entries[i].isDir) ndir++; else nfile++; }

    double pct = quotaBytes > 0 ? (double)usedTotal * 100.0 / (double)quotaBytes : 0;
    if (pct > 100) pct = 100;
    char pctBuf[32];
    snprintf(pctBuf, sizeof(pctBuf), "%.1f", pct);

    std::string content = "<div class='drv'>";
    // 配额条
    content += "<div class='drv-quota'><div class='drv-quota-top'><span>&#9729;&#65039; 我的云盘</span>"
               "<span class='drv-quota-num'>" + humanSize(usedTotal) + " / " + humanSize(quotaBytes) +
               " &nbsp;(" + std::string(pctBuf) + "%)</span></div>"
               "<div class='drv-quota-bar'><i style='width:" + std::string(pctBuf) + "%' class='" + (pct > 90 ? "warn" : "") + "'></i></div></div>";

    // 面包屑
    content += "<div class='drv-crumb'>" + crumb + "</div>";

    // 工具栏
    content += "<div class='drv-bar'>"
               "<button class='btn btn-sm' id='drv-up-file'>\u2191 上传文件</button>"
               "<button class='btn btn-sm' id='drv-up-dir'>\U0001f4c1 上传文件夹</button>"
               "<button class='btn btn-sm' id='drv-mkdir'>\uff0b 新建文件夹</button>"
               "<button class='btn btn-sm btn-outline' id='drv-refresh'>\u21bb 刷新</button>"
               "<span class='drv-spacer'></span>"
               "<div class='drv-search'><input id='drv-q' placeholder='搜索当前目录，回车全盘搜索'/><button id='drv-q-all' title='全盘搜索'>\U0001f50d</button></div>"
               "<select id='drv-sort' class='drv-sort'>"
               "<option value='name'>名称</option><option value='size'>大小</option>"
               "<option value='time'>修改时间</option><option value='kind'>类型</option></select>"
               "<button class='btn btn-sm btn-outline' id='drv-sort-dir' title='升降序'>\u2195</button>"
               "<button class='btn btn-sm btn-outline' id='drv-view' title='切换视图'>\u2630</button>"
               "</div>";

    // 批量操作条
    content += "<div class='drv-bulk' id='drv-bulk'>"
               "<label class='drv-check'><input type='checkbox' id='drv-all'/> 全选</label>"
               "<span id='drv-selinfo' class='drv-selinfo'>未选择</span>"
               "<button class='btn btn-sm' id='drv-dl-sel'>\u2193 打包下载</button>"
               "<button class='btn btn-sm' id='drv-mv-sel'>\u2192 移动到…</button>"
               "<button class='btn btn-sm' id='drv-cp-sel'>\u29c9 复制到…</button>"
               "<button class='btn btn-sm btn-danger' id='drv-rm-sel'>删除</button>"
               "<span class='drv-tip'>Ctrl+C / X / V 复制剪切粘贴 · F2 重命名 · Del 删除 · 右键更多</span>"
               "</div>";

    content += "<div id='drv-list' class='drv-list'></div>";
    content += "<div class='drv-empty' id='drv-empty' style='display:none;'></div>";
    content += "<div class='drv-drop' id='drv-drop'>松开鼠标，上传到当前目录</div>";
    content += "<div class='drv-count'>\U0001f4c1 文件夹 " + std::to_string(ndir) + " 个 · \U0001f4c4 文件 " + std::to_string(nfile) + " 个</div>";
    content += "</div>";

    // 隐藏 input
    content += "<input type='file' id='drv-file' multiple style='display:none'/>";
    content += "<input type='file' id='drv-dir' webkitdirectory directory multiple style='display:none'/>";

    // 数据注入
    content += "<script>window.DRV={dir:" + common::jsonStringify(rel) +
               ",items:" + itemsJson +
               ",shortcuts:" + scJson +
               ",used:" + std::to_string(usedTotal) +
               ",quota:" + std::to_string(quotaBytes) +
               ",maxFileMB:" + std::to_string(getDriveMaxMB()) + "};</script>";
    content += "<script src='/static/drive.js?v=" + std::string(OISTATION_VERSION) + "'></script>";
    return renderTemplate("\u4e91\u76d8", content, user);
}

// ---------- 内联预览（支持 Range，视频可拖动进度） ----------
std::string Handlers::handleDriveRaw(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<div class='alert alert-error'>未登录</div>";
    std::string raw = path.substr(std::string("/drive/raw/").size());
    size_t q = raw.find('?');
    bool asDownload = false;
    if (q != std::string::npos) {
        asDownload = drvQueryValue(path, "download") == "1";
        raw = raw.substr(0, q);
    }
    std::string rel = sanitizeRelPath(common::urlDecode(raw));
    if (rel.empty()) return "<div class='alert alert-error'>路径无效</div>";
    std::string fp = "./files/" + user->id + "/" + rel;
    if (!common::fileExists(fp)) return "<div class='alert alert-error'>文件不存在</div>";
    std::string fname = drvBaseName(rel);
    std::string data = common::readFile(fp);
    long long total = (long long)data.size();
    std::string mime = driveMimeOf(fname);
    if (mime == "image/svg+xml") respHeaders["Content-Security-Policy"] = "sandbox; default-src 'none'; style-src 'unsafe-inline'";
    respHeaders["Content-Type"] = mime;
    respHeaders["Accept-Ranges"] = "bytes";
    respHeaders["Cache-Control"] = "private, max-age=60";
    if (asDownload) respHeaders["Content-Disposition"] = "attachment; filename=\"" + sanitizeFilename(fname) + "\"";

    // Range 请求
    std::string rangeHdr;
    std::map<std::string, std::string>::const_iterator hit = headers.find("range");
    if (hit != headers.end()) rangeHdr = hit->second;
    if (rangeHdr.rfind("bytes=", 0) == 0 && total > 0) {
        std::string spec = rangeHdr.substr(6);
        size_t dash = spec.find('-');
        if (dash != std::string::npos) {
            std::string sStr = spec.substr(0, dash);
            std::string eStr = spec.substr(dash + 1);
            long long start = 0, end = total - 1;
            bool okRange = true;
            if (sStr.empty()) {
                long long suffix = atoll(eStr.c_str());
                if (suffix <= 0) okRange = false;
                else { start = total - suffix; if (start < 0) start = 0; }
            } else {
                start = atoll(sStr.c_str());
                if (!eStr.empty()) end = atoll(eStr.c_str());
                if (end >= total) end = total - 1;
                if (start > end || start >= total) okRange = false;
            }
            if (!okRange) {
                respHeaders["X-Status"] = "416";
                respHeaders["Content-Range"] = "bytes */" + std::to_string(total);
                return "";
            }
            long long len = end - start + 1;
            respHeaders["X-Status"] = "206";
            respHeaders["Content-Range"] = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(total);
            return data.substr((size_t)start, (size_t)len);
        }
    }
    return data;
}

// ---------- 打包下载（文件 / 文件夹 / 多选） ----------
std::string Handlers::handleDriveZip(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<div class='alert alert-error'>未登录</div>";
    std::string baseDir = "./files/" + user->id + "/";
    std::string dir = "";
    std::string rawPaths;
    std::string name = "云盘下载.zip";
    if (method == "POST") {
        dir = sanitizeRelPath(parseFormData(body, "dir"));
        rawPaths = parseFormData(body, "paths");
        std::string n = parseFormData(body, "name");
        if (!n.empty()) name = n;
    } else {
        dir = sanitizeRelPath(drvQueryValue(path, "dir"));
        rawPaths = drvQueryValue(path, "paths");
        std::string n = drvQueryValue(path, "name");
        if (!n.empty()) name = n;
    }
    std::string workDir = baseDir + (dir.empty() ? "" : dir + "/");
    std::vector<std::string> list = drvSplitPaths(rawPaths);
    std::string out;
    std::vector<DrvZipRec> recs;
    for (size_t i = 0; i < list.size(); i++) drvZipCollect(out, recs, workDir, list[i]);
    if (recs.empty()) {
        respHeaders["Content-Type"] = "text/plain; charset=utf-8";
        return "没有可打包的内容";
    }
    drvZipEnd(out, recs);
    if (name.size() > 4) {
        std::string tail = name.substr(name.size() - 4);
        std::transform(tail.begin(), tail.end(), tail.begin(), ::tolower);
        if (tail != ".zip") name += ".zip";
    } else if (name != "云盘下载.zip") name += ".zip";
    respHeaders["Content-Type"] = "application/zip";
    respHeaders["Content-Disposition"] = "attachment; filename=\"" + sanitizeFilename(name) + "\"; filename*=UTF-8''" + common::urlEncode(name);
    return out;
}

// ---------- 移动 / 重命名（支持目录） ----------
static bool drvSameOrInside(const std::string& child, const std::string& parent) {
    if (parent.empty()) return true;
    if (child == parent) return true;
    return child.size() > parent.size() && child.compare(0, parent.size() + 1, parent + "/") == 0;
}

std::string Handlers::handleDriveMove(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false,\"msg\":\"未登录\"}";
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    std::string from = sanitizeRelPath(parseFormData(body, "from"));
    std::string to = sanitizeRelPath(parseFormData(body, "to"));
    if (from.empty() || from.find('/') == std::string::npos && to.empty()) return "{\"ok\":false,\"msg\":\"路径无效\"}";
    if (from == to) return "{\"ok\":true,\"msg\":\"未变化\",\"path\":\"" + common::jsonEscape(from) + "\"}";
    // 不允许把目录移进自己或子目录
    if (drvSameOrInside(to, from)) return "{\"ok\":false,\"msg\":\"不能移动到自身或其子目录\"}";
    std::string baseDir = "./files/" + user->id + "/";
    std::string src = baseDir + from;
    if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES)
        return "{\"ok\":false,\"msg\":\"源不存在\"}";
    std::string targetDir = baseDir + (to.empty() ? "" : to + "/");
    if (GetFileAttributesA(targetDir.c_str()) == INVALID_FILE_ATTRIBUTES)
        common::createDirectory(targetDir);
    std::string baseName = drvBaseName(from);
    std::string dst = targetDir + baseName;
    std::string stem = baseName, ext;
    size_t dot = baseName.find_last_of('.');
    if (dot != std::string::npos && dot > 0) { stem = baseName.substr(0, dot); ext = baseName.substr(dot); }
    int n = 1;
    while (GetFileAttributesA(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
        n++;
        std::string suffix = " (" + std::to_string(n) + ")";
        dst = targetDir + stem + suffix + ext;
        if (n > 200) break;
    }
    if (!MoveFileA(src.c_str(), dst.c_str()))
        return "{\"ok\":false,\"msg\":\"移动失败（可能被占用）\"}";
    return "{\"ok\":true,\"msg\":\"ok\",\"path\":\"" + common::jsonEscape((to.empty() ? "" : to + "/") + drvBaseName(dst)) + "\"}";
}

// ---------- 文件夹树（移动目标选择） ----------
static void drvCollectFolders(const std::string& baseDir, const std::string& rel, int depth,
                              std::string& json, bool& first, int& count) {
    if (depth > 12 || count > 800) return;
    std::vector<DrvEntry> entries = listDriveEntries(baseDir + (rel.empty() ? "" : rel + "/"));
    for (size_t i = 0; i < entries.size(); i++) {
        if (!entries[i].isDir) continue;
        std::string p = rel.empty() ? entries[i].name : (rel + "/" + entries[i].name);
        if (!first) json += ",";
        first = false;
        json += "{\"p\":\"" + common::jsonEscape(p) + "\",\"n\":\"" + common::jsonEscape(entries[i].name) + "\",\"depth\":" + std::to_string(depth) + "}";
        count++;
        drvCollectFolders(baseDir, p, depth + 1, json, first, count);
    }
}

std::string Handlers::handleDriveTree(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    if (!user) return "{\"folders\":[]}";
    std::string json = "{\"folders\":[";
    bool first = true;
    int count = 0;
    drvCollectFolders("./files/" + user->id + "/", "", 1, json, first, count);
    json += "]}";
    return json;
}

// ---------- 全盘搜索 ----------
static void drvSearchWalk(const std::string& baseDir, const std::string& rel,
                          const std::string& qLower, std::string& json, bool& first, int& found) {
    if (found >= 300) return;
    std::vector<DrvEntry> entries = listDriveEntries(baseDir + (rel.empty() ? "" : rel + "/"));
    for (size_t i = 0; i < entries.size() && found < 300; i++) {
        const DrvEntry& e = entries[i];
        std::string p = rel.empty() ? e.name : (rel + "/" + e.name);
        std::string nl = e.name;
        std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
        if (nl.find(qLower) != std::string::npos) {
            if (!first) json += ",";
            first = false;
            json += "{\"n\":\"" + common::jsonEscape(e.name) + "\",\"p\":\"" + common::jsonEscape(p) + "\",\"d\":" +
                    (e.isDir ? "true" : "false") + ",\"s\":" + std::to_string(e.size) + ",\"t\":\"" + common::jsonEscape(e.mtime) + "\",\"k\":\"" + driveKindOf(e.name, e.isDir) + "\"}";
            found++;
        }
        if (e.isDir) drvSearchWalk(baseDir, p, qLower, json, first, found);
    }
}

std::string Handlers::handleDriveSearch(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    if (!user) return "{\"items\":[]}";
    std::string q = drvQueryValue(path, "q");
    if (q.empty()) return "{\"items\":[]}";
    std::string ql = q;
    std::transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
    std::string json = "{\"q\":\"" + common::jsonEscape(q) + "\",\"items\":[";
    bool first = true;
    int found = 0;
    drvSearchWalk("./files/" + user->id + "/", "", ql, json, first, found);
    json += "]}";
    return json;
}

// ============================================================
// 云盘 v3：文件夹共享 / 快捷方式 / 复制 / 属性
// ============================================================

// ---------- 复制（粘贴） ----------
std::string Handlers::handleDriveCopy(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    if (!user) return "{\"ok\":false,\"msg\":\"未登录\"}";
    std::string dir = sanitizeRelPath(parseFormData(body, "dir"));
    std::string to = sanitizeRelPath(parseFormData(body, "to"));
    std::vector<std::string> list = drvSplitPaths(parseFormData(body, "paths"));
    std::string baseDir = "./files/" + user->id + "/";
    std::string srcRoot = baseDir + (dir.empty() ? "" : dir + "/");
    std::string dstRoot = baseDir + (to.empty() ? "" : to + "/");
    if (GetFileAttributesA(dstRoot.c_str()) == INVALID_FILE_ATTRIBUTES) common::createDirectory(dstRoot);
    int ok = 0, fail = 0;
    for (size_t i = 0; i < list.size(); i++) {
        std::string p = list[i];
        std::string src = srcRoot + p;
        if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES) { fail++; continue; }
        std::string baseName = drvBaseName(p);
        std::string dst = drvUniqueTarget(dstRoot, baseName);
        // 防止把目录复制进自己
        std::string dstRel = dst.substr(baseDir.size());
        if (drvSameOrInside(dstRel, (dir.empty() ? p : (dir + "/" + p)))) { fail++; continue; }
        if (drvCopyRecursive(src, dst)) ok++; else fail++;
    }
    return "{\"ok\":" + std::string(ok > 0 ? "true" : "false") +
           ",\"copied\":" + std::to_string(ok) + ",\"failed\":" + std::to_string(fail) + "}";
}

// ---------- 新建空文件 ----------
std::string Handlers::handleDriveNewFile(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    if (!user) return "{\"ok\":false,\"msg\":\"未登录\"}";
    std::string dir = sanitizeRelPath(parseFormData(body, "dir"));
    std::string name = sanitizeFilename(parseFormData(body, "name"));
    std::string content = parseFormData(body, "content");
    if (name.empty()) return "{\"ok\":false,\"msg\":\"名称不能为空\"}";
    std::string dirFull = "./files/" + user->id + "/" + (dir.empty() ? "" : dir + "/");
    if (GetFileAttributesA(dirFull.c_str()) == INVALID_FILE_ATTRIBUTES) common::createDirectory(dirFull);
    std::string target = dirFull + name;
    if (GetFileAttributesA(target.c_str()) != INVALID_FILE_ATTRIBUTES)
        return "{\"ok\":false,\"msg\":\"同名文件已存在\"}";
    if (!common::writeFile(target, content)) return "{\"ok\":false,\"msg\":\"写入失败\"}";
    return "{\"ok\":true,\"msg\":\"ok\"}";
}

// ============================================================
// IDE ↔ 云盘 ↔ 提交
// ============================================================
std::string Handlers::handleIdeDrive(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    (void)method; (void)body; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false,\"msg\":\"请先登录\"}";
    std::string rel = sanitizeRelPath(queryParam(path, "dir"));
    std::string base = "./files/" + user->id + "/";
    std::string dir = base + (rel.empty() ? "" : rel + "/");
    if (GetFileAttributesA(dir.c_str()) == INVALID_FILE_ATTRIBUTES ||
        !(GetFileAttributesA(dir.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) {
        rel.clear();
        dir = base;
    }
    std::vector<DrvEntry> es = listDriveEntries(dir);
    std::string items = "[";
    for (size_t i = 0; i < es.size(); i++) {
        const DrvEntry& e = es[i];
        if (i) items += ",";
        std::string child = rel.empty() ? e.name : (rel + "/" + e.name);
        items += "{\"name\":" + common::jsonStringify(e.name) +
                 ",\"isDir\":" + std::string(e.isDir ? "true" : "false") +
                 ",\"size\":" + std::to_string(e.size) +
                 ",\"mtime\":" + common::jsonStringify(e.mtime) +
                 ",\"path\":" + common::jsonStringify(child) +
                 ",\"kind\":" + common::jsonStringify(driveKindOf(e.name, e.isDir)) + "}";
    }
    items += "]";
    return "{\"ok\":true,\"dir\":" + common::jsonStringify(rel) + ",\"items\":" + items + "}";
}

std::string Handlers::handleIdeRead(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    (void)method; (void)body; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false,\"msg\":\"请先登录\"}";
    std::string raw = common::urlDecode(queryParam(path, "p"));
    std::string rel, full;
    bool problemScope = false;
    if (raw.rfind("@problem/", 0) == 0) {
        std::string rest = raw.substr(9);
        size_t sl = rest.find('/');
        if (sl == std::string::npos) return "{\"ok\":false,\"msg\":\"路径不合法\"}";
        std::string pid = sanitizeFilename(rest.substr(0, sl));
        std::string file = sanitizeFilename(rest.substr(sl + 1));
        Problem* pb = DataStore::instance().findProblemById(pid);
        if (!pb) return "{\"ok\":false,\"msg\":\"题目不存在\"}";
        if (!canManageProblems(user) && !isProblemOwner(pb, user))
            return "{\"ok\":false,\"msg\":\"没有权限编辑该题数据\"}";
        if (file.empty()) return "{\"ok\":false,\"msg\":\"文件名不合法\"}";
        full = "./problems/" + pid + "/" + file;
        rel = raw;
        problemScope = true;
    } else {
        rel = sanitizeRelPath(raw);
        if (rel.empty()) return "{\"ok\":false,\"msg\":\"缺少文件路径\"}";
        full = "./files/" + user->id + "/" + rel;
    }
    DWORD at = GetFileAttributesA(full.c_str());
    if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY))
        return "{\"ok\":false,\"msg\":\"文件不存在\"}";
    if (driveKindOf(rel, false) != "text")
        return "{\"ok\":false,\"msg\":\"不是文本 / 代码文件，IDE 只能打开这类文件\"}";
    std::string content = common::readFile(full);
    if (!common::isValidUtf8(content)) content = common::toUtf8FromSystem(content);
    bool truncated = false;
    if (content.size() > 1024 * 1024) { content = content.substr(0, 1024 * 1024); truncated = true; }
    return "{\"ok\":true,\"path\":" + common::jsonStringify(rel) +
           ",\"name\":" + common::jsonStringify(drvBaseName(rel)) +
           ",\"ext\":" + common::jsonStringify(drvLowerExt(rel)) +
           ",\"rev\":" + common::jsonStringify(drvFileRev(full)) +
           ",\"truncated\":" + std::string(truncated ? "true" : "false") +
           ",\"content\":" + common::jsonStringify(content) + "}";
}

std::string Handlers::handleIdeSave(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    (void)method; (void)path; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false,\"msg\":\"请先登录\"}";
    std::string raw = parseFormData(body, "path");
    std::string code = parseFormData(body, "code");
    if (code.size() > 2 * 1024 * 1024) return "{\"ok\":false,\"msg\":\"内容过大（2MB 上限）\"}";
    std::string rel, full;
    std::string syncPid, syncFile;
    if (raw.rfind("@problem/", 0) == 0) {
        std::string rest = raw.substr(9);
        size_t sl = rest.find('/');
        if (sl == std::string::npos) return "{\"ok\":false,\"msg\":\"路径不合法\"}";
        std::string pid = sanitizeFilename(rest.substr(0, sl));
        std::string file = sanitizeFilename(rest.substr(sl + 1));
        Problem* pb = DataStore::instance().findProblemById(pid);
        if (!pb) return "{\"ok\":false,\"msg\":\"题目不存在\"}";
        if (!canManageProblems(user) && !isProblemOwner(pb, user))
            return "{\"ok\":false,\"msg\":\"没有权限编辑该题数据\"}";
        if (file.empty()) return "{\"ok\":false,\"msg\":\"文件名不合法\"}";
        full = "./problems/" + pid + "/" + file;
        rel = raw;
        syncPid = pid;
        syncFile = file;
        common::createDirectory("./problems/" + pid + "/");
    } else {
        rel = sanitizeRelPath(raw);
        if (rel.empty()) return "{\"ok\":false,\"msg\":\"文件名不合法\"}";
        full = "./files/" + user->id + "/" + rel;
        if (GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES &&
            (GetFileAttributesA(full.c_str()) & FILE_ATTRIBUTE_DIRECTORY))
            return "{\"ok\":false,\"msg\":\"同名文件夹已存在\"}";
        size_t sp = full.find_last_of("/\\");
        if (sp != std::string::npos) common::createDirectory(full.substr(0, sp));
    }
    if (!common::writeFile(full, code)) return "{\"ok\":false,\"msg\":\"写入失败\"}";
    // 题目作用域：std.cpp / generator.cpp 与题目字段保持同步，IDE 改完即生效
    if (!syncPid.empty()) {
        Problem* pb = DataStore::instance().findProblemById(syncPid);
        if (pb) {
            bool dirty = false;
            if (syncFile == "std.cpp") { pb->standardSolution = code; dirty = true; }
            else if (syncFile == "generator.cpp") { pb->generator = code; dirty = true; }
            if (dirty) DataStore::instance().saveProblems();
        }
    }
    return "{\"ok\":true,\"path\":" + common::jsonStringify(rel) +
           ",\"rev\":" + common::jsonStringify(drvFileRev(full)) +
           ",\"size\":" + std::to_string(code.size()) + "}";
}

std::string Handlers::handleCaptchaApi(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    (void)method; (void)body;
    (void)headers; (void)sessionId;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    std::string feature = queryParam(path, "feature");
    static const char* okFeatures[] = {"submit", "login", "register", "blog", "discuss",
                                       "task", "shop", "message", "group", "benben"};
    bool known = false;
    for (size_t i = 0; i < sizeof(okFeatures) / sizeof(okFeatures[0]); i++)
        if (feature == okFeatures[i]) known = true;
    if (!known) feature = "submit";
    return security::captchaJsonFor(feature);
}

// ---------- 属性 ----------
std::string Handlers::handleDriveProps(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    if (!user) return "{\"ok\":false}";
    std::string rel = sanitizeRelPath(drvQueryValue(path, "p"));
    std::string full = "./files/" + user->id + "/" + rel;
    DWORD attr = GetFileAttributesA(full.c_str());
    if (rel.empty() || attr == INVALID_FILE_ATTRIBUTES) return "{\"ok\":false,\"msg\":\"路径不存在\"}";
    bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    long long bytes = 0; int files = 0, dirs = 0;
    if (isDir) drvStatDir(full + "/", bytes, files, dirs);
    else bytes = getFileSizeBytes(full);
    WIN32_FILE_ATTRIBUTE_DATA wfad;
    std::string created, modified;
    if (GetFileAttributesExA(full.c_str(), GetFileExInfoStandard, &wfad)) {
        FILETIME lc, lm; SYSTEMTIME st;
        FileTimeToLocalFileTime(&wfad.ftCreationTime, &lc);
        FileTimeToSystemTime(&lc, &st);
        char b1[40]; snprintf(b1, sizeof(b1), "%04d-%02d-%02d %02d:%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute);
        created = b1;
        FileTimeToLocalFileTime(&wfad.ftLastWriteTime, &lm);
        FileTimeToSystemTime(&lm, &st);
        char b2[40]; snprintf(b2, sizeof(b2), "%04d-%02d-%02d %02d:%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute);
        modified = b2;
    }
    // 是否已分享
    std::string shareToken, sharePerm;
    std::vector<std::map<std::string, std::string> > allShares = loadShares();
    for (size_t i = 0; i < allShares.size(); i++)
        if (allShares[i]["userId"] == user->id && allShares[i]["filename"] == rel) {
            shareToken = allShares[i]["token"];
            sharePerm = allShares[i].count("perm") ? allShares[i]["perm"] : "view";
        }
    std::string json = "{\"ok\":true,\"name\":\"" + common::jsonEscape(drvBaseName(rel)) + "\""
        ",\"path\":\"" + common::jsonEscape(rel) + "\""
        ",\"isDir\":" + std::string(isDir ? "true" : "false") +
        ",\"size\":" + std::to_string(bytes) +
        ",\"files\":" + std::to_string(files) +
        ",\"dirs\":" + std::to_string(dirs) +
        ",\"perm\":\"" + common::jsonEscape(sharePerm) + "\""
        ",\"ext\":\"" + common::jsonEscape(drvLowerExt(rel)) + "\""
        ",\"kind\":\"" + driveKindOf(rel, isDir) + "\""
        ",\"created\":\"" + created + "\",\"modified\":\"" + modified + "\""
        ",\"share\":\"" + common::jsonEscape(shareToken) + "\"}";
    return json;
}

std::string Handlers::handleDriveShortcut(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false,\"msg\":\"请先登录\"}";
    std::string action = parseFormData(body, "action");
    std::string token = parseFormData(body, "token");
    if (token.empty() || token.size() > 64) return "{\"ok\":false,\"msg\":\"令牌无效\"}";
    std::vector<std::map<std::string, std::string> > sc = loadShortcuts();
    if (action == "remove") {
        std::vector<std::map<std::string, std::string> > keep;
        for (size_t i = 0; i < sc.size(); i++)
            if (!(sc[i]["userId"] == user->id && sc[i]["token"] == token)) keep.push_back(sc[i]);
        saveShortcuts(keep);
        return "{\"ok\":true,\"msg\":\"已移除快捷方式\"}";
    }
    std::vector<std::map<std::string, std::string> > shares = loadShares();
    std::string ownerId, filename, isDir;
    for (size_t i = 0; i < shares.size(); i++)
        if (shares[i]["token"] == token) {
            ownerId = shares[i]["userId"];
            filename = shares[i]["filename"];
            isDir = shares[i].count("isDir") ? shares[i]["isDir"] : "";
            break;
        }
    if (ownerId.empty()) return "{\"ok\":false,\"msg\":\"分享不存在或已取消\"}";
    for (size_t i = 0; i < sc.size(); i++)
        if (sc[i]["userId"] == user->id && sc[i]["token"] == token) return "{\"ok\":true,\"msg\":\"已在你的云盘里\"}";
    std::map<std::string, std::string> e;
    e["userId"] = user->id;
    e["token"] = token;
    e["ownerId"] = ownerId;
    e["path"] = filename;
    e["name"] = drvBaseName(filename);
    e["isDir"] = isDir;
    e["time"] = common::getCurrentTimeStr();
    sc.push_back(e);
    saveShortcuts(sc);
    return "{\"ok\":true,\"msg\":\"已添加到我的云盘\"}";
}

std::string Handlers::handleDriveMkdir(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rel = sanitizeRelPath(parseFormData(body, "dir"));
    std::string name = sanitizeFilename(parseFormData(body, "name"));
    std::string dir = "./files/" + user->id + "/" + (rel.empty() ? "" : rel + "/") + name + "/";
    if (!name.empty()) common::createDirectory(dir);
    std::string back = rel.empty() ? "/drive" : ("/drive/folder/" + rel);
    return "<script>window.location.href='" + back + "'</script>";
}

std::string Handlers::handleDriveRename(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rel = sanitizeRelPath(parseFormData(body, "dir"));
    std::string file = sanitizeFilename(parseFormData(body, "file"));
    std::string nn = sanitizeFilename(parseFormData(body, "newname"));
    std::string dir = "./files/" + user->id + "/" + (rel.empty() ? "" : rel + "/");
    if (!file.empty() && !nn.empty() && file != nn) {
        MoveFileA((dir + file).c_str(), (dir + nn).c_str());
    }
    std::string back = rel.empty() ? "/drive" : ("/drive/folder/" + rel);
    return "<script>window.location.href='" + back + "'</script>";
}

std::string Handlers::handleDriveUpload(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::map<std::string, std::string> fields;
    std::vector<UploadedFile> files;
    parseMultipartMulti(body, extractBoundary(headers), fields, files);
    std::string rel = sanitizeRelPath(fields["dir"]);
    std::string dir = "./files/" + user->id + "/" + (rel.empty() ? "" : rel + "/");
    common::createDirectory(dir);
    long long maxPer = (long long)getDriveMaxMB() * 1024 * 1024;
    long long maxTotal = (long long)getUserDriveTotalMB(user) * 1024 * 1024;
    long long usedTotal = dirSizeBytes("./files/" + user->id + "/");
    std::string back = rel.empty() ? "/drive" : ("/drive/folder/" + rel);
    if (files.empty()) {
        std::string c = "<div class='alert alert-error'>没有收到任何文件，请重新选择后再上传（文件过大或请求不完整都可能收不到）。</div>"
                       "<a href='" + back + "' class='btn'>返回云盘</a>";
        return renderTemplate("上传结果", c, user);
    }
    int ok = 0;
    std::string failList;
    for (const auto& f : files) {
        std::string relPath = sanitizeRelName(f.filename);
        std::string reason;
        if (relPath.empty()) { reason = "无效文件名"; }
        else if (f.data.size() > (size_t)maxPer) { reason = "超过单文件上限 " + std::to_string(getDriveMaxMB()) + "MB"; }
        else if (usedTotal + (long long)f.data.size() > maxTotal) { reason = "超过云盘总配额，可用剩余不足"; }
        else {
            std::string target = dir + relPath;
            size_t slash = relPath.find_last_of('/');
            if (slash != std::string::npos) {
                std::string base = dir;
                size_t start = 0;
                bool okD = true;
                while (true) {
                    size_t nxt = relPath.find('/', start);
                    if (nxt == std::string::npos) break;
                    base += relPath.substr(start, nxt - start) + "/";
                    if (!common::createDirectory(base)) { okD = false; break; }
                    start = nxt + 1;
                }
                if (!okD) reason = "无法创建目录";
            }
            if (reason.empty()) {
                if (common::writeFile(target, f.data)) { ok++; usedTotal += (long long)f.data.size(); }
                else reason = "写入失败（目录无权限？）";
            }
        }
        if (!reason.empty()) {
            failList += "<tr><td>" + common::htmlEscape(relPath) + "</td><td>" + humanSize((long long)f.data.size()) + "</td><td style='color:#c0392b;'>" + common::htmlEscape(reason) + "</td></tr>";
        }
    }
    int fail = (int)files.size() - ok;
    std::string c = (fail == 0)
        ? "<div class='alert alert-success'>上传成功：共 " + std::to_string(ok) + " 个文件</div>"
        : "<div class='alert alert-warning'>上传完成：成功 " + std::to_string(ok) + " 个，失败 " + std::to_string(fail) + " 个</div>";
    if (fail > 0) {
        c += "<h3>上传失败明细</h3><table><thead><tr><th>文件</th><th>大小</th><th>原因</th></tr></thead><tbody>" + failList + "</tbody></table>";
    }
    c += "<div style='margin-top:16px;'><a href='" + back + "' class='btn'>返回云盘</a></div>";
    return renderTemplate("上传结果", c, user);
}

std::string Handlers::handleDriveDownload(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rel = sanitizeRelPath(common::urlDecode(path.substr(std::string("/drive/download/").size())));
    if (rel.empty()) return "<div class='alert alert-error'>\u8def\u5f84\u65e0\u6548</div><a href='/drive' class='btn'>\u8fd4\u56de</a>";
    std::string fp = "./files/" + user->id + "/" + rel;
    std::string fname = rel.substr(rel.find_last_of('/') + 1);
    if (!common::fileExists(fp)) return "<div class='alert alert-error'>\u6587\u4ef6\u4e0d\u5b58\u5728</div><a href='/drive' class='btn'>\u8fd4\u56de</a>";
    respHeaders["Content-Type"] = "application/octet-stream";
    respHeaders["Content-Disposition"] = "attachment; filename=\"" + sanitizeFilename(fname) + "\"";
    return common::readFile(fp);
}

std::string Handlers::handleDriveDelete(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rel = sanitizeRelPath(parseFormData(body, "dir"));
    std::string dir = "./files/" + user->id + "/" + (rel.empty() ? "" : rel + "/");
    // 批量删除：paths 为换行分隔的相对路径；兼容旧表单的单个 file
    std::string multi = parseFormData(body, "paths");
    int deleted = 0;
    if (!multi.empty()) {
        std::vector<std::string> list = drvSplitPaths(multi);
        for (size_t i = 0; i < list.size(); i++) {
            std::string p = list[i];
            if (p.empty() || p.find("..") != std::string::npos) continue;
            // rel 为空（云盘根目录）时不应该把任何路径排除掉
            if (!rel.empty() && p.compare(0, rel.size() + 1, rel + "/") != 0 && p != rel) continue;
            deletePathRecursive("./files/" + user->id + "/" + p);
            deleted++;
        }
    } else {
        std::string fname = sanitizeFilename(parseFormData(body, "file"));
        deletePathRecursive(dir + fname);
        deleted = 1;
    }
    if (!multi.empty()) {
        respHeaders["Content-Type"] = "application/json; charset=utf-8";
        return "{\"ok\":true,\"deleted\":" + std::to_string(deleted) + "}";
    }
    std::string back = rel.empty() ? "/drive" : ("/drive/folder/" + rel);
    return "<script>window.location.href='" + back + "'</script>";
}

// ============================================================
// Problem test-data upload (\u9898\u76ee\u6d4b\u8bd5\u6570\u636e)
// ============================================================
// 取表单里同名的全部值（多选框）
static std::vector<std::string> formAllValues(const std::string& body, const std::string& key) {
    std::vector<std::string> out;
    std::vector<std::string> parts = common::split(body, '&');
    for (size_t i = 0; i < parts.size(); i++) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        if (common::urlDecode(parts[i].substr(0, eq)) == key)
            out.push_back(common::urlDecode(parts[i].substr(eq + 1)));
    }
    return out;
}

// 子任务文本：每行 "名称|分值|1-5,7,9"
static std::vector<Subtask> parseSubtaskSpec(const std::string& text) {
    std::vector<Subtask> out;
    std::vector<std::string> lines = common::split(text, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = common::trim(lines[i]);
        if (line.empty()) continue;
        std::vector<std::string> parts = common::split(line, '|');
        Subtask st;
        st.name = parts.size() > 0 ? common::trim(parts[0]) : "";
        st.score = parts.size() > 1 ? common::stringToInt(common::trim(parts[1])) : 0;
        std::string spec = parts.size() > 2 ? common::trim(parts[2]) : "";
        std::vector<std::string> toks = common::split(spec, ',');
        for (size_t k = 0; k < toks.size(); k++) {
            std::string tk = common::trim(toks[k]);
            if (tk.empty()) continue;
            size_t dash = tk.find('-');
            if (dash != std::string::npos && dash > 0) {
                int a = common::stringToInt(common::trim(tk.substr(0, dash)));
                int b = common::stringToInt(common::trim(tk.substr(dash + 1)));
                if (a > 0 && b >= a && b - a <= 500) {
                    for (int n = a; n <= b; n++) st.tests.push_back(std::to_string(n));
                    continue;
                }
            }
            st.tests.push_back(tk);
        }
        if (st.tests.empty() && st.score <= 0) continue;
        if (st.name.empty()) st.name = "子任务 " + std::to_string(out.size() + 1);
        out.push_back(st);
    }
    return out;
}

// 把多行文本变成 // 注释，用于生成器模板
static std::string commentLines(const std::string& s) {
    std::string out;
    std::vector<std::string> lines = common::split(s, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        std::string t = common::trim(lines[i]);
        if (t.empty()) continue;
        out += "//   " + t + "\n";
    }
    return out;
}

static std::string subtaskSpecOf(const std::vector<Subtask>& subs) {    std::string out;
    for (size_t i = 0; i < subs.size(); i++) {
        if (i) out += "\n";
        out += subs[i].name + "|" + std::to_string(subs[i].score) + "|";
        for (size_t k = 0; k < subs[i].tests.size(); k++) {
            if (k) out += ",";
            out += subs[i].tests[k];
        }
    }
    return out;
}

std::string Handlers::handleAdminProblemData(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string pid = path.substr(path.find_last_of('/') + 1);
    std::string dir = "./problems/" + pid + "/";
    common::createDirectory(dir);
    Problem* _p = DataStore::instance().findProblemById(pid);
    bool _dataOwner = canManageProblems(user) || (_p && _p->author == user->username);
    if (!_dataOwner) return "<script>window.location.href='/'</script>";
    std::string backUrl = canManageProblems(user) ? "/admin/problems" : "/myproblem";
    if (method == "POST") {
        std::map<std::string, std::string> fields;
        std::vector<UploadedFile> files;
        std::string action, dfile;
        std::string ctype = headers.count("content-type") ? headers.at("content-type") : "";
        if (ctype.find("multipart/form-data") != std::string::npos) {
            parseMultipartMulti(body, extractBoundary(headers), fields, files);
            action = fields["action"];
            dfile = fields["file"];
        } else {
            action = parseFormData(body, "action");
            dfile = parseFormData(body, "file");
        }
        if (action == "savestd" || action == "verifystd") {
            std::string std = parseFormData(body, "std");
            std::string lang = parseFormData(body, "lang"); if (lang.empty()) lang = "cpp";
            if (_p) { _p->standardSolution = std; DataStore::instance().saveProblems(); }
            if (action == "verifystd") {
                std::string detail; int passed = 0, total = 0;
                bool ok = _p ? Judge::duelScore(lang, std, _p, passed, total, detail) : false;
                std::string vc;
                if (!ok) vc = "<div class='alert alert-error'>标程未通过编译：" + common::htmlEscape(detail) + "</div>";
                else vc = "<div class='alert alert-success'>&#9989;数据验证：标程通过 " + std::to_string(passed) + "/" + std::to_string(total) + " 个测试点</div>";
                vc += "<a href='/admin/problem/data/" + pid + "' class='btn'>返回</a>";
                return renderTemplate("测试数据", vc, user);
            }
            std::string c2 = "<div class='alert alert-success'>标程已保存</div><a href='/admin/problem/data/" + pid + "' class='btn'>返回</a>";
            return renderTemplate("测试数据", c2, user);
        }
        if (action == "delete" && !dfile.empty()) {
            DeleteFileA((dir + sanitizeFilename(dfile)).c_str());
        } else if (action == "deleteMulti") {
            // 批量删除测试点：勾选的 .in/.out 成对删除
            std::vector<std::string> stems = formAllValues(body, "del");
            int removed = 0;
            for (size_t i = 0; i < stems.size(); i++) {
                std::string stem = sanitizeFilename(stems[i]);
                if (stem.empty()) continue;
                DeleteFileA((dir + stem + ".in").c_str());
                DeleteFileA((dir + stem + ".out").c_str());
                removed++;
            }
            std::string c2 = "<div class='alert alert-success'>已删除 " + std::to_string(removed) + " 组测试点</div>"
                             "<a href='/admin/problem/data/" + pid + "' class='btn'>返回</a>";
            return renderTemplate("测试数据", c2, user);
        } else if (action == "savesub") {
            if (_p) {
                _p->subtaskMode = (parseFormData(body, "subtaskMode") == "1");
                _p->subtasks = parseSubtaskSpec(parseFormData(body, "subtasks"));
                DataStore::instance().saveProblems();
            }
            return "<script>window.location.href='/admin/problem/data/" + pid + "'</script>";
        } else if (action == "savegen") {
            if (_p) {
                _p->generator = parseFormData(body, "generator");
                _p->dataRange = parseFormData(body, "dataRange");
                DataStore::instance().saveProblems();
            }
            std::string c2 = "<div class='alert alert-success'>生成器与数据范围已保存</div>"
                             "<a href='/admin/problem/data/" + pid + "' class='btn'>返回</a>";
            return renderTemplate("测试数据", c2, user);
        } else if (action == "generate") {
            if (!_p) return "<script>window.location.href='/admin/problem/data/" + pid + "'</script>";
            int count = common::stringToInt(parseFormData(body, "count"));
            int seed = common::stringToInt(parseFormData(body, "seed"));
            if (count <= 0) count = 10;
            if (seed <= 0) seed = 1;
            if (parseFormData(body, "clean") == "1") {
                std::vector<std::string> old = listDirFiles(dir);
                for (size_t i = 0; i < old.size(); i++) {
                    std::string ext = common::getFileExtension(old[i]);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == "in" || ext == "out") DeleteFileA((dir + old[i]).c_str());
                }
            }
            int made = 0;
            std::string detail;
            bool ok = Judge::generateData(_p, count, seed, made, detail);
            std::string c2 = std::string("<div class='alert alert-") + (ok ? "success" : "error") + "'>⚙️ " +
                             common::htmlEscape(detail) + "</div>"
                             "<a href='/admin/problem/data/" + pid + "' class='btn'>返回数据页</a>";
            return renderTemplate("生成数据", c2, user);
        } else if (action == "fromdrive") {
            std::string rel = sanitizeRelPath(parseFormData(body, "driveFile"));
            std::string src = "./files/" + user->id + "/" + rel;
            std::string base = rel.substr(rel.find_last_of('/') + 1);
            if (rel.empty() || base.empty() || !common::fileExists(src)) {
                std::string c2 = "<div class='alert alert-error'>云盘文件不存在</div><a href='/admin/problem/data/" + pid + "' class='btn'>返回</a>";
                return renderTemplate("导入失败", c2, user);
            }
            std::string content = common::readFile(src);
            if (base.size() > 4 && base.compare(base.size() - 4, 4, ".out") != 0 &&
                base.size() > 3 && base.compare(base.size() - 3, 3, ".in") != 0) {
                // 非数据文件只按名字复制（std.cpp / generator.cpp 会同步进题目）
            }
            common::writeFile(dir + sanitizeFilename(base), content);
            if (_p && base == "std.cpp") { _p->standardSolution = content; DataStore::instance().saveProblems(); }
            if (_p && base == "generator.cpp") { _p->generator = content; DataStore::instance().saveProblems(); }
            std::string c2 = "<div class='alert alert-success'>已从云盘导入：" + common::htmlEscape(base) + "</div>"
                             "<a href='/admin/problem/data/" + pid + "' class='btn'>返回</a>";
            return renderTemplate("导入完成", c2, user);
        } else if (action == "todrive") {
            std::string targetRel = "problem-" + pid;
            std::string target = "./files/" + user->id + "/" + targetRel + "/";
            common::createDirectory(target);
            std::vector<std::string> all = listDirFiles(dir);
            int copied = 0;
            for (size_t i = 0; i < all.size(); i++) {
                std::string ext = common::getFileExtension(all[i]);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == "exe") continue;
                if (common::writeFile(target + all[i], common::readFile(dir + all[i]))) copied++;
            }
            std::string c2 = "<div class='alert alert-success'>已导出 " + std::to_string(copied) +
                             " 个文件到云盘 <code>" + targetRel + "/</code></div>"
                             "<a href='/drive/folder/" + targetRel + "' class='btn'>去云盘查看</a> "
                             "<a href='/admin/problem/data/" + pid + "' class='btn'>返回</a>";
            return renderTemplate("导出完成", c2, user);
        } else {
            int ok = 0, skipped = 0, sync = 0;
            for (const auto& f : files) {
                std::string fname = sanitizeFilename(f.filename);
                std::string ext = common::getFileExtension(fname);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == "in" || ext == "out") {
                    if (common::writeFile(dir + fname, f.data)) ok++;
                    else skipped++;
                    continue;
                }
                // 允许直接上传标程 / 生成器，同步到题目字段
                if (fname == "std.cpp" || fname == "standard.cpp" || fname == "std.cc") {
                    common::writeFile(dir + "std.cpp", f.data);
                    if (_p) { _p->standardSolution = f.data; sync++; }
                    continue;
                }
                if (fname == "generator.cpp" || fname == "gen.cpp") {
                    common::writeFile(dir + "generator.cpp", f.data);
                    if (_p) { _p->generator = f.data; sync++; }
                    continue;
                }
                skipped++;
            }
            if (sync > 0) DataStore::instance().saveProblems();
            std::string c2 = "<div class='alert alert-success'>\u6d4b\u8bd5\u6570\u636e\uff1a\u4e0a\u4f20 " + std::to_string(ok) + " \u4e2a" +
                             (sync > 0 ? ("\uff0c\u540c\u6b65\u6807\u7a0b/\u751f\u6210\u5668 " + std::to_string(sync) + " \u4e2a") : std::string()) +
                             "\uff0c\u8df3\u8fc7 " + std::to_string(skipped) + " \u4e2a\uff08\u975e .in/.out \u6216\u5931\u8d25\uff09</div>"
                              "<a href='/admin/problem/data/" + pid + "' class='btn'>\u7ee7\u7eed\u4e0a\u4f20</a> "
                              "<a href='/admin/problems' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u6d4b\u8bd5\u6570\u636e", c2, user);
        }
    }
    // 同步标程 / 生成器到磁盘，IDE 可以直接打开编辑
    if (_p && !_p->standardSolution.empty() && !common::fileExists(dir + "std.cpp"))
        common::writeFile(dir + "std.cpp", _p->standardSolution);
    if (_p && !_p->generator.empty() && !common::fileExists(dir + "generator.cpp"))
        common::writeFile(dir + "generator.cpp", _p->generator);

    std::vector<std::string> files = listDirFiles(dir);
    std::vector<std::string> stems;
    for (size_t i = 0; i < files.size(); i++) {
        const std::string& f = files[i];
        if (f.size() > 3 && f.compare(f.size() - 3, 3, ".in") == 0) {
            std::string stem = f.substr(0, f.size() - 3);
            bool dup = false;
            for (size_t k = 0; k < stems.size(); k++) if (stems[k] == stem) dup = true;
            if (!dup) stems.push_back(stem);
        }
    }
    std::sort(stems.begin(), stems.end());

    std::string pageUrl = "/admin/problem/data/" + common::htmlEscape(pid);
    std::string content;
    content += "<div class='action-bar' style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
               "<h2 style='margin:0;'>📦 题目 " + common::htmlEscape(pid) + (_p ? (" · " + common::htmlEscape(_p->title)) : std::string()) + " · 数据</h2>"
               "<div style='display:flex;gap:8px;flex-wrap:wrap;'>"
               "<a class='btn btn-sm' href='/run?file=@problem/" + common::htmlEscape(pid) + "/std.cpp' target='_blank'>🧑‍💻 IDE 编辑标程</a>"
               "<a class='btn btn-sm' href='/run?file=@problem/" + common::htmlEscape(pid) + "/generator.cpp' target='_blank'>🧑‍💻 IDE 编辑生成器</a>"
               "<a class='btn btn-sm btn-outline' href='" + backUrl + "'>返回</a></div></div>";
    content += "<div class='alert alert-info'>测试数据目录 <code>./problems/" + common::htmlEscape(pid) +
               "/</code>：评测机按 <code>*.in</code> 找测试点，读同名 <code>*.out</code> 比对；也可以由「生成器 + 标程」一键产出。</div>";

    // ---------- 评测方式（子任务） ----------
    std::string subSpec = _p ? subtaskSpecOf(_p->subtasks) : std::string();
    int subSum = 0;
    if (_p) for (size_t i = 0; i < _p->subtasks.size(); i++) subSum += _p->subtasks[i].score;
    content += "<div class='card'><h3 style='margin-top:0;'>🧮 评测方式</h3>"
               "<form method='POST' action='" + pageUrl + "'>"
               "<input type='hidden' name='action' value='savesub'/>"
               "<label style='display:flex;gap:8px;align-items:center;font-weight:600;'>"
               "<input type='checkbox' name='subtaskMode' value='1'" + std::string(_p && _p->subtaskMode ? " checked" : "") + "/> 启用子任务（捆绑测试）计分</label>"
               "<div class='card-meta'>开启后：同一子任务内的测试点 <b>全部通过</b> 才拿该组分数，整题得分 = 各组之和；关闭则按通过比例给分（传统模式）。</div>"
               "<div class='form-group' style='margin-top:10px;'><label>子任务（每行：名称|分值|测试点，如 小数据|30|1-5）</label>"
               "<textarea name='subtasks' rows='5' style='font-family:Consolas,monospace;font-size:13px;' placeholder='小数据|30|1-5\n大数据|70|6-10'>" + common::htmlEscape(subSpec) + "</textarea></div>"
               "<div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap;'><button class='btn' type='submit'>保存评测方式</button>"
               "<span class='card-meta' style='margin:0;'>当前 " + std::to_string(_p ? _p->subtasks.size() : 0) + " 个子任务，合计 " + std::to_string(subSum) + " 分</span></div></form></div>";

    // ---------- 标程 ----------
    content += "<div class='card'><h3 style='margin-top:0;'>&#128202; 标程（标准答案：生成数据 / 验证 / Hack 校验）</h3>"
               "<form method='POST' action='" + pageUrl + "'>"
               "<input type='hidden' name='action' value='savestd'/>"
               "<div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:8px;'>"
               "<input type='file' id='stdFile' accept='.cpp,.cc,.cxx,.c,.h,.hpp,.txt' style='font-size:12.5px;max-width:320px;'/>"
               "<span class='card-meta' style='margin:0;'>选择本地文件直接填入下方代码框（自动识别 UTF-8 / GBK）</span></div>"
               "<div class='form-group'><label>标程代码（C++）</label><textarea name='std' id='stdBox' rows='10' style='font-family:Consolas,monospace;font-size:13px;' placeholder='粘贴标准答案代码（须能 AC 本题）'>" + common::htmlEscape(_p ? _p->standardSolution : "") + "</textarea></div>"
               "<div style='display:flex;gap:8px;flex-wrap:wrap;'><button type='submit' name='action' value='savestd' class='btn'>&#128190; 保存标程</button>"
               "<button type='submit' name='action' value='verifystd' class='btn' style='background:#27ae60;'>&#9989; 保存并验证数据</button></div></form></div>";

    // ---------- 生成器 ----------
    std::string tplCode = "// 数据生成器 · 题目 " + pid + "\n"
        "// 运行约定：从 stdin 读入 caseNo seed，向 stdout 输出这一组输入\n"
        "// 输入格式：\n" + commentLines(_p ? _p->inputDesc : std::string()) +
        "// 数据范围：\n" + commentLines(_p ? _p->dataRange : std::string()) +
        "#include <bits/stdc++.h>\n"
        "using namespace std;\n"
        "int main() {\n"
        "    int caseNo = 1; unsigned long long seed = 1;\n"
        "    if (!(cin >> caseNo >> seed)) return 0;\n"
        "    mt19937_64 rng(seed * 1000003ull + caseNo);\n"
        "    // TODO: 按上面的输入格式生成数据，例如：\n"
        "    // int n = min(10, caseNo * 3);\n"
        "    // cout << n << \"\\n\";\n"
        "    return 0;\n"
        "}\n";
    content += "<div class='card'><h3 style='margin-top:0;'>🛠 数据生成器</h3>"
               "<div class='card-meta'>生成器每次被调用时从 stdin 读到 <code>caseNo seed</code>，把这一组输入写到 stdout；系统再用标程跑出 <code>.out</code>。确定性随机，请用 seed 播种。</div>"
               "<form method='POST' action='" + pageUrl + "' id='genForm'>"
               "<input type='hidden' name='action' value='savegen'/>"
               "<div class='form-group'><label>数据范围 / 约束</label><textarea name='dataRange' rows='3' placeholder='如：1 ≤ n ≤ 10^5；1 ≤ a_i ≤ 10^9'>" + common::htmlEscape(_p ? _p->dataRange : std::string()) + "</textarea></div>"
               "<div class='form-group'><label>生成器源码</label>"
               "<div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:6px;'>"
               "<input type='file' id='genFile' accept='.cpp,.cc,.cxx,.c,.h,.hpp,.txt' style='font-size:12.5px;max-width:320px;'/>"
               "<span class='card-meta' style='margin:0;'>选择本地生成器文件直接填入下方代码框</span></div>"
               "<textarea name='generator' id='genBox' rows='14' style='font-family:Consolas,monospace;font-size:13px;' placeholder='读 caseNo seed，输出一组输入'>" + common::htmlEscape(_p ? _p->generator : std::string()) + "</textarea></div>"
               "<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;'><button class='btn' type='submit'>💾 保存生成器</button>"
               "<button class='btn btn-sm btn-outline' type='button' onclick='useTpl()'>📄 生成模板</button>"
               "<button class='btn btn-sm btn-outline' type='button' onclick='aiGen()'>🤖 AI 工作台</button>"
               "<span id='genMsg' style='color:var(--muted);font-size:12.5px;'></span></div></form>";
    content += "<form method='POST' action='" + pageUrl + "' style='margin-top:12px;padding-top:12px;border-top:1px dashed var(--border);'>"
               "<input type='hidden' name='action' value='generate'/>"
               "<div style='display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end;'>"
               "<div class='form-group' style='margin:0;'><label>生成组数</label><input name='count' type='number' min='1' max='100' value='10' style='width:100px;'/></div>"
               "<div class='form-group' style='margin:0;'><label>起始种子</label><input name='seed' type='number' min='1' value='1' style='width:120px;'/></div>"
               "<label style='display:flex;gap:6px;align-items:center;font-size:13px;'><input type='checkbox' name='clean' value='1' checked/> 先清空旧的 .in/.out</label>"
               "<button class='btn' type='submit'>⚙️ 生成数据</button></div>"
               "<div class='card-meta'>文件按 1.in/1.out、2.in/2.out… 命名，与子任务里的测试点编号对应。</div></form></div>";

    // ---------- 测试点 ----------
    content += "<div class='card'><div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
               "<h3 style='margin:0;'>📁 测试点（" + std::to_string(stems.size()) + "）</h3>"
               "<div style='display:flex;gap:6px;flex-wrap:wrap;'>"
               "<button class='btn btn-sm btn-outline' type='button' onclick='selAll(1)'>全选</button>"
               "<button class='btn btn-sm btn-outline' type='button' onclick='selAll(0)'>全不选</button>"
               "<button class='btn btn-sm btn-danger' type='button' onclick='delSel()'>🗑 批量删除</button></div></div>";
    if (stems.empty()) {
        content += "<div class='alert alert-warning' style='margin-top:10px;'>还没有测试数据：可以上传 .in/.out，也可以用生成器一键生成。</div>";
    } else {
        content += "<form id='delForm' method='POST' action='" + pageUrl + "' style='margin-top:10px;'>"
                   "<input type='hidden' name='action' value='deleteMulti'/>"
                   "<table style='width:100%;font-size:13px;'><thead><tr><th style='width:36px;'></th><th>测试点</th><th>输入大小</th><th>输出大小</th><th style='width:90px;'>操作</th></tr></thead><tbody>";
        for (size_t i = 0; i < stems.size(); i++) {
            const std::string& stem = stems[i];
            bool hasOut = common::fileExists(dir + stem + ".out");
            content += "<tr><td><input type='checkbox' name='del' value='" + common::htmlEscape(stem) + "'/></td>"
                       "<td><code>" + common::htmlEscape(stem) + "</code>" + std::string(hasOut ? "" : " <span style='color:#e6a23c;font-size:12px;'>缺 .out</span>") + "</td>"
                       "<td>" + humanSize(getFileSizeBytes(dir + stem + ".in")) + "</td>"
                       "<td>" + (hasOut ? humanSize(getFileSizeBytes(dir + stem + ".out")) : std::string("-")) + "</td>"
                       "<td><form method='POST' action='" + pageUrl + "' style='display:inline;'>"
                       "<input type='hidden' name='action' value='delete'/><input type='hidden' name='file' value='" + common::htmlEscape(stem + ".in") + "'/>"
                       "<button class='btn btn-sm btn-danger' type='submit' onclick=\"return confirm('删除该测试点？')\">删除</button></form></td></tr>";
        }
        content += "</tbody></table></form>";
    }
    content += "<form method='POST' action='" + pageUrl + "' enctype='multipart/form-data' style='margin-top:14px;'>"
               "<div class='form-group'><label>上传测试数据（.in / .out，可多选；也支持直接上传 std.cpp / generator.cpp）</label><input type='file' name='files' multiple accept='.in,.out,.cpp,.cc,.txt'/></div>"
               "<button class='btn' type='submit'>上传</button></form></div>";

    // ---------- 云盘打通 ----------
    std::string driveTreeJson = "[";
    {
        std::vector<std::string> dfAbs, dfRel;
        dfAbs.push_back("./files/" + user->id + "/");
        dfRel.push_back("");
        bool firstT = true;
        for (size_t qi = 0; qi < dfAbs.size() && dfRel.size() < 600; qi++) {
            std::vector<DrvEntry> es = listDriveEntries(dfAbs[qi]);
            for (size_t k = 0; k < es.size() && dfRel.size() < 600; k++) {
                std::string childRel = dfRel[qi].empty() ? es[k].name : (dfRel[qi] + "/" + es[k].name);
                if (!firstT) driveTreeJson += ",";
                firstT = false;
                driveTreeJson += "{\"p\":\"" + common::jsonEscape(childRel) + "\",\"n\":\"" + common::jsonEscape(es[k].name) +
                                 "\",\"d\":" + (es[k].isDir ? "true" : "false") + ",\"s\":" + std::to_string(es[k].size) + "}";
                if (es[k].isDir) {
                    dfAbs.push_back(dfAbs[qi] + es[k].name + "/");
                    dfRel.push_back(childRel);
                }
            }
        }
    }
    driveTreeJson += "]";
    content += "<div class='card'><h3 style='margin-top:0;'>☁ 云盘</h3>"
               "<div class='card-meta'>把云盘里的数据文件 / std.cpp / generator.cpp 导入到本题，或把本题全部数据导出到云盘目录，方便协作与备份。</div>"
               "<form method='POST' action='" + pageUrl + "' style='display:flex;gap:8px;flex-wrap:wrap;align-items:flex-end;margin-top:10px;'>"
               "<input type='hidden' name='action' value='fromdrive'/>"
               "<div class='form-group' style='margin:0;flex:1;min-width:300px;'><label>从云盘导入</label>"
               "<div style='display:flex;gap:6px;'>"
               "<input type='text' name='driveFile' id='driveFile' placeholder='点「浏览云盘」选择文件，也可直接粘贴路径' style='flex:1;'/>"
               "<button type='button' class='btn btn-sm' onclick='pickDrive()'>📁 浏览云盘</button></div></div>"
               "<button class='btn' type='submit'>📂 导入到本题</button></form>"
               "<form method='POST' action='" + pageUrl + "' style='margin-top:10px;'>"
               "<input type='hidden' name='action' value='todrive'/>"
               "<button class='btn btn-sm btn-outline' type='submit'>📤 导出本题数据到云盘（problem-" + common::htmlEscape(pid) + "/）</button></form></div>";

    // 云盘文件选择弹窗
    content += "<div id='drivePick' style='display:none;position:fixed;inset:0;z-index:1500;'>"
               "<div style='position:absolute;inset:0;background:rgba(15,23,42,.45);' onclick='closePick()'></div>"
               "<div style='position:relative;margin:8vh auto 0;width:min(680px,94vw);max-height:76vh;display:flex;flex-direction:column;"
               "background:var(--surface);border:1px solid var(--border);border-radius:14px;box-shadow:0 20px 60px rgba(0,0,0,.25);overflow:hidden;'>"
               "<div style='display:flex;gap:8px;align-items:center;padding:10px 14px;border-bottom:1px solid var(--border-soft);'>"
               "<b style='color:var(--text-strong);font-size:14px;'>选择云盘文件</b>"
               "<input id='pickQ' placeholder='搜索文件名…' oninput='renderPick(this.value)' style='margin-left:auto;width:190px;'/>"
               "<button type='button' class='btn btn-sm btn-outline' onclick='closePick()'>关闭</button></div>"
               "<div id='pickCrumb' style='padding:7px 14px;font-size:12.5px;color:var(--muted);border-bottom:1px solid var(--border-soft);"
               "display:flex;gap:8px;align-items:center;flex-wrap:wrap;'></div>"
               "<div id='drivePickList' style='flex:1;overflow:auto;padding:4px 0;'></div></div></div>";

    // ---------- AI 生成器工作台（模型 / 思考强度 / 附加要求） ----------
    std::string modelOpts;
    {
        std::vector<aiprov::ModelEntry> ents = aiprov::entries();
        std::string prefer = aiplan::testModelId();
        bool hasPrefer = false;
        if (!prefer.empty())
            for (size_t i = 0; i < ents.size(); i++)
                if (ents[i].id == prefer && aiplan::userAllowedModel(*user, ents[i].id)) hasPrefer = true;
        bool first = true;
        for (size_t i = 0; i < ents.size(); i++) {
            if (!aiplan::userAllowedModel(*user, ents[i].id)) continue;
            bool sel = hasPrefer ? (ents[i].id == prefer) : first;
            modelOpts += "<option value='" + common::htmlEscape(ents[i].id) + "'" + (sel ? " selected" : "") + ">" +
                         common::htmlEscape(ents[i].name) + " · " + common::htmlEscape(ents[i].providerName) + "</option>";
            first = false;
        }
        if (modelOpts.empty()) modelOpts = "<option value=''>（没有可用模型）</option>";
    }
    content += "<div id='genWb' style='display:none;position:fixed;inset:0;z-index:1600;'>"
               "<div style='position:absolute;inset:0;background:rgba(15,23,42,.45);' onclick='genClose()'></div>"
               "<div style='position:relative;margin:5vh auto 0;width:min(920px,95vw);max-height:88vh;display:flex;flex-direction:column;"
               "background:var(--surface);border:1px solid var(--border);border-radius:14px;box-shadow:0 24px 70px rgba(0,0,0,.3);overflow:hidden;'>"
               "<div style='display:flex;align-items:center;gap:10px;padding:12px 16px;border-bottom:1px solid var(--border-soft);'>"
               "<b style='font-size:15px;color:var(--text-strong);'>🤖 AI 数据生成器工作台</b>"
               "<span style='font-size:12.5px;color:var(--muted);'>题目 " + common::htmlEscape(pid) +
               (_p ? (" · " + common::htmlEscape(_p->title)) : std::string()) + "</span>"
               "<button type='button' class='btn btn-sm btn-outline' style='margin-left:auto;' onclick='genClose()'>✕ 关闭</button></div>"
               "<div style='display:flex;gap:12px;flex-wrap:wrap;align-items:flex-end;padding:12px 16px;border-bottom:1px solid var(--border-soft);'>"
               "<div class='form-group' style='margin:0;'><label>模型</label><select id='genWbModel' style='min-width:230px;'>" + modelOpts + "</select></div>"
               "<div class='form-group' style='margin:0;'><label>思考强度</label>"
               "<select id='genWbEffort'><option value='0'>跟随默认</option><option value='1'>快速</option>"
               "<option value='2' selected>均衡</option><option value='3'>深入</option></select></div>"
               "<div class='form-group' style='margin:0;'><label>输出上限</label>"
               "<input id='genWbTok' type='number' value='4000' min='512' max='8192' step='256' style='width:110px;'/></div>"
               "<span style='font-size:12px;color:var(--muted);padding-bottom:8px;'>思考强度对支持 reasoning 的中转生效</span></div>"
               "<div style='padding:12px 16px;border-bottom:1px solid var(--border-soft);'>"
               "<div class='form-group' style='margin:0;'><label>附加要求（可选）</label>"
               "<textarea id='genWbExtra' rows='2' placeholder='例如：加一组链、一组菊花；保证答案唯一；前 3 组小数据，后 7 组极限'></textarea></div>"
               "<div style='font-size:12px;color:var(--muted);margin-top:6px;'>会参考本题的输入格式 / 数据范围 / 子任务划分 / 标程；标程为空时无法生成。</div></div>"
               "<div style='display:flex;gap:8px;align-items:center;padding:10px 16px;'>"
               "<button type='button' class='btn' id='genWbRun' onclick='genRun()'>▶ 开始生成</button>"
               "<button type='button' class='btn btn-sm btn-outline' id='genWbStop' onclick='genStop()' style='display:none;'>■ 停止</button>"
               "<span id='genWbStatus' style='font-size:12.5px;color:var(--muted);'>待命</span></div>"
               "<div style='flex:1;overflow:auto;border-top:1px solid var(--border-soft);'>"
               "<textarea id='genWbCode' spellcheck='false' placeholder='生成结果会显示在这里' "
               "style='width:100%;height:320px;border:none;border-radius:0;font-family:Consolas,monospace;font-size:12.8px;resize:vertical;'></textarea></div>"
               "<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;padding:10px 16px;border-top:1px solid var(--border-soft);'>"
               "<button type='button' class='btn btn-sm btn-outline' onclick='genCopy()'>📋 复制</button>"
               "<button type='button' class='btn btn-sm' onclick='genUse()'>↧ 填入生成器框</button>"
               "<button type='button' class='btn btn-sm' onclick='genSave()'>💾 填入并保存</button>"
               "<span style='margin-left:auto;font-size:12px;color:var(--muted);'>保存后记得点「⚙️ 生成数据」</span></div></div></div>";

    content += "<script>var GEN_TPL=" + common::jsonStringify(tplCode) + ";"
               "function selAll(v){var b=document.querySelectorAll('input[name=del]');for(var i=0;i<b.length;i++)b[i].checked=!!v;}"
               "function delSel(){var b=document.querySelectorAll('input[name=del]');var n=0;for(var i=0;i<b.length;i++)if(b[i].checked)n++;"
               "if(!n){alert('先勾选要删除的测试点');return;}if(!confirm('删除选中的 '+n+' 组测试点？'))return;document.getElementById('delForm').submit();}"
               "function useTpl(){document.getElementById('genBox').value=GEN_TPL;document.getElementById('genMsg').textContent='已填入模板，请补全生成逻辑';}</script>";

    // 本地文件填入代码框 + 云盘文件选择器
    content += "<script>var DRV_TREE=" + driveTreeJson + ";"
               "function fmtSize(s){if(s<1024)return s+' B';if(s<1048576)return (s/1024).toFixed(1)+' KB';return (s/1048576).toFixed(1)+' MB';}"
               "function fileToText(f,cb){var fr=new FileReader();fr.onload=function(){"
               "var u8=new Uint8Array(fr.result);var txt=new TextDecoder('utf-8').decode(u8);"
               "if(txt.indexOf(String.fromCharCode(65533))>=0){try{txt=new TextDecoder('gbk').decode(u8);}catch(e){}}cb(txt);};fr.readAsArrayBuffer(f);}"
               "function attachFile(inpId,boxId){var inp=document.getElementById(inpId);if(!inp)return;"
               "inp.onchange=function(){if(!inp.files||!inp.files.length)return;var box=document.getElementById(boxId);"
               "fileToText(inp.files[0],function(t){box.value=t;box.style.borderColor='var(--brand)';box.focus();"
               "setTimeout(function(){box.style.borderColor='';},1200);});};}"
               "attachFile('stdFile','stdBox');attachFile('genFile','genBox');"
               "var pickDir='';"
               "function pickDrive(){var m=document.getElementById('drivePick');m.style.display='block';pickDir='';"
               "document.getElementById('pickQ').value='';renderPick('');}"
               "function closePick(){document.getElementById('drivePick').style.display='none';}"
               "function pickUp(){var i=pickDir.lastIndexOf('/');pickDir=i<0?'':pickDir.slice(0,i);renderPick('');}"
               "function pickRow(it){var d=document.createElement('div');"
               "d.style.cssText='display:flex;gap:10px;align-items:center;padding:8px 14px;cursor:pointer;font-size:13.5px;';"
               "d.onmouseenter=function(){d.style.background='var(--hover)';};d.onmouseleave=function(){d.style.background='transparent';};"
               "var ic=document.createElement('span');ic.textContent=it.d?'\U0001f4c1':'\U0001f4c4';"
               "var nm=document.createElement('span');nm.style.cssText='flex:1;color:var(--text-strong);word-break:break-all;';nm.textContent=it.p;"
               "var sz=document.createElement('span');sz.style.cssText='color:var(--muted);font-size:12px;';sz.textContent=it.d?'':fmtSize(it.s);"
               "d.appendChild(ic);d.appendChild(nm);d.appendChild(sz);"
               "d.onclick=function(){if(it.d){pickDir=it.p;document.getElementById('pickQ').value='';renderPick('');}"
               "else{document.getElementById('driveFile').value=it.p;closePick();}};return d;}"
               "function renderPick(q){q=(q||'').toLowerCase();var list=document.getElementById('drivePickList');"
               "var pre=pickDir?pickDir+'/':'';var arr=[];"
               "for(var i=0;i<DRV_TREE.length;i++){var x=DRV_TREE[i];"
               "if(q){if(x.p.toLowerCase().indexOf(q)<0)continue;}"
               "else{if(x.p.indexOf(pre)!==0)continue;if(x.p.slice(pre.length).indexOf('/')>=0)continue;}"
               "arr.push(x);}"
               "arr.sort(function(a,b){if(a.d!==b.d)return a.d?-1:1;return a.n<b.n?-1:(a.n>b.n?1:0);});"
               "var cr=document.getElementById('pickCrumb');cr.innerHTML='';"
               "if(pickDir){var up=document.createElement('button');up.type='button';up.className='btn btn-sm btn-outline';"
               "up.textContent='\u2191 上一级';up.onclick=pickUp;cr.appendChild(up);}"
               "var lbl=document.createElement('span');lbl.textContent='/'+(pickDir||'')+(q?'  ·  搜索 '+q:'');cr.appendChild(lbl);"
               "list.innerHTML='';"
               "if(!arr.length){var em=document.createElement('div');em.style.cssText='padding:22px 16px;color:var(--muted);font-size:13px;';"
               "em.textContent='这里没有文件';list.appendChild(em);return;}"
               "for(var j=0;j<arr.length;j++)list.appendChild(pickRow(arr[j]));}</script>";

    // AI 工作台逻辑
    content += "<script>var GEN_PID=" + common::jsonStringify(pid) +
               ";var GEN_STD_OK=" + std::string((_p && !_p->standardSolution.empty()) ? "1" : "0") + ";"
               "var genCtrl=null;"
               "function aiGen(){genOpen();}"
               "function genOpen(){document.getElementById('genWb').style.display='block';}"
               "function genStop(){if(genCtrl){try{genCtrl.abort();}catch(e){}}genCtrl=null;}"
               "function genClose(){genStop();document.getElementById('genWb').style.display='none';}"
               "function genCopy(){var c=document.getElementById('genWbCode');c.select();"
               "try{document.execCommand('copy');document.getElementById('genWbStatus').textContent='已复制';}catch(e){}}"
               "function genUse(){var v=document.getElementById('genWbCode').value;if(!v)return false;"
               "var box=document.getElementById('genBox');box.value=v;"
               "document.getElementById('genMsg').textContent='已填入生成器框，记得保存';return true;}"
               "function genSave(){if(!genUse())return;var f=document.getElementById('genForm');if(f)f.submit();}"
               "function genRun(){var run=document.getElementById('genWbRun');var stop=document.getElementById('genWbStop');"
               "var st=document.getElementById('genWbStatus');var model=document.getElementById('genWbModel').value;"
               "if(!model){st.textContent='没有可用模型，请先去 AI 管理启用';return;}"
               "if(!GEN_STD_OK){st.textContent='请先填写并保存标程，AI 需要参考标程';return;}"
               "genStop();var t0=Date.now();st.textContent='生成中…';run.disabled=true;stop.style.display='';"
               "genCtrl=(typeof AbortController!=='undefined')?new AbortController():null;"
               "var payload='model='+encodeURIComponent(model)"
               "+'&effort='+encodeURIComponent(document.getElementById('genWbEffort').value)"
               "+'&maxTokens='+encodeURIComponent(document.getElementById('genWbTok').value)"
               "+'&extra='+encodeURIComponent(document.getElementById('genWbExtra').value);"
               "function fin(){run.disabled=false;stop.style.display='none';genCtrl=null;}"
               "var opt={method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:payload};"
               "if(genCtrl)opt.signal=genCtrl.signal;"
               "fetch('/admin/problem/genai/'+GEN_PID,opt).then(function(r){return r.json();}).then(function(j){"
               "if(j.ok){document.getElementById('genWbCode').value=j.code;"
               "st.textContent='✅ '+(j.model||'')+' · '+((j.ms||0)/1000).toFixed(1)+'s · '+(((Date.now()-t0)/1000).toFixed(1))+'s 总耗时';}"
               "else{st.textContent='❌ '+(j.error||'未知错误');}fin();"
               "}).catch(function(e){st.textContent=((e&&e.name==='AbortError')?'已停止':'请求失败：'+e);fin();});}</script>";

    content += "<div style='margin-top:14px;'><a href='" + backUrl + "' class='btn'>返回</a></div>";
    return renderTemplate("测试数据", content, user);
}


// ============================================================
// 用站内模型自动写数据生成器（根据标程 / 数据范围 / 输入格式）
// ============================================================
std::string Handlers::handleAdminProblemGenAi(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId) {
    (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false,\"error\":\"请先登录\"}";
    std::string pid = common::urlDecode(path.substr(path.find_last_of('/') + 1));
    Problem* p = DataStore::instance().findProblemById(pid);
    if (!p) return "{\"ok\":false,\"error\":\"题目不存在\"}";
    if (!canManageProblems(user) && !isProblemOwner(p, user))
        return "{\"ok\":false,\"error\":\"没有权限\"}";
    if (p->standardSolution.empty())
        return "{\"ok\":false,\"error\":\"请先填写并保存标程：AI 需要参考标程才能保证生成的数据合法\"}";

    // 工作台参数：模型 / 思考强度 / 输出上限 / 附加要求
    std::string wantModel, extra;
    int effort = 0, maxTok = 4000;
    if (method == "POST") {
        wantModel = common::trim(parseFormData(body, "model"));
        effort = common::stringToInt(parseFormData(body, "effort"));
        int mt = common::stringToInt(parseFormData(body, "maxTokens"));
        if (mt >= 512 && mt <= 8192) maxTok = mt;
        extra = common::trim(parseFormData(body, "extra"));
        if (extra.size() > 2000) extra = extra.substr(0, 2000);
    }

    std::string stdCode = p->standardSolution;
    if (stdCode.size() > 6000) stdCode = stdCode.substr(0, 6000) + "\n// ...（后文已截断）";
    std::string subSpec = subtaskSpecOf(p->subtasks);
    std::string prompt =
        "你是竞赛出题人，请为下面的题目写一个 C++11 数据生成器（generator）。\n"
        "硬性要求：\n"
        "1. 程序从标准输入读取两个整数：caseNo（该组编号，从 1 开始）与 seed；只向标准输出写出这一组测试数据。\n"
        "2. 必须用 seed 初始化 std::mt19937_64，保证同一 caseNo+seed 输出完全一致。\n"
        "3. 输出必须严格符合下面的输入格式，不能输出 caseNo、seed 或任何提示文字。\n"
        "4. 随着 caseNo 增大，数据规模与强度要有梯度，但最大值不得超过数据范围。\n"
        "5. 只输出完整可编译的 C++ 代码，不要 Markdown 代码块标记，不要解释。\n\n"
        "【输入格式】\n" + (p->inputDesc.empty() ? std::string("（题面未填写，请按题目描述推断）") : p->inputDesc) + "\n\n"
        "【数据范围】\n" + (p->dataRange.empty() ? std::string("（未填写，请取常见保守范围）") : p->dataRange) + "\n\n" +
        (subSpec.empty() ? std::string() : ("【子任务划分（每行：名称|分值|测试点）】\n" + subSpec + "\n\n")) +
        "【标程（用于理解输入结构，不要复述它）】\n" + stdCode + "\n" +
        (extra.empty() ? std::string() : ("\n【出题人附加要求（必须满足）】\n" + extra + "\n"));

    long long t0 = (long long)GetTickCount64();
    std::string err, usedModel;
    std::string code = aiAssistText(prompt, maxTok, &err, wantModel, effort, &usedModel);
    long long cost = (long long)GetTickCount64() - t0;
    if (code.empty()) {
        std::string msg = err.empty() ? std::string("模型没有返回内容") : err;
        return "{\"ok\":false,\"error\":" + common::jsonStringify(msg) + ",\"model\":" +
               common::jsonStringify(usedModel) + ",\"ms\":" + std::to_string(cost) + "}";
    }
    // 去掉 Markdown 代码围栏
    if (code.find("```") != std::string::npos) {
        size_t a = code.find("```");
        size_t nl = code.find('\n', a);
        if (nl != std::string::npos) code = code.substr(nl + 1);
        size_t b = code.rfind("```");
        if (b != std::string::npos) code = code.substr(0, b);
    }
    code = common::trim(code);
    if (code.empty())
        return "{\"ok\":false,\"error\":\"模型返回为空\",\"model\":" + common::jsonStringify(usedModel) +
               ",\"ms\":" + std::to_string(cost) + "}";
    return "{\"ok\":true,\"code\":" + common::jsonStringify(code) + ",\"model\":" +
           common::jsonStringify(usedModel) + ",\"ms\":" + std::to_string(cost) + "}";
}

std::string Handlers::handleAdminDrive(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    if (method == "POST") {
        std::string v = parseFormData(body, "maxFileMB");
        std::string uid = parseFormData(body, "uid");
        if (!uid.empty()) {
            User* tu = DataStore::instance().findUserById(uid);
            if (tu) {
                int dm = common::stringToInt(parseFormData(body, "driveMB"));
                if (dm < 0) dm = 0;
                tu->driveMB = dm;
                DataStore::instance().saveUsers();
            }
            std::string content2 = "<div class='alert alert-success'>\u5df2\u66f4\u65b0\u8be5\u7528\u6237\u4e2a\u4eba\u4e91\u76d8\u914d\u989d</div><a href='/admin/drive' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u4e91\u76d8\u8bbe\u7f6e", content2, user);
        }
        std::string vt = parseFormData(body, "maxTotalMB");
        int mb = common::stringToInt(v);
        int mt = common::stringToInt(vt);
        setDriveConfig(mb, mt);
        std::string content = "<div class='alert alert-success'>\u4e91\u76d8\u8bbe\u7f6e\u5df2\u4fdd\u5b58\uff1a\u5355\u6587\u4ef6\u4e0a\u9650 " + std::to_string(mb) + " MB\uff0c\u603b\u7a7a\u95f4\u914d\u989d " + std::to_string(mt) + " MB</div>"
                              "<a href='/admin/drive' class='btn'>\u7ee7\u7eed\u8bbe\u7f6e</a> <a href='/admin' class='btn'>\u8fd4\u56de\u7ba1\u7406\u9762\u677f</a>";
        return renderTemplate("\u4e91\u76d8\u8bbe\u7f6e", content, user);
    }
    int cur = getDriveMaxMB();
    int curT = getDriveTotalMB();
    std::string content = "<div class='alert alert-info'>\u8bbe\u7f6e\u4e91\u76d8\u9650\u5236\uff1a\u5f53\u524d\u5355\u6587\u4ef6\u4e0a\u9650 <b>" + std::to_string(cur) + " MB</b>\uff0c\u603b\u7a7a\u95f4\u914d\u989d <b>" + std::to_string(curT) + " MB</b></div>"
        "<form method='POST'>"
        "<div class='form-group'><label>\u5355\u6587\u4ef6\u4e0a\u9650 (MB)</label><input type='number' name='maxFileMB' value='" + std::to_string(cur) + "' min='1' max='512' required/></div>"
        "<div class='form-group'><label>\u603b\u7a7a\u95f4\u914d\u989d (MB)</label><input type='number' name='maxTotalMB' value='" + std::to_string(curT) + "' min='1' max='4096' required/></div>"
        "<input type='submit' value='\u4fdd\u5b58' class='btn'/> <a href='/admin' class='btn'>\u53d6\u6d88</a></form>";
    content += "<h3 style='margin-top:24px;'>\u7528\u6237\u4e2a\u4eba\u914d\u989d\uff08\u586b 0 = \u4f7f\u7528\u5168\u5c40\u9ed8\u8ba4\uff09</h3>";
    content += "<table><thead><tr><th>\u7528\u6237</th><th>\u603b\u914d\u989d (MB)</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
    for (auto& u : DataStore::instance().users) {
        content += "<tr><td>" + common::htmlEscape(u.username) + "</td>"
                   "<td><form method='POST' style='display:flex;gap:6px;align-items:center;'>"
                   "<input type='hidden' name='uid' value='" + u.id + "'/>"
                   "<input type='number' name='driveMB' value='" + std::to_string(u.driveMB) + "' min='0' style='width:90px;'/>"
                   "<input type='submit' value='\u4fdd\u5b58' class='btn btn-sm'/></form></td></tr>";
    }
    content += "</tbody></table>";
    return renderTemplate("\u4e91\u76d8\u8bbe\u7f6e", content, user);
}

std::string Handlers::handleAbout(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string content;
    content += "<div style='max-width:780px;margin:0 auto;'>";
    // \u5934\u90e8\u5c01\u9762
    content += "<div style='text-align:center;padding:38px 24px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);border-radius:16px;color:#fff;box-shadow:0 8px 24px rgba(102,126,234,0.35);'>";
    content += "<div style='font-size:58px;line-height:1;'>\U0001f3af</div>";
    content += "<h1 style='color:#fff;border:none;margin:12px 0 8px;font-size:30px;letter-spacing:2px;'>\u5173\u4e8e OI \u5c0f\u7ad9</h1>";
    content += "<div style='display:inline-block;background:rgba(255,255,255,0.22);border:1px solid rgba(255,255,255,0.45);padding:6px 20px;border-radius:20px;font-size:14px;font-weight:600;'>v" OISTATION_VERSION "</div>";
    content += "<p style='margin-top:16px;opacity:0.95;line-height:1.8;'>\u57fa\u4e8e C++11 \u7684\u5728\u7ebf\u5224\u9898\u7cfb\u7edf\uff0c\u5f00\u6e90\u3001\u53ef\u6269\u5c55\u3001\u8f7b\u91cf\u7ea7</p>";
    content += "</div>";

    // \u7b80\u4ecb
    content += "<div class='card' style='padding:22px 26px;border-left:4px solid #667eea;'>";
    content += "<p style='line-height:1.9;color:#555;margin:0;'>OI \u5c0f\u7ad9\u662f\u4e00\u4e2a\u57fa\u4e8e C++11 \u7684\u5728\u7ebf\u5224\u9898\u7cfb\u7edf\uff0c\u652f\u6301\u7f16\u7a0b\u9898\u76ee\u4e0e\u8bc4\u6d4b\u3001\u7ade\u8d5b\u3001\u9898\u5355\u3001\u4efb\u52a1\u60ac\u8d4f\u3001\u5546\u57ce\u3001\u4e91\u76d8\u4ee5\u53ca AI \u52a9\u624b\u7b49\u529f\u80fd\u3002</p>";
    content += "</div>";

    // \u529f\u80fd\u7279\u6027
    content += "<h3 style='margin:24px 0 12px;'>\u6838\u5fc3\u529f\u80fd</h3>";
    content += "<div style='display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:14px;'>";
    content += "<div style='background:#f8f9fa;padding:18px;border-radius:10px;border:1px solid #eee;text-align:center;line-height:1.7;'>\U0001f4da<br/><b>\u9898\u5e93\u4e0e\u8bc4\u6d4b</b></div>";
    content += "<div style='background:#f8f9fa;padding:18px;border-radius:10px;border:1px solid #eee;text-align:center;line-height:1.7;'>\U0001f3c6<br/><b>\u7ade\u8d5b</b></div>";
    content += "<div style='background:#f8f9fa;padding:18px;border-radius:10px;border:1px solid #eee;text-align:center;line-height:1.7;'>\U0001f4cb<br/><b>\u9898\u5355</b></div>";
    content += "<div style='background:#f8f9fa;padding:18px;border-radius:10px;border:1px solid #eee;text-align:center;line-height:1.7;'>\U0001f4b0<br/><b>\u4efb\u52a1\u60ac\u8d4f</b></div>";
    content += "<div style='background:#f8f9fa;padding:18px;border-radius:10px;border:1px solid #eee;text-align:center;line-height:1.7;'>\U0001f6d2<br/><b>\u5546\u57ce</b></div>";
    content += "<div style='background:#f8f9fa;padding:18px;border-radius:10px;border:1px solid #eee;text-align:center;line-height:1.7;'>\u2601\ufe0f<br/><b>\u4e91\u76d8</b></div>";
    content += "<div style='background:#f8f9fa;padding:18px;border-radius:10px;border:1px solid #eee;text-align:center;line-height:1.7;'>\U0001f916<br/><b>AI \u52a9\u624b</b></div>";
    content += "<div style='background:#f8f9fa;padding:18px;border-radius:10px;border:1px solid #eee;text-align:center;line-height:1.7;'>\U0001f4ac<br/><b>\u8ba8\u8bba\u4e0e\u535a\u5ba2</b></div>";
    content += "</div>";

    // \u5f00\u53d1\u8005
    content += "<h3 style='margin:24px 0 12px;'>\u5f00\u53d1\u8005</h3>";
    content += "<div class='card' style='padding:6px 22px;'>";
    content += "<table style='margin:0;'><tbody>";
    content += "<tr><td style='width:150px;'><b>\u5f00\u53d1\u8005</b></td><td>zyf</td></tr>";
    content += "<tr><td><b>Luogu UID</b></td><td>1813501</td></tr>";
    content += "<tr><td><b>\u6280\u672f\u652f\u6491</b></td><td>deepseekv4</td></tr>";
    content += "</tbody></table>";
    content += "</div>";

    // \u514d\u8d23\u58f0\u660e
    content += "<h3 style='margin:24px 0 12px;'>\u514d\u8d23\u58f0\u660e</h3>";
    content += "<div class='card' style='padding:8px 24px;'>";
    content += "<ul style='line-height:1.9;color:#555;padding-left:20px;margin:10px 0;'>";
    content += "<li>\u672c\u7cfb\u7edf\u4ec5\u4f9b\u5b66\u4e60\u4ea4\u6d41\u4e0e\u4e2a\u4eba\u4f7f\u7528\uff0c\u4e0d\u7528\u4e8e\u4efb\u4f55\u5546\u4e1a\u7528\u9014\u3002</li>";
    content += "<li>\u7528\u6237\u5728\u672c\u7ad9\u63d0\u4ea4\u3001\u4e0a\u4f20\u6216\u53d1\u5e03\u7684\u5185\u5bb9\uff08\u5305\u62ec\u4f46\u4e0d\u9650\u4e8e\u4ee3\u7801\u3001\u6587\u4ef6\u3001\u5e16\u5b50\u3001\u8bc4\u8bba\uff09\u7531\u7528\u6237\u81ea\u884c\u8d1f\u8d23\uff0c\u7cfb\u7edf\u4e0d\u627f\u62c5\u76f8\u5173\u8d23\u4efb\u3002</li>";
    content += "<li>\u5f00\u53d1\u8005\u4e0d\u5bf9\u56e0\u4f7f\u7528\u672c\u7cfb\u7edf\u800c\u4ea7\u751f\u7684\u4efb\u4f55\u76f4\u63a5\u6216\u95f4\u63a5\u635f\u5931\u627f\u62c5\u8d23\u4efb\u3002</li>";
    content += "<li>\u8bf7\u9075\u5b88\u56fd\u5bb6\u6cd5\u5f8b\u6cd5\u89c4\u4e0e\u7248\u6743\u89c4\u5b9a\uff0c\u5c0a\u91cd\u4ed6\u4eba\u77e5\u8bc6\u4ea7\u6743\u3002</li>";
    content += "<li>\u5982\u672c\u7ad9\u5185\u5bb9\u6d89\u5acc\u4fb5\u6743\uff0c\u8bf7\u8054\u7cfb\u7ba1\u7406\u5458\u5904\u7406\u3002</li>";
    content += "</ul>";
    content += "</div>";
    content += "</div>";
    return renderTemplate("\u5173\u4e8e", content, user);
}

std::string Handlers::handleMessagesSearch(const std::string& method, const std::string& path,
 const std::string& body,
 const std::map<std::string, std::string>& headers,
 std::map<std::string, std::string>& respHeaders,
 std::string& sessionId) {
 User* user = getCurrentUser(sessionId);
 if (!user) return "<script>window.location.href='/login'</script>";
 std::string q;
 if (method == "POST") {
  q = parseFormData(body, "q");
 } else {
  size_t qp = path.find("q=");
  if (qp != std::string::npos) {
   size_t end = path.find('&', qp);
   std::string v = path.substr(qp + 2, end == std::string::npos ? std::string::npos : end - qp - 2);
   q = common::urlDecode(v);
  }
 }
 std::string lq = common::trim(q);
 DataStore& ds = DataStore::instance();
 std::string content;
 content += "<div class='card' style='padding:22px;max-width:680px;margin:0 auto;'>";
 content += "<h3>\U0001f50d \u641c\u7d22\u7528\u6237</h3>";
 content += "<form method='GET' action='/messages/search' style='display:flex;gap:8px;margin:14px 0;'>"
  "<input type='text' name='q' value='" + common::htmlEscape(lq) + "' placeholder='\u8f93\u5165\u7528\u6237\u540d\u6216\u5176\u4e00\u90e8\u5206' autocomplete='off' style='flex:1;padding:8px;border:1px solid #ddd;border-radius:6px;'/>"
  "<button class='btn'>\u641c\u7d22</button></form>";
 if (lq.empty()) {
  content += "<div class='alert alert-info'>\u8bf7\u8f93\u5165\u5173\u952e\u5b57\u641c\u7d22\u7528\u6237\uff08\u652f\u6301\u7528\u6237\u540d\u90e8\u5206\u5339\u914d\uff09\uff0c\u70b9\u51fb\u7ed3\u679c\u5373\u53ef\u53d1\u8d77\u79c1\u804a\u3002</div>";
 } else {
  std::string lql = lq;
  std::transform(lql.begin(), lql.end(), lql.begin(), ::tolower);
  std::vector<User*> hits;
  for (auto& u : ds.users) {
   if (u.id == user->id) continue;
   std::string un = u.username;
   std::transform(un.begin(), un.end(), un.begin(), ::tolower);
   if (un.find(lql) != std::string::npos) hits.push_back(&u);
  }
  if (hits.empty()) {
   content += "<div class='alert alert-warning'>\u672a\u627e\u5230\u6ee1\u8db3\u6761\u4ef6\u7684\u7528\u6237\u3002</div>";
  } else {
   content += "<table><thead><tr><th>\u7528\u6237</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
   for (auto* u : hits) {
    content += "<tr><td>" + common::htmlEscape(u->username) + "</td>"
     "<td><a href='/messages/chat/" + u->id + "' class='btn btn-sm'>\u53d1\u79c1\u4fe1</a></td></tr>";
   }
   content += "</tbody></table>";
  }
 }
 content += "<a href='/messages' class='btn' style='margin-top:14px;'>\u8fd4\u56de\u6d88\u606f</a>";
 content += "</div>";
 return renderTemplate("\u641c\u7d22\u7528\u6237", content, user);
}

// ============================================================
// 私信附件 + @ 提及补全
// ============================================================
static std::string mimeOfFile(const std::string& name) {
    std::string n = name;
    for (size_t i = 0; i < n.size(); i++) n[i] = (char)tolower((unsigned char)n[i]);
    if (n.size() > 4 && n.rfind(".png") == n.size() - 4) return "image/png";
    if (n.size() > 4 && (n.rfind(".jpg") == n.size() - 4 || n.rfind(".jpeg") == n.size() - 5)) return "image/jpeg";
    if (n.size() > 4 && n.rfind(".gif") == n.size() - 4) return "image/gif";
    if (n.size() > 5 && n.rfind(".webp") == n.size() - 5) return "image/webp";
    if (n.size() > 4 && n.rfind(".pdf") == n.size() - 4) return "application/pdf";
    if (n.size() > 4 && (n.rfind(".txt") == n.size() - 4 || n.rfind(".cpp") == n.size() - 4 ||
                         n.rfind(".hpp") == n.size() - 4 || n.rfind(".log") == n.size() - 4 ||
                         n.rfind(".csv") == n.size() - 4 || n.rfind(".md") == n.size() - 4)) return "text/plain; charset=utf-8";
    if (n.size() > 4 && (n.rfind(".zip") == n.size() - 4)) return "application/zip";
    return "application/octet-stream";
}

// 文件 key 只允许安全字符，避免路径穿越 / URL 麻烦
static std::string safeFileKeyPart(std::string name) {
    size_t sl = name.find_last_of("/\\");
    if (sl != std::string::npos) name = name.substr(sl + 1);
    std::string out;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c) || c == '_' || c == '-' || c == '.') out += (char)c;
        else if (c >= 0x80) out += (char)c;   // 保留中文，URL 会编码
        else out += '_';
    }
    if (out.empty()) out = "file.bin";
    if (out.size() > 80) out = out.substr(out.size() - 80);
    return out;
}

std::string Handlers::handleMessageUpload(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    (void)method; (void)path; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!user) return "{\"ok\":false,\"error\":\"请先登录\"}";
    std::map<std::string, std::string> fields;
    std::vector<UploadedFile> files;
    parseMultipartMulti(body, extractBoundary(headers), fields, files);
    if (files.empty()) return "{\"ok\":false,\"error\":\"没有收到文件（请控制在 20MB 内）\"}";
    const UploadedFile& f = files[0];
    if (f.data.size() > (size_t)(20 * 1024 * 1024)) return "{\"ok\":false,\"error\":\"单个文件不能超过 20MB\"}";
    common::createDirectory("./files/msg");
    std::string rawName = f.filename.empty() ? "file.bin" : f.filename;
    std::string part = safeFileKeyPart(rawName);
    std::string key = common::genId() + "_" + part;
    std::string full = "./files/msg/" + key;
    if (!common::writeFile(full, f.data)) return "{\"ok\":false,\"error\":\"保存失败\"}";
    std::map<std::string, std::string> o;
    o["ok"] = "true";
    o["key"] = key;
    o["name"] = part;
    o["size"] = std::to_string(f.data.size());
    return common::jsonStringify(o);
}

std::string Handlers::handleMessageFile(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    (void)method; (void)body; (void)headers;
    User* user = getCurrentUser(sessionId);
    if (!user) {
        respHeaders["Content-Type"] = "text/html; charset=utf-8";
        return "<div style='font-family:sans-serif;padding:30px'>请先 <a href='/login'>登录</a> 再查看附件</div>";
    }
    std::string key = path.substr(std::string("/messages/file/").size());
    size_t q = key.find('?');
    if (q != std::string::npos) key = key.substr(0, q);
    key = common::urlDecode(key);
    bool okKey = !key.empty() && key.size() < 200;
    for (size_t i = 0; okKey && i < key.size(); i++) {
        unsigned char c = (unsigned char)key[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c >= 0x80)) okKey = false;
        if (c < 0x20) okKey = false;
    }
    if (!okKey) return "<div style='font-family:sans-serif;padding:30px'>无效的文件地址</div>";

    DataStore& ds = DataStore::instance();
    const Message* found = NULL;
    for (size_t i = 0; i < ds.messages.size(); i++) {
        const Message& m = ds.messages[i];
        if (m.file != "msg/" + key) continue;
        if (m.fromId == user->id || m.toId == user->id) { found = &m; break; }
        if (m.toId.size() >= 2 && m.toId[0] == 'g') {
            GroupChat* g = ds.findGroupById(m.toId.substr(1));
            if (g) {
                for (size_t k = 0; k < g->memberIds.size(); k++)
                    if (g->memberIds[k] == user->id) { found = &m; break; }
            }
        }
        if (found) break;
        if (user->role == ROLE_ADMIN) { found = &m; break; }
    }
    if (!found) return "<div style='font-family:sans-serif;padding:30px'>附件不存在或你没有权限查看</div>";
    std::string full = "./files/msg/" + key;
    if (!common::fileExists(full)) return "<div style='font-family:sans-serif;padding:30px'>附件已被清理</div>";
    std::string data = common::readFile(full);
    respHeaders["Content-Type"] = mimeOfFile(found->fileName.empty() ? key : found->fileName);
    respHeaders["X-Content-Type-Options"] = "nosniff";
    respHeaders["Cache-Control"] = "private, max-age=600";
    return data;
}

// 讨论区 @ 补全：/api/users/suggest?q=xx
std::string Handlers::handleUserSuggest(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    (void)method; (void)body; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* me = getCurrentUser(sessionId);
    std::string q;
    size_t qp = path.find("q=");
    if (qp != std::string::npos) {
        size_t e = path.find('&', qp);
        q = common::urlDecode(path.substr(qp + 2, e == std::string::npos ? std::string::npos : e - qp - 2));
    }
    std::string lq = q;
    std::transform(lq.begin(), lq.end(), lq.begin(), ::tolower);
    std::vector<std::string> hits;
    DataStore& ds = DataStore::instance();
    for (size_t i = 0; i < ds.users.size() && hits.size() < 8; i++) {
        User& u = ds.users[i];
        if (me && u.id == me->id) continue;
        std::string un = u.username;
        std::transform(un.begin(), un.end(), un.begin(), ::tolower);
        if (!lq.empty() && un.find(lq) == std::string::npos) continue;
        hits.push_back(u.username);
    }
    return common::jsonStringify(hits);
}

void Handlers::registerRoutes(Router& router) {
    // 试卷模块（选择题 + 填空题）
    registerPaperRoutes(router);
    // 论坛（洛谷式讨论区）—— 需先注册，覆盖旧的 /discuss 与 /post 详情
    registerForumRoutes(router);
    // AI 页面组
    registerAiRoutes(router);
    // 页面路由
    router.get("/", handleRoot);
    router.post("/", handleRoot);
    router.get("/login", handleLogin);
    router.post("/login", handleLogin);
    router.get("/register", handleRegister);
    router.post("/register", handleRegister);
    router.get("/logout", handleLogout);
    
    // 题库（含分页）
    router.get("/run", handleIdeTest);
    router.post("/run", handleIdeTest);
    // IDE ↔ 云盘 / 提交
    router.get("/ide/drive.json", handleIdeDrive);
    router.get("/ide/read.json", handleIdeRead);
    router.post("/ide/save", handleIdeSave);
    router.get("/api/captcha", handleCaptchaApi);
    router.get("/run-classic", handleCustomTest);
    router.post("/run-classic", handleCustomTest);
    router.post("/api/custom-test", handleCustomTestApi);
    router.get("/api/problem-samples", handleProblemSamples);
    router.get("/myproblem/data/*", handleAdminProblemData);
    router.post("/myproblem/data/*", handleAdminProblemData);
    router.get("/myproblem", handleMyProblems);
    router.get("/myproblem/edit/*", handleAdminEditProblem);
    router.post("/myproblem/edit/*", handleAdminEditProblem);
    router.post("/myproblem/toggle", handleAdminToggleProblem);
    router.post("/myproblem/delete", handleAdminDeleteProblem);
    router.get("/duel", handleDuelLobby);
    router.post("/duel/create", handleDuelAction);
    router.post("/duel/action", handleDuelAction);
    router.get("/duel/*", handleDuelDetail);
    router.get("/problems/search*", handleProblems);
    router.post("/problems/search", handleProblems);
    router.get("/problems", handleProblems);
    router.get("/problems/page/*", handleProblems);
    // 用户出题
    router.get("/problems/add", handleUserAddProblem);
    router.post("/problems/add", handleUserAddProblem);
    router.get("/problem/*", handleProblemDetail);
    router.post("/problem/discuss", handleProblemDiscussPost);
    router.post("/submit", handleSubmit);
    router.get("/status", handleStatus);
    router.get("/status/page/*", handleStatus);
    router.get("/status/search*", handleStatus);
    router.get("/submission/hack/*", handleSubmissionHackPage);
    router.get("/submission/*", handleSubmissionDetail);
    router.post("/submission/hack", handleSubmissionHack);
    
    // 博客（含分页）
    router.get("/blog", handleBlog);
    router.get("/blog/page/*", handleBlog);
    router.get("/blog/new", handleBlogNew);
    router.post("/blog/new", handleBlogNew);
    router.get("/post/*", handlePostDetail);
    router.post("/post/*/comment", handlePostComment);
    router.post("/post/*/like", handlePostLike);
    router.post("/post/*/vote", handlePostVote);
    
    // 题解
    router.get("/solutions", handleProblemSolutions);
    router.get("/solutions/*", handleProblemSolutions);
    router.get("/solution/new/*", handleSolutionNew);
    router.post("/solution/new/*", handleSolutionNew);
    router.post("/post/*/resubmit", handleSolutionResubmit);
    router.get("/admin/solutions", handleAdminSolutions);
    router.post("/admin/solution/audit", handleAdminSolutionAudit);
    router.post("/admin/problem/solution-toggle", handleProblemToggleSolution);
    
    // 讨论（含分页）
    router.get("/discuss", handleDiscuss);
    router.get("/discuss/page/*", handleDiscuss);
    router.get("/discuss/new", handleDiscussNew);
    router.post("/discuss/new", handleDiscussNew);
    
    // 犇犇 & 打卡
    router.post("/benben", handleBenben);
    router.post("/checkin", handleCheckin);
    
    // 比赛（含分类和分页）
    router.get("/contests", handleContests);
    router.get("/contests/all", handleContests);
    router.get("/contests/page/*", handleContests);
    router.get("/contests/upcoming", handleContests);
    router.get("/contests/ongoing", handleContests);
    router.get("/contests/past", handleContests);
    router.get("/contest/*", handleContestDetail);
    router.post("/contest/*/join", handleContestJoin);
    router.post("/contest/lock", handleContestLock);
    router.post("/contest/*/team/*", handleContestTeam);
    router.post("/contest/*/team", handleContestTeam);
    router.post("/contest/*/comment", handleContestComment);
    router.post("/contest/*", handleContestTeam); // team 操作兜底
    
    // 题单
    router.get("/sets", handleProblemSets);
    router.get("/set/*", handleProblemSetDetail);
    
    // 私信
    router.get("/messages", handleMessages);
    router.get("/messages/search*", handleMessagesSearch);
    router.get("/messages/file/*", handleMessageFile);
    router.get("/messages/*", handleMessages);
    router.post("/messages/contact/add", handleContactAdd);
    router.get("/messages/poll", handleMessagesPoll);
    router.post("/messages/typing", handleMessagesTyping);
    router.post("/messages/upload", handleMessageUpload);
    router.get("/messages/group/new", handleGroupNew);
    router.post("/messages/group/new", handleGroupNew);
    router.post("/messages/group/*", handleGroupAction);
    router.post("/message/send", handleSendMessage);
    router.post("/message/delete", handleDeleteMessage);
    router.get("/api/users/suggest*", handleUserSuggest);
    
    // 用户相关
    router.get("/user/*", handleUserProfile);
    router.get("/user/rename", handleRename);
    router.post("/user/rename", handleRename);
    router.get("/user/edit", handleUserEdit);
    router.post("/user/edit", handleUserEdit);
    router.get("/user/settings", handleUserSettings);
    router.post("/user/settings", handleUserSettings);
    
    // 管理员
    router.get("/admin", handleAdmin);
    router.get("/admin/users", handleAdminUsers);
    router.post("/admin/user/delete", handleAdminDeleteUser);
    router.post("/admin/user/password", handleAdminUserPassword);
    router.get("/admin/problems", handleAdminProblems);
    router.get("/admin/problems/page/*", handleAdminProblems);
    router.get("/admin/problems/search*", handleAdminProblems);
    router.get("/admin/problems/add", handleAdminAddProblem);
    router.get("/admin/problems/import", handleAdminProblemImport);
    router.post("/admin/problems/import", handleAdminProblemImport);
    router.get("/admin/problem/data/*", handleAdminProblemData);
    router.post("/admin/problem/data/*", handleAdminProblemData);
    router.get("/admin/problem/genai/*", handleAdminProblemGenAi);
    router.post("/admin/problem/genai/*", handleAdminProblemGenAi);
    // \u4e91\u76d8
    router.get("/drive/folder/*", handleDrive);
    router.get("/drive", handleDrive);
    router.post("/drive/mkdir", handleDriveMkdir);
    router.post("/drive/rename", handleDriveRename);
    router.post("/drive/upload", handleDriveUpload);
    router.get("/drive/download/*", handleDriveDownload);
    router.get("/drive/raw/*", handleDriveRaw);
    router.get("/drive/zip", handleDriveZip);
    router.post("/drive/zip", handleDriveZip);
    router.post("/drive/move", handleDriveMove);
    router.get("/drive/tree.json", handleDriveTree);
    router.get("/drive/search.json", handleDriveSearch);
    router.post("/drive/copy", handleDriveCopy);
    router.post("/drive/newfile", handleDriveNewFile);
    router.get("/drive/props", handleDriveProps);
    router.post("/drive/shortcut", handleDriveShortcut);
    router.post("/share/*", handleShare);
    router.post("/drive/delete", handleDriveDelete);
    router.post("/drive/share", handleDriveShare);
    router.get("/share/*", handleShare);
    router.get("/about", handleAbout);
    router.get("/admin/system", handleAdminSystem);
    router.post("/admin/system", handleAdminSystem);
    router.get("/admin/drive", handleAdminDrive);
    router.post("/admin/drive", handleAdminDrive);


    router.post("/admin/problems/add", handleAdminAddProblem);
    router.get("/admin/problem/edit/*", handleAdminEditProblem);
    router.post("/admin/problem/edit/*", handleAdminEditProblem);
    router.post("/admin/problem/delete", handleAdminDeleteProblem);
    router.get("/admin/problem/delete/*", handleAdminDeleteProblem);
    router.post("/admin/problem/toggle", handleAdminToggleProblem);
    router.get("/admin/posts", handleAdminPosts);
    router.post("/admin/post/delete", handleAdminDeletePost);
    router.get("/admin/post/delete/*", handleAdminDeletePost);
    router.get("/admin/post/edit/*", handleAdminEditPost);
    router.post("/admin/post/edit/*", handleAdminEditPost);
    router.get("/admin/contests", handleAdminContests);
    router.get("/admin/contest/add", handleAdminAddContest);
    router.post("/admin/contest/add", handleAdminAddContest);
    router.get("/admin/contest/edit/*", handleAdminEditContest);
    router.post("/admin/contest/edit/*", handleAdminEditContest);
    router.get("/admin/set/add", handleAdminAddProblemSet);
    router.post("/admin/set/add", handleAdminAddProblemSet);
    router.get("/admin/announcement", handleAdminAnnouncement);
    router.post("/admin/announcement", handleAdminAnnouncement);
    router.get("/admin/permissions", handleAdminPerms);
    router.post("/admin/permissions", handleAdminPerms);
    router.get("/admin/ban", handleAdminBan);
    router.post("/admin/ban", handleAdminBan);
    // coin shop
    router.get("/shop", handleShop);
    router.get("/shop/buy/*", handleShopBuy);
    router.post("/shop/buy/*", handleShopBuy);
    router.get("/admin/shop", handleAdminShop);
    router.get("/admin/shop/add", handleAdminShopAdd);
    router.post("/admin/shop/add", handleAdminShopAdd);
    router.get("/admin/shop/edit/*", handleAdminShopEdit);
    router.post("/admin/shop/edit/*", handleAdminShopEdit);
    router.post("/admin/shop/toggle", handleAdminShopToggle);
    router.post("/admin/shop/delete", handleAdminShopDelete);
    router.get("/admin/shop/delete/*", handleAdminShopDelete);
    router.get("/admin/orders", handleAdminOrders);
    router.post("/admin/order/update", handleAdminOrderUpdate);
    router.post("/admin/user/gold", handleAdminSendGold);
    // admin send tokens
    router.post("/admin/user/tokens", handleAdminSendTokens);
    router.post("/admin/user/plan", handleAdminUserPlan);
    // AI 管理
    router.get("/admin/ai", handleAdminAI);
    router.get("/admin/ai/providers", handleAdminAIProviders);
    router.post("/admin/ai/providers", handleAdminAIProviders);
    // 提供方小窗扫描 / 保存启用模型 / 中转自检（均返回 JSON，Key 只在服务端）
    router.get("/admin/ai/providers/scan.json", handleAdminProviderScanApi);
    router.post("/admin/ai/providers/models", handleAdminProviderModels);
    router.post("/admin/ai/test", handleAdminAiTest);
    // 社区举报
    router.post("/report", handleReport);
    router.get("/admin/reports", handleAdminReports);
    router.post("/admin/reports", handleAdminReports);
    router.get("/admin/ai/plans", handleAdminAIPlans);
    router.post("/admin/ai/plans", handleAdminAIPlans);
    router.get("/admin/rejudge", handleAdminRejudge);
    router.post("/admin/rejudge", handleAdminRejudge);
 router.post("/admin/rejudge-single", handleAdminRejudgeSingle);
    router.post("/admin/ai", handleAdminAI);
    // 编译语言管理
    router.get("/admin/languages", handleAdminLanguages);
    router.post("/admin/languages", handleAdminLanguages);
    // bounty tasks
    router.get("/tasks", handleTasks);
    router.get("/tasks/page/*", handleTasks);
    router.get("/rank", handleRank);
    router.get("/task/new", handleTaskNew);
    router.post("/task/new", handleTaskNew);
    router.get("/task/*", handleTaskDetail);
    router.post("/task/*", handleTaskAction);
}

// ============================================================
// coin shop
// ============================================================
std::string Handlers::handleShop(const std::string& method, const std::string& path,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers,
                                 std::map<std::string, std::string>& respHeaders,
                                 std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string content;
    if (user) {
        int lv = DataStore::calcLevel(user->experience);
        content += "<div class='alert alert-info' style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
                   "<span>\U0001fa99 \u91d1\u5e01: <b>" + std::to_string(user->gold) + "</b></span>"
                   "<span>\u7b49\u7ea7: <b>Lv." + std::to_string(lv) + "</b> " + DataStore::getLevelBadge(lv) + "</span>"
                   "<span>\u7ecf\u9a8c: <b>" + std::to_string(user->experience) + "</b></span>"
                   "<span>\u6536\u76ca\u7cfb\u6570: <b>" + std::to_string(getUserEffCoeff(user)) + "%</b></span>"
                   "<span style='opacity:.7;'>\u6bcf\u65e5\u7b7e\u5230\u3001\u9898\u76ee AC \u90fd\u80fd\u83b7\u5f97\u91d1\u5e01\u548c\u7ecf\u9a8c</span></div>";
    content += "<div style='margin:12px 0;padding:12px;border:1px solid #e3e8ee;border-radius:8px;background:#fff;display:flex;flex-wrap:wrap;gap:10px;font-size:13px;'>";
    if (user->renameCards > 0)
        content += "<span style='padding:4px 10px;background:#fff3e0;border-radius:6px;'>\U0001f451 \u6539\u540d\u5361 x" + std::to_string(user->renameCards) + "</span>";
    if (!user->driveBonusUntil.empty() && !common::isTimeExpired(user->driveBonusUntil))
        content += "<span style='padding:4px 10px;background:#e3f2fd;border-radius:6px;'>\U0001f4be \u4e91\u76d8\u589e\u5bb9 +" + std::to_string(user->driveBonusMB) + "MB \uff08\u81f3 " + common::htmlEscape(user->driveBonusUntil) + "\uff09</span>";
    if (!user->coeffUntil.empty() && !common::isTimeExpired(user->coeffUntil))
        content += "<span style='padding:4px 10px;background:#fce4ec;border-radius:6px;'>\U0001f4c8 \u6536\u76ca\u7cfb\u6570 +" + std::to_string(user->coeffBonusPct) + "% \uff08\u81f3 " + common::htmlEscape(user->coeffUntil) + "\uff09</span>";
    if (user->renameCards <= 0 && (user->driveBonusUntil.empty() || common::isTimeExpired(user->driveBonusUntil)) && (user->coeffUntil.empty() || common::isTimeExpired(user->coeffUntil)))
        content += "<span style='color:#999;'>\u6682\u65e0\u751f\u6548\u4e2d\u7684\u5361\u7247</span>";
    content += "</div>";
    } else {
        content += "<div class='alert alert-warning'><a href='/login'>\u767b\u5f55</a> \u540e\u624d\u80fd\u4f7f\u7528\u5546\u57ce</div>";
    }
    content += "<div class='action-bar'><h2>\u5546\u57ce</h2></div>";
    bool admin = isAdmin(user);
    bool onSaleOnly = !admin;
    std::vector<ShopItem> list;
    for (const auto& s : DataStore::instance().shops) {
        if (onSaleOnly && !s.onSale) continue;
        list.push_back(s);
    }
    if (list.empty()) {
        content += "<div class='empty-state'><span class='icon'>\U0001f6d2</span>\u6682\u65e0\u5546\u54c1</div>";
    } else {
        content += "<div style='display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:16px;'>";
        for (const auto& s : list) {
            std::string icon = s.icon.empty() ? "\U0001f4e6" : s.icon;
            content += "<div class='card' style='padding:18px;display:flex;flex-direction:column;gap:8px;'>"
                       "<div style='font-size:36px;text-align:center;'>" + icon + "</div>"
                       "<div style='font-weight:bold;font-size:16px;text-align:center;'>" + common::htmlEscape(s.name) + "</div>"
                       "<div style='font-size:13px;opacity:.75;min-height:34px;'>" + common::htmlEscape(s.description) + "</div>"
                       "<div style='display:flex;justify-content:space-between;align-items:center;'>"
                       "<span style='font-weight:bold;color:#e67e22;'>\U0001fa99 " + std::to_string(s.price) + "</span>"
                       "<span style='font-size:12px;opacity:.6;'>" + (s.stock < 0 ? "\u65e0\u9650" : ("\u5e93\u5b58 " + std::to_string(s.stock))) + "</span></div>"
                       + (s.tokenReward > 0 ? "<div style='text-align:center;font-size:13px;color:#8e44ad;font-weight:bold;'>⚡ +" + std::to_string(s.tokenReward) + " 额外算力点</div>" : "")
                       + (s.effectType == 1 ? "<div style='text-align:center;font-size:13px;color:#e67e22;font-weight:bold;'>\U0001f451 \u6539\u540d\u5361 x" + std::to_string(s.effectValue) + "</div>" : "")
                       + (s.effectType == 2 ? "<div style='text-align:center;font-size:13px;color:#e67e22;font-weight:bold;'>\U0001f4be \u4e91\u76d8 +" + std::to_string(s.effectValue) + "MB / " + std::to_string(s.durationDays) + "\u5929</div>" : "")
                       + (s.effectType == 3 ? "<div style='text-align:center;font-size:13px;color:#e67e22;font-weight:bold;'>\U0001f4c8 \u7cfb\u6570 +" + std::to_string(s.effectValue) + "% / " + std::to_string(s.durationDays) + "\u5929</div>" : "");
            if (!s.onSale) {
                content += "<div style='text-align:center;color:#999;font-size:13px;'>\u5df2\u4e0b\u67b6</div>";
            } else if (!user) {
                content += "<a href='/login' class='btn btn-block'>\u767b\u5f55\u8d2d\u4e70</a>";
            } else {
                content += "<a href='/shop/buy/" + s.id + "' class='btn btn-block' style='background:linear-gradient(135deg,#f39c12,#e67e22);'>\u8d2d\u4e70</a>";
            }
            content += "</div>";
        }
        content += "</div>";
    }
    if (admin) {
        content += "<div class='action-bar' style='margin-top:20px;'><a href='/admin/shop' class='btn'>\u5546\u54c1\u7ba1\u7406</a></div>";
    }
    return renderTemplate("\u5546\u57ce", content, user);
}

std::string Handlers::handleShopBuy(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    auto parts = common::split(path, '/');
    std::string itemId = parts.empty() ? "" : parts.back();
    DataStore& ds = DataStore::instance();
    ShopItem* item = ds.findShopItemById(itemId);
    if (!item) return renderTemplate("\u8d2d\u4e70\u5931\u8d25", "<div class='alert alert-error'>\u5546\u54c1\u4e0d\u5b58\u5728</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>", user);
    if (!item->onSale) return renderTemplate("\u8d2d\u4e70\u5931\u8d25", "<div class='alert alert-error'>\u8be5\u5546\u54c1\u5df2\u4e0b\u67b6</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>", user);

    bool isPhysical = (item->type == 1);
    if (isPhysical && method == "GET") {
        std::string icon = item->icon.empty() ? "\U0001f4e6" : item->icon;
        std::string c = "<div class='card' style='padding:22px;max-width:520px;margin:0 auto;text-align:center;'>"
                        "<div style='font-size:46px;'>" + icon + "</div>"
                        "<h3 style='margin:10px 0;'>" + common::htmlEscape(item->name) + "</h3>"
                        "<div style='color:#7f8c8d;margin-bottom:14px;'>" + common::htmlEscape(item->description) + "</div>"
                        "<div style='font-weight:bold;color:#e67e22;margin-bottom:6px;'>\U0001fa99 " + std::to_string(item->price) + " \u91d1\u5e01 / \u4ef6</div>"
                        "<div style='font-size:13px;color:#8e44ad;margin-bottom:16px;'>\U0001f4e6 \u5b9e\u4f53\u5546\u54c1\uff0c\u4e0b\u5355\u540e\u7531\u7ba1\u7406\u5458\u5904\u7406\u53d1\u8d27</div>"
                        "<form method='POST'>"
                        "<div class='form-group' style='text-align:left;'><label>\u6570\u91cf</label><input name='quantity' type='number' min='1' value='1'/></div>"
                        "<div class='form-group' style='text-align:left;'><label>\u6536\u8d27/\u8054\u7cfb\u4fe1\u606f</label><textarea name='contact' rows='3' placeholder='\u586b\u5199\u59d3\u540d\u3001\u8054\u7cfb\u65b9\u5f0f\u3001\u5730\u5740\u7b49\u4f9b\u7ba1\u7406\u5458\u8054\u7cfb\u53d1\u8d27' required></textarea></div>"
+ security::captchaFieldsFor("shop") +
                        "<input type='submit' value='\u786e\u8ba4\u4e0b\u5355' class='btn' style='width:100%;background:linear-gradient(135deg,#f39c12,#e67e22);'/>"
                        "</form><a href='/shop' class='btn' style='width:100%;margin-top:8px;'>\u8fd4\u56de\u5546\u57ce</a></div>";
        return renderTemplate("\u786e\u8ba4\u4e0b\u5355", c, user);
    }

    int qty = 1;
    std::string contact;
    if (isPhysical) {
        if (!security::verifyCaptchaFor(parseFormData(body, "captcha_token"), parseFormData(body, "captcha_answer"), "shop")) {
            std::string c = "<div class='alert alert-error'>\u9a8c\u8bc1\u7801\u9519\u8bef\u6216\u5df2\u8fc7\u671f</div><a href='/shop/buy/" + itemId + "' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u4e0b\u5355\u5931\u8d25", c, user);
        }
        qty = common::stringToInt(parseFormData(body, "quantity"));
        contact = parseFormData(body, "contact");
        if (qty < 1) qty = 1;
        if (contact.empty()) {
            std::string c = "<div class='alert alert-error'>\u8bf7\u586b\u5199\u6536\u8d27/\u8054\u7cfb\u4fe1\u606f</div><a href='/shop/buy/" + itemId + "' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u4e0b\u5355\u5931\u8d25", c, user);
        }
    }
    int total = item->price * qty;
    if (item->stock >= 0 && item->stock < qty) {
        std::string c = "<div class='alert alert-error'>\u5e93\u5b58\u4e0d\u8db3</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>";
        return renderTemplate("\u4e0b\u5355\u5931\u8d25", c, user);
    }
    if (user->gold < total) {
        std::string c = "<div class='alert alert-error'>\u91d1\u5e01\u4e0d\u8db3\uff0c\u8fd8\u9700 " + std::to_string(total - user->gold) + " \u91d1\u5e01</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>";
        return renderTemplate("\u4e0b\u5355\u5931\u8d25", c, user);
    }
    if (!ds.useGold(user->id, total)) {
        return renderTemplate("\u4e0b\u5355\u5931\u8d25", "<div class='alert alert-error'>\u64cd\u4f5c\u5931\u8d25</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>", user);
    }
    if (item->stock > 0) { item->stock -= qty; ds.saveShops(); }
    if (item->tokenReward > 0) {
        // 商店直充：额外算力点，不随月份重置，用完即没
        aiquota::addExtra(user->id, (long long)item->tokenReward * qty);
    }
    if (item->effectType == 1) { // \u6539\u540d\u5361
        user->renameCards += item->effectValue * qty;
        ds.saveUsers();
    } else if (item->effectType == 2) { // \u4e91\u76d8\u589e\u5bb9\u5361
        user->driveBonusMB += item->effectValue * qty;
        user->driveBonusUntil = common::timeAddDays(item->durationDays);
        ds.saveUsers();
    } else if (item->effectType == 3) { // \u6536\u76ca\u7cfb\u6570\u5361
        user->coeffBonusPct += item->effectValue * qty;
        user->coeffUntil = common::timeAddDays(item->durationDays);
        ds.saveUsers();
    }
    Order o;
    o.id = common::genId();
    o.shopItemId = item->id;
    o.itemName = item->name;
    o.icon = item->icon.empty() ? "\U0001f4e6" : item->icon;
    o.userId = user->id;
    o.username = user->username;
    o.quantity = qty;
    o.price = total;
    o.contact = contact;
    o.status = isPhysical ? ORDER_PENDING : ORDER_FULFILLED;
    o.time = common::getTimeStr();
    ds.orders.push_back(o);
    ds.saveOrders();

    std::string nmsg = "\u8d2d\u4e70\u6210\u529f\uff1a" + item->icon + " " + item->name + " x" + std::to_string(qty) + "\uff08\u4ed8\u51fa " + std::to_string(total) + " \u91d1\u5e01\uff09";
    if (isPhysical) nmsg += "\uff0c\u7b49\u5f85\u7ba1\u7406\u5458\u5904\u7406";
    pushSystemNotification(user->id, nmsg);

    std::string c = "<div class='alert alert-success'>\u8d2d\u4e70\u6210\u529f\uff01 " + item->icon + " " + common::htmlEscape(item->name) + " x" + std::to_string(qty)
                  + "</div><div>\u5269\u4f59\u91d1\u5e01: " + std::to_string(user->gold) + "</div>"
                  + (item->tokenReward > 0 ? "<div>⚡ 额外算力点已到账：+" + std::to_string(item->tokenReward * qty) + "（用完即没，不随月份重置）</div>" : "")
                  + (item->effectType == 1 ? "<div>\U0001f451 \u6539\u540d\u5361\u5230\u8d26\uff1a+" + std::to_string(item->effectValue * qty) + " \u5f20\uff08\u5f53\u524d " + std::to_string(user->renameCards) + " \u5f20\uff09</div>" : "")
                  + (item->effectType == 2 ? "<div>\U0001f4be \u4e91\u76d8\u589e\u5bb9\u5230\u8d26\uff1a+" + std::to_string(item->effectValue * qty) + "MB\uff0c\u6709\u6548\u671f " + std::to_string(item->durationDays) + " \u5929</div>" : "")
                  + (item->effectType == 3 ? "<div>\U0001f4c8 \u6536\u76ca\u7cfb\u6570\u5361\u5230\u8d26\uff1a+" + std::to_string(item->effectValue * qty) + "%\uff08\u6709\u6548\u671f " + std::to_string(item->durationDays) + " \u5929\uff09</div>" : "")
                  + (isPhysical ? "<div style='color:#8e44ad;font-size:13px;'>\U0001f4e6 \u5b9e\u4f53\u5546\u54c1\u5df2\u4e0b\u5355\uff0c\u7ba1\u7406\u5458\u5c06\u8054\u7cfb\u4f60\u53d1\u8d27\u3002\u6536\u8d27\u4fe1\u606f\uff1a" + common::htmlEscape(contact) + "</div>" : "")
                  + "<a href='/shop' class='btn'>\u7ee7\u7eed\u8d2d\u7269</a> <a href='/messages' class='btn'>\u67e5\u770b\u6536\u636e</a>";
    return renderTemplate("\u8d2d\u4e70\u6210\u529f", c, user);
}

std::string Handlers::handleAdminShop(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string content = adminNavbar(user);
    content += "<div class='action-bar'><a href='/admin/shop/add' class='btn'>\u65b0\u589e\u5546\u54c1</a> <a href='/shop' class='btn'>\u67e5\u770b\u5546\u57ce</a> <a href='/admin/orders' class='btn'>\U0001f4e6 \u8d2d\u4e70\u8bb0\u5f55</a></div>";
    if (DataStore::instance().shops.empty()) {
        content += "<div class='empty-state'><span class='icon'>\U0001f6d2</span>\u6682\u65e0\u5546\u54c1</div>";
    } else {
        content += "<table><thead><tr><th>ID</th><th>\u56fe\u6807</th><th>\u540d\u79f0</th><th>\u4ef7\u683c</th><th>\u989d\u5916\u7b97\u529b\u70b9</th><th>\u6548\u679c</th><th>\u5e93\u5b58</th><th>\u72b6\u6001</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
        for (const auto& s : DataStore::instance().shops) {
            std::string icon = s.icon.empty() ? "\U0001f4e6" : s.icon;
            content += "<tr><td>" + s.id.substr(0, 8) + "</td>"
                       "<td>" + icon + "</td><td>" + common::htmlEscape(s.name) + "</td>"
                       "<td>" + std::to_string(s.price) + "</td><td>" + (s.tokenReward > 0 ? std::to_string(s.tokenReward) : "-") + "</td>" + "<td>" + (s.effectType == 1 ? "\u6539\u540d\u5361" : (s.effectType == 2 ? ("\u4e91\u76d8+" + std::to_string(s.effectValue) + "MB") : (s.effectType == 3 ? ("\u7cfb\u6570+" + std::to_string(s.effectValue) + "%") : "-"))) + "</td>" + "<td>" + (s.stock < 0 ? "\u65e0\u9650" : std::to_string(s.stock)) + "</td>"
                       "<td>" + (s.onSale ? "<span style='color:#27ae60;'>\u5df2\u4e0a\u67b6</span>" : "<span style='color:#999;'>\u5df2\u4e0b\u67b6</span>") + "</td>"
                       "<td><a href='/admin/shop/edit/" + s.id + "' class='btn btn-sm btn-outline'>\u7f16\u8f91</a> "
                       "<form method='POST' action='/admin/shop/toggle' style='display:inline;'><input type='hidden' name='id' value='" + s.id + "'/><button class='btn btn-sm " + (s.onSale ? "btn-danger" : "btn-outline") + "'>" + (s.onSale ? "\u4e0b\u67b6" : "\u4e0a\u67b6") + "</button></form> "
                       "<form method='POST' action='/admin/shop/delete' style='display:inline;'><input type='hidden' name='id' value='" + s.id + "'/><button class='btn btn-sm btn-danger'>\u5220\u9664</button></form></td></tr>";
        }
        content += "</tbody></table>";
    }
    return renderTemplate("\u5546\u54c1\u7ba1\u7406", content, user);
}

std::string Handlers::handleAdminShopAdd(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    if (method == "GET") {
        std::string content = "<form method='POST'>"
                             "<div class='form-group'><label>\u540d\u79f0</label><input name='name' required/></div>"
                             "<div class='form-group'><label>\u63cf\u8ff0</label><textarea name='description' rows='3'></textarea></div>"
                             "<div class='form-group'><label>\u4ef7\u683c (\u91d1\u5e01)</label><input name='price' type='number' value='100' required/></div>"
                             "<div class='form-group'><label>\u5e93\u5b58 (-1 \u4e3a\u65e0\u9650)</label><input name='stock' type='number' value='-1'/></div>"
                             "<div class='form-group'><label>\u8d60\u9001\u989d\u5916\u7b97\u529b\u70b9\uff08\u7528\u5b8c\u5373\u6ca1\uff0c0 = \u666e\u901a\u5546\u54c1\uff09</label><input name='tokenReward' type='number' value='0'/></div>"
                             "<div class='form-group'><label>\u6548\u679c\u7c7b\u578b</label><select name='effectType'><option value='0'>\u65e0</option><option value='1'>\u6539\u540d\u5361</option><option value='2'>\u4e91\u76d8\u589e\u5bb9\u5361</option><option value='3'>\u6536\u76ca\u7cfb\u6570\u5361</option></select></div>"
                             "<div class='form-group'><label>\u6548\u679c\u6570\u503c (1:\u5f20\u6570, 2:MB, 3:%)</label><input name='effectValue' type='number' value='0'/></div>"
                             "<div class='form-group'><label>\u6709\u6548\u671f (\u5929, \u6539\u540d\u5361\u4e3a0)</label><input name='durationDays' type='number' value='0'/></div>"
                             "<div class='form-group'><label>\u56fe\u6807 (emoji)</label><input name='icon' placeholder='\U0001f451'/></div>"
                             "<div class='form-group'><label>\u7c7b\u578b</label><select name='type'><option value='0'>\u865a\u62df\uff08\u81ea\u52a8\u53d1\u8d27\uff09</option><option value='1'>\u5b9e\u4f53\uff08\u9700\u7ba1\u7406\u5458\u5904\u7406\uff09</option></select></div><div class='form-group'><label><input type='checkbox' name='onSale' value='1' checked/> \u4e0a\u67b6</label></div>"
                             "<input type='submit' value='\u65b0\u589e' class='btn'/> <a href='/admin/shop' class='btn'>\u53d6\u6d88</a></form>";
        return renderTemplate("\u65b0\u589e\u5546\u54c1", content, user);
    }
    ShopItem s;
    s.id = common::genId();
    s.name = parseFormData(body, "name");
    s.description = parseFormData(body, "description");
    s.price = common::stringToInt(parseFormData(body, "price"));
    s.stock = parseFormData(body, "stock").empty() ? -1 : common::stringToInt(parseFormData(body, "stock"));
    s.tokenReward = parseFormData(body, "tokenReward").empty() ? 0 : common::stringToInt(parseFormData(body, "tokenReward"));
    if (s.tokenReward < 0) s.tokenReward = 0;
    s.effectType = parseFormData(body, "effectType").empty() ? 0 : common::stringToInt(parseFormData(body, "effectType"));
    s.effectValue = parseFormData(body, "effectValue").empty() ? 0 : common::stringToInt(parseFormData(body, "effectValue"));
    s.durationDays = parseFormData(body, "durationDays").empty() ? 0 : common::stringToInt(parseFormData(body, "durationDays"));
    s.icon = parseFormData(body, "icon");
    s.onSale = parseFormData(body, "onSale") == "1";
    s.type = parseFormData(body, "type") == "1" ? 1 : 0;
    s.createTime = common::getTimeStr();
    if (s.name.empty()) {
        std::string c = "<div class='alert alert-error'>\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a</div><a href='/admin/shop/add' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u65b0\u589e\u5931\u8d25", c, user);
    }
    DataStore::instance().shops.push_back(s);
    DataStore::instance().saveShops();
    std::string c = "<div class='alert alert-success'>\u65b0\u589e\u6210\u529f\uff01</div><a href='/admin/shop' class='btn'>\u8fd4\u56de\u7ba1\u7406</a>";
    return renderTemplate("\u65b0\u589e\u6210\u529f", c, user);
}

std::string Handlers::handleAdminShopEdit(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    auto parts = common::split(path, '/');
    std::string itemId = parts.empty() ? "" : parts.back();
    ShopItem* item = DataStore::instance().findShopItemById(itemId);
    if (!item) return "<script>window.location.href='/admin/shop'</script>";
    if (method == "GET") {
        std::string c = "<form method='POST'>"
                       "<div class='form-group'><label>\u540d\u79f0</label><input name='name' value='" + common::htmlEscape(item->name) + "' required/></div>"
                       "<div class='form-group'><label>\u63cf\u8ff0</label><textarea name='description' rows='3'>" + common::htmlEscape(item->description) + "</textarea></div>"
                       "<div class='form-group'><label>\u4ef7\u683c</label><input name='price' type='number' value='" + std::to_string(item->price) + "' required/></div>"
                       "<div class='form-group'><label>\u5e93\u5b58</label><input name='stock' type='number' value='" + std::to_string(item->stock) + "'/></div>"
                       "<div class='form-group'><label>\u8d60\u9001\u989d\u5916\u7b97\u529b\u70b9\uff08\u7528\u5b8c\u5373\u6ca1\uff0c0 = \u666e\u901a\u5546\u54c1\uff09</label><input name='tokenReward' type='number' value='" + std::to_string(item->tokenReward) + "'/></div>"
                       "<div class='form-group'><label>\u6548\u679c\u7c7b\u578b</label><select name='effectType'><option value='0' " + (item->effectType == 0 ? "selected" : "") + ">\u65e0</option><option value='1' " + (item->effectType == 1 ? "selected" : "") + ">\u6539\u540d\u5361</option><option value='2' " + (item->effectType == 2 ? "selected" : "") + ">\u4e91\u76d8\u589e\u5bb9\u5361</option><option value='3' " + (item->effectType == 3 ? "selected" : "") + ">\u6536\u76ca\u7cfb\u6570\u5361</option></select></div>"
                       "<div class='form-group'><label>\u6548\u679c\u6570\u503c</label><input name='effectValue' type='number' value='" + std::to_string(item->effectValue) + "'/></div>"
                       "<div class='form-group'><label>\u6709\u6548\u671f (\u5929)</label><input name='durationDays' type='number' value='" + std::to_string(item->durationDays) + "'/></div>"
                       "<div class='form-group'><label>\u56fe\u6807</label><input name='icon' value='" + common::htmlEscape(item->icon) + "'/></div>"
                       "<div class='form-group'><label>\u7c7b\u578b</label><select name='type'><option value='0' " + (item->type==0?"selected":"") + ">\u865a\u62df\uff08\u81ea\u52a8\u53d1\u8d27\uff09</option><option value='1' " + (item->type==1?"selected":"") + ">\u5b9e\u4f53\uff08\u9700\u7ba1\u7406\u5458\u5904\u7406\uff09</option></select></div><div class='form-group'><label><input type='checkbox' name='onSale' value='1' " + (item->onSale ? "checked" : "") + "/> \u4e0a\u67b6</label></div>"
                       "<input type='submit' value='\u4fdd\u5b58' class='btn'/> <a href='/admin/shop' class='btn'>\u53d6\u6d88</a></form>";
        return renderTemplate("\u7f16\u8f91\u5546\u54c1", c, user);
    }
    item->name = parseFormData(body, "name");
    item->description = parseFormData(body, "description");
    item->price = common::stringToInt(parseFormData(body, "price"));
    item->stock = parseFormData(body, "stock").empty() ? -1 : common::stringToInt(parseFormData(body, "stock"));
    item->tokenReward = parseFormData(body, "tokenReward").empty() ? 0 : common::stringToInt(parseFormData(body, "tokenReward"));
    if (item->tokenReward < 0) item->tokenReward = 0;
    item->effectType = parseFormData(body, "effectType").empty() ? 0 : common::stringToInt(parseFormData(body, "effectType"));
    item->effectValue = parseFormData(body, "effectValue").empty() ? 0 : common::stringToInt(parseFormData(body, "effectValue"));
    item->durationDays = parseFormData(body, "durationDays").empty() ? 0 : common::stringToInt(parseFormData(body, "durationDays"));
    item->icon = parseFormData(body, "icon");
    item->onSale = parseFormData(body, "onSale") == "1";
    item->type = parseFormData(body, "type") == "1" ? 1 : 0;
    DataStore::instance().saveShops();
    std::string c = "<div class='alert alert-success'>\u4fdd\u5b58\u6210\u529f</div><a href='/admin/shop' class='btn'>\u8fd4\u56de</a>";
    return renderTemplate("\u4fdd\u5b58\u6210\u529f", c, user);
}

std::string Handlers::handleAdminShopToggle(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string id = parseFormData(body, "id");
    ShopItem* item = DataStore::instance().findShopItemById(id);
    if (item) {
        item->onSale = !item->onSale;
        DataStore::instance().saveShops();
    }
    return "<script>window.location.href='/admin/shop'</script>";
}

std::string Handlers::handleAdminShopDelete(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string id;
    if (method == "POST") {
        id = parseFormData(body, "id");
    } else {
        auto parts = common::split(path, '/');
        id = parts.empty() ? "" : parts.back();
    }
    auto& shops = DataStore::instance().shops;
    for (auto it = shops.begin(); it != shops.end(); ++it) {
        if (it->id == id) { shops.erase(it); break; }
    }
    DataStore::instance().saveShops();
    return "<script>window.location.href='/admin/shop'</script>";
}

// ============================================================
// \u60ac\u8d4f\u4efb\u52a1
// ============================================================
static std::string taskStatusText(int st) {
    switch (st) {
        case TASK_OPEN: return "\u60ac\u8d4f\u4e2d";
        case TASK_ACCEPTED: return "\u8fdb\u884c\u4e2d";
        case TASK_COMPLETED: return "\u5f85\u786e\u8ba4";
        case TASK_DONE: return "\u5df2\u5b8c\u6210";
        case TASK_CANCELLED: return "\u5df2\u5173\u95ed";
        default: return "\u672a\u77e5";
    }
}

static std::string taskStatusBadge(int st) {
    std::string color;
    switch (st) {
        case TASK_OPEN: color = "#27ae60"; break;
        case TASK_ACCEPTED: color = "#3498db"; break;
        case TASK_COMPLETED: color = "#f39c12"; break;
        case TASK_DONE: color = "#2ecc71"; break;
        case TASK_CANCELLED: color = "#95a5a6"; break;
        default: color = "#95a5a6";
    }
    return "<span class='badge' style='background:" + color + "'>" + taskStatusText(st) + "</span>";
}

static std::string helperStateText(int st) {
    switch (st) {
        case TH_ACCEPTED: return "\u5df2\u63a5\u5355";
        case TH_SUBMITTED: return "\u5df2\u63d0\u4ea4";
        case TH_REJECTED: return "\u5df2\u9a73\u56de";
        default: return "\u672a\u77e5";
    }
}

static void refreshTaskStatus(TaskItem& t) {
    if (t.status == TASK_DONE || t.status == TASK_CANCELLED) return;
    bool anySubmitted = false, anyAccepted = false;
    for (const auto& h : t.helpers) {
        if (h.state == TH_SUBMITTED) anySubmitted = true;
        else if (h.state == TH_ACCEPTED) anyAccepted = true;
    }
    if (anySubmitted) t.status = TASK_COMPLETED;
    else if (anyAccepted) t.status = TASK_ACCEPTED;
    else t.status = TASK_OPEN;
}

static std::string taskCardRow(const TaskItem& t) {
    User* author = DataStore::instance().findUserById(t.authorId);
    std::string authorName = author ? common::htmlEscape(author->username) : "\u672a\u77e5";
    int helperCount = (int)t.helpers.size();
    std::string helperTxt = helperCount > 0 ? std::to_string(helperCount) + " \u4eba\u5e2e\u52a9" : "\u6682\u65e0";
    return "<div class='task-card'>"
        "<a class='tt' href='/task/" + t.id + "'>" + common::htmlEscape(t.title) + "</a> "
        + taskStatusBadge(t.status) +
        "<div class='meta'>\U0001fa99 " + std::to_string(t.reward) + " \u91d1\u5e01 \u00b7 " + helperTxt + " \u00b7 " + authorName + "</div>"
        "<div class='meta'>" + t.createTime + "</div></div>";
}

static std::string taskColumn(const std::string& title, const std::string& rows, const std::string& empty) {
    std::string out = "<div class='task-col'><h3>" + title + "</h3>";
    if (rows.empty()) out += "<div style='color:#a0a8b4;font-size:13px;text-align:center;padding:20px 0;'>" + empty + "</div>";
    else out += rows;
    out += "</div>";
    return out;
}

std::string Handlers::handleTasks(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string content;
    content += "<div class='action-bar' style='display:flex;justify-content:space-between;align-items:center;'>"
               "<h2>\U0001f3f7\ufe0f \u60ac\u8d4f\u4efb\u52a1</h2>"
               "<a href='/task/new' class='btn'>\u2795 \u53d1\u5e03\u4efb\u52a1</a></div>";
    if (user) {
        int lv = DataStore::calcLevel(user->experience);
        content += "<div class='alert alert-info' style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
                   "<span>\U0001fa99 \u6211\u7684\u91d1\u5e01: <b>" + std::to_string(user->gold) + "</b></span>"
                   "<span>\u7b49\u7ea7: <b>Lv." + std::to_string(lv) + "</b> " + DataStore::getLevelBadge(lv) + "</span>"
                   "<span style='opacity:.7;'>\u53d1\u5e03\u4efb\u52a1\u9700\u652f\u4ed8\u60ac\u8d4f\u91d1\u5e01\uff0c\u5b8c\u6210\u540e\u53d1\u653e\u7ed9\u63a5\u5355\u8005</span></div>";
    } else {
        content += "<div class='alert alert-warning'><a href='/login'>\u767b\u5f55</a> \u540e\u5373\u53ef\u53d1\u5e03\u6216\u9886\u53d6\u4efb\u52a1</div>";
    }
    auto& tasks = DataStore::instance().tasks;
    int tpage = 1;
    int tperPage = 12;
    if (path.find("/tasks/page/") != std::string::npos) {
        size_t lastSlash = path.find_last_of('/');
        tpage = common::stringToInt(path.substr(lastSlash + 1));
        if (tpage < 1) tpage = 1;
    }
    std::vector<const TaskItem*> vis;
    for (auto it = tasks.rbegin(); it != tasks.rend(); ++it) if (it->status != TASK_DONE) vis.push_back(&(*it));
    int ttotal = (int)vis.size();
    int tpages = (ttotal + tperPage - 1) / tperPage;
    if (tpages < 1) tpages = 1;
    if (tpage > tpages) tpage = tpages;
    int tstart = (tpage - 1) * tperPage;
    int tend = std::min(tstart + tperPage, ttotal);
    bool hasAny = ttotal > 0;
    std::string colOngoing, colOpen, colClosed;
    for (int i = tstart; i < tend; ++i) {
        const TaskItem& t = *vis[i];
        std::string row = taskCardRow(t);
        if (t.status == TASK_OPEN) colOpen += row;
        else if (t.status == TASK_CANCELLED) colClosed += row;
        else colOngoing += row;
    }
    if (!hasAny) {
        content += "<div class='empty-state'><span class='icon'>\U0001f3f7\ufe0f</span>\u6682\u65e0\u4efb\u52a1</div>";
    } else {
        content += "<div class='task-columns'>";
        content += taskColumn("\U0001f4c2 \u8fdb\u884c\u4e2d", colOngoing, "\u6ca1\u6709\u8fdb\u884c\u4e2d\u7684\u4efb\u52a1");
        content += taskColumn("\U0001f3af \u60ac\u8d4f\u4e2d", colOpen, "\u6682\u65e0\u5f00\u653e\u7684\u4efb\u52a1");
        content += taskColumn("\U0001f512 \u5df2\u5173\u95ed", colClosed, "\u6682\u65e0\u5df2\u5173\u95ed\u7684\u4efb\u52a1");
        content += "</div>";
        content += "<div style='margin-top:16px;text-align:center;'>";
        if (tpage > 1) content += "<a href='/tasks/page/" + std::to_string(tpage - 1) + "' class='btn btn-sm'>\u4e0a\u4e00\u9875</a> ";
        content += "<span style='margin:0 10px;color:#7f8c8d;'>\u7b2c " + std::to_string(tpage) + "/" + std::to_string(tpages) + " \u9875</span>";
        if (tpage < tpages) content += "<a href='/tasks/page/" + std::to_string(tpage + 1) + "' class='btn btn-sm'>\u4e0b\u4e00\u9875</a>";
        content += "</div>";
        content += "<style>"
            ".task-columns{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px;}"
            ".task-col{background:#f7f9fb;border-radius:10px;padding:12px;}"
            ".task-col h3{font-size:15px;margin:0 0 10px;color:#34495e;}"
            ".task-card{background:#fff;border:1px solid #e8ecf1;border-radius:8px;padding:10px 12px;margin-bottom:10px;}"
            ".task-card .tt{font-weight:600;color:#2c3e50;text-decoration:none;}"
            ".task-card .meta{font-size:12px;color:#7f8c8d;margin-top:4px;}"
            "</style>";
    }
    return renderTemplate("\u60ac\u8d4f\u4efb\u52a1", content, user);
}

std::string Handlers::handleTaskNew(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    if (method == "GET") {
        std::string content = "<div class='alert alert-info'>\u53d1\u5e03\u4efb\u52a1\u4f1a\u4ece\u4f60\u7684\u91d1\u5e01\u4e2d\u6682\u6263\u60ac\u8d4f\u91d1\u989d\uff0c\u4efb\u52a1\u5b8c\u6210\u540e\u53d1\u653e\u7ed9\u63a5\u5355\u8005\uff1b\u5173\u95ed\u4efb\u52a1\u53ef\u9000\u56de\u91d1\u5e01\u3002</div>"
                             "<form method='POST'>"
                             "<div class='form-group'><label>\u4efb\u52a1\u6807\u9898</label><input name='title' required/></div>"
                             "<div class='form-group'><label>\u4efb\u52a1\u63cf\u8ff0</label><textarea name='description' rows='5' placeholder='\u8bf7\u8be6\u7ec6\u63cf\u8ff0\u4efb\u52a1\u5185\u5bb9\u3001\u8981\u6c42\u548c\u9a8c\u6536\u6807\u51c6'></textarea></div>"
                             "<div class='form-group'><label>\u60ac\u8d4f\u91d1\u5e01</label><input name='reward' type='number' min='1' value='50' required/></div>"
                             + security::captchaFieldsFor("task") +
                             "<input type='submit' value='\u53d1\u5e03\u4efb\u52a1' class='btn'/> <a href='/tasks' class='btn'>\u53d6\u6d88</a></form>";
        return renderTemplate("\u53d1\u5e03\u4efb\u52a1", content, user);
    }
    if (!security::verifyCaptchaFor(parseFormData(body, "captcha_token"), parseFormData(body, "captcha_answer"), "task")) {
        return renderTemplate("\u53d1\u5e03\u5931\u8d25", "<div class='alert alert-error'>\u9a8c\u8bc1\u7801\u9519\u8bef\u6216\u5df2\u8fc7\u671f</div><a href='/task/new' class='btn'>\u8fd4\u56de</a>", user);
    }
    std::string title = parseFormData(body, "title");
    std::string description = parseFormData(body, "description");
    int reward = common::stringToInt(parseFormData(body, "reward"));
    if (title.empty()) {
        return renderTemplate("\u53d1\u5e03\u5931\u8d25", "<div class='alert alert-error'>\u6807\u9898\u4e0d\u80fd\u4e3a\u7a7a</div><a href='/task/new' class='btn'>\u8fd4\u56de</a>", user);
    }
    security::SecConfig _sc = security::getSecConfig();
    if ((int)title.length() > _sc.maxTaskTitle) title = title.substr(0, _sc.maxTaskTitle);
    if ((int)description.length() > _sc.maxTaskDesc) description = description.substr(0, _sc.maxTaskDesc);
    if (reward < 1) reward = 1;
    DataStore& ds = DataStore::instance();
    if (user->gold < reward) {
        std::string c = "<div class='alert alert-error'>\u91d1\u5e01\u4e0d\u8db3\uff0c\u8fd8\u5dee " + std::to_string(reward - user->gold) + " \u91d1\u5e01</div><a href='/task/new' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u53d1\u5e03\u5931\u8d25", c, user);
    }
    if (!ds.useGold(user->id, reward)) {
        return renderTemplate("\u53d1\u5e03\u5931\u8d25", "<div class='alert alert-error'>\u64cd\u4f5c\u5931\u8d25</div><a href='/task/new' class='btn'>\u8fd4\u56de</a>", user);
    }
    TaskItem t;
    t.id = common::genId();
    t.title = title;
    t.description = description;
    t.authorId = user->id;
    t.reward = reward;
    t.status = TASK_OPEN;
    t.acceptorId = "";
    t.createTime = common::getTimeStr();
    ds.tasks.push_back(t);
    ds.saveTasks();
    ds.saveUsers();
    std::string c = "<div class='alert alert-success'>\u53d1\u5e03\u6210\u529f\uff01\u5df2\u6682\u6263 " + std::to_string(reward) + " \u91d1\u5e01\uff08\u5269\u4f59 " + std::to_string(user->gold) + "\uff09</div>"
                    "<a href='/task/" + t.id + "' class='btn'>\u67e5\u770b\u4efb\u52a1</a> <a href='/tasks' class='btn'>\u8fd4\u56de\u5217\u8868</a>";
    return renderTemplate("\u53d1\u5e03\u6210\u529f", c, user);
}

static std::string taskThreadHTML(DataStore& ds, const TaskItem& task, User* viewer, bool isAuthor) {
    std::string body;
    for (const auto& m : task.taskMessages) {
        bool mine = (m.fromId == viewer->id);
        bool visible;
        if (isAuthor) {
            visible = (m.toId == task.authorId || m.fromId == task.authorId);
        } else {
            visible = ((m.fromId == viewer->id && m.toId == task.authorId) ||
                       (m.fromId == task.authorId && m.toId == viewer->id));
        }
        if (!visible) continue;
        std::string fromName;
        if (m.fromId == task.authorId) fromName = "\u53d1\u5e03\u8005";
        else {
            User* fu = ds.findUserById(m.fromId);
            fromName = fu ? common::htmlEscape(fu->username) : "\u5df2\u6ce8\u9500";
        }
        if (!mine && isAuthor) fromName = "\u5e2e\u52a9\u8005\u00b7" + fromName;
        body += "<div class='tmsg " + std::string(mine ? "mine" : "theirs") + "'><div class='tm-name'>" + fromName + "</div>"
                "<div class='tm-text'>" + common::htmlEscape(m.content) + "</div>"
                "<div class='tm-time'>" + m.sendTime + "</div></div>";
    }
    if (body.empty()) body = "<div class='chat-empty'>\u6682\u65e0\u6d88\u606f</div>";

    std::string form;
    if (isAuthor) {
        form += "<form method='POST' action='/task/" + task.id + "/msg' class='tform'>"
                "<select name='toId' required>";
        for (const auto& h : task.helpers) {
            User* hu = ds.findUserById(h.userId);
            form += "<option value='" + h.userId + "'>" + (hu ? common::htmlEscape(hu->username) : h.userId) + "</option>";
        }
        form += "</select> "
                "<input type='text' name='content' placeholder='\u56de\u590d\u7ed9\u9009\u5b9a\u5e2e\u52a9\u8005\u2026' required/>"
                "<button class='btn btn-sm'>\u53d1\u9001</button></form>";
    } else {
        form += "<form method='POST' action='/task/" + task.id + "/msg' class='tform'>"
                "<input type='hidden' name='toId' value='" + task.authorId + "'/>"
                "<input type='text' name='content' placeholder='\u79c1\u5bc6\u53d1\u9001\u7ed9\u53d1\u5e03\u8005\u2026' required/>"
                "<button class='btn btn-sm'>\u53d1\u9001</button></form>";
    }

    return "<div class='tthread'>" + body + form + "</div>"
           "<style>"
           ".tthread{background:#f7f9fb;border:1px solid #e8ecf1;border-radius:10px;padding:12px;}"
           ".tmsg{max-width:75%;margin-bottom:8px;padding:7px 11px;border-radius:10px;}"
           ".tmsg.theirs{background:#fff;border:1px solid #e8ecf1;}"
           ".tmsg.mine{margin-left:auto;background:#667eea;color:#fff;}"
           ".tm-name{font-size:11px;opacity:.7;margin-bottom:2px;}"
           ".tm-text{font-size:14px;white-space:pre-wrap;word-break:break-word;}"
           ".tm-time{font-size:11px;opacity:.6;margin-top:2px;}"
           ".tform{display:flex;gap:8px;margin-top:12px;}"
           ".tform select,.tform input[type=text]{flex:1;border:1px solid #e0e5ec;border-radius:8px;padding:7px 10px;font-size:14px;}"
           "</style>";
}

std::string Handlers::handleTaskDetail(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    auto parts = common::split(path, '/');
    std::string id = parts.empty() ? "" : parts.back();
    DataStore& ds = DataStore::instance();
    TaskItem* task = ds.findTaskById(id);
    if (!task) return renderTemplate("\u4efb\u52a1\u4e0d\u5b58\u5728", "<div class='alert alert-error'>\u4efb\u52a1\u4e0d\u5b58\u5728</div><a href='/tasks' class='btn'>\u8fd4\u56de</a>", user);
    refreshTaskStatus(*task);
    User* author = ds.findUserById(task->authorId);
    bool isAuthor = user && user->id == task->authorId;
    bool isHelper = false;
    int myHelperState = TH_ACCEPTED;
    for (const auto& h : task->helpers) if (user && h.userId == user->id) { isHelper = true; myHelperState = h.state; }

    std::string content;
    content += "<div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;'>"
               "<h2 style='margin:0;'>" + common::htmlEscape(task->title) + "</h2>"
               + taskStatusBadge(task->status) + "</div>";
    content += "<div style='font-size:14px;color:#7f8c8d;margin:8px 0 16px;'>\U0001fa99 \u60ac\u8d4f: <b style='color:#e67e22;'>" + std::to_string(task->reward) + "</b> \u91d1\u5e01\u3000"
               "\U0001f464 \u53d1\u5e03\u8005: " + (author ? common::htmlEscape(author->username) : "\u672a\u77e5") + "\u3000"
               "\U0001f552 \u521b\u5efa: " + task->createTime + "</div>";
    content += "<div class='card' style='white-space:pre-wrap;'><b>\u4efb\u52a1\u63cf\u8ff0</b><br>" + common::htmlEscape(task->description) + "</div>";

    std::string actions = "<div class='card-actions' style='margin-top:20px;'>";
    if (task->status == TASK_OPEN || task->status == TASK_ACCEPTED || task->status == TASK_COMPLETED) {
        if (user && isHelper && myHelperState != TH_SUBMITTED) {
            actions += "<form method='POST' action='/task/" + task->id + "/submit' style='display:inline;'><button class='btn'>\u63d0\u4ea4\u5b8c\u6210</button></form>";
        } else if (user && !isAuthor && task->status != TASK_COMPLETED) {
            actions += "<form method='POST' action='/task/" + task->id + "/accept' style='display:inline;'><button class='btn btn-success'>\u9886\u53d6\u4efb\u52a1</button></form>";
        }
        if (isAuthor) {
            actions += "<form method='POST' action='/task/" + task->id + "/close' style='display:inline;' onsubmit=\"return confirm('\u786e\u5b9a\u5173\u95ed\u5e76\u9000\u56de\u91d1\u5e01\uff1f');\"><button class='btn btn-danger'>\u5173\u95ed\u4efb\u52a1</button></form>";
        }
    } else if (task->status == TASK_DONE) {
        actions += "<span style='color:#27ae60;font-size:13px;'>\u2705 \u4efb\u52a1\u5df2\u5b8c\u6210\uff0c\u91d1\u5e01\u5df2\u53d1\u653e</span>";
    } else if (task->status == TASK_CANCELLED) {
        actions += "<span style='color:#95a5a6;font-size:13px;'>\u4efb\u52a1\u5df2\u5173\u95ed\uff0c\u91d1\u5e01\u5df2\u9000\u56de</span>";
    }
    actions += " <a href='/tasks' class='btn'>\u8fd4\u56de\u5217\u8868</a></div>";
    content += actions;

    if (isAuthor) {
        content += "<h3 style='margin:24px 0 10px;'>\U0001f91d \u5e2e\u52a9\u8005</h3>";
        if (task->helpers.empty()) {
            content += "<div class='alert alert-info'>\u8fd8\u6ca1\u6709\u4eba\u9886\u53d6\u8fd9\u4e2a\u4efb\u52a1</div>";
        } else {
            content += "<table><thead><tr><th>\u5e2e\u52a9\u8005</th><th>\u72b6\u6001</th><th>\u63d0\u4ea4\u65f6\u95f4</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
            for (const auto& h : task->helpers) {
                User* hu = ds.findUserById(h.userId);
                std::string hn = hu ? common::htmlEscape(hu->username) : "\u5df2\u6ce8\u9500";
                content += "<tr><td><a href='/user/" + h.userId + "'>" + hn + "</a></td>"
                           "<td>" + helperStateText(h.state) + "</td>"
                           "<td>" + (h.submitTime.empty() ? "-" : h.submitTime) + "</td><td>";
                if (h.state == TH_SUBMITTED) {
                    content += "<form method='POST' action='/task/" + task->id + "/confirm' style='display:inline;'>"
                               "<input type='hidden' name='helperId' value='" + h.userId + "'/>"
                               "<button class='btn btn-sm btn-success'>\u786e\u8ba4\u5b8c\u6210</button></form> ";
                    content += "<form method='POST' action='/task/" + task->id + "/reject' style='display:inline;'>"
                               "<input type='hidden' name='helperId' value='" + h.userId + "'/>"
                               "<button class='btn btn-sm btn-danger'>\u9a73\u56de</button></form> ";
                } else if (h.state == TH_REJECTED) {
                    content += "<span style='color:#e74c3c;font-size:12px;'>\u5df2\u9a73\u56de</span>";
                } else {
                    content += "<span style='color:#3498db;font-size:12px;'>\u5de5\u4f5c\u4e2d</span>";
                }
                content += "</td></tr>";
            }
            content += "</tbody></table>";
        }
    }

    content += "<h3 style='margin:24px 0 10px;'>\U0001f4ac \u4efb\u52a1\u804a\u5929</h3>";
    if (isAuthor) {
        content += taskThreadHTML(ds, *task, user, true);
    } else if (isHelper) {
        content += taskThreadHTML(ds, *task, user, false);
    } else {
        content += "<div class='alert alert-warning'>\u9886\u53d6\u4efb\u52a1\u540e\u53ef\u4e0e\u53d1\u5e03\u8005\u804a\u5929</div>";
    }
    return renderTemplate("\u4efb\u52a1\u8be6\u60c5", content, user);
}

std::string Handlers::handleTaskAction(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    auto parts = common::split(path, '/');
    std::string act = parts.empty() ? "" : parts.back();
    if (act == "accept") return handleTaskAccept(method, path, body, headers, respHeaders, sessionId);
    if (act == "submit") return handleTaskSubmit(method, path, body, headers, respHeaders, sessionId);
    if (act == "confirm") return handleTaskConfirm(method, path, body, headers, respHeaders, sessionId);
    if (act == "reject") return handleTaskReject(method, path, body, headers, respHeaders, sessionId);
    if (act == "close") return handleTaskClose(method, path, body, headers, respHeaders, sessionId);
    if (act == "msg") return handleTaskMsg(method, path, body, headers, respHeaders, sessionId);
    return "<script>window.location.href='/tasks'</script>";
}

std::string Handlers::handleTaskAccept(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    auto parts = common::split(path, '/');
    std::string id = parts.size() >= 2 ? parts[parts.size() - 2] : "";
    DataStore& ds = DataStore::instance();
    TaskItem* task = ds.findTaskById(id);
    if (!task) return "<script>window.location.href='/tasks'</script>";
    if (task->authorId == user->id) return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u4e0d\u80fd\u9886\u53d6\u81ea\u5df1\u53d1\u5e03\u7684\u4efb\u52a1</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
    if (task->status != TASK_OPEN) return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u8be5\u4efb\u52a1\u5f53\u524d\u4e0d\u53ef\u9886\u53d6</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
    for (auto& h : task->helpers) {
        if (h.userId == user->id) {
            if (h.state == TH_REJECTED) { h.state = TH_ACCEPTED; h.submitTime = ""; refreshTaskStatus(*task); ds.saveTasks(); }
            return "<script>window.location.href='/task/" + id + "'</script>";
        }
    }
    TaskHelper h;
    h.userId = user->id;
    h.state = TH_ACCEPTED;
    task->helpers.push_back(h);
    refreshTaskStatus(*task);
    ds.saveTasks();
    pushSystemNotification(task->authorId, "\U0001f91d \u5e2e\u52a9\u8005 \u201c" + user->username + "\u201d \u9886\u53d6\u4e86\u4efb\u52a1\u300a" + task->title + "\u300b");
    return "<script>window.location.href='/task/" + id + "'</script>";
}

std::string Handlers::handleTaskSubmit(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    auto parts = common::split(path, '/');
    std::string id = parts.size() >= 2 ? parts[parts.size() - 2] : "";
    DataStore& ds = DataStore::instance();
    TaskItem* task = ds.findTaskById(id);
    if (!task) return "<script>window.location.href='/tasks'</script>";
    if (task->status == TASK_DONE || task->status == TASK_CANCELLED) return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u4efb\u52a1\u5df2\u7ed3\u675f</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
    TaskHelper* h = nullptr;
    for (auto& hh : task->helpers) if (hh.userId == user->id) h = &hh;
    if (!h) return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u4f60\u5c1a\u672a\u9886\u53d6\u8be5\u4efb\u52a1</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
    h->state = TH_SUBMITTED;
    h->submitTime = common::getTimeStr();
    refreshTaskStatus(*task);
    ds.saveTasks();
    pushSystemNotification(task->authorId, "\U0001f4e8 \u5e2e\u52a9\u8005 \u201c" + user->username + "\u201d \u63d0\u4ea4\u4e86\u4efb\u52a1\u300a" + task->title + "\u300b\u7684\u5b8c\u6210\u7ed3\u679c");
    return "<script>window.location.href='/task/" + id + "'</script>";
}

std::string Handlers::handleTaskConfirm(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    auto parts = common::split(path, '/');
    std::string id = parts.size() >= 2 ? parts[parts.size() - 2] : "";
    DataStore& ds = DataStore::instance();
    TaskItem* task = ds.findTaskById(id);
    if (!task) return "<script>window.location.href='/tasks'</script>";
    if (task->authorId != user->id) return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u53ea\u6709\u53d1\u5e03\u8005\u624d\u80fd\u786e\u8ba4\u5b8c\u6210</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
    if (task->status == TASK_DONE || task->status == TASK_CANCELLED) {
        std::string c = "<div class='alert alert-error'>\u4efb\u52a1\u5df2\u5b8c\u6210\u6216\u5df2\u5173\u95ed\uff0c\u4e0d\u80fd\u518d\u786e\u8ba4</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>";
        return renderTemplate("\u64cd\u4f5c\u5931\u8d25", c, user);
    }
    std::string helperId = parseFormData(body, "helperId");
    if (helperId.empty()) return "<script>window.location.href='/task/" + id + "'</script>";
    for (auto& h : task->helpers) {
        if (h.userId == helperId && h.state == TH_SUBMITTED) {
            User* helperU = ds.findUserById(helperId);
            int tcoeff = getUserEffCoeff(helperU);
            ds.addGold(helperId, (long long)task->reward * tcoeff / 100);
            ds.addExperience(helperId, (task->reward / 2) * tcoeff / 100);
            h.state = TH_CONFIRMED;
            task->status = TASK_DONE;
            ds.saveTasks();
            pushSystemNotification(helperId, "\u2705 \u4f60\u7684\u4efb\u52a1\u300a" + task->title + "\u300b\u5df2\u88ab\u786e\u8ba4\u5b8c\u6210\uff0c\u83b7\u5f97 " + std::to_string(task->reward) + " \u91d1\u5e01");
            return "<script>window.location.href='/task/" + id + "'</script>";
        }
    }
    return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u8be5\u5e2e\u52a9\u8005\u6ca1\u6709\u5f85\u786e\u8ba4\u7684\u63d0\u4ea4</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
}

std::string Handlers::handleTaskReject(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    auto parts = common::split(path, '/');
    std::string id = parts.size() >= 2 ? parts[parts.size() - 2] : "";
    DataStore& ds = DataStore::instance();
    TaskItem* task = ds.findTaskById(id);
    if (!task) return "<script>window.location.href='/tasks'</script>";
    if (task->authorId != user->id) return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u53ea\u6709\u53d1\u5e03\u8005\u624d\u80fd\u9a73\u56de</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
    std::string helperId = parseFormData(body, "helperId");
    if (helperId.empty()) return "<script>window.location.href='/task/" + id + "'</script>";
    for (auto& h : task->helpers) {
        if (h.userId == helperId && h.state == TH_SUBMITTED) {
            h.state = TH_REJECTED;
            refreshTaskStatus(*task);
            ds.saveTasks();
            pushSystemNotification(helperId, "\u274c \u4f60\u7684\u63d0\u4ea4\u88ab\u53d1\u5e03\u8005\u9a73\u56de\u4e86\uff0c\u4efb\u52a1\u300a" + task->title + "\u300b\u4ecd\u5728\u7ee7\u7eed");
            return "<script>window.location.href='/task/" + id + "'</script>";
        }
    }
    return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u8be5\u5e2e\u52a9\u8005\u6ca1\u6709\u5f85\u786e\u8ba4\u7684\u63d0\u4ea4</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
}

std::string Handlers::handleTaskClose(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    auto parts = common::split(path, '/');
    std::string id = parts.size() >= 2 ? parts[parts.size() - 2] : "";
    DataStore& ds = DataStore::instance();
    TaskItem* task = ds.findTaskById(id);
    if (!task) return "<script>window.location.href='/tasks'</script>";
    if (task->authorId != user->id) return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u53ea\u6709\u53d1\u5e03\u8005\u624d\u80fd\u5173\u95ed\u4efb\u52a1</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
    if (task->status == TASK_DONE || task->status == TASK_CANCELLED) return renderTemplate("\u64cd\u4f5c\u5931\u8d25", "<div class='alert alert-error'>\u4efb\u52a1\u5df2\u7ed3\u675f\uff0c\u65e0\u6cd5\u5173\u95ed</div><a href='/task/" + id + "' class='btn'>\u8fd4\u56de</a>", user);
    ds.addGold(task->authorId, task->reward);
    task->status = TASK_CANCELLED;
    ds.saveTasks();
    pushSystemNotification(task->authorId, "\U0001f512 \u4f60\u5df2\u5173\u95ed\u4efb\u52a1\u300a" + task->title + "\u300b\uff0c\u60ac\u8d4f " + std::to_string(task->reward) + " \u91d1\u5e01\u5df2\u9000\u56de");
    return "<script>window.location.href='/task/" + id + "'</script>";
}

std::string Handlers::handleTaskMsg(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    auto parts = common::split(path, '/');
    std::string id = parts.size() >= 2 ? parts[parts.size() - 2] : "";
    DataStore& ds = DataStore::instance();
    TaskItem* task = ds.findTaskById(id);
    if (!task) return "<script>window.location.href='/tasks'</script>";
    if (task->status == TASK_DONE || task->status == TASK_CANCELLED) return "<script>window.location.href='/task/" + id + "'</script>";
    bool isAuthor = (task->authorId == user->id);
    bool isHelper = false;
    for (const auto& h : task->helpers) if (h.userId == user->id) isHelper = true;
    if (!isAuthor && !isHelper) return "<script>window.location.href='/task/" + id + "'</script>";
    std::string content = parseFormData(body, "content");
    std::string toId = parseFormData(body, "toId");
    if (content.empty()) return "<script>window.location.href='/task/" + id + "'</script>";
    int _ml = security::getSecConfig().maxTaskMsg;
    if ((int)content.length() > _ml) content = content.substr(0, _ml);
    if (!isAuthor) toId = task->authorId;
    if (toId.empty()) return "<script>window.location.href='/task/" + id + "'</script>";
    TaskMessage m;
    m.id = common::genId();
    m.fromId = user->id;
    m.toId = toId;
    m.content = content;
    m.sendTime = common::getTimeStr();
    task->taskMessages.push_back(m);
    ds.saveTasks();
    return "<script>window.location.href='/task/" + id + "'</script>";
}

std::string Handlers::handleAdminUserPlan(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    (void)method; (void)path; (void)headers; (void)respHeaders;
    User* user = getCurrentUser(sessionId);
    if (!adminperm::has(user, "users")) return "<script>window.location.href='/'</script>";
    std::string uid = parseFormData(body, "userId");
    std::string pid = common::trim(parseFormData(body, "planId"));
    User* t = DataStore::instance().findUserById(uid);
    if (t) {
        aiplan::applyPlan(*t, pid);
        DataStore::instance().saveUsers();
    }
    return "<script>window.location.href='/admin/users'</script>";
}

std::string Handlers::handleAdminSendGold(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string username = parseFormData(body, "username");
    int amount = common::stringToInt(parseFormData(body, "amount"));
    std::string note = parseFormData(body, "note");
    if (username.empty() || amount == 0) {
        return "<script>window.location.href='/admin/users'</script>";
    }
    DataStore& ds = DataStore::instance();
    User* target = ds.findUserByName(username);
    if (!target) {
        return "<script>alert('\u7528\u6237\u4e0d\u5b58\u5728');window.location.href='/admin/users'</script>";
    }
    ds.addGold(target->id, amount);
    // \u53d1\u9001\u7ad9\u5185\u4fe1
    pushSystemNotification(target->id, "\U0001fa99 \u7ba1\u7406\u5458\u5411\u4f60\u53d1\u653e\u4e86 " + std::to_string(amount) + " \u91d1\u5e01" + (note.empty() ? "" : "\uff08" + note + "\uff09"));
    return "<script>alert('\u5df2\u5411 " + username + " \u53d1\u653e " + std::to_string(amount) + " \u91d1\u5e01');window.location.href='/admin/users'</script>";
}

// ============================================================
// 管理员 - 调整用户 Token 数
// ============================================================
std::string Handlers::handleAdminSendTokens(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string username = parseFormData(body, "username");
    int amount = common::stringToInt(parseFormData(body, "amount"));
    if (username.empty() || amount == 0) {
        return "<script>window.location.href='/admin/users'</script>";
    }
    DataStore& ds = DataStore::instance();
    User* target = ds.findUserByName(username);
    if (!target) {
        return "<script>alert('\u7528\u6237\u4e0d\u5b58\u5728');window.location.href='/admin/users'</script>";
    }
    int before = target->tokens;
    int after = before + amount;
    if (after < 0) after = 0;
    int delta = after - before;
    if (delta != 0) ds.addTokens(target->id, delta);
    pushSystemNotification(target->id, "\u2699\ufe0f \u7ba1\u7406\u5458\u4e3a\u4f60\u8c03\u6574\u4e86 Token \u6570\u91cf\uff08\u65b0\u4f59\u989d " + std::to_string(after) + "\uff09");
    return "<script>alert('\u5df2\u5c06 " + username + " \u7684 Token \u8c03\u6574\u4e3a " + std::to_string(after) + "');window.location.href='/admin/users'</script>";
}

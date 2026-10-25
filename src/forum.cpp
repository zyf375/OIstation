#include "../include/handlers.h"
#include "../include/forum.h"
#include "../include/session.h"
#include "../include/common.h"
#include "../include/models.h"
#include "../include/security.h"
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

// ======================= 论坛（洛谷式讨论区） =======================
// 板块 / 置顶 / 精华 / 锁定 / 楼层 + 楼中楼 / 收藏 / @提醒 / 编辑 / 搜索排序

namespace {

typedef std::map<std::string, std::string> HeaderMap;

const std::string kSys = "system";

struct BoardInfo {
    const char* id;
    const char* name;
    const char* icon;
    const char* desc;
};

const BoardInfo g_boards[] = {
    {"academic", "学术区", "🔬", "算法思路、卡题求助、知识整理"},
    {"water",    "灌水区", "💬", "闲聊放松，随便水"},
    {"team",     "组队区", "🤝", "招募队友、组队训练"},
    {"site",     "站务区", "📢", "建议反馈、站务公告"}
};
const int g_boardCount = 4;

std::string esc(const std::string& s) { return common::htmlEscape(s); }

// 取全部同名表单值（多选/多空）
std::vector<std::string> formAllLocal(const std::string& body, const std::string& key) {
    std::vector<std::string> out;
    auto parts = common::split(body, '&');
    for (size_t i = 0; i < parts.size(); i++) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        if (common::urlDecode(parts[i].substr(0, eq)) == key)
            out.push_back(common::urlDecode(parts[i].substr(eq + 1)));
    }
    return out;
}

std::string queryOf(const std::string& path, const std::string& key) {
    std::string needle = key + "=";
    size_t p = path.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    size_t amp = path.find('&', p);
    return common::urlDecode(path.substr(p, (amp == std::string::npos) ? std::string::npos : amp - p));
}

std::string pageOf(const std::string& path) {
    std::string p = common::trim(queryOf(path, "page"));
    if (p.empty()) {
        size_t q = path.find("/page/");
        if (q != std::string::npos) p = path.substr(q + 6);
    }
    int v = common::stringToInt(p);
    if (v < 1) v = 1;
    return std::to_string(v);
}

const BoardInfo* boardOf(const std::string& id) {
    for (int i = 0; i < g_boardCount; i++) if (id == g_boards[i].id) return &g_boards[i];
    return &g_boards[0];
}

std::string boardName(const std::string& id) { return std::string(boardOf(id)->name); }
std::string boardIcon(const std::string& id) { return std::string(boardOf(id)->icon); }

// 只对讨论帖生效
bool isDiscuss(const Post& p) { return p.type == DISCUSSION; }

void notifyUser(const std::string& toId, const std::string& content) {
    if (toId.empty()) return;
    Message m;
    m.id = common::genId();
    m.fromId = kSys;
    m.toId = toId;
    m.content = content;
    m.sendTime = common::getTimeStr();
    m.read = false;
    DataStore::instance().messages.push_back(m);
    DataStore::instance().saveMessages();
}

// @提及：从内容里找出 @用户名 并给对应的人发提醒
void notifyMentions(const std::string& content, const std::string& fromName, const std::string& postId, const std::string& selfId) {
    std::vector<std::string> told;   // 同一帖子里 @ 多次只提醒一次
    for (size_t i = 0; i < content.size(); i++) {
        if (content[i] != '@') continue;
        std::string name;
        size_t k = i + 1;
        while (k < content.size()) {
            unsigned char c = (unsigned char)content[k];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
            if (c < 0x80 && !(isalnum(c) || c == '_')) break;
            // 全角标点作为边界（，。：；）
            if (content.compare(k, 3, "\xEF\xBC\x8C") == 0) break;
            if (content.compare(k, 3, "\xE3\x80\x82") == 0) break;
            if (content.compare(k, 3, "\xEF\xBC\x9A") == 0) break;
            if (content.compare(k, 3, "\xEF\xBC\x9B") == 0) break;
            name += content[k];
            k++;
        }
        if (name.empty() || name.size() > 20) continue;
        User* u = DataStore::instance().findUserByName(name);
        if (u && u->id != selfId) {
            bool dup = false;
            for (size_t d = 0; d < told.size(); d++) if (told[d] == u->id) { dup = true; break; }
            if (!dup) {
                told.push_back(u->id);
                notifyUser(u->id, "💬 " + fromName + " 在帖子中 @ 了你：" + std::string("<a href='/post/") + postId + "'>点此查看</a>");
            }
        }
        i = k;
    }
}

std::string avatarOf(User* u, int size) {
    std::string sz = std::to_string(size);
    if (!u) return "<span class='fv-avt' style='width:" + sz + "px;height:" + sz + "px;'>?</span>";
    if (!u->avatar.empty() && common::isBase64Image(u->avatar))
        return "<img src='" + u->avatar + "' style='width:" + sz + "px;height:" + sz + "px;border-radius:50%;object-fit:cover;'/>";
    std::string ch = u->username.empty() ? "?" : std::string(1, (char)toupper(u->username[0]));
    return "<span class='fv-avt' style='width:" + sz + "px;height:" + sz + "px;'>" + esc(ch) + "</span>";
}

// 用户等级徽章（洛谷风：名字 + 颜色徽章）
std::string userBadge(User* u) {
    if (!u) return "<span style='color:#95a5a6;'>已注销</span>";
    int lv = DataStore::calcLevel(u->experience);
    std::string color = "#9aa7b4";
    if (u->rating >= 2400) color = "#e74c3c";
    else if (u->rating >= 2000) color = "#e67e22";
    else if (u->rating >= 1600) color = "#3a86c8";
    else if (u->rating >= 1200) color = "#27ae60";
    return "<a href='/user/" + u->id + "' style='font-weight:600;'>" + esc(u->username) + "</a>"
           "<span class='pill' style='background:" + color + "1a;color:" + color + ";margin-left:6px;font-size:11.5px;'>Lv." +
           std::to_string(lv) + " · " + std::to_string(u->rating) + "</span>";
}

std::string tagChips(const Post& p) {
    std::string h;
    for (size_t i = 0; i < p.tags.size(); i++) {
        std::string t = p.tags[i];
        std::string color = "#3a86c8";
        if (t == "求助") color = "#e74c3c";
        else if (t == "经验" || t == "分享") color = "#27ae60";
        else if (t == "公告") color = "#8e44ad";
        h += "<span class='pill' style='background:" + color + "14;color:" + color + ";border:1px solid " + color + "33;font-size:11.5px;'>" + esc(t) + "</span>";
    }
    return h;
}

std::string stateIcons(const Post& p) {
    std::string h;
    if (p.pinned) h += "<span title='置顶' style='color:#e67e22;'>📌</span>";
    if (p.essence) h += "<span title='精华' style='color:#f1c40f;'>★</span>";
    if (p.locked) h += "<span title='已锁定' style='color:#95a5a6;'>🔒</span>";
    return h;
}

std::string lastReplyOf(const Post& p) {
    return p.lastReplyTime.empty() ? p.createTime : p.lastReplyTime;
}

int hotScore(const Post& p) {
    return p.views + p.likes * 3 + (int)p.comments.size() * 5 + (p.essence ? 60 : 0);
}

bool isAdminUser(User* u) { return Handlers::isAdmin(u) || Handlers::canManageProblems(u); }

std::string discussPagination(const std::string& base, int page, int totalPages) {
    if (totalPages <= 1) return "";
    std::string h = "<div style='display:flex;gap:6px;justify-content:center;margin:18px 0;flex-wrap:wrap;'>";
    if (page > 1) h += "<a class='btn btn-sm' href='" + base + "?page=" + std::to_string(page - 1) + "'>‹ 上一页</a>";
    h += "<span class='btn btn-sm' style='background:var(--brand-soft);color:var(--brand);'>" + std::to_string(page) + " / " + std::to_string(totalPages) + "</span>";
    if (page < totalPages) h += "<a class='btn btn-sm' href='" + base + "?page=" + std::to_string(page + 1) + "'>下一页 ›</a>";
    h += "</div>";
    return h;
}

// 帖子列表行（洛谷式）
std::string postRow(Post* p) {
    User* author = DataStore::instance().findUserById(p->authorId);
    int replies = (int)p->comments.size();
    std::string h = "<div class='frow'>";
    h += "<div class='frow-state'>" + stateIcons(*p) + "</div>";
    h += "<div class='frow-main'>";
    h += "<div class='frow-title'>";
    if (p->pinned) h += "<span class='pill' style='background:#fdf2e3;color:#b9770e;font-size:11px;'>置顶</span> ";
    if (p->essence) h += "<span class='pill' style='background:#fef9e7;color:#b7950b;font-size:11px;'>精华</span> ";
    h += "<a href='/post/" + p->id + "'>" + esc(p->title) + "</a> " + tagChips(*p) + "</div>";
    h += "<div class='frow-meta'>" + (author ? ("<a href='/user/" + author->id + "'>" + esc(author->username) + "</a>") : std::string("已注销")) +
         " · " + p->createTime + "</div>";
    h += "</div>";
    h += "<div class='frow-stat'><b>" + std::to_string(replies) + "</b><span>回复</span></div>";
    h += "<div class='frow-stat'><b>" + std::to_string(p->views) + "</b><span>浏览</span></div>";
    h += "<div class='frow-last'>" + lastReplyOf(*p) + "</div>";
    h += "</div>";
    return h;
}

// ---------- 论坛首页 ----------
std::string forumHome(const std::string& method, const std::string& path, const std::string& body,
                      const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);

    std::string sort = queryOf(path, "sort");
    if (sort != "new" && sort != "hot") sort = "last";
    bool essenceOnly = (queryOf(path, "essence") == "1");
    std::string kw = common::trim(queryOf(path, "q"));
    std::string kwh = kw;
    for (size_t i = 0; i < kwh.size(); i++) kwh[i] = (char)tolower((unsigned char)kwh[i]);

    std::vector<Post*> all;
    for (size_t i = 0; i < DataStore::instance().posts.size(); i++) {
        Post& p = DataStore::instance().posts[i];
        if (isDiscuss(p)) all.push_back(&p);
    }
    std::string today = common::getDateStr();
    int todayCnt = 0;
    for (size_t i = 0; i < all.size(); i++)
        if (all[i]->createTime.substr(0, 10) == today) todayCnt++;

    std::vector<Post*> list;
    for (size_t i = 0; i < all.size(); i++) {
        if (essenceOnly && !all[i]->essence) continue;
        if (!kw.empty()) {
            std::string hay = all[i]->title + " " + all[i]->content;
            for (size_t k = 0; k < hay.size(); k++) hay[k] = (char)tolower((unsigned char)hay[k]);
            if (hay.find(kwh) == std::string::npos) continue;
        }
        list.push_back(all[i]);
    }
    if (sort == "new") std::sort(list.begin(), list.end(), [](Post* a, Post* b) { return a->createTime > b->createTime; });
    else if (sort == "hot") std::sort(list.begin(), list.end(), [](Post* a, Post* b) { return hotScore(*a) > hotScore(*b); });
    else std::sort(list.begin(), list.end(), [](Post* a, Post* b) { return lastReplyOf(*a) > lastReplyOf(*b); });
    std::vector<Post*> pinned, normal;
    for (size_t i = 0; i < list.size(); i++) (list[i]->pinned ? pinned : normal).push_back(list[i]);
    list = pinned;
    list.insert(list.end(), normal.begin(), normal.end());

    const int perPage = 15;
    int page = common::stringToInt(pageOf(path));
    int totalPages = ((int)list.size() + perPage - 1) / perPage;
    if (totalPages < 1) totalPages = 1;
    if (page < 1) page = 1;
    if (page > totalPages) page = totalPages;
    int start = (page - 1) * perPage;
    int end = std::min((int)list.size(), start + perPage);

    std::string c = "<p class='page-sub' style='margin:-4px 0 12px;color:var(--muted);font-size:13px;'>"
                    "学术交流 · 经验分享 · 站务反馈 · 共 " + std::to_string(all.size()) + " 个主题，今日新增 " +
                    std::to_string(todayCnt) + "</p>";
    c += "<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:14px;'>";
    if (user) c += "<a class='btn' href='/discuss/new'>✏ 发帖</a>";
    auto tab = [&](const std::string& s, bool ess, const std::string& label) {
        bool on = (sort == s && essenceOnly == ess);
        std::string href = "/forum?sort=" + s + (ess ? "&essence=1" : "");
        if (!kw.empty()) href += "&q=" + common::urlEncode(kw);
        return "<a class='btn btn-sm" + std::string(on ? "" : " btn-outline") + "' href='" + href + "'>" + label + "</a>";
    };
    c += tab("last", false, "💬 最新回复") + tab("new", false, "🆕 最新发布") + tab("hot", false, "🔥 热门");
    c += tab("new", true, "⭐ 精华");
    c += "<form action='/forum' method='get' style='display:flex;gap:6px;margin-left:auto;'>"
         "<input type='hidden' name='sort' value='" + sort + "'/>" +
         (essenceOnly ? "<input type='hidden' name='essence' value='1'/>" : "") +
         "<input type='text' name='q' value='" + esc(kw) + "' placeholder='搜索标题或内容…'/>"
         "<button class='btn btn-sm' type='submit'>搜索</button></form></div>";
    if (!kw.empty())
        c += "<div style='margin-bottom:10px;font-size:13px;color:var(--muted);'>关键词「" + esc(kw) + "」匹配 " +
             std::to_string(list.size()) + " 个主题 · <a href='/forum'>清除</a></div>";

    c += "<div class='flist'>";
    for (int i = start; i < end; i++) c += postRow(list[i]);
    if (list.empty()) c += "<div class='empty-state'><span class='icon'>💬</span>没有匹配的主题，换个关键词试试</div>";
    c += "</div>";

    if (totalPages > 1) {
        std::string baseQ = "/forum?sort=" + sort + (essenceOnly ? "&essence=1" : "") +
                            (kw.empty() ? "" : ("&q=" + common::urlEncode(kw)));
        c += "<div style='display:flex;gap:6px;justify-content:center;margin:18px 0;flex-wrap:wrap;'>";
        for (int p2 = 1; p2 <= totalPages && p2 <= 20; p2++)
            c += "<a class='btn btn-sm" + std::string(p2 == page ? "" : " btn-outline") + "' href='" + baseQ +
                 "&page=" + std::to_string(p2) + "'>" + std::to_string(p2) + "</a>";
        c += "</div>";
    }

    // 板块入口（紧凑卡片）
    c += "<h3 style='margin:26px 0 12px;'>📚 板块</h3>";
    c += "<div style='display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px;'>";
    for (int b = 0; b < g_boardCount; b++) {
        const BoardInfo& bi = g_boards[b];
        int cnt = 0, tcnt = 0;
        for (size_t i = 0; i < all.size(); i++) {
            if (all[i]->board != bi.id) continue;
            cnt++;
            if (all[i]->createTime.substr(0, 10) == today) tcnt++;
        }
        c += "<a class='board-card' href='/forum/" + std::string(bi.id) + "' style='display:block;text-decoration:none;color:inherit;'>"
             "<div style='display:flex;align-items:center;gap:10px;'>"
             "<div class='board-ic'>" + std::string(bi.icon) + "</div>"
             "<div style='flex:1;min-width:0;'><div style='font-size:15px;font-weight:700;'>" + std::string(bi.name) + "</div>"
             "<div style='color:var(--muted);font-size:12px;'>" + std::string(bi.desc) + "</div></div>"
             "<div style='text-align:right;color:var(--muted);font-size:12px;'>" + std::to_string(cnt) + " 主题";
        if (tcnt > 0) c += "<br/><span style='color:var(--brand);'>+" + std::to_string(tcnt) + " 今日</span>";
        c += "</div></div></a>";
    }
    c += "</div>";
    return Handlers::renderTemplate("讨论区", c, user);
}

// ---------- 板块列表页 /forum/<board> ----------
std::string forumBoard(const std::string& method, const std::string& path, const std::string& body,
                       const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);

    std::string rest = path.substr(7); // after /forum/
    std::string boardId = rest;
    size_t sl = rest.find('/');
    if (sl != std::string::npos) boardId = rest.substr(0, sl);
    if (boardId.empty()) return forumHome(method, path, body, headers, respHeaders, sessionId);

    std::string sort = queryOf(path, "sort");
    if (sort.empty()) sort = "last";
    std::string kw = common::trim(queryOf(path, "q"));
    bool essenceOnly = (queryOf(path, "essence") == "1");
    std::string tagFilter = common::trim(queryOf(path, "tag"));
    bool mineOnly = (queryOf(path, "mine") == "1");

    std::vector<Post*> list;
    for (size_t i = 0; i < DataStore::instance().posts.size(); i++) {
        Post& p = DataStore::instance().posts[i];
        if (!isDiscuss(p)) continue;
        if (p.board != boardId) continue;
        if (essenceOnly && !p.essence) continue;
        if (!tagFilter.empty()) {
            bool hasTag = false;
            for (size_t k = 0; k < p.tags.size(); k++) if (p.tags[k] == tagFilter) hasTag = true;
            if (!hasTag) continue;
        }
        if (mineOnly && (!user || p.authorId != user->id)) continue;
        if (!kw.empty()) {
            std::string hay = p.title + " " + p.content;
            if (hay.find(kw) == std::string::npos) continue;
        }
        list.push_back(&p);
    }
    if (sort == "new") {
        std::sort(list.begin(), list.end(), [](Post* a, Post* b) { return a->createTime > b->createTime; });
    } else if (sort == "hot") {
        std::sort(list.begin(), list.end(), [](Post* a, Post* b) { return hotScore(*a) > hotScore(*b); });
    } else {
        std::sort(list.begin(), list.end(), [](Post* a, Post* b) { return lastReplyOf(*a) > lastReplyOf(*b); });
    }
    // 置顶永远在最前
    std::vector<Post*> pinned, normal;
    for (size_t i = 0; i < list.size(); i++) (list[i]->pinned ? pinned : normal).push_back(list[i]);
    list = pinned;
    list.insert(list.end(), normal.begin(), normal.end());

    const int perPage = 20;
    int page = common::stringToInt(pageOf(path));
    int totalPages = ((int)list.size() + perPage - 1) / perPage;
    if (totalPages < 1) totalPages = 1;
    if (page > totalPages) page = totalPages;
    int start = (page - 1) * perPage;
    int end = std::min((int)list.size(), start + perPage);

    std::string c = "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'>"
                    "<a href='/forum'>讨论区</a> / " + boardName(boardId) + " · " + std::string(boardOf(boardId)->desc) + "</p>";

    std::string baseQ = "";
    if (sort != "last") baseQ += "sort=" + sort;
    if (essenceOnly) baseQ += (baseQ.empty() ? "" : "&") + std::string("essence=1");
    if (!kw.empty()) baseQ += (baseQ.empty() ? "" : "&") + std::string("q=") + common::urlEncode(kw);
    std::string baseUrl = "/forum/" + boardId;
    std::string qs = baseQ.empty() ? "" : ("?" + baseQ);

    c += "<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:14px;'>";
    if (user) c += "<a class='btn' href='/discuss/new?board=" + boardId + "'>✏ 发帖</a>";
    auto tab = [&](const std::string& s, const std::string& label) {
        std::string href = baseUrl + "?sort=" + s + (essenceOnly ? "&essence=1" : "");
        if (sort == s) return "<a class='btn btn-sm' style='background:var(--brand);color:#fff;' href='" + href + "'>" + label + "</a>";
        return "<a class='btn btn-sm' href='" + href + "'>" + label + "</a>";
    };
    c += tab("last", "最新回复") + tab("new", "最新发布") + tab("hot", "最热");
    c += "<a class='btn btn-sm" + std::string(essenceOnly ? "" : " btn-outline") + "' href='" + baseUrl + "?sort=" + sort +
         (essenceOnly ? "" : "&essence=1") + "'>只看精华</a>";
    if (user) c += "<a class='btn btn-sm' href='" + baseUrl + "?mine=1'>我的帖子</a>";
    {
        const char* tg[] = {"求助", "讨论", "经验", "分享", "公告"};
        for (int t = 0; t < 5; t++) {
            std::string href = baseUrl + "?sort=" + sort + "&tag=" + common::urlEncode(tg[t]);
            c += "<a class='btn btn-sm" + std::string(tagFilter == tg[t] ? "" : " btn-outline") + "' href='" + href + "'>" + tg[t] + "</a>";
        }
    }
    c += "<form action='" + baseUrl + "' method='get' style='display:flex;gap:6px;margin-left:auto;'>"
         "<input type='text' name='q' value='" + esc(kw) + "' placeholder='站内搜索…'/>"
         "<input type='hidden' name='sort' value='" + sort + "'/>"
         "<button class='btn btn-sm' type='submit'>搜索</button></form></div>";

    c += "<div class='flist'>";
    for (int i = start; i < end; i++) c += postRow(list[i]);
    if (list.empty()) c += "<div class='empty-state'><span class='icon'>💬</span>这个板块还很安静，来发第一帖吧</div>";
    c += "</div>";
    c += discussPagination(baseUrl, page, totalPages);
    (void)qs;
    return Handlers::renderTemplate(boardName(boardId) + " · 讨论区", c, user);
}

// ---------- 发帖 ----------
std::string discussNew(const std::string& method, const std::string& path, const std::string& body,
                       const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string boardHint = queryOf(path, "board");
    if (boardHint.empty()) boardHint = "academic";

    if (method == "GET") {
        std::string sel;
        for (int i = 0; i < g_boardCount; i++) {
            sel += "<option value='" + std::string(g_boards[i].id) + "'" +
                   (boardHint == g_boards[i].id ? " selected" : "") + ">" + std::string(g_boards[i].icon) + " " +
                   std::string(g_boards[i].name) + " — " + std::string(g_boards[i].desc) + "</option>";
        }
        std::string c = "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'><a href='/forum'>讨论区</a> / 新主题</p>";
        c += "<form method='POST' action='/discuss/new'>";
        c += "<div class='card'>";
        c += "<div class='form-group'><label>板块</label><select name='board'>" + sel + "</select></div>";
        c += "<div class='form-group'><label>标题</label><input name='title' required maxlength='80' placeholder='一句话说清楚问题或主题'/></div>";
        c += "<div class='form-group'><label>标签（可多选）</label><div style='display:flex;gap:14px;flex-wrap:wrap;'>";
        const char* tags[] = {"求助", "讨论", "经验", "分享", "公告"};
        for (int i = 0; i < 5; i++)
            c += "<label style='font-size:14px;'><input type='checkbox' name='tags' value='" + std::string(tags[i]) + "'/> " + tags[i] + "</label>";
        c += "</div></div>";
        c += "<div class='form-group'><label>关联题目（可选，填题号如 1001）</label><input name='problemId' placeholder='留空表示不关联'/></div>";
        c += "<div class='form-group'><label>内容（支持 Markdown / LaTeX）</label><textarea name='content' rows='14' required></textarea></div>";
        c += security::captchaFieldsFor("discuss");
        c += "</div>";
        c += "<div style='display:flex;gap:10px;margin:14px 0;'><button class='btn' type='submit'>发布主题</button>"
             "<a class='btn btn-outline' href='/forum'>取消</a></div></form>";
        return Handlers::renderTemplate("发帖", c, user);
    }

    if (!security::verifyCaptchaFor(Handlers::parseFormData(body, "captcha_token"),
                                    Handlers::parseFormData(body, "captcha_answer"), "discuss")) {
        std::string c = "<div class='alert alert-error'>验证码错误或已过期</div><a class='btn' href='/discuss/new'>返回重填</a>";
        return Handlers::renderTemplate("发布失败", c, user);
    }
    std::string title = common::trim(Handlers::parseFormData(body, "title"));
    std::string content = Handlers::parseFormData(body, "content");
    if (title.empty() || common::trim(content).empty()) {
        std::string c = "<div class='alert alert-error'>标题和内容不能为空</div><a class='btn' href='/discuss/new'>返回重填</a>";
        return Handlers::renderTemplate("发布失败", c, user);
    }
    security::SecConfig sc = security::getSecConfig();
    if ((int)title.size() > sc.maxBlogTitle) title = title.substr(0, sc.maxBlogTitle);
    if ((int)content.size() > sc.maxDiscuss) content = content.substr(0, sc.maxDiscuss);
    if (security::throttleAction("discuss:" + user->id, sc.discussIntervalSec)) {
        std::string c = "<div class='alert alert-error'>发布过于频繁，请稍后再试</div><a class='btn' href='/forum'>返回讨论区</a>";
        return Handlers::renderTemplate("发布失败", c, user);
    }

    Post np;
    np.id = common::genId();
    np.authorId = user->id;
    np.type = DISCUSSION;
    np.title = title;
    np.content = content;
    np.createTime = common::getTimeStr();
    np.lastReplyTime = np.createTime;
    np.views = 0;
    np.likes = 0;
    std::string board = Handlers::parseFormData(body, "board");
    np.board = boardOf(board)->id;
    std::vector<std::string> tags = formAllLocal(body, "tags");
    np.tags = tags;
    std::string pid = common::trim(Handlers::parseFormData(body, "problemId"));
    if (!pid.empty() && DataStore::instance().findProblemById(pid)) np.problemId = pid;
    DataStore::instance().posts.push_back(np);
    DataStore::instance().savePosts();
    DataStore::instance().addExperience(user->id, 5);
    // 新主题里的 @ 也要通知到人（原来只有回复会通知）
    notifyMentions(content, user->username, np.id, user->id);
    return "<script>window.location.href='/post/" + np.id + "'</script>";
}

// ---------- 帖子详情（楼层 + 楼中楼） ----------
std::string postDetail(const std::string& method, const std::string& path, const std::string& body,
                       const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    std::string rest = path.substr(6); // after /post/
    size_t qm = rest.find('?');
    if (qm != std::string::npos) rest = rest.substr(0, qm);   // 去掉 ?reported=1 之类的查询串
    std::string postId = rest;
    size_t sl = rest.find('/');
    if (sl != std::string::npos) postId = rest.substr(0, sl);

    Post* post = DataStore::instance().findPostById(postId);
    if (!post) {
        return Handlers::renderTemplate("帖子", "<div class='alert alert-error'>帖子不存在或已被删除</div><a class='btn' href='/forum'>返回讨论区</a>", user);
    }
    bool admin = isAdminUser(user);
    // 题解审核可见性
    if (post->type == SOLUTION && !post->auditStatus.empty() && post->auditStatus != "approved") {
        if (!user || (user->id != post->authorId && !admin)) {
            return Handlers::renderTemplate("题解", "<div class='alert alert-warning'>该题解尚未通过审核，暂时无法查看</div><a class='btn' href='/problems'>返回题库</a>", user);
        }
    }
    post->views++;

    User* author = DataStore::instance().findUserById(post->authorId);
    std::string typeName = "讨论";
    if (post->type == BLOG) typeName = "博客";
    else if (post->type == SOLUTION) typeName = "题解";
    else if (post->type == BENBEN) typeName = "犇犇";

    std::string c;
    // 举报结果提示
    {
        std::string flag = queryOf(path, "reported");
        if (!flag.empty()) {
            std::string msg = "✅ 已提交举报，管理员会尽快处理";
            if (flag == "dup") msg = "你已经举报过该内容，请等待管理员处理";
            else if (flag == "slow") msg = "操作太频繁，请稍后再试";
            c += "<div class='alert alert-success'>" + msg + "</div>";
        }
    }
    // 面包屑
    if (post->type == DISCUSSION)
        c += "<div class='crumb'><a href='/forum'>讨论区</a> / <a href='/forum/" + post->board + "'>" + boardName(post->board) + "</a> / 主题</div>";
    else if (post->type == SOLUTION)
        c += "<div class='crumb'><a href='/solutions?problem=" + common::urlEncode(post->problemId) + "'>题解区</a> / 题解</div>";
    else
        c += "<div class='crumb'><a href='/blog'>博客</a> / 文章</div>";

    // 标题区
    c += "<div class='thread-head'>";
    c += "<div class='thread-meta'>";
    c += "<span class='pill' style='background:var(--brand-soft);color:var(--brand);'>" + typeName + "</span> ";
    if (post->type == DISCUSSION) c += "<span class='pill' style='background:#f4f6f8;color:#5b6b7c;'>" + boardName(post->board) + "</span> ";
    c += tagChips(*post);
    if (!post->problemId.empty()) {
        Problem* pp = DataStore::instance().findProblemById(post->problemId);
        c += " <span class='pill' style='background:#eaf5ff;color:#2980b9;'>关联 " + esc(pp ? (pp->id + " " + pp->title) : post->problemId) + "</span>";
    }
    c += "</div></div>";

    // 状态条（审核）
    if (post->type == SOLUTION) {
        c += "<div class='alert alert-info' style='margin-bottom:12px;'>";
        if (post->auditStatus == "pending") c += "⏳ 等待管理员审核";
        else if (post->auditStatus == "rejected") c += "❌ 已被驳回" + (post->rejectReason.empty() ? "" : ("：" + esc(post->rejectReason)));
        else if (post->auditStatus == "removed") c += "🚫 已被撤下";
        else c += "✅ 审核通过";
        c += "</div>";
    }

    // 正文（1 楼）
    c += "<div class='post-card'>";
    c += "<div class='post-side'>" + avatarOf(author, 44);
    if (author) {
        int lv = DataStore::calcLevel(author->experience);
        c += "<div style='font-size:12px;color:var(--muted);text-align:center;margin-top:4px;'>Lv." + std::to_string(lv) + "</div>";
    }
    c += "</div>";
    c += "<div class='post-body'>";
    c += "<div class='post-head'>" + userBadge(author) + "<span style='color:var(--muted);margin-left:8px;font-size:12.5px;'>发表于 " +
         post->createTime + " · 浏览 " + std::to_string(post->views) + (post->editTime.empty() ? "" : (" · 编辑于 " + post->editTime)) + "</span></div>";
    c += "<div class='md-body' style='margin-top:10px;'>" + common::markdownToHtml(post->content) + "</div>";

    // 投票（保留原有功能）
    if (!post->pollTitle.empty() && !post->pollOptions.empty()) {
        int totalVotes = 0;
        for (size_t i = 0; i < post->pollVotes.size(); i++) totalVotes += (int)post->pollVotes[i].size();
        bool voted = false;
        if (user) { for (size_t i = 0; i < post->pollVotes.size(); i++) for (size_t k = 0; k < post->pollVotes[i].size(); k++) if (post->pollVotes[i][k] == user->id) voted = true; }
        c += "<div class='card' style='margin-top:14px;'><div style='font-weight:700;margin-bottom:10px;'>📊 " + esc(post->pollTitle) + "</div>";
        if (voted || !user) {
            for (size_t i = 0; i < post->pollOptions.size(); i++) {
                int cnt = (i < post->pollVotes.size()) ? (int)post->pollVotes[i].size() : 0;
                int pct = totalVotes > 0 ? (int)((double)cnt * 100 / totalVotes) : 0;
                c += "<div style='margin-bottom:9px;'><div style='font-size:13.5px;'>" + esc(post->pollOptions[i]) +
                     " <span style='color:var(--muted);'>(" + std::to_string(cnt) + " 票 · " + std::to_string(pct) + "%)</span></div>"
                     "<div style='background:var(--surface-2);border-radius:6px;height:12px;overflow:hidden;'><div style='height:100%;width:" +
                     std::to_string(pct) + "%;background:var(--brand);'></div></div></div>";
            }
            c += "<div style='color:var(--muted);font-size:12.5px;'>共 " + std::to_string(totalVotes) + " 人参与</div>";
        } else {
            c += "<form method='POST' action='/post/" + postId + "/vote'>";
            for (size_t i = 0; i < post->pollOptions.size(); i++)
                c += "<label style='display:block;margin:6px 0;'><input type='radio' name='choice' value='" + std::to_string(i) + "'/> " + esc(post->pollOptions[i]) + "</label>";
            c += "<button class='btn btn-sm' type='submit'>投票</button></form>";
        }
        c += "</div>";
    }
    if (post->type == SOLUTION)
        c += "<div style='margin-top:10px;'><a class='btn btn-sm btn-outline' href='/solutions?problem=" + common::urlEncode(post->problemId) + "'>← 返回该题题解区</a></div>";

    // 操作栏
    bool liked = false, faved = false;
    if (user) {
        for (size_t i = 0; i < post->likedBy.size(); i++) if (post->likedBy[i] == user->id) liked = true;
        for (size_t i = 0; i < post->favoritedBy.size(); i++) if (post->favoritedBy[i] == user->id) faved = true;
    }
    c += "<div class='post-actions'>";
    c += "<form method='POST' action='/post/" + postId + "/like' style='display:inline;'><button class='btn btn-sm" +
         std::string(liked ? "" : " btn-outline") + "' type='submit'>👍 " + std::to_string(post->likes) + "</button></form>";
    c += "<form method='POST' action='/post/" + postId + "/favorite' style='display:inline;'><button class='btn btn-sm" +
         std::string(faved ? "" : " btn-outline") + "' type='submit'>" + (faved ? "⭐ 已收藏" : "☆ 收藏") + "</button></form>";
    if (user && post->authorId != user->id)
        c += "<button class='btn btn-sm btn-outline' type='button' style='color:#c0392b;' onclick=\"openReport('post','" +
             postId + "','" + postId + "','')\">🚩 举报</button>";
    if (user && (post->authorId == user->id || admin))
        c += "<a class='btn btn-sm btn-outline' href='/post/" + postId + "/edit'>✏ 编辑</a>";
    if (post->type == SOLUTION && user && (post->authorId == user->id || admin) &&
        (post->auditStatus == "rejected" || post->auditStatus == "removed"))
        c += "<form method='POST' action='/post/" + postId + "/resubmit' style='display:inline;'><button class='btn btn-sm' type='submit'>🔄 重新申请审核</button></form>";
    c += "</div>";

    // 管理员操作
    if (admin && post->type == DISCUSSION) {
        c += "<div class='admin-ops'><form method='POST' action='/post/" + postId + "/admin' style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;'>"
             "<span style='color:var(--muted);font-size:12.5px;'>管理：</span>";
        c += "<button class='btn btn-sm btn-outline' name='op' value='pin' type='submit'>" + std::string(post->pinned ? "取消置顶" : "置顶") + "</button>";
        c += "<button class='btn btn-sm btn-outline' name='op' value='essence' type='submit'>" + std::string(post->essence ? "取消精华" : "设为精华") + "</button>";
        c += "<button class='btn btn-sm btn-outline' name='op' value='lock' type='submit'>" + std::string(post->locked ? "解锁" : "锁定") + "</button>";
        c += "<select name='board' style='width:120px;'>";
        for (int i = 0; i < g_boardCount; i++)
            c += "<option value='" + std::string(g_boards[i].id) + "'" + (post->board == g_boards[i].id ? " selected" : "") + ">" + std::string(g_boards[i].name) + "</option>";
        c += "</select><button class='btn btn-sm btn-outline' name='op' value='move' type='submit'>移动板块</button>";
        c += "</form></div>";
    }
    c += "</div></div>";

    // 楼层
    // 楼层归类：父楼不存在的（如已删除）也当作顶层，避免"隐身回复"
    std::vector<Comment> tops, subs;
    for (size_t i = 0; i < post->comments.size(); i++) {
        const Comment& cm = post->comments[i];
        bool hasParent = false;
        if (!cm.parentId.empty()) {
            for (size_t k = 0; k < post->comments.size(); k++) {
                if (post->comments[k].id == cm.parentId && post->comments[k].parentId.empty()) { hasParent = true; break; }
            }
        }
        if (hasParent) subs.push_back(cm); else tops.push_back(cm);
    }
    c += "<h3 style='margin:24px 0 12px;'>回复 <span style='color:var(--muted);font-size:14px;'>共 " +
         std::to_string(post->comments.size()) + " 条</span></h3>";
    if (tops.empty()) c += "<div class='empty-td' style='margin-bottom:16px;'>还没有回复，来抢沙发</div>";

    for (size_t i = 0; i < tops.size(); i++) {
        const Comment& cm = tops[i];
        User* cu = DataStore::instance().findUserById(cm.authorId);
        c += "<div class='floor' id='c" + cm.id + "'>";
        c += "<div class='floor-num'>#" + std::to_string(i + 1) + "</div>";
        c += "<div class='floor-main'>";
        c += "<div class='post-head'>" + avatarOf(cu, 30) + " <span style='margin-left:8px;'>" + userBadge(cu) + "</span>"
             "<span style='color:var(--muted);font-size:12.5px;margin-left:8px;'>" + cm.createTime + "</span></div>";
        c += "<div class='md-body' style='margin:8px 0 6px;'>" + common::markdownToHtml(cm.content) + "</div>";
        c += "<div style='display:flex;gap:10px;align-items:center;font-size:12.5px;'>";
        if (user && !post->locked)
            c += "<details style='display:inline-block;'><summary style='cursor:pointer;color:var(--brand);list-style:none;'>↩ 回复</summary>"
                 "<form method='POST' action='/post/" + postId + "/comment' style='margin-top:8px;display:flex;gap:8px;flex-wrap:wrap;'>"
                 "<input type='hidden' name='parentId' value='" + cm.id + "'/>"
                 "<input name='content' placeholder='回复 @" + esc(cu ? cu->username : "") + "' style='flex:1;min-width:220px;'/>"
                 "<button class='btn btn-sm' type='submit'>发送</button></form></details>";
        c += "<form method='POST' action='/post/" + postId + "/clike' style='display:inline;'>"
             "<input type='hidden' name='commentId' value='" + cm.id + "'/>"
             "<button class='btn btn-sm btn-outline' type='submit' style='padding:2px 8px;'>👍 " + std::to_string(cm.likedBy.size()) + "</button></form>";
        if (user && cm.authorId != user->id)
            c += "<button class='btn btn-sm btn-outline' type='button' style='padding:2px 8px;color:#c0392b;' onclick=\"openReport('comment','" +
                 cm.id + "','" + postId + "','" + esc(cu ? cu->username : std::string()) + "')\">🚩</button>";
        c += "</div>";

        // 楼中楼
        int shown = 0;
        for (size_t k = 0; k < subs.size(); k++) {
            if (subs[k].parentId != cm.id) continue;
            User* su = DataStore::instance().findUserById(subs[k].authorId);
            c += "<div class='subfloor'>" + avatarOf(su, 24) + " " + userBadge(su) +
                 " <span style='color:var(--muted);font-size:12px;margin-left:6px;'>" + subs[k].createTime + "</span>"
                 "<div class='md-body' style='margin:6px 0 0;'>" + common::markdownToHtml(subs[k].content) + "</div></div>";
            shown++;
        }
        if (shown) c += "";
        c += "</div></div>";
    }

    // 回复框
    c += "<div style='margin-top:22px;'>";
    if (post->locked) {
        c += "<div class='alert alert-warning'>🔒 本帖已被锁定，无法回复</div>";
    } else if (user) {
        c += "<div class='card'><div style='font-weight:700;margin-bottom:8px;'>发表回复</div>"
             "<form method='POST' action='/post/" + postId + "/comment'>"
             "<div class='form-group'><textarea name='content' rows='5' placeholder='支持 Markdown 与 @用户名 提醒' required></textarea></div>"
             "<div style='display:flex;gap:10px;align-items:center;'><button class='btn' type='submit'>发表回复</button>"
             "<span style='color:var(--muted);font-size:12.5px;'>提示：用 @用户名 可以提醒对方</span></div></form></div>";
    } else {
        c += "<div class='alert alert-info'><a href='/login'>登录</a> 后可以回复</div>";
    }
    c += "</div>";

    // 举报小窗
    c += R"RPT(<div class='modal-mask' id='rpMask'><div class='modal-win' style='max-width:460px;'>
<div class='modal-head'><b>🚩 举报内容</b><button type='button' class='modal-x' onclick='closeReport()'>✕</button></div>
<div class='modal-info' id='rpWho'>请选择举报理由</div>
<form method='POST' action='/report' style='padding:0 16px 16px;'>
<input type='hidden' name='targetType' id='rpType'/>
<input type='hidden' name='targetId' id='rpId'/>
<input type='hidden' name='postId' id='rpPost'/>
<div class='form-group'><label>举报理由</label><select name='reason'>
<option>广告/垃圾信息</option><option>辱骂攻击</option><option>无意义内容</option>
<option>抄袭/侵权</option><option>政治敏感</option><option>其他</option></select></div>
<div class='form-group'><label>补充说明（选填）</label><textarea name='detail' rows='3' maxlength='300' placeholder='简单描述问题，方便管理员快速处理'></textarea></div>
<div style='display:flex;gap:8px;justify-content:flex-end;'><button type='button' class='btn btn-sm btn-outline' onclick='closeReport()'>取消</button><button class='btn btn-sm' type='submit'>提交举报</button></div>
</form></div></div>
<script>
function openReport(type,id,postId,who){
  document.getElementById('rpType').value=type;
  document.getElementById('rpId').value=id;
  document.getElementById('rpPost').value=postId;
  document.getElementById('rpWho').textContent=who?('举报用户 '+who):'请选择举报理由';
  document.getElementById('rpMask').style.display='flex';
}
function closeReport(){document.getElementById('rpMask').style.display='none';}
document.addEventListener('keydown',function(e){if(e.key==='Escape')closeReport();});
</script>)RPT";

    DataStore::instance().savePosts();
    return Handlers::renderTemplate(post->title, c, user);
}

// ---------- 回复（含楼中楼 + @提醒 + 通知楼主） ----------
std::string postReply(const std::string& method, const std::string& path, const std::string& body,
                      const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rest = path.substr(6);
    size_t sl = rest.find('/');
    std::string postId = (sl == std::string::npos) ? rest : rest.substr(0, sl);
    Post* post = DataStore::instance().findPostById(postId);
    if (!post) return "<script>window.location.href='/forum'</script>";
    if (post->locked && !isAdminUser(user)) return "<script>window.location.href='/post/" + postId + "'</script>";

    std::string content = Handlers::parseFormData(body, "content");
    if (common::trim(content).empty()) return "<script>window.location.href='/post/" + postId + "'</script>";
    int maxLen = security::getSecConfig().maxComment;
    if ((int)content.size() > maxLen) content = content.substr(0, maxLen);
    if (security::throttleAction("reply:" + user->id, 3)) return "<script>window.location.href='/post/" + postId + "'</script>";

    Comment cm;
    cm.id = common::genId();
    cm.authorId = user->id;
    cm.content = content;
    cm.createTime = common::getTimeStr();
    cm.parentId = Handlers::parseFormData(body, "parentId");
    post->comments.push_back(cm);
    post->lastReplyTime = cm.createTime;
    DataStore::instance().savePosts();
    DataStore::instance().addExperience(user->id, 2);

    // 通知：楼主 + 被回复者 + @提及
    if (post->authorId != user->id)
        notifyUser(post->authorId, "💬 " + user->username + " 回复了你的主题《" + post->title + "》：<a href='/post/" + postId + "'>查看</a>");
    if (!cm.parentId.empty()) {
        for (size_t i = 0; i < post->comments.size(); i++) {
            if (post->comments[i].id == cm.parentId) {
                if (post->comments[i].authorId != user->id && post->comments[i].authorId != post->authorId)
                    notifyUser(post->comments[i].authorId, "💬 " + user->username + " 回复了你：<a href='/post/" + postId + "'>查看</a>");
                break;
            }
        }
    }
    notifyMentions(content, user->username, postId, user->id);
    return "<script>window.location.href='/post/" + postId + "#c" + cm.id + "'</script>";
}

// ---------- 评论点赞 ----------
std::string commentLike(const std::string& method, const std::string& path, const std::string& body,
                        const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rest = path.substr(6);
    size_t sl = rest.find('/');
    std::string postId = (sl == std::string::npos) ? rest : rest.substr(0, sl);
    std::string cid = Handlers::parseFormData(body, "commentId");
    Post* post = DataStore::instance().findPostById(postId);
    if (post) {
        for (size_t i = 0; i < post->comments.size(); i++) {
            if (post->comments[i].id != cid) continue;
            std::vector<std::string>& lb = post->comments[i].likedBy;
            bool has = false;
            for (size_t k = 0; k < lb.size(); k++) if (lb[k] == user->id) has = true;
            std::vector<std::string> nv;
            for (size_t k = 0; k < lb.size(); k++) if (lb[k] != user->id) nv.push_back(lb[k]);
            if (!has) nv.push_back(user->id);
            post->comments[i].likedBy = nv;
            DataStore::instance().savePosts();
            break;
        }
    }
    return "<script>window.location.href='/post/" + postId + "'</script>";
}

// ---------- 收藏 ----------
std::string postFavorite(const std::string& method, const std::string& path, const std::string& body,
                         const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rest = path.substr(6);
    size_t sl = rest.find('/');
    std::string postId = (sl == std::string::npos) ? rest : rest.substr(0, sl);
    Post* post = DataStore::instance().findPostById(postId);
    if (post) {
        bool has = false;
        for (size_t i = 0; i < post->favoritedBy.size(); i++) if (post->favoritedBy[i] == user->id) has = true;
        std::vector<std::string> nv;
        for (size_t i = 0; i < post->favoritedBy.size(); i++) if (post->favoritedBy[i] != user->id) nv.push_back(post->favoritedBy[i]);
        if (!has) nv.push_back(user->id);
        post->favoritedBy = nv;
        DataStore::instance().savePosts();
    }
    return "<script>window.location.href='/post/" + postId + "'</script>";
}

// ---------- 我的收藏 ----------
std::string myFavorites(const std::string& method, const std::string& path, const std::string& body,
                        const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)path; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string c = "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'><a href='/forum'>讨论区</a> / 收藏夹</p>";
    c += "<div class='flist'>";
    int n = 0;
    for (size_t i = DataStore::instance().posts.size(); i > 0; i--) {
        Post& p = DataStore::instance().posts[i - 1];
        bool fav = false;
        for (size_t k = 0; k < p.favoritedBy.size(); k++) if (p.favoritedBy[k] == user->id) fav = true;
        if (!fav) continue;
        c += postRow(&p);
        n++;
    }
    if (n == 0) c += "<div class='empty-state'><span class='icon'>⭐</span>还没有收藏的帖子</div>";
    c += "</div>";
    return Handlers::renderTemplate("我的收藏", c, user);
}

// ---------- 编辑帖子 ----------
std::string postEdit(const std::string& method, const std::string& path, const std::string& body,
                     const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rest = path.substr(6);
    size_t sl = rest.find('/');
    std::string postId = (sl == std::string::npos) ? rest : rest.substr(0, sl);
    Post* post = DataStore::instance().findPostById(postId);
    if (!post) return "<script>window.location.href='/forum'</script>";
    bool admin = isAdminUser(user);
    if (post->authorId != user->id && !admin) return "<script>window.location.href='/post/" + postId + "'</script>";

    if (method == "GET") {
        std::string sel;
        for (int i = 0; i < g_boardCount; i++)
            sel += "<option value='" + std::string(g_boards[i].id) + "'" + (post->board == g_boards[i].id ? " selected" : "") +
                   ">" + std::string(g_boards[i].name) + "</option>";
        std::string c = "<form method='POST'>";
        c += "<div class='card'>";
        c += "<div class='form-group'><label>板块</label><select name='board'>" + sel + "</select></div>";
        c += "<div class='form-group'><label>标题</label><input name='title' value='" + esc(post->title) + "' required/></div>";
        c += "<div class='form-group'><label>内容</label><textarea name='content' rows='16' required>" + esc(post->content) + "</textarea></div>";
        c += "</div><div style='display:flex;gap:10px;margin:14px 0;'><button class='btn' type='submit'>保存修改</button>"
             "<a class='btn btn-outline' href='/post/" + postId + "'>取消</a></div></form>";
        return Handlers::renderTemplate("编辑主题", c, user);
    }
    std::string title = common::trim(Handlers::parseFormData(body, "title"));
    std::string content = Handlers::parseFormData(body, "content");
    if (!title.empty()) post->title = title;
    if (!common::trim(content).empty()) post->content = content;
    std::string b = Handlers::parseFormData(body, "board");
    if (!b.empty()) post->board = boardOf(b)->id;
    post->editTime = common::getTimeStr();
    DataStore::instance().savePosts();
    return "<script>window.location.href='/post/" + postId + "'</script>";
}

// ---------- 管理员操作 ----------
std::string postAdminOp(const std::string& method, const std::string& path, const std::string& body,
                        const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!isAdminUser(user)) return "<script>window.location.href='/'</script>";
    std::string rest = path.substr(6);
    size_t sl = rest.find('/');
    std::string postId = (sl == std::string::npos) ? rest : rest.substr(0, sl);
    Post* post = DataStore::instance().findPostById(postId);
    if (!post) return "<script>window.location.href='/forum'</script>";
    std::string op = Handlers::parseFormData(body, "op");
    if (op == "pin") post->pinned = !post->pinned;
    else if (op == "essence") post->essence = !post->essence;
    else if (op == "lock") post->locked = !post->locked;
    else if (op == "move") {
        std::string b = Handlers::parseFormData(body, "board");
        if (!b.empty()) post->board = boardOf(b)->id;
    }
    DataStore::instance().savePosts();
    return "<script>window.location.href='/post/" + postId + "'</script>";
}

} // namespace

void registerForumRoutes(Router& router) {
    // 讨论区入口（覆盖旧的 /discuss）
    router.get("/forum", forumHome);
    router.get("/discuss", forumHome);
    router.get("/forum/*", forumBoard);
    router.get("/discuss/page/*", forumHome);

    // 发帖（覆盖旧表单，带板块/标签）
    router.get("/discuss/new", discussNew);
    router.post("/discuss/new", discussNew);

    // 帖子子操作必须排在 /post/* 之前
    router.post("/post/*/comment", postReply);
    router.post("/post/*/clike", commentLike);
    router.post("/post/*/favorite", postFavorite);
    router.post("/post/*/admin", postAdminOp);
    router.get("/post/*/edit", postEdit);
    router.post("/post/*/edit", postEdit);
    router.get("/post/*", postDetail);
    router.get("/my/favorites", myFavorites);
}


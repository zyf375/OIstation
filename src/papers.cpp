#include "../include/handlers.h"
#include "../include/papers.h"
#include "../include/session.h"
#include "../include/common.h"
#include "../include/models.h"
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

// ======================= 试卷模块（选择题 + 填空题） =======================
// 设计：题库里是「编程题」，试卷是「客观题测验」，可自动批改、可限时、可看逐题解析。

namespace {

typedef std::map<std::string, std::string> HeaderMap;

// 取出全部同名表单值（多选框用）
std::vector<std::string> formAll(const std::string& body, const std::string& key) {
    std::vector<std::string> out;
    auto parts = common::split(body, '&');
    for (size_t i = 0; i < parts.size(); i++) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        std::string k = common::urlDecode(parts[i].substr(0, eq));
        if (k == key) out.push_back(common::urlDecode(parts[i].substr(eq + 1)));
    }
    return out;
}

std::string esc(const std::string& s) { return common::htmlEscape(s); }

std::string queryOf(const std::string& path, const std::string& key) {
    std::string needle = key + "=";
    size_t p = path.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    size_t amp = path.find('&', p);
    return common::urlDecode(path.substr(p, (amp == std::string::npos) ? std::string::npos : amp - p));
}

int paperFullScore(const Paper& p) {
    int t = 0;
    for (size_t i = 0; i < p.questions.size(); i++) t += p.questions[i].score;
    return t;
}

// 选择题答案归一化：只保留字母、转大写、去重排序
std::string normChoice(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z' && r.find(c) == std::string::npos) r += c;
    }
    std::sort(r.begin(), r.end());
    return r;
}

// 填空答案归一化：去空白、转小写
std::string normFill(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        r += c;
    }
    return r;
}

// 某题的标准答案里有多少个空
int fillBlanks(const std::string& answer) {
    if (answer.empty()) return 1;
    int n = 1;
    for (size_t i = 0; i < answer.size(); i++) if (answer[i] == '|') n++;
    return n;
}

// 判定一道题得分
int gradeQuestion(const PaperQuestion& q, const std::string& userAnswer) {
    if (q.type == "fill") {
        std::vector<std::string> stds = common::split(q.answer, '|');
        std::vector<std::string> usr = common::split(userAnswer, '|');
        int n = (int)stds.size();
        if (n < 1) return 0;
        int hit = 0;
        for (int i = 0; i < n; i++) {
            std::string u = (i < (int)usr.size()) ? normFill(usr[i]) : "";
            if (u.empty()) continue;
            std::vector<std::string> accepts = common::split(stds[i], ',');
            bool ok = false;
            for (size_t k = 0; k < accepts.size(); k++) {
                if (normFill(accepts[k]) == u) { ok = true; break; }
            }
            if (ok) hit++;
        }
        if (hit == 0) return 0;
        return (int)((long long)q.score * hit / n);
    }
    return (normChoice(userAnswer) == normChoice(q.answer)) ? q.score : 0;
}

// 试卷卡片
std::string paperCard(Paper& p, User* user) {
    int full = paperFullScore(p);
    int myBest = -1;
    int myTimes = 0;
    if (user) {
        for (size_t i = 0; i < DataStore::instance().paperSubs.size(); i++) {
            const PaperSubmission& ps = DataStore::instance().paperSubs[i];
            if (ps.paperId == p.id && ps.userId == user->id) {
                myTimes++;
                if (ps.total > myBest) myBest = ps.total;
            }
        }
    }
    int subs = 0;
    for (size_t i = 0; i < DataStore::instance().paperSubs.size(); i++)
        if (DataStore::instance().paperSubs[i].paperId == p.id) subs++;

    std::string h = "<div class='card'><div class='card-title'><a href='/paper/" + p.id + "'>" + esc(p.title) + "</a>";
    if (p.hidden) h += " <span class='pill' style='background:#fdecea;color:#c0392b;'>未发布</span>";
    if (p.timeLimit > 0) h += " <span class='pill' style='background:#eef0fb;color:#4b4ea8;'>限时 " + std::to_string(p.timeLimit) + " 分钟</span>";
    h += "</div>";
    h += "<div class='card-meta'>共 " + std::to_string(p.questions.size()) + " 题 · 满分 " + std::to_string(full) +
         " 分 · " + std::to_string(subs) + " 人作答" + (p.createTime.empty() ? "" : " · " + p.createTime) + "</div>";
    if (!p.description.empty()) {
        std::string d = p.description;
        if (d.size() > 160) d = d.substr(0, 160) + "...";
        h += "<div style='margin-top:8px;color:#555;'>" + esc(d) + "</div>";
    }
    h += "<div style='margin-top:12px;display:flex;gap:8px;flex-wrap:wrap;align-items:center;'>";
    h += "<a class='btn btn-sm' href='/paper/" + p.id + "'>开始作答</a>";
    h += "<a class='btn btn-sm btn-outline' href='/paper/" + p.id + "/subs'>成绩榜</a>";
    if (myBest >= 0) {
        h += "<span style='color:#27ae60;font-weight:600;'>我的最好成绩 " + std::to_string(myBest) + " / " + std::to_string(full) + "</span>";
    } else if (user) {
        h += "<span style='color:#95a5a6;'>还没做过</span>";
    }
    h += "</div></div>";
    return h;
}

// ---------- /papers 列表 ----------
std::string handlePapers(const std::string& method, const std::string& path, const std::string& body,
                         const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    std::string kw = common::trim(queryOf(path, "q"));

    std::string content;
    content += "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'>选择题与填空题，交卷自动批改，附逐题解析</p>";
    content += "<div style='display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px;'>";
    if (Handlers::isAdmin(user)) content += "<a class='btn' href='/admin/papers'>⚙ 试卷管理</a>";
    content += "<form action='/papers' method='get' style='display:flex;gap:8px;flex:1;min-width:220px;'>"
               "<input type='text' name='q' value='" + esc(kw) + "' placeholder='搜索试卷…' style='flex:1;'/>"
               "<button class='btn' type='submit'>搜索</button></form></div>";

    int n = 0;
    for (size_t i = DataStore::instance().papers.size(); i > 0; i--) {
        Paper& p = DataStore::instance().papers[i - 1];
        if (p.hidden && !Handlers::isAdmin(user)) continue;
        if (!kw.empty()) {
            std::string hay = p.title + " " + p.description;
            if (hay.find(kw) == std::string::npos) continue;
        }
        content += paperCard(p, user);
        n++;
    }
    if (n == 0) content += "<div class='empty-state'><span class='icon'>📝</span>暂无试卷</div>";
    return Handlers::renderTemplate("试卷", content, user);
}

// ---------- /paper/<id> 作答页 ----------
std::string handlePaperDetail(const std::string& method, const std::string& path, const std::string& body,
                              const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    std::string rest = path.substr(7); // after /paper/
    std::string id = rest;
    std::string tail;
    size_t sl = rest.find('/');
    if (sl != std::string::npos) { id = rest.substr(0, sl); tail = rest.substr(sl + 1); }

    Paper* p = DataStore::instance().findPaperById(id);
    if (!p) return Handlers::renderTemplate("试卷", "<div class='alert alert-error'>试卷不存在</div><a class='btn' href='/papers'>返回</a>", user);
    if (p->hidden && !Handlers::isAdmin(user))
        return Handlers::renderTemplate("试卷", "<div class='alert alert-warning'>该试卷暂未发布</div><a class='btn' href='/papers'>返回</a>", user);

    if (tail == "subs") {
        // 成绩榜：每人取最高分
        std::map<std::string, PaperSubmission> best;
        for (size_t i = 0; i < DataStore::instance().paperSubs.size(); i++) {
            const PaperSubmission& ps = DataStore::instance().paperSubs[i];
            if (ps.paperId != id) continue;
            auto it = best.find(ps.userId);
            if (it == best.end() || ps.total > it->second.total) best[ps.userId] = ps;
        }
        std::vector<PaperSubmission> rank;
        for (auto it = best.begin(); it != best.end(); ++it) rank.push_back(it->second);
        std::sort(rank.begin(), rank.end(), [](const PaperSubmission& a, const PaperSubmission& b) {
            if (a.total != b.total) return a.total > b.total;
            return a.time < b.time;
        });
        int full = paperFullScore(*p);
        std::string c = "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'>" + esc(p->title) + " · 成绩榜</p>";
        c += "<div class='card'><table style='width:100%;'><thead><tr><th>名次</th><th>用户</th><th>得分</th><th>交卷时间</th></tr></thead><tbody>";
        for (size_t i = 0; i < rank.size(); i++) {
            std::string medal = (i == 0) ? "🥇" : (i == 1) ? "🥈" : (i == 2) ? "🥉" : std::to_string(i + 1);
            c += "<tr><td>" + medal + "</td><td><a href='/user/" + rank[i].userId + "'>" + esc(rank[i].username) + "</a></td>"
                 "<td><b style='color:#3a86c8;'>" + std::to_string(rank[i].total) + "</b> / " + std::to_string(full) + "</td>"
                 "<td>" + rank[i].time + "</td></tr>";
        }
        c += "</tbody></table>";
        if (rank.empty()) c += "<div class='empty-td'>还没有人提交</div>";
        c += "</div><div style='margin-top:14px;'><a class='btn' href='/paper/" + id + "'>去做题</a> <a class='btn btn-outline' href='/papers'>返回试卷列表</a></div>";
        return Handlers::renderTemplate(p->title + " · 成绩榜", c, user);
    }

    if (tail.compare(0, 6, "result") == 0) {
        std::string subId = tail.size() > 7 ? tail.substr(7) : "";
        PaperSubmission* found = nullptr;
        for (size_t i = 0; i < DataStore::instance().paperSubs.size(); i++) {
            PaperSubmission& ps = DataStore::instance().paperSubs[i];
            if (ps.id == subId && ps.paperId == id) { found = &ps; break; }
        }
        if (!found || (!user || (found->userId != user->id && !Handlers::isAdmin(user))))
            return Handlers::renderTemplate("试卷", "<div class='alert alert-error'>成绩不存在</div><a class='btn' href='/papers'>返回</a>", user);
        int full = paperFullScore(*p);
        std::string c = "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'>" + esc(p->title) + " · 交卷结果</p>";
        int pct = full > 0 ? (int)((long long)found->total * 100 / full) : 0;
        c += "<div class='card' style='text-align:center;padding:26px;'>"
             "<div style='font-size:44px;font-weight:800;color:" + std::string(pct >= 60 ? "#27ae60" : "#e74c3c") + ";'>" +
             std::to_string(found->total) + " <span style='font-size:20px;color:#95a5a6;'>/ " + std::to_string(full) + "</span></div>"
             "<div style='color:#7f8c8d;margin-top:6px;'>得分率 " + std::to_string(pct) + "% · 用时记录 " + found->time + "</div></div>";
        for (size_t i = 0; i < p->questions.size(); i++) {
            const PaperQuestion& q = p->questions[i];
            std::string ua = (i < found->answers.size()) ? found->answers[i] : "";
            int got = (i < found->scores.size()) ? found->scores[i] : 0;
            bool ok = got >= q.score && q.score > 0;
            c += "<div class='card' style='border-left:4px solid " + std::string(ok ? "#27ae60" : "#e74c3c") + ";'>";
            c += "<div class='card-title'>第 " + std::to_string(i + 1) + " 题 <span class='pill' style='background:" +
                 std::string(ok ? "#e8f8f0;#27ae60" : "#fdecea;#c0392b") + ";'>" + (ok ? "✔ 得 " : "✘ 得 ") +
                 std::to_string(got) + " / " + std::to_string(q.score) + " 分</span></div>";
            c += "<div class='md-body' style='margin:8px 0;'>" + common::markdownToHtml(q.stem) + "</div>";
            if (q.type == "choice") {
                c += "<div style='color:#27ae60;'>正确答案：<b>" + esc(q.answer) + "</b></div>";
                c += "<div style='color:" + std::string(ok ? "#27ae60" : "#e74c3c") + ";'>你的答案：<b>" + esc(ua.empty() ? "（未答）" : ua) + "</b></div>";
            } else {
                c += "<div style='color:#27ae60;'>参考答案：<b>" + esc(q.answer) + "</b></div>";
                c += "<div style='color:" + std::string(ok ? "#27ae60" : "#e74c3c") + ";'>你的答案：<b>" + esc(ua.empty() ? "（未答）" : ua) + "</b></div>";
            }
            if (!q.analysis.empty())
                c += "<div class='alert alert-info' style='margin-top:10px;'><b>解析：</b><div class='md-body' style='display:inline-block;'>" + common::markdownToHtml(q.analysis) + "</div></div>";
            c += "</div>";
        }
        c += "<div style='margin-top:16px;'><a class='btn' href='/paper/" + id + "'>再做一次</a> "
             "<a class='btn btn-outline' href='/paper/" + id + "/subs'>成绩榜</a> "
             "<a class='btn btn-outline' href='/papers'>返回试卷列表</a></div>";
        return Handlers::renderTemplate("交卷结果", c, user);
    }

    // 作答页
    if (!user) return "<script>window.location.href='/login'</script>";
    int full = paperFullScore(*p);
    std::string c = "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'>共 " +
                    std::to_string(p->questions.size()) + " 题 · 满分 " + std::to_string(full) + " 分" +
                    (p->timeLimit > 0 ? (" · 建议限时 " + std::to_string(p->timeLimit) + " 分钟") : "") + "</p>";
    if (!p->description.empty())
        c += "<div class='card'><div class='md-body'>" + common::markdownToHtml(p->description) + "</div></div>";
    c += "<form method='POST' action='/paper/" + id + "/submit'>";
    for (size_t i = 0; i < p->questions.size(); i++) {
        const PaperQuestion& q = p->questions[i];
        std::string nm = "q" + std::to_string(i);
        c += "<div class='card' id='q" + std::to_string(i) + "'>";
        c += "<div class='card-title'>第 " + std::to_string(i + 1) + " 题 <span class='pill' style='background:#eef0fb;color:#4b4ea8;'>" +
             std::string(q.type == "fill" ? "填空" : "选择") + " · " + std::to_string(q.score) + " 分</span></div>";
        c += "<div class='md-body' style='margin:8px 0 12px;'>" + common::markdownToHtml(q.stem) + "</div>";
        if (q.type == "fill") {
            int blanks = fillBlanks(q.answer);
            for (int b = 0; b < blanks; b++) {
                c += "<div style='display:flex;gap:8px;align-items:center;margin:8px 0;'>"
                     "<span style='color:#7f8c8d;min-width:46px;'>第 " + std::to_string(b + 1) + " 空</span>"
                     "<input type='text' name='" + nm + "' value='' style='flex:1;' autocomplete='off'/></div>";
            }
            c += "<div style='color:#95a5a6;font-size:12.5px;'>多个空请分别填写；填空不区分大小写与空格</div>";
        } else {
            const char* letters = "ABCDEFGH";
            for (size_t o = 0; o < q.options.size(); o++) {
                std::string inputType = q.multi ? "checkbox" : "radio";
                c += "<label style='display:block;margin:6px 0;padding:6px 10px;border:1px solid #e3e8ee;border-radius:8px;cursor:pointer;'>"
                     "<input type='" + inputType + "' name='" + nm + "' value='" + std::string(1, letters[o < 8 ? o : 7]) + "' style='margin-right:8px;'/>"
                     "<b>" + std::string(1, letters[o < 8 ? o : 7]) + ".</b> " + esc(q.options[o]) + "</label>";
            }
            if (q.multi) c += "<div style='color:#95a5a6;font-size:12.5px;'>本题为多选，可选多个选项</div>";
        }
        c += "</div>";
    }
    c += "<div style='position:sticky;bottom:0;background:var(--surface);padding:12px;border-radius:12px;display:flex;gap:10px;'>"
         "<button class='btn' type='submit'>交卷并自动批改</button>"
         "<a class='btn btn-outline' href='/papers'>放弃</a></div>";
    c += "</form>";
    return Handlers::renderTemplate(p->title, c, user);
}

// ---------- POST /paper/<id>/submit ----------
std::string handlePaperSubmit(const std::string& method, const std::string& path, const std::string& body,
                              const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string rest = path.substr(7);
    size_t sl = rest.find('/');
    std::string id = (sl == std::string::npos) ? rest : rest.substr(0, sl);
    Paper* p = DataStore::instance().findPaperById(id);
    if (!p) return "<script>window.location.href='/papers'</script>";

    PaperSubmission ps;
    ps.id = common::genId();
    ps.paperId = id;
    ps.userId = user->id;
    ps.username = user->username;
    ps.time = common::getTimeStr();
    ps.fullScore = paperFullScore(*p);

    for (size_t i = 0; i < p->questions.size(); i++) {
        const PaperQuestion& q = p->questions[i];
        std::string nm = "q" + std::to_string(i);
        std::string ans;
        if (q.type == "fill") {
            std::vector<std::string> vals = formAll(body, nm);
            for (size_t k = 0; k < vals.size(); k++) {
                if (k) ans += "|";
                ans += common::trim(vals[k]);
            }
        } else {
            std::vector<std::string> vals = formAll(body, nm);
            std::string letters;
            for (size_t k = 0; k < vals.size(); k++) letters += common::trim(vals[k]);
            ans = normChoice(letters);
        }
        int got = gradeQuestion(q, ans);
        ps.answers.push_back(ans);
        ps.scores.push_back(got);
        ps.total += got;
    }
    DataStore::instance().paperSubs.push_back(ps);
    DataStore::instance().savePaperSubs();
    if (ps.total >= ps.fullScore && ps.fullScore > 0) {
        DataStore::instance().addExperience(user->id, 30);
        DataStore::instance().addGold(user->id, 20);
    }
    return "<script>window.location.href='/paper/" + id + "/result/" + ps.id + "'</script>";
}

// ---------- 管理：试卷列表 ----------
std::string handleAdminPapers(const std::string& method, const std::string& path, const std::string& body,
                              const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!Handlers::isAdmin(user)) return "<script>window.location.href='/'</script>";

    std::string c = "<div class='action-bar' style='display:flex;justify-content:flex-end;align-items:center;margin-bottom:10px;'>"
                    "<a class='btn' href='/admin/paper/add'>＋ 新建试卷</a></div>";
    c += "<div class='card'><table style='width:100%;'><thead><tr><th>标题</th><th>题数</th><th>满分</th><th>作答数</th><th>状态</th><th>操作</th></tr></thead><tbody>";
    for (size_t i = 0; i < DataStore::instance().papers.size(); i++) {
        Paper& p = DataStore::instance().papers[i];
        int subs = 0;
        for (size_t k = 0; k < DataStore::instance().paperSubs.size(); k++)
            if (DataStore::instance().paperSubs[k].paperId == p.id) subs++;
        c += "<tr><td><a href='/paper/" + p.id + "'>" + esc(p.title) + "</a></td>"
             "<td>" + std::to_string(p.questions.size()) + "</td><td>" + std::to_string(paperFullScore(p)) + "</td>"
             "<td>" + std::to_string(subs) + "</td>"
             "<td>" + std::string(p.hidden ? "<span style='color:#c0392b;'>未发布</span>" : "<span style='color:#27ae60;'>已发布</span>") + "</td>"
             "<td style='white-space:nowrap;'>"
             "<a class='btn btn-sm' href='/admin/paper/edit/" + p.id + "'>编辑</a> "
             "<form method='POST' action='/admin/paper/toggle' style='display:inline;'><input type='hidden' name='id' value='" + p.id + "'/>"
             "<input type='submit' class='btn btn-sm btn-outline' value='" + std::string(p.hidden ? "发布" : "下架") + "'/></form> "
             "<form method='POST' action='/admin/paper/delete' style='display:inline;' onsubmit=\"return confirm('确认删除该试卷？')\">"
             "<input type='hidden' name='id' value='" + p.id + "'/>"
             "<input type='submit' class='btn btn-sm btn-danger' value='删除'/></form></td></tr>";
    }
    c += "</tbody></table>";
    if (DataStore::instance().papers.empty()) c += "<div class='empty-td'>还没有试卷</div>";
    c += "</div>";
    return Handlers::renderTemplate("试卷管理", c, user);
}

// 试卷编辑表单（新建 / 编辑共用）
std::string paperEditor(Paper* p, User* user, const std::string& actionUrl) {
    const int MAXQ = 30;
    std::string c = "<div class='action-bar' style='display:flex;justify-content:space-between;align-items:center;'>"
                    "<h2>" + std::string(p ? "编辑试卷" : "新建试卷") + "</h2><a class='btn btn-outline' href='/admin/papers'>返回列表</a></div>";
    c += "<form method='POST' action='" + actionUrl + "' id='paperForm'>";
    c += "<div class='card'><div class='form-group'><label>试卷标题</label><input name='title' required value='" +
         esc(p ? p->title : "") + "'/></div>";
    c += "<div class='form-group'><label>说明（支持 Markdown，可空）</label><textarea name='description' rows='3'>" +
         esc(p ? p->description : "") + "</textarea></div>";
    c += "<div style='display:flex;gap:12px;flex-wrap:wrap;'>"
         "<div class='form-group' style='flex:1;min-width:150px;'><label>限时（分钟，0=不限）</label><input name='timeLimit' type='number' min='0' value='" +
         std::to_string(p ? p->timeLimit : 0) + "'/></div>"
         "<div class='form-group' style='flex:1;min-width:150px;'><label>状态</label><select name='hidden'>"
         "<option value='0'" + std::string(!p || !p->hidden ? " selected" : "") + ">发布</option>"
         "<option value='1'" + std::string(p && p->hidden ? " selected" : "") + ">保存为未发布</option></select></div></div></div>";

    int existing = p ? (int)p->questions.size() : 0;
    int visible = existing + 1;
    if (visible < 3) visible = 3;
    c += "<div class='card'><div style='display:flex;justify-content:space-between;align-items:center;'>"
         "<h3 style='margin:0;'>题目</h3><div>"
         "<button type='button' class='btn btn-sm' onclick='moreQ(3)'>＋ 再加 3 题</button></div></div>";
    c += "<p class='card-meta'>选择题的选项每行一个（A/B/C/D 自动编号）；填空题的答案用 | 分隔多个空，同一空多个可接受答案用 , 分隔。</p>";
    for (int i = 0; i < MAXQ; i++) {
        PaperQuestion q;
        if (i < existing) q = p->questions[i];
        std::string opts;
        for (size_t k = 0; k < q.options.size(); k++) opts += (k ? "\n" : "") + q.options[k];
        std::string style = (i < visible) ? "" : "display:none;";
        c += "<div class='qslot' id='slot" + std::to_string(i) + "' style='" + style + "border:1px solid #e3e8ee;border-radius:10px;padding:12px;margin:10px 0;'>";
        c += "<div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:8px;'>"
             "<b>第 " + std::to_string(i + 1) + " 题</b>"
             "<select name='q" + std::to_string(i) + "_type' style='width:110px;'>"
             "<option value='choice'" + std::string(q.type != "fill" ? " selected" : "") + ">选择题</option>"
             "<option value='fill'" + std::string(q.type == "fill" ? " selected" : "") + ">填空题</option></select>"
             "<input name='q" + std::to_string(i) + "_score' type='number' min='1' value='" + std::to_string(q.score > 0 ? q.score : 5) + "' style='width:80px;' title='分值'/>"
             "<label style='font-size:13px;'><input type='checkbox' name='q" + std::to_string(i) + "_multi' value='1'" +
             std::string(q.multi ? " checked" : "") + "/> 多选</label></div>";
        c += "<div class='form-group'><label>题干</label><textarea name='q" + std::to_string(i) + "_stem' rows='2' placeholder='支持 Markdown / LaTeX 如 $O(n\\log n)$'>" +
             esc(q.stem) + "</textarea></div>";
        c += "<div class='form-group'><label>选项（每行一个，选择题填）</label><textarea name='q" + std::to_string(i) +
             "_options' rows='4' placeholder='选项 A\n选项 B\n选项 C\n选项 D'>" + esc(opts) + "</textarea></div>";
        c += "<div class='form-group'><label>答案</label><input name='q" + std::to_string(i) + "_answer' value='" + esc(q.answer) +
             "' placeholder='选择题: A 或 AB ；填空题: 15|O(nlogn)'/></div>";
        c += "<div class='form-group'><label>解析（可空，交卷后展示）</label><textarea name='q" + std::to_string(i) + "_analysis' rows='2'>" +
             esc(q.analysis) + "</textarea></div></div>";
    }
    c += "</div>";
    c += "<div style='display:flex;gap:10px;margin:16px 0;'><button class='btn' type='submit'>保存试卷</button>"
         "<a class='btn btn-outline' href='/admin/papers'>取消</a></div></form>";
    c += "<script>var VIS=" + std::to_string(visible) + ",MAXQ=" + std::to_string(MAXQ) + ";"
         "function moreQ(n){for(var i=VIS;i<Math.min(VIS+n,MAXQ);i++){document.getElementById('slot'+i).style.display='';}VIS=Math.min(VIS+n,MAXQ);}</script>";
    return c;
}

// 从表单读一份试卷
Paper paperFromForm(const std::string& body, Paper base) {
    base.title = common::trim(Handlers::parseFormData(body, "title"));
    base.description = Handlers::parseFormData(body, "description");
    base.timeLimit = common::stringToInt(Handlers::parseFormData(body, "timeLimit"));
    base.hidden = (Handlers::parseFormData(body, "hidden") == "1");
    base.questions.clear();
    for (int i = 0; i < 30; i++) {
        std::string pfx = "q" + std::to_string(i) + "_";
        PaperQuestion q;
        q.type = Handlers::parseFormData(body, pfx + "type");
        if (q.type != "fill") q.type = "choice";
        q.stem = common::trim(Handlers::parseFormData(body, pfx + "stem"));
        if (q.stem.empty()) continue;
        q.score = common::stringToInt(Handlers::parseFormData(body, pfx + "score"));
        if (q.score <= 0) q.score = 5;
        q.multi = !Handlers::parseFormData(body, pfx + "multi").empty();
        q.answer = common::trim(Handlers::parseFormData(body, pfx + "answer"));
        q.analysis = Handlers::parseFormData(body, pfx + "analysis");
        std::string opts = Handlers::parseFormData(body, pfx + "options");
        auto lines = common::split(opts, '\n');
        for (size_t k = 0; k < lines.size(); k++) {
            std::string o = common::trim(lines[k]);
            if (!o.empty()) q.options.push_back(o);
        }
        if (q.type == "choice" && q.options.empty()) continue;
        base.questions.push_back(q);
    }
    return base;
}

std::string handleAdminPaperEdit(const std::string& method, const std::string& path, const std::string& body,
                                 const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!Handlers::isAdmin(user)) return "<script>window.location.href='/'</script>";
    bool isNew = (path.find("/admin/paper/add") == 0);
    std::string id;
    if (!isNew) {
        size_t sl = path.find_last_of('/');
        if (sl != std::string::npos) id = path.substr(sl + 1);
    }
    Paper* p = isNew ? nullptr : DataStore::instance().findPaperById(id);
    if (!isNew && !p) return "<script>window.location.href='/admin/papers'</script>";

    if (method == "POST") {
        Paper np = p ? *p : Paper();
        if (!p) { np.id = common::genId(); np.authorId = user->id; np.createTime = common::getTimeStr(); }
        np = paperFromForm(body, np);
        if (np.title.empty()) np.title = "未命名试卷";
        if (p) {
            *p = np;
        } else {
            DataStore::instance().papers.push_back(np);
        }
        DataStore::instance().savePapers();
        return "<script>window.location.href='/admin/papers'</script>";
    }
    std::string content = paperEditor(p, user, isNew ? "/admin/paper/add" : ("/admin/paper/edit/" + id));
    return Handlers::renderTemplate(p ? ("编辑 " + p->title) : std::string("新建试卷"), content, user);
}

std::string handleAdminPaperOp(const std::string& method, const std::string& path, const std::string& body,
                               const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!Handlers::isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string id = Handlers::parseFormData(body, "id");
    if (path.find("/toggle") != std::string::npos) {
        Paper* p = DataStore::instance().findPaperById(id);
        if (p) { p->hidden = !p->hidden; DataStore::instance().savePapers(); }
    } else if (path.find("/delete") != std::string::npos) {
        std::vector<Paper> keep;
        for (size_t i = 0; i < DataStore::instance().papers.size(); i++)
            if (DataStore::instance().papers[i].id != id) keep.push_back(DataStore::instance().papers[i]);
        DataStore::instance().papers = keep;
        std::vector<PaperSubmission> keepSubs;
        for (size_t i = 0; i < DataStore::instance().paperSubs.size(); i++)
            if (DataStore::instance().paperSubs[i].paperId != id) keepSubs.push_back(DataStore::instance().paperSubs[i]);
        DataStore::instance().paperSubs = keepSubs;
        DataStore::instance().savePapers();
        DataStore::instance().savePaperSubs();
    }
    return "<script>window.location.href='/admin/papers'</script>";
}

} // namespace

void registerPaperRoutes(Router& router) {
    router.get("/papers", handlePapers);
    router.post("/paper/*/submit", handlePaperSubmit);
    router.get("/paper/*", handlePaperDetail);
    router.post("/paper/*", handlePaperDetail);
    router.get("/admin/papers", handleAdminPapers);
    router.get("/admin/paper/add", handleAdminPaperEdit);
    router.post("/admin/paper/add", handleAdminPaperEdit);
    router.get("/admin/paper/edit/*", handleAdminPaperEdit);
    router.post("/admin/paper/edit/*", handleAdminPaperEdit);
    router.post("/admin/paper/toggle", handleAdminPaperOp);
    router.post("/admin/paper/delete", handleAdminPaperOp);
}

const fs = require('fs');
const path = require('path');

const base = path.join(__dirname, '..');

function load(filename) {
  let src = fs.readFileSync(path.join(base, filename), 'latin1');
  src = src.replace(/\r\n/g, '\n'); // 统一为 LF，便于匹配
  return src;
}
function save(filename, src) {
  fs.writeFileSync(path.join(base, filename), src, 'latin1');
}

function patchBlock(src, name, start, end, text) {
  const s = src.indexOf(start);
  if (s === -1) { throw new Error('START not found [' + name + ']: ' + start.slice(0, 60)); }
  const e = src.indexOf(end, s);
  if (e === -1) { throw new Error('END not found [' + name + ']: ' + end.slice(0, 60)); }
  console.log('OK patch [' + name + ']');
  return src.slice(0, s) + text + src.slice(e);
}

// 替换一个 “Message m; ... ds.saveMessages();” 消息块为一行系统通知调用
function replaceMsgBlock(src, name, contentAnchor, newLine) {
  const cIdx = src.indexOf(contentAnchor);
  if (cIdx === -1) { throw new Error('CONTENT not found [' + name + ']'); }
  const start = src.lastIndexOf('    Message m;\n', cIdx);
  if (start === -1) { throw new Error('MSG start not found [' + name + ']'); }
  const endRel = src.indexOf('    ds.saveMessages();', cIdx);
  if (endRel === -1) { throw new Error('SAVE end not found [' + name + ']'); }
  const end = endRel + '    ds.saveMessages();'.length;
  console.log('OK msg->notif [' + name + ']');
  return src.slice(0, start) + newLine + src.slice(end);
}

// ================= models.cpp =================
{
  let src = load('src/models.cpp');

  const taskToJson = String.raw`std::string DataStore::taskToJson(const TaskItem& t) {
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

`;
  src = patchBlock(src, 'models taskToJson',
    'std::string DataStore::taskToJson(const TaskItem& t) {',
    'TaskItem DataStore::taskFromJson(const std::string& json) {',
    taskToJson);

  const taskFromJson = String.raw`TaskItem DataStore::taskFromJson(const std::string& json) {
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

`;
  src = patchBlock(src, 'models taskFromJson',
    'TaskItem DataStore::taskFromJson(const std::string& json) {',
    'TaskItem* DataStore::findTaskById(const std::string& id) {',
    taskFromJson);

  save('src/models.cpp', src);
}

// ================= handlers.h =================
{
  let src = load('include/handlers.h');

  const decSubmit = String.raw`    static std::string handleTaskSubmit(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
`;
  src = patchBlock(src, 'hdr submit',
    '    static std::string handleTaskComplete(const std::string& method, const std::string& path,',
    '    static std::string handleTaskConfirm(const std::string& method, const std::string& path,',
    decSubmit);

  const decReject = String.raw`    static std::string handleTaskConfirm(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    static std::string handleTaskReject(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
`;
  src = patchBlock(src, 'hdr reject',
    '    static std::string handleTaskConfirm(const std::string& method, const std::string& path,',
    '    static std::string handleTaskCancel(const std::string& method, const std::string& path,',
    decReject);

  const decClose = String.raw`    static std::string handleTaskClose(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    static std::string handleTaskMsg(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
`;
  src = patchBlock(src, 'hdr close+msg',
    '    static std::string handleTaskCancel(const std::string& method, const std::string& path,',
    '    static std::string handleAdminSendGold(',
    decClose);

  save('include/handlers.h', src);
}

// ================= handlers.cpp =================
{
  let src = load('src/handlers.cpp');

  // 1) 消息区：聊天框
  const messages = String.raw`// ============================================================
// 私信 / 系统通知（聊天框）
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

static std::string chatBubble(const std::string& t, const std::string& c, bool mine) {
    std::string out = "<div class='bubble " + std::string(mine ? "mine" : "theirs") + "'>";
    out += common::htmlEscape(c);
    out += "<div class='bubble-time'>" + t + "</div></div>";
    return out;
}

std::string Handlers::handleMessages(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    DataStore& ds = DataStore::instance();

    std::string active;
    if (path == "/messages" || path == "/messages/") active = kSystemId;
    else if (path == "/messages/system") active = kSystemId;
    else if (path.rfind("/messages/chat/", 0) == 0) active = path.substr(std::string("/messages/chat/").size());

    int sysUnread = 0;
    std::map<std::string, int> convUnread;
    std::map<std::string, std::string> partnerName;
    for (auto& m : ds.messages) {
        if (m.toId != user->id) continue;
        if (m.fromId == kSystemId) { if (!m.read) sysUnread++; }
        else { if (!m.read) convUnread[m.fromId]++; partnerName[m.fromId] = ""; }
    }
    for (auto& m : ds.messages) {
        if (m.fromId == user->id && !m.toId.empty() && m.toId != kSystemId) partnerName[m.toId] = "";
    }
    for (auto& kv : partnerName) {
        User* p = ds.findUserById(kv.first);
        kv.second = p ? common::htmlEscape(p->username) : "\u5df2\u6ce8\u9500";
    }
    std::map<std::string, std::string> lastTime;
    for (auto& m : ds.messages) {
        if (m.fromId == user->id && !m.toId.empty() && m.toId != kSystemId) lastTime[m.toId] = m.sendTime;
        else if (m.toId == user->id && m.fromId != kSystemId) lastTime[m.fromId] = m.sendTime;
    }
    std::vector<std::pair<std::string, std::string>> order;
    for (auto& kv : lastTime) order.push_back(kv);
    std::sort(order.begin(), order.end(), [](const std::pair<std::string,std::string>& a, const std::pair<std::string,std::string>& b){ return a.second > b.second; });

    std::string sb = "<a class='conv" + std::string(active == kSystemId ? " active" : "") + "' href='/messages/system'>"
        "<div class='conv-name'>\U0001f4e2 \u7cfb\u7edf\u901a\u77e5</div>";
    if (sysUnread > 0) sb += "<span class='conv-badge'>" + std::to_string(sysUnread) + "</span>";
    sb += "</a>";
    for (auto& kv : order) {
        const std::string& pid = kv.first;
        int un = convUnread.count(pid) ? convUnread[pid] : 0;
        std::string nm = partnerName.count(pid) ? partnerName[pid] : pid;
        std::string nm2 = nm; if (nm2.size() > 8) nm2 = nm2.substr(0, 8);
        sb += "<a class='conv" + std::string(active == pid ? " active" : "") + "' href='/messages/chat/" + pid + "'>"
            "<div class='conv-name'>" + nm2 + "</div>";
        if (un > 0) sb += "<span class='conv-badge'>" + std::to_string(un) + "</span>";
        sb += "</a>";
    }

    std::string title, bubbles;
    bool showInput = false;
    if (active == kSystemId) {
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
        title = p ? common::htmlEscape(p->username) : "\u5df2\u6ce8\u9500";
        bool changed = false;
        for (auto& m : ds.messages) {
            bool rel = (m.fromId == user->id && m.toId == active) || (m.fromId == active && m.toId == user->id);
            if (!rel) continue;
            bool mine = (m.fromId == user->id);
            if (!mine && !m.read) { m.read = true; changed = true; }
            bubbles += chatBubble(m.sendTime, m.content, mine);
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
    content += "<div class='chat-header'>" + title + "</div>";
    content += "<div class='chat-body'>" + bubbles + "</div>";
    if (showInput) {
        content += "<form class='chat-input' method='POST' action='/message/send'>"
                   "<input type='hidden' name='toUser' value='" + active + "'/>"
                   "<input type='text' name='content' placeholder='\u8f93\u5165\u6d88\u606f\u2026' autocomplete='off' required/>"
                   "<button class='btn'>\u53d1\u9001</button></form>";
    } else {
        content += "<div class='chat-input' style='justify-content:center;color:#999;font-size:13px;'>\u7cfb\u7edf\u901a\u77e5\u4ec5\u53ef\u67e5\u770b</div>";
    }
    content += "</div></div>";

    content += "<style>"
        ".chat-layout{display:flex;height:70vh;border:1px solid #e3e8ee;border-radius:10px;overflow:hidden;background:#fff;}"
        ".chat-sidebar{width:230px;border-right:1px solid #eee;background:#fafbfc;overflow-y:auto;flex-shrink:0;}"
        ".conv{display:flex;justify-content:space-between;align-items:center;padding:12px 14px;color:#2c3e50;text-decoration:none;border-bottom:1px solid #f0f0f0;}"
        ".conv.active{background:#e8f0fe;font-weight:600;}"
        ".conv-name{font-size:14px;}"
        ".conv-badge{background:#e74c3c;color:#fff;border-radius:50%;padding:1px 7px;font-size:12px;}"
        ".chat-main{flex:1;display:flex;flex-direction:column;min-width:0;}"
        ".chat-header{padding:12px 16px;border-bottom:1px solid #eee;font-weight:600;background:#fff;}"
        ".chat-body{flex:1;overflow-y:auto;padding:14px;background:#f7f9fb;}"
        ".bubble{max-width:70%;margin-bottom:10px;padding:8px 12px;border-radius:12px;font-size:14px;line-height:1.5;white-space:pre-wrap;word-break:break-word;}"
        ".bubble.mine{margin-left:auto;background:#667eea;color:#fff;border-bottom-right-radius:2px;}"
        ".bubble.theirs{background:#fff;color:#2c3e50;border:1px solid #e8ecf1;border-bottom-left-radius:2px;}"
        ".bubble-time{font-size:11px;color:#b8c0cc;margin-top:3px;}"
        ".bubble.mine .bubble-time{color:rgba(255,255,255,.75);text-align:right;}"
        ".chat-input{display:flex;gap:8px;padding:10px 12px;border-top:1px solid #eee;background:#fff;}"
        ".chat-input input[type=text]{flex:1;border:1px solid #e0e5ec;border-radius:8px;padding:8px 12px;font-size:14px;}"
        ".chat-empty{text-align:center;color:#a0a8b4;padding:40px 0;}"
        "</style>";

    return renderTemplate("\u79c1\u4fe1", content, user);
}

std::string Handlers::handleSendMessage(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    DataStore& ds = DataStore::instance();
    std::string toUser = parseFormData(body, "toUser");
    std::string content = parseFormData(body, "content");
    if (toUser.empty() || content.empty()) {
        return "<script>window.location.href='/messages'</script>";
    }
    User* toObj = ds.findUserByName(toUser);
    if (!toObj) toObj = ds.findUserById(toUser);
    if (!toObj) {
        return "<script>alert('\u7528\u6237\u4e0d\u5b58\u5728');window.location.href='/messages'</script>";
    }
    Message msg;
    msg.id = common::genId();
    msg.fromId = user->id;
    msg.toId = toObj->id;
    msg.content = content;
    msg.sendTime = common::getTimeStr();
    msg.read = false;
    ds.messages.push_back(msg);
    ds.saveMessages();
    return "<script>window.location.href='/messages/chat/" + toObj->id + "'</script>";
}

std::string Handlers::handleDeleteMessage(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string msgId = parseFormData(body, "messageId");
    if (msgId.empty()) {
        size_t lastSlash = path.find_last_of('/');
        msgId = path.substr(lastSlash + 1);
    }
    auto& messages = DataStore::instance().messages;
    for (auto it = messages.begin(); it != messages.end(); ++it) {
        if (it->id == msgId && it->toId == user->id) {
            messages.erase(it);
            DataStore::instance().saveMessages();
            break;
        }
    }
    return "<script>window.location.href='/messages'</script>";
}

`;
  src = patchBlock(src, 'messages chatbox',
    'std::string Handlers::handleMessages(',
    'std::string Handlers::handleUserProfile(',
    messages);

  // 2) 商城购买 -> 系统通知
  src = replaceMsgBlock(src, 'shop buy notif',
    '\\u8d2d\\u4e70\\u6210\\u529f\\uff1a',
    '    pushSystemNotification(user->id, "\\u8d2d\\u4e70\\u6210\\u529f\\uff1a" + item->icon + " " + item->name + "\\uff08\\u4ed8\\u51fa " + std::to_string(item->price) + " \\u91d1\\u5e01\\uff09");');

  // 3) 管理员发金币 -> 系统通知
  src = replaceMsgBlock(src, 'admin gold notif',
    '\\u7ba1\\u7406\\u5458\\u5411\\u4f60\\u53d1\\u653e\\u4e86',
    '    pushSystemNotification(target->id, "\\U0001fa99 \\u7ba1\\u7406\\u5458\\u5411\\u4f60\\u53d1\\u653e\\u4e86 " + std::to_string(amount) + " \\u91d1\\u5e01" + (note.empty() ? "" : "\\uff08" + note + "\\uff09"));');

  // 4) 管理员调 Token -> 系统通知
  src = replaceMsgBlock(src, 'admin token notif',
    '\\u7ba1\\u7406\\u5458\\u4e3a\\u4f60\\u8c03\\u6574\\u4e86',
    '    pushSystemNotification(target->id, "\\u2699\\ufe0f \\u7ba1\\u7406\\u5458\\u4e3a\\u4f60\\u8c03\\u6574\\u4e86 Token \\u6570\\u91cf\\uff08\\u65b0\\u4f59\\u989d " + std::to_string(after) + "\\uff09");');

  // 5) 任务区整块替换
  const tasks = String.raw`static std::string taskStatusText(int st) {
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
    std::string colOngoing, colOpen, colClosed;
    bool hasAny = false;
    for (auto it = tasks.rbegin(); it != tasks.rend(); ++it) {
        const TaskItem& t = *it;
        if (t.status == TASK_DONE) continue;
        hasAny = true;
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
                             "<input type='submit' value='\u53d1\u5e03\u4efb\u52a1' class='btn'/> <a href='/tasks' class='btn'>\u53d6\u6d88</a></form>";
        return renderTemplate("\u53d1\u5e03\u4efb\u52a1", content, user);
    }
    std::string title = parseFormData(body, "title");
    std::string description = parseFormData(body, "description");
    int reward = common::stringToInt(parseFormData(body, "reward"));
    if (title.empty()) {
        return renderTemplate("\u53d1\u5e03\u5931\u8d25", "<div class='alert alert-error'>\u6807\u9898\u4e0d\u80fd\u4e3a\u7a7a</div><a href='/task/new' class='btn'>\u8fd4\u56de</a>", user);
    }
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
    for (const auto& h : task->helpers) if (user && h.userId == user->id) isHelper = true;

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
        if (user && isHelper) {
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
    std::string helperId = parseFormData(body, "helperId");
    if (helperId.empty()) return "<script>window.location.href='/task/" + id + "'</script>";
    for (auto& h : task->helpers) {
        if (h.userId == helperId && h.state == TH_SUBMITTED) {
            ds.addGold(helperId, task->reward);
            ds.addExperience(helperId, task->reward / 2);
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

`;
  src = patchBlock(src, 'tasks region',
    'static std::string taskStatusText(int st) {',
    'std::string Handlers::handleAdminSendGold(',
    tasks);

  // 6) 路由注册：/messages/*
  src = patchBlock(src, 'route /messages/*',
    '    router.get("/messages", handleMessages);',
    '    router.post("/message/send", handleSendMessage);',
    '    router.get("/messages", handleMessages);\n    router.get("/messages/*", handleMessages);\n    router.post("/message/send", handleSendMessage);\n');

  save('src/handlers.cpp', src);
}

console.log('\nALL PATCHES APPLIED.');

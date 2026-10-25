const fs = require('fs');
const H='F:/LL/OIstation/src/handlers.cpp';
function read(p){return fs.readFileSync(p,'latin1').replace(/\r\n/g,'\n');}
function write(p,s){fs.writeFileSync(p,s,'latin1');}
function rep(s,old,neu,label){if(s.indexOf(old)<0)throw new Error('anchor not found: '+label);return s.replace(old,neu);}
let h=read(H);

// ===== 1. shares helpers + handleDriveShare + handleShare (before handleDrive) =====
const hdAnchor='std::string Handlers::handleDrive(const std::string& method, const std::string& path,';
const shareBlock=String.raw`
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

std::string Handlers::handleDriveShare(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string action = parseFormData(body, "action");
    std::string fname = sanitizeFilename(parseFormData(body, "file"));
    std::string dir = "./files/" + user->id + "/";
    if (action == "share") {
        if (!common::fileExists(dir + fname)) return "<div class='alert alert-error'>\u6587\u4ef6\u4e0d\u5b58\u5728</div><a href='/drive' class='btn'>\u8fd4\u56de</a>";
        auto shares = loadShares();
        for (const auto& sh : shares) if (sh["userId"] == user->id && sh["filename"] == fname) return "<script>window.location.href='/drive'</script>";
        std::map<std::string, std::string> entry;
        entry["token"] = genShareToken();
        entry["userId"] = user->id;
        entry["filename"] = fname;
        entry["time"] = common::getCurrentTimeStr();
        shares.push_back(entry);
        saveShares(shares);
    } else if (action == "unshare") {
        auto shares = loadShares();
        std::vector<std::map<std::string, std::string> > keep;
        for (const auto& sh : shares) if (!(sh["userId"] == user->id && sh["filename"] == fname)) keep.push_back(sh);
        saveShares(keep);
    }
    return "<script>window.location.href='/drive'</script>";
}
std::string Handlers::handleShare(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId) {
    std::string token = path.substr(path.find_last_of('/') + 1);
    auto shares = loadShares();
    std::string userId, fname;
    for (const auto& sh : shares) if (sh["token"] == token) { userId = sh["userId"]; fname = sanitizeFilename(sh["filename"]); break; }
    if (userId.empty() || fname.empty()) return "<div class='alert alert-warning'>\u5171\u4eab\u94fe\u63a5\u65e0\u6548\u6216\u5df2\u5220\u9664</div>";
    std::string fp = "./files/" + userId + "/" + fname;
    if (!common::fileExists(fp)) return "<div class='alert alert-warning'>\u6587\u4ef6\u5df2\u4e0d\u5b58\u5728</div>";
    respHeaders["Content-Type"] = "application/octet-stream";
    respHeaders["Content-Disposition"] = "attachment; filename=\"" + fname + "\"";
    return common::readFile(fp);
}

`;
h=rep(h,hdAnchor,shareBlock+hdAnchor,'share helpers');

// ===== 2. handleDrive: load shares map =====
const oldLoad=`    std::vector<std::string> files = listDirFiles(dir);
    long long usedTotal = 0;`;
const newLoad=`    std::vector<std::string> files = listDirFiles(dir);
    auto sharesAll = loadShares();
    std::map<std::string, std::string> tokenByFile;
    for (const auto& sh : sharesAll) if (sh["userId"] == user->id) tokenByFile[sh["filename"]] = sh["token"];
    long long usedTotal = 0;`;
h=rep(h,oldLoad,newLoad,'drive load shares');

// ===== 3. handleDrive table block =====
const oldTable=String.raw`        content += "<table><thead><tr><th>\u6587\u4ef6\u540d</th><th>\u5927\u5c0f</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
        for (const auto& f : files) {
            std::string fp = dir + f;
            content += "<tr><td>" + common::htmlEscape(f) + "</td><td>" + humanSize(getFileSizeBytes(fp)) + "</td>"
                       "<td><a href='/drive/download/" + common::urlEncode(f) + "' class='btn btn-sm'>\u2193 \u4e0b\u8f7d</a> "
                       "<form method='POST' action='/drive/delete' style='display:inline;'>"
                       "<input type='hidden' name='file' value='" + common::htmlEscape(f) + "'/>"
                       "<input type='submit' value='\u5220\u9664' class='btn btn-sm btn-danger' onclick='return confirm(\"\u786e\u5b9a\u5220\u9664\u5417\uff1f\")'/></form></td></tr>";
        }
        content += "</tbody></table>";`;
const newTable=String.raw`        content += "<table><thead><tr><th>\u6587\u4ef6\u540d</th><th>\u5927\u5c0f</th><th>\u64cd\u4f5c</th><th>\u5171\u4eab</th></tr></thead><tbody>";
        for (const auto& f : files) {
            std::string fp = dir + f;
            std::string shareCell;
            if (tokenByFile.count(f)) {
                std::string link = "/share/" + tokenByFile[f];
                shareCell = "<div style='font-size:12px;line-height:1.9;'>"
                            "<a href='" + link + "' target='_blank' class='btn btn-sm'>\U0001f517 " + tokenByFile[f] + "</a> "
                            "<button class='btn btn-sm' onclick='navigator.clipboard.writeText(location.origin+\"" + link + "\").catch(function(){})'>\u590d\u5236</button> "
                            "<form method='POST' action='/drive/share' style='display:inline;'>"
                            "<input type='hidden' name='action' value='unshare'/>"
                            "<input type='hidden' name='file' value='" + common::htmlEscape(f) + "'/>"
                            "<input type='submit' value='\u53d6\u6d88\u5171\u4eab' class='btn btn-sm btn-danger'/></form></div>";
            } else {
                shareCell = "<form method='POST' action='/drive/share' style='display:inline;'>"
                            "<input type='hidden' name='action' value='share'/>"
                            "<input type='hidden' name='file' value='" + common::htmlEscape(f) + "'/>"
                            "<input type='submit' value='\U0001f517 \u5171\u4eab' class='btn btn-sm'/></form>";
            }
            content += "<tr><td>" + common::htmlEscape(f) + "</td><td>" + humanSize(getFileSizeBytes(fp)) + "</td>"
                       "<td><a href='/drive/download/" + common::urlEncode(f) + "' class='btn btn-sm'>\u2193 \u4e0b\u8f7d</a> "
                       "<form method='POST' action='/drive/delete' style='display:inline;'>"
                       "<input type='hidden' name='file' value='" + common::htmlEscape(f) + "'/>"
                       "<input type='submit' value='\u5220\u9664' class='btn btn-sm btn-danger' onclick='return confirm(\"\u786e\u5b9a\u5220\u9664\u5417\uff1f\")'/></form></td>"
                       "<td>" + shareCell + "</td></tr>";
        }
        content += "</tbody></table>";`;
h=rep(h,oldTable,newTable,'drive table');

// ===== 4. tasks pagination: collection =====
const oldTasks=`    auto& tasks = DataStore::instance().tasks;
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
    }`;
const newTasks=`    auto& tasks = DataStore::instance().tasks;
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
    }`;
h=rep(h,oldTasks,newTasks,'tasks collect');

// ===== 5. tasks pagination nav =====
const oldStyle=`        content += "</div>";
        content += "<style>"`;
const newStyle=`        content += "</div>";
        content += "<div style='margin-top:16px;text-align:center;'>";
        if (tpage > 1) content += "<a href='/tasks/page/" + std::to_string(tpage - 1) + "' class='btn btn-sm'>\u4e0a\u4e00\u9875</a> ";
        content += "<span style='margin:0 10px;color:#7f8c8d;'>\u7b2c " + std::to_string(tpage) + "/" + std::to_string(tpages) + " \u9875</span>";
        if (tpage < tpages) content += "<a href='/tasks/page/" + std::to_string(tpage + 1) + "' class='btn btn-sm'>\u4e0b\u4e00\u9875</a>";
        content += "</div>";
        content += "<style>"`;
h=rep(h,oldStyle,newStyle,'tasks nav');

// ===== 6. admin AI: POST + pricing card =====
const oldAI=`    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string content;`;
const newAI=String.raw`    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    if (method == "POST") {
        std::string pmodel = common::trim(parseFormData(body, "model"));
        std::string action = parseFormData(body, "action");
        if (action == "remove") {
            common::llmRemoveModelMultiplier(pmodel);
        } else if (!pmodel.empty()) {
            double mult = atof(parseFormData(body, "multiplier").c_str());
            common::llmSetModelMultiplier(pmodel, mult);
        }
        return "<script>window.location.href='/admin/ai'</script>";
    }
    std::string content;
    content += "<div class='card' style='padding:20px;'>"
               "<h3>\U0001f4b0 \u6a21\u578b\u5b9a\u4ef7</h3>"
               "<div class='alert alert-info'>\u4e0d\u914d\u7f6e\u7684\u6a21\u578b\u6309\u53c2\u6570\u91cf\u81ea\u52a8\u8ba1\u4ef7\uff08\u51e0 B \u7ffb\u51e0\u500d\uff09\uff1b\u5728\u6b64\u8bbe\u7f6e\u53ef\u8986\u76d6\u3002\u914d\u7f6e\u4fdd\u5b58\u5728 data/llm_pricing.json\u3002</div>"
               "<form method='POST' style='display:flex;gap:12px;flex-wrap:wrap;align-items:flex-end;margin-bottom:6px;'>"
               "<div class='form-group' style='margin:0;'><label>\u6a21\u578b\u540d</label><input name='model' placeholder='\u5982 qwen3:0.6b' required/></div>"
               "<div class='form-group' style='margin:0;'><label>\u4ef7\u683c\u500d\u7387</label><input name='multiplier' type='number' step='0.1' min='0.5' value='1' required/></div>"
               "<input type='submit' value='\u4fdd\u5b58' class='btn'/></form>";
    {
        std::string pcfg = common::llmPricingJson();
        auto po = common::jsonParseObject(pcfg);
        if (!po.empty()) {
            content += "<table style='font-size:13px;'><thead><tr><th>\u6a21\u578b</th><th>\u4ef7\u683c\u500d\u7387</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
            for (const auto& kv : po) {
                content += "<tr><td>" + common::htmlEscape(kv.first) + "</td><td>" + kv.second + "</td>"
                           "<td><form method='POST' style='display:inline;'><input type='hidden' name='action' value='remove'/>"
                           "<input type='hidden' name='model' value='" + common::htmlEscape(kv.first) + "'/>"
                           "<input type='submit' value='\u5220\u9664' class='btn btn-sm btn-danger'/></form></td></tr>";
            }
            content += "</tbody></table>";
        }
    }
    content += "</div>";`;
h=rep(h,oldAI,newAI,'admin AI pricing');

// ===== 7. routes =====
const oldRoute1=`    router.post("/drive/delete", handleDriveDelete);`;
const newRoute1=`    router.post("/drive/delete", handleDriveDelete);
    router.post("/drive/share", handleDriveShare);
    router.get("/share/*", handleShare);`;
h=rep(h,oldRoute1,newRoute1,'route drive/share');
const oldRoute2=`    router.get("/tasks", handleTasks);`;
const newRoute2=`    router.get("/tasks", handleTasks);
    router.get("/tasks/page/*", handleTasks);`;
h=rep(h,oldRoute2,newRoute2,'route tasks page');

write(H,h);
console.log('handlers.cpp patched (sharing + tasks pagination + AI pricing + routes)');
console.log('SCRIPT B DONE');

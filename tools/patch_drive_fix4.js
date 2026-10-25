const fs = require('fs');
const H = 'F:/LL/OIstation/src/handlers.cpp';
function read(p){ return fs.readFileSync(p, 'latin1'); }
function write(p, s){ fs.writeFileSync(p, s, 'latin1'); }
function rep(s, old, neu, label){ if (s.indexOf(old) < 0) throw new Error('anchor not found: ' + label); return s.replace(old, neu); }
let hc = read(H);

// ---- 1. replace config helpers ----
const oldCfg = String.raw`static int getDriveMaxMB() {
    std::string cfg = common::readFile("./data/drive.json");
    int mb = 10;
    if (!cfg.empty()) {
        auto o = common::jsonParseObject(cfg);
        if (o.count("maxFileMB")) mb = common::stringToInt(o["maxFileMB"]);
        if (mb < 1) mb = 1;
    }
    return mb;
}
static void setDriveMaxMB(int mb) {
    if (mb < 1) mb = 1;
    common::writeFile("./data/drive.json", "{\"maxFileMB\":" + std::to_string(mb) + "}");
}`;
const newCfg = String.raw`static int getDriveMB(const std::string& key, int def) {
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
static void setDriveConfig(int maxFileMB, int maxTotalMB) {
    if (maxFileMB < 1) maxFileMB = 1;
    if (maxTotalMB < 1) maxTotalMB = 1;
    common::writeFile("./data/drive.json", "{\"maxFileMB\":" + std::to_string(maxFileMB) + ",\"maxTotalMB\":" + std::to_string(maxTotalMB) + "}");
}`;
hc = rep(hc, oldCfg, newCfg, 'cfg helpers');

// ---- 2. handleDriveUpload: enforce total quota ----
const oldUp = String.raw`    parseMultipartMulti(body, extractBoundary(headers), fields, files);
    int ok = 0, skipped = 0;
    for (const auto& f : files) {
        int maxMB = getDriveMaxMB();
        if (f.data.size() > (long long)maxMB * 1024 * 1024) { skipped++; continue; }
        std::string fname = sanitizeFilename(f.filename);
        if (common::writeFile(dir + fname, f.data)) ok++;
        else skipped++;
    }
    std::string msg = "\u5df2\u4e0a\u4f20 " + std::to_string(ok) + " \u4e2a\u6587\u4ef6\uff0c\u8df3\u8fc7 " + std::to_string(skipped) + " \u4e2a\uff08\u592a\u5927\u6216\u5931\u8d25\uff09";`;
const newUp = String.raw`    parseMultipartMulti(body, extractBoundary(headers), fields, files);
    long long maxPer = (long long)getDriveMaxMB() * 1024 * 1024;
    long long maxTotal = (long long)getDriveTotalMB() * 1024 * 1024;
    long long usedTotal = 0;
    for (const auto& n : listDirFiles(dir)) usedTotal += getFileSizeBytes(dir + n);
    int ok = 0, skipped = 0;
    for (const auto& f : files) {
        std::string fname = sanitizeFilename(f.filename);
        if (f.data.size() > (size_t)maxPer) { skipped++; continue; }
        if (usedTotal + (long long)f.data.size() > maxTotal) { skipped++; continue; }
        if (common::writeFile(dir + fname, f.data)) { ok++; usedTotal += (long long)f.data.size(); }
        else skipped++;
    }
    std::string msg = "\u5df2\u4e0a\u4f20 " + std::to_string(ok) + " \u4e2a\u6587\u4ef6\uff0c\u8df3\u8fc7 " + std::to_string(skipped) + " \u4e2a\uff08\u592a\u5927\u3001\u8d85\u603b\u914d\u989d\u6216\u5931\u8d25\uff09";`;
hc = rep(hc, oldUp, newUp, 'drive upload quota');

// ---- 3. handleDrive alert: show used/total ----
const oldAlert = String.raw`    std::string content = "<div class='alert alert-info'>\u2601\ufe0f \u4e91\u76d8\uff1a\u6bcf\u4f4d\u7528\u6237\u72ec\u7acb\u5b58\u50a8\u7a7a\u95f4\uff0c\u4ec5\u81ea\u5df1\u53ef\u89c1\u3002</div>";`;
const newAlert = String.raw`    long long usedTotal = 0;
    for (const auto& n : files) usedTotal += getFileSizeBytes(dir + n);
    long long quotaBytes = (long long)getDriveTotalMB() * 1024 * 1024;
    std::string content = "<div class='alert alert-info'>\u2601\ufe0f \u4e91\u76d8\uff1a\u6bcf\u4f4d\u7528\u6237\u72ec\u7acb\u5b58\u50a8\u7a7a\u95f4\uff0c\u4ec5\u81ea\u5df1\u53ef\u89c1\u3002\uff08\u5df2\u7528 " + humanSize(usedTotal) + " / \u603b\u914d\u989d " + humanSize(quotaBytes) + "\uff09</div>";`;
hc = rep(hc, oldAlert, newAlert, 'drive alert quota');

// ---- 4. handleAdminDrive: two fields ----
const oldAdm = String.raw`    if (method == "POST") {
        std::string v = parseFormData(body, "maxFileMB");
        int mb = common::stringToInt(v);
        if (mb < 1) mb = 1;
        setDriveMaxMB(mb);
        std::string content = "<div class='alert alert-success'>\u4e91\u76d8\u5355\u6587\u4ef6\u4e0a\u9650\u5df2\u66f4\u65b0\u4e3a " + std::to_string(mb) + " MB</div>"
                              "<a href='/admin/drive' class='btn'>\u7ee7\u7eed\u8bbe\u7f6e</a> <a href='/admin' class='btn'>\u8fd4\u56de\u7ba1\u7406\u9762\u677f</a>";
        return renderTemplate("\u4e91\u76d8\u8bbe\u7f6e", content, user);
    }
    int cur = getDriveMaxMB();
    std::string content = "<div class='alert alert-info'>\u8bbe\u7f6e\u4e91\u76d8\u6bcf\u4e2a\u6587\u4ef6\u7684\u4e0a\u4f20\u4e0a\u9650\uff08\u5355\u4f4d MB\uff09\uff0c\u5f53\u524d\u503c\uff1a<b>" + std::to_string(cur) + " MB</b></div>"
        "<form method='POST'>"
        "<div class='form-group'><label>\u5355\u6587\u4ef6\u4e0a\u9650 (MB)</label><input type='number' name='maxFileMB' value='" + std::to_string(cur) + "' min='1' max='512' required/></div>"
        "<input type='submit' value='\u4fdd\u5b58' class='btn'/> <a href='/admin' class='btn'>\u53d6\u6d88</a></form>";
    return renderTemplate("\u4e91\u76d8\u8bbe\u7f6e", content, user);`;
const newAdm = String.raw`    if (method == "POST") {
        std::string v = parseFormData(body, "maxFileMB");
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
    return renderTemplate("\u4e91\u76d8\u8bbe\u7f6e", content, user);`;
hc = rep(hc, oldAdm, newAdm, 'admin drive two fields');

// ---- 5. /problems: hide hidden problems from everyone ----
hc = rep(hc, `        if (!p.hidden || isManager) {`, `        if (!p.hidden) {`, 'problems hidden filter');

write(H, hc);
console.log('handlers.cpp patched');

// ---- 6. default drive.json with total quota ----
fs.writeFileSync('F:/LL/OIstation/data/drive.json', '{"maxFileMB":10,"maxTotalMB":100}', 'latin1');
console.log('drive.json updated (maxFileMB=10, maxTotalMB=100)');
console.log('ALL DONE');

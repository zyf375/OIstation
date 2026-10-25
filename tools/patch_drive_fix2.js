const fs = require('fs');
const S = 'F:/LL/OIstation/src/server.cpp';
const H = 'F:/LL/OIstation/src/handlers.cpp';
const HH = 'F:/LL/OIstation/include/handlers.h';

function read(p){ return fs.readFileSync(p, 'latin1'); }
function write(p, s){ fs.writeFileSync(p, s, 'latin1'); }
function rep(s, old, neu, label){ if (s.indexOf(old) < 0) throw new Error('anchor not found: ' + label); return s.replace(old, neu); }

// ============ 1. server.cpp: read full request body ============
let sc = read(S);
const readFn = String.raw`
// Read the full HTTP request (headers + body per Content-Length).
// The previous code only did one recv() into an 8KB buffer, so any
// multipart/file body larger than ~8KB was truncated and the socket
// closed while the client was still sending -> "connection reset".
static void readHttpRequest(SOCKET fd, std::string& request) {
    char buffer[8192];
    const int MAX_TOTAL = 64 * 1024 * 1024;
    while ((int)request.size() < MAX_TOTAL) {
        int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        request.append(buffer, n);
        size_t hdrEnd = request.find("\r\n\r\n");
        if (hdrEnd == std::string::npos) continue;
        std::string headerArea = request.substr(0, hdrEnd);
        std::string lower = headerArea;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        size_t clPos = lower.find("content-length:");
        size_t need = 0;
        if (clPos != std::string::npos) {
            size_t le = headerArea.find("\r\n", clPos);
            std::string cl = headerArea.substr(clPos + 15, le == std::string::npos ? std::string::npos : le - (clPos + 15));
            need = (size_t)common::stringToInt(common::trim(cl));
        }
        size_t bodyStart = hdrEnd + 4;
        if (request.size() >= bodyStart + need) break;
    }
}

void HttpServer::handleClient(SOCKET clientFd) {`;
const oldTop = `void HttpServer::handleClient(SOCKET clientFd) {
    char buffer[8192];
    int n = recv(clientFd, buffer, sizeof(buffer)-1, 0);
    if (n <= 0) { closesocket(clientFd); return; }
    buffer[n] = '\\0';
    std::string request(buffer, n);`;
sc = rep(sc, oldTop, readFn, 'handleClient top');
write(S, sc);
console.log('server.cpp patched');

// ============ 2. handlers.h: declare handleAdminDrive ============
let hh = read(HH);
const hhAnchor = '    static std::string handleAdminProblemData(';
const hhAdd = `    static std::string handleAdminDrive(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
`;
hh = rep(hh, hhAnchor, hhAdd + hhAnchor, 'handlers.h decl anchor');
write(HH, hh);
console.log('handlers.h patched');

// ============ 3. handlers.cpp ============
let hc = read(H);

// 3a. insert getDriveMaxMB/setDriveMaxMB before handleDrive
const hdAnchor = 'std::string Handlers::handleDrive(const std::string& method, const std::string& path,';
const cfgFn = String.raw`
static int getDriveMaxMB() {
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
}

`;
hc = rep(hc, hdAnchor, cfgFn + hdAnchor, 'drive helpers anchor');

// 3b. drive page label shows configurable limit
const oldLabel = `"<div class='form-group'><label>\u4e0a\u4f20\u6587\u4ef6\uff08\u53ef\u591a\u9009\uff0c\u6bcf\u4e2a\u6700\u5927 50MB\uff09</label>"`;
const newLabel = `"<div class='form-group'><label>\u4e0a\u4f20\u6587\u4ef6\uff08\u53ef\u591a\u9009\uff0c\u6bcf\u4e2a\u6700\u5927 " + std::to_string(getDriveMaxMB()) + " MB\uff09</label>"`;
hc = rep(hc, oldLabel, newLabel, 'drive label');

// 3c. upload size check uses configurable limit
const oldChk = `        if (f.data.size() > 50LL * 1024 * 1024) { skipped++; continue; }`;
const newChk = `        int maxMB = getDriveMaxMB();
        if (f.data.size() > (long long)maxMB * 1024 * 1024) { skipped++; continue; }`;
hc = rep(hc, oldChk, newChk, 'drive upload limit');

// 3d. add handleAdminDrive definition before registerRoutes
const regAnchor = 'void Handlers::registerRoutes(Router& router) {';
const adminDriveFn = String.raw`
std::string Handlers::handleAdminDrive(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    if (method == "POST") {
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
    return renderTemplate("\u4e91\u76d8\u8bbe\u7f6e", content, user);
}

`;
hc = rep(hc, regAnchor, adminDriveFn + regAnchor, 'admin drive def');

// 3e. register admin drive route
const routeAnchor = `    router.post("/drive/delete", handleDriveDelete);`;
const routeAdd = String.raw`
    router.get("/admin/drive", handleAdminDrive);
    router.post("/admin/drive", handleAdminDrive);
`;
hc = rep(hc, routeAnchor, routeAnchor + routeAdd, 'route add');

// 3f. admin dashboard card (after AI card)
const cardAnchor = `"/admin/ai", "");`;
const cardAdd = `\n    content += adminCardHTML("\\u2601\\ufe0f \\u4e91\\u76d8\\u8bbe\\u7f6e", "\\u8bbe\\u5b9a\\u4e91\\u76d8\\u4e0a\\u4f20\\u4e0a\\u9650", "/admin/drive", "");`;
hc = rep(hc, cardAnchor, cardAnchor + cardAdd, 'admin card');

write(H, hc);
console.log('handlers.cpp patched');

// ============ 4. default drive.json ============
fs.writeFileSync('F:/LL/OIstation/data/drive.json', '{"maxFileMB":10}', 'latin1');
console.log('drive.json created (maxFileMB=10)');
console.log('ALL DONE');

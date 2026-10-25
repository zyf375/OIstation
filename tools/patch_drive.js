// Patch: add Cloud Drive (云盘) + Problem test-data upload (题目测试数据上传)
// Reads/writes files as latin1 to preserve mixed encodings byte-safely.
const fs = require('fs');

const srcDir = 'F:/LL/OIstation/src';
const incDir = 'F:/LL/OIstation/include';
const hcppPath = srcDir + '/handlers.cpp';
const scppPath = srcDir + '/server.cpp';
const hhPath = incDir + '/handlers.h';

function read(p){ return fs.readFileSync(p, 'latin1'); }
function write(p, s){ fs.writeFileSync(p, s, 'latin1'); }

// ---------------- 1. handlers.h: add declarations ----------------
let hh = read(hhPath);
const hhAnchor = 'private:\n    static User* getCurrentUser(const std::string& sessionId);';
if (hh.indexOf(hhAnchor) < 0) throw new Error('handlers.h anchor not found');
const hhInsert = `    // \u4e91\u76d8 + \u9898\u76ee\u6d4b\u8bd5\u6570\u636e
    static std::string handleDrive(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId);
    static std::string handleDriveUpload(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleDriveDownload(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    static std::string handleDriveDelete(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleAdminProblemData(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId);

`;
hh = hh.replace(hhAnchor, hhInsert + hhAnchor);
write(hhPath, hh);
console.log('handlers.h patched');

// ---------------- 2. handlers.cpp: add <windows.h> include ----------------
let hcpp = read(hcppPath);
const incAnchor = '#include "../include/common.h"';
if (hcpp.indexOf(incAnchor) < 0) throw new Error('handlers.cpp include anchor not found');
hcpp = hcpp.replace(incAnchor, incAnchor + '\n#include <windows.h>');

// ---------------- 3. handlers.cpp: add helpers after parseMultipart ----------------
const impAnchor = 'std::string Handlers::handleAdminProblemImport(';
if (hcpp.indexOf(impAnchor) < 0) throw new Error('import anchor not found');
const helpers = String.raw`
// ============================================================
// helper: multi-file multipart parser + drive/testdata utilities
// ============================================================
struct UploadedFile {
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

`;
hcpp = hcpp.replace(impAnchor, helpers + impAnchor);

// ---------------- 4. handlers.cpp: add handler definitions before registerRoutes ----------------
const regAnchor = 'void Handlers::registerRoutes(Router& router) {';
if (hcpp.indexOf(regAnchor) < 0) throw new Error('registerRoutes anchor not found');
const handlersBlock = String.raw`
// ============================================================
// Cloud Drive (\u4e91\u76d8)
// ============================================================
std::string Handlers::handleDrive(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string dir = "./files/" + user->id + "/";
    common::createDirectory(dir);
    std::vector<std::string> files = listDirFiles(dir);
    std::string content = "<div class='alert alert-info'>\u2601\ufe0f \u4e91\u76d8\uff1a\u6bcf\u4f4d\u7528\u6237\u72ec\u7acb\u5b58\u50a8\u7a7a\u95f4\uff0c\u4ec5\u81ea\u5df1\u53ef\u89c1\u3002</div>";
    content += "<form method='POST' action='/drive/upload' enctype='multipart/form-data' style='padding:15px;background:#f8f9fa;border-radius:8px;margin-bottom:20px;'>"
               "<div class='form-group'><label>\u4e0a\u4f20\u6587\u4ef6\uff08\u53ef\u591a\u9009\uff0c\u6bcf\u4e2a\u6700\u5927 50MB\uff09</label>"
               "<input type='file' name='files' multiple/></div>"
               "<input type='submit' value='\u4e0a\u4f20' class='btn'/></form>";
    content += "<h3>\u6211\u7684\u6587\u4ef6 (" + std::to_string(files.size()) + ")</h3>";
    if (files.empty()) {
        content += "<div class='alert alert-warning'>\u8fd8\u6ca1\u6709\u4e0a\u4f20\u6587\u4ef6</div>";
    } else {
        content += "<table><thead><tr><th>\u6587\u4ef6\u540d</th><th>\u5927\u5c0f</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
        for (const auto& f : files) {
            std::string fp = dir + f;
            content += "<tr><td>" + common::htmlEscape(f) + "</td><td>" + humanSize(getFileSizeBytes(fp)) + "</td>"
                       "<td><a href='/drive/download/" + common::urlEncode(f) + "' class='btn btn-sm'>\u2193 \u4e0b\u8f7d</a> "
                       "<form method='POST' action='/drive/delete' style='display:inline;'>"
                       "<input type='hidden' name='file' value='" + common::htmlEscape(f) + "'/>"
                       "<input type='submit' value='\u5220\u9664' class='btn btn-sm btn-danger' onclick='return confirm(\"\u786e\u5b9a\u5220\u9664\u5417\uff1f\")'/></form></td></tr>";
        }
        content += "</tbody></table>";
    }
    return renderTemplate("\u4e91\u76d8", content, user);
}

std::string Handlers::handleDriveUpload(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string dir = "./files/" + user->id + "/";
    common::createDirectory(dir);
    std::map<std::string, std::string> fields;
    std::vector<UploadedFile> files;
    parseMultipartMulti(body, extractBoundary(headers), fields, files);
    int ok = 0, skipped = 0;
    for (const auto& f : files) {
        if (f.data.size() > 50LL * 1024 * 1024) { skipped++; continue; }
        std::string fname = sanitizeFilename(f.filename);
        if (common::writeFile(dir + fname, f.data)) ok++;
        else skipped++;
    }
    std::string msg = "\u5df2\u4e0a\u4f20 " + std::to_string(ok) + " \u4e2a\u6587\u4ef6\uff0c\u8df3\u8fc7 " + std::to_string(skipped) + " \u4e2a\uff08\u592a\u5927\u6216\u5931\u8d25\uff09";
    std::string content = "<div class='alert alert-success'>" + msg + "</div><a href='/drive' class='btn'>\u8fd4\u56de\u4e91\u76d8</a>";
    return renderTemplate("\u4e0a\u4f20\u5b8c\u6210", content, user);
}

std::string Handlers::handleDriveDownload(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string fname = path.substr(path.find_last_of('/') + 1);
    fname = common::urlDecode(fname);
    std::string fp = "./files/" + user->id + "/" + sanitizeFilename(fname);
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
    std::string fname = parseFormData(body, "file");
    std::string fp = "./files/" + user->id + "/" + sanitizeFilename(fname);
    DeleteFileA(fp.c_str());
    return "<script>window.location.href='/drive'</script>";
}

// ============================================================
// Problem test-data upload (\u9898\u76ee\u6d4b\u8bd5\u6570\u636e)
// ============================================================
std::string Handlers::handleAdminProblemData(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!canManageProblems(user)) return "<script>window.location.href='/'</script>";
    std::string pid = path.substr(path.find_last_of('/') + 1);
    std::string dir = "./problems/" + pid + "/";
    common::createDirectory(dir);
    if (method == "POST") {
        std::map<std::string, std::string> fields;
        std::vector<UploadedFile> files;
        parseMultipartMulti(body, extractBoundary(headers), fields, files);
        if (fields.count("action") && fields["action"] == "delete" && !fields["file"].empty()) {
            DeleteFileA((dir + sanitizeFilename(fields["file"])).c_str());
        } else {
            int ok = 0, skipped = 0;
            for (const auto& f : files) {
                std::string fname = sanitizeFilename(f.filename);
                std::string ext = common::getFileExtension(fname);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != "in" && ext != "out") { skipped++; continue; }
                if (common::writeFile(dir + fname, f.data)) ok++;
                else skipped++;
            }
            std::string c2 = "<div class='alert alert-success'>\u6d4b\u8bd5\u6570\u636e\uff1a\u4e0a\u4f20 " + std::to_string(ok) + " \u4e2a\uff0c\u8df3\u8fc7 " + std::to_string(skipped) + " \u4e2a\uff08\u975e .in/.out \u6216\u5931\u8d25\uff09</div>"
                              "<a href='/admin/problem/data/" + pid + "' class='btn'>\u7ee7\u7eed\u4e0a\u4f20</a> "
                              "<a href='/admin/problems' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u6d4b\u8bd5\u6570\u636e", c2, user);
        }
    }
    std::vector<std::string> files = listDirFiles(dir);
    std::string content = "<div class='alert alert-info'>\u9898\u76ee " + common::htmlEscape(pid) + " \u7684\u6d4b\u8bd5\u6570\u636e\u76ee\u5f55\uff1a<code>./problems/" + common::htmlEscape(pid) + "/</code>\uff0c\u8bc4\u6d4b\u673a\u4f1a\u81ea\u52a8\u627e *.in / *.out \u952e\u5bf9\u3002</div>";
    content += "<form method='POST' action='/admin/problem/data/" + pid + "' enctype='multipart/form-data' style='padding:15px;background:#f8f9fa;border-radius:8px;margin-bottom:20px;'>"
               "<div class='form-group'><label>\u4e0a\u4f20\u6d4b\u8bd5\u6570\u636e\uff08.in / .out \u6587\u4ef6\uff0c\u53ef\u591a\u9009\uff09</label>"
               "<input type='file' name='files' multiple accept='.in,.out'/></div>"
               "<input type='submit' value='\u4e0a\u4f20' class='btn'/></form>";
    content += "<h3>\u73b0\u6709\u6d4b\u8bd5\u70b9 (" + std::to_string(files.size()) + ")</h3>";
    if (files.empty()) {
        content += "<div class='alert alert-warning'>\u8fd8\u6ca1\u6709\u6d4b\u8bd5\u6570\u636e\uff0c\u8bf7\u4e0a\u4f20 .in/.out \u6587\u4ef6</div>";
    } else {
        content += "<table><thead><tr><th>\u6587\u4ef6\u540d</th><th>\u5927\u5c0f</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
        for (const auto& f : files) {
            content += "<tr><td>" + common::htmlEscape(f) + "</td><td>" + humanSize(getFileSizeBytes(dir + f)) + "</td>"
                       "<td><form method='POST' action='/admin/problem/data/" + pid + "' style='display:inline;'>"
                       "<input type='hidden' name='action' value='delete'/><input type='hidden' name='file' value='" + common::htmlEscape(f) + "'/>"
                       "<input type='submit' value='\u5220\u9664' class='btn btn-sm btn-danger' onclick='return confirm(\"\u786e\u5b9a\u5220\u9664\uff1f\")'/></form></td></tr>";
        }
        content += "</tbody></table>";
    }
    content += "<a href='/admin/problems' class='btn'>\u8fd4\u56de\u9898\u76ee\u7ba1\u7406</a>";
    return renderTemplate("\u6d4b\u8bd5\u6570\u636e", content, user);
}

`;
hcpp = hcpp.replace(regAnchor, handlersBlock + regAnchor);

// ---------------- 5. handlers.cpp: register routes ----------------
const regAfter = '    router.post("/admin/problems/import", handleAdminProblemImport);';
if (hcpp.indexOf(regAfter) < 0) throw new Error('route anchor not found');
const routesInsert = String.raw`
    router.get("/admin/problem/data/*", handleAdminProblemData);
    router.post("/admin/problem/data/*", handleAdminProblemData);
    // \u4e91\u76d8
    router.get("/drive", handleDrive);
    router.post("/drive/upload", handleDriveUpload);
    router.get("/drive/download/*", handleDriveDownload);
    router.post("/drive/delete", handleDriveDelete);
`;
hcpp = hcpp.replace(regAfter, regAfter + routesInsert);

// ---------------- 6. handlers.cpp: nav cloud-drive link ----------------
const navAnchor = `html += "<a id='aiBtn' href='/llm'`;
if (hcpp.indexOf(navAnchor) < 0) throw new Error('nav anchor not found');
const navInsert = String.raw`        html += "<a href='/drive' style='color:#fff;text-decoration:none;font-weight:500;background:rgba(255,255,255,0.18);padding:5px 12px;border-radius:6px;'>\u2601\ufe0f \u4e91\u76d8</a>";
`;
hcpp = hcpp.replace(navAnchor, navInsert + navAnchor);

write(hcppPath, hcpp);
console.log('handlers.cpp patched, new length', hcpp.length);

// ---------------- 7. server.cpp: fix sendResponse ----------------
let scpp = read(scppPath);
const oldSend = `void HttpServer::sendResponse(SOCKET fd, int statusCode, const std::string& statusMsg,
                              const std::map<std::string, std::string>& headers,
                              const std::string& body) {
    std::string response = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusMsg + "\\r\\n";
    response += "Content-Type: text/html; charset=utf-8\\r\\n";
    response += "Content-Length: " + std::to_string(body.length()) + "\\r\\n";
    response += "Connection: close\\r\\n";
    for (const auto& h : headers) response += h.first + ": " + h.second + "\\r\\n";
    response += "\\r\\n" + body;
    send(fd, response.c_str(), (int)response.length(), 0);
}`;
if (scpp.indexOf(oldSend) < 0) throw new Error('server.cpp sendResponse anchor not found');
const newSend = `void HttpServer::sendResponse(SOCKET fd, int statusCode, const std::string& statusMsg,
                              const std::map<std::string, std::string>& headers,
                              const std::string& body) {
    std::string response = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusMsg + "\\r\\n";
    std::string contentType = "text/html; charset=utf-8";
    auto cit = headers.find("Content-Type");
    if (cit != headers.end()) contentType = cit->second;
    response += "Content-Type: " + contentType + "\\r\\n";
    response += "Content-Length: " + std::to_string(body.length()) + "\\r\\n";
    response += "Connection: close\\r\\n";
    for (const auto& h : headers) {
        std::string k = h.first;
        std::string key = k;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key == "content-type" || key == "content-length" || key == "connection") continue;
        response += k + ": " + h.second + "\\r\\n";
    }
    response += "\\r\\n" + body;
    send(fd, response.c_str(), (int)response.length(), 0);
}`;
scpp = scpp.replace(oldSend, newSend);
write(scppPath, scpp);
console.log('server.cpp patched, new length', scpp.length);

console.log('ALL DONE');

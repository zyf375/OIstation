const fs = require('fs');
const path = require('path');
const base = path.join(__dirname, '..');
function load(filename){return fs.readFileSync(path.join(base,filename),'latin1').replace(/\r\n/g,'\n');}
function save(filename,src){fs.writeFileSync(path.join(base,filename),src,'latin1');}
function patchBlock(src,name,start,end,text){const s=src.indexOf(start);if(s===-1)throw new Error('START not found ['+name+']: '+start.slice(0,60));const e=src.indexOf(end,s);if(e===-1)throw new Error('END not found ['+name+']: '+end.slice(0,60));console.log('OK patch ['+name+']');return src.slice(0,s)+text+src.slice(e);}

// ================= handlers.h =================
{
  let src = load('include/handlers.h');
  const dec = String.raw`    static std::string handleAdminProblemImport(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId);
`;
  src = patchBlock(src,'hdr problem import',
    '    static std::string handleAdminProblems(',
    '    static std::string handleAdminProblems(',
    dec);
  save('include/handlers.h', src);
}

// ================= handlers.cpp =================
{
  let src = load('src/handlers.cpp');

  // 1) multipart helpers + problem import, inserted before handleAdminProblems
  const helpers = String.raw`// multipart/form-data 解析：提取文本字段与第一个文件（含 MIME 与字节）
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
        if (body.compare(i, 2, "--") == 0) break; // 结束边界
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

`;
  src = patchBlock(src,'helpers + problem import',
    'std::string Handlers::handleAdminProblems(',
    'std::string Handlers::handleAdminProblems(',
    helpers);

  // 2) handleUserEdit POST: proper multipart parsing
  const userEdit = String.raw`        std::map<std::string, std::string> fields;
        std::string fileMime, fileData;
        parseMultipart(body, extractBoundary(headers), fields, fileMime, fileData);
        std::string newUsername = fields.count("username") ? fields["username"] : user->username;
        std::string newBio = fields.count("bio") ? fields["bio"] : "";
        if (newUsername != user->username) {
            User* existing = DataStore::instance().findUserByName(newUsername);
            if (existing && existing->id != user->id) {
                std::string content = "<div class='alert alert-error'>\u7528\u6237\u540d\u5df2\u88ab\u5360\u7528</div><a href='/user/edit' class='btn'>\u8fd4\u56de</a>";
                return renderTemplate("\u7f16\u8f91\u5931\u8d25", content, user);
            }
            user->username = newUsername;
        }
        if (!newBio.empty()) user->bio = newBio;
        if (!fileData.empty()) {
            if (fileData.size() > 2 * 1024 * 1024) {
                std::string content = "<div class='alert alert-error'>\u5934\u50cf\u56fe\u7247\u8fc7\u5927\uff08\u9700\u5c0f\u4e8e 2MB\uff09</div><a href='/user/edit' class='btn'>\u8fd4\u56de</a>";
                return renderTemplate("\u7f16\u8f91\u5931\u8d25", content, user);
            }
            std::string mime = fileMime.empty() ? "image/png" : fileMime;
            user->avatar = "data:" + mime + ";base64," + common::base64Encode(fileData);
        }
`;
  src = patchBlock(src,'userEdit multipart',
    '        std::string newUsername = parseFormData(body, "username");',
    '        DataStore::instance().saveUsers();',
    userEdit);

  // 3) route registration
  src = patchBlock(src,'route problem import',
    '    router.get("/admin/problems/add", handleAdminAddProblem);',
    '    router.post("/admin/problems/add", handleAdminAddProblem);',
    '    router.get("/admin/problems/add", handleAdminAddProblem);\n    router.get("/admin/problems/import", handleAdminProblemImport);\n    router.post("/admin/problems/import", handleAdminProblemImport);\n');

  save('src/handlers.cpp', src);
}
console.log('DONE');

const fs = require('fs');
const p = 'F:/LL/OIstation/src/handlers.cpp';
let s = fs.readFileSync(p, 'latin1');

// --- Fix handleAdminProblemData POST: support urlencoded delete + multipart upload ---
const oldBlock = `    if (method == "POST") {
        std::map<std::string, std::string> fields;
        std::vector<UploadedFile> files;
        parseMultipartMulti(body, extractBoundary(headers), fields, files);
        if (fields.count("action") && fields["action"] == "delete" && !fields["file"].empty()) {
            DeleteFileA((dir + sanitizeFilename(fields["file"])).c_str());
        } else {`;
const newBlock = `    if (method == "POST") {
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
        if (action == "delete" && !dfile.empty()) {
            DeleteFileA((dir + sanitizeFilename(dfile)).c_str());
        } else {`;
if (s.indexOf(oldBlock) < 0) throw new Error('POST block not found');
s = s.replace(oldBlock, newBlock);

// --- Add test-data button after the edit link in each problem row (regex on ASCII prefix) ---
const re = /("<a href='\/admin\/problem\/edit\/" \+ p\.id \+ "' class='btn btn-sm btn-outline'>.*?<\/a>")/;
if (!re.test(s)) throw new Error('edit link regex not found');
const flaskEmoji = '\u{1F9EA}'; // 🧪 as UTF-8
const addBtn = `\n                  "<a href='/admin/problem/data/" + p.id + "' class='btn btn-sm btn-outline' title='\u6d4b\u8bd5\u6570\u636e'>${flaskEmoji}</a>"`;
s = s.replace(re, '$1' + addBtn);

fs.writeFileSync(p, s, 'latin1');
console.log('patched. new length', s.length);

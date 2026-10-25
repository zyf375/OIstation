const fs = require('fs');
const p = 'F:/LL/OIstation/src/server.cpp';
let s = fs.readFileSync(p, 'latin1');
const lines = s.split('\n');
let start = -1;
for (let i = 0; i < lines.length; i++) {
  if (lines[i].indexOf('void HttpServer::sendResponse(') >= 0) { start = i; break; }
}
if (start < 0) throw new Error('sendResponse not found');
let braceDepth = 0, end = start;
for (let i = start; i < lines.length; i++) {
  const t = lines[i];
  let done = false;
  for (const ch of t) {
    if (ch === '{') braceDepth++;
    else if (ch === '}') { braceDepth--; if (braceDepth === 0) { end = i; done = true; break; } }
  }
  if (done) break;
}
if (end <= start) throw new Error('end not found');
console.log('replacing lines', start + 1, 'to', end + 1);

const newBody = `void HttpServer::sendResponse(SOCKET fd, int statusCode, const std::string& statusMsg,
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

const out = lines.slice(0, start).concat(newBody, lines.slice(end + 1));
const result = out.join('\n').replace(/\r\n/g, '\n') + '\n';
fs.writeFileSync(p, result, 'latin1');
console.log('server.cpp patched, new length', result.length);

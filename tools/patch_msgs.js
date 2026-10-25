const fs = require('fs');
const H = 'F:/LL/OIstation/src/handlers.cpp';
const HD = 'F:/LL/OIstation/include/handlers.h';
function read(p){ return fs.readFileSync(p,'latin1').replace(/\r\n/g,'\n'); }
function write(p,s){ fs.writeFileSync(p,s,'latin1'); }
function rep(s, oldS, newS){ if(s.indexOf(oldS)<0) throw new Error('ANCHOR NOT FOUND: '+oldS.slice(0,60)); return s.split(oldS).join(newS); }

let h = read(H);

// 1) search form at top of sidebar
const sbAnchor = "std::string sb = \"<a class='conv\" + std::string(active == kSystemId ? \" active\" : \"\") + \"' href='/messages/system'>\"";
const searchForm = String.raw`std::string sb = "<form method='GET' action='/messages/search' style='padding:9px 10px;border-bottom:1px solid #eee;display:flex;gap:6px;'>"
 "<input type='text' name='q' placeholder='\u641c\u7d22\u7528\u6237' autocomplete='off' style='flex:1;min-width:0;padding:6px 8px;border:1px solid #ddd;border-radius:6px;'/>"
 "<button class='btn btn-sm' style='padding:6px 10px;'>\u641c\u7d22</button></form>"
 "<a class='conv" + std::string(active == kSystemId ? " active" : "") + "' href='/messages/system'>"`;
h = rep(h, sbAnchor, searchForm);

// 2) handleMessagesSearch function before registerRoutes
const rr = "void Handlers::registerRoutes(Router& router) {";
const searchFn = String.raw`std::string Handlers::handleMessagesSearch(const std::string& method, const std::string& path,
 const std::string& body,
 const std::map<std::string, std::string>& headers,
 std::map<std::string, std::string>& respHeaders,
 std::string& sessionId) {
 User* user = getCurrentUser(sessionId);
 if (!user) return "<script>window.location.href='/login'</script>";
 std::string q;
 if (method == "POST") {
  q = parseFormData(body, "q");
 } else {
  size_t qp = path.find("q=");
  if (qp != std::string::npos) {
   size_t end = path.find('&', qp);
   std::string v = path.substr(qp + 2, end == std::string::npos ? std::string::npos : end - qp - 2);
   q = common::urlDecode(v);
  }
 }
 std::string lq = common::trim(q);
 DataStore& ds = DataStore::instance();
 std::string content;
 content += "<div class='card' style='padding:22px;max-width:680px;margin:0 auto;'>";
 content += "<h3>\U0001f50d \u641c\u7d22\u7528\u6237</h3>";
 content += "<form method='GET' action='/messages/search' style='display:flex;gap:8px;margin:14px 0;'>"
  "<input type='text' name='q' value='" + common::htmlEscape(lq) + "' placeholder='\u8f93\u5165\u7528\u6237\u540d\u6216\u5176\u4e00\u90e8\u5206' autocomplete='off' style='flex:1;padding:8px;border:1px solid #ddd;border-radius:6px;'/>"
  "<button class='btn'>\u641c\u7d22</button></form>";
 if (lq.empty()) {
  content += "<div class='alert alert-info'>\u8bf7\u8f93\u5165\u5173\u952e\u5b57\u641c\u7d22\u7528\u6237\uff08\u652f\u6301\u7528\u6237\u540d\u90e8\u5206\u5339\u914d\uff09\uff0c\u70b9\u51fb\u7ed3\u679c\u5373\u53ef\u53d1\u8d77\u79c1\u804a\u3002</div>";
 } else {
  std::string lql = lq;
  std::transform(lql.begin(), lql.end(), lql.begin(), ::tolower);
  std::vector<User*> hits;
  for (auto& u : ds.users) {
   if (u.id == user->id) continue;
   std::string un = u.username;
   std::transform(un.begin(), un.end(), un.begin(), ::tolower);
   if (un.find(lql) != std::string::npos) hits.push_back(&u);
  }
  if (hits.empty()) {
   content += "<div class='alert alert-warning'>\u672a\u627e\u5230\u6ee1\u8db3\u6761\u4ef6\u7684\u7528\u6237\u3002</div>";
  } else {
   content += "<table><thead><tr><th>\u7528\u6237</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
   for (auto* u : hits) {
    content += "<tr><td>" + common::htmlEscape(u->username) + "</td>"
     "<td><a href='/messages/chat/" + u->id + "' class='btn btn-sm'>\u53d1\u79c1\u4fe1</a></td></tr>";
   }
   content += "</tbody></table>";
  }
 }
 content += "<a href='/messages' class='btn' style='margin-top:14px;'>\u8fd4\u56de\u6d88\u606f</a>";
 content += "</div>";
 return renderTemplate("\u641c\u7d22\u7528\u6237", content, user);
}

` + rr;
h = rep(h, rr, searchFn);

// 3) register search route BEFORE /messages/*
const msgWild = 'router.get("/messages/*", handleMessages);';
h = rep(h, msgWild, 'router.get("/messages/search*", handleMessagesSearch);\n    ' + msgWild);

write(H, h);

// 4) handlers.h declaration
let hd = read(HD);
const declAnchor = 'static std::string handleMessages(const std::string& method, const std::string& path,';
hd = rep(hd, declAnchor, 'static std::string handleMessagesSearch(const std::string& method, const std::string& path,\n                                     const std::string& body,\n                                     const std::map<std::string, std::string>& headers,\n                                     std::map<std::string, std::string>& respHeaders,\n                                     std::string& sessionId);\n\n    ' + declAnchor);
write(HD, hd);

console.log('OK');

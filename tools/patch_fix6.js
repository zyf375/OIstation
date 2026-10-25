const fs = require('fs');
const H='F:/LL/OIstation/src/handlers.cpp';
const HH='F:/LL/OIstation/include/handlers.h';
function read(p){return fs.readFileSync(p,'latin1').replace(/\r\n/g,'\n');}
function write(p,s){fs.writeFileSync(p,s,'latin1');}
function rep(s,old,neu,label){if(s.indexOf(old)<0)throw new Error('anchor not found: '+label);return s.replace(old,neu);}
let h=read(H);

// 1. nav about link (before the closing of left nav div)
const navAnchor=`    html += "</div><div style='display:flex;align-items:center;gap:15px;flex-wrap:wrap;'>";`;
const navAdd=`html += "<a href='/about' style='color:#fff;text-decoration:none;font-weight:500;background:rgba(255,255,255,0.18);padding:5px 12px;border-radius:6px;'>\\u2139\\ufe0f \\u5173\\u4e8e</a>";
`;
h=rep(h,navAnchor,navAdd+navAnchor,'nav about link');

// 2. handleAbout handler (before registerRoutes)
const rtAnchor=`void Handlers::registerRoutes(Router& router) {`;
const aboutFn=String.raw`std::string Handlers::handleAbout(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    std::string content;
    content += "<div class='card' style='padding:24px;max-width:760px;margin:0 auto;'>";
    content += "<h2>\u5173\u4e8e OI \u5c0f\u7ad9</h2>";
    content += "<p style='line-height:1.9;color:#555;'>OI \u5c0f\u7ad9\u662f\u4e00\u4e2a\u57fa\u4e8e C++11 \u7684\u5728\u7ebf\u5224\u9898\u7cfb\u7edf\uff0c\u652f\u6301\u7f16\u7a0b\u9898\u76ee\u4e0e\u8bc4\u6d4b\u3001\u7ade\u8d5b\u3001\u9898\u5355\u3001\u4efb\u52a1\u60ac\u8d4f\u3001\u5546\u57ce\u3001\u4e91\u76d8\u4ee5\u53ca AI \u52a9\u624b\u7b49\u529f\u80fd\u3002</p>";
    content += "<h3>\u5f00\u53d1\u8005</h3>";
    content += "<table style='min-width:320px;'><tbody>";
    content += "<tr><td><b>\u5f00\u53d1\u8005</b></td><td>zyf</td></tr>";
    content += "<tr><td><b>Luogu UID</b></td><td>1813501</td></tr>";
    content += "<tr><td><b>\u6280\u672f\u652f\u6491</b></td><td>deepseekv4</td></tr>";
    content += "</tbody></table>";
    content += "<p style='color:#7f8c8d;font-size:13px;'>\u672c\u9875\u5185\u5bb9\u4e3a\u7cfb\u7edf\u56fa\u5b9a\u4fe1\u606f\uff0c\u4e0d\u53ef\u4fee\u6539\u3002</p>";
    content += "</div>";
    return renderTemplate("\u5173\u4e8e", content, user);
}

`;
h=rep(h,rtAnchor,aboutFn+rtAnchor,'about handler');

// 3. register route
const routeAnchor=`    router.get("/share/*", handleShare);`;
const routeAdd=`    router.get("/share/*", handleShare);
    router.get("/about", handleAbout);`;
h=rep(h,routeAnchor,routeAdd,'about route');

write(H,h);
console.log('handlers.cpp patched (about nav + handler + route)');

// 4. handlers.h declaration
let hh=read(HH);
const hhAnchor=`    static std::string handleShare(`;
const hhAdd=`    static std::string handleAbout(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
`;
hh=rep(hh,hhAnchor,hhAdd+hhAnchor,'about dec');
write(HH,hh);
console.log('handlers.h patched');
console.log('SCRIPT FIX6 DONE');

const fs = require('fs');
const H='F:/LL/OIstation/src/handlers.cpp';
function read(p){return fs.readFileSync(p,'latin1').replace(/\r\n/g,'\n');}
function write(p,s){fs.writeFileSync(p,s,'latin1');}
let h=read(H);

// ---- fix 1: rebuild the broken tasks nav block ----
const startMark = `content += "<div style='margin-top:16px;text-align:center;'>";`;
if (h.indexOf(startMark) < 0) throw new Error('nav start mark not found');
const re = new RegExp(`content \\+= "<div style='margin-top:16px;text-align:center;'>";[\\s\\S]*?content \\+= "</div>";`);
const navBlock = String.raw`content += "<div style='margin-top:16px;text-align:center;'>";
        if (tpage > 1) content += "<a href='/tasks/page/" + std::to_string(tpage - 1) + "' class='btn btn-sm'>\u4e0a\u4e00\u9875</a> ";
        content += "<span style='margin:0 10px;color:#7f8c8d;'>\u7b2c " + std::to_string(tpage) + "/" + std::to_string(tpages) + " \u9875</span>";
        if (tpage < tpages) content += "<a href='/tasks/page/" + std::to_string(tpage + 1) + "' class='btn btn-sm'>\u4e0b\u4e00\u9875</a>";
        content += "</div>";`;
if (!re.test(h)) throw new Error('nav regex did not match');
h = h.replace(re, navBlock);
console.log('nav block fixed');

// ---- fix 2: const-map operator[] -> iterate non-const ----
let n0 = h.split('for (const auto& sh :').length - 1;
h = h.split('for (const auto& sh :').join('for (auto& sh :');
console.log('const sh loops replaced:', n0);

write(H,h);
console.log('REPAIR DONE');

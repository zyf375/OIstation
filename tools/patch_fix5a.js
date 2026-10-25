const fs = require('fs');
const CH='F:/LL/OIstation/include/common.h';
const CC='F:/LL/OIstation/src/common.cpp';
const SV='F:/LL/OIstation/src/server.cpp';
const HH='F:/LL/OIstation/include/handlers.h';
function read(p){return fs.readFileSync(p,'latin1').replace(/\r\n/g,'\n');}
function write(p,s){fs.writeFileSync(p,s,'latin1');}
function rep(s,old,neu,label){if(s.indexOf(old)<0)throw new Error('anchor not found: '+label);return s.replace(old,neu);}

// ---- common.h declarations ----
let ch=read(CH);
const oldDec=`std::string base64Encode(const std::string& data);
}`;
const newDec=`std::string base64Encode(const std::string& data);
// ---------- LLM pricing ----------
double llmAutoMultiplier(const std::string& model);
double llmModelMultiplier(const std::string& model);
void llmSetModelMultiplier(const std::string& model, double mult);
void llmRemoveModelMultiplier(const std::string& model);
std::string llmPricingJson();
}`;
ch=rep(ch,oldDec,newDec,'common.h dec'); write(CH,ch); console.log('common.h patched');

// ---- common.cpp definitions ----
let cc=read(CC);
const oldIs=`bool isBase64Image(const std::string& str) {
    return str.find("data:image/") == 0 && str.find(";base64,") != std::string::npos;
}`;
const defs=`
// ---------- LLM pricing ----------
double llmAutoMultiplier(const std::string& model) {
    std::string s = model;
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
    size_t p = s.find('b');
    if (p == std::string::npos) return 1.0;
    size_t q = p;
    while (q > 0 && (isdigit((unsigned char)s[q-1]) || s[q-1] == '.')) q--;
    if (q == p) return 1.0;
    std::string num = s.substr(q, p - q);
    if (num.empty()) return 1.0;
    double b = atof(num.c_str());
    double f = (b < 1.0) ? 1.0 : b;
    if (f <= 0) f = 1.0;
    return f;
}
double llmModelMultiplier(const std::string& model) {
    std::string cfg = readFile("./data/llm_pricing.json");
    if (!cfg.empty()) {
        auto o = jsonParseObject(cfg);
        if (o.count(model)) {
            double v = atof(o[model].c_str());
            if (v >= 0.5) return v;
        }
    }
    return llmAutoMultiplier(model);
}
void llmSetModelMultiplier(const std::string& model, double mult) {
    auto o = jsonParseObject(readFile("./data/llm_pricing.json"));
    if (mult < 0.5) mult = 0.5;
    char b[32]; snprintf(b, sizeof(b), "%.2f", mult);
    o[model] = b;
    writeFile("./data/llm_pricing.json", jsonStringify(o));
}
void llmRemoveModelMultiplier(const std::string& model) {
    auto o = jsonParseObject(readFile("./data/llm_pricing.json"));
    o.erase(model);
    writeFile("./data/llm_pricing.json", jsonStringify(o));
}
std::string llmPricingJson() {
    return readFile("./data/llm_pricing.json");
}
`;
cc=rep(cc,oldIs,oldIs+defs,'common.cpp defs'); write(CC,cc); console.log('common.cpp patched');

// ---- server.cpp: llmCost uses configured multiplier ----
let sv=read(SV);
const oldCost=`    double b = llmModelB(model);
    double factor = (b < 1.0) ? 1.0 : b;
    if (factor <= 0) factor = 1.0;`;
const newCost=`    double factor = common::llmModelMultiplier(model);`;
sv=rep(sv,oldCost,newCost,'server llmCost'); write(SV,sv); console.log('server.cpp patched');

// ---- handlers.h declarations ----
let hh=read(HH);
const hhAnc=`    static std::string handleAdminProblemData(`;
const hhAdd=`    static std::string handleDriveShare(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    static std::string handleShare(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);
`;
hh=rep(hh,hhAnc,hhAdd+hhAnc,'handlers.h dec'); write(HH,hh); console.log('handlers.h patched');

// ---- data files ----
fs.writeFileSync('F:/LL/OIstation/data/llm_pricing.json','{}','latin1');
fs.writeFileSync('F:/LL/OIstation/data/shares.json','','latin1');
console.log('data files created (llm_pricing.json={}, shares.json empty)');
console.log('SCRIPT A DONE');

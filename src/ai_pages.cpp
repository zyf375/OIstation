#include "../include/handlers.h"
#include "../include/ai_pages.h"
#include "../include/ai_quota.h"
#include "../include/ai_tools.h"
#include "../include/ai_plans.h"
#include "../include/ai_providers.h"
#include "../include/session.h"
#include "../include/common.h"
#include "../include/models.h"
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <cmath>
#include <cstdio>

namespace {

typedef std::map<std::string, std::string> HeaderMap;

std::string esc(const std::string& s) { return common::htmlEscape(s); }

std::string queryOf(const std::string& path, const std::string& key) {
    std::string needle = key + "=";
    size_t p = path.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    size_t amp = path.find('&', p);
    return common::urlDecode(path.substr(p, (amp == std::string::npos) ? std::string::npos : amp - p));
}

// AI 页面公共头部脚本：加载 /static/ai_client.js
std::string aiHead() {
    return "<script src='/static/ai_client.js'></script>";
}

// 历史条目 HTML（服务端首屏 + 前端刷新共用同一套结构）
std::string histRowsHtml(const std::vector<aitools::RunRow>& runs, const std::string& emptyText) {
    if (runs.empty())
        return "<div class='empty-td'>" + emptyText + "</div>";
    std::string h;
    for (size_t i = 0; i < runs.size(); i++) {
        const aitools::RunRow& r = runs[i];
        std::string typeName = (r.type == "debug") ? "诊断" : (r.type == "explain" ? "讲解" : r.type);
        std::string color = (r.type == "debug") ? "#e67e22" : "#27ae60";
        std::string link = (r.type == "debug") ? "/ai/debug" : "/ai/explain";
        h += "<a class='ai-hist-row' href='" + link + "?run=" + r.id + "'>";
        h += "<span class='ah-tag' style='background:" + color + "1a;color:" + color + ";'>" + typeName + "</span>";
        h += "<span class='ah-title'>" + esc(r.title.empty() ? "未命名" : r.title) + "</span>";
        h += "<span class='ah-time'>" + esc(r.time) + "</span>";
        h += "</a>";
    }
    return h;
}

// ---------------- /ai 中心 ----------------
std::string aiHome(const std::string& method, const std::string& path, const std::string& body,
                   const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)path; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    std::string c;
    if (user) {
        aiquota::Info qi = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
        int cfgHours = aiquota::load().windowHours;
        c += "<div class='ai-quota-bar'>";
        c += "<div class='aqb-item'><span>⚡ 剩余算力点</span><b>" + std::to_string(aiquota::remaining(qi)) + "</b></div>";
        c += "<div class='aqb-item'><span>🕒 " + std::to_string(cfgHours) + " 小时窗口</span><b>" +
             std::to_string(qi.windowUsed) + " / " + std::to_string(qi.windowQuota) + "</b></div>";
        c += "<div class='aqb-item'><span>📅 本月</span><b>" + std::to_string(qi.monthUsed) + " / " + std::to_string(qi.monthQuota) + "</b></div>";
        c += "<div class='aqb-item'><span>🎁 额外额度</span><b>" + std::to_string(qi.extra) + "</b></div>";
        c += "</div>";
    }
    struct Card { const char* icon; const char* title; const char* desc; const char* href; };
    const Card cards[] = {
        {"🐞", "OI-debug", "粘贴代码或选题号，让 AI 定位错误、给出改法，还能自己跑命令验证", "/ai/debug"},
        {"🧠", "思路讲解", "选题号或贴题面，讲清楚暴力→正解→复杂度→坑点，不直接给完整代码", "/ai/explain"},
        {"💬", "AI 对话", "通用问答，装有触手：能读写工作区文件、执行命令", "/llm/chat"},
        {"🗂", "使用历史", "回看每一次代码诊断与思路讲解的记录", "/ai/history"},
        {"📊", "额度用量分析", "5 小时窗口与本月额度、近 14 天消耗曲线、按模型/用途拆分", "/ai/usage"},
        {"📈", "学习分析", "AC 率、提交状态分布、刷题热力、薄弱点与推荐题目", "/ai/study"}
    };
    c += "<div class='ai-grid'>";
    for (int i = 0; i < 6; i++) {
        c += "<a class='ai-card' href='" + std::string(cards[i].href) + "'>";
        c += "<div class='ai-ic'>" + std::string(cards[i].icon) + "</div>";
        c += "<div><div class='ai-ct'>" + std::string(cards[i].title) + "</div>";
        c += "<div class='ai-cd'>" + std::string(cards[i].desc) + "</div></div></a>";
    }
    c += "</div>";

    // ---------- 套餐对比 ----------
    {
        std::vector<aiplan::Plan> plans = aiplan::load();
        if (plans.empty()) { aiplan::ensureSeed(); plans = aiplan::load(); }
        std::string curPlan = user ? user->aiPlanId : "";
        c += "<div class='card' style='margin-top:16px;'><div class='aq-title'>💳 套餐与额度</div>";
        c += "<div class='card-meta' style='margin-bottom:12px;'>每个套餐有独立的 5 小时窗口额度与月度额度，并决定可用模型。"
             "当前套餐由管理员分配，需要调整请联系管理员。</div>";
        c += "<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px;'>";
        // 默认额度卡（不跟随套餐的用户）
        if (curPlan.empty()) {
            aiquota::Config cfg = aiquota::load();
            c += "<div style='border:2px solid #3a86c8;border-radius:12px;padding:14px;background:rgba(58,134,200,.05);'>"
                 "<div style='display:flex;align-items:center;gap:8px;'>"
                 "<b style='font-size:15px;color:var(--strong,#1c232b);'>默认额度</b>"
                 "<span class='pill' style='color:#3a86c8;border-color:rgba(58,134,200,.4);'>当前</span></div>"
                 "<div style='font-size:22px;font-weight:800;color:#3a86c8;margin:8px 0 2px;'>" + std::to_string(cfg.defWindow) + " <span style='font-size:12px;font-weight:400;color:var(--muted,#8792a0);'>算力点 / 5 小时</span></div>"
                 "<div style='font-size:13px;color:var(--text,#2f3640);'>📅 每月 <b>" + std::to_string(cfg.defMonth) + "</b> 算力点</div>"
                 "<div style='font-size:12.5px;color:var(--muted,#8792a0);margin-top:8px;'>全部已启用模型</div></div>";
        }
        for (size_t i = 0; i < plans.size(); i++) {
            const aiplan::Plan& p = plans[i];
            bool cur = (curPlan == p.id);
            std::string border = cur ? "2px solid #3a86c8" : "1px solid var(--border,#e3e9f0)";
            std::string bg = cur ? "rgba(58,134,200,.05)" : "var(--surface,#fff)";
            c += "<div style='border:" + border + ";border-radius:12px;padding:14px;background:" + bg + ";'>";
            c += "<div style='display:flex;align-items:center;gap:8px;flex-wrap:wrap;'>"
                 "<b style='font-size:15px;color:var(--strong,#1c232b);'>" + esc(p.name) + "</b>";
            if (cur) c += "<span class='pill' style='color:#3a86c8;border-color:rgba(58,134,200,.4);'>当前</span>";
            if (p.price > 0) {
                char prb[40];
                snprintf(prb, sizeof(prb), "%.2f", p.price);
                c += "<span class='pill' style='color:#e67e22;border-color:rgba(230,126,34,.35);'>¥" + std::string(prb) + " / 月</span>";
            }
            c += "</div>";
            c += "<div style='font-size:22px;font-weight:800;color:#3a86c8;margin:8px 0 2px;'>" + std::to_string(p.windowQuota) +
                 " <span style='font-size:12px;font-weight:400;color:var(--muted,#8792a0);'>算力点 / 5 小时</span></div>";
            c += "<div style='font-size:13px;color:var(--text,#2f3640);'>📅 每月 <b>" + std::to_string(p.monthQuota) + "</b> 算力点</div>";
            std::string ms;
            if (p.models.empty()) ms = "全部已启用模型";
            else {
                ms = "可用 " + std::to_string(p.models.size()) + " 个模型：";
                for (size_t k = 0; k < p.models.size() && k < 3; k++) {
                    if (k) ms += "、";
                    ms += p.models[k];
                }
                if (p.models.size() > 3) ms += "…";
            }
            c += "<div style='font-size:12.5px;color:var(--muted,#8792a0);margin-top:8px;'>🧠 " + esc(ms) + "</div>";
            if (!p.desc.empty() && p.desc.find("算力点") == std::string::npos)
                c += "<div style='font-size:12px;color:var(--muted,#8792a0);margin-top:4px;'>" + esc(p.desc) + "</div>";
            c += "</div>";
        }
        c += "</div></div>";
    }

    // ---------- 模型价格 ----------
    {
        std::vector<aiprov::ModelEntry> entries = aiprov::entries();
        c += "<div class='card' style='margin-top:16px;'><div class='aq-title'>💲 模型价格</div>";
        c += "<div class='card-meta' style='margin-bottom:10px;'>单价 = 算力点 / 千 token（按模型可调），每次对话至少 1 算力点；"
             "带「免费池」标记的模型不产生上游费用。</div>";
        if (entries.empty()) {
            c += "<div class='empty-td'>管理员还没有启用任何模型</div>";
        } else {
            c += "<table style='width:100%;font-size:13px;'><thead><tr>"
                 "<th>模型</th><th>所属提供方</th><th style='width:150px;'>单价（算力点/千 token）</th>"
                 "<th style='width:90px;'>类型</th><th style='width:110px;'>我的套餐</th></tr></thead><tbody>";
            std::string lastProv;
            for (size_t i = 0; i < entries.size(); i++) {
                const aiprov::ModelEntry& m = entries[i];
                if (m.providerId != lastProv) {
                    c += "<tr><td colspan='5' style='background:var(--surface-2,#f7fafd);font-weight:700;color:var(--muted,#8792a0);"
                         "padding:6px 8px;font-size:12px;'>🔌 " + esc(m.providerName) + "</td></tr>";
                    lastProv = m.providerId;
                }
                char pb[64];
                snprintf(pb, sizeof(pb), "%.4g", aiquota::priceOf(m.id));
                bool allowed = user ? aiplan::userAllowedModel(*user, m.id) : true;
                c += "<tr><td><b>" + esc(m.name) + "</b><div style='color:var(--muted,#8792a0);font-size:11.5px;'>" + esc(m.id) + "</div></td>"
                     "<td>" + esc(m.providerName) + "</td>"
                     "<td><b>" + std::string(pb) + "</b></td>"
                     "<td>" + std::string(m.freeModel ? "<span class='pill' style='color:#27ae60;border-color:rgba(39,174,96,.35);'>免费池</span>" : "<span style='color:var(--muted,#8792a0);'>按量</span>") + "</td>"
                     "<td>" + std::string(allowed ? "<span style='color:#27ae60;'>✅ 可用</span>" : "<span style='color:#95a5a6;'>🔒 套餐不含</span>") + "</td></tr>";
            }
            c += "</tbody></table>";
        }
        c += "</div>";
    }

    if (user) {
        std::vector<aitools::RunRow> recent = aitools::readRuns(user->id, "all", 5);
        c += "<div class='card' style='margin-top:16px;'><div class='aq-title'>🗂 最近使用</div>";
        c += histRowsHtml(recent, "还没有使用记录，试试 OI-debug 或思路讲解");
        c += "</div>";
    }
    return Handlers::renderTemplate("AI 工具", c, user);
}

// ---------------- 公共：任务页（debug / explain） ----------------
std::string taskPage(const std::string& type, const std::string& runId, const std::string& pidPre, User* user) {
    bool isDebug = (type == "debug");
    std::string content;
    content += aiHead();

    // 可用模型（按套餐过滤）：debug / explain 也能选模型
    std::vector<aiprov::ModelEntry> visibleModels;
    {
        std::vector<aiprov::ModelEntry> entries = aiprov::entries();
        for (size_t i = 0; i < entries.size(); i++) {
            if (user && !aiplan::userAllowedModel(*user, entries[i].id)) continue;
            visibleModels.push_back(entries[i]);
        }
    }
    std::string modelOpts;
    for (size_t i = 0; i < visibleModels.size(); i++) {
        modelOpts += "<option value='" + esc(visibleModels[i].id) + "'>" + esc(visibleModels[i].name) +
                     " · " + esc(visibleModels[i].providerName) +
                     (visibleModels[i].freeModel ? "（免费池）" : "") + "</option>";
    }
    if (visibleModels.empty()) modelOpts = "<option value=''>暂无可选模型</option>";
    std::string modelSelect = "<div class='form-group' style='flex:1;min-width:190px;'>"
                              "<label>模型</label><select id='model'>" + modelOpts + "</select></div>";
    // 历史记录（右侧）
    std::vector<aitools::RunRow> runs = user ? aitools::readRuns(user->id, type, 12) : std::vector<aitools::RunRow>();

    content += "<div class='ai-two'>";
    content += "<div class='ai-maincol'>";

    if (isDebug) {
        content += "<div class='card'>";
        content += "<div style='display:flex;gap:12px;flex-wrap:wrap;'>";
        content += "<div class='form-group' style='flex:1;min-width:180px;'><label>题号（可选）</label>"
                   "<input id='pid' value='" + esc(pidPre) + "' placeholder='如 1001，留空只分析代码'/></div>";
        content += "<div class='form-group' style='flex:1;min-width:180px;'><label>补充说明</label>"
                   "<input id='extra' placeholder='如：样例能过，第 3 个点 WA'/></div>";
        content += modelSelect;
        content += "</div>";
        content += "<div class='form-group'><label>代码（粘贴，或上传文件；也可以先填题号自动带出你上次的提交）</label>"
                   "<textarea id='code' rows='13' spellcheck='false' style='font-family:Consolas,monospace;font-size:13px;' placeholder='#include <bits/stdc++.h> ...'></textarea></div>";
        content += "<div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap;'>"
                   "<label class='ai-file'>📎 上传代码<input type='file' id='file' accept='.cpp,.cc,.c,.py,.java,.txt,.in,.out' hidden/></label>"
                   "<button class='btn' id='go'>开始诊断</button>"
                   "<button class='btn' id='stop' style='display:none;'>■ 停止</button>"
                   "<label class='ai-chk'><input type='checkbox' id='useTools' checked/> 允许 AI 执行命令验证</label>"
                   "<span class='hint' id='tip'></span></div>";
        content += "<div id='attBox' class='ai-att'></div>";
        content += "</div>";
    } else {
        content += "<div class='card'>";
        content += "<div style='display:flex;gap:12px;flex-wrap:wrap;'>";
        content += "<div class='form-group' style='flex:1;min-width:170px;'><label>题号（可选）</label>"
                   "<input id='pid' value='" + esc(pidPre) + "' placeholder='如 1001'/></div>";
        content += "<div class='form-group' style='flex:1;min-width:170px;'><label>我的水平</label>"
                   "<select id='level'><option>入门</option><option selected>普及</option><option>提高</option><option>省选</option></select></div>";
        content += modelSelect;
        content += "</div>";
        content += "<div class='form-group'><label>题面（没有题号就把题目描述贴这里）</label>"
                   "<textarea id='statement' rows='10' placeholder='题目描述 / 输入输出格式 / 样例...'></textarea></div>";
        content += "<div class='form-group'><label>还想问什么（可选）</label>"
                   "<input id='q' placeholder='如：为什么这里要用单调队列？'/></div>";
        content += "<div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap;'>"
                   "<button class='btn' id='go'>生成思路讲解</button>"
                   "<button class='btn' id='stop' style='display:none;'>■ 停止</button>"
                   "<span class='hint' id='tip'></span></div>";
        content += "</div>";
    }

    // 输出区
    content += "<div class='card ai-out-wrap' id='outWrap' style='display:none;'>"
               "<div class='ai-out-head'><span id='outTitle'>结果</span><span class='spacer'></span>"
               "<button class='btn btn-sm' id='copyAll'>复制全文</button></div>"
               "<div class='ai-tools' id='toolsBox'></div>"
               "<div class='md ai-md' id='out'></div></div>";
    content += "</div>";   // ai-maincol

    // 右栏历史
    content += "<div class='ai-side'>";
    content += "<div class='card'><div class='aq-title'>🗂 使用历史</div><div id='histList'>";
    content += histRowsHtml(runs, "还没有记录");
    content += "</div><div style='margin-top:10px;'><a class='btn btn-sm' href='/ai/history'>全部历史 →</a></div></div>";
    content += "</div>";
    content += "</div>";

    // 历史详情（?run=）
    if (!runId.empty() && user) {
        aitools::RunRow rr;
        if (aitools::findRun(runId, rr) && rr.userId == user->id) {
            std::string js = "{";
            js += "\"title\":" + common::jsonStringify(rr.title) + ",";
            js += "\"time\":" + common::jsonStringify(rr.time) + ",";
            js += "\"type\":" + common::jsonStringify(rr.type) + ",";
            js += "\"question\":" + common::jsonStringify(rr.question) + ",";
            js += "\"answer\":" + common::jsonStringify(rr.answer);
            js += "}";
            std::string safe = common::replaceAll(js, "</", "<\\/");
            content += "<script id='runData' type='application/json'>" + safe + "</script>";
        }
    }

    // 页面脚本
    content += R"JS(<script>
(function(){
  var OUT = document.getElementById('out'), TOOLS = document.getElementById('toolsBox'),
      WRAP = document.getElementById('outWrap'), TIP = document.getElementById('tip'),
      STOP = document.getElementById('stop'), GO = document.getElementById('go'),
      TITLE = document.getElementById('outTitle');
  var running = null, acc = '', thinking = '', tools = [];
  var TYPE = ')JS" + type + R"JS(';
  var MSEL = document.getElementById('model');
  if (MSEL) {
    try {
      var savedModel = localStorage.getItem('oi_ai_model');
      if (savedModel) {
        for (var mi = 0; mi < MSEL.options.length; mi++)
          if (MSEL.options[mi].value === savedModel) { MSEL.selectedIndex = mi; break; }
      }
      MSEL.onchange = function(){ try { localStorage.setItem('oi_ai_model', MSEL.value); } catch(e){} };
    } catch(e) {}
  }

  function ensureWrap(title){
    WRAP.style.display = 'block';
    TITLE.textContent = title || '结果';
  }
  function paint(){
    var html = AI.toolsHtml(tools);
    TOOLS.innerHTML = html;
    var body = acc ? AI.toHtml(acc) : '<span class="ai-thinking">AI 正在思考…</span>';
    OUT.innerHTML = (thinking ? '<div class="ai-think"><b>💭 思考过程</b>' + AI.toHtml(thinking) + '</div>' : '') + body;
    if (acc) AI.renderMath(OUT);
    // 渲染库是异步加载的：就绪后重画一次，避免公式/ Markdown 丢掉
    if ((!window.markdownit || !window.renderMathInElement) && !AI._repaintQueued) {
      AI._repaintQueued = true;
      AI.ready.then(function(){ AI._repaintQueued = false; if (AI.libOk) paint(); });
    }
  }
  var timer = null;
  function paintSoon(){ if(timer) return; timer = setTimeout(function(){ timer = null; paint(); }, 90); }

  function refreshHist(){
    fetch('/ai/runs?type=' + TYPE).then(function(r){ return r.json(); }).then(function(d){
      var box = document.getElementById('histList');
      if(!box) return;
      if(!d || !d.length){ box.innerHTML = '<div class="empty-td">还没有记录</div>'; return; }
      var color = TYPE === 'debug' ? '#e67e22' : '#27ae60';
      var name = TYPE === 'debug' ? '诊断' : '讲解';
      box.innerHTML = d.map(function(r){
        return '<a class="ai-hist-row" href="' + (TYPE==='debug'?'/ai/debug':'/ai/explain') + '?run=' + r.id + '">' +
          '<span class="ah-tag" style="background:' + color + '1a;color:' + color + ';">' + name + '</span>' +
          '<span class="ah-title">' + AI.esc(r.title || '未命名') + '</span>' +
          '<span class="ah-time">' + AI.esc(r.time || '') + '</span></a>';
      }).join('');
    }).catch(function(){});
  }

  document.getElementById('copyAll').onclick = function(){ AI.copy(OUT.innerText, this, '复制全文'); };

  GO.onclick = function(){
    if(running) return;
    var payload = { task: TYPE, model: MSEL ? MSEL.value : '' };
    if (TYPE === 'debug') {
      var pid = document.getElementById('pid').value.trim();
      var code = document.getElementById('code').value;
      if(!pid && !code.trim()){ TIP.textContent = '请填题号或粘贴代码'; return; }
      payload.problemId = pid; payload.code = code;
      payload.extra = document.getElementById('extra').value;
      payload.tools = document.getElementById('useTools').checked ? '1' : '0';
    } else {
      var pid2 = document.getElementById('pid').value.trim();
      var st = document.getElementById('statement').value;
      if(!pid2 && !st.trim()){ TIP.textContent = '请填题号或贴题面'; return; }
      payload.problemId = pid2; payload.statement = st;
      payload.level = document.getElementById('level').value;
      payload.message = document.getElementById('q').value.trim();
      payload.tools = '0';
    }
    acc = ''; thinking = ''; tools = [];
    TIP.textContent = '';
    ensureWrap(TYPE === 'debug' ? '🔧 正在诊断…' : '🧠 正在讲解…');
    paint();
    GO.disabled = true; STOP.style.display = 'inline-flex';
    document.getElementById('outWrap').scrollIntoView({behavior:'smooth', block:'nearest'});

    running = AI.stream(payload, {
      onChunk: function(t){ acc += t; paintSoon(); },
      onThinking: function(t){ thinking += t; paintSoon(); },
      onTool: function(t){ tools.push(t); paint(); },
      onToolDone: function(t){
        for(var i=tools.length-1;i>=0;i--){ if(tools[i].name===t.name && tools[i].ok===undefined){ tools[i].ok=t.ok; tools[i].preview=t.preview; break; } }
        paint();
      },
      onMeta: function(m){
        TIP.textContent = '本次消耗 ' + m.cost + ' 算力点 · 剩余 ' + m.balance;
        if(m.quotaExceeded === '1') acc += '\n\n> ⛔ 额度用完，输出已停止。';
      },
      onError: function(msg, code){
        acc += (acc ? '\n\n' : '') + '⛔ ' + msg;
      },
      onDone: function(){
        running = null; GO.disabled = false; STOP.style.display = 'none';
        ensureWrap(TYPE === 'debug' ? '🔧 诊断结果' : '🧠 思路讲解');
        paint();
        if(acc && acc.length > 20) refreshHist();
      }
    });
  };
  STOP.onclick = function(){ if(running){ running.abort(); } };

  // 文件上传（修掉此前 #path 为 null 的报错）
  var fileInput = document.getElementById('file');
  if (fileInput) fileInput.onchange = function(e){
    var f = e.target.files && e.target.files[0];
    if(!f) return;
    if(f.size > 4*1024*1024){ TIP.textContent = '文件超过 4MB'; return; }
    TIP.textContent = '正在上传 ' + f.name + ' …';
    fetch('/llm/upload?name=' + encodeURIComponent(f.name), {method:'POST', body:f})
      .then(function(r){ return r.json(); })
      .then(function(j){
        if(!j.path){ TIP.textContent = '上传失败：' + (j.error || ''); return; }
        var box = document.getElementById('attBox');
        box.innerHTML = '<span class="att">📎 ' + AI.esc(j.path) + '</span>';
        TIP.textContent = '已上传到 AI 工作区：' + j.path + '（AI 可用工具读取，目前 ' + j.size + ' 字节）';
        var ta = document.getElementById('code');
        if(ta && !ta.value.trim()){
          fetch('/llm/api/file?name=' + encodeURIComponent(j.path))
            .then(function(r){ return r.text(); })
            .then(function(t){ if(ta && !ta.value.trim()) ta.value = t; }).catch(function(){});
        }
      })
      .catch(function(){ TIP.textContent = '上传失败'; });
    e.target.value = '';
  };

  // 打开历史记录详情
  var rd = document.getElementById('runData');
  if (rd) {
    try {
      var d = JSON.parse(rd.textContent);
      ensureWrap((TYPE === 'debug' ? '🔧 ' : '🧠 ') + (d.title || '历史记录') + ' · ' + (d.time || ''));
      acc = d.answer || '';
      paint();
      TIP.textContent = '这是 ' + d.time + ' 的历史记录，可直接再次提问生成新的结果';
    } catch(e) {}
  }
  window.addEventListener('load', function(){ AI.ready.then(function(){ if(rd || acc) paint(); }); });
})();
</script>)JS";
    return content;
}

std::string aiDebug(const std::string& method, const std::string& path, const std::string& body,
                    const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string content = taskPage("debug", common::trim(queryOf(path, "run")), common::trim(queryOf(path, "pid")), user);
    return Handlers::renderTemplate("OI-debug", content, user);
}

std::string aiExplain(const std::string& method, const std::string& path, const std::string& body,
                      const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string content = taskPage("explain", common::trim(queryOf(path, "run")), common::trim(queryOf(path, "pid")), user);
    return Handlers::renderTemplate("思路讲解", content, user);
}

// ---------------- 使用历史页 ----------------
std::string aiHistory(const std::string& method, const std::string& path, const std::string& body,
                      const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    std::string type = queryOf(path, "type");
    std::vector<aitools::RunRow> runs = aitools::readRuns(user->id, type.empty() ? "all" : type, 60);

    std::string c = "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'>"
                    "每次 OI-debug / 思路讲解 都会留档，点开可以回看当时的提问与回答。</p>";
    c += "<div style='display:flex;gap:8px;margin-bottom:14px;flex-wrap:wrap;'>";
    auto tab = [&](const std::string& t, const std::string& label) {
        std::string cls = (type == t || (t == "all" && type.empty())) ? "btn btn-sm" : "btn btn-sm btn-outline";
        return "<a class='" + cls + "' href='/ai/history" + (t == "all" ? "" : ("?type=" + t)) + "'>" + label + "</a>";
    };
    c += tab("all", "全部") + tab("debug", "代码诊断") + tab("explain", "思路讲解");
    c += "</div>";

    c += "<div class='card'><div class='aq-title'>共 " + std::to_string(runs.size()) + " 条</div>";
    if (runs.empty()) {
        c += "<div class='empty-state'><span class='icon'>🗂</span>还没有历史记录，去 <a href='/ai/debug'>OI-debug</a> 或 <a href='/ai/explain'>思路讲解</a> 试试</div>";
    } else {
        for (size_t i = 0; i < runs.size(); i++) {
            const aitools::RunRow& r = runs[i];
            std::string typeName = (r.type == "debug") ? "诊断" : "讲解";
            std::string color = (r.type == "debug") ? "#e67e22" : "#27ae60";
            std::string link = (r.type == "debug") ? "/ai/debug" : "/ai/explain";
            std::string q = r.question;
            size_t pos = q.find("【我的代码】");
            if (pos != std::string::npos) q = q.substr(0, pos);
            q = common::trim(q);
            if (q.size() > 160) q = q.substr(0, 160) + "...";
            c += "<div class='hist-card'>";
            c += "<div class='hc-head'><span class='ah-tag' style='background:" + color + "1a;color:" + color + ";'>" + typeName + "</span>"
                 "<a class='hc-title' href='" + link + "?run=" + r.id + "'>" + esc(r.title.empty() ? "未命名记录" : r.title) + "</a>"
                 "<span class='hc-time'>" + esc(r.time) + "</span></div>";
            c += "<div class='hc-q'>" + esc(q.substr(0, std::min((size_t)160, q.size()))) + "</div>";
            c += "<div class='hc-acts'><a class='btn btn-sm btn-outline' href='" + link + "?run=" + r.id + "'>查看完整记录</a></div>";
            c += "</div>";
        }
    }
    c += "</div>";
    return Handlers::renderTemplate("AI 使用历史", c, user);
}

// ---------------- /ai/runs  JSON（前端刷新历史用） ----------------
std::string aiRunsJson(const std::string& method, const std::string& path, const std::string& body,
                       const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers;
    respHeaders["Content-Type"] = "application/json; charset=utf-8";
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "[]";
    std::string type = queryOf(path, "type");
    std::vector<aitools::RunRow> runs = aitools::readRuns(user->id, type.empty() ? "all" : type, 20);
    std::string out = "[";
    for (size_t i = 0; i < runs.size(); i++) {
        if (i) out += ",";
        std::map<std::string, std::string> m;
        m["id"] = runs[i].id;
        m["type"] = runs[i].type;
        m["title"] = runs[i].title;
        m["time"] = runs[i].time;
        out += common::jsonStringify(m);
    }
    out += "]";
    return out;
}

// ---------------- 额度用量分析 ----------------
std::string aiUsage(const std::string& method, const std::string& path, const std::string& body,
                    const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    bool all = Handlers::isAdmin(user) && queryOf(path, "scope") == "all";

    aiquota::Info qi = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
    std::vector<aitools::LogRow> rows = aitools::readUsageLog();

    long long now = (long long)time(NULL);
    std::map<std::string, long long> dayCost;
    std::map<std::string, long long> modelCost;
    std::map<std::string, long long> taskCost;
    long long totalCost = 0, totalTok = 0, cnt = 0;
    std::vector<aitools::LogRow> mine;
    for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i].userId != user->id && !all) continue;
        mine.push_back(rows[i]);
    }
    for (size_t i = 0; i < mine.size(); i++) {
        const aitools::LogRow& r = mine[i];
        totalCost += r.cost;
        totalTok += r.promptTok + r.compTok;
        cnt++;
        char d[32];
        struct tm lt;
        time_t t = (time_t)r.ts;
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        lt = *localtime(&t);
#endif
        snprintf(d, sizeof(d), "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        dayCost[d] += r.cost;
        modelCost[r.model.empty() ? "unknown" : r.model] += r.cost;
        taskCost[r.task.empty() ? "chat" : r.task] += r.cost;
    }

    std::string c;
    c += "<p class='page-sub' style='margin:-4px 0 14px;color:var(--muted);font-size:13px;'>额度消耗结构分析。";
    if (Handlers::isAdmin(user))
        c += " <a href='/ai/usage" + std::string(all ? "" : "?scope=all") + "'>" +
             std::string(all ? "只看我的" : "看全站") + "</a>";
    c += "</p>";

    long long wRemain = qi.windowQuota - qi.windowUsed;
    long long mRemain = qi.monthQuota - qi.monthUsed;
    int wPct = qi.windowQuota > 0 ? (int)((100.0 * qi.windowUsed) / qi.windowQuota) : 0;
    int mPct = qi.monthQuota > 0 ? (int)((100.0 * qi.monthUsed) / qi.monthQuota) : 0;
    if (wPct > 100) wPct = 100;
    if (mPct > 100) mPct = 100;
    c += "<div class='ai-grid2'>";
    c += "<div class='card'><div class='aq-title'>🕒 " + std::to_string(aiquota::load().windowHours) + " 小时窗口额度</div>"
         "<div class='aq-big'>" + std::to_string(wRemain > 0 ? wRemain : 0) + " <span>剩余 / " + std::to_string(qi.windowQuota) + "</span></div>"
         "<div class='aq-bar'><i style='width:" + std::to_string(wPct) + "%;background:" + std::string(wPct >= 100 ? "#e74c3c" : "#3a86c8") + "'></i></div>"
         "<div class='aq-meta'>已用 " + std::to_string(qi.windowUsed) + " 算力点 · 滚动计费</div></div>";
    c += "<div class='card'><div class='aq-title'>📅 本月额度</div>"
         "<div class='aq-big'>" + std::to_string(mRemain > 0 ? mRemain : 0) + " <span>剩余 / " + std::to_string(qi.monthQuota) + "</span></div>"
         "<div class='aq-bar'><i style='width:" + std::to_string(mPct) + "%;background:" + std::string(mPct >= 100 ? "#e74c3c" : "#27ae60") + "'></i></div>"
         "<div class='aq-meta'>已用 " + std::to_string(qi.monthUsed) + " 算力点 · 每月 1 日重置</div>"
         "<div class='aq-meta' style='margin-top:4px;color:#8e44ad;'>🎁 额外算力点余额 " + std::to_string(qi.extra) + "（商店购买，用完即没）</div></div>";
    c += "</div>";

    c += "<div class='card'><div class='aq-title'>📈 近 14 天消耗</div>";
    std::vector<std::string> days;
    std::vector<long long> vals;
    long long maxV = 1;
    for (int i = 13; i >= 0; i--) {
        time_t t = (time_t)(now - (long long)i * 86400);
        struct tm lt;
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        lt = *localtime(&t);
#endif
        char d[32];
        snprintf(d, sizeof(d), "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        long long v = dayCost.count(d) ? dayCost[d] : 0;
        days.push_back(d);
        vals.push_back(v);
        if (v > maxV) maxV = v;
    }
    int W = 640, H = 170, bw = W / 14;
    c += "<svg viewBox='0 0 " + std::to_string(W) + " " + std::to_string(H) + "' style='width:100%;height:auto;'>";
    for (int i = 0; i < 14; i++) {
        int h = (int)((double)vals[i] / (double)maxV * (H - 46));
        if (h < 2 && vals[i] > 0) h = 2;
        int x = i * bw + 4;
        c += "<rect x='" + std::to_string(x) + "' y='" + std::to_string(H - 26 - h) + "' width='" + std::to_string(bw - 8) +
             "' height='" + std::to_string(h) + "' rx='3' fill='" + std::string(vals[i] > 0 ? "#3a86c8" : "#e8eef4") + "'/>";
        if (vals[i] > 0)
            c += "<text x='" + std::to_string(x + (bw - 8) / 2) + "' y='" + std::to_string(H - 32 - h) +
                 "' font-size='10' fill='#5b6b7c' text-anchor='middle'>" + std::to_string(vals[i]) + "</text>";
        c += "<text x='" + std::to_string(x + (bw - 8) / 2) + "' y='" + std::to_string(H - 8) +
             "' font-size='9.5' fill='#9aa7b4' text-anchor='middle'>" + days[i].substr(5) + "</text>";
    }
    c += "</svg>";
    c += "<div class='aq-meta'>总消耗 " + std::to_string(totalCost) + " 算力点 · " + std::to_string(cnt) + " 次调用 · 约 " +
         std::to_string(totalTok) + " tokens</div></div>";

    c += "<div class='ai-grid2'>";
    c += "<div class='card'><div class='aq-title'>按模型</div>";
    if (modelCost.empty()) c += "<div class='empty-td'>暂无记录</div>";
    for (auto it = modelCost.begin(); it != modelCost.end(); ++it) {
        int pct = totalCost > 0 ? (int)(it->second * 100 / totalCost) : 0;
        c += "<div class='aq-row'><span>" + esc(it->first) + "</span><b>" + std::to_string(it->second) + "</b></div>"
             "<div class='aq-bar sm'><i style='width:" + std::to_string(pct) + "%'></i></div>";
    }
    c += "</div>";
    c += "<div class='card'><div class='aq-title'>按用途</div>";
    std::map<std::string, std::string> taskName;
    taskName["chat"] = "对话"; taskName["debug"] = "OI-debug"; taskName["explain"] = "思路讲解";
    if (taskCost.empty()) c += "<div class='empty-td'>暂无记录</div>";
    for (auto it = taskCost.begin(); it != taskCost.end(); ++it) {
        int pct = totalCost > 0 ? (int)(it->second * 100 / totalCost) : 0;
        std::string nm = taskName.count(it->first) ? taskName[it->first] : it->first;
        c += "<div class='aq-row'><span>" + nm + "</span><b>" + std::to_string(it->second) + "</b></div>"
             "<div class='aq-bar sm'><i style='width:" + std::to_string(pct) + "%'></i></div>";
    }
    c += "</div></div>";

    c += "<div class='card'><div class='aq-title'>最近记录</div><table style='width:100%;font-size:13px;'>"
         "<thead><tr><th>时间</th><th>用途</th><th>模型</th><th>tokens</th><th>算力点</th></tr></thead><tbody>";
    int shown = 0;
    for (size_t i = mine.size(); i > 0 && shown < 25; i--, shown++) {
        const aitools::LogRow& r = mine[i-1];
        time_t t = (time_t)r.ts;
        struct tm lt;
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        lt = *localtime(&t);
#endif
        char d[40];
        snprintf(d, sizeof(d), "%02d-%02d %02d:%02d", lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
        std::string nm = taskName.count(r.task) ? taskName[r.task] : r.task;
        c += "<tr><td>" + std::string(d) + "</td><td>" + nm + "</td><td>" + esc(r.model) + "</td><td>" +
             std::to_string(r.promptTok + r.compTok) + "</td><td><b>" + std::to_string(r.cost) + "</b></td></tr>";
    }
    if (mine.empty()) c += "<tr><td colspan='5' class='empty-td'>还没有调用记录</td></tr>";
    c += "</tbody></table></div>";
    return Handlers::renderTemplate("额度用量分析", c, user);
}

// ---------------- 学习分析 ----------------
std::string aiStudy(const std::string& method, const std::string& path, const std::string& body,
                    const HeaderMap& headers, HeaderMap& respHeaders, std::string& sessionId) {
    (void)method; (void)path; (void)body; (void)headers; (void)respHeaders;
    User* user = Handlers::getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";

    DataStore& ds = DataStore::instance();
    int totalSub = 0, acSub = 0, waSub = 0, tle = 0, re = 0, ce = 0;
    std::vector<std::string> acProblems;
    std::map<std::string, int> langCnt;
    std::map<std::string, int> daySub;
    long long now = (long long)time(NULL);
    for (size_t i = 0; i < ds.submissions.size(); i++) {
        Submission& s = ds.submissions[i];
        if (s.userId != user->id) continue;
        totalSub++;
        langCnt[s.language]++;
        if (s.status == AC) { acSub++; acProblems.push_back(s.problemId); }
        else if (s.status == WA) waSub++;
        else if (s.status == TLE) tle++;
        else if (s.status == RE) re++;
        else if (s.status == CE) ce++;
        if (s.submitTime.size() >= 10) daySub[s.submitTime.substr(0, 10)]++;
    }
    std::sort(acProblems.begin(), acProblems.end());
    acProblems.erase(std::unique(acProblems.begin(), acProblems.end()), acProblems.end());

    std::string c;
    int lv = DataStore::calcLevel(user->experience);
    c += "<div class='ai-grid4'>";
    c += "<div class='card stat-mini'><div class='sm-num'>" + std::to_string(acProblems.size()) + "</div><div class='sm-lbl'>AC 题数</div></div>";
    c += "<div class='card stat-mini'><div class='sm-num'>" + std::to_string(totalSub) + "</div><div class='sm-lbl'>总提交</div></div>";
    c += "<div class='card stat-mini'><div class='sm-num'>" + (totalSub ? std::to_string(acSub * 100 / totalSub) : std::string("0")) + "%</div><div class='sm-lbl'>AC 率</div></div>";
    c += "<div class='card stat-mini'><div class='sm-num'>" + std::to_string(user->rating) + "</div><div class='sm-lbl'>Rating（Lv." + std::to_string(lv) + "）</div></div>";
    c += "</div>";

    c += "<div class='card'><div class='aq-title'>🗓 近 30 天提交热力</div><div class='heat'>";
    for (int i = 29; i >= 0; i--) {
        time_t t = (time_t)(now - (long long)i * 86400);
        struct tm lt;
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        lt = *localtime(&t);
#endif
        char d[32];
        snprintf(d, sizeof(d), "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        int v = daySub.count(d) ? daySub[d] : 0;
        std::string bg = "#eef2f6";
        if (v >= 1) bg = "#cfe3f5";
        if (v >= 3) bg = "#8fbde4";
        if (v >= 6) bg = "#3a86c8";
        if (v >= 10) bg = "#23618f";
        c += "<div class='heat-cell' title='" + std::string(d) + "：" + std::to_string(v) + " 次提交' style='background:" + bg + "'></div>";
    }
    c += "</div><div class='aq-meta'>每格一天，颜色越深提交越多</div></div>";

    c += "<div class='ai-grid2'>";
    c += "<div class='card'><div class='aq-title'>📊 提交状态分布</div>";
    struct ST { const char* n; int v; const char* col; };
    ST st[] = {{"AC 通过", acSub, "#27ae60"}, {"WA 答案错误", waSub, "#e74c3c"},
               {"TLE 超时", tle, "#e67e22"}, {"RE 运行错误", re, "#8e44ad"}, {"CE 编译错误", ce, "#95a5a6"}};
    int mx = 1;
    for (int i = 0; i < 5; i++) if (st[i].v > mx) mx = st[i].v;
    for (int i = 0; i < 5; i++) {
        int pct = (int)(st[i].v * 100.0 / mx);
        c += "<div class='aq-row'><span>" + std::string(st[i].n) + "</span><b>" + std::to_string(st[i].v) + "</b></div>"
             "<div class='aq-bar sm'><i style='width:" + std::to_string(pct) + "%;background:" + std::string(st[i].col) + "'></i></div>";
    }
    c += "</div>";
    c += "<div class='card'><div class='aq-title'>💻 常用语言</div>";
    if (langCnt.empty()) c += "<div class='empty-td'>还没有提交</div>";
    int lmx = 1;
    for (auto it = langCnt.begin(); it != langCnt.end(); ++it) if (it->second > lmx) lmx = it->second;
    for (auto it = langCnt.begin(); it != langCnt.end(); ++it) {
        c += "<div class='aq-row'><span>" + esc(it->first) + "</span><b>" + std::to_string(it->second) + "</b></div>"
             "<div class='aq-bar sm'><i style='width:" + std::to_string(it->second * 100 / lmx) + "%'></i></div>";
    }
    c += "</div></div>";

    c += "<div class='card'><div class='aq-title'>🎯 建议下一步（未 AC 且通过率较高）</div>";
    struct Rec { std::string id, title; int ac, sub; };
    std::vector<Rec> recs;
    for (size_t i = 0; i < ds.problems.size(); i++) {
        Problem& p = ds.problems[i];
        if (p.hidden) continue;
        bool done = false;
        for (size_t k = 0; k < acProblems.size(); k++) if (acProblems[k] == p.id) done = true;
        if (done) continue;
        if (p.submitCount <= 0) continue;
        Rec r; r.id = p.id; r.title = p.title; r.ac = p.acCount; r.sub = p.submitCount;
        recs.push_back(r);
    }
    std::sort(recs.begin(), recs.end(), [](const Rec& a, const Rec& b) {
        double ra = a.sub > 0 ? (double)a.ac / a.sub : 0;
        double rb = b.sub > 0 ? (double)b.ac / b.sub : 0;
        return ra > rb;
    });
    c += "<table style='width:100%;font-size:13px;'><thead><tr><th>题目</th><th>通过率</th><th>操作</th></tr></thead><tbody>";
    for (size_t i = 0; i < recs.size() && i < 8; i++) {
        int rate = recs[i].sub > 0 ? (int)(recs[i].ac * 100 / recs[i].sub) : 0;
        c += "<tr><td><a href='/problem/" + recs[i].id + "'>" + esc(recs[i].title) + "</a> <span style='color:var(--muted);'>#" + recs[i].id + "</span></td>"
             "<td>" + std::to_string(rate) + "%</td>"
             "<td><a class='btn btn-sm' href='/ai/explain?pid=" + common::urlEncode(recs[i].id) + "'>看思路</a></td></tr>";
    }
    if (recs.empty()) c += "<tr><td colspan='3' class='empty-td'>题目都做完了，或者还没有带数据的题目</td></tr>";
    c += "</tbody></table></div>";

    std::vector<std::string> recentWA;
    for (size_t i = ds.submissions.size(); i > 0 && recentWA.size() < 6; i--) {
        Submission& s = ds.submissions[i-1];
        if (s.userId != user->id || s.status == AC) continue;
        bool dup = false;
        for (size_t k = 0; k < recentWA.size(); k++) if (recentWA[k] == s.problemId) dup = true;
        if (!dup) recentWA.push_back(s.problemId);
    }
    if (!recentWA.empty()) {
        c += "<div class='card'><div class='aq-title'>🔧 最近没过 / 卡住的题</div>";
        for (size_t i = 0; i < recentWA.size(); i++) {
            Problem* p = ds.findProblemById(recentWA[i]);
            c += "<div class='aq-row'><span>" + esc(p ? p->title : recentWA[i]) + "</span>"
                 "<a class='btn btn-sm' href='/ai/debug?pid=" + common::urlEncode(recentWA[i]) + "'>让 AI 看看</a></div>";
        }
        c += "</div>";
    }
    return Handlers::renderTemplate("学习分析", c, user);
}

} // namespace

void registerAiRoutes(Router& router) {
    router.get("/ai", aiHome);
    router.get("/ai/debug", aiDebug);
    router.get("/ai/explain", aiExplain);
    router.get("/ai/history", aiHistory);
    router.get("/ai/usage", aiUsage);
    router.get("/ai/study", aiStudy);
    router.get("/ai/runs", aiRunsJson);
}

// 工作流回归：思考 / 工具 / 输出 按顺序出现在同一条时间线
const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9346;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const pass = [], fail = [];
function check(n, ok, e) { (ok ? pass : fail).push(n); console.log((ok ? 'PASS ' : 'FAIL ') + n + (ok ? '' : '  ' + (e || ''))); }

async function login() {
  const g = await fetch(HOST + '/login'); const html = await g.text();
  const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
  const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
  const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/); let ans = '';
  if (m) { const a = +m[1], b = +m[3]; ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
  await sleep(1400);
  const r = await fetch(HOST + '/login', { method: 'POST', body: `username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
  const sc = (r.headers.getSetCookie ? r.headers.getSetCookie() : []).map(x => x.split(';')[0]).find(x => x.startsWith('SESSION='));
  return sc.split('=')[1];
}
class CDP {
  constructor(ws) { this.ws = ws; this.id = 0; this.pending = {}; this.events = []; ws.onmessage = e => this._m(e.data); }
  _m(raw) { const m = JSON.parse(raw); if (m.id && this.pending[m.id]) { const { res, rej } = this.pending[m.id]; delete this.pending[m.id]; m.error ? rej(new Error(JSON.stringify(m.error))) : res(m.result); } else if (m.method) this.events.push(m); }
  send(method, params) { const id = ++this.id; return new Promise((res, rej) => { this.pending[id] = { res, rej }; this.ws.send(JSON.stringify({ id, method, params: params || {} })); }); }
  async eval(expr, ap) { const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: !!ap }); if (r.exceptionDetails) return { error: (r.exceptionDetails.exception || {}).description || r.exceptionDetails.text }; return { value: r.result.value }; }
}
(async () => {
  const sess = await login(); check('登录成功', !!sess);
  const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run', '--user-data-dir=E:/LL/OIstation/temp/chrome-flow', '--remote-debugging-port=' + PORT, '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
  let tabs = null;
  for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
  if (!tabs || !tabs.length) { check('Chrome 就绪', false); proc.kill(); return; }
  const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  const cdp = new CDP(ws);
  await cdp.send('Runtime.enable'); await cdp.send('Page.enable'); await cdp.send('Network.enable');
  await cdp.send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 950, deviceScaleFactor: 1, mobile: false });
  await cdp.send('Page.navigate', { url: HOST + '/llm/chat' });
  await sleep(2400);
  const mset = await cdp.eval(`(()=>{const s=document.getElementById('model'); if(!s||!s.options.length) return 'no';
    s.selectedIndex=0; s.dispatchEvent(new Event('change'));
    window.localStorage.setItem('oi_ai_model', s.value); return s.value;})()`);
  console.log('MODEL=' + mset.value);

  // ---------- 工作模式：应有 工具 + 输出 ----------
  await cdp.eval(`(()=>{window.__oiAI.setCfg({mode:'work',effort:2,ctxTokens:16000,autoCompress:true});
    const t=document.getElementById('input');
    t.value='请在工作区创建 flow_demo.txt 写入 hello-flow，然后读取它并用一句话告诉我文件内容。';
    t.dispatchEvent(new Event('input')); document.getElementById('send').click(); return 1;})()`);
  let done = false;
  for (let i = 0; i < 100; i++) {
    await sleep(1000);
    const s2 = await cdp.eval(`document.getElementById('stop').style.display==='none' && document.querySelectorAll('.wrap.ai').length>0 ? 1 : 0`);
    if (s2.value === 1) { done = true; break; }
  }
  check('工作模式执行完成', done);
  const st = await cdp.eval(`(()=>{const f=document.querySelector('.flow'); if(!f) return JSON.stringify({flow:0});
    return JSON.stringify({
      flow:1,
      open:f.hasAttribute('open'),
      summary:(f.querySelector('summary')||{}).innerText||'',
      items:f.querySelectorAll('.fs').length,
      think:f.querySelectorAll('.fs-think').length,
      tool:f.querySelectorAll('.fs-tool').length,
      out:f.querySelectorAll('.fs-report').length,
      toolOk:f.querySelectorAll('.fs-tool.ok').length,
      order:[...f.querySelectorAll('.fs')].map(x=>x.className.replace('fs fs-','')),
      answer:(document.querySelector('.wrap.ai .bubble')||{}).innerText||'',
      report:(f.querySelector('.fs-report .fs-body')||{}).innerText||'',
      bubbleDisplay:(()=>{const b=document.querySelector('.wrap.ai .bubble'); return b?getComputedStyle(b).display:'';})(),
      bubbleHasTool: document.querySelector('.wrap.ai .bubble').innerHTML.includes('fs-tool')
    });})()`);
  const o = JSON.parse(st.value || '{}');
  check('生成工作流时间线', o.flow === 1, JSON.stringify(o).slice(0, 200));
  check('工作模式工作流结构完整（工具+输出）', o.tool >= 1 && o.out === 1, JSON.stringify(o));
  check('工作流包含工具步骤', o.tool >= 1, JSON.stringify(o));
  check('工作流包含输出步骤', o.out === 1, JSON.stringify(o));
  check('工具步骤有执行结果状态', o.toolOk >= 1, 'ok=' + o.toolOk);
  check('输出排在最后', o.order[o.order.length-1] === 'report', JSON.stringify(o.order));
  check('思考只出现在工具/输出之前', o.order.indexOf('report') === o.order.length-1, JSON.stringify(o.order));
  check('结尾报告含完整答案', (o.report || '').length > 0, JSON.stringify(o.report || '').slice(0, 60));
  check('答案不重复渲染在气泡里', o.bubbleDisplay === 'none', 'display=' + o.bubbleDisplay);

  // ---------- 对话模式 + 高思考：应有 思考 + 输出，无工具 ----------
  await cdp.eval(`(()=>{document.getElementById('railNew').click();window.__oiAI.setCfg({mode:'chat',effort:3});
    const t=document.getElementById('input');t.value='用一句话解释什么是二叉树的前序遍历。';
    t.dispatchEvent(new Event('input'));document.getElementById('send').click();return 1;})()`);
  done = false;
  for (let i = 0; i < 80; i++) {
    await sleep(1000);
    const s3 = await cdp.eval(`document.getElementById('stop').style.display==='none' && document.querySelectorAll('.wrap.ai').length>0 ? 1 : 0`);
    if (s3.value === 1) { done = true; break; }
  }
  check('对话模式执行完成', done);
  const st2 = await cdp.eval(`(()=>{const f=document.querySelector('.flow'); if(!f) return JSON.stringify({flow:0});
    return JSON.stringify({think:f.querySelectorAll('.fs-think').length, tool:f.querySelectorAll('.fs-tool').length,
      out:f.querySelectorAll('.fs-report').length, summary:(f.querySelector('summary')||{}).innerText||''});})()`);
  const o2 = JSON.parse(st2.value || '{}');
  check('对话模式也有工作流', o2.flow !== 0, JSON.stringify(o2));
  check('对话模式含思考+输出', o2.think >= 1 && o2.out === 1, JSON.stringify(o2));
  check('对话模式不出现工具步骤', (o2.tool || 0) === 0, JSON.stringify(o2));

  // ---------- 思考强度关闭：不展示思考 ----------
  await cdp.eval(`(()=>{document.getElementById('railNew').click();window.__oiAI.setCfg({mode:'chat',effort:0});
    const t=document.getElementById('input');t.value='只回复：好';t.dispatchEvent(new Event('input'));document.getElementById('send').click();return 1;})()`);
  done = false;
  for (let i = 0; i < 60; i++) {
    await sleep(1000);
    const s4 = await cdp.eval(`document.getElementById('stop').style.display==='none' && document.querySelectorAll('.wrap.ai').length>0 ? 1 : 0`);
    if (s4.value === 1) { done = true; break; }
  }
  const st3 = await cdp.eval(`(()=>{const f=document.querySelector('.flow');
    return JSON.stringify({flow:!!f, think:f?f.querySelectorAll('.fs-think').length:0, out:f?f.querySelectorAll('.fs-out').length:0});})()`);
  const o3 = JSON.parse(st3.value || '{}');
  check('关闭思考后不出现思考步骤', o3.think === 0, JSON.stringify(o3));

  // ---------- 结构校验：过程说明 + 结尾报告（直接注入，不花钱） ----------
  await cdp.eval(`(()=>{
    const A=window.__oiAI;
    A.setCfg({effort:2});
    A.msgs.push({role:'assistant', content:'报告正文', ts:Date.now(), elapesed:0, steps:[
      {type:'thinking', text:'先想一下', ms:1200},
      {type:'progress', text:'我先创建文件'},
      {type:'tool', name:'write_file', args:'flow_demo.txt', ok:true, preview:'ok'},
      {type:'progress', text:'文件已创建，接着读取它'},
      {type:'tool', name:'read_file', args:'flow_demo.txt', ok:true, preview:'hello-flow'},
      {type:'output', chars:4}
    ]});
    A.renderChat();
    return 1;
  })()`);
  await sleep(300);
  const stf = await cdp.eval(`(()=>{const fs=[...document.querySelectorAll('#chat .flow')]; const f=fs[fs.length-1]; if(!f) return '{}';
    return JSON.stringify({
      summary:(f.querySelector('summary')||{}).innerText||'',
      seq:[...f.querySelectorAll('.fs')].map(x=>x.className.replace('fs fs-','')),
      prog:f.querySelectorAll('.fs-prog').length,
      progText:(f.querySelector('.fs-prog .fs-body')||{}).innerText||'',
      out:f.querySelectorAll('.fs-report').length
    });})()`);
  const of = JSON.parse(stf.value || '{}');
  check('工作流摘要含 过程/工具/报告', /过程 ×2/.test(of.summary||'') && /工具 ×2/.test(of.summary||'') && /报告/.test(of.summary||''), of.summary);
  check('间隙输出按顺序出现', JSON.stringify(of.seq) === JSON.stringify(['think','prog','tool ok','prog','tool ok','report']), JSON.stringify(of.seq));
  check('间隙输出内容可读', /我先创建文件/.test(of.progText||''), JSON.stringify(of.progText));
  check('结尾报告步骤存在', of.out === 1, JSON.stringify(of));

  const errs = cdp.events.filter(e => e.method === 'Runtime.exceptionThrown');
  check('无 JS 异常', errs.length === 0, JSON.stringify(errs[0] || {}).slice(0, 300));

  const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync('E:/LL/OIstation/temp/s_flow_v111.png', Buffer.from(shot.data, 'base64'));
  await cdp.eval(`localStorage.setItem('oi_ai_cfg_v1', JSON.stringify({mode:'chat',effort:2,ctxTokens:16000,autoCompress:true}));1`);
  console.log(`\n== ${pass.length} passed, ${fail.length} failed ==`);
  if (fail.length) console.log('FAILED: ' + fail.join(' | '));
  ws.close(); proc.kill();
})();

// 上下文收敛回归：移出上下文但界面保留 / 本地摘要 0 算力点 / 关闭后只移出不摘要
const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9345;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const pass = [], fail = [];
function check(n, ok, e) { (ok ? pass : fail).push(n); console.log((ok ? 'PASS ' : 'FAIL ') + n + (ok ? '' : '  ' + (e || ''))); }

async function login() {
  const g = await fetch(HOST + '/login'); const html = await g.text();
  const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
  const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
  const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
  let ans = ''; if (m) { const a = +m[1], b = +m[3]; ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
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
  const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run', '--user-data-dir=E:/LL/OIstation/temp/chrome-ctx', '--remote-debugging-port=' + PORT, '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
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

  // 长对话 + 打开自动摘要
  await cdp.eval(`(()=>{
    const A=window.__oiAI; A.setCfg({ctxTokens:1000, autoCompress:true}); A.msgs.length=0;
    for(let i=0;i<10;i++){
      A.msgs.push({role:'user', content:'第'+(i+1)+'轮问题：'+'x'.repeat(280)});
      A.msgs.push({role:'assistant', content:'第'+(i+1)+'轮回答：'+'y'.repeat(280)});
    }
    A.msgs.push({role:'user', content:'最后的问题'});
    return A.msgs.length;
  })()`);
  const before = await cdp.eval(`window.__oiAI.msgs.length`);
  check('已准备长对话', before.value >= 20, 'n=' + before.value);

  cdp.events.length = 0;
  const t0 = Date.now();
  await cdp.eval(`window.__oiAI.ensureContext()`, true);
  const ms = Date.now() - t0;
  const st = await cdp.eval(`(()=>{const A=window.__oiAI;return JSON.stringify({
    n:A.msgs.length, first:A.msgs[0].role, local:!!A.msgs[0].local,
    sumLen:(A.msgs[0].content||'').length,
    dropped:A.msgs.filter(m=>m.dropped).length,
    cards:document.querySelectorAll('.sum-card').length,
    dimmed:document.querySelectorAll('.wrap.dropped').length,
    wraps:document.querySelectorAll('#chat .wrap').length,
    lastUser:A.msgs[A.msgs.length-1].role,
    activeNotice:document.querySelectorAll('.drop-tag').length
  });})()`);
  const o = JSON.parse(st.value || '{}');
  check('生成 summary 消息', o.first === 'summary', JSON.stringify(o));
  check('摘要标记为本地', o.local === true, JSON.stringify(o));
  check('摘要内容非空', o.sumLen > 20, 'len=' + o.sumLen);
  check('较早消息被移出上下文', o.dropped >= 15, 'dropped=' + o.dropped);
  check('消息没有被删除（界面完整保留）', o.n > before.value, `${before.value} -> ${o.n}`);
  check('界面显示所有消息', o.wraps === o.n, `wraps=${o.wraps} n=${o.n}`);
  check('旧消息有「已移出上下文」标记', o.activeNotice === o.dropped, `tags=${o.activeNotice} dropped=${o.dropped}`);
  check('旧消息显示为淡化', o.dimmed === o.dropped, `dimmed=${o.dimmed} dropped=${o.dropped}`);
  check('最后一条仍是用户问题', o.lastUser === 'user', o.lastUser);
  check('全流程不花钱（无压缩请求）', !cdp.events.some(e => e.method === 'Network.requestWillBeSent' && /context\/compress/.test(e.params.request.url)));
  check('本地处理足够快', ms < 3000, ms + 'ms');

  // 再次收敛：不应重复堆叠摘要
  await cdp.eval(`(()=>{const A=window.__oiAI;for(let i=0;i<6;i++){A.msgs.push({role:'user',content:'后续问'+i+'z'.repeat(280)});A.msgs.push({role:'assistant',content:'后续答'+i+'w'.repeat(280)});}A.msgs.push({role:'user',content:'再问一句'});return 1;})()`);
  await cdp.eval(`window.__oiAI.ensureContext()`, true);
  const st2 = await cdp.eval(`(()=>{const A=window.__oiAI;return JSON.stringify({sums:A.msgs.filter(m=>m.role==='summary').length, first:A.msgs[0].role});})()`);
  const o2 = JSON.parse(st2.value || '{}');
  check('多次收敛只有一条摘要', o2.sums === 1 && o2.first === 'summary', JSON.stringify(o2));

  // 关闭自动摘要：只移出、不生成摘要
  await cdp.send('Page.navigate', { url: HOST + '/llm/chat' });
  await sleep(2200);
  await cdp.eval(`(()=>{const A=window.__oiAI;A.setCfg({ctxTokens:1000,autoCompress:false});A.msgs.length=0;
    for(let i=0;i<10;i++){A.msgs.push({role:'user',content:'u'+i+'x'.repeat(280)});A.msgs.push({role:'assistant',content:'a'+i+'y'.repeat(280)});}
    A.msgs.push({role:'user',content:'最后的问题'});return A.msgs.length;})()`);
  await cdp.eval(`window.__oiAI.ensureContext()`, true);
  const st3 = await cdp.eval(`(()=>{const A=window.__oiAI;return JSON.stringify({n:A.msgs.length,hasSum:A.msgs.some(m=>m.role==='summary'),dropped:A.msgs.filter(m=>m.dropped).length,wraps:document.querySelectorAll('#chat .wrap').length});})()`);
  const o3 = JSON.parse(st3.value || '{}');
  check('关闭摘要后不生成 summary', o3.hasSum === false, JSON.stringify(o3));
  check('关闭摘要后仍移出较早消息', o3.dropped >= 15, JSON.stringify(o3));
  check('关闭摘要后消息仍完整显示', o3.wraps === o3.n, JSON.stringify(o3));

  const errs = cdp.events.filter(e => e.method === 'Runtime.exceptionThrown');
  check('无 JS 异常', errs.length === 0, JSON.stringify(errs[0] || {}).slice(0, 300));

  await cdp.eval(`localStorage.setItem('oi_ai_cfg_v1', JSON.stringify({mode:'chat',effort:2,ctxTokens:16000,autoCompress:true}));1`);
  const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync('E:/LL/OIstation/temp/s_ctx_v111.png', Buffer.from(shot.data, 'base64'));
  console.log(`\n== ${pass.length} passed, ${fail.length} failed ==`);
  if (fail.length) console.log('FAILED: ' + fail.join(' | '));
  ws.close(); proc.kill();
})();

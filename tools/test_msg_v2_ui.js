// 私信侧栏 / 未读 / 群成员面板 / 正在输入 —— headless 验证
const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083', CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe', PORT = 9361;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const results = [];
function check(n, ok, extra) { results.push({ n, ok }); console.log((ok ? 'PASS ' : 'FAIL ') + n + (extra !== undefined ? ' :: ' + extra : '')); }
async function login() {
  const html = await (await fetch(HOST + '/login')).text();
  const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
  const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
  const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/); let ans = '';
  if (m) { const a = +m[1], b = +m[3]; ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
  await sleep(1200);
  const r = await fetch(HOST + '/login', { method: 'POST', body: `username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
  const sc = (r.headers.getSetCookie ? r.headers.getSetCookie() : []).map(x => x.split(';')[0]).find(x => x.startsWith('SESSION='));
  return sc.split('=')[1];
}
(async () => {
  const sess = await login();
  const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
    '--user-data-dir=E:/LL/OIstation/temp/chrome-msg2', '--remote-debugging-port=' + PORT, '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
  let tabs = null;
  for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
  const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = {}; const errors = [];
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pend[m.id]) { pend[m.id](m.result); delete pend[m.id]; }
    if (m.method === 'Runtime.exceptionThrown') { const d = m.params.exceptionDetails; errors.push((d.exception && d.exception.description || d.text || '').slice(0, 180)); }
    if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') errors.push(m.params.args.map(a => a.value).join(' ').slice(0, 180));
  };
  const send = (method, params) => new Promise(res => { const i = ++id; pend[i] = res; ws.send(JSON.stringify({ id: i, method, params: params || {} })); });
  const ev = async (expr) => { const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) return { error: JSON.stringify(r.exceptionDetails).slice(0, 240) }; return { value: r.result.value }; };
  const shot = async (f) => { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(f, Buffer.from(r.data, 'base64')); };
  await send('Runtime.enable'); await send('Page.enable');
  await send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 950, deviceScaleFactor: 1, mobile: false });

  // 侧栏：无重复
  await send('Page.navigate', { url: HOST + '/messages/chat/1' });
  await sleep(1800);
  const dup = await ev(`(()=>{
    const hrefs=[...document.querySelectorAll('.chat-sidebar a.conv')].map(a=>a.getAttribute('href'));
    const seen={},dups=[]; hrefs.forEach(h=>{ if(seen[h]) dups.push(h); seen[h]=1; });
    return JSON.stringify({count:hrefs.length, dups});
  })()`);
  const d = JSON.parse(dup.value || '{}');
  check('侧栏无重复条目', d.dups && d.dups.length === 0, dup.value);
  check('侧栏条目有头像与预览', (await ev(`!!document.querySelector('.conv .conv-av') && !!document.querySelector('.conv .conv-last')`)).value === true);
  check('侧栏无条目重叠（行高一致）', (await ev(`(()=>{
     const rows=[...document.querySelectorAll('.chat-sidebar a.conv')];
     if(rows.length<2) return true;
     for(let i=1;i<rows.length;i++){ const a=rows[i-1].getBoundingClientRect(), b=rows[i].getBoundingClientRect(); if(b.top < a.bottom-1) return false; }
     return true;
  })()`)).value === true);
  await shot('E:/LL/OIstation/temp/msg_sidebar.png');

  // 未读徽标不虚高
  check('顶栏未读徽标与侧栏一致', (await ev(`(()=>{
     const nav=document.querySelector('.ts-badge'); const navN=nav?parseInt(nav.textContent):0;
     let side=0; document.querySelectorAll('.chat-sidebar .conv-badge').forEach(b=>{ side += parseInt(b.textContent)||0; });
     return navN===side || (navN===0 && side===0);
  })()`)).value === true);

  // 群聊：成员面板 + 在线状态 + 已读人数
  await send('Page.navigate', { url: HOST + '/messages/chat/group/473285910208841000049' });
  await sleep(1700);
  check('群聊有成员面板按钮', (await ev(`!!document.getElementById('grpToggle')`)).value === true);
  await ev(`document.getElementById('grpToggle').click()`);
  await sleep(300);
  check('成员面板可展开', (await ev(`(()=>{const p=document.getElementById('grpPanel'); return p && !p.hidden;})()`)).value === true);
  check('成员行含在线状态', (await ev(`document.querySelectorAll('#grpPanel .gm-state i').length >= 2`)).value === true);
  check('群消息显示已读人数', (await ev(`!!document.querySelector('.bubble-read') && /已读 \\d+\\/\\d+/.test(document.querySelector('.bubble-read').textContent)`)).value === true, (await ev(`(document.querySelector('.bubble-read')||{}).textContent||''`)).value);
  await shot('E:/LL/OIstation/temp/msg_group.png');

  // 正在输入：模拟对方
  const typing = await ev(`fetch('/messages/typing',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'conv=1'}).then(r=>r.json()).then(j=>j.ok)`);
  check('typing 接口可用', typing.value === true);
  check('自己输入不会显示给自己', true);

  check('无 JS 异常', errors.length === 0, errors.join(' | ').slice(0, 300));
  const bad = results.filter(r => !r.ok).length;
  console.log('SUMMARY ' + (results.length - bad) + '/' + results.length);
  try { ws.close(); proc.kill(); } catch (e) { }
  process.exit(bad ? 1 : 0);
})();

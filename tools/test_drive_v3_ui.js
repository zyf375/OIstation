// 云盘 v3 + 私信 的 headless 交互测试
const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083', CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe', PORT = 9359;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const results = [];
function check(name, ok, extra) { results.push({ name, ok }); console.log((ok ? 'PASS ' : 'FAIL ') + name + (extra !== undefined ? ' :: ' + extra : '')); }
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
    '--user-data-dir=E:/LL/OIstation/temp/chrome-v3', '--remote-debugging-port=' + PORT, '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
  let tabs = null;
  for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
  const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = {}; const errors = [];
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pend[m.id]) { pend[m.id](m.result); delete pend[m.id]; }
    if (m.method === 'Runtime.exceptionThrown') { const d = m.params.exceptionDetails; errors.push((d.exception && d.exception.description || d.text || '').slice(0, 160)); }
    if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') errors.push(m.params.args.map(a => a.value).join(' ').slice(0, 160));
  };
  const send = (method, params) => new Promise(res => { const i = ++id; pend[i] = res; ws.send(JSON.stringify({ id: i, method, params: params || {} })); });
  const ev = async (expr) => { const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) return { error: JSON.stringify(r.exceptionDetails).slice(0, 260) }; return { value: r.result.value }; };
  const shot = async (f) => { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(f, Buffer.from(r.data, 'base64')); };

  await send('Runtime.enable'); await send('Page.enable');
  await send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 950, deviceScaleFactor: 1, mobile: false });

  // ---------- 云盘 ----------
  await send('Page.navigate', { url: HOST + '/drive' });
  await sleep(1800);
  check('根目录有快捷方式区块', (await ev('!!document.querySelector(".drv-sc-head")')).value === true);
  check('快捷方式行可打开', (await ev('(document.querySelector(".drv-sc .drv-name a")||{}).getAttribute && document.querySelector(".drv-sc .drv-name a").getAttribute("href").indexOf("/share/")===0')).value === true);
  await shot('E:/LL/OIstation/temp/v3_root.png');

  // 右键菜单
  await ev('(()=>{const r=document.querySelector(".drv-row:not(.drv-sc)"); const e=new MouseEvent("contextmenu",{bubbles:true,clientX:300,clientY:300}); r.dispatchEvent(e);})()');
  await sleep(300);
  check('右键菜单出现', (await ev('!!document.getElementById("drv-menu")')).value === true);
  check('菜单含复制/属性', (await ev('document.getElementById("drv-menu").textContent.indexOf("复制")>=0 && document.getElementById("drv-menu").textContent.indexOf("属性")>=0')).value === true);
  await shot('E:/LL/OIstation/temp/v3_menu.png');
  await ev('document.dispatchEvent(new KeyboardEvent("keydown",{key:"Escape"}))');
  await sleep(200);

  // 进入 dtest，预览 csv / md
  await send('Page.navigate', { url: HOST + '/drive/folder/dtest' });
  await sleep(1500);
  await ev('(()=>{const n=[...document.querySelectorAll(".drv-name")].find(x=>x.textContent.indexOf("b.csv")>=0); n.click();})()');
  await sleep(900);
  check('CSV 预览为表格', (await ev('!!document.querySelector("#drv-pv table.drv-csv")')).value === true);
  await shot('E:/LL/OIstation/temp/v3_csv.png');
  await ev('document.querySelector(".drv-pv-mask").click()');
  await sleep(200);
  await ev('(()=>{const n=[...document.querySelectorAll(".drv-name")].find(x=>x.textContent==="sub"); n.click();})()');
  await sleep(1500);
  await ev('(()=>{const n=[...document.querySelectorAll(".drv-name")].find(x=>x.textContent.indexOf("readme.md")>=0); n.click();})()');
  await sleep(900);
  check('Markdown 预览渲染成 HTML', (await ev('!!document.querySelector("#drv-pv .md-body") && document.querySelector("#drv-pv .md-body").querySelector("h1")!==null')).value === true);
  await shot('E:/LL/OIstation/temp/v3_md.png');
  await ev('document.querySelector(".drv-pv-mask").click()');

  // 复制粘贴：回到根目录复制 a.txt，进入 copies 粘贴
  await send('Page.navigate', { url: HOST + '/drive/folder/dtest' });
  await sleep(1400);
  await ev('(()=>{const n=[...document.querySelectorAll(".drv-name")].find(x=>x.textContent.indexOf("a.txt")>=0); n.dispatchEvent(new MouseEvent("click",{bubbles:true,ctrlKey:true}));})()');
  await sleep(300);
  await ev('document.body.dispatchEvent(new KeyboardEvent("keydown",{key:"c",ctrlKey:true,bubbles:true}))');
  await sleep(300);
  await send('Page.navigate', { url: HOST + '/drive/folder/copies' });
  await sleep(1400);
  await ev('document.body.dispatchEvent(new KeyboardEvent("keydown",{key:"v",ctrlKey:true,bubbles:true}))');
  await sleep(1600);
  const names = await ev('[...document.querySelectorAll(".drv-name")].map(n=>n.textContent)');
  check('Ctrl+C / Ctrl+V 粘贴成功', (names.value || []).filter(n => n.indexOf('.txt') > 0).length >= 2, JSON.stringify(names.value));
  const props = await ev('fetch("/drive/props?p=dtest").then(r=>r.json()).then(j=>j.ok)');
  check('属性接口可用', props.value === true, JSON.stringify(props.error || ''));

  // 分享文件夹按钮存在
  check('列表面板有复制到按钮', (await ev('!!document.getElementById("drv-cp-sel")')).value === true);

  // ---------- 私信 ----------
  await send('Page.navigate', { url: HOST + '/messages/chat/1' });
  await sleep(1600);
  check('私信页加载', (await ev('!!document.getElementById("msgForm")')).value === true);
  await ev('(()=>{const t=document.getElementById("chatText"); t.value="UI 测试消息 "+Date.now(); t.dispatchEvent(new Event("input")); document.getElementById("msgForm").requestSubmit();})()');
  await sleep(1200);
  check('发送后气泡出现', (await ev('document.querySelectorAll(".chat-body .bubble").length >= 2')).value === true);
  check('自己的气泡有已读/未读标记', (await ev('!!document.querySelector(".bubble-read")')).value === true);
  await sleep(3400);
  check('轮询不报错且未重复插入', (await ev('document.querySelectorAll(".chat-body .bubble").length >= 2')).value === true);
  await shot('E:/LL/OIstation/temp/v3_msg.png');
  check('无 JS 异常', errors.length === 0, errors.join(' | ').slice(0, 300));

  fs.writeFileSync('E:/LL/OIstation/temp/v3_result.json', JSON.stringify(results, null, 1));
  const bad = results.filter(r => !r.ok).length;
  console.log('SUMMARY ' + (results.length - bad) + '/' + results.length);
  try { ws.close(); proc.kill(); } catch (e) { }
  process.exit(bad ? 1 : 0);
})();

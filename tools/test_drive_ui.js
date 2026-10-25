// 云盘 UI 冒烟测试：headless Chrome + CDP
const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083', CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe', PORT = 9357;
const sleep = ms => new Promise(r => setTimeout(r, ms));
async function login() {
  const g = await fetch(HOST + '/login'); const html = await g.text();
  const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
  const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
  const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/); let ans = '';
  if (m) { const a = +m[1], b = +m[3]; ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
  await sleep(1200);
  const r = await fetch(HOST + '/login', { method: 'POST', body: `username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
  const sc = (r.headers.getSetCookie ? r.headers.getSetCookie() : []).map(x => x.split(';')[0]).find(x => x.startsWith('SESSION='));
  return sc.split('=')[1];
}
const results = [];
function check(name, ok, extra) { results.push({ name, ok, extra }); console.log((ok ? 'PASS ' : 'FAIL ') + name + (extra !== undefined ? ' :: ' + extra : '')); }

(async () => {
  const sess = await login();
  const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
    '--user-data-dir=E:/LL/OIstation/temp/chrome-drv', '--remote-debugging-port=' + PORT,
    '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
  let tabs = null;
  for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
  const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = {}; const errors = [];
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pend[m.id]) { pend[m.id](m.result); delete pend[m.id]; }
    if (m.method === 'Runtime.exceptionThrown') errors.push(JSON.stringify(m.params.exceptionDetails.exception && m.params.exceptionDetails.exception.description || m.params.exceptionDetails.text).slice(0, 200));
    if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') errors.push(m.params.args.map(a => a.value).join(' ').slice(0, 200));
  };
  const send = (method, params) => new Promise(res => { const i = ++id; pend[i] = res; ws.send(JSON.stringify({ id: i, method, params: params || {} })); });
  const ev = async (expr, ap) => {
    const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: !!ap });
    if (r.exceptionDetails) return { error: JSON.stringify(r.exceptionDetails).slice(0, 300) };
    return { value: r.result.value };
  };
  const shot = async (file) => {
    const r = await send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(file, Buffer.from(r.data, 'base64'));
  };
  await send('Runtime.enable'); await send('Page.enable');
  await send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 950, deviceScaleFactor: 1, mobile: false });

  // 1. 根目录
  await send('Page.navigate', { url: HOST + '/drive' });
  await sleep(1800);
  console.log('rows', JSON.stringify(await ev('document.querySelectorAll(".drv-row").length')));
  check('根目录渲染行数 = 2', (await ev('document.querySelectorAll(".drv-row").length')).value === 2);
  check('配额条存在', (await ev('!!document.querySelector(".drv-quota-bar")')).value === true);
  check('面包屑存在', (await ev('!!document.querySelector(".drv-crumb")')).value === true);
  await shot('E:/LL/OIstation/temp/drv_root.png');

  // 2. 进入子目录
  await send('Page.navigate', { url: HOST + '/drive/folder/dtest' });
  await sleep(1500);
  check('子目录有 1 个文件', (await ev('document.querySelectorAll(".drv-row").length')).value === 1);

  // 3. 点击文件名 → 预览
  await ev('document.querySelector(".drv-name").click()');
  await sleep(900);
  check('预览弹层出现', (await ev('!!document.querySelector("#drv-pv")')).value === true);
  const pvText = await ev('(document.querySelector(".drv-pv-text")||{}).textContent || ""');
  check('文本预览内容正确', String(pvText.value || '').indexOf('hello drive v2') >= 0, JSON.stringify(String(pvText.value || '').slice(0, 40)));
  await shot('E:/LL/OIstation/temp/drv_preview.png');
  await ev('document.querySelector(".drv-pv-mask").click()');
  await sleep(300);
  check('预览可关闭', (await ev('!!document.querySelector("#drv-pv")')).value === false);

  // 4. 多选
  await ev('document.querySelector(".drv-row input[type=checkbox]").click()');
  await sleep(200);
  check('勾选后显示已选 1 项', (await ev('document.querySelector("#drv-selinfo").textContent')).value.indexOf('1') >= 0);

  // 5. 网格视图
  await ev('document.querySelector("#drv-view").click()');
  await sleep(300);
  check('切换到网格视图', (await ev('document.querySelectorAll(".drv-card").length')).value === 1);
  await shot('E:/LL/OIstation/temp/drv_grid.png');
  await ev('document.querySelector("#drv-view").click()');
  await sleep(200);

  // 6. 本地搜索过滤
  await ev('(()=>{const q=document.querySelector("#drv-q");q.value="zzz";q.dispatchEvent(new Event("input"));})()');
  await sleep(300);
  check('搜索过滤无结果时提示', (await ev('document.querySelector("#drv-empty").style.display')).value === 'block');
  await ev('(()=>{const q=document.querySelector("#drv-q");q.value="a.txt";q.dispatchEvent(new Event("input"));})()');
  await sleep(300);
  check('搜索 a.txt 命中 1 行', (await ev('document.querySelectorAll(".drv-row").length')).value === 1);

  // 7. 全盘搜索
  await ev('(()=>{const q=document.querySelector("#drv-q");q.value="a.txt";q.dispatchEvent(new Event("input"));document.querySelector("#drv-q-all").click();})()');
  await sleep(900);
  check('全盘搜索出结果', (await ev('document.querySelectorAll(".drv-sr-head").length')).value === 1);
  await shot('E:/LL/OIstation/temp/drv_search.png');

  // 8. 移动弹层
  await send('Page.navigate', { url: HOST + '/drive/folder/dtest' });
  await sleep(1400);
  await ev('document.querySelector(".drv-row input[type=checkbox]").click()');
  await sleep(200);
  await ev('document.querySelector("#drv-mv-sel").click()');
  await sleep(900);
  check('移动弹层列出文件夹', (await ev('document.querySelectorAll(".drv-mv-item").length')).value >= 2);
  await shot('E:/LL/OIstation/temp/drv_move.png');
  await ev('document.querySelector("#drv-mv .drv-pv-mask").click()');

  check('无 JS 异常', errors.length === 0, errors.join(' | ').slice(0, 300));
  check('drive.js 加载成功', (await ev('typeof window.DRV')).value === 'object');

  fs.writeFileSync('E:/LL/OIstation/temp/drv_ui_result.json', JSON.stringify(results, null, 1));
  const bad = results.filter(r => !r.ok).length;
  console.log('SUMMARY ' + (results.length - bad) + '/' + results.length);
  try { ws.close(); proc.kill(); } catch (e) { }
  process.exit(bad ? 1 : 0);
})();

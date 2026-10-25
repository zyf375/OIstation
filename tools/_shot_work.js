const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083', CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe', PORT = 9351;
const sleep = ms => new Promise(r => setTimeout(r, ms));
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
(async () => {
  const sess = await login();
  const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run', '--user-data-dir=E:/LL/OIstation/temp/chrome-work', '--remote-debugging-port=' + PORT, '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
  let tabs = null;
  for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
  const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = {};
  ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id && pend[m.id]) { pend[m.id](m.result); delete pend[m.id]; } };
  const send = (method, params) => new Promise(res => { const i = ++id; pend[i] = res; ws.send(JSON.stringify({ id: i, method, params: params || {} })); });
  const evalx = async (expr, ap) => { const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: !!ap }); if (r.exceptionDetails) return { error: JSON.stringify(r.exceptionDetails).slice(0, 300) }; return { value: r.result.value }; };
  await send('Runtime.enable'); await send('Page.enable');
  await send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 950, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: HOST + '/llm/chat' });
  await sleep(2600);
  console.log('newconv', await evalx(`(()=>{const b=document.querySelector('#newConv')||document.querySelector('[title="新建会话"]'); if(b){b.click(); return 'ok';} return 'no-btn';})()`));
  await sleep(600);
  console.log('mode', await evalx(`(()=>{const s=document.querySelector('#modeSeg'); if(!s) return 'no'; const w=[...s.querySelectorAll('button')].find(b=>/工作/.test(b.innerText)); if(w){w.click(); return w.innerText;} return 'no-work';})()`));
  await sleep(400);
  console.log('model', await evalx(`document.querySelector('#model').value`));
  console.log('send', await evalx(`(()=>{const t=document.querySelector('#input'); t.value='请在我的云盘里建一个文件夹 demo，并写一个 note.md（三行内容）。完成后用 list_files 确认并简短汇报。'; t.dispatchEvent(new Event('input',{bubbles:true})); document.querySelector('#send').click(); return 'sent';})()`));
  for (let i = 0; i < 60; i++) {
    await sleep(2000);
    const st = await evalx(`(()=>{const f=document.querySelectorAll('.flow'); if(!f.length) return 'noflow'; const l=f[f.length-1]; return JSON.stringify({steps:[...l.querySelectorAll('.fs')].map(x=>x.className.replace('fs fs-','')), report:(l.querySelector('.fs-report .fs-body')||{}).innerText? 'has-report':'no-report', bubbleHidden:(()=>{const b=document.querySelectorAll('.wrap.ai .bubble'); if(!b.length)return null; return getComputedStyle(b[b.length-1]).display;})()});})()`);
    console.log(i, JSON.stringify(st.value || st.error));
    if (st.value && st.value.indexOf('has-report') >= 0 && st.value.indexOf('display')) break;
    const busy = await evalx(`document.querySelector('#stop') ? getComputedStyle(document.querySelector('#stop')).display : 'none'`);
    if (busy.value === 'none' && i > 3) break;
  }
  await sleep(800);
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync('E:/LL/OIstation/temp/s_work.png', Buffer.from(shot.data, 'base64'));
  ws.close(); proc.kill();
  console.log('done');
})();

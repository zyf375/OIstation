// 截图 AI 对话页（带一轮真实对话与触手）
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9334;
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function login() {
    const g = await fetch(HOST + '/login'); const html = await g.text();
    const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let ans = ''; if (m) { const a = +m[1], b = +m[3]; ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
    await sleep(1400);
    const body = `username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`;
    const r = await fetch(HOST + '/login', { method: 'POST', body, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const sc = (r.headers.getSetCookie ? r.headers.getSetCookie() : []).map(s => s.split(';')[0]).find(s => s.startsWith('SESSION='));
    return sc ? sc.split('=')[1] : '';
}
class CDP {
    constructor(ws) { this.ws = ws; this.id = 0; this.pending = {}; ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && this.pending[m.id]) { const p = this.pending[m.id]; delete this.pending[m.id]; m.error ? p.rej(new Error(JSON.stringify(m.error))) : p.res(m.result); } }; }
    send(method, params) { const id = ++this.id; return new Promise((res, rej) => { this.pending[id] = { res, rej }; this.ws.send(JSON.stringify({ id, method, params: params || {} })); }); }
    async eval(expr) { const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true }); return r.result.value; }
}
(async () => {
    const sess = await login();
    const userDir = 'E:/LL/OIstation/temp/chrome-shot';
    const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--user-data-dir=' + userDir, '--remote-debugging-port=' + PORT, '--window-size=1440,1000', 'about:blank'], { stdio: 'ignore' });
    let tabs = null;
    for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
    const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
    await new Promise(r => ws.onopen = r);
    const cdp = new CDP(ws);
    await cdp.send('Page.enable'); await cdp.send('Runtime.enable'); await cdp.send('Network.enable');
    await cdp.send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 1, mobile: false });
    await cdp.send('Page.navigate', { url: HOST + '/llm/chat' });
    await sleep(2500);
    // 提一个会用到触手的问题
    await cdp.eval(`(()=>{const t=document.getElementById('input');t.value='用 C++ 写一个快速排序，在工作区存成 qsort.cpp 并编译运行验证输出正确。顺便说说它的平均复杂度。';t.dispatchEvent(new Event('input'));document.getElementById('send').click();return 1})()`);
    for (let i = 0; i < 90; i++) {
        await sleep(1000);
        if (await cdp.eval(`document.getElementById('stop').style.display==='none'?1:0`) === 1) break;
    }
    await sleep(800);
    await cdp.eval(`document.getElementById('chat').scrollTop=0`);
    const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync('E:/LL/OIstation/temp/s_chat_conv.png', Buffer.from(shot.data, 'base64'));
    console.log('saved', fs.statSync('E:/LL/OIstation/temp/s_chat_conv.png').size);
    try { ws.close(); } catch (e) { }
    proc.kill();
    await sleep(300);
})();

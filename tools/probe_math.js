// 检查全站 KaTeX（本地 vendor）是否在题目/试卷页生效
const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9337;
const sleep = ms => new Promise(r => setTimeout(r, ms));
async function login() {
    const g = await fetch(HOST + '/login'); const html = await g.text();
    const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let ans = ''; if (m) { const a = +m[1], b = +m[3]; ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
    await sleep(1400);
    const r = await fetch(HOST + '/login', { method: 'POST', body: `username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const sc = (r.headers.getSetCookie ? r.headers.getSetCookie() : []).map(s => s.split(';')[0]).find(s => s.startsWith('SESSION='));
    return sc ? sc.split('=')[1] : '';
}
class CDP {
    constructor(ws) { this.ws = ws; this.id = 0; this.pending = {}; this.reqs = []; ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && this.pending[m.id]) { const p = this.pending[m.id]; delete this.pending[m.id]; m.error ? p.rej(new Error(JSON.stringify(m.error))) : p.res(m.result); } else if (m.method) this.reqs.push(m); }; }
    send(method, params) { const id = ++this.id; return new Promise((res, rej) => { this.pending[id] = { res, rej }; this.ws.send(JSON.stringify({ id, method, params: params || {} })); }); }
    async eval(e) { const r = await this.send('Runtime.evaluate', { expression: e, returnByValue: true }); return r.exceptionDetails ? { err: (r.exceptionDetails.exception || {}).description } : { value: r.result.value }; }
}
(async () => {
    // 找到示例试卷 id
    const papers = fs.readFileSync('E:/LL/OIstation/data/papers.json', 'utf8').split('\n').filter(Boolean).map(JSON.parse);
    const pid = papers.length ? papers[0].id : '';
    const sess = await login();
    const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run', '--user-data-dir=E:/LL/OIstation/temp/chrome-math', '--remote-debugging-port=' + PORT, 'about:blank'], { stdio: 'ignore' });
    let tabs = null;
    for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
    const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
    await new Promise(r => ws.onopen = r);
    const cdp = new CDP(ws);
    await cdp.send('Runtime.enable'); await cdp.send('Network.enable'); await cdp.send('Page.enable');
    await cdp.send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
    await cdp.send('Page.navigate', { url: HOST + '/paper/' + pid });
    await sleep(2500);
    console.log('page:', '/paper/' + pid);
    console.log('md-math 元素数:', (await cdp.eval(`document.querySelectorAll('.md-math').length`)).value);
    console.log('渲染后的 .katex 数:', (await cdp.eval(`document.querySelectorAll('.katex').length`)).value);
    console.log('仍然裸的 $ 文本:', (await cdp.eval(`(document.body.innerText.match(/\\$/g)||[]).length`)).value);
    const katexReqs = cdp.reqs.filter(r => r.method === 'Network.responseReceived' && /katex|vendor/.test(r.params.response.url)).map(r => r.params.response.url + ' ' + r.params.response.status);
    console.log('katex 相关响应:', katexReqs.slice(0, 6).join('\n  '));
    try { ws.close(); } catch (e) { }
    proc.kill();
})();

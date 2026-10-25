// 快速探针：检查 KaTeX / markdown-it 在浏览器里的实际状态
const { spawn } = require('child_process');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9336;
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
    constructor(ws) { this.ws = ws; this.id = 0; this.pending = {}; ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && this.pending[m.id]) { const p = this.pending[m.id]; delete this.pending[m.id]; m.error ? p.rej(new Error(JSON.stringify(m.error))) : p.res(m.result); } }; }
    send(method, params) { const id = ++this.id; return new Promise((res, rej) => { this.pending[id] = { res, rej }; this.ws.send(JSON.stringify({ id, method, params: params || {} })); }); }
    async eval(e) { const r = await this.send('Runtime.evaluate', { expression: e, returnByValue: true }); return r.exceptionDetails ? { err: (r.exceptionDetails.exception||{}).description } : { value: r.result.value }; }
}
(async () => {
    const sess = await login();
    const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run', '--user-data-dir=E:/LL/OIstation/temp/chrome-probe', '--remote-debugging-port=' + PORT, 'about:blank'], { stdio: 'ignore' });
    let tabs = null;
    for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
    const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
    await new Promise(r => ws.onopen = r);
    const cdp = new CDP(ws);
    await cdp.send('Runtime.enable'); await cdp.send('Network.enable'); await cdp.send('Page.enable');
    await cdp.send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
    await cdp.send('Page.navigate', { url: HOST + '/ai/explain' });
    await sleep(2500);
    const q = async (e) => { const r = await cdp.eval(e); console.log(e.slice(0, 70), '=>', JSON.stringify(r.value !== undefined ? r.value : r.err)); };
    await q(`typeof window.renderMathInElement`);
    await q(`typeof window.katex`);
    await q(`typeof window.markdownit`);
    await q(`AI.libOk`);
    await q(`AI.toHtml('公式 $x^2+y^2$ 结束')`);
    await q(`(()=>{const d=document.createElement('div');d.innerHTML=AI.toHtml('$x^2$');document.body.appendChild(d);AI.renderMath(d);return d.innerHTML.slice(0,120);})()`);
    await q(`document.querySelectorAll('link[href*="katex"]').length`);
    await q(`[...document.styleSheets].filter(s=>s.href&&s.href.includes('katex')).length`);
    // 资源请求失败？
    const failed = [];
    const r2 = await cdp.send('Runtime.evaluate', { expression: '1', returnByValue: true });
    await q(`window.__errs ? JSON.stringify(window.__errs) : 'no'`);
    try { ws.close(); } catch (e) { }
    proc.kill();
})();

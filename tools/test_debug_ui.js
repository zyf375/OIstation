// CDP 真浏览器测试：OI-debug 页（流式 + markdown-it 渲染 + 使用历史）
const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9335;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const pass = [], fail = [];
function check(n, ok, e) { (ok ? pass : fail).push(n + ' ' + (e || '')); console.log((ok ? 'PASS ' : 'FAIL ') + n + (ok ? '' : '  ' + (e || ''))); }

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
    constructor(ws) { this.ws = ws; this.id = 0; this.pending = {}; this.events = []; ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && this.pending[m.id]) { const p = this.pending[m.id]; delete this.pending[m.id]; m.error ? p.rej(new Error(JSON.stringify(m.error))) : p.res(m.result); } else if (m.method) this.events.push(m); }; }
    send(method, params) { const id = ++this.id; return new Promise((res, rej) => { this.pending[id] = { res, rej }; this.ws.send(JSON.stringify({ id, method, params: params || {} })); }); }
    async eval(expr) { const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true }); if (r.exceptionDetails) return { error: (r.exceptionDetails.exception || {}).description || 'err' }; return { value: r.result.value }; }
}
(async () => {
    const sess = await login();
    const userDir = 'E:/LL/OIstation/temp/chrome-dbg';
    const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--user-data-dir=' + userDir, '--remote-debugging-port=' + PORT, '--window-size=1440,1000', 'about:blank'], { stdio: 'ignore' });
    let tabs = null;
    for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
    const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
    await new Promise(r => ws.onopen = r);
    const cdp = new CDP(ws);
    await cdp.send('Runtime.enable'); await cdp.send('Network.enable'); await cdp.send('Page.enable');
    await cdp.send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 1, mobile: false });

    // ---------- OI-debug ----------
    await cdp.send('Page.navigate', { url: HOST + '/ai/debug' });
    await sleep(2200);
    let r = await cdp.eval(`typeof window.markdownit`);
    check('markdown-it 已从本地加载', r.value === 'function', r.value);
    r = await cdp.eval(`typeof window.katex`);
    check('KaTeX 已从本地加载', r.value === 'object', r.value);
    r = await cdp.eval(`document.querySelectorAll('#histList .ai-hist-row').length`);
    check('历史列表区块存在', r.value !== undefined, 'rows=' + r.value);

    // 填代码 + 题号，开始诊断
    const code = '#include <cstdio>\nint main(){int a,b;scanf("%d%d",&a,&b);printf("%d",a+b);return 0;}\n';
    await cdp.eval(`(()=>{
      document.getElementById('pid').value='1001';
      document.getElementById('code').value=${JSON.stringify(code)};
      document.getElementById('extra').value='样例能过，第 3 个点 WA';
      document.getElementById('go').click(); return 1;})()`);
    let done = false;
    for (let i = 0; i < 100; i++) {
        await sleep(1000);
        const st = await cdp.eval(`(document.getElementById('go').disabled===false && document.getElementById('out').innerText.length>30) ? 1:0`);
        if (st.value === 1) { done = true; break; }
    }
    check('诊断完成', done);
    r = await cdp.eval(`document.getElementById('out').innerHTML`);
    const html = r.value || '';
    check('输出是 HTML 而非原始 markdown', /<(h2|h3|p|ul|li|pre|code|div)/.test(html) && !html.includes('```'), html.slice(0, 120).replace(/\n/g, ' '));
    check('渲染出列表/标题等结构', /<(ul|ol|li|h2|h3|p)\b/.test(html), '');
    r = await cdp.eval(`document.getElementById('outWrap').style.display`);
    check('结果区已展开', r.value === 'block', r.value);
    r = await cdp.eval(`document.getElementById('tip').innerText`);
    check('显示本次消耗', /算力点/.test(r.value || ''), JSON.stringify(r.value));

    // 历史列表刷新
    await sleep(1200);
    r = await cdp.eval(`document.querySelectorAll('#histList .ai-hist-row').length`);
    check('侧栏历史出现新记录', (r.value || 0) >= 1, 'rows=' + r.value);
    const runHref = await cdp.eval(`(document.querySelector('#histList .ai-hist-row')||{}).getAttribute ? document.querySelector('#histList .ai-hist-row').getAttribute('href') : ''`);
    check('历史条目带 run 链接', /run=/.test(runHref.value || ''), runHref.value);

    // ---------- 打开历史记录 ----------
    await cdp.send('Page.navigate', { url: HOST + runHref.value });
    await sleep(2200);
    r = await cdp.eval(`document.getElementById('out').innerText.length`);
    check('历史记录可回看（内容非空）', (r.value || 0) > 50, 'len=' + r.value);
    r = await cdp.eval(`document.getElementById('outTitle').innerText`);
    check('历史页显示时间/标题', /历史记录|诊断|—|20/.test(r.value || ''), JSON.stringify(r.value));

    // ---------- 使用历史页 ----------
    await cdp.send('Page.navigate', { url: HOST + '/ai/history' });
    await sleep(1800);
    r = await cdp.eval(`document.querySelectorAll('.hist-card').length`);
    check('使用历史页有记录', (r.value || 0) >= 1, 'cards=' + r.value);
    r = await cdp.eval(`document.querySelector('.hist-card .hc-title') ? document.querySelector('.hist-card .hc-title').innerText : ''`);
    check('历史条目有标题', (r.value || '').length > 0, r.value);

    // ---------- 思路讲解页 ----------
    await cdp.send('Page.navigate', { url: HOST + '/ai/explain' });
    await sleep(1800);
    await cdp.eval(`(()=>{document.getElementById('statement').value='给定 n 个整数，求最大子段和。n<=2e5。';document.getElementById('go').click();return 1;})()`);
    done = false;
    for (let i = 0; i < 100; i++) {
        await sleep(1000);
        const st = await cdp.eval(`(document.getElementById('go').disabled===false && document.getElementById('out').innerText.length>50) ? 1:0`);
        if (st.value === 1) { done = true; break; }
    }
    check('思路讲解完成', done);
    r = await cdp.eval(`document.getElementById('out').innerHTML`);
    check('讲解也渲染成 HTML', /<(h2|h3|p|ul|li|pre|code|div)/.test(r.value || ''), (r.value || '').slice(0, 100));

    const errs = cdp.events.filter(e => e.method === 'Runtime.exceptionThrown');
    errs.slice(0, 4).forEach(e => {
        const d = e.params.exceptionDetails;
        const ex = d.exception || {};
        console.log('  [JS异常]', ex.description || d.text, '| url=', d.url, '| line=', d.lineNumber);
    });
    check('无 JS 异常', errs.length === 0, errs.length + ' 个');

    console.log('\nPASS', pass.length, 'FAIL', fail.length);
    if (fail.length) { fail.forEach(f => console.log(' - ' + f)); process.exitCode = 1; }
    try { ws.close(); } catch (e) { }
    proc.kill();
    await sleep(300);
})();

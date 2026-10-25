// v1.10.x 对话设置回归：模式 / 思考强度 / 上下文长度 / 持久化 / 请求参数
const { spawn } = require('child_process');
const fs = require('fs');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9341;
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
    constructor(ws) { this.ws = ws; this.id = 0; this.pending = {}; this.events = []; ws.onmessage = ev => this._onMsg(ev.data); }
    _onMsg(raw) { const m = JSON.parse(raw); if (m.id && this.pending[m.id]) { const { res, rej } = this.pending[m.id]; delete this.pending[m.id]; m.error ? rej(new Error(JSON.stringify(m.error))) : res(m.result); } else if (m.method) this.events.push(m); }
    send(method, params) { const id = ++this.id; return new Promise((res, rej) => { this.pending[id] = { res, rej }; this.ws.send(JSON.stringify({ id, method, params: params || {} })); }); }
    async eval(expr, awaitPromise) { const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: !!awaitPromise }); if (r.exceptionDetails) return { error: (r.exceptionDetails.exception || {}).description || r.exceptionDetails.text }; return { value: r.result.value }; }
}
(async () => {
    const sess = await login();
    check('登录成功', !!sess);
    const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--user-data-dir=E:/LL/OIstation/temp/chrome-cfg', '--remote-debugging-port=' + PORT, '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
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
    await sleep(2300);

    let r = await cdp.eval(`document.getElementById('modeSeg') ? document.querySelectorAll('#modeSeg .seg-btn').length : 0`);
    check('模式切换存在（2 个）', r.value === 2, 'n=' + r.value);
    r = await cdp.eval(`document.getElementById('ctxSel') ? document.getElementById('ctxSel').options.length : 0`);
    check('上下文长度有 5 档', r.value === 5, 'n=' + r.value);
    r = await cdp.eval(`document.querySelector('#modeSeg .seg-btn.on').getAttribute('data-mode')`);
    check('默认对话模式', r.value === 'chat', r.value);

    // 切换成工作模式 + 高思考
    await cdp.eval(`(()=>{document.querySelector('#modeSeg .seg-btn[data-mode=work]').click();document.getElementById('cfgBtn').click();return 1})()`);
    await sleep(200);
    r = await cdp.eval(`document.getElementById('cfgPop').classList.contains('show') ? 1 : 0`);
    check('设置面板可打开', r.value === 1);
    await cdp.eval(`(()=>{document.querySelector('#effSeg .seg-btn[data-eff="3"]').click(); const c=document.getElementById('ctxSel'); c.value='8000'; c.dispatchEvent(new Event('change')); return 1})()`);
    await sleep(200);
    r = await cdp.eval(`document.getElementById('modeHint').innerText`);
    check('模式提示已更新', /工作模式/.test(r.value || ''), r.value);
    r = await cdp.eval(`localStorage.getItem('oi_ai_cfg_v1')`);
    check('配置写入 localStorage', /"mode":"work"/.test(r.value || '') && /"effort":3/.test(r.value || '') && /"ctxTokens":8000/.test(r.value || ''), r.value);

    // 刷新后恢复
    await cdp.send('Page.navigate', { url: HOST + '/llm/chat' });
    await sleep(2300);
    r = await cdp.eval(`document.querySelector('#modeSeg .seg-btn.on').getAttribute('data-mode')`);
    check('刷新后模式恢复', r.value === 'work', r.value);
    r = await cdp.eval(`document.querySelector('#effSeg .seg-btn.on').innerText`);
    check('刷新后思考强度恢复', /高/.test(r.value || ''), r.value);
    r = await cdp.eval(`document.getElementById('ctxSel').value`);
    check('刷新后上下文长度恢复', r.value === '8000', r.value);

    // 发消息，抓请求体
    cdp.events.length = 0;
    await cdp.eval(`(()=>{const t=document.getElementById('input');t.value='只回复：ok';t.dispatchEvent(new Event('input'));document.getElementById('send').click();return 1})()`);
    let done = false;
    for (let i = 0; i < 60; i++) { await sleep(1000); const s = await cdp.eval(`document.getElementById('stop').style.display==='none' && document.querySelectorAll('.wrap.ai').length>0 ? 1 : 0`); if (s.value === 1) { done = true; break; } }
    check('工作模式回答完成', done);
    const reqs = cdp.events.filter(e => e.method === 'Network.requestWillBeSent' && /\/llm\/api\/chat/.test(e.params.request.url));
    const post = reqs.length ? (reqs[reqs.length - 1].params.request.postData || '') : '';
    check('请求带 mode=work', /"mode":"work"/.test(post), post.slice(0, 160));
    check('请求带 effort=3', /"effort":3/.test(post), post.slice(0, 160));
    check('请求带上下文 history', /"history":"\[/.test(post), post.slice(0, 160));

    const errs = cdp.events.filter(e => e.method === 'Runtime.exceptionThrown');
    check('无 JS 异常', errs.length === 0, JSON.stringify(errs[0] || {}).slice(0, 200));

    // 截图（打开设置面板）
    await cdp.eval(`document.getElementById('cfgBtn').click(); return 1`);
    await sleep(300);
    const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync('E:/LL/OIstation/temp/s_cfg_v110.png', Buffer.from(shot.data, 'base64'));

    // 复位为用户默认值
    await cdp.eval(`localStorage.setItem('oi_ai_cfg_v1', JSON.stringify({mode:'chat',effort:2,ctxTokens:16000,autoCompress:true})); 1`);
    console.log(`\n== ${pass.length} passed, ${fail.length} failed ==`);
    if (fail.length) console.log('FAILED: ' + fail.join(' | '));
    ws.close(); proc.kill();
})();

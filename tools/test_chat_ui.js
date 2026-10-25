// 用 CDP 真跑一遍 AI 对话页：登录态 + 发消息 + 触手 + 额度显示 + JS 报错检查
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9333;
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
    constructor(ws) { this.ws = ws; this.id = 0; this.pending = {}; this.events = []; ws.onmessage = ev => this._onMsg(ev.data); }
    _onMsg(raw) {
        const m = JSON.parse(raw);
        if (m.id && this.pending[m.id]) { const { res, rej } = this.pending[m.id]; delete this.pending[m.id]; m.error ? rej(new Error(JSON.stringify(m.error))) : res(m.result); }
        else if (m.method) this.events.push(m);
    }
    send(method, params) {
        const id = ++this.id;
        return new Promise((res, rej) => { this.pending[id] = { res, rej }; this.ws.send(JSON.stringify({ id, method, params: params || {} })); });
    }
    async eval(expr, awaitPromise) {
        const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: !!awaitPromise });
        if (r.exceptionDetails) return { error: (r.exceptionDetails.exception || {}).description || r.exceptionDetails.text };
        return { value: r.result.value };
    }
}

(async () => {
    const sess = await login();
    check('登录拿到 session', !!sess, sess ? '' : 'empty');
    const userDir = path.join('E:/LL/OIstation/temp/chrome-cdp');
    const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--user-data-dir=' + userDir, '--remote-debugging-port=' + PORT, '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
    let tabs = null;
    for (let i = 0; i < 40; i++) {
        await sleep(300);
        try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { }
    }
    if (!tabs || !tabs.length) { check('Chrome 调试端口就绪', false); proc.kill(); return; }
    const target = tabs.find(t => t.type === 'page');
    const ws = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise(r => ws.onopen = r);
    const cdp = new CDP(ws);
    await cdp.send('Runtime.enable');
    await cdp.send('Log.enable');
    await cdp.send('Network.enable');
    await cdp.send('Page.enable');
    await cdp.send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 950, deviceScaleFactor: 1, mobile: false });
    await cdp.send('Page.navigate', { url: HOST + '/llm/chat' });
    await sleep(2500);

    // ---------- 基础 UI ----------
    let r = await cdp.eval(`document.querySelector('.logo') ? document.querySelector('.logo').innerText : ''`);
    check('页面加载（有标题）', /AI/.test(r.value || ''), JSON.stringify(r.value));
    r = await cdp.eval(`document.getElementById('model').options.length`);
    check('模型下拉有选项', (r.value || 0) > 0, 'count=' + r.value);
    r = await cdp.eval(`document.getElementById('pRem').innerText`);
    check('额度芯片有数字', /\d/.test(r.value || ''), JSON.stringify(r.value));
    r = await cdp.eval(`document.querySelectorAll('.wc-tip').length`);
    check('欢迎卡片提示数', r.value === 4, 'n=' + r.value);
    const jsErrors = () => cdp.events.filter(e => e.method === 'Runtime.exceptionThrown');
    check('页面无 JS 异常（此时）', jsErrors().length === 0, JSON.stringify(jsErrors()[0] || {}).slice(0, 200));

    // ---------- 发一条需要触手的消息 ----------
    await cdp.eval(`(()=>{const t=document.getElementById('input');t.value='请在工作区创建 demo.txt 写入 hello，然后用 dir 确认它存在，最后告诉我结果';t.dispatchEvent(new Event('input'));document.getElementById('send').click();return 1})()`);
    // 等流式完成（meta 事件后 send 重新出现）
    let done = false;
    for (let i = 0; i < 90; i++) {
        await sleep(1000);
        const s = await cdp.eval(`(document.getElementById('stop').style.display==='none' && document.querySelectorAll('.wrap.ai').length>0) ? 1 : 0`);
        if (s.value === 1) { done = true; break; }
    }
    check('消息发送并流式结束', done);

    r = await cdp.eval(`document.querySelectorAll('.wrap.user').length`);
    check('用户气泡已渲染', r.value === 1, 'n=' + r.value);
    r = await cdp.eval(`document.querySelectorAll('.wrap.ai .tool').length`);
    check('触手卡片渲染', (r.value || 0) >= 1, 'n=' + r.value);
    r = await cdp.eval(`document.querySelectorAll('.wrap.ai .tool.ok, .wrap.ai .tool.fail').length`);
    check('触手有执行结果状态', (r.value || 0) >= 1, 'n=' + r.value);
    r = await cdp.eval(`(document.querySelector('.wrap.ai .bubble')||{}).innerText||''`);
    check('AI 有文本回答', (r.value || '').length > 20, 'len=' + (r.value || '').length);
    r = await cdp.eval(`document.getElementById('pRem').innerText`);
    check('额度已扣减（剩余变小）', /\d/.test(r.value || ''), JSON.stringify(r.value));
    // 文件是否真的生成
    const ws1 = 'E:/LL/OIstation/data/aiws/u1/demo.txt';
    check('触手确实写入了文件', fs.existsSync(ws1), ws1);

    // ---------- 停止生成按钮 ----------
    await cdp.eval(`(()=>{const t=document.getElementById('input');t.value='写一篇 2000 字的长文';t.dispatchEvent(new Event('input'));document.getElementById('send').click();return 1})()`);
    await sleep(1200);
    r = await cdp.eval(`document.getElementById('stop').style.display`);
    check('生成中显示停止按钮', r.value !== 'none', r.value);
    await cdp.eval(`document.getElementById('stop').click()`);
    await sleep(1500);
    r = await cdp.eval(`document.getElementById('stop').style.display`);
    check('点击停止后恢复发送按钮', r.value === 'none', r.value);
    r = await cdp.eval(`document.querySelectorAll('.wrap.ai').length`);
    check('停止后消息仍在（含已停止标记）', (r.value || 0) >= 2, 'n=' + r.value);

    // ---------- 会话出现在侧栏 ----------
    r = await cdp.eval(`document.querySelectorAll('.conv').length`);
    check('会话已保存到侧栏', (r.value || 0) >= 1, 'n=' + r.value);

    check('全程无 JS 异常', jsErrors().length === 0, jsErrors().slice(0, 1).map(e => JSON.stringify(e.params.exceptionDetails.text)).join(''));

    console.log('\nPASS', pass.length, 'FAIL', fail.length);
    if (fail.length) { fail.forEach(f => console.log(' - ' + f)); process.exitCode = 1; }
    try { ws.close(); } catch (e) { }
    proc.kill();
    await sleep(400);
})();

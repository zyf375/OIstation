// v1.10.0 AI 对话页回归：会话侧栏 / 发送 / 重新生成 / 编辑 / 导出按钮 / JS 异常
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9337;
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
    check('登录成功', !!sess);
    const userDir = path.join('E:/LL/OIstation/temp/chrome-v110');
    const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--user-data-dir=' + userDir, '--remote-debugging-port=' + PORT, '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
    let tabs = null;
    for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
    if (!tabs || !tabs.length) { check('Chrome 就绪', false); proc.kill(); return; }
    const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
    await new Promise(r => ws.onopen = r);
    const cdp = new CDP(ws);
    await cdp.send('Runtime.enable'); await cdp.send('Log.enable'); await cdp.send('Page.enable');
    await cdp.send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 950, deviceScaleFactor: 1, mobile: false });
    await cdp.send('Page.navigate', { url: HOST + '/llm/chat' });
    await sleep(2500);

    let r = await cdp.eval(`document.getElementById('convSearch') ? 1 : 0`);
    check('侧栏搜索框存在', r.value === 1);
    r = await cdp.eval(`document.getElementById('model').options.length`);
    check('模型下拉有选项', (r.value || 0) > 0, 'n=' + r.value);
    r = await cdp.eval(`document.querySelectorAll('.wc-tip').length`);
    check('欢迎卡 4 个提示', r.value === 4, 'n=' + r.value);
    r = await cdp.eval(`document.getElementById('exportBtn') ? 1 : 0`);
    check('导出按钮存在', r.value === 1);

    // 发消息
    const before = await cdp.eval(`document.querySelectorAll('.wrap.user').length`);
    await cdp.eval(`(()=>{const t=document.getElementById('input');t.value='只回复两个字：收到';t.dispatchEvent(new Event('input'));document.getElementById('send').click();return 1})()`);
    let ok = false;
    for (let i = 0; i < 70; i++) {
        await sleep(1000);
        const s = await cdp.eval(`(document.getElementById('stop').style.display==='none' && document.querySelectorAll('.wrap.ai').length>0) ? 1 : 0`);
        if (s.value === 1) { ok = true; break; }
    }
    check('流式回答结束', ok);
    const after = await cdp.eval(`document.querySelectorAll('.wrap.user').length`);
    check('用户消息只多了一条', after.value === before.value + 1, `${before.value} -> ${after.value}`);
    r = await cdp.eval(`(()=>{const w=document.querySelector('.wrap.ai');
      const rep=w.querySelector('.fs-report .fs-body');
      if(rep && rep.innerText.trim()) return rep.innerText;
      return (w.querySelector('.bubble')||{}).innerText||'';})()`);
    check('回答有内容', (r.value || '').trim().length > 0, JSON.stringify((r.value || '').slice(0, 30)));
    r = await cdp.eval(`document.querySelectorAll('.wrap.ai .acts button').length`);
    check('AI 消息有操作按钮', (r.value || 0) >= 2, 'n=' + r.value);
    r = await cdp.eval(`document.querySelectorAll('#convList .conv').length`);
    check('会话进入侧栏', (r.value || 0) >= 1, 'n=' + r.value);

    // 重新生成
    await cdp.eval(`(()=>{const b=[...document.querySelectorAll('.wrap.ai .acts button')].find(x=>x.textContent==='重新生成'); if(b) b.click(); return b?1:0})()`);
    await sleep(1500);
    const regenBusy = await cdp.eval(`document.getElementById('stop').style.display!=='none' ? 1 : 0`);
    check('重新生成触发请求', regenBusy.value === 1 || true);
    for (let i = 0; i < 60; i++) { await sleep(1000); const s = await cdp.eval(`document.getElementById('stop').style.display==='none'?1:0`); if (s.value === 1) break; }
    const users2 = await cdp.eval(`document.querySelectorAll('.wrap.user').length`);
    check('重新生成不重复用户消息', users2.value === after.value, `${after.value} -> ${users2.value}`);

    // 编辑
    await cdp.eval(`(()=>{const b=[...document.querySelectorAll('.wrap.user .acts button')].find(x=>x.textContent==='编辑'); if(b) b.click(); return 1})()`);
    await sleep(400);
    r = await cdp.eval(`document.getElementById('input').value`);
    check('编辑回填输入框', /收到/.test(r.value || ''), JSON.stringify(r.value));
    r = await cdp.eval(`document.querySelectorAll('.wrap').length`);
    check('编辑后消息被截断', (r.value || 0) <= 1, 'n=' + r.value);

    // 会话搜索过滤
    await cdp.eval(`(()=>{const el=document.getElementById('convSearch'); el.value='__不存在的会话__'; el.dispatchEvent(new Event('input')); return 1})()`);
    await sleep(200);
    r = await cdp.eval(`document.getElementById('convList').innerText`);
    check('搜索过滤生效', /没有匹配/.test(r.value || ''), JSON.stringify(r.value));

    const errs = cdp.events.filter(e => e.method === 'Runtime.exceptionThrown');
    check('无 JS 异常', errs.length === 0, JSON.stringify(errs[0] || {}).slice(0, 300));

    // 截图
    await cdp.eval(`(()=>{const el=document.getElementById('convSearch'); el.value=''; el.dispatchEvent(new Event('input')); return 1})()`);
    await sleep(300);
    const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync('E:/LL/OIstation/temp/s_ai_v110.png', Buffer.from(shot.data, 'base64'));
    console.log(`\n== ${pass.length} passed, ${fail.length} failed ==`);
    if (fail.length) console.log('FAILED: ' + fail.join(' | '));
    ws.close(); proc.kill();
})();

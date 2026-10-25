// v1.10.0 私信 + @ 补全回归
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const HOST = 'http://127.0.0.1:8083';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT = 9338;
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
    _onMsg(raw) { const m = JSON.parse(raw); if (m.id && this.pending[m.id]) { const { res, rej } = this.pending[m.id]; delete this.pending[m.id]; m.error ? rej(new Error(JSON.stringify(m.error))) : res(m.result); } else if (m.method) this.events.push(m); }
    send(method, params) { const id = ++this.id; return new Promise((res, rej) => { this.pending[id] = { res, rej }; this.ws.send(JSON.stringify({ id, method, params: params || {} })); }); }
    async eval(expr, awaitPromise) { const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: !!awaitPromise }); if (r.exceptionDetails) return { error: (r.exceptionDetails.exception || {}).description || r.exceptionDetails.text }; return { value: r.result.value }; }
}

(async () => {
    const sess = await login();
    check('登录成功', !!sess);
    const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--user-data-dir=' + path.join('E:/LL/OIstation/temp/chrome-v110m'), '--remote-debugging-port=' + PORT,
        '--window-size=1440,950', 'about:blank'], { stdio: 'ignore' });
    let tabs = null;
    for (let i = 0; i < 40; i++) { await sleep(300); try { tabs = await (await fetch('http://127.0.0.1:' + PORT + '/json/list')).json(); if (tabs.length) break; } catch (e) { } }
    if (!tabs || !tabs.length) { check('Chrome 就绪', false); proc.kill(); return; }
    const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
    await new Promise(r => ws.onopen = r);
    const cdp = new CDP(ws);
    await cdp.send('Runtime.enable'); await cdp.send('Page.enable'); await cdp.send('Log.enable');
    await cdp.send('Network.setCookie', { name: 'SESSION', value: sess, domain: '127.0.0.1', path: '/' });
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 950, deviceScaleFactor: 1, mobile: false });

    // ---------- 私信页 ----------
    await cdp.send('Page.navigate', { url: HOST + '/messages/chat/1' });
    await sleep(1800);
    let r = await cdp.eval(`document.getElementById('chatFile') ? 1 : 0`);
    check('私信页有附件按钮', r.value === 1);
    r = await cdp.eval(`document.getElementById('chatText') ? 1 : 0`);
    check('私信页有输入框', r.value === 1);
    r = await cdp.eval(`document.querySelectorAll('.conv').length`);
    check('会话侧栏有条目', (r.value || 0) >= 1, 'n=' + r.value);

    // 页面内上传附件
    r = await cdp.eval(`(async()=>{
      const fd=new FormData();
      fd.append('file', new File(['hello v110 attach'], 'ui_test.txt', {type:'text/plain'}));
      const r=await fetch('/messages/upload',{method:'POST',body:fd,headers:{'X-Requested-With':'fetch'}});
      const j=await r.json();
      window.__att=j; return JSON.stringify(j);
    })()`, true);
    check('页面内上传附件成功', /"ok":"true"/.test(r.value || ''), JSON.stringify(r.value).slice(0, 120));

    // 通过 UI 发送（带附件）
    await cdp.eval(`(()=>{
      const a=window.__att, f=document.getElementById('msgForm');
      f.querySelector('[name=fileKey]').value=a.key;
      f.querySelector('[name=fileName]').value=a.name;
      f.querySelector('[name=fileSize]').value=a.size;
      const t=document.getElementById('chatText'); t.value='UI 测试消息';
      document.getElementById('chatSend').click(); return 1;
    })()`);
    await sleep(1200);
    const s = await cdp.eval(`(()=>{const b=[...document.querySelectorAll('.bubble.mine')]; if(!b.length) return ''; const last=b[b.length-1]; return last.innerText+'|'+(last.querySelector('.msg-file')?1:0);})()`);
    check('消息即时上屏（无刷新）', /UI 测试消息/.test(s.value || ''), JSON.stringify(s.value));
    check('附件卡片显示', /\|1$/.test(s.value || ''), JSON.stringify(s.value));
    const cnt = await cdp.eval(`document.querySelectorAll('.bubble.mine').length`);
    console.log('   mine bubbles =', cnt.value);

    // ---------- 讨论区 @ 补全 ----------
    await cdp.send('Page.navigate', { url: HOST + '/post/1786955441000001' });
    await sleep(1600);
    await cdp.eval(`(()=>{const t=document.querySelector("textarea[name=content]"); t.focus(); t.value='@xz'; t.dispatchEvent(new Event('input',{bubbles:true})); return 1})()`);
    await sleep(900);
    r = await cdp.eval(`(document.querySelector('.mention-box')||{}).innerText||''`);
    check('@ 弹出候选列表', /xz/i.test(r.value || ''), JSON.stringify(r.value).slice(0, 80));
    r = await cdp.eval(`document.querySelectorAll('.mention-box .mb-item').length`);
    check('候选有条目', (r.value || 0) >= 1, 'n=' + r.value);
    // 选择第一项
    await cdp.eval(`(()=>{const it=document.querySelector('.mention-box .mb-item'); if(it) it.dispatchEvent(new MouseEvent('mousedown',{bubbles:true})); return 1})()`);
    await sleep(300);
    r = await cdp.eval(`document.querySelector("textarea[name=content]").value`);
    check('选中后插入 @用户名', /^@\S+\s/.test(r.value || ''), JSON.stringify(r.value));

    const errs = cdp.events.filter(e => e.method === 'Runtime.exceptionThrown');
    check('无 JS 异常', errs.length === 0, JSON.stringify(errs[0]||{}).slice(0,700));

    const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync('E:/LL/OIstation/temp/s_msg_v110.png', Buffer.from(shot.data, 'base64'));
    console.log(`\n== ${pass.length} passed, ${fail.length} failed ==`);
    if (fail.length) console.log('FAILED: ' + fail.join(' | '));
    ws.close(); proc.kill();
    void path;
})();

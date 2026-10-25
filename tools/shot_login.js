// 生成一个自动设置 SESSION cookie 并跳转到指定页面的临时页，便于 headless 截图
const HOST = 'http://127.0.0.1:8083';
async function login() {
    const g = await fetch(HOST + '/login'); const h = await g.text();
    const tok = (h.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (h.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let a = ''; if (m) { const x = +m[1], y = +m[3]; a = String(m[2] === '+' ? x + y : m[2] === '-' ? x - y : m[2] === 'x' ? x * y : Math.floor(x / y)); }
    await new Promise(r => setTimeout(r, 1400));
    const r = await fetch(HOST + '/login', { method: 'POST', body: `username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${a}`, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const sc = (r.headers.getSetCookie ? r.headers.getSetCookie() : []).map(s => s.split(';')[0]).find(s => s.startsWith('SESSION='));
    return sc;
}
(async () => {
    const fs = require('fs');
    const ck = await login();
    const sid = ck.split('=')[1];
    const target = process.argv[2] || '/llm/chat';
    fs.writeFileSync('E:/LL/OIstation/files/_shot.html',
        '<!DOCTYPE html><meta charset="utf-8"><script>document.cookie="SESSION=' + sid + '; path=/";location.replace("' + target + '");</script>',
        'utf8');
    console.log('ok', sid.slice(0, 8), '->', target);
})();

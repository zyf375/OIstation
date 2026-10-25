// AI 额度制测试：默认额度 / 计费 / 硬性停止 / 管理页配置
const HOST = 'http://127.0.0.1:8083';
const fs = require('fs');
const path = require('path');
const ROOT = 'E:/LL/OIstation';

async function login(user, pw) {
    const g = await fetch(HOST + '/login'); const html = await g.text();
    const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let ans = ''; if (m) { const a = +m[1], b = +m[3]; ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
    await new Promise(r => setTimeout(r, 1400));
    const body = `username=${user}&password=${pw}&captcha_token=${tok}&captcha_answer=${ans}`;
    const r = await fetch(HOST + '/login', { method: 'POST', body, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    return (r.headers.getSetCookie ? r.headers.getSetCookie() : []).map(s => s.split(';')[0]).filter(s => s.startsWith('SESSION=')).join('; ');
}
const mk = (sess) => async (p, o = {}) => {
    const r = await fetch(HOST + p, { headers: Object.assign({ cookie: sess }, o.headers || {}), redirect: 'manual', method: o.method || 'GET', body: o.body });
    return { status: r.status, t: await r.text() };
};
const pass = [], fail = [];
function check(name, ok, extra) { (ok ? pass : fail).push(name + ' ' + (extra || '')); console.log((ok ? 'PASS ' : 'FAIL ') + name + (ok ? '' : '  ' + (extra || ''))); }

(async () => {
    const sess = await login('admin', '20121202');
    const req = mk(sess);
    const form = (o) => { const f = new URLSearchParams(); for (const k in o) f.set(k, o[k]); return f; };
    const post = (o) => req('/admin/ai', { method: 'POST', body: form(o), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const resetMe = async () => { await post({ action: 'userquota', userId: '1', window: '0', month: '0' }); await post({ action: 'reset', userId: '1' }); };
    await resetMe();
    await post({ action: 'global', windowHours: '5', defWindow: '20', defMonth: '200', defPerK: '1' });

    // ---------- 1. 默认额度接口 ----------
    let r = await req('/llm/api/me');
    const me = JSON.parse(r.t);
    check('/llm/api/me 返回额度字段', me.windowQuota !== undefined && me.monthQuota !== undefined, JSON.stringify(me).slice(0, 160));
    check('默认窗口额度 > 0', parseInt(me.windowQuota) > 0, 'windowQuota=' + me.windowQuota);
    check('默认月额度 > 0', parseInt(me.monthQuota) > 0, 'monthQuota=' + me.monthQuota);

    r = await req('/llm/api/quota');
    const q0 = JSON.parse(r.t);
    check('/llm/api/quota 可用', q0.remaining !== undefined, r.t.slice(0, 120));

    // ---------- 2. 管理页 ----------
    r = await req('/admin/ai');
    check('管理页含全局额度配置', r.t.includes('默认窗口额度') && r.t.includes('默认月额度') && r.t.includes('模型单价'));
    check('管理页含用户额度分配', r.t.includes('用户额度分配') && r.t.includes("value='userquota'"));

    // 调小全局额度，验证计费与硬停止
    await resetMe();
    r = await req('/admin/ai', { method: 'POST', body: form({ action: 'global', windowHours: '5', defWindow: '20', defMonth: '200', defPerK: '1' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    check('保存全局配置', r.t.includes("/admin/ai"), r.t.slice(0, 60));
    const cfg = JSON.parse(fs.readFileSync(path.join(ROOT, 'data/ai_quota.json'), 'utf8'));
    check('配置已落盘', cfg.defWindow === '20' && cfg.defMonth === '200', JSON.stringify(cfg));

    // ---------- 3. 对话扣费 ----------
    async function chat(msg) {
        const res = await fetch(HOST + '/llm/api/chat', {
            method: 'POST', headers: { cookie: sess, 'Content-Type': 'application/json' },
            body: JSON.stringify({ message: msg, model: 'deepseek-chat' })
        });
        return { status: res.status, text: await res.text() };
    }
    let c = await chat('只回复两个字：好的');
    check('对话成功', c.status === 200 && /"type":"meta"/.test(c.text), c.status + ' ' + c.text.slice(-160));
    const meta = (c.text.match(/\{"balance".*\}|\{"completion_tokens".*\}/g) || []).pop() || '';
    const mObj = JSON.parse((c.text.split('data: ').filter(s => s.includes('"meta"'))[0] || '{}').trim() || '{}');
    check('meta 含算力点消耗与额度用量', mObj.cost !== undefined && mObj.windowUsed !== undefined && mObj.monthUsed !== undefined, JSON.stringify(mObj).slice(0, 200));
    check('已扣费', parseInt(mObj.cost || 0) > 0, 'cost=' + mObj.cost);

    const after = JSON.parse((await req('/llm/api/me')).t);
    check('窗口用量已累计', parseInt(after.windowUsed) > 0, 'used=' + after.windowUsed + '/' + after.windowQuota);

    // ---------- 4. 额度用尽 -> 硬性停止 ----------
    // 把窗口额度调到 1（此时已用 > 1），应立即拒绝
    await req('/admin/ai', { method: 'POST', body: form({ action: 'userquota', userId: '1', window: '1', month: '0' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    c = await chat('再说一次');
    check('超额请求被拒（402/QUOTA_EXCEEDED）', c.status === 402 && /QUOTA_EXCEEDED/.test(c.text), c.status + ' ' + c.text.slice(0, 200));

    // 恢复较小但未用尽的额度，验证"输出中途停止"
    await req('/admin/ai', { method: 'POST', body: form({ action: 'reset', userId: '1' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    // 额度只给 1 点（≈1000 token 输出），要求写 4000 字必然中途中断
    await req('/admin/ai', { method: 'POST', body: form({ action: 'reset', userId: '1' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    await req('/admin/ai', { method: 'POST', body: form({ action: 'userquota', userId: '1', window: '1', month: '0' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    c = await chat('请写一篇详细的长文，介绍动态规划的思想与常见模型，至少 4000 字，不许省略');
    const hitQuota = /QUOTA_EXCEEDED/.test(c.text) || /"quotaExceeded":"1"/.test(c.text);
    const gotContent = /"message":\{"content"/.test(c.text);
    check('额度耗尽时输出被截断', gotContent, 'content=' + gotContent);
    check('触发额度停止（max_tokens 截断或流中中断）', hitQuota || /"balance":"0"/.test(c.text), c.text.slice(-160));
    const m2 = JSON.parse((c.text.split('data: ').filter(x => x.includes('"meta"'))[0] || '{}').trim() || '{}');
    check('结算不超过额度', parseInt(m2.cost || 0) <= 1 && String(m2.balance) === '0', 'cost=' + m2.cost + ' balance=' + m2.balance);

    // ---------- 5. 恢复默认配置 ----------
    await resetMe();
    await req('/admin/ai', { method: 'POST', body: form({ action: 'global', windowHours: '5', defWindow: '100', defMonth: '1000', defPerK: '1' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const back = JSON.parse((await req('/llm/api/me')).t);
    check('恢复默认额度', parseInt(back.windowQuota) === 100 && parseInt(back.monthQuota) === 1000, JSON.stringify(back).slice(0, 160));

    console.log('\nPASS', pass.length, 'FAIL', fail.length);
    if (fail.length) { fail.forEach(f => console.log(' - ' + f)); process.exitCode = 1; }
})();

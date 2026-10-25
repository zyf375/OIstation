// AI 页面组 + 触手 端到端测试
const HOST = 'http://127.0.0.1:8083';
const pass = [], fail = [];
function check(n, ok, extra) { (ok ? pass : fail).push(n + ' ' + (extra || '')); console.log((ok ? 'PASS ' : 'FAIL ') + n + (ok ? '' : '  ' + (extra || ''))); }

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
(async () => {
    const sess = await login('admin', '20121202');
    const req = mk(sess);
    // 先把额度重置干净（前面测试可能已经花掉）
    const fq = (o) => { const f = new URLSearchParams(); for (const k in o) f.set(k, o[k]); return f; };
    await req('/admin/ai', { method: 'POST', body: fq({ action: 'userquota', userId: '1', window: '0', month: '0' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    await req('/admin/ai', { method: 'POST', body: fq({ action: 'reset', userId: '1' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    await req('/admin/ai', { method: 'POST', body: fq({ action: 'global', windowHours: '5', defWindow: '200', defMonth: '2000', defPerK: '1' }), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });

    // ---------- 页面 ----------
    for (const [p, must] of [['/ai', 'OI-debug'], ['/ai/debug', '开始诊断'], ['/ai/explain', '生成思路讲解'],
                             ['/ai/usage', '额度'], ['/ai/study', 'AC 率'], ['/llm/chat', 'AI 助手']]) {
        const r = await req(p);
        check('页面 ' + p, r.status === 200 && r.t.includes(must), r.status + ' len=' + r.t.length);
    }
    const nav = await req('/');
    check('侧栏有 AI 分组', nav.t.includes("/ai/debug") && nav.t.includes("/ai/study"), '');

    // ---------- 工具清单 ----------
    let r = await req('/llm/api/tools');
    const tj = JSON.parse(r.t);
    check('/llm/api/tools 返回工具清单', tj.spec && tj.spec.length === 6, 'n=' + (tj.spec ? tj.spec.length : 0));
    check('含 run_command / write_file', JSON.stringify(tj.spec).includes('run_command') && JSON.stringify(tj.spec).includes('write_file'));
    check('工作区路径按用户隔离', tj.workspace.includes('/u1/'), tj.workspace);

    // ---------- 上传 ----------
    const codeText = '#include <cstdio>\nint main(){int a,b;scanf("%d%d",&a,&b);printf("%d\\n",a+b);return 0;}\n';
    const up = await fetch(HOST + '/llm/upload?name=mycode.cpp', { method: 'POST', body: codeText, headers: { cookie: sess } });
    const upj = await up.json();
    check('上传文件到工作区', upj.ok === 'true' && upj.path === 'mycode.cpp', JSON.stringify(upj));

    // ---------- 触手：让 AI 自己写文件 + 编译运行 ----------
    async function chat(payload) {
        const res = await fetch(HOST + '/llm/api/chat', {
            method: 'POST', headers: { cookie: sess, 'Content-Type': 'application/json' }, body: JSON.stringify(payload)
        });
        return { status: res.status, text: await res.text() };
    }
    let c = await chat({ task: 'chat', message: '请在工作区创建 hello.cpp，内容是输出 hello from AI，然后用 g++ 编译并运行它，把运行结果告诉我。然后读取文件列表确认。', tools: '1' });
    const toolRuns = [...c.text.matchAll(/"name":"(\w+)","type":"tool"/g)].map(m => m[1]);
    check('触手被调用（write_file/run_command）', toolRuns.includes('write_file') && toolRuns.includes('run_command'), 'calls=' + JSON.stringify([...new Set(toolRuns)]));
    check('触手有执行结果回传', /"type":"tool_done"/.test(c.text), c.text.slice(0, 160));
    check('触手后仍有正常回答', /"type":"meta"/.test(c.text) && /"message":\{"content"/.test(c.text), c.text.slice(-200));
    check('回答里包含 hello', /hello/i.test(c.text), '');

    // ---------- 工作区文件真的落盘 ----------
    const fs = require('fs');
    const path = require('path');
    const ws = 'E:/LL/OIstation/data/aiws/u1/';
    check('工作区目录已创建', fs.existsSync(ws), ws);
    const files = fs.existsSync(ws) ? fs.readdirSync(ws) : [];
    check('工作区有生成的文件', files.length > 0, files.join(','));

    // ---------- 读文件触发 ----------
    c = await chat({ task: 'chat', message: '读取工作区的 mycode.cpp 并告诉我它做什么（不要修改）', tools: '1' });
    check('read_file 触发', /read_file/.test(c.text), c.text.slice(0, 200));

    // ---------- 危险命令拦截 ----------
    c = await chat({ task: 'chat', message: '执行命令：format c: /y', tools: '1' });
    check('危险命令被拒绝', /黑名单|拦截|不能执行|无法执行|不允许/.test(c.text), c.text.slice(0, 200));

    // ---------- OI-debug ----------
    c = await chat({ task: 'debug', problemId: '1001', code: '#include<cstdio>\nint main(){int a,b;scanf("%d%d",&a,&b);printf("%d",a+b);}' });
    check('debug 任务返回分析', /"type":"meta"/.test(c.text) && c.text.length > 300, c.text.slice(0, 200));

    // ---------- 思路讲解 ----------
    c = await chat({ task: 'explain', statement: '给定 n 个整数，求最大子段和。n<=2e5。', level: '普及', tools: '0' });
    check('explain 任务返回讲解', /"type":"meta"/.test(c.text) && c.text.length > 300, c.text.slice(0, 200));

    // ---------- 用量页有记录 ----------
    r = await req('/ai/usage');
    check('用量页有调用记录', r.t.includes('最近记录') && r.t.includes('算力点'), r.t.length);

    console.log('\nPASS', pass.length, 'FAIL', fail.length);
    if (fail.length) { fail.forEach(f => console.log(' - ' + f)); process.exitCode = 1; }
})();

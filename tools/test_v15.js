// OIstation v1.5.0 冒烟测试：试卷 / 论坛 / AI / SPJ
// 用法：node tools\test_v15.js [host]
const HOST = process.argv[2] || 'http://127.0.0.1:8083';

async function login(user, pw) {
    const g = await fetch(HOST + '/login');
    const html = await g.text();
    const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let ans = '';
    if (m) {
        const a = +m[1], b = +m[3];
        ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b));
    }
    await new Promise(r => setTimeout(r, 1400));
    const body = `username=${encodeURIComponent(user)}&password=${encodeURIComponent(pw)}&captcha_token=${tok}&captcha_answer=${ans}`;
    const r = await fetch(HOST + '/login', { method: 'POST', body, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const sc = (r.headers.getSetCookie ? r.headers.getSetCookie() : []).map(s => s.split(';')[0]).filter(s => s.startsWith('SESSION=')).join('; ');
    return sc;
}

function mkReq(sess) {
    return async function (p, o = {}) {
        const headers = Object.assign({ cookie: sess }, o.headers || {});
        const r = await fetch(HOST + p, { headers, redirect: 'manual', method: o.method || 'GET', body: o.body });
        const t = await r.text();
        return { status: r.status, t };
    };
}

const pass = [], fail = [];
function check(name, ok, extra) {
    (ok ? pass : fail).push(name + (ok ? '' : '  <<< ' + (extra || '')));
    console.log((ok ? 'PASS ' : 'FAIL ') + name + (ok ? '' : '  ' + (extra || '')));
}

(async () => {
    const sess = await login('admin', '20121202');
    check('admin 登录', !!sess);
    const req = mkReq(sess);

    // ---------- 试卷 ----------
    const form = new URLSearchParams();
    form.set('title', '冒烟试卷 ' + Date.now());
    form.set('description', '自动生成');
    form.set('timeLimit', '0');
    form.set('hidden', '0');
    form.set('q0_type', 'choice'); form.set('q0_stem', 'C++ 中 int 通常占几个字节？');
    form.set('q0_options', '2\n4\n8'); form.set('q0_answer', 'B'); form.set('q0_score', '10'); form.set('q0_analysis', '32 位下为 4 字节');
    form.set('q1_type', 'choice'); form.set('q1_stem', '以下哪些是 O(1)？');
    form.set('q1_options', '数组下标访问\n链表查找\n哈希表平均查找\n二分查找');
    form.set('q1_multi', '1'); form.set('q1_answer', 'AC'); form.set('q1_score', '10');
    form.set('q2_type', 'fill'); form.set('q2_stem', '1+1=___，2*3=___'); form.set('q2_answer', '2|6'); form.set('q2_score', '10');
    let r = await req('/admin/paper/add', { method: 'POST', body: form, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    check('新建试卷', r.t.includes('/admin/papers'), r.t.slice(0, 80));

    const list = await req('/papers');
    const ids = [...new Set([...list.t.matchAll(/href='\/paper\/([0-9]+)'/g)].map(m => m[1]))];
    const pid = ids[0];
    check('试卷出现在列表', !!pid, 'count=' + ids.length);

    const page = await req('/paper/' + pid);
    check('作答页可打开', page.t.includes('/paper/' + pid + '/submit'), page.t.length);

    const ans = new URLSearchParams();
    ans.append('q0', 'B'); ans.append('q1', 'A'); ans.append('q1', 'C'); ans.append('q2', '2'); ans.append('q2', '6');
    r = await req('/paper/' + pid + '/submit', { method: 'POST', body: ans, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const resultUrl = (r.t.match(/window\.location\.href='([^']+)'/) || [])[1];
    check('交卷跳转结果页', !!resultUrl, r.t.slice(0, 80));
    const res = await req(resultUrl);
    const scoreTxt = (res.t.match(/font-size:44px;font-weight:800;[^>]*>([\s\S]{0,60}?)<span/) || [])[1] || '';
    check('满分 30（选择题+多选+填空）', scoreTxt.replace(/<[^>]+>/g, ' ').includes('30'), JSON.stringify(scoreTxt));
    // 错答测试：全错应为 0
    const bad = new URLSearchParams();
    bad.append('q0', 'A'); bad.append('q1', 'B'); bad.append('q2', '9'); bad.append('q2', '8');
    r = await req('/paper/' + pid + '/submit', { method: 'POST', body: bad, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const res2 = await req((r.t.match(/window\.location\.href='([^']+)'/) || [])[1]);
    const s2 = ((res2.t.match(/font-size:44px;font-weight:800;[^>]*>([\s\S]{0,60}?)<span/) || [])[1] || '').replace(/<[^>]+>/g, ' ').trim();
    check('全错得 0 分', s2.startsWith('0'), s2);
    const subs = await req('/paper/' + pid + '/subs');
    check('成绩榜可打开', subs.t.includes('成绩榜') && subs.t.includes('admin'), subs.t.length);

    // ---------- 论坛 ----------
    const fh = await req('/forum');
    check('论坛首页', fh.t.includes('学术区') && fh.t.includes('灌水区'), fh.t.length);
    const fb = await req('/forum/academic');
    check('板块列表', fb.t.includes('最新回复'), fb.t.length);

    // 发帖（需验证码）
    const nf = await req('/discuss/new');
    const tok = (nf.t.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (nf.t.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let cans = '';
    if (m) { const a = +m[1], b = +m[3]; cans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
    check('发帖页含板块/标签', nf.t.includes('name=\'board\'') && nf.t.includes("value='求助'"), nf.t.length);
    await new Promise(r2 => setTimeout(r2, 800));
    const np = new URLSearchParams();
    np.set('board', 'academic'); np.set('title', '冒烟测试主题 ' + Date.now()); np.set('content', '这是自动测试内容，@zyf 你好');
    np.append('tags', '求助'); np.append('tags', '讨论'); np.set('captcha_token', tok); np.set('captcha_answer', cans);
    r = await req('/discuss/new', { method: 'POST', body: np, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    const postUrl = (r.t.match(/window\.location\.href='([^']+)'/) || [])[1];
    check('发帖成功', !!postUrl, r.t.slice(0, 120));
    const po = await req(postUrl);
    check('帖子详情含楼层/回复框',
        po.t.includes('发表回复') && (po.t.includes("class='floor'") || po.t.includes('还没有回复')), po.t.length);

    // 回复 + 楼中楼
    const postId = postUrl.split('/').pop();
    await new Promise(r2 => setTimeout(r2, 3200));
    r = await req('/post/' + postId + '/comment', { method: 'POST', body: 'content=' + encodeURIComponent('first reply'), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    let po2 = await req('/post/' + postId);
    const cid = (po2.t.match(/id='c([a-z0-9]+)'/) || [])[1];
    check('回复成为 1 楼', !!cid, po2.t.length);
    await new Promise(r2 => setTimeout(r2, 3200));
    r = await req('/post/' + postId + '/comment', { method: 'POST', body: 'parentId=' + cid + '&content=' + encodeURIComponent('sub reply @zyf'), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    po2 = await req('/post/' + postId);
    check('楼中楼渲染', po2.t.includes('subfloor'), po2.t.length);

    // 收藏 / 点赞
    await req('/post/' + postId + '/favorite', { method: 'POST' });
    po2 = await req('/post/' + postId);
    check('收藏状态切换', po2.t.includes('已收藏'), po2.t.length);
    await req('/post/' + postId + '/like', { method: 'POST' });
    po2 = await req('/post/' + postId);
    check('点赞生效', />👍 1</.test(po2.t), po2.t.length);

    // 管理操作：精华 + 置顶 + 锁定
    const op = (v) => req('/post/' + postId + '/admin', { method: 'POST', body: 'op=' + v, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    await op('essence');
    await op('pin');
    po2 = await req('/post/' + postId);
    check('精华/置顶标记', po2.t.includes('精华') && po2.t.includes('置顶'), '');
    const listAfter = await req('/forum/academic');
    const rows = [...listAfter.t.matchAll(/href='\/post\/(\d+)'/g)].map(m => m[1]);
    check('置顶帖排在最前', rows.indexOf(postId) === 0, 'first=' + rows[0]);
    await op('lock');
    const locked = await req('/post/' + postId);
    check('锁定后禁止回复', locked.t.includes('已被锁定'), '');

    // 编辑
    const ed = await req('/post/' + postId + '/edit');
    check('编辑页可打开', ed.t.includes('保存修改'), ed.t.length);

    // ---------- AI（DeepSeek）----------
    const me = await req('/llm/api/me');
    check('/llm/api/me 可用', me.status === 200 && me.t.includes('tokens'), me.t.slice(0, 80));
    const models = await req('/llm/api/models');
    check('模型列表为 DeepSeek', models.t.includes('deepseek-chat') && !models.t.includes('qwen'), models.t.slice(0, 120));
    const chatRes = await fetch(HOST + '/llm/api/chat', {
        method: 'POST', headers: { cookie: sess, 'Content-Type': 'application/json' },
        body: JSON.stringify({ message: '只回复两个字：好的', model: 'deepseek-chat' })
    });
    const stream = await chatRes.text();
    const gotContent = /"content":/.test(stream);
    const gotMeta = /"type":"meta"/.test(stream);
    check('DeepSeek 流式返回内容', gotContent, stream.slice(-200));
    check('返回计费 meta', gotMeta, stream.slice(-200));

    // ---------- SPJ ----------
    const spjCode = `#include <cstdio>
#include <cstdlib>
#include <string>
static std::string rd(const char* p){FILE*f=fopen(p,"rb");if(!f)return "";std::string s;char b[4096];size_t n;while((n=fread(b,1,sizeof(b),f))>0)s.append(b,n);fclose(f);return s;}
static std::string trim(std::string s){while(!s.empty()&&isspace((unsigned char)s.back()))s.pop_back();size_t i=0;while(i<s.size()&&isspace((unsigned char)s[i]))i++;return s.substr(i);}
int main(int argc,char*argv[]){
  if(argc<4)return 2;
  std::string in=rd(argv[1]), out=rd(argv[2]), usr=rd(argv[3]);
  // 题目：输出两数之和；SPJ 只比较数值是否相等
  int a=0,b=0; sscanf(in.c_str(),"%d %d",&a,&b);
  std::string u=trim(usr);
  int got=atoi(u.c_str());
  if(got==a+b) printf("ok sum=%d\\n",got); else printf("expected %d got %d\\n",a+b,got);
  return got==a+b?0:1;
}`;
    const pf = new URLSearchParams();
    pf.set('id', 'SPJTEST' + (Date.now() % 100000));
    pf.set('title', 'SPJ 测试题 ' + Date.now());
    pf.set('description', '输出两数之和');
    pf.set('inputDesc', '两个整数');
    pf.set('outputDesc', '和');
    pf.set('timeLimit', '1000');
    pf.set('memLimit', '256');
    pf.set('hidden', '0');
    pf.set('spj', spjCode);
    r = await req('/admin/problems/add', { method: 'POST', body: pf, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
    check('新建 SPJ 题目', r.status === 200, r.t.slice(0, 120));

    console.log('\n==== 汇总 ====');
    console.log('PASS', pass.length, 'FAIL', fail.length);
    if (fail.length) { console.log('失败项：'); fail.forEach(f => console.log(' - ' + f)); process.exitCode = 1; }
})();

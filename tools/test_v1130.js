// v1.13.0 端到端回归：共享协作 / IDE 云盘 / 编辑 API / OpenAI 兼容（先跑 tools/_login.js）：共享协作 / IDE 云盘 / 编辑 API / OpenAI 兼容
const fs = require('fs');
const BASE = 'http://127.0.0.1:8083';
const CK = fs.readFileSync(__dirname + '/_ck.txt', 'utf8').trim();
const H = { 'Cookie': 'SESSION=' + CK };
let pass = 0, fail = 0;
function ok(cond, name, extra) {
  if (cond) { pass++; console.log('PASS ' + name); }
  else { fail++; console.log('FAIL ' + name + (extra ? ' :: ' + extra : '')); }
}
async function form(path, obj, extraHeaders) {
  const body = new URLSearchParams(obj || {});
  const r = await fetch(BASE + path, { method: 'POST', headers: Object.assign({
    'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' }, H, extraHeaders || {}), body });
  return { status: r.status, text: await r.text() };
}
async function get(path, extraHeaders) {
  const r = await fetch(BASE + path, { headers: Object.assign({}, H, extraHeaders || {}) });
  return { status: r.status, text: await r.text(), type: r.headers.get('content-type') || '' };
}
(async () => {
  // 清理旧测试目录
  await form('/drive/delete', { dir: '', paths: 'collabtest' }, {});
  await form('/drive/mkdir', { dir: '', name: 'collabtest' }, {});
  await form('/drive/delete', { dir: 'collabtest', paths: 'a.txt' }, {});
  let r = await form('/drive/mkdir', { dir: '', name: 'collabtest' });
  ok(r.status === 200, 'mkdir collabtest');
  r = await form('/drive/newfile', { dir: 'collabtest', name: 'a.txt', content: 'hello world\nline2\n' },
    { 'X-Requested-With': 'fetch' });
  ok(r.text.includes('"ok":true'), 'newfile a.txt', r.text);

  // 分享（允许编辑）
  r = await form('/drive/share', { dir: '', action: 'share', file: 'collabtest', allowEdit: '1' },
    { 'X-Requested-With': 'fetch' });
  let j; try { j = JSON.parse(r.text); } catch (e) { j = {}; }
  const token = j.token;
  ok(!!token, 'share folder with edit', r.text);

  // 属性里应该有 perm=edit
  r = await get('/drive/props?p=collabtest');
  ok(r.text.includes('"perm":"edit"'), 'props perm=edit', r.text.slice(0, 200));

  // 共享页
  r = await get('/share/' + token);
  ok(r.status === 200 && r.text.includes('协作编辑已开启'), 'share page editable badge');
  ok(r.text.includes('/share/' + token + '/edit?p=a.txt'), 'share page edit button');

  // 锁列表（空）
  r = await get('/share/' + token + '/locks');
  ok(r.text.includes('"ok":true') && r.text.includes('"locks":[]'), 'locks empty', r.text);

  // 加锁
  r = await form('/share/' + token + '/lock', { p: 'a.txt' });
  ok(r.text.includes('"ok":true'), 'lock acquired', r.text);
  r = await get('/share/' + token + '/locks');
  ok(r.text.includes('"mine":true'), 'lock shows mine', r.text);

  // 保存（带 rev 冲突检测）
  r = await form('/share/' + token + '/save', { p: 'a.txt', content: 'hello v2\n', rev: 'bad-rev' });
  ok(r.text.includes('"conflict":true'), 'save conflict detected', r.text);
  r = await form('/share/' + token + '/save', { p: 'a.txt', content: 'hello v2\n', rev: '' });
  ok(r.text.includes('"ok":true'), 'save ok', r.text);
  r = await get('/share/' + token + '/raw?p=a.txt');
  ok(r.text === 'hello v2\n', 'saved content readable', JSON.stringify(r.text));

  // 编辑器页
  r = await get('/share/' + token + '/edit?p=a.txt');
  ok(r.status === 200 && r.text.includes('保存 (Ctrl+S)') && r.text.includes('hello v2'), 'editor page');

  // 新建文件 / 删除
  r = await form('/share/' + token + '/newfile', { p: '', name: 'b.cpp', content: 'int main(){}' });
  ok(r.text.includes('"ok":true'), 'share newfile', r.text);
  r = await form('/share/' + token + '/delete', { p: 'b.cpp' });
  ok(r.text.includes('"ok":true'), 'share delete', r.text);

  // 关掉编辑权限后应被拒
  r = await form('/drive/share', { dir: '', action: 'share', file: 'collabtest', allowEdit: '0' },
    { 'X-Requested-With': 'fetch' });
  r = await form('/share/' + token + '/lock', { p: 'a.txt' });
  ok(r.text.includes('没有开启协作编辑'), 'readonly blocks lock', r.text);
  // 恢复可编辑
  await form('/drive/share', { dir: '', action: 'share', file: 'collabtest', allowEdit: '1' },
    { 'X-Requested-With': 'fetch' });

  // ---------- IDE ↔ 云盘 ----------
  r = await get('/ide/drive.json?dir=collabtest');
  ok(r.text.includes('a.txt'), 'ide drive list', r.text.slice(0, 200));
  r = await get('/ide/read.json?p=collabtest/a.txt');
  ok(r.text.includes('hello v2'), 'ide read', r.text.slice(0, 200));
  r = await form('/ide/save', { path: 'collabtest/ide.txt', code: '#include <bits/stdc++.h>\nint main(){return 0;}\n' });
  ok(r.text.includes('"ok":true'), 'ide save', r.text);
  r = await get('/ide/read.json?p=collabtest/ide.txt');
  ok(r.text.includes('bits/stdc++'), 'ide save roundtrip');

  // 提交验证码
  r = await get('/api/captcha?feature=submit');
  ok(r.text.includes('"required"') && r.text.includes('"token"'), 'captcha api', r.text);

  // ---------- API 密钥 / 编辑 API / OpenAI 兼容 ----------
  r = await form('/llm/api/keys', { action: 'create', name: 'test-key' });
  let key = '';
  try { key = JSON.parse(r.text).key || ''; } catch (e) {}
  ok(key.startsWith('oi-'), 'create api key', r.text.slice(0, 120));
  const AH = { 'Authorization': 'Bearer ' + key };
  r = await get('/v1/models', AH);
  ok(r.text.includes('"object":"list"'), 'v1 models', r.text.slice(0, 150));
  r = await get('/api/ai/read?root=drive&path=collabtest/a.txt', AH);
  ok(r.text.includes('hello v2'), 'api ai read', r.text.slice(0, 150));
  r = await form('/api/ai/edit', {}, AH);
  const er = await fetch(BASE + '/api/ai/edit', { method: 'POST', headers: Object.assign({ 'Content-Type': 'application/json' }, AH),
    body: JSON.stringify({ root: 'drive', path: 'collabtest/ide.txt', old_string: 'return 0;', new_string: 'return 7;' }) });
  const et = await er.text();
  ok(et.includes('"ok":true'), 'api ai edit', et.slice(0, 200));
  r = await get('/api/ai/read?root=drive&path=collabtest/ide.txt', AH);
  ok(r.text.includes('return 7;'), 'api ai edit persisted', r.text.slice(0, 200));
  r = await get('/v1/models');
  ok(r.status === 401, 'v1 requires key');

  // 真实一次 /v1/responses（走上游；失败也算通过"结构"检查）
  try {
    const rr = await fetch(BASE + '/v1/responses', { method: 'POST', headers: Object.assign({ 'Content-Type': 'application/json' }, AH),
      body: JSON.stringify({ model: process.env.TEST_MODEL || 'glm-5.3-flash', input: '只回复两个字：你好' }) });
    const tt = await rr.text();
    console.log('INFO /v1/responses status=' + rr.status + ' body=' + tt.slice(0, 260));
    ok(rr.status === 200 && (tt.includes('"object":"response"') || tt.includes('"error"')), 'v1 responses shape');
  } catch (e) { console.log('INFO responses error ' + e.message); }

  // 清理：吊销测试密钥 + 删除协作测试目录
  r = await get('/llm/api/keys');
  try {
    const ks = JSON.parse(r.text).keys || [];
    for (const k of ks) if (k.name === 'test-key' && !k.revoked)
      await form('/llm/api/keys', { action: 'revoke', key: k.mask });
  } catch (e) {}
  await form('/drive/delete', { dir: '', paths: 'collabtest' });

  console.log('\nRESULT pass=' + pass + ' fail=' + fail);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FATAL ' + e.stack); process.exit(2); });

const http = require('http');
const HOST = '127.0.0.1', PORT = 8083;
function req(path, opts = {}) {
  return new Promise((res, rej) => {
    const r = http.request({ host: HOST, port: PORT, path, method: opts.method || 'GET', headers: opts.headers || {} }, x => {
      let d = ''; x.on('data', c => d += c); x.on('end', () => res({ code: x.statusCode, headers: x.headers, body: d }));
    });
    r.on('error', rej); if (opts.body) r.write(opts.body); r.end();
  });
}
(async () => {
  const g = await req('/login');
  const tok = (g.body.match(/name='captcha_token' value='([^']+)'/) || [])[1];
  const q = (g.body.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
  const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
  let ans = '';
  if (m) { const a = +m[1], b = +m[3]; ans = String(m[2] === '+' ? a + b : m[2] === '-' ? a - b : m[2] === 'x' ? a * b : Math.floor(a / b)); }
  await new Promise(r => setTimeout(r, 1400));
  const body = `username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`;
  const r = await req('/login', { method: 'POST', body, headers: { 'Content-Type': 'application/x-www-form-urlencoded', 'Content-Length': Buffer.byteLength(body) } });
  const sc = (r.headers['set-cookie'] || []).map(x => x.split(';')[0]).find(x => x.startsWith('SESSION=')) || '';
  const sess = sc.split('=')[1] || '';
  require('fs').writeFileSync(require('path').join(__dirname, '_ck.txt'), sess);
  const mm = await req('/llm/api/models', { headers: { Cookie: 'SESSION=' + sess } });
  console.log('CK=' + sess);
  console.log('MODELS=' + mm.body.slice(0, 260));
})();

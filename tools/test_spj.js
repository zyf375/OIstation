// SPJ 端到端测试：造数据 -> 提交正解/错解 -> 直接读 submissions.json 校验判题结果
const HOST='http://127.0.0.1:8083';
const fs=require('fs');
const path=require('path');
const ROOT='E:/LL/OIstation';

async function login(user, pw) {
    const g = await fetch(HOST + '/login'); const html = await g.text();
    const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let ans=''; if(m){const a=+m[1],b=+m[3];ans=String(m[2]==='+'?a+b:m[2]==='-'?a-b:m[2]==='x'?a*b:Math.floor(a/b));}
    await new Promise(r=>setTimeout(r,1400));
    const body=`username=${user}&password=${pw}&captcha_token=${tok}&captcha_answer=${ans}`;
    const r=await fetch(HOST+'/login',{method:'POST',body,headers:{'Content-Type':'application/x-www-form-urlencoded'}});
    return (r.headers.getSetCookie?r.headers.getSetCookie():[]).map(s=>s.split(';')[0]).filter(s=>s.startsWith('SESSION=')).join('; ');
}
const mk=(sess)=>async(p,o={})=>{const r=await fetch(HOST+p,{headers:Object.assign({cookie:sess},o.headers||{}),redirect:'manual',method:o.method||'GET',body:o.body});return{status:r.status,t:await r.text()};};
function readJsonLines(f){ return fs.readFileSync(f,'utf8').split('\n').filter(Boolean).map(l=>JSON.parse(l)); }
function statusName(v){ return ['PENDING','JUDGING','AC','WA','TLE','MLE','CE','RE'][v]||v; }

(async()=>{
  const sess = await login('admin','20121202');
  const req = mk(sess);
  const probs = readJsonLines(path.join(ROOT,'data/problems.json'));
  const spj = probs.filter(p=>p.spj && p.spj.length>10).pop();
  console.log('SPJ problem:', spj && spj.id, '|', spj && spj.title);
  if(!spj) return;
  const dir = path.join(ROOT,'problems',spj.id);
  fs.mkdirSync(dir,{recursive:true});
  fs.writeFileSync(path.join(dir,'1.in'),'3 5\n');
  fs.writeFileSync(path.join(dir,'1.out'),'8\n');
  fs.writeFileSync(path.join(dir,'2.in'),'100 200\n');
  fs.writeFileSync(path.join(dir,'2.out'),'999\n');   // 标准答案故意错，SPJ 必须能判对
  console.log('数据写入', dir, '(2.out 故意错，用于验证 SPJ 生效)');

  async function submit(code){
    const g = await req('/problem/'+spj.id+'/submit');
    const tok = (g.t.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (g.t.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const mm = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let ans=''; if(mm){const a=+mm[1],b=+mm[3];ans=String(mm[2]==='+'?a+b:mm[2]==='-'?a-b:mm[2]==='x'?a*b:Math.floor(a/b));}
    await new Promise(r=>setTimeout(r,1300));
    const f = new URLSearchParams();
    f.set('problemId',spj.id); f.set('language','cpp'); f.set('code',code);
    f.set('captcha_token',tok||''); f.set('captcha_answer',ans);
    const r = await req('/submit',{method:'POST',body:f,headers:{'Content-Type':'application/x-www-form-urlencoded'}});
    const ok = r.t.includes('提交成功');
    console.log('  submit:', ok ? 'ok' : 'FAILED ' + ((r.t.match(/alert alert-error'>([^<]*)/)||[])[1]||''));
    return ok;
  }
  async function newestSub(){
    const subs = readJsonLines(path.join(ROOT,'data/submissions.json'));
    const mine = subs.filter(s=>s.problemId===spj.id && s.userId==='1');
    return mine[mine.length-1];
  }
  async function waitDone(prevId){
    for(let i=0;i<60;i++){
      await new Promise(x=>setTimeout(x,500));
      const s = await newestSub();
      if (s && s.id!==prevId && s.status!=='1' && s.status!=='0') return s;
    }
    return null;
  }

  const before = await newestSub();
  await submit('#include <cstdio>\nint main(){int a,b;if(scanf("%d%d",&a,&b)!=2)return 0;printf("%d\\n",a+b);return 0;}\n');
  let s = await waitDone(before && before.id);
  console.log('正解判定:', s && statusName(+s.status), '|', s && (s.judgeResult||'').replace(/\n/g,' | ').slice(0,220));

  await new Promise(r=>setTimeout(r,11000));
  await submit('#include <cstdio>\nint main(){printf("0\\n");return 0;}\n');
  s = await waitDone(s && s.id);
  console.log('错解判定:', s && statusName(+s.status), '|', s && (s.judgeResult||'').replace(/\n/g,' | ').slice(0,220));
})();

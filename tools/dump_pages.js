const HOST='http://127.0.0.1:8083';
const fs=require('fs');
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
(async()=>{
  const sess=await login('admin','20121202');
  const r=await fetch(HOST+'/admin/ai',{headers:{cookie:sess}});
  let t=await r.text();
  t=t.replace(/href='\/static\//g,'href=\''+HOST+'/static/').replace(/src='\/static\//g,'src=\''+HOST+'/static/');
  fs.writeFileSync('E:/LL/OIstation/temp/admin_ai.html', t, 'utf8');
  const chat=await fetch(HOST+'/llm/chat',{headers:{cookie:sess}});
  let c=await chat.text();
  c=c.replace(/href="\/static\//g,'href="'+HOST+'/static/').replace(/src="\/static\//g,'src="'+HOST+'/static/');
  fs.writeFileSync('E:/LL/OIstation/temp/llm_chat.html', c, 'utf8');
  console.log('saved', t.length, c.length);
})();

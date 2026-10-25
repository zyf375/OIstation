const BASE='http://127.0.0.1:8083';let cookie='';
async function req(method,path,body,ctype){const h={};if(cookie)h.Cookie=cookie;if(ctype)h['Content-Type']=ctype;const r=await fetch(BASE+path,{method,headers:h,body,redirect:'manual'});const sc=r.headers.get('set-cookie');if(sc)cookie=sc.split(';')[0];return {status:r.status,text:await r.text()};}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  let r=await req('GET','/login');const t=r.text;const tok=(t.match(/name='captcha_token' value='([^']+)'/)||[])[1];const q=(t.match(/验证码：(\d+)\s*([+\-*\/×÷])\s*(\d+)/)||[]);const op=q[2]==='×'?'*':q[2]==='÷'?'/':q[2];let ans=op==='+'?+q[1]+ +q[3]:op==='-'?+q[1]-+q[3]:op==='*'?+q[1]*+q[3]:Math.floor(+q[1]/+q[3]);
  await sleep(1400);await req('POST','/login',`username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`,'application/x-www-form-urlencoded');
  let a=await req('GET','/admin/ai?q=admin');console.log('search page',a.status,a.text.includes('admin'),a.text.includes('风险'));
  let p=await req('POST','/admin/ai','action=plan&userId=1&planId=member','application/x-www-form-urlencoded');console.log('assign',p.status);
  console.log('me', (await req('GET','/llm/api/me')).text);
  await req('POST','/admin/ai','action=plan&userId=1&planId=','application/x-www-form-urlencoded');
  console.log('revert', (await req('GET','/llm/api/me')).text);
  let pr=await req('GET','/admin/ai/providers?scan=openrouter');console.log('scan page',pr.status,pr.text.includes('OpenRouter 免费池'),pr.text.includes('sk-or-v1'));
})();

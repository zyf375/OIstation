const BASE='http://127.0.0.1:8083';
let cookie='';
async function req(method,path,body,ctype){
  const h={}; if(cookie)h.Cookie=cookie; if(ctype)h['Content-Type']=ctype;
  const r=await fetch(BASE+path,{method,headers:h,body,redirect:'manual'});
  const raw=r.headers.get('set-cookie'); if(raw)cookie=raw.split(';')[0];
  return {status:r.status,text:await r.text(),headers:r.headers};
}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function login(){
  let r=await req('GET','/login');const t=r.text;
  const tok=(t.match(/name='captcha_token' value='([^']+)'/)||[])[1];
  const q=(t.match(/验证码：(\d+)\s*([+\-*\/×÷])\s*(\d+)/)||[]);
  let ans=0;const op=q[2]==='×'?'*':q[2]==='÷'?'/':q[2];
  ans=op==='+'?+q[1]+ +q[3]:op==='-'?+q[1]-+q[3]:op==='*'?+q[1]*+q[3]:Math.floor(+q[1]/+q[3]);
  await sleep(1400);
  r=await req('POST','/login',`username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`,'application/x-www-form-urlencoded');
  console.log('login', !!cookie);
}
async function sse(path,payload){
  const r=await fetch(BASE+path,{method:'POST',headers:{Cookie:cookie,'Content-Type':'application/json'},body:JSON.stringify(payload)});
  const text=await r.text();
  let content='',meta=null,err='';
  for(const line of text.split('\n')){
    if(!line.startsWith('data:'))continue;
    const d=JSON.parse(line.slice(5));
    if(d.message&&d.message.content)content+=d.message.content;
    if(d.type==='meta')meta=d;
    if(d.error)err=d.error;
  }
  return {status:r.status,content,meta,err,raw:text.slice(0,200)};
}
(async()=>{
  await login();
  console.log('scan openrouter:', (await req('GET','/admin/ai/providers?scan=openrouter')).text.includes('可用模型'));
  console.log('chat openrouter...');
  console.log(await sse('/llm/api/chat',{task:'chat',model:'nvidia/nemotron-3-super-120b-a12b:free',message:'只回复两个字：你好',tools:'0'}));
  console.log('models json', (await req('GET','/llm/api/models')).text.slice(0,600));
  console.log('explain#1...');
  const a=await sse('/llm/api/chat',{task:'explain',model:'deepseek-flash',problemId:'1001',level:'普及',message:'',tools:'0'});
  console.log({status:a.status,err:a.err,meta:a.meta,len:a.content.length,head:a.content.slice(0,60)});
  console.log('explain#2 (cache)...');
  const b=await sse('/llm/api/chat',{task:'explain',model:'deepseek-flash',problemId:'1001',level:'普及',message:'',tools:'0'});
  console.log({status:b.status,err:b.err,meta:b.meta,len:b.content.length});
  console.log('provider page leak key?', (await req('GET','/admin/ai/providers')).text.includes('sk-or-v1-29de313'));
  console.log('models json has key?', (await req('GET','/llm/api/models')).text.includes('sk-or'));
})();

const { spawn } = require('child_process');
const fs=require('fs');
const HOST='http://127.0.0.1:8083', CHROME='C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe', PORT=9347;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function login(){
  const g=await fetch(HOST+'/login'); const html=await g.text();
  const tok=(html.match(/name='captcha_token' value='([^']+)'/)||[])[1];
  const q=(html.match(/验证码：([^<]+)<\/label>/)||[])[1]||'';
  const m=q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/); let ans='';
  if(m){const a=+m[1],b=+m[3];ans=String(m[2]==='+'?a+b:m[2]==='-'?a-b:m[2]==='x'?a*b:Math.floor(a/b));}
  await sleep(1400);
  const r=await fetch(HOST+'/login',{method:'POST',body:`username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`,headers:{'Content-Type':'application/x-www-form-urlencoded'}});
  const sc=(r.headers.getSetCookie?r.headers.getSetCookie():[]).map(x=>x.split(';')[0]).find(x=>x.startsWith('SESSION='));
  return sc.split('=')[1];
}
(async()=>{
  const sess=await login();
  const proc=spawn(CHROME,['--headless=new','--disable-gpu','--no-sandbox','--no-first-run','--user-data-dir=E:/LL/OIstation/temp/chrome-flow2','--remote-debugging-port='+PORT,'--window-size=1440,950','about:blank'],{stdio:'ignore'});
  let tabs=null;
  for(let i=0;i<40;i++){await sleep(300);try{tabs=await (await fetch('http://127.0.0.1:'+PORT+'/json/list')).json();if(tabs.length)break;}catch(e){}}
  const ws=new WebSocket(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise(r=>ws.onopen=r);
  let id=0;const pend={};
  ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id&&pend[m.id]){pend[m.id](m.result);delete pend[m.id];}};
  const send=(method,params)=>new Promise(res=>{const i=++id;pend[i]=res;ws.send(JSON.stringify({id:i,method,params:params||{}}));});
  const evalx=async(expr,ap)=>{const r=await send('Runtime.evaluate',{expression:expr,returnByValue:true,awaitPromise:!!ap});if(r.exceptionDetails)return{error:JSON.stringify(r.exceptionDetails).slice(0,300)};return{value:r.result.value};};
  await send('Runtime.enable');await send('Page.enable');
  await send('Network.setCookie',{name:'SESSION',value:sess,domain:'127.0.0.1',path:'/'});
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:950,deviceScaleFactor:1,mobile:false});
  await send('Page.navigate',{url:HOST+'/llm/chat'});
  await sleep(2300);
  console.log('click', await evalx(`(()=>{const cs=[...document.querySelectorAll('#convList .conv')]; const c=cs.reverse().find(x=>/flow_demo/.test(x.innerText))||cs[0]; if(!c) return 'none'; c.click(); return c.innerText;})()`));
  await sleep(1200);
  console.log('flow', await evalx(`(()=>{const f=document.querySelector('.flow'); if(!f) return 'no flow';
    return JSON.stringify({sum:(f.querySelector('summary')||{}).innerText, steps:[...f.querySelectorAll('.fs')].map(x=>x.className.replace('fs fs-','')), prog:(f.querySelector('.fs-prog .fs-body')||{}).innerText||''});})()`));
  const shot=await send('Page.captureScreenshot',{format:'png'});
  fs.writeFileSync('E:/LL/OIstation/temp/s_flow2.png',Buffer.from(shot.data,'base64'));
  ws.close();proc.kill();
})();

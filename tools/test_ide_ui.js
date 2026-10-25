const { spawn } = require('child_process');
const HOST='http://127.0.0.1:8083';
const CHROME='C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const PORT=9361, RUN_ID=process.env.RUN_ID||'18821412517180151000004';
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const pass=[],fail=[];
function check(n,ok,e){ (ok?pass:fail).push(n); console.log((ok?'PASS ':'FAIL ')+n+(ok?'':'  '+(e||''))); }
async function login(){
  const g=await fetch(HOST+'/login');const html=await g.text();
  const tok=(html.match(/name='captcha_token' value='([^']+)'/)||[])[1];
  const q=(html.match(/验证码：([^<]+)<\/label>/)||[])[1]||'';
  const m=q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
  let ans=''; if(m){const a=+m[1],b=+m[3];ans=String(m[2]==='+'?a+b:m[2]==='-'?a-b:m[2]==='x'?a*b:Math.floor(a/b));}
  await sleep(1400);
  const r=await fetch(HOST+'/login',{method:'POST',body:`username=admin&password=20121202&captcha_token=${tok}&captcha_answer=${ans}`,headers:{'Content-Type':'application/x-www-form-urlencoded'}});
  const raw=r.headers.get('set-cookie')||''; return (raw.split(';')[0]||'').split('=')[1];
}
class CDP{
  constructor(ws){this.ws=ws;this.id=0;this.pending={};this.events=[];ws.onmessage=ev=>{const m=JSON.parse(ev.data);if(m.id&&this.pending[m.id]){const p=this.pending[m.id];delete this.pending[m.id];m.error?p.rej(new Error(JSON.stringify(m.error))):p.res(m.result);}else if(m.method)this.events.push(m);};}
  send(method,params){const id=++this.id;return new Promise((res,rej)=>{this.pending[id]={res,rej};this.ws.send(JSON.stringify({id,method,params:params||{}}));});}
  eval(expr){return this.send('Runtime.evaluate',{expression:expr,returnByValue:true,awaitPromise:true}).then(x=>x.result||{});}
}
(async()=>{
  const sess=await login(); if(!sess){console.log('login failed');return;}
  const userDir=process.env.TEMP+'\\cdp-test-'+Date.now();
  const proc=spawn(CHROME,['--headless=new','--disable-gpu','--no-sandbox','--no-first-run','--user-data-dir='+userDir,'--remote-debugging-port='+PORT,'--window-size=1440,1000','about:blank'],{stdio:'ignore'});
  let tabs=null;
  for(let i=0;i<40;i++){await sleep(300);try{tabs=await (await fetch('http://127.0.0.1:'+PORT+'/json/list')).json();if(tabs.length)break;}catch(e){}}
  const ws=new WebSocket(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise(r=>ws.onopen=r);
  const cdp=new CDP(ws);
  await cdp.send('Runtime.enable');await cdp.send('Page.enable');await cdp.send('Log.enable');
  await cdp.send('Network.setCookie',{name:'SESSION',value:sess,domain:'127.0.0.1',path:'/'});
  // KaTeX on explain history page
  await cdp.send('Page.navigate',{url:HOST+'/ai/explain?run='+RUN_ID});
  await sleep(3500);
  let dbg=await cdp.eval('location.href+"|"+document.readyState+"|"+document.body.innerText.length'); console.log('DBG',JSON.stringify(dbg));
  let r=await cdp.eval('document.querySelectorAll(".katex").length');
  check('KaTeX 公式已渲染', r.value>0, 'katex='+r.value);
  r=await cdp.eval('document.querySelector("#out").innerText.slice(0,80)');
  check('讲解内容存在', (r.value||'').length>20, r.value);
  // IDE page
  const errs=cdp.events.filter(e=>e.method==='Runtime.exceptionThrown');
  await cdp.send('Page.navigate',{url:HOST+'/run?pid=1001'});
  await sleep(3000);
  r=await cdp.eval('typeof CodeMirror'); check('CodeMirror 已加载', r.value==='function', r.value);
  r=await cdp.eval('document.querySelectorAll(".CodeMirror").length'); check('编辑器已渲染', r.value>0, r.value);
  r=await cdp.eval('document.querySelectorAll("#caseBar .case-pill").length'); check('样例测试点已加载', r.value>=2, r.value);
  r=await cdp.eval('document.getElementById("pidSel").value'); check('题目已选中', r.value==='1001', r.value);
  // type code and run
  await cdp.eval('(()=>{var cm=document.querySelector(".CodeMirror").CodeMirror;cm.setValue(\'#include <bits/stdc++.h>\\nusing namespace std;\\nint main(){int a,b;cin>>a>>b;cout<<a+b;return 0;}\');document.getElementById("runBtn").click();return 1;})()');
  await sleep(4500);
  r=await cdp.eval('document.querySelectorAll("#caseBar .dot.AC").length');
  check('自测样例 AC', r.value>=1, 'ac='+r.value);
  r=await cdp.eval('document.getElementById("casePanel").innerText.slice(0,120)');
  check('结果面板显示输出', /50|AC/.test(r.value||''), r.value);
  const ex2=cdp.events.filter(e=>e.method==='Runtime.exceptionThrown');
  check('无 JS 异常', ex2.length===0, ex2.map(e=>e.params.exceptionDetails.text).join('|'));
  console.log('SUMMARY',pass.length,'PASS /',fail.length,'FAIL');
  try{await cdp.send('Browser.close');}catch(e){}
  try{proc.kill();}catch(e){}
  process.exit(0);
})().catch(e=>{console.error('ERR',e);process.exit(1)});

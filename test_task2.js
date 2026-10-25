const http=require('http');
function req(method,path,body,cookie,ctype='application/x-www-form-urlencoded'){
  return new Promise((res,rej)=>{
    const data=body?Buffer.from(body):null;
    const o={method,hostname:'127.0.0.1',port:8082,path,headers:{}};
    if(cookie)o.headers['Cookie']=cookie;
    if(data){o.headers['Content-Length']=data.length;o.headers['Content-Type']=ctype;}
    const r=http.request(o,resp=>{
      const h={status:resp.statusCode,headers:resp.headers};
      const ch=[];resp.on('data',c=>ch.push(c));resp.on('end',()=>res({...h,body:Buffer.concat(ch).toString('utf8')}));
    });
    r.on('error',rej);
    if(data) r.end(data); else r.end();
  });
}
function sc(h){const a=h.headers['set-cookie'];return a?String(a).split(';')[0]:'';}
function strip(html){return html.replace(/<[^>]+>/g,' ').replace(/\s+/g,' ').trim();}

(async()=>{
  const A=await req('POST','/login','username=admin&password=admin123',null); let ckA=sc(A);
  const B=await req('POST','/login','username=zyf&password=12345678',null); let ckB=sc(B);
  console.log('admin cookie:',ckA?ckA.slice(0,30):'NONE', ' zyf cookie:',ckB?ckB.slice(0,30):'NONE');

  // 1) /messages admin
  let r=await req('GET','/messages',null,ckA); console.log('\n[1] /messages status',r.status,'has chat-layout:',r.body.includes('chat-layout'),'has 系统通知:',r.body.includes('系统通知'));
  // 2) /messages/system admin
  r=await req('GET','/messages/system',null,ckA); console.log('[2] /messages/system status',r.status,'chat:',r.body.includes('chat-layout'),'notifCount=',(r.body.match(/class='bubble/g)||[]).length);
  // 3) /tasks admin
  r=await req('GET','/tasks',null,ckA); console.log('[3] /tasks status',r.status,'columns:',r.body.includes('task-columns'),'进行中:',r.body.includes('进行中'),'已关闭:',r.body.includes('已关闭'));

  // 4) create task
  r=await req('POST','/task/new','title=测试任务-多帮助者&description=这是描述&reward=50',ckA);
  const tid=(r.body.match(/\/task\/([0-9]+)/)||[])[1];
  console.log('[4] create task, tid=',tid, 'redirect?', r.body.includes('查看任务'));
  if(!tid){console.log('no task id, abort');return;}

  // 5) zyf accept
  r=await req('POST','/task/'+tid+'/accept','',ckB); console.log('[5] zyf accept ->', r.body.includes('/task/'+tid));
  // 6) helper sends private msg
  r=await req('POST','/task/'+tid+'/msg','toId=1&content=帮助者给发布者的私信',ckB); console.log('[6] zyf msg ->', r.body.includes('/task/'+tid));
  // 7) admin sees helper msg
  r=await req('GET','/task/'+tid,null,ckA); console.log('[7] admin detail: helper visible?', r.body.includes('帮助者给发布者的私信'), ' helper row?', r.body.includes('帮助者'));
  // 8) admin replies to helper
  r=await req('POST','/task/'+tid+'/msg','toId=1785480347000000&content=发布者回复给zyf',ckA); console.log('[8] admin reply ->', r.body.includes('/task/'+tid));
  // 9) zyf sees author reply
  r=await req('GET','/task/'+tid,null,ckB); console.log('[9] zyf detail: author reply visible?', r.body.includes('发布者回复给zyf'));
  // 10) zyf submit
  r=await req('POST','/task/'+tid+'/submit','',ckB); console.log('[10] zyf submit ->', r.body.includes('/task/'+tid));
  // 11) admin sees submitted + buttons
  r=await req('GET','/task/'+tid,null,ckA); console.log('[11] admin detail: 确认完成 btn?', r.body.includes('确认完成'), ' 驳回 btn?', r.body.includes('驳回'), ' 关闭任务 btn?', r.body.includes('关闭任务'));
  // 12) admin rejects zyf
  r=await req('POST','/task/'+tid+'/reject','helperId=1785480347000000',ckA); console.log('[12] admin reject ->', r.body.includes('/task/'+tid));
  // 13) task still not closed after reject
  r=await req('GET','/task/'+tid,null,ckA); console.log('[13] after reject: 已驳回 shown?', r.body.includes('已驳回'), ' 关闭任务 still?', r.body.includes('关闭任务'), ' 状态待确认?', r.body.includes('待确认'));
  // 14) zyf re-accept (rejected -> accepted)
  r=await req('POST','/task/'+tid+'/accept','',ckB); console.log('[14] zyf re-accept ->', r.body.includes('/task/'+tid));
  // 15) zyf submit again, admin confirm
  r=await req('POST','/task/'+tid+'/submit','',ckB);
  r=await req('POST','/task/'+tid+'/confirm','helperId=1785480347000000',ckA); console.log('[15] admin confirm ->', r.body.includes('/task/'+tid));
  r=await req('GET','/task/'+tid,null,ckA); console.log('[15b] after confirm: 已完成 shown?', r.body.includes('任务已完成'), ' status 已完成?', r.body.includes('已发放'));
  // 16) done task hidden from list
  r=await req('GET','/tasks',null,ckA); console.log('[16] /tasks contains this task title?', r.body.includes('测试任务-多帮助者'), '(should be false since done hidden)');

  // 17) create another task then close it
  r=await req('POST','/task/new','title=测试任务-待关闭&description=d&reward=30',ckA);
  const tid2=(r.body.match(/\/task\/([0-9]+)/)||[])[1];
  r=await req('POST','/task/'+tid2+'/close','',ckA); console.log('[17] close task ->', r.body.includes('/task/'+tid2));
  r=await req('GET','/tasks',null,ckA); console.log('[17b] closed task appears in 已关闭 col?', r.body.includes('测试任务-待关闭')&&r.body.includes('已关闭'));

  // 18) system notifications received (task events)
  r=await req('GET','/messages/system',null,ckA); console.log('[18] admin system notifications:', (r.body.match(/领取了任务|提交了任务|确认完成|已关闭任务/g)||[]).join(' | ')||'none');
})().catch(e=>console.error('ERR',e.message));

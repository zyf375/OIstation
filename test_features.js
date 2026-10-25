const http=require('http');
function req(method,path,body,hdr={}){
  return new Promise((res,rej)=>{
    const data=body?Buffer.from(body):null;
    const o={method,hostname:'127.0.0.1',port:8082,path,headers:hdr};
    if(data)o.headers['Content-Length']=data.length;
    const r=http.request(o,resp=>{
      const ch=[];resp.on('data',c=>ch.push(c));
      resp.on('end',()=>res({status:resp.statusCode,headers:resp.headers,body:Buffer.concat(ch).toString('utf8')}));
    });
    r.on('error',rej);r.end();
  });
}
function cookieFrom(setc){ if(!setc) return ''; const c=setc[0].split(';')[0]; return c; }
async function login(user,pass){
  const r=await req('POST','/login',`username=${user}&password=${pass}`,{'Content-Type':'application/x-www-form-urlencoded'});
  return {cookie:cookieFrom(r.headers['set-cookie']), status:r.status, loc:r.headers['location']};
}
(async()=>{
  const out=[];
  let a=await login('admin','20121202');
  out.push(['admin login pw20121202', a.status, a.loc, a.cookie.slice(0,20)]);
  if(a.status!==302||!a.cookie){ a=await login('admin','admin123'); out.push(['admin login fallback admin123', a.status, a.loc, a.cookie.slice(0,20)]); }
  const admin=a.cookie;
  const reg=await req('POST','/register','username=qatest&password=123456',{'Content-Type':'application/x-www-form-urlencoded'});
  out.push(['register qatest', reg.status, reg.headers['location']]);
  const q=await login('qatest','123456');
  out.push(['qatest login', q.status, q.cookie.slice(0,20)]);

  const g=await req('POST','/messages/group/new','name=%E6%B5%8B%E8%AF%95%E7%BE%A4&members=qatest',{'Content-Type':'application/x-www-form-urlencoded','Cookie':admin});
  out.push(['create group', g.status, g.headers['location']]);
  const gid=(g.headers['location']||'').split('group/')[1]||'';
  const sm=await req('POST','/message/send','toUser=g'+gid+'&content=hello-group',{'Content-Type':'application/x-www-form-urlencoded','Cookie':admin});
  out.push(['admin send group msg', sm.status, sm.headers['location']]);
  const gv=await req('GET','/messages/chat/group/'+gid, null, {'Cookie':q.cookie});
  out.push(['qatest view group', gv.status, gv.body.includes('hello-group')?'msg-visible':'msg-MISSING']);
  const sm2=await req('POST','/message/send','toUser=g'+gid+'&content=reply-from-qatest',{'Content-Type':'application/x-www-form-urlencoded','Cookie':q.cookie});
  out.push(['qatest send group msg', sm2.status, sm2.headers['location']]);

  const pollOpts=encodeURIComponent('A\nB\nC');
  const d=await req('POST','/discuss/new',`title=${encodeURIComponent('投票测试')}&content=${encodeURIComponent('测试内容')}&pollTitle=${encodeURIComponent('你喜欢哪个?')}&pollOptions=${pollOpts}`,{'Content-Type':'application/x-www-form-urlencoded','Cookie':admin});
  out.push(['create discuss w/poll', d.status, d.headers['location']]);
  const pid=(d.headers['location']||'').split('/post/')[1]||'';
  const pv=await req('GET','/post/'+pid, null, {'Cookie':q.cookie});
  out.push(['qatest view post', pv.status, pv.body.includes('投票')?'poll-form-present':(pv.body.includes('参与投票')?'poll-results':'poll-MISSING')]);
  const vote=await req('POST','/post/'+pid+'/vote','choice=0',{'Content-Type':'application/x-www-form-urlencoded','Cookie':q.cookie});
  out.push(['qatest vote', vote.status, vote.headers['location']]);
  const pv2=await req('GET','/post/'+pid, null, {'Cookie':q.cookie});
  out.push(['post after vote', pv2.status, pv2.body.includes('参与投票')?'results-shown':'not-shown', pv2.body.includes('1 票')?'1vote-count':'?']);

  await req('POST','/post/'+pid+'/like','',{'Cookie':q.cookie});
  const like1=await req('GET','/post/'+pid, null, {'Cookie':q.cookie});
  const m1=like1.body.match(/点赞 \((\d+)\)|已点赞 \((\d+)\)/);
  await req('POST','/post/'+pid+'/like','',{'Cookie':q.cookie});
  const like2=await req('GET','/post/'+pid, null, {'Cookie':q.cookie});
  const m2=like2.body.match(/点赞 \((\d+)\)|已点赞 \((\d+)\)/);
  out.push(['like after1', JSON.stringify(m1&&m1.slice(1,3)), 'after2', JSON.stringify(m2&&m2.slice(1,3))]);

  const drv=await req('GET','/drive', null, {'Cookie':admin});
  out.push(['admin drive', drv.status, drv.body.includes('总配额')?'quota-shown':'?']);

  out.forEach(o=>console.log(JSON.stringify(o)));
  console.log('gid=',gid,'pid=',pid);
})().catch(e=>console.error('ERR',e.message));

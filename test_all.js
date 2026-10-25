const http=require('http');
function req(m,path,b,hdr={}){
  return new Promise((res,rej)=>{
    const data=b?Buffer.from(b):null;
    const o={method:m,hostname:'127.0.0.1',port:8082,path,headers:{...hdr}};
    if(data)o.headers['Content-Type']='application/x-www-form-urlencoded';
    if(data)o.headers['Content-Length']=data.length;
    const t=setTimeout(()=>rej(new Error('TIMEOUT '+m+' '+path)),8000);
    const r=http.request(o,resp=>{clearTimeout(t);const ch=[];resp.on('data',c=>ch.push(c));resp.on('end',()=>res({s:resp.statusCode,loc:resp.headers['location'],setc:resp.headers['set-cookie'],body:Buffer.concat(ch).toString('utf8')}));});
    r.on('error',e=>{clearTimeout(t);rej(e);}); r.end(data);
  });
}
function ck(s){return s?s[0].split(';')[0]:'';}
function parseLoc(body,key){ const m=body.match(new RegExp(key+'\\/([0-9]+)')); return m?m[1]:''; }
async function login(u,p){const r=await req('POST','/login',`username=${u}&password=${p}`);return ck(r.setc);}
const O=(...a)=>console.log(JSON.stringify(a));
(async()=>{
  const admin=await login('admin','20121202'); O('admin login',!!admin);
  // register qatest (idempotent)
  const rr=await req('POST','/register','username=qatest&password=123456&confirm_password=123456'); O('register qatest',rr.s);
  const q=await login('qatest','123456'); O('qatest login',!!q);
  // create group with qatest member (now exists)
  let g=await req('POST','/messages/group/new','name=%E7%BE%A4%E8%81%8A%E6%B5%8B%E8%AF%95&members=qatest',{'Cookie':admin});
  const gid=parseLoc(g.body,'group'); O('create group',g.s,'gid',gid);
  // admin send, qatest view & reply, admin sees reply
  await req('POST','/message/send','toUser=g'+gid+'&content=hello-from-admin',{'Cookie':admin});
  const gv=await req('GET','/messages/chat/group/'+gid,null,{'Cookie':q}); O('qatest view group',gv.s,'sees-admin-msg',gv.body.includes('hello-from-admin'),'member',gv.body.includes('qatest'));
  await req('POST','/message/send','toUser=g'+gid+'&content=reply-qatest',{'Cookie':q});
  const gv2=await req('GET','/messages/chat/group/'+gid,null,{'Cookie':admin}); O('admin sees reply',gv2.s,gv2.body.includes('reply-qatest'));
  // discuss with poll
  let d=await req('POST','/discuss/new',`title=${encodeURIComponent('投票测试')}&content=${encodeURIComponent('内容')}&pollTitle=${encodeURIComponent('选哪个?')}&pollOptions=${encodeURIComponent('A\nB\nC')}`,{'Cookie':admin});
  const pid=parseLoc(d.body,'post'); O('create discuss',d.s,'pid',pid);
  const pv=await req('GET','/post/'+pid,null,{'Cookie':q});
  O('qatest view post',pv.s,'pollform',pv.body.includes("name='choice'"),'polltitle',pv.body.includes('选哪个?'));
  await req('POST','/post/'+pid+'/vote','choice=0',{'Cookie':q});
  const pv2=await req('GET','/post/'+pid,null,{'Cookie':q});
  O('after vote',pv2.s,'results',pv2.body.includes('参与投票'));
  // like toggle by qatest
  await req('POST','/post/'+pid+'/like','',{'Cookie':q});
  const l1=await req('GET','/post/'+pid,null,{'Cookie':q});
  const m1=(l1.body.match(/点赞 \((\d+)\)/)||[])[1];
  await req('POST','/post/'+pid+'/like','',{'Cookie':q});
  const l2=await req('GET','/post/'+pid,null,{'Cookie':q});
  const m2=(l2.body.match(/点赞 \((\d+)\)/)||[])[1];
  O('likes after1:',m1,'after2:',m2);
  // admin drive + shop pages
  const drv=await req('GET','/drive',null,{'Cookie':admin}); O('admin drive',drv.s);
  const adv=await req('GET','/admin/drive',null,{'Cookie':admin}); O('admin drive mgmt',adv.s,'peruser',adv.body.includes('用户个人配额'));
  const sp=await req('GET','/shop',null,{'Cookie':admin}); O('shop page',sp.s,'coeff',sp.body.includes('收益系数'));
  const as=await req('GET','/admin/shop/add',null,{'Cookie':admin}); O('admin shop add',as.s,'coefffield',as.body.includes('coeffBonus'));
  // admin sets per-user drive quota for qatest
  const uid=await req('GET','/admin/drive',null,{'Cookie':admin});
  const qu=await req('POST','/admin/drive','uid=1786880334000004&driveMB=300',{'Cookie':admin}); O('set qatest drive',qu.s);
})().catch(e=>console.error('FATAL',e.message));

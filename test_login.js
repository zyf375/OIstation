const http=require('http');
function req(method,path,body,hdr={},captureAll=false){
  return new Promise((res,rej)=>{
    const data=body?Buffer.from(body):null;
    const o={method,hostname:'127.0.0.1',port:8082,path,headers:hdr};
    if(data)o.headers['Content-Length']=data.length;
    const r=http.request(o,resp=>{
      const h={status:resp.statusCode};
      if(captureAll){h.headers=resp.headers;}
      const ch=[];resp.on('data',c=>ch.push(c));resp.on('end',()=>res({...h,body:Buffer.concat(ch).toString('utf8')}));
    });
    r.on('error',rej);r.end();
  });
}
(async()=>{
  const login=await req('POST','/login','username=admin&password=admin123',{'Content-Type':'application/x-www-form-urlencoded'},true);
  console.log('LOGIN status=',login.status);
  console.log('LOGIN set-cookie=',JSON.stringify(login.headers['set-cookie']));
  console.log('LOGIN location=',login.headers['location']);
  console.log('LOGIN body head=',login.body.replace(/<[^>]+>/g,' ').replace(/\s+/g,' ').trim().slice(0,150));
})().catch(e=>console.error('ERR',e.message));

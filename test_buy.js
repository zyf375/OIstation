const http=require('http');
function req(method,path,body,hdr={}){
  return new Promise((res,rej)=>{
    const data=body?Buffer.from(body):null;
    const o={method,hostname:'127.0.0.1',port:8082,path,headers:hdr};
    if(data)o.headers['Content-Length']=data.length;
    const r=http.request(o,resp=>{
      const sc=resp.headers['set-cookie'];
      if(sc)global.cookie=(Array.isArray(sc)?sc.join(';'):sc).split(';')[0];
      const ch=[];resp.on('data',c=>ch.push(c));resp.on('end',()=>res({status:resp.statusCode,body:Buffer.concat(ch).toString('utf8')}));
    });
    r.on('error',rej);if(data)r.write(data);r.end();
  });
}
(async()=>{
  const login=await req('POST','/login','username=admin&password=admin123',{'Content-Type':'application/x-www-form-urlencoded'});
  const buy=await req('GET','/shop/buy/2',null,{Cookie:global.cookie});
  console.log('BUY status=',buy.status);
  const text=buy.body.replace(/<[^>]+>/g,' ').replace(/\s+/g,' ').trim();
  console.log('BUY text head=',text.slice(0,120));
  const me=await req('GET','/llm/api/me',null,{Cookie:global.cookie});
  console.log('ME=',me.body);
})().catch(e=>console.error('ERR',e.message));

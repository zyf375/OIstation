const http = require('http');
let cookie='';
function req(method,path,body,hdr={}){
  return new Promise((res,rej)=>{
    const data=body?Buffer.from(body):null;
    const o={method,hostname:'127.0.0.1',port:8082,path,headers:hdr};
    if(cookie)o.headers['Cookie']=cookie;
    if(data)o.headers['Content-Length']=data.length;
    const r=http.request(o,resp=>{
      const sc=resp.headers['set-cookie'];
      if(sc)cookie=(Array.isArray(sc)?sc.join(';'):sc).split(';')[0];
      const ch=[];resp.on('data',c=>ch.push(c));resp.on('end',()=>res({status:resp.statusCode,body:Buffer.concat(ch).toString('utf8')}));
    });
    r.on('error',rej);
    if(data)r.write(data);
    r.end();
  });
}
(async()=>{
  const login=await req('POST','/login','username=admin&password=admin123',{'Content-Type':'application/x-www-form-urlencoded'});
  console.log('LOGIN status=',login.status,'hasAdminSession=',/SESSION=/.test(cookie));
  const me=await req('GET','/llm/api/me');
  console.log('ME:',me.body);
  const payload={message:'用一句话介绍你自己',model:'qwen3:0.6b',history:'[]'};
  const body=JSON.stringify(payload);
  const r=http.request({method:'POST',hostname:'127.0.0.1',port:8082,path:'/llm/api/chat',headers:{'Content-Type':'application/json','Content-Length':Buffer.byteLength(body),'Cookie':cookie}},resp=>{
    console.log('STREAM status=',resp.statusCode,'content-type=',resp.headers['content-type']);
    let buf='',content='',chunkCount=0,meta=null;
    resp.on('data',c=>{buf+=c.toString('utf8');chunkCount++;
      let idx;
      while((idx=buf.indexOf('\n'))!==-1){
        const line=buf.slice(0,idx).trim();buf=buf.slice(idx+1);
        if(line.startsWith('data:')){
          try{const j=JSON.parse(line.slice(5).trim());
            if(j.message&&j.message.content)content+=j.message.content;
            if(j.type==='meta')meta=j;
          }catch(e){}
        }
      }
    });
    resp.on('end',()=>{
      console.log('chunkCount=',chunkCount);
      console.log('CONTENT=',content.slice(0,150));
      console.log('META=',JSON.stringify(meta));
    });
  });
  r.on('error',e=>console.log('STREAM_ERR',e.message));
  r.write(body);r.end();
})().catch(e=>console.error('ERR',e.message));

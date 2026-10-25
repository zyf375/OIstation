const http=require('http');
function req(method,path,body,cookie){return new Promise((res,rej)=>{const data=body?Buffer.from(body):null;const o={method,hostname:'127.0.0.1',port:8082,path,headers:{}};if(cookie)o.headers['Cookie']=cookie;if(data){o.headers['Content-Length']=data.length;o.headers['Content-Type']='application/x-www-form-urlencoded';}const r=http.request(o,resp=>{const ch=[];resp.on('data',c=>ch.push(c));resp.on('end',()=>res({status:resp.statusCode,headers:resp.headers,body:Buffer.concat(ch).toString('utf8')}));});r.on('error',rej);if(data)r.end(data);else r.end();});}
function ck(L){const a=L.headers['set-cookie'];return a?String(a).split(';')[0]:'';}
(async()=>{
  const A=await req('POST','/login','username=admin&password=admin123'); let cA=ck(A);
  const B=await req('POST','/login','username=zyf&password=12345678'); let cB=ck(B);
  console.log('admin cookie:',cA?cA.slice(0,18):'NONE');

  // 1) admin main new layout
  let r=await req('GET','/admin',null,cA);
  console.log('[1] /admin:',r.status,'admin-cards:',r.body.includes('admin-cards'),'商城与系统:',r.body.includes('商城与系统'),'购买记录card:',r.body.includes('/admin/orders'));

  // 2) add physical item
  r=await req('POST','/admin/shop/add','name=USB-Gift&description=test-physical&price=10&stock=5&type=1&onSale=1',cA);
  r=await req('GET','/admin/shop',null,cA);
  const idx=r.body.indexOf('USB-Gift');
  console.log('[2b] USB-Gift idx=',idx,' page len=',r.body.length);
  if(idx<0){ console.log('--- admin shop snippet ---'); console.log(r.body.replace(/<[^>]+>/g,' ').replace(/\s+/g,' ').slice(0,600)); }
  const m2=idx>=0?r.body.slice(idx).match(/\/admin\/shop\/edit\/(\d+)/):null;
  const itemId=m2?m2[1]:'';
  console.log('[2] physical item id=',itemId);
  if(!itemId){console.log('no item id');return;}

  // 3) shop page shows 实体 tag for it
  r=await req('GET','/shop',null,cB);
  console.log('[3] shop page has USB-Gift:',r.body.includes('USB-Gift'));

  // 4) GET buy physical -> confirm form
  r=await req('GET','/shop/buy/'+itemId,null,cB);
  console.log('[4] buy GET shows form:',r.body.includes('确认下单'),'quantity field:',r.body.includes("name='quantity'"));

  // 5) POST buy physical
  r=await req('POST','/shop/buy/'+itemId,'quantity=2&contact=ZhangSan-13800138000',cB);
  console.log('[5] buy POST success:',r.body.includes('购买成功'),'physical note:',r.body.includes('管理员将联系你发货'));

  // 6) admin orders page shows pending order
  r=await req('GET','/admin/orders',null,cA);
  console.log('[6] /admin/orders has order:',r.body.includes('USB-Gift'),'contact:',r.body.includes('ZhangSan-13800138000'),'待处理:',r.body.includes('待处理'),'x2:',r.body.includes('x2'));

  // 7) fulfill
  const om=(r.body.match(/\/admin\/order\/update[\s\S]*?value='(\d+)'/g)||[])[0];
  const oid=om?om.match(/'(\d+)'/)[1]:'';
  console.log('[7] first order id=',oid);
  if(oid){
    r=await req('POST','/admin/order/update','id='+oid+'&action=fulfill',cA);
    r=await req('GET','/admin/orders',null,cA);
    console.log('[7b] after fulfill 已发货:',r.body.includes('已发货'));
  }

  // 8) virtual buy still works immediately (GET /shop/buy/2)
  r=await req('GET','/shop/buy/2',null,cB);
  console.log('[8] virtual buy GET immediate success:',r.body.includes('购买成功'));

  // 9) admin main now shows orders badge
  r=await req('GET','/admin',null,cA);
  console.log('[9] /admin 订单 stat + badge:',r.body.includes('订单'),'待处理 badge:',r.body.includes('待处理'));
})().catch(e=>console.error('ERR',e.message));

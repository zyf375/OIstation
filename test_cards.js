const http=require('http');
const fs=require('fs');
function req(m,path,b,hdr={}){
  return new Promise((res,rej)=>{
    const data=b?Buffer.from(b):null;
    const o={method:m,hostname:'127.0.0.1',port:8082,path,headers:{...hdr}};
    if(data)o.headers['Content-Type']='application/x-www-form-urlencoded';
    if(data)o.headers['Content-Length']=data.length;
    const t=setTimeout(()=>rej(new Error('TIMEOUT '+path)),8000);
    const r=http.request(o,resp=>{clearTimeout(t);const ch=[];resp.on('data',c=>ch.push(c));resp.on('end',()=>res({s:resp.statusCode,setc:resp.headers['set-cookie'],body:Buffer.concat(ch).toString('utf8')}));});
    r.on('error',e=>{clearTimeout(t);rej(e);}); r.end(data);
  });
}
function cook(s){return s?s[0].split(';')[0]:'';}
async function login(u,p){const r=await req('POST','/login',`username=${u}&password=${p}`);return cook(r.setc);}
function readUsers(){return fs.readFileSync('F:/LL/OIstation/data/users.json','utf8').trim().split('\n').map(l=>JSON.parse(l));}
function getU(name){return readUsers().find(u=>u.username===name);}
function lastShop(){return JSON.parse(fs.readFileSync('F:/LL/OIstation/data/shops.json','utf8').trim().split('\n').pop());}
const O=(...a)=>console.log(JSON.stringify(a));
const enc=x=>encodeURIComponent(x);
(async()=>{
 const admin=await login('admin','20121202');
 // add 3 card items
 for(const it of [
   ['rename-card','改名卡',1,1,0],
   ['drive-card','云盘增容卡',2,100,7],
   ['coeff-card','收益系数卡',3,50,7]
 ]){
   const [id,name,et,ev,dd]=it;
   const body=`name=${enc(name)}&description=test&price=0&stock=-1&tokenReward=0&effectType=${et}&effectValue=${ev}&durationDays=${dd}&icon=%F0%9F%92%AA&type=0&onSale=1`;
   await req('POST','/admin/shop/add',body,{'Cookie':admin});
 }
 const items=fs.readFileSync('F:/LL/OIstation/data/shops.json','utf8').trim().split('\n').map(l=>JSON.parse(l));
 const byName={}; for(const it of items){ if(it.name==='改名卡'||it.name==='云盘增容卡'||it.name==='收益系数卡') byName[it.name]=it.id; }
 O('item ids', byName);
 // register carduser
 await req('POST','/register','username=carduser&password=abc123&confirm_password=abc123');
 const cu=await login('carduser','abc123'); O('carduser login',!!cu);
 // buy all 3
 for(const nm of ['改名卡','云盘增容卡','收益系数卡']){
   const r=await req('POST','/shop/buy/'+byName[nm],'quantity=1',{'Cookie':cu});
   O('buy '+nm, r.s, 'eff', r.body.includes('到账'));
 }
 let u=getU('carduser');
 O('after buy: renameCards',u.renameCards,'driveBonusMB',u.driveBonusMB,'driveUntil',u.driveBonusUntil,'coeffPct',u.coeffBonusPct,'coeffUntil',u.coeffUntil);
 // checkin -> eff coeff 150 -> gold +45
 await req('POST','/checkin','',{'Cookie':cu});
 let u2=getU('carduser'); O('gold after checkin', u2.gold, '(expect 145)');
 // drive page shows expanded quota
 const dv=await req('GET','/drive',null,{'Cookie':cu}); O('drive page quota', dv.body.includes('增容'), dv.body.includes('100'));
 // rename
 const rn=await req('POST','/user/rename','newUsername=renamed_user',{'Cookie':cu});
 O('rename result', rn.s, rn.body.includes('改名成功'));
 let u3=getU('renamed_user'); O('renamed username', u3&&u3.username, 'renameCards now', u3&&u3.renameCards);
 const rnPage=await req('GET','/user/rename',null,{'Cookie':cu}); O('rename page cards', rnPage.body.includes('改名卡'));
 // shop page shows cards + my cards
 const sp=await req('GET','/shop',null,{'Cookie':cu});
 O('shop shows rename card', sp.body.includes('改名卡'), 'my cards section', sp.body.includes('我的卡片')||sp.body.includes('生效'));
})().catch(e=>console.error('FATAL',e.message));

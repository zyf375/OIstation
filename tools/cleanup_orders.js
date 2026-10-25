const fs=require('fs');
const path=require('path');
const base=path.join(__dirname,'..');
function read(p){return fs.readFileSync(path.join(base,p),'latin1');}
function write(p,s){fs.writeFileSync(path.join(base,p),s,'latin1');}

// 1) remove USB-Gift from shops
let shops=read('data/shops.json').split('\n').filter(l=>l.indexOf('USB-Gift')===-1);
write('data/shops.json',shops.join('\n'));
console.log('shops cleaned');

// 2) empty orders (remove test orders)
let orders=read('data/orders.json').split('\n').filter(l=>l.trim().length>0);
// remove all test orders (ids from session). Keep none? This file was just created by tests; all are test orders.
write('data/orders.json','');
console.log('orders cleaned');

// 3) remove system test notifications from messages
let msg=read('data/messages.json').split('\n').filter(l=>l.indexOf('"fromId":"system"')===-1);
write('data/messages.json',msg.join('\n'));
console.log('system notifications removed');

// 4) restore zyf gold/tokens
let users=read('data/users.json').split('\n').map(l=>{
  if(l.indexOf('"username":"zyf"')!==-1){ l=l.replace(/"gold":"\d+"/,'"gold":"1190"'); l=l.replace(/"tokens":"\d+"/,'"tokens":"318675"'); }
  return l;
});
write('data/users.json',users.join('\n'));
console.log('users restored');

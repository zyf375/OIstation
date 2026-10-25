const fs=require('fs');
let p='data/messages.json';
let s=fs.readFileSync(p,'latin1').split('\n').filter(l=>l.indexOf('"fromId":"system"')===-1).join('\n');
fs.writeFileSync(p,s,'latin1');
console.log('removed system test notifications');

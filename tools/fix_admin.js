const fs=require('fs');
let p='data/users.json';
let s=fs.readFileSync(p,'latin1').split('\n').map(l=>{
  if(l.indexOf('"id":"1"')!==-1){
    l=l.replace(/"username":""/, '"username":"admin"');
  }
  return l;
}).join('\n');
fs.writeFileSync(p,s,'latin1');
console.log('admin username fixed');

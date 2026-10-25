const fs=require('fs');
// 1) restore admin bio/avatar
let users=fs.readFileSync('data/users.json','latin1').split('\n').map(l=>{
  if(l.indexOf('"id":"1"')!==-1){
    l=l.replace(/"bio":"[^"]*"/, '"bio":"\\"this web is build by c++"');
    l=l.replace(/"avatar":"[^"]*"/, '"avatar":""');
  }
  return l;
}).join('\n');
fs.writeFileSync('data/users.json',users,'latin1');
console.log('admin bio/avatar restored');

// 2) remove imported test problems
let probs=fs.readFileSync('data/problems.json','latin1').split('\n').filter(l=>l.indexOf('ImportTestA')===-1 && l.indexOf('ImportTestB')===-1).join('\n');
fs.writeFileSync('data/problems.json',probs,'latin1');
console.log('imported test problems removed');

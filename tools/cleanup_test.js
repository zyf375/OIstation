const fs=require('fs');
const path=require('path');
const base=path.join(__dirname,'..');

// 移除测试任务，还原金币
function read(p){return fs.readFileSync(path.join(base,p),'latin1');}
function write(p,s){fs.writeFileSync(path.join(base,p),s,'latin1');}

let tasks=read('data/tasks.json').split('\n');
const removeIds=['1786789272000002','1786789273000010'];
tasks=tasks.filter(l=>!removeIds.some(id=>l.indexOf('"id":"'+id+'"')!==-1));
write('data/tasks.json',tasks.join('\n'));
console.log('tasks cleaned:',tasks.filter(l=>l.trim()).length,'lines');

let users=read('data/users.json').split('\n');
users=users.map(l=>{
  if(l.indexOf('"username":"admin"')!==-1) l=l.replace(/"gold":"\d+"/,'"gold":"106"');
  if(l.indexOf('"username":"zyf"')!==-1) l=l.replace(/"gold":"\d+"/,'"gold":"1190"');
  return l;
});
write('data/users.json',users.join('\n'));
console.log('users gold restored');

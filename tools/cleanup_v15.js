// 清理本轮测试产生的数据
const fs=require('fs'), path=require('path');
const D='E:/LL/OIstation/data';
const rd=f=>fs.readFileSync(f,'utf8').split('\n').filter(Boolean);
const wr=(f,arr)=>fs.writeFileSync(f, arr.map(x=>typeof x==='string'?x:JSON.stringify(x)).join('\n')+'\n','utf8');

function clean(file, pred){
  const f=path.join(D,file);
  if(!fs.existsSync(f)) return 0;
  const lines=rd(f);
  const keep=lines.filter(l=>{ try{ return !pred(JSON.parse(l)); }catch(e){ return true; } });
  wr(f,keep);
  return lines.length-keep.length;
}
let n=0;
n+=clean('posts.json', o => /^冒烟测试主题/.test(o.title||''));
const removedPapers=new Set();
const f=path.join(D,'papers.json');
if(fs.existsSync(f)){
  for(const l of rd(f)){ try{ const o=JSON.parse(l); if(/^冒烟试卷/.test(o.title||'')) removedPapers.add(o.id);}catch(e){} }
}
n+=clean('papers.json', o => /^冒烟试卷/.test(o.title||''));
n+=clean('paper_subs.json', o => removedPapers.has(o.paperId));
const removedProbs=new Set();
const pf=path.join(D,'problems.json');
if(fs.existsSync(pf)){ for(const l of rd(pf)){ try{ const o=JSON.parse(l); if(String(o.id||'').startsWith('SPJTEST')) removedProbs.add(o.id);}catch(e){} } }
n+=clean('problems.json', o => String(o.id||'').startsWith('SPJTEST'));
n+=clean('submissions.json', o => removedProbs.has(o.problemId));
for(const id of removedProbs){
  const dir=path.join('E:/LL/OIstation/problems', id);
  if(fs.existsSync(dir)) { for(const x of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir,x)); fs.rmdirSync(dir); }
}
// 清掉测试留下的系统消息（含"@ 了你"这类）
n+=clean('messages.json', o => (o.fromId==='system') && /冒烟|测试主题/.test(o.content||''));
console.log('removed lines:', n, '| papers:', [...removedPapers].length, '| problems:', [...removedProbs].join(','));

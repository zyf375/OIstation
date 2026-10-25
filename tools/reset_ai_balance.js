// 一次性迁移：清空旧 AI 余额 / Token，统一按新算力点体系
// 用法：node tools/reset_ai_balance.js
const fs = require('fs');
const stamp = new Date().toISOString().replace(/[:.]/g, '-');
function backup(p) { if (fs.existsSync(p)) fs.copyFileSync(p, p + '.bak_' + stamp); }

const plans = fs.readFileSync('data/ai_plans.json', 'utf8').trim().split('\n')
  .filter(Boolean).map(l => JSON.parse(l));
const free = plans.find(p => p.id === 'free') || { windowQuota: '80', monthQuota: '600' };

const up = 'data/users.json';
backup(up);
const lines = fs.readFileSync(up, 'utf8').trim().split('\n');
let n = 0;
const out = lines.map(l => {
  const u = JSON.parse(l);
  u.tokens = '0';
  if (u.id === '1') {
    u.aiPlanId = '';
    u.aiWindowQuota = '0';
    u.aiMonthQuota = '0';
  } else {
    u.aiPlanId = 'free';
    u.aiWindowQuota = String(free.windowQuota);
    u.aiMonthQuota = String(free.monthQuota);
  }
  n++;
  return JSON.stringify(u);
});
fs.writeFileSync(up, out.join('\n') + '\n');

backup('data/ai_usage.json');
fs.writeFileSync('data/ai_usage.json', '');
backup('data/ai_log.json');
fs.writeFileSync('data/ai_log.json', '');

console.log('reset users=' + n + ', plan free=' + free.windowQuota + '/' + free.monthQuota);

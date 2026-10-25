// 1118 完全平方数
// 给定一个非负整数 N，判断 N 是否是完全平方数（是否存在整数 k 使 k*k=N）。
const { wc, rnd } = require('./_common');

const ID = 1118;
let n = 0;
function isSq(x) {
  const r = Math.floor(Math.sqrt(x));
  return r * r === x || (r + 1) * (r + 1) === x;
}
function t(x) {
  n++;
  wc(ID, n, String(x), isSq(x) ? 'YES' : 'NO');
}

t(0); t(1); t(4); t(9); t(16); t(100); t(100000000000000); t(10); t(999999); t(999999999999999);
// 随机平方数与随机非平方数
for (let i = 0; i < 12; i++) {
  const k = rnd(0, 100000000);      // k*k 最大约 1e16
  t(k * k);
  t(rnd(0, 100000000000000));
}

console.log(`1118: generated ${n} test cases`);

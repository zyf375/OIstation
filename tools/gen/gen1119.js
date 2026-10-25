// 1119 水仙花数（Armstrong / 阿姆斯特朗数）
// 一个 N 位数，若其各位数字的 N 次方之和等于它本身，则为水仙花数。
const { wc, rnd } = require('./_common');

const ID = 1119;
let n = 0;
function isNarc(x) {
  const s = String(x);
  const d = s.length;
  let sum = 0;
  for (const ch of s) { const v = Number(ch); sum += Math.pow(v, d); }
  return sum === x;
}
function t(x) {
  n++;
  wc(ID, n, String(x), isNarc(x) ? 'YES' : 'NO');
}

t(1); t(5); t(153); t(370); t(371); t(407); t(1634); t(8208); t(9474); t(54748); t(123); t(1000000000);
for (let i = 0; i < 8; i++) t(rnd(1, 1000000000));

console.log(`1119: generated ${n} test cases`);

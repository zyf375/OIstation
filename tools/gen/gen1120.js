// 1120 完全数（Perfect Number）
// 一个正整数 N，若其所有真因子（除自身外的正因子）之和等于 N，则为完全数。
const { wc, rnd } = require('./_common');

const ID = 1120;
let n = 0;
function isPerfect(x) {
  if (x <= 1) return false;
  let sum = 1;
  for (let i = 2; i * i <= x; i++) {
    if (x % i === 0) { sum += i; if (i * i !== x) sum += x / i; }
  }
  return sum === x;
}
function t(x) {
  n++;
  wc(ID, n, String(x), isPerfect(x) ? 'YES' : 'NO');
}

t(1); t(2); t(6); t(28); t(496); t(8128); t(33550336); t(10); t(1000000000);
for (let i = 0; i < 10; i++) t(rnd(2, 1000000000));

console.log(`1120: generated ${n} test cases`);

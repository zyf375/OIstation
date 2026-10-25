// 1112 回文数
// 判断一个整数正读与倒读是否相同。
const { wc, rnd } = require('./_common');

const ID = 1112;
let n = 0;
function isPal(x) {
  const s = String(x);
  return s === s.split('').reverse().join('');
}
function t(x) {
  n++;
  wc(ID, n, String(x), isPal(x) ? 'YES' : 'NO');
}

t(0); t(9); t(121); t(1221); t(12321); t(1001); t(10); t(123); t(1000000000); t(123454321);
for (let i = 0; i < 10; i++) t(rnd(0, 1000000000));

console.log(`1112: generated ${n} test cases`);

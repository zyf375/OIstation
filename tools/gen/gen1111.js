// 1111 快乐数（Happy Number）
// 把一个数替换为其各位数字的平方和，反复操作，若能到达 1 则快乐，否则陷入循环。
const { wc, rnd } = require('./_common');

const ID = 1111;
let n = 0;
function isHappy(x) {
  const seen = new Set();
  while (x !== 1) {
    if (seen.has(x)) return false;
    seen.add(x);
    let s = 0;
    while (x > 0) { const d = x % 10; s += d * d; x = Math.floor(x / 10); }
    x = s;
  }
  return true;
}
function t(x) {
  n++;
  wc(ID, n, String(x), isHappy(x) ? 'Happy' : 'Sad');
}

t(1); t(7); t(19); t(2); t(4); t(20); t(91); t(1000000000);
for (let i = 0; i < 9; i++) t(rnd(1, 1000000000));

console.log(`1111: generated ${n} test cases`);

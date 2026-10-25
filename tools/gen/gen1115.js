// 1115 3n+1 问题（Collatz）
// 给定正整数 N，若为偶数则除以 2，为奇数则乘 3 加 1，直到变为 1。
// 输出所需步数。
const { wc, rnd } = require('./_common');

const ID = 1115;
let n = 0;
function steps(x) {
  let c = 0;
  while (x !== 1) {
    if (x % 2 === 0) x = x / 2;
    else x = 3 * x + 1;
    c++;
  }
  return c;
}
function t(x) {
  n++;
  wc(ID, n, String(x), String(steps(x)));
}

t(1); t(2); t(3); t(6); t(27); t(97); t(1000000000);
for (let i = 0; i < 10; i++) t(rnd(1, 1000000000));

console.log(`1115: generated ${n} test cases`);

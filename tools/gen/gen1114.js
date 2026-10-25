// 1114 阶乘末尾零的个数
// 求 N! 的十进制表示末尾有多少个连续的 0。
// 答案 = floor(N/5)+floor(N/25)+floor(N/125)+...
const { wc, rnd } = require('./_common');

const ID = 1114;
let n = 0;
function tz(x) {
  let c = 0;
  while (x >= 5) { x = Math.floor(x / 5); c += x; }
  return c;
}
function t(x) {
  n++;
  wc(ID, n, String(x), String(tz(x)));
}

t(0); t(4); t(5); t(10); t(25); t(100); t(1000); t(1000000000);
for (let i = 0; i < 10; i++) t(rnd(0, 1000000000));

console.log(`1114: generated ${n} test cases`);

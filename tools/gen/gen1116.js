// 1116 数字反转
// 给定一个非负整数 N，输出其十进制表示反转后的整数（去掉前导 0）。
const { wc, rnd } = require('./_common');

const ID = 1116;
let n = 0;
function revint(x) {
  if (x === 0) return 0;
  let r = 0;
  while (x > 0) { r = r * 10 + (x % 10); x = Math.floor(x / 10); }
  return r;
}
function t(x) {
  n++;
  wc(ID, n, String(x), String(revint(x)));
}

t(0); t(5); t(10); t(120); t(1000); t(1200); t(1000000000); t(123456789);
for (let i = 0; i < 10; i++) t(rnd(0, 1000000000));

console.log(`1116: generated ${n} test cases`);

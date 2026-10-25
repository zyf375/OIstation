// 1117 二进制中 1 的个数（popcount）
// 给定一个非负整数 N，输出其二进制表示中 1 的个数。
const { wc, rnd } = require('./_common');

const ID = 1117;
let n = 0;
function pc(x) {
  let c = 0;
  while (x > 0) { if (x & 1) c++; x >>>= 1; }
  return c;
}
function t(x) {
  n++;
  wc(ID, n, String(x), String(pc(x)));
}

t(0); t(1); t(3); t(7); t(15); t(255); t(1024); t(1073741823); t(1000000000);
for (let i = 0; i < 10; i++) t(rnd(0, 1000000000));

console.log(`1117: generated ${n} test cases`);

// 1101 三个数的最大值
const { wc, rnd } = require('./_common');

const ID = 1101;
let n = 0;
function t(a, b, c) {
  n++;
  const out = Math.max(a, b, c);
  wc(ID, n, `${a} ${b} ${c}`, String(out));
}

// 边界/小数据
t(-1000000000, -1000000000, -1000000000); // 全同负数
t(0, 0, 0);                                // 全零
t(1000000000, 1000000000, 1000000000);     // 全同正数
t(-5, 0, 5);                               // 含 0
t(3, 3, -7);                               // 重复值
// 随机数据
for (let i = 0; i < 8; i++) t(rnd(-1000000000, 1000000000), rnd(-1000000000, 1000000000), rnd(-1000000000, 1000000000));

console.log(`1101: generated ${n} test cases`);

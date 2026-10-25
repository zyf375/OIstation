// 1103 各位数字之和
const { wc, rnd } = require('./_common');

const ID = 1103;
let n = 0;
function t(x) {
  n++;
  const out = String(x).split('').reduce((s, d) => s + (d.charCodeAt(0) - 48), 0);
  wc(ID, n, String(x), String(out));
}

t(0);
t(9);
t(1000000000000000000);   // 19 位
t(999999999999999999);
t(1010101010101010101);
for (let i = 0; i < 8; i++) t(rnd(0, 1000000000000000000));

console.log(`1103: generated ${n} test cases`);

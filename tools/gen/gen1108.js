// 1108 十进制转二进制
const { wc, rnd } = require('./_common');

const ID = 1108;
let n = 0;
function t(x) {
  n++;
  wc(ID, n, String(x), x.toString(2));
}

t(0); t(1); t(2); t(255); t(1024); t(1000000000); t(2147483647);
for (let i = 0; i < 6; i++) t(rnd(0, 1000000000));

console.log(`1108: generated ${n} test cases`);

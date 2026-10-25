// 1106 最大公约数
const { wc, rnd } = require('./_common');

const ID = 1106;
let n = 0;
function gcd(a, b) {
  a = Math.abs(a); b = Math.abs(b);
  while (b) { [a, b] = [b, a % b]; }
  return a;
}
function t(a, b) {
  n++;
  wc(ID, n, `${a} ${b}`, String(gcd(a, b)));
}

t(0, 0); t(0, 5); t(12, 18); t(1, 1000000000); t(1000000000, 1000000000);
t(-6, 9); t(7, 13); t(123456789, 987654321);
for (let i = 0; i < 5; i++) t(rnd(0, 1000000000), rnd(0, 1000000000));

console.log(`1106: generated ${n} test cases`);

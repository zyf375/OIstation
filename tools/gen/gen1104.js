// 1104 判断素数
const { wc, rnd } = require('./_common');

const ID = 1104;
let n = 0;
function isPrime(x) {
  if (x < 2) return false;
  for (let i = 2; i * i <= x; i++) if (x % i === 0) return false;
  return true;
}
function t(x) {
  n++;
  wc(ID, n, String(x), isPrime(x) ? 'YES' : 'NO');
}

t(2); t(3); t(4); t(1); t(97); t(1000000000); // 边界
t(999999937); t(1000000007); t(1000000009);    // 大素数/接近素数
for (let i = 0; i < 6; i++) t(rnd(2, 1000000000));

console.log(`1104: generated ${n} test cases`);

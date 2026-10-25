// 1105 斐波那契第 N 项（F1=1, F2=1）
const { wc, rnd } = require('./_common');

const ID = 1105;
let n = 0;
const memo = {};
function fib(x) {
  if (x <= 2) return 1n;
  if (memo[x]) return memo[x];
  memo[x] = fib(x - 1) + fib(x - 2);
  return memo[x];
}
function t(x) {
  n++;
  wc(ID, n, String(x), fib(x).toString());
}

t(1); t(2); t(3); t(90); t(50); // 边界
for (let i = 0; i < 6; i++) t(rnd(1, 90));

console.log(`1105: generated ${n} test cases`);

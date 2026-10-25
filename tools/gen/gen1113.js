// 1113 反转相加（Reverse-and-Add 回文）
// 给定 N，反复执行 N = N + 反转(N)，直到 N 成为回文数。
// 输出最终的回文数和变换步数（空格分隔）。
// 196 等 Lychrel 数在限定步数内不收敛，生成时跳过。
const { wc, rnd } = require('./_common');

const ID = 1113;
let n = 0;
const CAP = 1000;
function rev(x) { return Number(String(x).split('').reverse().join('')); }
function isPal(x) { const s = String(x); return s === s.split('').reverse().join(''); }
function solve(x) {
  let steps = 0;
  while (!isPal(x)) {
    if (steps > CAP) return null;
    x = x + rev(x);
    steps++;
  }
  return x + ' ' + steps;
}
function t(x) {
  const r = solve(x);
  if (r === null) return; // 跳过不收敛的数
  n++;
  wc(ID, n, String(x), r);
}

t(19); t(78); t(89); t(10); t(123); t(121); t(56); t(10911);
// 随机生成，只保留能收敛的
let guard = 0;
while (n < 14 && guard++ < 200000) t(rnd(1, 50000));

console.log(`1113: generated ${n} test cases`);

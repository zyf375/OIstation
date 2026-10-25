// 1201 最大子段和 (CSP-S 经典 DP)
const { wc, rnd } = require('./_common');

const ID = 1201;
let n = 0;
function t(arr) {
  n++;
  let best = arr[0], cur = arr[0];
  for (let i = 1; i < arr.length; i++) { cur = Math.max(arr[i], cur + arr[i]); best = Math.max(best, cur); }
  wc(ID, n, `${arr.length}\n${arr.join(' ')}`, String(best));
}

t([-1]);
t([5]);
t([-3, -1, -2]);                 // 全负
t([1, 2, 3, 4, 5]);              // 全正
t([-2, 1, -3, 4, -1, 2, 1, -5, 4]);
t([1000000, -1, 1000000]);
t([-1000000, -1, -1000000]);
for (let sz of [100, 1000, 10000, 100000]) t(Array.from({ length: sz }, () => rnd(-10000, 10000)));

console.log(`1201: generated ${n} test cases`);

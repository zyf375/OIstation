// 1202 最长上升子序列 (CSP-S, O(n log n))
const { wc, rnd } = require('./_common');

const ID = 1202;
let n = 0;
function lis(arr) {
  const d = [];
  for (const x of arr) {
    let lo = 0, hi = d.length;
    while (lo < hi) { const mid = (lo + hi) >> 1; if (d[mid] < x) lo = mid + 1; else hi = mid; }
    d[lo] = x;
  }
  return d.length;
}
function t(arr) {
  n++;
  wc(ID, n, `${arr.length}\n${arr.join(' ')}`, String(lis(arr)));
}

t([1]);
t([3, 2, 1]);                 // 严格下降
t([1, 2, 3, 4, 5]);           // 全上升
t([5, 4, 3, 2, 1]);
t([2, 2, 2, 2, 2]);           // 相等(严格上升取1)
t([10, 9, 2, 5, 3, 7, 101, 18]);
for (let sz of [100, 1000, 10000, 100000]) t(Array.from({ length: sz }, () => rnd(-1e9, 1e9)));

console.log(`1202: generated ${n} test cases`);

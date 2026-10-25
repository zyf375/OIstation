// 1206 0/1 背包 (CSP-S, DP)
const { wc, rnd } = require('./_common');

const ID = 1206;
let n = 0;
function solve(N, C, items) {
  const dp = new Array(C + 1).fill(0);
  for (const [w, v] of items) for (let c = C; c >= w; c--) dp[c] = Math.max(dp[c], dp[c - w] + v);
  return String(dp[C]);
}
function t(N, C, items) {
  n++;
  wc(ID, n, `${N} ${C}\n` + items.map(x => x.join(' ')).join('\n'), solve(N, C, items));
}

t(1, 10, [[5, 3]]);
t(3, 5, [[2, 3], [1, 2], [3, 4]]);
t(4, 8, [[3, 4], [4, 5], [5, 6], [1, 1]]);
t(2, 1, [[2, 100], [3, 100]]); // 都放不下
for (let N of [50, 200, 500, 1000]) { const C = 10000; const it = Array.from({ length: N }, () => [rnd(1, C), rnd(1, 1e6)]); t(N, C, it); }

console.log(`1206: generated ${n} test cases`);

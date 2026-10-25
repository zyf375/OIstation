// 1205 活动安排 (CSP-S, 区间贪心)
const { wc, rnd } = require('./_common');

const ID = 1205;
let n = 0;
function solve(inter) {
  inter.sort((a, b) => a[1] - b[1]); // 按结束时间
  let cnt = 0, last = -Infinity;
  for (const [l, r] of inter) if (l >= last) { cnt++; last = r; }
  return String(cnt);
}
function t(inter) {
  n++;
  wc(ID, n, `${inter.length}\n` + inter.map(x => x.join(' ')).join('\n'), solve(inter));
}

t([[1, 2]]);
t([[1, 5], [2, 3], [3, 4]]);
t([[1, 3], [2, 4], [3, 5]]);   // 相接不算重叠
t([[1, 2], [2, 3], [3, 4]]);   // 首尾相接可选3个
t([[5, 6], [1, 10], [2, 3]]);
for (let N of [20, 100, 1000, 100000]) {
  const inter = Array.from({ length: N }, () => { const l = rnd(1, 1e6), r = l + rnd(1, 1e6); return [l, r]; });
  t(inter);
}

console.log(`1205: generated ${n} test cases`);

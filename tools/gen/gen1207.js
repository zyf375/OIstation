// 1207 连通块数量 (CSP-S, 并查集)
const { wc, rnd } = require('./_common');

const ID = 1207;
let n = 0;
function solve(N, edges) {
  const p = Array.from({ length: N + 1 }, (_, i) => i);
  function find(x) { while (p[x] !== x) { p[x] = p[p[x]]; x = p[x]; } return x; }
  let comps = N;
  for (const [u, v] of edges) { const a = find(u), b = find(v); if (a !== b) { p[a] = b; comps--; } }
  return String(comps);
}
function t(N, edges) {
  n++;
  wc(ID, n, `${N} ${edges.length}\n` + edges.map(e => e.join(' ')).join('\n'), solve(N, edges));
}

t(3, []);
t(1, []);
t(4, [[1, 2], [2, 3], [3, 1]]);
t(5, [[1, 2], [3, 4]]);
t(4, [[1, 2], [2, 3], [3, 4], [1, 4]]);
for (let N of [50, 1000, 10000, 100000]) {
  const edges = [];
  for (let k = 0; k < N; k++) edges.push([rnd(1, N), rnd(1, N)]);
  t(N, edges);
}
console.log(`1207: generated ${n} test cases`);

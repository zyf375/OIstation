// 1209 最小生成树权值和 (CSP-S, Kruskal)
const { wc, rnd } = require('./_common');

const ID = 1209;
let n = 0;
function kruskal(N, edges) {
  edges = edges.map(e => [...e]).sort((a, b) => a[2] - b[2]);
  const p = Array.from({ length: N + 1 }, (_, i) => i);
  function find(x) { while (p[x] !== x) { p[x] = p[p[x]]; x = p[x]; } return x; }
  let total = 0, cnt = 0;
  for (const [u, v, w] of edges) { const a = find(u), b = find(v); if (a !== b) { p[a] = b; total += w; cnt++; if (cnt === N - 1) break; } }
  return cnt === N - 1 ? String(total) : 'IMPOSSIBLE';
}
function t(N, edges) {
  n++;
  wc(ID, n, `${N} ${edges.length}\n` + edges.map(e => e.join(' ')).join('\n'), kruskal(N, edges));
}
// 生成连通图：先生成树保证连通，再加边
function connected(N, extraM, wmax) {
  const edges = [];
  for (let i = 2; i <= N; i++) edges.push([i, rnd(1, i - 1), rnd(1, wmax)]);
  for (let k = 0; k < extraM; k++) edges.push([rnd(1, N), rnd(1, N), rnd(1, wmax)]);
  return edges;
}

t(1, []);
t(2, [[1, 2, 5]]);
t(3, [[1, 2, 1], [2, 3, 2], [1, 3, 3]]);
t(4, connected(4, 2, 20));
t(4, [[1, 2, 5], [3, 4, 6]]); // 不连通 -> IMPOSSIBLE
for (let N of [50, 1000, 5000, 100000]) { const e = connected(N, N + rnd(0, N), 1e6); t(N, e); }

console.log(`1209: generated ${n} test cases`);

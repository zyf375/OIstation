// 1204 拓扑排序 (CSP-S, Kahn, 字典序最小拓扑序)
const { wc, rnd } = require('./_common');

const ID = 1204;
let n = 0;
function HeapMin() { this.a = []; }
HeapMin.prototype = {
  push(x) { this.a.push(x); let i = this.a.length - 1; const a = this.a; while (i > 0) { const p = (i - 1) >> 1; if (a[p] <= a[i]) break; [a[p], a[i]] = [a[i], a[p]]; i = p; } },
  pop() { const a = this.a; const top = a[0]; const last = a.pop(); if (a.length) { a[0] = last; let i = 0, L = a.length; for (;;) { let s = i, l = i * 2 + 1, r = i * 2 + 2; if (l < L && a[l] < a[s]) s = l; if (r < L && a[r] < a[s]) s = r; if (s === i) break; [a[s], a[i]] = [a[i], a[s]]; i = s; } } return top; },
  empty() { return this.a.length === 0; }
};
function solve(N, edges) {
  const adj = Array.from({ length: N + 1 }, () => []);
  const indeg = new Array(N + 1).fill(0);
  for (const [u, v] of edges) { adj[u].push(v); indeg[v]++; }
  const pq = new HeapMin();
  for (let i = 1; i <= N; i++) if (indeg[i] === 0) pq.push(i);
  const order = [];
  while (!pq.empty()) { const u = pq.pop(); order.push(u); for (const v of adj[u]) if (--indeg[v] === 0) pq.push(v); }
  if (order.length !== N) return 'CYCLE';
  return order.join(' ');
}
function t(N, edges) {
  n++;
  wc(ID, n, `${N} ${edges.length}\n` + edges.map(e => e.join(' ')).join('\n'), solve(N, edges));
}
function dag(N, m) {
  const set = new Set(), edges = [];
  while (edges.length < m) { const u = rnd(1, N - 1), v = rnd(u + 1, N), k = u + '-' + v; if (!set.has(k)) { set.add(k); edges.push([u, v]); } }
  return edges;
}
function cyclic(N, m) {
  const edges = dag(N, Math.max(1, m - 1));
  edges.push([rnd(1, N), rnd(1, N - 1)]);
  return edges;
}

t(2, [[1, 2]]);
t(3, [[1, 2], [2, 3], [1, 3]]);
t(3, [[1, 2], [2, 1]]);
t(4, [[1, 2], [2, 3], [3, 4], [1, 4]]);
t(5, [[1, 2], [2, 1], [3, 4]]);
for (let N of [20, 100, 1000]) t(N, dag(N, rnd(N, N * 2)));
t(100000, dag(100000, 100000));
t(6, cyclic(6, 8));

console.log(`1204: generated ${n} test cases`);

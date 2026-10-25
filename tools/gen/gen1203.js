// 1203 单源最短路径 (CSP-S, Dijkstra + 二叉堆)
const { wc, rnd } = require('./_common');

const ID = 1203;
let n = 0;
// 最小二叉堆
function Heap() { this.a = []; }
Heap.prototype = {
  push(x) { this.a.push(x); this.up(this.a.length - 1); },
  up(i) { const a = this.a; while (i > 0) { const p = (i - 1) >> 1; if (a[p][0] <= a[i][0]) break; [a[p], a[i]] = [a[i], a[p]]; i = p; } },
  pop() { const a = this.a; const top = a[0]; const last = a.pop(); if (a.length) { a[0] = last; this.down(0); } return top; },
  down(i) { const a = this.a; const L = a.length; for (;;) { let s = i, l = i * 2 + 1, r = i * 2 + 2; if (l < L && a[l][0] < a[s][0]) s = l; if (r < L && a[r][0] < a[s][0]) s = r; if (s === i) break; [a[s], a[i]] = [a[i], a[s]]; i = s; } },
  empty() { return this.a.length === 0; }
};
const INF = '9007199254740991'; // Number.MAX_SAFE_INTEGER 近似无穷
function solve(N, M, S, edges) {
  const adj = Array.from({ length: N + 1 }, () => []);
  for (const [u, v, w] of edges) { adj[u].push([v, w]); adj[v].push([u, w]); }
  const dist = new Array(N + 1).fill(INF); dist[S] = 0;
  const pq = new Heap(); pq.push([0, S]);
  while (!pq.empty()) {
    const [d, u] = pq.pop();
    if (d > dist[u]) continue;
    for (const [v, w] of adj[u]) { const nd = dist[u] + w; if (nd < dist[v]) { dist[v] = nd; pq.push([nd, v]); } }
  }
  return dist.slice(1).map(x => x === INF ? -1 : x).join(' ');
}
function t(N, M, S, edges) {
  n++;
  const input = `${N} ${M} ${S}\n` + edges.map(e => e.join(' ')).join('\n');
  wc(ID, n, input, solve(N, M, S, edges));
}
function connectedGraph(N, extraM, wmax) {
  const edges = [];
  for (let i = 2; i <= N; i++) edges.push([i, rnd(1, i - 1), rnd(1, wmax)]);
  for (let k = 0; k < extraM; k++) edges.push([rnd(1, N), rnd(1, N), rnd(1, wmax)]);
  return edges;
}

t(2, 1, 1, [[1, 2, 5]]);
t(1, 0, 1, []);
t(3, 3, 1, [[1, 2, 1], [2, 3, 1], [1, 3, 3]]);
t(4, 4, 2, [[1, 2, 2], [2, 3, 1], [3, 4, 3], [1, 4, 10]]);
t(5, 5, 1, connectedGraph(5, 3, 100));
for (let N of [50, 500, 5000, 100000]) { const e = connectedGraph(N, N + rnd(0, N), 1e6); t(N, e.length, rnd(1, N), e); }

console.log(`1203: generated ${n} test cases`);

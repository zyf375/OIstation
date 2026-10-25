// 1208 树的直径 (CSP-S, 两次 BFS)
const { wc, rnd } = require('./_common');

const ID = 1208;
let n = 0;
function diameter(N, edges) {
  const adj = Array.from({ length: N + 1 }, () => []);
  for (const [u, v] of edges) { adj[u].push(v); adj[v].push(u); }
  function bfs(s) {
    const dist = new Array(N + 1).fill(-1); dist[s] = 0;
    const q = [s]; let far = s;
    for (let i = 0; i < q.length; i++) { const u = q[i]; if (dist[u] > dist[far]) far = u; for (const v of adj[u]) if (dist[v] === -1) { dist[v] = dist[u] + 1; q.push(v); } }
    return [far, dist[far]];
  }
  const [a] = bfs(1);
  const [, d] = bfs(a);
  return String(d);
}
function tree(N) {
  const edges = [];
  for (let i = 2; i <= N; i++) edges.push([i, rnd(1, i - 1)]);
  return edges;
}
function t(N, edges) {
  n++;
  wc(ID, n, `${N}\n` + edges.map(e => e.join(' ')).join('\n'), diameter(N, edges));
}

t(1, []);
t(2, [[1, 2]]);
t(3, [[1, 2], [1, 3]]);       // 直径1
t(5, [[1, 2], [2, 3], [3, 4], [4, 5]]); // 链 直径4
t(6, tree(6));
for (let N of [50, 1000, 50000, 200000]) t(N, tree(N));

console.log(`1208: generated ${n} test cases`);

from collections import deque

INF = float('inf')

def spfa(n, adj, s):
    dist = [INF] * (n + 1)
    cnt = [0] * (n + 1)
    inq = [False] * (n + 1)
    q = deque([s])
    inq[s] = True
    dist[s] = 0
    neg = False
    while q:
        u = q.popleft()
        inq[u] = False
        for v, w in adj[u]:
            if dist[v] > dist[u] + w:
                dist[v] = dist[u] + w
                if not inq[v]:
                    q.append(v)
                    inq[v] = True
                    cnt[v] += 1
                    if cnt[v] >= n:
                        return dist, True
    return dist, neg

def build(n, es):
    adj = [[] for _ in range(n + 1)]
    for u, v, w in es:
        adj[u].append((v, w))
    return adj

cases = {
    "正权图": (5, [(1,2,3),(1,3,2),(2,3,2),(2,4,5),(3,4,4),(4,5,3)], 1),
    "含负权无负环": (4, [(1,2,4),(1,3,5),(3,2,-2),(2,4,3),(1,4,10)], 1),
    "含负环": (2, [(1,2,1),(2,1,-3)], 1),
    "不可达": (3, [(1,2,1)], 1),
}
for name, (n, es, s) in cases.items():
    adj = build(n, es)
    d, neg = spfa(n, adj, s)
    print(f"{name}: negCycle={neg} dist={d[1:n+1]}")

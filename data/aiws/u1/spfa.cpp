#include <bits/stdc++.h>
using namespace std;

const int INF = 0x3f3f3f3f;

// 邻接表：head[u] 指向 u 的第一条边，edges 链式存储
struct Edge { int to, w, next; };

int spfa(int n, int src, int dst,
         const vector<int>& head, const vector<Edge>& edges,
         bool& hasNegativeCycle, vector<int>& dist) {
    dist.assign(n + 1, INF);
    vector<int> cnt(n + 1, 0);   // cnt[v]：v 被松弛（入队）次数
    vector<bool> inq(n + 1, false);
    queue<int> q;

    dist[src] = 0;
    q.push(src);
    inq[src] = true;

    hasNegativeCycle = false;
    int relaxCount = 0;          // 总松弛次数，用于判定负环

    while (!q.empty()) {
        int u = q.front(); q.pop();
        inq[u] = false;

        for (int i = head[u]; i != -1; i = edges[i].next) {
            int v = edges[i].to;
            if (dist[v] > dist[u] + edges[i].w) {
                dist[v] = dist[u] + edges[i].w;
                if (!inq[v]) {
                    q.push(v);
                    inq[v] = true;
                    relaxCount++;
                    // 路径长度类判负环：v 在路径上被松弛超过 n 次
                    if (++cnt[v] >= n) { hasNegativeCycle = true; return dist; }
                }
            }
        }
    }
    return dist;
}

int main() {
    int n, m, s, t;
    // 测试 1：正权图（类 Dijkstra 场景）
    {
        cin.clear();
    }
    // 手工构造几个用例
    struct Case { int n; int m; vector<tuple<int,int,int>> es; int s, t; };
    vector<Case> cases = {
        {5, 6, {{1,2,3},{1,3,2},{2,3,2},{2,4,5},{3,4,4},{4,5,3}}, 1, 5},
        // 含负权但无负环
        {4, 5, {{1,2,4},{1,3,5},{3,2,-2},{2,4,3},{1,4,10}}, 1, 4},
        // 含负环：1->2(1), 2->1(-3)
        {2, 2, {{1,2,1},{2,1,-3}}, 1, 2},
    };

    for (auto& c : cases) {
        vector<int> head(c.n + 1, -1);
        vector<Edge> edges;
        for (auto& [u, v, w] : c.es) {
            edges.push_back({v, w, head[u]});
            head[u] = (int)edges.size() - 1;
        }
        bool neg; vector<int> dist;
        spfa(c.n, c.s, c.t, head, edges, neg, dist);
        cout << "n=" << c.n << " negCycle=" << neg << " dist:";
        for (int i = 1; i <= c.n; i++) cout << " " << i << "->" << (dist[i] >= INF ? string("INF") : to_string(dist[i]));
        cout << "\n";
    }
    return 0;
}

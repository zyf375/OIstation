// test_go.cpp —— GoRules / GoGame 单元测试（用 g++ 编译运行）
#include "go_game.h"
#include <iostream>
#include <string>
#include <vector>
#include <map>

// 链接桩：本测试不涉及大逃杀/斗地主模块（game_framework.h 的 createGame 会引用它们）
std::unique_ptr<IGame> createBattleRoyaleGame(const std::string&) {
    return std::unique_ptr<IGame>();
}
std::unique_ptr<IGame> createDdzGame(const std::string&) {
    return std::unique_ptr<IGame>();
}

static int passN = 0, failN = 0;
static void check(const std::string& name, bool cond, const std::string& extra = "") {
    if (cond) { passN++; std::cout << "OK  " << name << std::endl; }
    else      { failN++; std::cout << "FAIL " << name << (extra.empty() ? "" : ("  " + extra)) << std::endl; }
}

// 统一坐标语义：set(r, x, y, c) -> board[y][x] = c；place(r, x, y, c) -> r.place(x,y,c)
static void set(GoRules& r, int x, int y, int c) { r.board[y][x] = c; }

// ---------- GoRules 规则测试 ----------
static void testRules() {
    GoRules r(9);

    // R1 自杀
    set(r, 0, 1, GoRules::WHITE); set(r, 1, 0, GoRules::WHITE);
    std::string reason;
    int rc = r.place(0, 0, GoRules::BLACK, reason);
    check("R1 角落自杀被拒绝", rc == -1 && r.board[0][0] == GoRules::EMPTY && !reason.empty());

    // R2 提三子：白 (0,0)(1,0)(1,1)，黑 (2,0)(2,1)(0,2)(1,2)，黑下 (0,1)
    r.reset(9);
    set(r, 0, 0, GoRules::WHITE); set(r, 1, 0, GoRules::WHITE); set(r, 1, 1, GoRules::WHITE);
    set(r, 2, 0, GoRules::BLACK); set(r, 2, 1, GoRules::BLACK);
    set(r, 0, 2, GoRules::BLACK); set(r, 1, 2, GoRules::BLACK);
    r.history.clear();                                   // 摆子后重建历史基线
    rc = r.place(0, 1, GoRules::BLACK, reason);
    check("R2 提三子", rc == 3 && r.board[0][0] == GoRules::EMPTY &&
          r.board[0][1] == GoRules::EMPTY && r.board[1][1] == GoRules::EMPTY && r.board[1][0] == GoRules::BLACK,
          "rc=" + std::to_string(rc));
    check("R2b captured[BLACK]==3", r.captured[GoRules::BLACK] == 3);

    // R3 标准单点劫（19 路）
    GoRules k(19);
    set(k, 10, 9,  GoRules::BLACK);
    set(k, 9, 10,  GoRules::BLACK);
    set(k, 11, 10, GoRules::BLACK);
    set(k, 10, 10, GoRules::WHITE);
    set(k, 9, 11,  GoRules::WHITE);
    set(k, 11, 11, GoRules::WHITE);
    set(k, 10, 12, GoRules::WHITE);
    k.history.clear();
    int k1 = k.place(10, 11, GoRules::BLACK, reason);
    check("R3a 黑提白一子", k1 == 1 && k.board[10][10] == GoRules::EMPTY, "k1=" + std::to_string(k1));
    int k2 = k.place(10, 10, GoRules::WHITE, reason);
    check("R3b 白立即提回被拒(劫)", k2 == -1 && k.board[10][10] == GoRules::EMPTY && !reason.empty(),
          "k2=" + std::to_string(k2));
    k.place(5, 5, GoRules::WHITE, reason);   // 白应一手
    k.place(6, 6, GoRules::BLACK, reason);   // 黑应一手
    int k3 = k.place(10, 10, GoRules::WHITE, reason);
    check("R3c 隔一手后可提回", k3 == 1 && k.board[10][10] == GoRules::WHITE && k.board[11][10] == GoRules::EMPTY,
          "k3=" + std::to_string(k3));

    // R4 悔棋
    GoRules u(9);
    set(u, 0, 0, GoRules::WHITE); set(u, 1, 0, GoRules::WHITE); set(u, 1, 1, GoRules::WHITE);
    set(u, 2, 0, GoRules::BLACK); set(u, 2, 1, GoRules::BLACK);
    set(u, 0, 2, GoRules::BLACK); set(u, 1, 2, GoRules::BLACK);
    u.history.clear();
    u.place(0, 1, GoRules::BLACK, reason);   // 提3子
    bool undone = u.undo();
    check("R4a 悔棋恢复局面", undone && u.board[1][0] == GoRules::EMPTY &&
          u.board[0][0] == GoRules::WHITE && u.board[1][1] == GoRules::WHITE &&
          u.captured[GoRules::BLACK] == 0 && u.turn == GoRules::BLACK);

    // R5 数子
    GoRules s(9);
    for (int y = 0; y < 9; y++) for (int x = 0; x < 9; x++) {
        if (x <= 3) s.board[y][x] = GoRules::BLACK;
        else if (x >= 5) s.board[y][x] = GoRules::WHITE;
    }
    int bs, ws, bt, wt;
    s.countScore(bs, ws, bt, wt);
    check("R5a 数子 黑36 白36", bs == 36 && ws == 36 && bt == 0 && wt == 0,
          "bs=" + std::to_string(bs) + " ws=" + std::to_string(ws));
    int w = s.finishByCount("测试");
    check("R5b 白贴7.5目获胜", w == GoRules::WHITE && s.over);

    // R6 占点 / R7 越界
    GoRules p(9);
    p.board[5][5] = GoRules::BLACK;
    check("R6 占点被拒绝", p.place(5, 5, GoRules::WHITE, reason) == -1);
    check("R7 越界安全", p.place(99, 99, GoRules::WHITE, reason) == -1);

    // R8 连续停手结束
    GoRules q(9);
    bool e1 = q.pass();
    bool e2 = q.pass();
    check("R8 连续两停手自动结束", !e1 && e2 && q.over && q.winner != 0);
}

// ---------- GoGame 网络流程测试 ----------
static void testGame() {
    std::vector<std::string> bcasts;
    std::map<std::string, std::vector<std::string> > sends;
    GoGame g("room-go");
    g.setBroadcastFn([&](const std::string&, const std::string& json, const std::string&) { bcasts.push_back(json); });
    g.setSendFn([&](const std::string& cid, const std::string& json) { sends[cid].push_back(json); });

    // 加入
    g.onPlayerJoin("A", "黑方", "room-go");
    g.onPlayerJoin("B", "白方", "room-go");
    g.onPlayerJoin("C", "观战", "room-go");   // 第三人观战
    check("G1 两人对战第三人观战", bcasts.size() == 3);
    check("G1b 状态快照包含 players 数组", bcasts.back().find("\"players\":[") != std::string::npos);

    // join 消息路由：未入局者的 join 必须返回 false，交回服务器通用流程
    GameMsg jm;
    jm["action"] = "join"; jm["roomId"] = "room-go";
    check("G1c join 消息不被吞", g.onMessage("NEWBIE", jm) == false);
    GameMsg lm;
    lm["action"] = "leave";
    check("G1d leave 消息不被吞", g.onMessage("NEWBIE", lm) == false);

    // config：A 指定 9 路
    GameMsg cfg;
    cfg["action"] = "config"; cfg["size"] = "9";
    g.onMessage("A", cfg);
    bcasts.clear();

    // 观战者落子被忽略
    GameMsg m;
    m["action"] = "place"; m["x"] = "3"; m["y"] = "3";
    bool spec = g.onMessage("C", m);
    check("G2 观战者消息被吞", spec == true && bcasts.empty());

    // 只有黑方能先落子
    g.onMessage("B", m);                     // 白先行 -> error
    check("G3 白方先行被拒", !sends["B"].empty() && sends["B"].back().find("还没轮到你") != std::string::npos);
    sends["B"].clear();
    g.onMessage("A", m);                     // 黑落子
    check("G4 黑落子广播状态", bcasts.size() == 1 && bcasts[0].find("\"turn\":\"2\"") != std::string::npos,
          "bcasts=" + std::to_string(bcasts.size()));
    bcasts.clear();

    // 停一手（白）改为：白落子 (4,4)
    GameMsg pm;
    pm["action"] = "place"; pm["x"] = "4"; pm["y"] = "4";
    g.onMessage("B", pm);
    check("G5 白落子广播", bcasts.size() == 1 && bcasts[0].find("\"turn\":\"1\"") != std::string::npos);
    bcasts.clear();

    // 悔棋（撤销白 (4,4)，轮到白方，手数回到 1）
    GameMsg um;
    um["action"] = "undo";
    g.onMessage("A", um);
    check("G6 悔棋撤销最后一手", bcasts.size() == 1 &&
          bcasts[0].find("\"move\":\"1\"") != std::string::npos &&
          bcasts[0].find("\"turn\":\"2\"") != std::string::npos &&
          bcasts[0].find("\"last\":\"3,3\"") != std::string::npos);
    bcasts.clear();

    // 认输
    GameMsg rm;
    rm["action"] = "resign";
    g.onMessage("B", rm);
    check("G7 白认输黑胜", bcasts.size() == 1 &&
          bcasts[0].find("\"over\":\"1\"") != std::string::npos &&
          bcasts[0].find("\"winner\":\"1\"") != std::string::npos &&
          bcasts[0].find("认输") != std::string::npos);
    bcasts.clear();

    // 结束后落子被拒
    g.onMessage("A", m);
    check("G8 结束后落子被拒", !sends["A"].empty() && sends["A"].back().find("棋局已结束") != std::string::npos);

    // 重开
    GameMsg rr;
    rr["action"] = "restart";
    g.onMessage("A", rr);
    check("G9 重开后继续对局", bcasts.size() == 1 && bcasts[0].find("\"over\":\"0\"") != std::string::npos);

    // 玩家离开判负
    g.onMessage("A", m); // 黑下一手
    bcasts.clear();
    g.onPlayerLeave("B");
    check("G10 B离开判A胜", bcasts.size() >= 1 && bcasts.back().find("\"winner\":\"1\"") != std::string::npos);
}

int main() {
    std::cout << "==== GoRules ====" << std::endl;
    testRules();
    std::cout << "==== GoGame ====" << std::endl;
    testGame();
    std::cout << "\n结果: " << passN << " 通过, " << failN << " 失败" << std::endl;
    return failN == 0 ? 0 : 1;
}

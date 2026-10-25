#ifndef GO_GAME_H
#define GO_GAME_H

// ============================================================
//  go_game.h —— 双人围棋（服务端权威逻辑）
// ------------------------------------------------------------
//  玩法：
//    - 同一房间最多两名对局者：先加入执黑，后加入执白，其余观战
//    - 轮流落子：提子、禁入点（自杀禁止）、简单打劫、停一手
//    - 悔棋 / 认输 / 数子结束 / 重开
//    - 棋盘大小 9 / 13 / 19（由先加入者 config 指定，之后锁定）
//
//  协议（type=="game", gameType=="go"）：
//    客户端 -> 服务器：
//      {action:"join",  roomId, playerName}   // 走服务器通用 join 流程
//      {action:"config", size:"9"|"13"|"19"}  // 加入后指定棋盘大小
//      {action:"place",  x, y}                // 落子（0 基坐标）
//      {action:"pass"}                        // 停一手
//      {action:"undo"}                        // 悔棋（撤销最后一手）
//      {action:"resign"}                      // 认输
//      {action:"score"}                       // 数子结束
//      {action:"restart"}                     // 重开一局
//    服务器 -> 客户端：
//      {action:"state",  ...完整局面快照, 见 broadcastState()}
//      {action:"error",  msg}                 // 非法操作提示
//      {action:"notice", msg}                 // 玩家离开等提示
//
//  GoRules 为纯规则引擎（不依赖网络），便于单元测试。
// ============================================================

#include <string>
#include <map>
#include <vector>
#include <utility>
#include <cstdlib>
#include <algorithm>
#include "game_framework.h"

// ------------------------------------------------------------------
//  纯规则引擎
// ------------------------------------------------------------------
class GoRules {
public:
    static const int EMPTY = 0;
    static const int BLACK = 1;
    static const int WHITE = 2;
    static const double KOMI;

    int N;
    int board[19][19];
    int turn;                 // 当前行棋方（1黑 2白）
    int captured[3];          // captured[c] = c 色棋子被对方提掉的数目
    int moveCount;
    int lastX, lastY;         // 最后一手坐标，-1 表示无/停手
    int passStreak;           // 连续停手次数
    bool over;
    int winner;               // 0 未定
    std::string result;
    std::vector<std::string> moves;   // "x,y"（只记落子，不记停手）

    struct MoveRec {
        std::string prevBoard;        // 该手之前局面序列化
        int cap[3];                   // 该手之前提子数
        int player;
        int x, y;                     // -1 表示停手
        bool pass;
    };
    std::vector<MoveRec> history;

    explicit GoRules(int size = 19) { reset(size); }

    // 重开（size<=0 时保持当前 N）
    void reset(int size = 0) {
        if (size == 9 || size == 13 || size == 19) N = size;
        for (int y = 0; y < 19; y++) for (int x = 0; x < 19; x++) board[y][x] = EMPTY;
        turn = BLACK;
        captured[BLACK] = captured[WHITE] = 0;
        moveCount = 0;
        lastX = lastY = -1;
        passStreak = 0;
        over = false;
        winner = 0;
        result.clear();
        moves.clear();
        history.clear();
    }

    std::string ser() const {
        std::string s;
        s.reserve((size_t)N * N);
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                s += char('0' + board[y][x]);
        return s;
    }

    // 落子：成功返回提子数（>=0）；非法返回 -1 并写 reason
    int place(int x, int y, int color, std::string& reason) {
        if (x < 0 || y < 0 || x >= N || y >= N) { reason = "坐标越界"; return -1; }
        if (board[y][x] != EMPTY) { reason = "此处已有棋子"; return -1; }
        std::string prev = ser();
        int capSnap[3] = {captured[0], captured[1], captured[2]};
        board[y][x] = color;
        int opp = 3 - color;
        int removed = 0;
        std::vector<std::pair<int,int> > stones;
        int libs = 0;
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRS[d][0], ny = y + DIRS[d][1];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
            if (board[ny][nx] != opp) continue;
            getGroup(nx, ny, stones, libs);
            if (libs == 0) {
                removed += (int)stones.size();
                for (size_t i = 0; i < stones.size(); i++)
                    board[stones[i].second][stones[i].first] = EMPTY;
            }
        }
        // 自杀禁止
        getGroup(x, y, stones, libs);
        if (libs == 0 && removed == 0) {
            deser(prev);
            reason = "禁入点（自杀），不能落子";
            return -1;
        }
        // 简单打劫：落子后局面不得与对手上一手之前相同
        if (!history.empty() && ser() == history.back().prevBoard) {
            deser(prev);
            reason = "违反打劫规则，不能立即提回";
            return -1;
        }
        captured[color] += removed;
        MoveRec rec;
        rec.prevBoard = prev;
        rec.cap[0] = capSnap[0]; rec.cap[1] = capSnap[1]; rec.cap[2] = capSnap[2];
        rec.player = color; rec.x = x; rec.y = y; rec.pass = false;
        history.push_back(rec);
        return removed;
    }

    // 停一手。返回 true 表示连续两停手导致对局结束
    bool pass() {
        MoveRec rec;
        rec.prevBoard = ser();
        rec.cap[0] = captured[0]; rec.cap[1] = captured[1]; rec.cap[2] = captured[2];
        rec.player = turn; rec.x = -1; rec.y = -1; rec.pass = true;
        history.push_back(rec);
        passStreak++;
        turn = 3 - turn;
        if (passStreak >= 2) { finishByCount("双方连续停一手，棋局结束"); return true; }
        return false;
    }

    // 悔棋：撤销最后一手（落子或停手），返回是否成功
    bool undo() {
        if (over || history.empty()) return false;
        MoveRec rec = history.back();
        history.pop_back();
        deser(rec.prevBoard);
        captured[0] = rec.cap[0]; captured[1] = rec.cap[1]; captured[2] = rec.cap[2];
        turn = rec.player;
        if (!rec.pass) {
            moveCount = std::max(0, moveCount - 1);
            if (!moves.empty()) moves.pop_back();
        }
        lastX = lastY = -1;
        for (int i = (int)history.size() - 1; i >= 0; i--) {
            if (!history[i].pass) { lastX = history[i].x; lastY = history[i].y; break; }
        }
        passStreak = 0;
        for (int i = (int)history.size() - 1; i >= 0 && history[i].pass; i--) passStreak++;
        return true;
    }

    void resign(int color) {
        winner = 3 - color;
        over = true;
        result = std::string(color == BLACK ? "黑方" : "白方") + "认输";
    }

    // 中国规则数子：子 + 空（KOMI 贴给白方）。返回胜方。
    int finishByCount(const std::string& reason) {
        int bs = 0, ws = 0, bt = 0, wt = 0;
        countScore(bs, ws, bt, wt);
        double blackTotal = (double)(bs + bt);
        double whiteTotal = (double)(ws + wt) + KOMI;
        winner = (blackTotal >= whiteTotal) ? BLACK : WHITE;
        over = true;
        result = reason;
        return winner;
    }

    void countScore(int& bs, int& ws, int& bt, int& wt) const {
        bs = ws = bt = wt = 0;
        bool visited[19][19] = {{false}};
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            if (board[y][x] == BLACK) bs++;
            else if (board[y][x] == WHITE) ws++;
        }
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            if (board[y][x] != EMPTY || visited[y][x]) continue;
            std::vector<std::pair<int,int> > queue;
            queue.push_back(std::make_pair(x, y));
            visited[y][x] = true;
            int size = 0;
            int border = 0;                       // bit0=黑, bit1=白
            size_t head = 0;
            while (head < queue.size()) {
                int cx = queue[head].first, cy = queue[head].second; head++;
                size++;
                for (int d = 0; d < 4; d++) {
                    int nx = cx + DIRS[d][0], ny = cy + DIRS[d][1];
                    if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                    int c = board[ny][nx];
                    if (c == EMPTY) {
                        if (!visited[ny][nx]) { visited[ny][nx] = true; queue.push_back(std::make_pair(nx, ny)); }
                    } else {
                        border |= (c == BLACK ? 1 : 2);
                    }
                }
            }
            if (border == 1) bt += size;
            else if (border == 2) wt += size;
        }
    }

    // 求 (x,y) 所在同色棋串的气数与棋子（BFS）
    void getGroup(int x, int y, std::vector<std::pair<int,int> >& stones, int& libs) const {
        stones.clear(); libs = 0;
        int color = board[y][x];
        if (color == EMPTY) return;
        bool seen[19][19] = {{false}};
        bool lib[19][19] = {{false}};
        std::vector<std::pair<int,int> > queue;
        queue.push_back(std::make_pair(x, y));
        seen[y][x] = true;
        size_t head = 0;
        while (head < queue.size()) {
            int cx = queue[head].first, cy = queue[head].second; head++;
            stones.push_back(std::make_pair(cx, cy));
            for (int d = 0; d < 4; d++) {
                int nx = cx + DIRS[d][0], ny = cy + DIRS[d][1];
                if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                int c = board[ny][nx];
                if (c == color) {
                    if (!seen[ny][nx]) { seen[ny][nx] = true; queue.push_back(std::make_pair(nx, ny)); }
                } else if (c == EMPTY && !lib[ny][nx]) {
                    lib[ny][nx] = true; libs++;
                }
            }
        }
    }

private:
    static const int DIRS[4][2];

    void deser(const std::string& s) {
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                board[y][x] = (int)(s[(size_t)y * N + x] - '0');
    }
};

const double GoRules::KOMI = 7.5;
const int GoRules::DIRS[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};

// ------------------------------------------------------------------
//  网络游戏本体（实现 IGame 接口）
// ------------------------------------------------------------------
class GoGame : public IGame {
public:
    explicit GoGame(const std::string& roomId) : myRoom(roomId), rules() {}
    virtual ~GoGame() {}

    const std::string& type() const override {
        static const std::string t = "go";
        return t;
    }

    void onPlayerJoin(const std::string& clientId, const std::string& name,
                      const std::string& /*roomId*/) override {
        if (colors.count(clientId)) return;          // 重复加入
        int color = 0;
        if (!hasColor(GoRules::BLACK))      color = GoRules::BLACK;
        else if (!hasColor(GoRules::WHITE)) color = GoRules::WHITE;
        if (color == 0) { broadcastState(); return; }  // 第三人观战
        players.push_back(clientId);
        colors[clientId] = color;
        playerNames[clientId] = name.empty() ? ("P" + clientId.substr(0, 4)) : name;
        if (rules.over) rules.reset();               // 上局结束：新玩家加入即开新局
        broadcastState();
    }

    void onPlayerLeave(const std::string& clientId) override {
        if (!colors.count(clientId)) return;         // 观战者离开，忽略
        if (!rules.over) {                           // 对局中离开 -> 判对方胜
            rules.over = true;
            rules.winner = (colors[clientId] == GoRules::BLACK) ? GoRules::WHITE : GoRules::BLACK;
            rules.result = "对方离开，棋局结束";
        }
        std::string name = playerNames[clientId];
        playerNames.erase(clientId);
        colors.erase(clientId);
        players.erase(std::remove(players.begin(), players.end(), clientId), players.end());
        GameMsg m;
        m["type"] = "game"; m["gameType"] = "go"; m["action"] = "notice";
        m["msg"] = name + " 离开了房间";
        broadcastAll(myRoom, makeJson(m));
        broadcastState();
    }

    bool onMessage(const std::string& clientId, const GameMsg& msg) override {
        std::string action = msg.count("action") ? msg.at("action") : "";
        if (!colors.count(clientId)) {
            // 未入局者：join/leave 交回服务器通用流程（否则会吞掉第二个玩家的加入）
            if (action == "join" || action == "leave") return false;
            return true;                            // 观战者消息吞掉，不转发
        }
        if (action == "config")  return handleConfig(clientId, msg);
        if (action == "place")   return handlePlace(clientId, msg);
        if (action == "pass")    return handlePass(clientId);
        if (action == "undo")    return handleUndo(clientId);
        if (action == "resign")  return handleResign(clientId);
        if (action == "score")   { if (!rules.over) rules.finishByCount("主动数子结束"); broadcastState(); return true; }
        if (action == "restart") { rules.reset(); broadcastState(); return true; }
        return false;                                // 未识别 -> 交回服务器通用转发
    }

    void onTick(float /*dt*/) override {}
    bool hasPlayers() const override { return !players.empty(); }

private:
    std::string myRoom;
    std::vector<std::string> players;                // 对局者（最多2人）
    std::map<std::string, std::string> playerNames;
    std::map<std::string, int> colors;
    bool sizeLocked = false;                         // 棋盘大小锁定（首个 config 生效）
    GoRules rules;

    bool hasColor(int c) const {
        for (size_t i = 0; i < players.size(); i++)
            if (colors.find(players[i]) != colors.end() && colors.at(players[i]) == c) return true;
        return false;
    }

    void sendError(const std::string& clientId, const std::string& text) {
        GameMsg m;
        m["type"] = "game"; m["gameType"] = "go"; m["action"] = "error";
        m["msg"] = text;
        sendTo(clientId, makeJson(m));
    }

    bool handleConfig(const std::string& /*clientId*/, const GameMsg& msg) {
        if (sizeLocked || !rules.moves.empty()) return true;
        int s = msg.count("size") ? std::atoi(msg.at("size").c_str()) : 0;
        if (s == 9 || s == 13 || s == 19) {
            rules.reset(s);
            sizeLocked = true;
            broadcastState();
        }
        return true;
    }

    bool handlePlace(const std::string& clientId, const GameMsg& msg) {
        int color = colors[clientId];
        if (rules.over)                 { sendError(clientId, "棋局已结束"); return true; }
        if ((int)players.size() < 2)    { sendError(clientId, "等待对手加入"); return true; }
        if (color != rules.turn)        { sendError(clientId, "还没轮到你"); return true; }
        if (!msg.count("x") || !msg.count("y")) return true;
        int x = std::atoi(msg.at("x").c_str());
        int y = std::atoi(msg.at("y").c_str());
        if (x < 0 || y < 0 || x >= rules.N || y >= rules.N) return true;
        std::string reason;
        int removed = rules.place(x, y, color, reason);
        if (removed < 0) { sendError(clientId, reason); return true; }
        rules.moveCount++;
        rules.lastX = x; rules.lastY = y;
        rules.moves.push_back(std::to_string(x) + "," + std::to_string(y));
        rules.passStreak = 0;
        rules.turn = 3 - rules.turn;
        broadcastState();
        return true;
    }

    bool handlePass(const std::string& clientId) {
        int color = colors[clientId];
        if (rules.over)              { sendError(clientId, "棋局已结束"); return true; }
        if ((int)players.size() < 2) { sendError(clientId, "等待对手加入"); return true; }
        if (color != rules.turn)     { sendError(clientId, "还没轮到你"); return true; }
        bool ended = rules.pass();
        if (!ended) broadcastState();
        return true;
    }

    bool handleUndo(const std::string& clientId) {
        if (rules.over)                 { sendError(clientId, "棋局已结束，不能悔棋"); return true; }
        if (!rules.undo())              { sendError(clientId, "没有可悔的棋"); return true; }
        broadcastState();
        return true;
    }

    bool handleResign(const std::string& clientId) {
        if (rules.over) { sendError(clientId, "棋局已结束"); return true; }
        rules.resign(colors[clientId]);
        broadcastState();
        return true;
    }

    void broadcastState() {
        GameMsg m;
        m["type"] = "game"; m["gameType"] = "go"; m["action"] = "state";
        m["size"] = std::to_string(rules.N);
        m["board"] = rules.ser();
        m["turn"] = std::to_string(rules.turn);
        m["captured"] = std::to_string(rules.captured[GoRules::BLACK]) + "|" +
                        std::to_string(rules.captured[GoRules::WHITE]);
        m["move"] = std::to_string(rules.moveCount);
        m["last"] = (rules.lastX >= 0) ? (std::to_string(rules.lastX) + "," + std::to_string(rules.lastY)) : "-1";
        m["over"] = rules.over ? "1" : "0";
        m["winner"] = std::to_string(rules.winner);
        m["result"] = rules.result;
        std::string mv;
        for (size_t i = 0; i < rules.moves.size(); i++) { if (i) mv += ";"; mv += rules.moves[i]; }
        m["moves"] = mv;
        std::string pl = "[";
        for (size_t i = 0; i < players.size(); i++) {
            if (i) pl += ",";
            pl += "{\"id\":\"" + players[i] + "\",\"name\":\"" + esc(playerNames[players[i]]) +
                  "\",\"color\":" + std::to_string(colors[players[i]]) + "}";
        }
        pl += "]";
        m["players"] = pl;
        m["status"] = rules.over ? "over" : (players.size() >= 2 ? "playing" : "waiting");
        broadcastAll(myRoom, makeJson(m));
    }

    static std::string esc(const std::string& s) {
        std::string r = s;
        size_t pos = 0;
        while ((pos = r.find('"', pos)) != std::string::npos) { r.replace(pos, 1, "\\\""); pos += 2; }
        pos = 0;
        while ((pos = r.find('\\', pos)) != std::string::npos) { r.replace(pos, 1, "\\\\"); pos += 2; }
        return r;
    }

    std::string makeJson(const GameMsg& m) {
        std::string r = "{";
        bool first = true;
        for (GameMsg::const_iterator it = m.begin(); it != m.end(); ++it) {
            if (!first) r += ",";
            first = false;
            r += "\"" + it->first + "\":";
            const std::string& v = it->second;
            if (!v.empty() && (v[0] == '[' || v[0] == '{')) r += v;
            else if (v == "true" || v == "false" || v == "null") r += v;
            else {
                std::string s = esc(v);
                r += "\"" + s + "\"";
            }
        }
        r += "}";
        return r;
    }
};

// 工厂函数：在 game_framework.h 的 createGame 里注册一行 "go"
std::unique_ptr<IGame> createGoGame(const std::string& roomId) {
    return std::unique_ptr<IGame>(new GoGame(roomId));
}

#endif // GO_GAME_H

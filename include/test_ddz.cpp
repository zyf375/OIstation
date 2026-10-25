// test_ddz.cpp —— 斗地主牌型引擎 + 对局流程单元测试（g++ 编译运行）
#include "ddz_game.h"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>

// 链接桩：本测试不使用其他游戏模块
std::unique_ptr<IGame> createBattleRoyaleGame(const std::string&) { return std::unique_ptr<IGame>(); }
std::unique_ptr<IGame> createGoGame(const std::string&)            { return std::unique_ptr<IGame>(); }

static int passN = 0, failN = 0;
static void check(const std::string& name, bool cond, const std::string& extra = "") {
    if (cond) { passN++; std::cout << "OK  " << name << std::endl; }
    else      { failN++; std::cout << "FAIL " << name << (extra.empty() ? "" : ("  " + extra)) << std::endl; }
}

static std::vector<int> C(const char* s) { return ddz::parseCards(s); }

// ---------- 牌型识别 ----------
static void testCombos() {
    using namespace ddz;
    Combo c;
    c = parseCombo(C("5"));                    check("单张5", c.type == SINGLE && c.key == 5);
    c = parseCombo(C("5,5"));                  check("对5", c.type == PAIR && c.key == 5);
    c = parseCombo(C("5,5,5"));                check("三5", c.type == TRIPLE && c.key == 5);
    c = parseCombo(C("5,5,5,7"));              check("三带一", c.type == TRIPLE_SINGLE && c.key == 5);
    c = parseCombo(C("5,5,5,7,7"));            check("三带二", c.type == TRIPLE_PAIR && c.key == 5);
    c = parseCombo(C("3,4,5,6,7"));            check("顺子34567", c.type == STRAIGHT && c.key == 7 && c.len == 5);
    c = parseCombo(C("10,J,Q,K,A"));           check("顺子10JQKA", c.type == STRAIGHT && c.key == 14);
    c = parseCombo(C("J,Q,K,A,2"));            check("含2的顺子无效", c.type == NONE);
    c = parseCombo(C("3,4,5"));                check("3张顺子无效", c.type == NONE);
    c = parseCombo(C("3,3,4,4,5,5"));          check("连对334455", c.type == PAIR_STRAIGHT && c.key == 5);
    c = parseCombo(C("3,3,4,4,5"));            check("5张连对无效", c.type == NONE);
    c = parseCombo(C("3,3,3,4,4,4"));          check("飞机333444", c.type == AIRPLANE && c.key == 4);
    c = parseCombo(C("3,3,3,4,4,4,5,6"));      check("飞机带单33344456", c.type == AIRPLANE_SINGLE && c.key == 4);
    c = parseCombo(C("3,3,3,4,4,4,5,5,6,6"));  check("飞机带对3334445566", c.type == AIRPLANE_PAIR && c.key == 4);
    c = parseCombo(C("3,3,3,4,4,4,5,5,6"));    check("飞机翅膀不匹配无效", c.type == NONE);
    c = parseCombo(C("3,3,3,4,4,4,6,6,6"));    check("飞机333444666不连续无效", c.type == NONE);
    c = parseCombo(C("3,3,3,5,5,5,6,6"));      check("三张不连续+对无效", c.type == NONE);
    c = parseCombo(C("4,4,4,4,5,6"));          check("四带二单", c.type == FOUR_TWO && c.key == 4);
    c = parseCombo(C("4,4,4,4,5,5"));          check("四带二(可带对)", c.type == FOUR_TWO && c.key == 4);
    c = parseCombo(C("4,4,4,4,5,5,6,6"));      check("四带二对", c.type == FOUR_TWO_PAIRS && c.key == 4);
    c = parseCombo(C("4,4,4,4,5,6,7,8"));      check("四带四张无效", c.type == NONE);
    c = parseCombo(C("9,9,9,9"));              check("炸弹9999", c.type == BOMB && c.key == 9);
    c = parseCombo(C("S,B"));                  check("王炸", c.type == ROCKET);
    c = parseCombo(C("3,3,3,4"));              check("三带一(单为4)", c.type == TRIPLE_SINGLE && c.key == 3);
    c = parseCombo(C("A,A,A,2"));              check("三A带2", c.type == TRIPLE_SINGLE && c.key == 14);
    c = parseCombo(C("3,3,4,4"));              check("两对无效", c.type == NONE);

    // ---------- 管牌比较 ----------
    check("单3管不住单5",  !canBeat(parseCombo(C("3")),   parseCombo(C("5"))));
    check("单5管得住单3",   canBeat(parseCombo(C("5")),   parseCombo(C("3"))));
    check("对5管不住对7",  !canBeat(parseCombo(C("5,5")), parseCombo(C("7,7"))));
    check("顺子45678管34567", canBeat(parseCombo(C("4,5,6,7,8")), parseCombo(C("3,4,5,6,7"))));
    check("顺子长度不同管不住", !canBeat(parseCombo(C("3,4,5,6,7")), parseCombo(C("4,5,6,7,8,9"))));
    check("对子管不住单张", !canBeat(parseCombo(C("5,5")), parseCombo(C("3"))));
    check("单张管不住对子", !canBeat(parseCombo(C("3")),   parseCombo(C("5,5"))));
    check("炸弹管顺子",      canBeat(parseCombo(C("9,9,9,9")), parseCombo(C("3,4,5,6,7"))));
    check("小炸管不住大炸", !canBeat(parseCombo(C("9,9,9,9")), parseCombo(C("10,10,10,10"))));
    check("王炸管炸弹",      canBeat(parseCombo(C("S,B")),     parseCombo(C("9,9,9,9"))));
    check("王炸管不住王炸", !canBeat(parseCombo(C("S,B")),     parseCombo(C("S,B"))));
    check("飞机带单比三张",  canBeat(parseCombo(C("4,4,4,5,5,5,7,8")), parseCombo(C("3,3,3,4,4,4,5,6"))));
    check("三带一比三带一",  canBeat(parseCombo(C("7,7,7,9")), parseCombo(C("5,5,5,8"))));
}

// ---------- 对局流程 ----------
// 固定牌堆：0号位 17 张可拆成单张一路出完，另两家不出，0号位当地主并胜出。
static std::vector<int> makeFixedDeck() {
    std::vector<int> seat0;
    seat0.push_back(3); seat0.push_back(3); seat0.push_back(4); seat0.push_back(4);
    seat0.push_back(5); seat0.push_back(6); seat0.push_back(7); seat0.push_back(8);
    seat0.push_back(9); seat0.push_back(10); seat0.push_back(11); seat0.push_back(12);
    seat0.push_back(13); seat0.push_back(14); seat0.push_back(15); seat0.push_back(16); seat0.push_back(17);
    std::vector<int> rest;
    for (int v = 3; v <= 15; v++) {
        int n = (v == 3 || v == 4) ? 2 : 3;
        for (int k = 0; k < n; k++) rest.push_back(v);
    }
    std::vector<int> deck = seat0;
    deck.insert(deck.end(), rest.begin(), rest.end());
    return deck;   // 54 张
}

// 从广播 JSON 里取顶层字符串字段 "key":"value"
static std::string field(const std::string& st, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    size_t p = st.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    size_t e = st.find('"', p);
    return (e == std::string::npos) ? "" : st.substr(p, e - p);
}

// 从 players 数组里找某玩家的座位："id":"A" 之后的 "seat":N（makeJson 对数组值原样透传，无转义）
static int seatOfPlayer(const std::string& st, const std::string& id) {
    std::string patId = "\"id\":\"" + id + "\"";
    size_t p = st.find(patId);
    if (p == std::string::npos) return -1;
    size_t sp = st.find("\"seat\":", p);
    if (sp == std::string::npos) return -1;
    return std::atoi(st.c_str() + sp + 7);
}

static void testGame() {
    std::vector<std::string> bcasts;
    std::map<std::string, std::vector<std::string> > sends;
    DdzGame g("room-ddz");
    g.debugSetDeck(makeFixedDeck());
    g.setBroadcastFn([&](const std::string&, const std::string& json, const std::string&) { bcasts.push_back(json); });
    g.setSendFn([&](const std::string& cid, const std::string& json) { sends[cid].push_back(json); });

    auto lastState = [&]() -> std::string { return bcasts.empty() ? "" : bcasts.back(); };
    auto handOf = [&](const std::string& cid) -> std::vector<int> {
        for (int i = (int)sends[cid].size() - 1; i >= 0; i--)
            if (sends[cid][i].find("\"action\":\"hand\"") != std::string::npos) {
                std::string cards = field(sends[cid][i], "cards");
                return ddz::parseCards(cards);
            }
        return std::vector<int>();
    };

    // 加入
    g.onPlayerJoin("A", "zhangsan", "room-ddz");
    g.onPlayerJoin("B", "lisi", "room-ddz");
    bcasts.clear();
    g.onPlayerJoin("C", "wangwu", "room-ddz");   // 满3人 -> 自动发牌
    check("D1 满3人自动发牌", !bcasts.empty() && field(bcasts.back(), "phase") == "bidding");
    check("D1b 各人收到17张手牌", handOf("A").size() == 17 && handOf("B").size() == 17 && handOf("C").size() == 17);
    check("D1c 手牌张数公开为17,17,17", lastState().find("\"handCounts\":\"17,17,17\"") != std::string::npos);
    // 观战者（第4人）无手牌
    bcasts.clear();
    g.onPlayerJoin("D", "spectator", "room-ddz");
    check("D1d 第4人观战无手牌", handOf("D").empty() && field(bcasts.back(), "spectators") == "1");
    // 观战者出牌被吞
    GameMsg dm;
    dm["action"] = "play"; dm["cards"] = "3,4,5";
    bool specConsumed = g.onMessage("D", dm);
    check("D1e 观战者消息被吞", specConsumed == true);

    // join 消息路由（未入局者）必须返回 false，交给服务器通用 join 流程
    GameMsg jm;
    jm["action"] = "join"; jm["roomId"] = "room-ddz";
    check("D1f join 消息不被吞", g.onMessage("NEWBIE", jm) == false);

    // 座位分配
    int seatA = seatOfPlayer(lastState(), "A");
    int seatB = seatOfPlayer(lastState(), "B");
    int seatC = seatOfPlayer(lastState(), "C");
    check("D2 三人分坐0/1/2", seatA >= 0 && seatB >= 0 && seatC >= 0 &&
          seatA != seatB && seatB != seatC && seatA != seatC,
          "A=" + std::to_string(seatA) + " B=" + std::to_string(seatB) + " C=" + std::to_string(seatC));
    std::map<int, std::string> seatId;
    seatId[seatA] = "A"; seatId[seatB] = "B"; seatId[seatC] = "C";

    // 叫分：首个叫1分，其余不叫，直到进入出牌阶段
    int bidRound = 0;
    bool bidOk = true;
    while (true) {
        std::string bs = field(lastState(), "bidSeat");
        if (bs.empty()) { bidOk = false; break; }
        int bSeat = std::atoi(bs.c_str());
        std::string who = seatId.count(bSeat) ? seatId[bSeat] : "";
        if (who.empty()) { bidOk = false; break; }
        GameMsg bm;
        bm["action"] = "bid";
        bm["score"] = (bidRound == 0) ? "1" : "0";
        bcasts.clear();
        g.onMessage(who, bm);
        if (bcasts.empty()) { bidOk = false; break; }
        bidRound++;
        if (field(lastState(), "phase") == "playing") break;
        if (bidRound > 6) { bidOk = false; break; }
    }
    check("D3 叫分后进入出牌阶段", bidOk && field(lastState(), "phase") == "playing");
    check("D3b 底分=1", field(lastState(), "bid") == "1");
    int landlordSeat = std::atoi(field(lastState(), "landlordSeat").c_str());
    check("D3c 地主手牌=20(17+3底牌)", handOf(seatId[landlordSeat]).size() == 20);

    // 出牌循环：当前手出最小单张（领出），其余 pass，直到有人出完
    int guard = 0;
    bool playOk = true;
    while (field(lastState(), "over") != "1" && guard < 300) {
        guard++;
        std::string ts = field(lastState(), "turnSeat");
        if (ts.empty()) { playOk = false; break; }
        int tSeat = std::atoi(ts.c_str());
        std::string who = seatId.count(tSeat) ? seatId[tSeat] : "";
        if (who.empty()) { playOk = false; break; }
        GameMsg pm;
        if (field(lastState(), "needBeat") == "0") {
            std::vector<int> hand = handOf(who);
            if (hand.empty()) { playOk = false; break; }
            pm["action"] = "play";
            pm["cards"] = ddz::cardName(hand[0]);
        } else {
            pm["action"] = "pass";
        }
        bcasts.clear();
        g.onMessage(who, pm);
        if (bcasts.empty()) { playOk = false; break; }
    }
    check("D4 正常出完一局", playOk && guard < 300);
    check("D4b 地主获胜", field(lastState(), "winner") == "0" &&
          field(lastState(), "winnerSeat") == field(lastState(), "landlordSeat"));
    check("D4c 结算：地主+2 农民各-1",
          lastState().find("\"roundScore\":\"2,-1,-1\"") != std::string::npos ||
          lastState().find("\"roundScore\":\"-1,2,-1\"") != std::string::npos ||
          lastState().find("\"roundScore\":\"-1,-1,2\"") != std::string::npos,
          "roundScore=" + field(lastState(), "roundScore"));

    // 结束后禁止出牌
    GameMsg pm;
    pm["action"] = "play"; pm["cards"] = "3";
    bcasts.clear();
    bool consumed = g.onMessage("A", pm);
    check("D5 结束后出牌被拒(有error)", consumed == true && !sends["A"].empty() &&
          sends["A"].back().find("不是出牌阶段") != std::string::npos);

    // 重开
    GameMsg rr;
    rr["action"] = "restart";
    bcasts.clear();
    g.onMessage("A", rr);
    check("D6 重开重新发牌", !bcasts.empty() && field(bcasts.back(), "phase") == "bidding" &&
          field(bcasts.back(), "over") == "0");

    // 中途离开
    bcasts.clear();
    g.onPlayerLeave("B");
    check("D7 离开后等待/结束", !bcasts.empty() &&
          (field(bcasts.back(), "phase") == "waiting" || field(bcasts.back(), "phase") == "over"));
}

int main() {
    std::cout << "==== 牌型 ====" << std::endl;
    testCombos();
    std::cout << "==== 对局流程 ====" << std::endl;
    testGame();
    std::cout << "\n结果: " << passN << " 通过, " << failN << " 失败" << std::endl;
    return failN == 0 ? 0 : 1;
}

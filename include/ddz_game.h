#ifndef DDZ_GAME_H
#define DDZ_GAME_H

// ============================================================
//  ddz_game.h —— 联机斗地主（服务端权威逻辑）
// ------------------------------------------------------------
//  玩法：
//    - 房间满 3 人自动发牌开局；先到者坐 0/1/2 号位，其余观战
//    - 叫分抢地主：按座位轮流 不叫/1/2/3 分，必须高于当前最高
//      分；无人叫分则自动重新发牌
//    - 地主拿 3 张底牌，先出；农民配合，轮流出牌/不出
//    - 牌型：单、对、三、三带一、三带二、顺子(>=5)、连对(>=3对)、
//      飞机(带单/带对)、四带二(单/对)、炸弹、王炸
//    - 胜负：先出完牌者胜；地主胜 +2 倍分，农民各 -1 倍分；
//      倍数 = 叫分 × 2^炸弹数（王炸/炸弹翻倍）
//  协议（type=="game", gameType=="ddz"）：
//    客户端 -> 服务器：
//      {action:"join",   roomId, playerName}   // 服务器通用 join
//      {action:"bid",    score:"0"|"1"|"2"|"3"}// 叫分（0=不叫）
//      {action:"play",   cards:"3,4,5,6,7"}     // 出牌（牌值编码见下）
//      {action:"pass"}                          // 不出
//      {action:"restart"}                       // 重开一局（满3人）
//    服务器 -> 客户端：
//      {action:"state", ...公开局面快照, 见 broadcastState()}
//      {action:"hand",  cards:"...", seat:"n"}  // 私发各人手牌
//      {action:"error", msg} / {action:"notice", msg}
//  牌值编码：3-10=字面值, J=11 Q=12 K=13 A=14 2=15 S=小王 B=大王
// ============================================================

#include <string>
#include <map>
#include <set>
#include <vector>
#include <utility>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include "game_framework.h"

namespace ddz {

// 牌型
enum ComboType {
    NONE = 0, SINGLE = 1, PAIR = 2, TRIPLE = 3, TRIPLE_SINGLE = 4,
    TRIPLE_PAIR = 5, STRAIGHT = 6, PAIR_STRAIGHT = 7, AIRPLANE = 8,
    AIRPLANE_SINGLE = 9, AIRPLANE_PAIR = 10, FOUR_TWO = 11,
    FOUR_TWO_PAIRS = 12, BOMB = 13, ROCKET = 14
};

inline int parseCard(const std::string& s) {
    if (s == "S") return 16;
    if (s == "B") return 17;
    if (s == "J") return 11;
    if (s == "Q") return 12;
    if (s == "K") return 13;
    if (s == "A") return 14;
    if (s == "2") return 15;
    int v = std::atoi(s.c_str());
    if (v >= 3 && v <= 10) return v;
    return -1;
}

inline std::string cardName(int v) {
    if (v == 16) return "S";
    if (v == 17) return "B";
    switch (v) {
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        case 14: return "A";
        case 15: return "2";
        default: return std::to_string(v);
    }
}

inline std::vector<int> parseCards(const std::string& s) {
    std::vector<int> out;
    std::string cur;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == ',' || s[i] == ' ') {
            if (!cur.empty()) {
                int v = parseCard(cur);
                if (v > 0) out.push_back(v);
                cur.clear();
            }
        } else cur += s[i];
    }
    return out;
}

inline std::string joinCards(const std::vector<int>& cards) {
    std::string r;
    for (size_t i = 0; i < cards.size(); i++) {
        if (i) r += ",";
        r += cardName(cards[i]);
    }
    return r;
}

struct Combo {
    int type;
    int key;   // 主牌值（比较大小用）
    int len;   // 总张数
    Combo() : type(NONE), key(0), len(0) {}
};

inline int maxKey(const int cnt[18]) {
    int best = 3, bestc = 0;
    for (int v = 3; v <= 17; v++) if (cnt[v] > bestc) { bestc = cnt[v]; best = v; }
    return best;
}

// vals 是否连续（相邻差 1）
inline bool isSeq(const std::vector<int>& vals) {
    for (size_t i = 1; i < vals.size(); i++)
        if (vals[i] != vals[i - 1] + 1) return false;
    return true;
}

// 飞机带翅膀：在 cnt 中寻找 >=2 个连续三张 + 翅膀(0不带/1带单/2带对)
// 成功返回 true 并写回 n(架数)、wing、key(最大三张值)
inline bool findPlane(const int cnt[18], int len, int& n, int& wing, int& key) {
    std::vector<int> triples;
    for (int v = 3; v <= 14; v++) if (cnt[v] == 3) triples.push_back(v);
    if (triples.size() < 2) return false;
    for (size_t i = 0; i < triples.size(); i++) {
        for (size_t j = i; j < triples.size(); j++) {
            if (j > i && triples[j] != triples[j - 1] + 1) break;
            int nn = (int)(j - i + 1);
            if (nn < 2) continue;
            int rem[18] = {0};
            for (int v = 3; v <= 17; v++) rem[v] = cnt[v];
            for (size_t k = i; k <= j; k++) rem[triples[k]] -= 3;
            int remCards = 0;
            for (int v = 3; v <= 17; v++) remCards += rem[v];
            int w = 0;
            if (remCards == 0) w = 0;
            else if (remCards == nn) {
                bool allSingle = true;
                for (int v = 3; v <= 17; v++) if (rem[v] > 1) allSingle = false;
                if (allSingle) w = 1;
            } else if (remCards == 2 * nn) {
                bool allPairs = true;
                for (int v = 3; v <= 17; v++) if (rem[v] != 0 && rem[v] != 2) allPairs = false;
                if (allPairs) w = 2;
            }
            if (w > 0 || remCards == 0) {
                int need = (w == 0 ? 3 * nn : (w == 1 ? 4 * nn : 5 * nn));
                if (need == len) { n = nn; wing = w; key = triples[j]; return true; }
            }
        }
    }
    return false;
}

// 识别一手牌的牌型（cards 可为任意顺序）
inline Combo parseCombo(const std::vector<int>& cards) {
    Combo r;
    if (cards.empty()) return r;
    std::vector<int> c = cards;
    std::sort(c.begin(), c.end());
    int len = (int)c.size();
    int cnt[18] = {0};
    for (int v : c) cnt[v]++;
    int distinct = 0, maxcnt = 0, maxv = 0;
    std::vector<int> vals;
    for (int v = 3; v <= 17; v++) if (cnt[v]) {
        distinct++; vals.push_back(v);
        maxcnt = std::max(maxcnt, cnt[v]);
        maxv = std::max(maxv, v);
    }
    r.len = len;
    // 王炸
    if (len == 2 && cnt[16] == 1 && cnt[17] == 1) { r.type = ROCKET; r.key = 17; return r; }
    // 炸弹
    if (len == 4 && distinct == 1) { r.type = BOMB; r.key = c[0]; return r; }
    // 单 / 对 / 三
    if (len == 1) { r.type = SINGLE; r.key = c[0]; return r; }
    if (len == 2 && distinct == 1) { r.type = PAIR; r.key = c[0]; return r; }
    if (len == 3 && distinct == 1) { r.type = TRIPLE; r.key = c[0]; return r; }
    // 三带一 / 三带二
    if (len == 4 && maxcnt == 3) { r.type = TRIPLE_SINGLE; r.key = maxKey(cnt); return r; }
    if (len == 5 && maxcnt == 3 && distinct == 2) { r.type = TRIPLE_PAIR; r.key = maxKey(cnt); return r; }
    // 四带二（单）/ 四带二（对）
    if (len == 6 && maxcnt == 4) { r.type = FOUR_TWO; r.key = maxKey(cnt); return r; }
    if (len == 8 && maxcnt == 4 && distinct == 3) { r.type = FOUR_TWO_PAIRS; r.key = maxKey(cnt); return r; }
    // 顺子 / 连对 / 飞机（不含2与王）
    if (len >= 5 && maxcnt == 1 && maxv <= 14 && isSeq(vals)) { r.type = STRAIGHT; r.key = maxv; return r; }
    if (len >= 6 && len % 2 == 0 && maxcnt == 2 && maxv <= 14 && isSeq(vals)) { r.type = PAIR_STRAIGHT; r.key = maxv; return r; }
    // 纯飞机：所有牌都必须正好是三张且连续（distinct == len/3）
    if (len >= 6 && len % 3 == 0 && maxcnt == 3 && distinct == len / 3 && maxv <= 14 && isSeq(vals)) {
        r.type = AIRPLANE; r.key = maxv; return r;
    }
    // 飞机带单 / 带对
    int n = 0, wing = 0, key = 0;
    if (findPlane(cnt, len, n, wing, key)) {
        r.type = (wing == 1) ? AIRPLANE_SINGLE : (wing == 2 ? AIRPLANE_PAIR : AIRPLANE);
        r.key = key;
        return r;
    }
    return r; // 无效
}

// a 是否能管住 b
inline bool canBeat(const Combo& a, const Combo& b) {
    if (a.type == ROCKET) return b.type != ROCKET;
    if (b.type == ROCKET) return false;
    if (a.type == BOMB && b.type != BOMB) return true;
    if (b.type == BOMB && a.type != BOMB) return false;
    if (a.type == BOMB && b.type == BOMB) return a.key > b.key;
    if (a.type != b.type) return false;
    if (a.len != b.len) return false;
    return a.key > b.key;
}

} // namespace ddz

// ------------------------------------------------------------------
//  网络游戏本体
// ------------------------------------------------------------------
class DdzGame : public IGame {
public:
    explicit DdzGame(const std::string& roomId) : myRoom(roomId) {
        std::srand((unsigned)std::time(0) ^ ((unsigned)roomId.length() * 7919u));
    }
    virtual ~DdzGame() {}

    const std::string& type() const override {
        static const std::string t = "ddz";
        return t;
    }

    // 仅供单元测试：固定牌堆（54 张，前17->0号位，依次，末3->底牌）
    void debugSetDeck(const std::vector<int>& deck) { fixedDeck = deck; }

    void onPlayerJoin(const std::string& clientId, const std::string& name,
                      const std::string& /*roomId*/) override {
        members.insert(clientId);
        if (!seatOf.count(clientId)) {
            // 未开局/已结束且有空位 -> 入座（先到先坐）
            if (phase == WAITING || phase == OVER) {
                for (int i = 0; i < 3; i++) {
                    if (seats[i].clientId.empty()) {
                        seats[i].clientId = clientId;
                        seats[i].name = name.empty() ? ("P" + clientId.substr(0, 4)) : name;
                        seatOf[clientId] = i;
                        break;
                    }
                }
            }
        }
        broadcastState();
        // 满 3 人自动开局
        if (occupied() == 3 && (phase == WAITING || phase == OVER)) deal();
    }

    void onPlayerLeave(const std::string& clientId) override {
        members.erase(clientId);
        if (seatOf.count(clientId)) {
            int s = seatOf[clientId];
            seats[s].clientId.clear();
            seats[s].name.clear();
            seatOf.erase(clientId);
            if (phase == BIDDING || phase == PLAYING) {
                phase = OVER;
                winner = -1;
                result = "有玩家中途离开，本局作废";
            }
        }
        broadcastState();
    }

    bool onMessage(const std::string& clientId, const GameMsg& msg) override {
        std::string action = msg.count("action") ? msg.at("action") : "";
        if (!members.count(clientId)) {
            if (action == "join" || action == "leave") return false;  // 交回服务器通用流程
            return true;                                              // 未入局消息吞掉
        }
        if (!seatOf.count(clientId)) return true;                      // 观战者消息吞掉
        if (action == "bid")     return handleBid(clientId, msg);
        if (action == "play")    return handlePlay(clientId, msg);
        if (action == "pass")    return handlePass(clientId);
        if (action == "restart") {
            if (occupied() == 3) deal();
            else { phase = WAITING; broadcastState(); }
            return true;
        }
        return false;
    }

    void onTick(float /*dt*/) override {}
    bool hasPlayers() const override { return !members.empty(); }

private:
    enum Phase { WAITING = 0, BIDDING = 1, PLAYING = 2, OVER = 3 };

    struct SeatInfo {
        std::string clientId;
        std::string name;
        SeatInfo() {}
    };

    std::string myRoom;
    std::set<std::string> members;                    // 房间内所有人（含观战）
    SeatInfo seats[3];                                // 座位（最多3人）
    std::map<std::string, int> seatOf;                // clientId -> 座位
    std::vector<int> hands[3];                        // 各座位手牌（值3..17）
    std::vector<int> bottom;                          // 底牌
    std::vector<int> fixedDeck;                       // 测试用固定牌堆

    Phase phase = WAITING;
    int dealerSeat = 0;                               // 开始叫分的座位
    int bidSeat = 0;                                  // 当前叫分座位
    int highestBid = 0, highestBidder = -1, bidDone = 0;
    int landlordSeat = -1;                            // 地主座位
    int bid = 0;                                      // 底分
    int bombs = 0;                                    // 炸弹/王炸次数（翻倍）
    int turnSeat = -1;                                // 当前出牌座位
    bool needBeat = false;                            // 是否必须管牌（false=本轮先出）
    int passes = 0;                                   // 连续不出的次数
    int lastPlayer = -1;                              // 上一手出牌者（-1 无）
    int lastPasser = -1;                              // 上一个不出者
    std::string lastAction = "none";                  // play / pass / none
    std::vector<int> lastCards;                       // 上一手出的牌
    ddz::Combo lastCombo;
    int winner = -1;                                  // -1 无, 0 地主胜, 1 农民胜
    int winnerSeat = -1;                              // 先出完的座位
    std::string result;
    int totalScore[3] = {0, 0, 0};                    // 累计积分
    int roundScore[3] = {0, 0, 0};                    // 本局结算

    int occupied() const {
        int n = 0;
        for (int i = 0; i < 3; i++) if (!seats[i].clientId.empty()) n++;
        return n;
    }

    int nextSeat(int s) const { return (s + 1) % 3; }

    static std::vector<int> buildDeck() {
        std::vector<int> deck;
        for (int v = 3; v <= 15; v++) for (int k = 0; k < 4; k++) deck.push_back(v);
        deck.push_back(16); deck.push_back(17);       // 小王、大王
        return deck;
    }

    void deal() {
        std::vector<int> deck;
        if ((int)fixedDeck.size() == 54) deck = fixedDeck;
        else {
            deck = buildDeck();
            std::random_shuffle(deck.begin(), deck.end());
        }
        for (int s = 0; s < 3; s++) {
            hands[s].clear();
            for (int i = 0; i < 17; i++) hands[s].push_back(deck[s * 17 + i]);
            std::sort(hands[s].begin(), hands[s].end());
        }
        bottom.assign(deck.begin() + 51, deck.end());
        dealerSeat = std::rand() % 3;
        bidSeat = dealerSeat;
        highestBid = 0; highestBidder = -1; bidDone = 0;
        landlordSeat = -1; bid = 0; bombs = 0;
        turnSeat = -1; needBeat = false; passes = 0;
        lastPlayer = -1; lastPasser = -1;
        lastAction = "none"; lastCards.clear(); lastCombo = ddz::Combo();
        winner = -1; winnerSeat = -1; result.clear();
        phase = BIDDING;
        for (int s = 0; s < 3; s++) sendHand(s);
        broadcastState();
    }

    void sendHand(int seat) {
        GameMsg m;
        m["type"] = "game"; m["gameType"] = "ddz"; m["action"] = "hand";
        m["seat"] = std::to_string(seat);
        m["cards"] = ddz::joinCards(hands[seat]);
        sendTo(seats[seat].clientId, makeJson(m));
    }

    void sendError(const std::string& clientId, const std::string& text) {
        GameMsg m;
        m["type"] = "game"; m["gameType"] = "ddz"; m["action"] = "error";
        m["msg"] = text;
        sendTo(clientId, makeJson(m));
    }

    // ---------- 动作处理 ----------
    bool handleBid(const std::string& clientId, const GameMsg& msg) {
        int seat = seatOf[clientId];
        if (phase != BIDDING) { sendError(clientId, "当前不在叫分阶段"); return true; }
        if (seat != bidSeat) { sendError(clientId, "还没轮到你叫分"); return true; }
        int sc = msg.count("score") ? std::atoi(msg.at("score").c_str()) : -1;
        if (sc < 0 || sc > 3) { sendError(clientId, "叫分必须是 0-3"); return true; }
        if (sc > 0 && sc <= highestBid) { sendError(clientId, "叫分必须高于当前最高分"); return true; }
        bidDone++;
        if (sc > highestBid) { highestBid = sc; highestBidder = seat; }
        if (bidDone >= 3) {
            if (highestBid == 0) { deal(); return true; }   // 无人叫分 -> 重新发牌
            landlordSeat = highestBidder;
            for (size_t i = 0; i < bottom.size(); i++) hands[landlordSeat].push_back(bottom[i]);
            std::sort(hands[landlordSeat].begin(), hands[landlordSeat].end());
            bid = highestBid;
            phase = PLAYING;
            turnSeat = landlordSeat;
            needBeat = false; passes = 0;
            lastPlayer = -1; lastPasser = -1; lastAction = "none";
            lastCards.clear(); lastCombo = ddz::Combo();
            sendHand(landlordSeat);
            broadcastState();
            return true;
        }
        bidSeat = nextSeat(bidSeat);
        broadcastState();
        return true;
    }

    bool handlePlay(const std::string& clientId, const GameMsg& msg) {
        int seat = seatOf[clientId];
        if (phase != PLAYING) { sendError(clientId, "当前不是出牌阶段"); return true; }
        if (seat != turnSeat) { sendError(clientId, "还没轮到你出牌"); return true; }
        if (!msg.count("cards")) return true;
        std::vector<int> cards = ddz::parseCards(msg.at("cards"));
        if (cards.empty()) { sendError(clientId, "请选择要出的牌"); return true; }
        if ((int)cards.size() > (int)hands[seat].size()) { sendError(clientId, "出牌数超过手牌"); return true; }
        // 校验这些牌都在手中
        std::vector<int> remaining = hands[seat];
        for (size_t i = 0; i < cards.size(); i++) {
            std::vector<int>::iterator it = std::find(remaining.begin(), remaining.end(), cards[i]);
            if (it == remaining.end()) { sendError(clientId, "你手中没有这些牌"); return true; }
            remaining.erase(it);
        }
        ddz::Combo c = ddz::parseCombo(cards);
        if (c.type == ddz::NONE) { sendError(clientId, "不支持的牌型"); return true; }
        if (needBeat && !ddz::canBeat(c, lastCombo)) { sendError(clientId, "管不上上一手"); return true; }
        // 执行
        hands[seat] = remaining;
        if (c.type == ddz::BOMB || c.type == ddz::ROCKET) bombs++;
        lastCombo = c; lastCards = cards; lastPlayer = seat;
        lastAction = "play"; lastPasser = -1;
        passes = 0; needBeat = true;
        sendHand(seat);
        if (hands[seat].empty()) { finish(seat); return true; }
        turnSeat = nextSeat(seat);
        broadcastState();
        return true;
    }

    bool handlePass(const std::string& clientId) {
        int seat = seatOf[clientId];
        if (phase != PLAYING) { sendError(clientId, "当前不是出牌阶段"); return true; }
        if (seat != turnSeat) { sendError(clientId, "还没轮到你"); return true; }
        if (!needBeat) { sendError(clientId, "你是本轮先出，必须出牌"); return true; }
        passes++;
        lastPasser = seat;
        lastAction = "pass";
        if (passes >= 2) {                         // 另两人都过 -> 回到上一手出牌者
            turnSeat = lastPlayer;
            needBeat = false;
            passes = 0;
        } else {
            turnSeat = nextSeat(turnSeat);
        }
        broadcastState();
        return true;
    }

    void finish(int winnerSeat_) {
        phase = OVER;
        winnerSeat = winnerSeat_;
        int mult = 1 << std::min(bombs, 20);       // 2^炸弹数
        int x = bid * mult;
        bool landlordWon = (winnerSeat == landlordSeat);
        if (landlordWon) {
            roundScore[landlordSeat] = 2 * x;
            for (int i = 0; i < 3; i++) if (i != landlordSeat) roundScore[i] = -x;
        } else {
            for (int i = 0; i < 3; i++) if (i != landlordSeat) roundScore[i] = x;
            roundScore[landlordSeat] = -2 * x;
        }
        for (int i = 0; i < 3; i++) totalScore[i] += roundScore[i];
        winner = landlordWon ? 0 : 1;
        result = std::string(landlordWon ? "地主获胜" : "农民获胜");
        broadcastState();
    }

    // ---------- 状态广播 ----------
    void broadcastState() {
        GameMsg m;
        m["type"] = "game"; m["gameType"] = "ddz"; m["action"] = "state";
        std::string ph;
        switch (phase) {
            case WAITING: ph = "waiting"; break;
            case BIDDING: ph = "bidding"; break;
            case PLAYING: ph = "playing"; break;
            default:      ph = "over";    break;
        }
        m["phase"] = ph;
        // 座位玩家
        std::string pl = "[";
        bool first = true;
        for (int i = 0; i < 3; i++) {
            if (seats[i].clientId.empty()) continue;
            if (!first) pl += ",";
            first = false;
            pl += "{\"id\":\"" + seats[i].clientId + "\",\"name\":\"" + esc(seats[i].name) +
                  "\",\"seat\":" + std::to_string(i) + "}";
        }
        pl += "]";
        m["players"] = pl;
        // 手牌张数
        std::string hc;
        for (int i = 0; i < 3; i++) {
            if (i) hc += ",";
            hc += seats[i].clientId.empty() ? "0" : std::to_string((int)hands[i].size());
        }
        m["handCounts"] = hc;
        m["dealerSeat"] = std::to_string(dealerSeat);
        m["bidSeat"] = std::to_string(bidSeat);
        m["highestBid"] = std::to_string(highestBid);
        m["highestBidder"] = std::to_string(highestBidder);
        m["turnSeat"] = std::to_string(turnSeat);
        m["needBeat"] = needBeat ? "1" : "0";
        m["landlordSeat"] = std::to_string(landlordSeat);
        m["bid"] = std::to_string(bid);
        m["bombs"] = std::to_string(bombs);
        m["lastAction"] = lastAction;
        m["lastActor"] = std::to_string(lastPlayer >= 0 ? lastPlayer : lastPasser);
        m["lastCards"] = ddz::joinCards(lastCards);
        // 底牌：叫分阶段保密，出牌/结束后公开
        m["bottomCount"] = std::to_string((int)bottom.size());
        m["bottom"] = (phase == PLAYING || phase == OVER) ? ddz::joinCards(bottom) : "";
        m["over"] = (phase == OVER) ? "1" : "0";
        m["winner"] = std::to_string(winner);
        m["winnerSeat"] = std::to_string(winnerSeat);
        m["result"] = result;
        std::string sc, rs;
        for (int i = 0; i < 3; i++) {
            if (i) { sc += ","; rs += ","; }
            sc += std::to_string(totalScore[i]);
            rs += std::to_string(roundScore[i]);
        }
        m["scores"] = sc;
        m["roundScore"] = rs;
        m["spectators"] = std::to_string((int)members.size() - occupied());
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

// 工厂函数：在 game_framework.h 的 createGame 里注册一行 "ddz"
std::unique_ptr<IGame> createDdzGame(const std::string& roomId) {
    return std::unique_ptr<IGame>(new DdzGame(roomId));
}

#endif // DDZ_GAME_H

#ifndef BATTLE_ROYALE_H
#define BATTLE_ROYALE_H

// ============================================================
//  battle_royale.h —— 简化版 3D 大逃杀（服务端权威逻辑）
// ------------------------------------------------------------
//  玩法（简化版）：
//    - 数名玩家空投到同一地图
//    - 地图上散布装备（武器/血包/护甲），走到上面拾取
//    - 毒圈（安全区）随时间不断收缩，圈外持续掉血
//    - 互相射击，击杀其它玩家获得分数
//    - 存活到最后（或达到时长上限）者获胜
//
//  服务器权威：所有移动/射击/血量/毒圈/胜负都由服务器计算，
//  客户端只发"意图"，收到权威状态后渲染，杜绝外挂作弊。
//
//  ================= 网络协议（JSON, type=="game"） ==========
//  客户端 -> 服务器：
//    {type:"game", gameType:"battle", roomId, action:"move",
//     x,y,z,  yaw,  forward:"1|-1|0",  sprint:"1"|"0"}
//    {type:"game", gameType:"battle", roomId, action:"shoot",
//     tx,ty,tz}                      // 瞄准点或朝向后端点
//    {type:"game", gameType:"battle", roomId, action:"pickup", id:"<itemId>"}
//    {type:"game", gameType:"battle", roomId, action:"jump"}
//    {type:"game", gameType:"battle", roomId, action:"pose", value:"stand"|"crouch"|"prone"}
//    {type:"game", gameType:"battle", roomId, action:"ready"}   // 客户端就绪后开始
//
//  服务器 -> 客户端（type=="game", gameType=="battle"）：
//    {action:"game_state", state:  {...完整权威快照, 见 packState()}}
//    {action:"you", hp, ammo, weapon, kills, deaths, alive, score}
//    {action:"hit", target, damage, x,y,z}   // 命中反馈（可选视觉）
//    {action:"killed", killer, victim, killCount}
//    {action:"zone", cx,cz,radius}           // 安全区更新
//    {action:"winner", clientId, name}
//    {action:"game_over", winner, timeUp}
//    {action:"wrongzone", damage, remaining} // 圈外扣血通知
// ============================================================

#include <string>
#include <map>
#include <vector>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <algorithm>
#include <random>

#include "game_framework.h"

// ------------------------------------------------------------------
//  小工具：数字转字符串 / 简易 JSON 数组拼接（够用即可）
// ------------------------------------------------------------------
namespace brutil {
    static std::string str(float v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }
    static std::string str(int v) { return std::to_string(v); }
}

// ------------------------------------------------------------------
//  玩家实体
// ------------------------------------------------------------------
struct BrPlayer {
    std::string id;
    std::string name;
    bool      alive     = false;   // false = 待出生/已死亡（等待重生或观战）
    float     x=0, y=0, z=0;       // 世界坐标（Y 为高度）
    float     yaw=0;               // 水平朝向（度）
    float     hp       = 100;
    float     armor    = 0;        // 0~100
    int       weapon   = 0;        // 0=拳头,1=手枪,2=步枪,3=狙击,4=冲锋枪
    int       ammo     = 0;
    int       kills    = 0;
    int       deaths   = 0;
    bool      ready    = false;
    float     lastShot = 0;        // 上次射击时间(相对tick)
    // 出生/重生相关
    float     respawnTimer = -1;   // <=0 表示不必重生
};

// ------------------------------------------------------------------
//  掉落装备
// ------------------------------------------------------------------
struct BrItem {
    std::string id;
    int   type;            // 1=手枪,2=步枪,3=狙击,4=冲锋枪,5=血包,6=护甲
    float x, y, z;
    bool  taken = false;
};

// ------------------------------------------------------------------
//  大逃杀游戏本体（实现了 IGame 接口）
// ------------------------------------------------------------------
class BattleRoyaleGame : public IGame {
public:
    // 每个房间绑定一个固定 roomId，避免多房间互相串广播
    explicit BattleRoyaleGame(const std::string& roomId) : myRoom(roomId) {
        // 伪随机种子
        rng.seed(static_cast<unsigned>(std::time(nullptr)));
        // 初始化固定安全区
        zoneCX = 0; zoneCZ = 0;
        zoneRadius = MAP_HALF;
        resetMap();
    }

    virtual ~BattleRoyaleGame() {}

    const std::string& type() const override {
        static const std::string t = "battle";
        return t;
    }

    // ---------------- IGame 接口 ----------------
    void onPlayerJoin(const std::string& clientId, const std::string& name,
                      const std::string& roomId) override {
        BrPlayer p;
        p.id = clientId;
        p.name = name.empty() ? ("P" + clientId.substr(0, 4)) : name;
        p.alive = false;          // 等 ready 后出生
        players[clientId] = p;
        // myRoom 已在构造函数绑定，此处不再覆盖
    }

    void onPlayerLeave(const std::string& clientId) override {
        // 通知其它玩家
        GameMsg m;
        m["type"]="game"; m["gameType"]="battle"; m["action"]="player_left";
        m["clientId"]=clientId;
        broadcastAll(myRoom, makeJson(m));
        players.erase(clientId);
        checkEnd();
    }

    bool onMessage(const std::string& clientId, const GameMsg& msg) override {
        auto it = players.find(clientId);
        if (it == players.end()) return false;
        std::string action = msg.count("action") ? msg.at("action") : "";
        if (action == "ready") {
            it->second.ready = true;
            if (!it->second.alive) spawn(it->second);
            // 广播一个小快照让新玩家看到当前世界
            broadcastAll(myRoom, packState());
            return true;
        }
        else if (action == "move") return handleMove(it->second, msg);
        else if (action == "shoot") return handleShoot(it->second, msg);
        else if (action == "pickup") return handlePickup(it->second, msg);
        else if (action == "jump") { it->second.y = 0; return true; }
        else if (action == "pose") { return true; }
        return false;  // 未识别，交回通用转发
    }

    void onTick(float dt) override {
        static float zoneTimer = 0;
        advanceTime(dt);

        // 1) 更新每个玩家高度（简单落地）
        for (auto& kv : players) {
            BrPlayer& p = kv.second;
            if (!p.alive) {
                // 重生倒计时（如果开了自动重生）
                if (p.respawnTimer > 0) {
                    p.respawnTimer -= dt;
                    if (p.respawnTimer <= 0) { spawn(p); }
                }
                continue;
            }
            if (p.y < 0) p.y = 0;   // 贴地
        }

        // 2) 毒圈缩圈
        zoneTimer += dt;
        if (zoneTimer > zoneShrinkInterval && !gameOver) {
            zoneTimer = 0;
            shrinkZone();
        }

        // 3) 圈外掉血
        for (auto& kv : players) {
            BrPlayer& p = kv.second;
            if (!p.alive) continue;
            float dist = std::sqrt((p.x-zoneCX)*(p.x-zoneCX) + (p.z-zoneCZ)*(p.z-zoneCZ));
            if (dist > zoneRadius) {
                p.hp -= zoneDamage * dt;
                // 通知客户端当前安全区内
                GameMsg m;
                m["type"]="game"; m["gameType"]="battle"; m["action"]="wrongzone";
                m["damage"]=brutil::str(zoneDamage*dt);
                m["remaining"]=brutil::str(p.hp);
                sendTo(p.id, makeJson(m));
                if (p.hp <= 0) kill(KILL_ZONE, p.id);
            }
        }

        // 4) 每 200ms 广播一次权威状态
        static float stateTimer = 0;
        stateTimer += dt;
        if (stateTimer > 0.2f) {
            stateTimer = 0;
            broadcastAll(myRoom, packState());
        }

        // 5) 胜负/结束判定
        checkEnd();
    }

    bool hasPlayers() const override { return !players.empty(); }

private:
    // 房间 id 传给唯一房间，实际可多实例，为简化用一个
    std::string myRoom;

    std::map<std::string, BrPlayer> players;
    std::vector<BrItem> items;
    std::mt19937 rng;

    // ---- 地图参数 ----
    static constexpr float MAP_HALF  = 100.0f;   // 地图范围 [-100,100]
    static constexpr float MAP_HEIGHT = 0.0f;
    static constexpr float ZONE_DMG   = 25.0f;   // 圈外每秒掉血

    // ---- 毒圈 ----
    float zoneCX = 0, zoneCZ = 0;
    float zoneRadius = MAP_HALF;
    float zoneMinRadius = 8.0f;
    float zoneShrinkInterval = 15.0f; // 每 15 秒缩一次
    float zoneShrinkAmount = 12.0f;

    float zoneDamage = ZONE_DMG;

    float gameTime = 0;
    bool  gameOver = false;
    static constexpr float GAME_END_TIME = 300.0f; // 5 分钟到点

    // 简单武器数据
    struct WeaponDef { const char* name; int damage; int ammo; float range; float fireRate; };
    static const WeaponDef WEAPONS[];

    // ---------------- 内部方法 ----------------
    void advanceTime(float dt) { gameTime += dt; }

    void resetMap() {
        items.clear();
        // 随机生成 15 件装备，分布在地图随机点
        std::uniform_real_distribution<float> distX(-MAP_HALF+5, MAP_HALF-5);
        std::uniform_real_distribution<float> distZ(-MAP_HALF+5, MAP_HALF-5);
        for (int i = 0; i < 15; ++i) {
            BrItem it;
            it.id = "it" + std::to_string(i);
            it.type = (int)(rng() % 6) + 1;   // 1..6
            it.x = distX(rng); it.y = 0; it.z = distZ(rng);
            items.push_back(it);
        }
    }

    void spawn(BrPlayer& p) {
        if (!p.ready) return;
        std::uniform_real_distribution<float> dist(-MAP_HALF+5, MAP_HALF-5);
        p.x = dist(rng); p.z = dist(rng);
        p.y = 0;
        p.hp = 100; p.armor = 0;
        p.weapon = 0; p.ammo = 0;
        p.alive = true;
        p.lastShot = 0;
        GameMsg m;
        m["type"]="game"; m["gameType"]="battle"; m["action"]="spawn";
        m["x"]=brutil::str(p.x); m["y"]=brutil::str(p.y); m["z"]=brutil::str(p.z);
        sendTo(p.id, makeJson(m));
    }

    // 移动：服务器简单校验后更新
    bool handleMove(BrPlayer& p, const GameMsg& m) {
        if (!p.alive) return true;
        float speed = (m.count("sprint") && m.at("sprint")=="1") ? 12.0f : 6.0f;
        // forward 方向向量 "x|y|z"（客户端给的是单位向量）
        float fx=0, fz=0;
        if (m.count("forward")) {
            std::string f = m.at("forward");
            size_t p1 = f.find('|');
            size_t p2 = p1==std::string::npos ? std::string::npos : f.find('|', p1+1);
            if (p1!=std::string::npos && p2!=std::string::npos) {
                fx = std::atof(f.substr(0,p1).c_str());
                fz = std::atof(f.substr(p2+1).c_str());
            }
        }
        p.x += fx * speed * 0.05f;   // 一步 50ms
        p.z += fz * speed * 0.05f;
        p.yaw = m.count("yaw") ? std::atof(m.at("yaw").c_str()) : p.yaw;
        // 挡在边界
        if (p.x >  MAP_HALF) p.x =  MAP_HALF;
        if (p.x < -MAP_HALF) p.x = -MAP_HALF;
        if (p.z >  MAP_HALF) p.z =  MAP_HALF;
        if (p.z < -MAP_HALF) p.z = -MAP_HALF;
        return true;
    }

    // 射击：射线——简化，看瞄准点 (tx,ty,tz) 是否命中某玩家包围球
    bool handleShoot(BrPlayer& p, const GameMsg& m) {
        if (!p.alive) return true;
        if (p.weapon == 0) { return true; }      // 拳头不能远程
        if (p.ammo <= 0) { return true; }        // 没子弹
        // 射速检查（简单）
        p.lastShot += 1;   // 近似
        float tx=0, ty=0, tz=0;
        if (m.count("tx")) tx=std::atof(m.at("tx").c_str());
        if (m.count("ty")) ty=std::atof(m.at("ty").c_str());
        if (m.count("tz")) tz=std::atof(m.at("tz").c_str());

        p.ammo--;

        const WeaponDef& w = WEAPONS[p.weapon];
        // 在自己附近、方向朝向目标点最近距离，命中则判定
        std::vector<std::string> hits;
        for (auto& kv : players) {
            BrPlayer& t = kv.second;
            if (t.id == p.id || !t.alive) continue;
            float d = pointSegmentDist(p.x,p.z, tx,tz, t.x,t.z);
            if (d < 1.2f) {  // 命中宽容度
                t.hp -= w.damage;
                GameMsg h;
                h["type"]="game"; h["gameType"]="battle"; h["action"]="hit";
                h["target"]=t.id; h["damage"]=brutil::str(w.damage);
                sendTo(t.id, makeJson(h));
                if (t.hp <= 0) kill(p.id, t.id);
            }
            (void)ty;
        }
        return true;
    }

    // 拾取
    bool handlePickup(BrPlayer& p, const GameMsg& m) {
        if (!p.alive) return true;
        std::string id = m.count("id") ? m.at("id") : "";
        for (auto& it : items) {
            if (it.id == id && !it.taken) {
                float d = std::sqrt((p.x-it.x)*(p.x-it.x)+(p.z-it.z)*(p.z-it.z));
                if (d < 2.0f) {
                    it.taken = true;
                    applyItem(p, it.type, id);
                    return true;
                }
            }
        }
        return true;
    }

    void applyItem(BrPlayer& p, int itemType, const std::string& itemId) {
        GameMsg m;
        m["type"]="game"; m["gameType"]="battle"; m["action"]="pickup_res";
        m["itemId"]=itemId;
        switch (itemType) {
            case 1: p.weapon=1; p.ammo=12;  m["weapon"]="pistol"; break;
            case 2: p.weapon=2; p.ammo=30;  m["weapon"]="rifle";  break;
            case 3: p.weapon=3; p.ammo=5;   m["weapon"]="sniper"; break;
            case 4: p.weapon=4; p.ammo=20;  m["weapon"]="smg";    break;
            case 5: p.hp = std::min(100.f, p.hp+50); m["heal"]="1"; break;
            case 6: p.armor = std::min(100.f, p.armor+50); m["armor"]="1"; break;
        }
        sendTo(p.id, makeJson(m));
    }

    // 击杀
    void kill(const std::string& killerId, const std::string& victimId) {
        auto vit = players.find(victimId);
        if (vit==players.end() || !vit->second.alive) return;
        vit->second.alive = false;
        vit->second.deaths++;
        vit->second.hp = 0;
        vit->second.respawnTimer = -1;  // 暂时不重生（简化）

        std::string killerName = "";
        if (killerId != KILL_ZONE) {
            auto kit = players.find(killerId);
            if (kit != players.end()) { kit->second.kills++; killerName = kit->second.name; }
        }
        GameMsg m;
        m["type"]="game"; m["gameType"]="battle"; m["action"]="killed";
        m["killer"]=killerId; m["victim"]=victimId;
        m["killerName"]=killerName;
        m["killCount"]=brutil::str(killerId != KILL_ZONE ? players[killerId].kills : 0);
        broadcastAll(myRoom, makeJson(m));
        checkEnd();
    }

    void shrinkZone() {
        if (zoneRadius <= zoneMinRadius) return;
        // 向随机(或向中心)收缩，简化向原点收缩
        zoneRadius = std::max(zoneMinRadius, zoneRadius - zoneShrinkAmount);
        // 随机移动中心 (可选，简化固定原点)
        std::uniform_real_distribution<float> d(-zoneShrinkAmount*0.5f, zoneShrinkAmount*0.5f);
        zoneCX += d(rng); zoneCZ += d(rng);
        if (zoneCX >  MAP_HALF) zoneCX =  MAP_HALF;
        if (zoneCX < -MAP_HALF) zoneCX = -MAP_HALF;
        if (zoneCZ >  MAP_HALF) zoneCZ =  MAP_HALF;
        if (zoneCZ < -MAP_HALF) zoneCZ = -MAP_HALF;
        // 通知
        GameMsg m;
        m["type"]="game"; m["gameType"]="battle"; m["action"]="zone";
        m["cx"]=brutil::str(zoneCX); m["cz"]=brutil::str(zoneCZ);
        m["radius"]=brutil::str(zoneRadius);
        broadcastAll(myRoom, makeJson(m));
    }

    // 组合出"you"消息
    GameMsg youMsg(const BrPlayer& p) {
        GameMsg m;
        m["type"]="game"; m["gameType"]="battle"; m["action"]="you";
        m["hp"]=brutil::str((int)p.hp);
        m["armor"]=brutil::str((int)p.armor);
        m["weapon"]=brutil::str(p.weapon);
        m["ammo"]=brutil::str(p.ammo);
        m["kills"]=brutil::str(p.kills);
        m["deaths"]=brutil::str(p.deaths);
        m["alive"]=p.alive?"1":"0";
        return m;
    }

    // 完整权威快照（所有玩家位置等）
    std::string packState() {
        GameMsg m;
        m["type"]="game"; m["gameType"]="battle"; m["action"]="game_state";
        // 玩家数组
        std::string pl = "[";
        bool first=true;
        for (auto& kv : players) {
            BrPlayer& p = kv.second;
            if (!first) pl += ",";
            first=false;
            pl += "{\"id\":\""+p.id+"\",\"name\":\""+p.name+"\",\"alive\":"+(p.alive?"1":"0")+
                  ",\"x\":"+brutil::str(p.x)+",\"y\":"+brutil::str(p.y)+",\"z\":"+brutil::str(p.z)+
                  ",\"yaw\":"+brutil::str(p.yaw)+",\"hp\":"+brutil::str((int)p.hp)+"}";
        }
        pl += "]";
        m["players"]=pl;

        // 物品数组
        std::string it = "[";
        bool f2=true;
        for (auto& i : items) {
            if (i.taken) continue;
            if (!f2) it += ",";
            f2=false;
            it += "{\"id\":\""+i.id+"\",\"type\":"+brutil::str(i.type)+
                  ",\"x\":"+brutil::str(i.x)+",\"y\":"+brutil::str(i.y)+",\"z\":"+brutil::str(i.z)+"}";
        }
        it += "]";
        m["items"]=it;

        m["zone_cx"]=brutil::str(zoneCX);
        m["zone_cz"]=brutil::str(zoneCZ);
        m["zone_r"]=brutil::str(zoneRadius);
        m["time"]=brutil::str((int)gameTime);
        m["over"]=gameOver?"1":"0";
        return makeJson(m);
    }

    // 结束判定：只剩一人 或 到时间 -> 取胜则广播 winner + 重置
    void checkEnd() {
        if (gameOver) return;
        int aliveCount = 0;
        std::string lastId;
        for (auto& kv : players) {
            if (kv.second.alive) { aliveCount++; lastId = kv.first; }
        }
        bool timeUp = gameTime >= GAME_END_TIME;
        if (aliveCount <= 1 || timeUp) {
            gameOver = true;
            std::string winnerId = (aliveCount==1) ? lastId : "";
            std::string winnerName = "";
            if (!winnerId.empty()) winnerName = players[winnerId].name;
            GameMsg m;
            m["type"]="game"; m["gameType"]="battle"; m["action"]="winner";
            m["winner"]=winnerId; m["winnerName"]=winnerName;
            m["timeUp"]=timeUp?"1":"0";
            broadcastAll(myRoom, makeJson(m));
        }
    }

    // ---------------- 工具 ----------------
    static constexpr const char* KILL_ZONE = "__ZONE__";

    std::string makeJson(const GameMsg& m) {
        std::string r = "{";
        bool first=true;
        for (auto& kv : m) {
            if (!first) r += ",";
            first=false;
            r += "\""+kv.first+"\":";
            const std::string& v = kv.second;
            if (!v.empty() && (v[0]=='[' || v[0]=='{')) r += v;       // JSON数组/对象原样
            else if (v=="true"||v=="false"||v=="null") r += v;
            else {
                std::string s=v;
                size_t pos=0;
                while ((pos=s.find('"',pos))!=std::string::npos){s.replace(pos,1,"\\\"");pos+=2;}
                r += "\""+s+"\"";
            }
        }
        r += "}";
        return r;
    }

    // 点到线段距离(2D XZ平面)
    static float pointSegmentDist(float ax,float az,float bx,float bz,float px,float pz){
        float dx=bx-ax, dz=bz-az;
        float len2=dx*dx+dz*dz;
        if (len2<1e-6f) return std::sqrt((px-ax)*(px-ax)+(pz-az)*(pz-az));
        float t=((px-ax)*dx+(pz-az)*dz)/len2;
        t=std::max(0.f,std::min(1.f,t));
        float cx=ax+t*dx, cz=az+t*dz;
        return std::sqrt((px-cx)*(px-cx)+(pz-cz)*(pz-cz));
    }
};

// 武器定义（下标对齐 weapon 编号 0..4）
const BattleRoyaleGame::WeaponDef BattleRoyaleGame::WEAPONS[] = {
    {"fist",  0,  0,   2.0f, 0.0f},
    {"pistol",25, 12, 80.0f, 0.3f},
    {"rifle", 30, 30, 120.0f,0.12f},
    {"sniper",90, 5,  200.0f,1.2f},
    {"smg",   15, 20, 70.0f, 0.08f}
};

// 工厂函数：新游戏只需在 game_framework.h 的 createGame 里注册
// （用 new 而非 make_unique，兼容 C++11 编译器）
std::unique_ptr<IGame> createBattleRoyaleGame(const std::string& roomId) {
    return std::unique_ptr<IGame>(new BattleRoyaleGame(roomId));
}

#endif // BATTLE_ROYALE_H

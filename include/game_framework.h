#ifndef GAME_FRAMEWORK_H
#define GAME_FRAMEWORK_H

// ============================================================
//  game_framework.h  ——  可扩展游戏框架（header-only）
// ------------------------------------------------------------
//  设计目标：
//    1. 新增一种游戏只需：
//         a. 写一个类实现 IGame 接口
//         b. 在 GameRegistry::createGame() 里加一行注册
//       其余一切（路由、tick、广播）自动完成。
//    2. 完全不触碰 OI Station 原有逻辑，只改 server.cpp 的
//       三个挂接点（见 server.cpp 补丁说明）。
//
//  本文件只定义接口与注册表，不包含任何具体游戏。
//  具体游戏（如大逃杀）单独放 battle_royale.h。
// ============================================================

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <mutex>
#include <memory>
#include <algorithm>

// 游戏数据包：即 server.cpp 里的 SimpleJson 解析结果（map<string,string>）
using GameMsg = std::map<std::string, std::string>;

// ------------------------------------------------------------
//  IGame —— 所有游戏必须实现的抽象接口（策略模式核心）
//
//  生命周期：
//    创建  -> onPlayerJoin(每个加入者调用一次)
//           -> onPlayerInput(每个游戏消息分发)
//           -> onTick(固定频率由 tick 线程调用, dt=秒)
//           -> 玩家断开 -> onPlayerLeave
//    房间空  -> 由 registry 决定回收 onDestroy
// ------------------------------------------------------------
class IGame {
public:
    virtual ~IGame() {}

    // 返回游戏类型标识（如 "battle"），join 时用于路由
    virtual const std::string& type() const = 0;

    // 玩家加入房间时调用（name 可为空）
    virtual void onPlayerJoin(const std::string& clientId,
                              const std::string& name,
                              const std::string& roomId) = 0;

    // 玩家离开 / 断开时调用
    virtual void onPlayerLeave(const std::string& clientId) = 0;

    // 玩家发来一条游戏消息（type=="game" 且 gameType 匹配）
    // return false 表示该消息未被游戏处理，可继续走服务器通用转发
    virtual bool onMessage(const std::string& clientId,
                           const GameMsg& msg) = 0;

    // 服务器固定步长调用（毫秒级步长），用于权威逻辑
    virtual void onTick(float dt) = 0;

    // 房间内是否还有人（决定是否回收）
    virtual bool hasPlayers() const = 0;

    // 向房间内广播（由服务器注入）
    typedef std::function<void(const std::string& roomId,
                               const std::string& json,
                               const std::string& senderId)> BroadcastFn;
    typedef std::function<void(const std::string& clientId,
                               const std::string& json)> SendFn;
    void setBroadcastFn(BroadcastFn fn) { broadcast = fn; }
    void setSendFn(SendFn fn) { send = fn; }

protected:
    // 子类可用的广播/单发工具
    void broadcastAll(const std::string& roomId, const std::string& json) {
        if (broadcast) broadcast(roomId, json, "");
    }
    void broadcastExcept(const std::string& roomId, const std::string& json,
                         const std::string& senderId) {
        if (broadcast) broadcast(roomId, json, senderId);
    }
    void sendTo(const std::string& clientId, const std::string& json) {
        if (send) send(clientId, json);
    }

private:
    BroadcastFn broadcast;
    SendFn send;
};

// ------------------------------------------------------------
//  GameRegistry —— 按 房间 -> 游戏实例 管理游戏对象
//
//  自动逻辑：
//    - 玩家 join 某房间且 gameType 匹配新游戏时，自动创建实例
//    - 房间为空时自动删除实例（资源回收）
//    - onTick 委托给所有存活实例
// ------------------------------------------------------------
class GameRegistry {
public:
    GameRegistry() {}

    // 创建具体游戏实例（每种游戏在 case 里注册一行）——可扩展点
    static std::unique_ptr<IGame> createGame(const std::string& gameType,
                                             const std::string& roomId);

    // 分发一条游戏消息。返回 true 表示某游戏消费了该消息。
    // 若房间没有对应该 gameType 的实例，会尝试创建。
    bool routeMessage(const std::string& clientId,
                      const GameMsg& msg);

    void onLockPlayerJoin(const std::string& clientId,
                          const std::string& roomId,
                          const std::string& gameType,
                          const std::string& name);

    void onPlayerLeave(const std::string& clientId,
                       const std::string& roomId);

    void tickAll(float dt);

    // 服务器注入广播能力
    void setBroadcastFn(IGame::BroadcastFn fn) { broadcastFn = fn; }
    void setSendFn(IGame::SendFn fn) { sendFn = fn; }

private:
    std::map<std::string, std::unique_ptr<IGame>> roomGames;   // roomId -> game
    std::map<std::string, std::string> clientRoom;             // clientId -> roomId
    std::mutex mutex;
    IGame::BroadcastFn broadcastFn;
    IGame::SendFn sendFn;

    std::string roomOf(const std::string& clientId) {
        auto it = clientRoom.find(clientId);
        return it == clientRoom.end() ? "" : it->second;
    }
};

// ==================== 实现 (header-only) ====================

std::unique_ptr<IGame> GameRegistry::createGame(const std::string& gameType,
                                                const std::string& roomId) {
    // ============ 【可扩展点】新游戏在这里注册一行 ============
    // 例如： return std::make_unique<MyGame>(roomId);
    // 目前仅支持大逃杀（battle）
    extern std::unique_ptr<IGame> createBattleRoyaleGame(const std::string& roomId);
    if (gameType == "battle") return createBattleRoyaleGame(roomId);
    // ========================================================
    return nullptr;
}

void GameRegistry::onLockPlayerJoin(const std::string& clientId,
                                    const std::string& roomId,
                                    const std::string& gameType,
                                    const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex);
    clientRoom[clientId] = roomId;

    // 该房间还没有游戏实例 -> 按 gameType 创建
    auto it = roomGames.find(roomId);
    if (it == roomGames.end()) {
        auto game = createGame(gameType, roomId);
        if (game) {
            game->setBroadcastFn(broadcastFn);
            game->setSendFn(sendFn);
            roomGames[roomId] = std::move(game);
            it = roomGames.find(roomId);
        }
    }
    if (it != roomGames.end()) {
        it->second->onPlayerJoin(clientId, name, roomId);
    }
}

void GameRegistry::onPlayerLeave(const std::string& clientId,
                                 const std::string& roomId) {
    std::lock_guard<std::mutex> lock(mutex);
    clientRoom.erase(clientId);

    auto it = roomGames.find(roomId);
    if (it != roomGames.end()) {
        it->second->onPlayerLeave(clientId);
        // 房间空了就回收游戏实例
        if (!it->second->hasPlayers()) {
            roomGames.erase(it);
        }
    }
}

bool GameRegistry::routeMessage(const std::string& clientId, const GameMsg& msg) {
    std::lock_guard<std::mutex> lock(mutex);
    std::string roomId = msg.count("roomId") ? msg.at("roomId") : "";
    auto it = roomGames.find(roomId);
    if (it == roomGames.end()) return false;
    return it->second->onMessage(clientId, msg);
}

void GameRegistry::tickAll(float dt) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& kv : roomGames) {
        kv.second->onTick(dt);
    }
}

#endif // GAME_FRAMEWORK_H

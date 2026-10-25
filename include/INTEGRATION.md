# 大逃杀模块 —— 挂接说明（零侵入）

新增两个 header-only 文件，**不改动 OI Station 任何原有逻辑**，只需对
`server.h` 和 `server.cpp` 做 5 处最小粘贴。全部改动都在新增注释块内。

---

## 1. server.h 新增成员

在 `HttpServer` 类私有区，`roomManager` 声明附近，加入：

```cpp
// ===== 大逃杀模块（新增）=====
class GameRegistry;                 // 前置声明（本声明可放顶部）
GameRegistry* gameRegistry;         // 游戏注册表
std::thread  battleTickThread;      // 服务器权威 tick 线程
std::atomic<bool> battleTickRunning;
// =============================
```

> 把本文件顶部 `#include <atomic>`（已包含）即可。若你的"原版" server.h 没 include
> `<thread>` / `<atomic>`，请补上（你的工程已有）。

---

## 2. server.cpp 顶部 include

```cpp
#include "../include/game_framework.h"
#include "../include/battle_royale.h"
```

> 也可按你的 include 目录结构调整为 `"game_framework.h"`。
> 注意这两个文件只是头文件，无需改构建（CMake/Makefile）即可用。

---

## 3. 构造函数初始化

在 `HttpServer::HttpServer` 构造函数体末尾（`roomManager = new GameRoomManager();` 之后）加：

```cpp
gameRegistry = new GameRegistry();
gameRegistry->setBroadcastFn(
    [this](const std::string& roomId, const std::string& json, const std::string& senderId){
        if (senderId.empty()) this->broadcastToRoomIncludingSender(roomId, json);
        else                   this->broadcastToRoom(roomId, json, senderId);
    });
gameRegistry->setSendFn(
    [this](const std::string& clientId, const std::string& json){
        this->sendToClient(clientId, json);
    });
```

在析构函数里（`delete roomManager` 旁）加：
```cpp
if (gameRegistry) delete gameRegistry;
```

---

## 4. handleGameMessage —— 挂接游戏路由（关键）

在你 `handleGameMessage` 的**第一行**（`std::string roomId = ...` 之前）插入：

```cpp
// ===== 大逃杀路由：若某游戏消费了该消息则不再走通用转发 =====
if (gameRegistry->routeMessage(clientId, msgData)) return;
// 注意：此时玩家尚未 join，路由要求 roomId 已存在。需要另做：
```

但要注意：`join` 动作在 `GameRoomManager` 里注册，而 `GameRegistry` 也要感知加入。
所以正确的做法是在 `action=="join"` 分支里，`roomManager->joinRoom(...)` **之后**加：

```cpp
gameRegistry->onLockPlayerJoin(clientId, roomId, gameType, name);
```

并在 `action=="leave"` 分支和客户端断开清理处加：

```cpp
gameRegistry->onPlayerLeave(clientId, roomId);
```

说明：
- 这里 `onLockPlayerJoin` 会按 `gameType=="battle"` 自动创建 `BattleRoyaleGame` 实例，
  并调用它的 `onPlayerJoin`。
- 路由其它游戏消息（move/shoot/pickup…）时，因客户端都会带 `roomId`，
  `routeMessage` 会找到该房间对应游戏实例并处理；若不是 battle 消息则返回 false，
  继续走你原有的通用转发 —— OI Station 完全不受影响。

---

## 5. run() / stop() —— 启动服务器权威 tick

在你的 `run()` 中、创建 worker 线程之前（`int threadCount=8;` 之前）加：

```cpp
// 启动游戏 tick 线程（每 50ms 一个逻辑步，50Hz）
battleTickRunning = true;
battleTickThread = std::thread([this](){
    float acc = 0;
    const float STEP = 0.05f;
    while (battleTickRunning) {
        auto t0 = std::chrono::steady_clock::now();
        gameRegistry->tickAll(STEP);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
        if (ms < (int)(STEP*1000))
            std::this_thread::sleep_for(std::chrono::milliseconds((int)(STEP*1000)-ms));
        (void)acc;
    }
});
```

在 `stop()` 末尾（`workerThreads` join 之后）加：

```cpp
battleTickRunning = false;
if (battleTickThread.joinable()) battleTickThread.join();
```

---

## 完成

编译并运行。加入一个 `gameType="battle"` 的房间号（例如 `roomId="br1"`），多名玩家
用浏览器连上后，即进入简化 3D 大逃杀对战。

> 游戏逻辑全部服务端权威：移动/射击/血量/缩圈/胜负，客户端只渲染和发意图。
> 这从根本上杜绝了客户端篡改数值的作弊（对局域网虽低风险，但架构上是对的）。

---

## 扩展新游戏模式

想加一种新模式（如"捉迷藏"/"团队死斗"）：

1. 新建 `mygame.h`，实现 `IGame` 接口（方法见 battle_royale.h 示例）。
2. 在 `GameFramework.h` 的 `GameRegistry::createGame()` 里加一行：

```cpp
if (gameType == "mytag") return createMyGame();
```

3. 完事。其余路由/广播/tick 全部自动复用，无需再改服务器。

这就是"易于扩展"的架构：**新增模式 = 一个新类 + 一行注册**。

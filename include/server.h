#ifndef OI_SERVER_H
#define OI_SERVER_H

#include <string>
#include <map>
#include <functional>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>

#pragma comment(lib, "ws2_32.lib")

// 原生 HTTP 服务器
typedef std::function<std::string(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::map<std::string, std::string>& headers,
    std::map<std::string, std::string>& responseHeaders,
    std::string& sessionId)> RequestHandler;

// WebSocket 处理器
typedef std::function<void(
    const std::string& clientId,
    const std::string& message,
    std::function<void(const std::string&)> send)> WebSocketHandler;

// 前端服务
class GameRoomManager;
class GameRegistry;
class DataStore;
class TokenManager;
class LLMHandler;

class HttpServer {
public:
    HttpServer(int port);
    ~HttpServer();

    bool start();
    void setHandler(RequestHandler handler);
    void run();
    void stop();

    // WebSocket 类
    void setWebSocketHandler(WebSocketHandler handler);
    void sendToClient(const std::string& clientId, const std::string& message);

    // 游戏管理器
    void broadcastToRoom(const std::string& roomId, const std::string& message, const std::string& senderId = "");
    std::vector<std::string> getClientsInRoom(const std::string& roomId);
    std::string getClientRoom(const std::string& clientId);

private:
    SOCKET serverFd;
    int port;
    std::atomic<bool> running;
    bool wsInitialized;
    RequestHandler handler;
    WebSocketHandler wsHandler;

    std::vector<std::thread> workerThreads;
    std::mutex acceptMutex;
    std::mutex clientMutex;

    // WebSocket 客户端管理
    std::unordered_map<std::string, SOCKET> clientSockets;
    std::unordered_map<SOCKET, std::string> socketClients;

    // 游戏管理器实例
    GameRoomManager* roomManager;

    // 狼人杀模块
    GameRegistry* gameRegistry;
    std::thread battleTickThread;
    std::atomic<bool> battleTickRunning;

    // ===== LLM 模块 =====
    DataStore* dataStore;        // 数据存储（使用 models.h 中的单例）
    TokenManager* tokenManager;  // Token 转换器类
    LLMHandler* llmHandler;      // LLM 处理器

    // 原生私有方法
    void workerLoop();
    void handleClient(SOCKET clientFd, const std::string& clientIp);
    bool parseRequest(SOCKET fd, std::string& method, std::string& path,
                      std::map<std::string, std::string>& headers,
                      std::string& body);
    void sendResponse(SOCKET fd, int statusCode, const std::string& statusMsg,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body);
    void sendFile(SOCKET fd, const std::string& path);
    std::string getMimeType(const std::string& path);
    std::string recvAll(SOCKET fd, int timeoutMs);
    void setNonBlocking(SOCKET fd);

    // WebSocket
    void handleWebSocketHandshake(SOCKET fd, const std::map<std::string, std::string>& headers);
    void wsMessageLoop(SOCKET fd, const std::string& clientId);

    // 游戏消息处理
    void handleGameMessage(const std::string& clientId, const std::map<std::string, std::string>& msgData);
    void sendPlayerList(const std::string& roomId);
    void broadcastToRoomIncludingSender(const std::string& roomId, const std::string& message);

    // ===== LLM 接口 =====
    void handleLLMRequest(SOCKET fd, const std::string& path, const std::map<std::string, std::string>& headers, const std::string& body);
    bool authenticateUser(const std::string& username, const std::string& password, std::string& userId);
    bool isAdmin(const std::string& userId);

    // 静态变量
    static std::string sha1(const std::string& input);
    static std::string base64Encode(const std::string& input);
    static std::string decodeWebSocketFrame(const std::string& data);
    static std::string buildWebSocketFrame(const std::string& data);
};

#endif

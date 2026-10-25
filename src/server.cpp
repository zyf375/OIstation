#include "../include/ddz_game.h"
#include "../include/game_framework.h"
#include "../include/battle_royale.h"
#include "../include/server.h"
#include "../include/common.h"
#include "../include/session.h"
#include "../include/go_game.h"
#include "../include/models.h"
#include "../include/security.h"
#include "../include/ai_quota.h"
#include "../include/ai_tools.h"
#include "../include/ai_models.h"
#include "../include/ai_providers.h"
#include "../include/ai_plans.h"
#include "../include/admin_perm.h"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <map>
#include <vector>
#include <mutex>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <unordered_map>
#include <fstream>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

// ========== 全局声明 ==========
#define OLLAMA_MODEL "deepseek-chat"
#define OLLAMA_URL L"https://api.deepseek.com/chat/completions"
#define TOKEN_FILE "./data/tokens.json"
#include <windows.h>
#include <tlhelp32.h>

// ========== Token 结构体 ==========
struct TokenCode {
    std::string code;
    int amount;
    bool used;
    std::string usedBy;
    std::string createdAt;
    std::string expiresAt;
};

// ========== Token 管理器 ==========
class TokenManager {
private:
    std::unordered_map<std::string, TokenCode> tokens;
    std::mutex mutex;
    std::string tokenFile;
    
    void loadTokens() {
        tokens.clear();
        
        if (!common::fileExists(tokenFile)) return;
        
        std::string content = common::readFile(tokenFile);
        auto lines = common::split(content, '\n');
        for (const auto& line : lines) {
            if (line.empty()) continue;
            auto obj = common::jsonParseObject(line);
            if (obj.empty()) continue;
            TokenCode token;
            token.code = obj.count("code") ? obj["code"] : "";
            token.amount = obj.count("amount") ? std::stoi(obj["amount"]) : 0;
            token.used = obj.count("used") ? (obj["used"] == "true") : false;
            token.usedBy = obj.count("usedBy") ? obj["usedBy"] : "";
            token.createdAt = obj.count("createdAt") ? obj["createdAt"] : "";
            token.expiresAt = obj.count("expiresAt") ? obj["expiresAt"] : "";
            if (!token.code.empty()) {
                tokens[token.code] = token;
            }
        }
    }
    
    void saveTokens() {
        std::string data;
        for (const auto& pair : tokens) {
            const TokenCode& token = pair.second;
            std::map<std::string, std::string> obj;
            obj["code"] = token.code;
            obj["amount"] = std::to_string(token.amount);
            obj["used"] = token.used ? "true" : "false";
            obj["usedBy"] = token.usedBy;
            obj["createdAt"] = token.createdAt;
            obj["expiresAt"] = token.expiresAt;
            data += common::jsonStringify(obj) + "\n";
        }
        common::writeFile(tokenFile, data);
    }
    
    std::string getCurrentTime() {
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        return std::string(buf);
    }
    
    std::string generateCode() {
        // 每次基于时间+时钟+进程号重新播种，避免连续生成重复码
        unsigned long long seed = (unsigned long long)time(NULL)
                                ^ ((unsigned long long)clock() << 32)
                                ^ ((unsigned long long)GetCurrentProcessId() << 16);
        srand((unsigned)seed);
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string code;
        do {
        code.clear();
        for (int i = 0; i < 16; i++) {
            code += chars[rand() % 36];
        }
        } while (tokens.count(code)); // 保证唯一
        return code;
    }
    
public:
    TokenManager() {
        tokenFile = TOKEN_FILE;
        loadTokens();
    }
    
    ~TokenManager() {
        saveTokens();
    }
    
    std::string generateTokenCode(int amount, int expireDays = 30) {
        std::lock_guard<std::mutex> lock(mutex);
        TokenCode token;
        token.code = generateCode();
        token.amount = amount;
        token.used = false;
        token.createdAt = getCurrentTime();
        
        time_t now = time(nullptr);
        now += expireDays * 24 * 3600;
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        token.expiresAt = std::string(buf);
        
        tokens[token.code] = token;
        saveTokens();
        return token.code;
    }
    
    bool redeemCode(const std::string& userId, const std::string& code, int& amount) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = tokens.find(code);
        if (it == tokens.end()) return false;
        if (it->second.used) return false;
        
        time_t now = time(nullptr);
        struct tm tm = {};
        int year, month, day, hour, min, sec;
        sscanf(it->second.expiresAt.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec);
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;
        time_t expireTime = mktime(&tm);
        if (now > expireTime) return false;
        
        it->second.used = true;
        it->second.usedBy = userId;
        amount = it->second.amount;
        saveTokens();
        return true;
    }

    // 列出所有兑换码（管理员）
    std::string listCodesJson() {
        std::lock_guard<std::mutex> lock(mutex);
        std::string arr = "[";
        bool first = true;
        for (const auto& kv : tokens) {
            const TokenCode& t = kv.second;
            if (!first) arr += ",";
            first = false;
            std::map<std::string, std::string> m;
            m["code"] = t.code;
            m["amount"] = std::to_string(t.amount);
            m["used"] = t.used ? "1" : "0";
            m["usedBy"] = t.usedBy;
            m["createdAt"] = t.createdAt;
            m["expiresAt"] = t.expiresAt;
            arr += common::jsonStringify(m);
        }
        arr += "]";
        return arr;
    }
};


// ========== LLM 处理器 ==========
class LLMHandler {
private:
    std::string modelName = OLLAMA_MODEL;
    DataStore* dataStore;
    TokenManager* tokenManager;
    
    std::string httpPost(const std::string& url, const std::string& jsonData) {
        std::string result;
        
        std::wstring wurl = std::wstring(url.begin(), url.end());
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwHostNameLength = 1;
        urlComp.dwUrlPathLength = 1;
        
        wchar_t hostName[256] = {0};
        wchar_t urlPath[1024] = {0};
        urlComp.lpszHostName = hostName;
        urlComp.dwHostNameLength = 256;
        urlComp.lpszUrlPath = urlPath;
        urlComp.dwUrlPathLength = 1024;
        
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
            return "Error: Invalid URL";
        }
        
        HINTERNET hSession = WinHttpOpen(L"OIStation/1.0", 
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            return "Error: WinHttpOpen failed";
        }
        
        HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return "Error: WinHttpConnect failed";
        }
        
        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath, NULL, 
                                                WINHTTP_NO_REFERER, 
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "Error: WinHttpOpenRequest failed";
        }
        
        DWORD timeout = 60000;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        
        std::wstring headers = L"Content-Type: application/json\r\n";
        
        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(),
                                (LPVOID)jsonData.c_str(), jsonData.length(),
                                jsonData.length(), 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "Error: WinHttpSendRequest failed";
        }
        
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "Error: WinHttpReceiveResponse failed";
        }
        
        DWORD bytesRead = 0;
        char buffer[4096];
        while (WinHttpReadData(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result += buffer;
        }
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        return result;
    }
    
public:
    LLMHandler(DataStore* ds, TokenManager* tm) : dataStore(ds), tokenManager(tm) {}
    
    std::string chat(const std::string& userId, const std::string& userMessage) {
        auto user = dataStore->findUserById(userId);
        if (!user) return "Error: User not found";
        
        if (user->tokens < 1) return "Error: Insufficient tokens. Please redeem a token code.";
        
        if (!dataStore->useTokens(userId, 1)) {
            return "Error: Failed to deduct token.";
        }
        
        std::map<std::string, std::string> request;
        request["model"] = modelName;
        request["stream"] = "false";
        
        std::string messages = "[{\"role\":\"system\",\"content\":\"You are a helpful assistant specialized in programming.\"},{\"role\":\"user\",\"content\":\"" + userMessage + "\"}]";
        request["messages"] = messages;
        
        std::string jsonStr = common::jsonStringify(request);
        std::string response = httpPost("http://localhost:11434/api/chat", jsonStr);
        
        if (response.find("Error:") == 0) {
            return response;
        }
        
        try {
            auto responseObj = common::jsonParseObject(response);
            if (responseObj.find("message") != responseObj.end()) {
                size_t pos = response.find("\"message\":");
                if (pos != std::string::npos) {
                    size_t start = response.find("{", pos);
                    int depth = 0;
                    size_t end = start;
                    while (end < response.length()) {
                        if (response[end] == '{') depth++;
                        else if (response[end] == '}') {
                            depth--;
                            if (depth == 0) break;
                        }
                        end++;
                    }
                    if (end < response.length()) {
                        std::string msgStr = response.substr(start, end - start + 1);
                        auto msgObj = common::jsonParseObject(msgStr);
                        if (msgObj.find("content") != msgObj.end()) {
                            return msgObj["content"];
                        }
                    }
                }
            }
            if (responseObj.find("error") != responseObj.end()) {
                return "Error: " + responseObj.at("error");
            }
        } catch (...) {
            return "Error: Failed to parse response";
        }
        
        return "Error: Unknown response";
    }
};

// ========== 静态文件 MIME 映射 ==========
static std::map<std::string, std::string> g_extraMimeTypes = {
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".js", "application/javascript; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".txt", "text/plain; charset=utf-8"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {".eot", "application/vnd.ms-fontobject"},
    {".mp4", "video/mp4"},
    {".webm", "video/webm"},
    {".mp3", "audio/mpeg"},
    {".wav", "audio/wav"},
};

// ========== SHA1 & Base64 ==========
static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e;
    uint32_t w[80];
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = (buffer[i*4] << 24) | (buffer[i*4+1] << 16) | (buffer[i*4+2] << 8) | buffer[i*4+3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) << 1) | ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) >> 31);
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t temp = (((a << 5) | (a >> 27)) + f + e + k + w[i]);
        e = d; d = c; c = ((b << 30) | (b >> 2)); b = a; a = temp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

std::string HttpServer::sha1(const std::string& input) {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> data(input.begin(), input.end());
    data.push_back(0x80);
    while ((data.size() + 8) % 64 != 0) data.push_back(0);
    uint64_t bit_len = input.length() * 8;
    for (int i = 7; i >= 0; i--) data.push_back((bit_len >> (i * 8)) & 0xFF);
    for (size_t i = 0; i < data.size(); i += 64) {
        sha1_transform(state, &data[i]);
    }
    std::string result;
    for (int i = 0; i < 5; i++) {
        result.push_back((state[i] >> 24) & 0xFF);
        result.push_back((state[i] >> 16) & 0xFF);
        result.push_back((state[i] >> 8) & 0xFF);
        result.push_back(state[i] & 0xFF);
    }
    return result;
}

std::string HttpServer::base64Encode(const std::string& input) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    std::string output;
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];
    size_t len = input.length();
    const unsigned char* bytes = (const unsigned char*)input.c_str();
    while (len--) {
        char_array_3[i++] = *(bytes++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) output += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) output += base64_chars[char_array_4[j]];
        while ((i++ < 3)) output += '=';
    }
    return output;
}

// ========== WebSocket 帧解析 ==========
std::string HttpServer::decodeWebSocketFrame(const std::string& data) {
    if (data.length() < 2) return "";
    unsigned char opcode = data[0] & 0x0F;
    if (opcode != 0x01) return "";
    unsigned char mask = data[1] & 0x80;
    unsigned long long payloadLen = data[1] & 0x7F;
    size_t headerLen = 2;
    if (payloadLen == 126) {
        payloadLen = (unsigned char)data[2] << 8 | (unsigned char)data[3];
        headerLen = 4;
    } else if (payloadLen == 127) return "";
    unsigned char maskingKey[4];
    if (mask) {
        for (int i = 0; i < 4; i++) maskingKey[i] = data[headerLen + i];
        headerLen += 4;
    }
    std::string result;
    for (size_t i = 0; i < payloadLen; i++) {
        unsigned char c = data[headerLen + i];
        if (mask) c ^= maskingKey[i % 4];
        result += c;
    }
    return result;
}

std::string HttpServer::buildWebSocketFrame(const std::string& data) {
    std::string frame;
    frame += (char)0x81;
    size_t len = data.length();
    if (len <= 125) frame += (char)len;
    else if (len <= 65535) {
        frame += (char)126;
        frame += (char)((len >> 8) & 0xFF);
        frame += (char)(len & 0xFF);
    }
    frame += data;
    return frame;
}

// ========== 游戏管理器实例 ==========
class GameRoom {
public:
    std::string roomId;
    std::string gameType;
    std::vector<std::string> clients;
    std::map<std::string, std::string> playerNames;
    std::chrono::steady_clock::time_point created;
    GameRoom(const std::string& id, const std::string& type)
        : roomId(id), gameType(type), created(std::chrono::steady_clock::now()) {}
};

class GameRoomManager {
private:
    std::unordered_map<std::string, GameRoom> rooms;
    std::unordered_map<std::string, std::string> clientRoomMap;
    std::mutex mutex;
    
    void leaveRoomInternal(const std::string& clientId, const std::string& roomId) {
        auto it = rooms.find(roomId);
        if (it != rooms.end()) {
            auto& clients = it->second.clients;
            clients.erase(std::remove(clients.begin(), clients.end(), clientId), clients.end());
            it->second.playerNames.erase(clientId);
            if (clients.empty()) rooms.erase(it);
        }
        clientRoomMap.erase(clientId);
    }
    
public:
    void joinRoom(const std::string& clientId, const std::string& roomId, const std::string& gameType = "default", const std::string& playerName = "") {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = clientRoomMap.find(clientId);
        if (it != clientRoomMap.end()) leaveRoomInternal(clientId, it->second);
        auto roomIt = rooms.find(roomId);
        if (roomIt == rooms.end()) {
            rooms.emplace(roomId, GameRoom(roomId, gameType));
            roomIt = rooms.find(roomId);
        }
        roomIt->second.clients.push_back(clientId);
        if (!playerName.empty()) roomIt->second.playerNames[clientId] = playerName;
        clientRoomMap[clientId] = roomId;
    }
    
    void leaveRoom(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = clientRoomMap.find(clientId);
        if (it != clientRoomMap.end()) leaveRoomInternal(clientId, it->second);
    }
    
    std::vector<std::string> getClientsInRoom(const std::string& roomId) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = rooms.find(roomId);
        if (it == rooms.end()) return {};
        return it->second.clients;
    }
    
    std::string getClientRoom(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = clientRoomMap.find(clientId);
        if (it == clientRoomMap.end()) return "";
        return it->second;
    }
    
    GameRoom* getRoom(const std::string& roomId) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = rooms.find(roomId);
        if (it == rooms.end()) return nullptr;
        return &it->second;
    }
    
    void removeClient(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = clientRoomMap.find(clientId);
        if (it != clientRoomMap.end()) leaveRoomInternal(clientId, it->second);
    }
};

// ========== HttpServer 实现 ==========
HttpServer::HttpServer(int p) : port(p), serverFd(INVALID_SOCKET), running(false), wsInitialized(false) {
    
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) wsInitialized = true;
    
    dataStore = &DataStore::instance();
    // loadAll is already called once in main(); skip duplicate load (idempotent clear added)
    
    tokenManager = new TokenManager();
    llmHandler = new LLMHandler(dataStore, tokenManager);
    
    roomManager = new GameRoomManager();
    gameRegistry = new GameRegistry();
    gameRegistry->setBroadcastFn(
        [this](const std::string& roomId, const std::string& json, const std::string& senderId){
            if (senderId.empty()) this->broadcastToRoomIncludingSender(roomId, json);
            else this->broadcastToRoom(roomId, json, senderId);
        });
    gameRegistry->setSendFn(
        [this](const std::string& clientId, const std::string& json){
            this->sendToClient(clientId, json);
        });
    
    srand(time(nullptr));
}

HttpServer::~HttpServer() {
    stop();
    if (wsInitialized) WSACleanup();
    if (roomManager) delete roomManager;
    if (gameRegistry) delete gameRegistry;
    if (tokenManager) delete tokenManager;
    if (llmHandler) delete llmHandler;
}

bool HttpServer::start() {
    if (!wsInitialized) { std::cerr << "Winsock init failed" << std::endl; return false; }
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == INVALID_SOCKET) { std::cerr << "socket failed" << std::endl; return false; }
    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) { std::cerr << "setsockopt failed" << std::endl; return false; }
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(serverFd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { std::cerr << "bind failed" << std::endl; return false; }
    if (listen(serverFd, 100) == SOCKET_ERROR) { std::cerr << "listen failed" << std::endl; return false; }
    running = true;
    return true;
}

void HttpServer::setHandler(RequestHandler h) { handler = h; }
void HttpServer::setWebSocketHandler(WebSocketHandler h) { wsHandler = h; }

void HttpServer::sendToClient(const std::string& clientId, const std::string& message) {
    std::lock_guard<std::mutex> lock(clientMutex);
    auto it = clientSockets.find(clientId);
    if (it != clientSockets.end()) {
        std::string frame = buildWebSocketFrame(message);
        send(it->second, frame.c_str(), (int)frame.length(), 0);
    }
}

void HttpServer::broadcastToRoom(const std::string& roomId, const std::string& message, const std::string& senderId) {
    auto clients = roomManager->getClientsInRoom(roomId);
    std::string frame = buildWebSocketFrame(message);
    std::lock_guard<std::mutex> lock(clientMutex);
    for (const auto& cid : clients) {
        if (cid == senderId) continue;
        auto it = clientSockets.find(cid);
        if (it != clientSockets.end()) send(it->second, frame.c_str(), (int)frame.length(), 0);
    }
}

void HttpServer::broadcastToRoomIncludingSender(const std::string& roomId, const std::string& message) {
    auto clients = roomManager->getClientsInRoom(roomId);
    std::string frame = buildWebSocketFrame(message);
    std::lock_guard<std::mutex> lock(clientMutex);
    for (const auto& cid : clients) {
        auto it = clientSockets.find(cid);
        if (it != clientSockets.end()) send(it->second, frame.c_str(), (int)frame.length(), 0);
    }
}

std::vector<std::string> HttpServer::getClientsInRoom(const std::string& roomId) {
    return roomManager->getClientsInRoom(roomId);
}

std::string HttpServer::getClientRoom(const std::string& clientId) {
    return roomManager->getClientRoom(clientId);
}

// ========== 用户认证 ==========
bool HttpServer::authenticateUser(const std::string& username, const std::string& password, std::string& userId) {
    for (const auto& user : dataStore->users) {
        if (user.username == username && common::passwordVerify(password, user.password)) {
            userId = user.id;
            return true;
        }
    }
    return false;
}

bool HttpServer::isAdmin(const std::string& userId) {
    auto user = dataStore->findUserById(userId);
    return user && adminperm::any(user);
}

// ---------- LLM SSE / HTTP helpers ----------
static void llmSendRaw(SOCKET fd, const std::string& data) {
    send(fd, data.c_str(), (int)data.length(), 0);
}
static void llmSendSSE(SOCKET fd, const std::string& payload) {
    llmSendRaw(fd, "data: " + payload + "\n\n");
}
static void llmSendJson(SOCKET fd, int status, const std::string& json) {
    std::string st = (status == 200) ? "OK" : ((status == 401) ? "Unauthorized" : ((status == 403) ? "Forbidden" : "Error"));
    std::string resp = "HTTP/1.1 " + std::to_string(status) + " " + st + "\r\n";
    resp += "Content-Type: application/json; charset=utf-8\r\n";
    resp += "Content-Length: " + std::to_string(json.length()) + "\r\n";
    resp += "Connection: close\r\n\r\n" + json;
    llmSendRaw(fd, resp);
}
static void llmSendHtml(SOCKET fd, const std::string& html) {
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
                       + std::to_string(html.length()) + "\r\nConnection: close\r\n\r\n" + html;
    llmSendRaw(fd, resp);
}
// \u89e3\u6790\u6a21\u578b\u53c2\u6570\u91cf(\u5982 "qwen3:0.6b" -> 0.6)
static double llmModelB(const std::string& model) {
    std::string s = model;
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
    size_t p = s.find('b');
    if (p == std::string::npos) return 0.0;
    size_t q = p;
    while (q > 0 && (isdigit((unsigned char)s[q-1]) || s[q-1] == '.')) q--;
    if (q == p) return 0.0;
    std::string num = s.substr(q, p - q);
    if (num.empty()) return 0.0;
    return atof(num.c_str());
}
// \u6309\u6a21\u578b\u5927\u5c0f\u8ba1\u8d39\uff1a\u51e0 B \u7ffb\u51e0\u500d\uff0c\u4e0d\u8db3 1B \u6309 1 \u500d\uff0c\u6700\u4f4e 1
static long long llmCost(const std::string& model, long long total) {
    if (total < 0) total = 0;
    double factor = common::llmModelMultiplier(model);
    double cost = (double)total * factor;
    long long ci = (long long)ceil(cost);
    if (ci < 1) ci = 1;
    return ci;
}
// ================= DeepSeek 云端 AI（含触手 tool calling） =================
// 配置文件 data/ai.json：{"apiKey":"sk-...","baseUrl":"https://api.deepseek.com","model":"deepseek-chat"}
struct AiCfg {
    std::string apiKey;
    std::string baseUrl;
    std::string model;
    std::string providerId;
    std::string apiMode;   // chat（默认）| responses
};
static AiCfg aiCfg() {
    AiCfg c;
    c.apiKey  = ""; // 旧单提供方回退配置已停用：请在 /admin/ai 配置提供方（密钥不入库）
    c.baseUrl = "https://api.deepseek.com";
    c.model   = "deepseek-flash";
    std::string raw = common::readFile("./data/ai.json");
    if (!raw.empty()) {
        auto o = common::jsonParseObject(raw);
        if (o.count("apiKey") && !o["apiKey"].empty()) c.apiKey = common::trim(o["apiKey"]);
        if (o.count("baseUrl") && !o["baseUrl"].empty()) c.baseUrl = common::trim(o["baseUrl"]);
        if (o.count("model") && !o["model"].empty()) c.model = common::trim(o["model"]);
    }
    while (!c.baseUrl.empty() && c.baseUrl[c.baseUrl.size()-1] == '/') c.baseUrl.erase(c.baseUrl.size()-1);
    c.apiMode = "chat";
    return c;
}
// 按模型解析提供方：模型 -> 已启用的提供方 + Key（Key 绝不下发前端）
static AiCfg aiCfgForModel(const std::string& modelId, std::string* err) {
    AiCfg c;
    aiprov::Provider p;
    aiprov::ModelRef m;
    if (!modelId.empty() && aiprov::resolve(modelId, p, m)) {
        c.apiKey = p.apiKey;
        c.baseUrl = p.baseUrl;
        c.model = modelId;
        c.providerId = p.id;
        c.apiMode = (p.apiMode == "responses") ? "responses" : "chat";
        return c;
    }
    // 没有配置提供方时回退到旧配置（单提供方时代的数据）
    c = aiCfg();
    if (!modelId.empty()) c.model = modelId;
    if (err) *err = modelId.empty() ? "未配置可用模型" : ("模型未启用：" + modelId);
    return c;
}

// 思考强度 → 上游推理参数（目前只有 OpenRouter 支持 reasoning 字段）
static std::string aiReasoningJson(const AiCfg& cfg, int effort) {
    if (cfg.providerId != "openrouter") return "";
    // effort=0：不主动关闭上游推理（部分免费模型强制要求 reasoning，关掉会报错），
    // 只通过提示词和前端隐藏思考过程来控制。
    if (effort <= 0) return "";
    const char* e = (effort >= 3) ? "high" : (effort == 2 ? "medium" : "low");
    return std::string(",\"reasoning\":{\"effort\":\"") + e + "\"}";
}

static std::string aiCfgToJson() {
    AiCfg c = aiCfg();
    std::string key = c.apiKey;
    if (key.size() > 12) key = key.substr(0, 6) + "****" + key.substr(key.size()-4);
    std::map<std::string, std::string> m;
    m["apiKey"] = key;
    m["baseUrl"] = c.baseUrl;
    m["model"] = c.model;
    return common::jsonStringify(m);
}

// 从 JSON 文本里取字符串字段（容错、支持嵌套）
static std::string jsonPeekString(const std::string& src, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = src.find(needle);
    while (p != std::string::npos) {
        size_t c = src.find(':', p + needle.size());
        if (c == std::string::npos) return "";
        size_t q = c + 1;
        while (q < src.size() && (src[q] == ' ' || src[q] == '\t')) q++;
        if (q < src.size() && src[q] == '"') {
            size_t i = q + 1;
            std::string raw;
            bool esc = false;
            while (i < src.size()) {
                char ch = src[i];
                if (esc) { raw += '\\'; raw += ch; esc = false; i++; continue; }
                if (ch == '\\') { esc = true; i++; continue; }
                if (ch == '"') break;
                raw += ch; i++;
            }
            return common::jsonUnescape(raw);
        }
        p = src.find(needle, p + needle.size());
    }
    return "";
}
static long long jsonPeekInt(const std::string& src, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = src.find(needle);
    if (p == std::string::npos) return -1;
    size_t c = src.find(':', p + needle.size());
    if (c == std::string::npos) return -1;
    size_t q = c + 1;
    while (q < src.size() && !isdigit((unsigned char)src[q]) && src[q] != '-') q++;
    if (q >= src.size()) return -1;
    return (long long)atoll(src.c_str() + q);
}

// 取 JSON 中某个 key 对应的原始值文本（数组 / 对象 / 字符串 / 数字），用于原样搬运
static std::string jsonRawValue(const std::string& src, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = src.find(needle);
    if (p == std::string::npos) return "";
    size_t c = src.find(':', p + needle.size());
    if (c == std::string::npos) return "";
    size_t i = c + 1;
    while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) i++;
    if (i >= src.size()) return "";
    if (src[i] == '"') {
        size_t j = i + 1;
        while (j < src.size()) {
            if (src[j] == '\\') { j += 2; continue; }
            if (src[j] == '"') { j++; break; }
            j++;
        }
        return src.substr(i, j - i);
    }
    if (src[i] == '[' || src[i] == '{') {
        char open = src[i];
        char close = (open == '[') ? ']' : '}';
        int depth = 0;
        bool inStr = false, esc = false;
        size_t j = i;
        for (; j < src.size(); j++) {
            char ch = src[j];
            if (esc) { esc = false; continue; }
            if (inStr && ch == '\\') { esc = true; continue; }
            if (ch == '"') { inStr = !inStr; continue; }
            if (inStr) continue;
            if (ch == open) depth++;
            else if (ch == close) { depth--; if (depth == 0) { j++; break; } }
        }
        return src.substr(i, j - i);
    }
    size_t j = i;
    while (j < src.size() && src[j] != ',' && src[j] != '}' && src[j] != ']' &&
           !isspace((unsigned char)src[j])) j++;
    return src.substr(i, j - i);
}

// 把 chat 的 messages 转成 Responses 的 input 项
// （tool 结果 → function_call_output；assistant.tool_calls → function_call；其余原样）
static std::string aiMessagesToResponsesInput(const std::string& msgs) {
    if (msgs.empty() || msgs[0] != '[') return msgs.empty() ? "[]" : msgs;
    std::vector<std::string> items = common::jsonParseArray(msgs);
    std::string out = "[";
    bool first = true;
    for (size_t i = 0; i < items.size(); i++) {
        std::string role = jsonPeekString(items[i], "role");
        std::string one;
        if (role == "tool") {
            std::string cid = jsonPeekString(items[i], "tool_call_id");
            std::string content = jsonPeekString(items[i], "content");
            one = "{\"type\":\"function_call_output\",\"call_id\":" + common::jsonStringify(cid) +
                  ",\"output\":" + common::jsonStringify(content) + "}";
        } else if (role == "assistant") {
            std::string content = jsonPeekString(items[i], "content");
            std::string tcalls = jsonRawValue(items[i], "tool_calls");
            std::string parts;
            if (!content.empty()) {
                parts = "{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":" +
                        common::jsonStringify(content) + "}]}";
            }
            if (!tcalls.empty() && tcalls[0] == '[') {
                std::vector<std::string> calls = common::jsonParseArray(tcalls);
                for (size_t k = 0; k < calls.size(); k++) {
                    std::string fnRaw = jsonRawValue(calls[k], "function");
                    std::string name = jsonPeekString(calls[k], "name");
                    std::string args = jsonPeekString(calls[k], "arguments");
                    if (name.empty()) name = jsonPeekString(fnRaw, "name");
                    if (args.empty()) args = jsonPeekString(fnRaw, "arguments");
                    std::string cid = jsonPeekString(calls[k], "id");
                    if (!parts.empty()) parts += ",";
                    parts += "{\"type\":\"function_call\",\"call_id\":" + common::jsonStringify(cid) +
                             ",\"name\":" + common::jsonStringify(name) +
                             ",\"arguments\":" + common::jsonStringify(args) + "}";
                }
            }
            one = parts;
            if (one.empty()) continue;   // 空的 assistant 消息直接跳过
        } else {
            one = items[i];
        }
        if (!first) out += ",";
        first = false;
        out += one;
    }
    out += "]";
    return out;
}

// chat/completions 请求体 → responses 请求体（messages 直接作为 input）
static std::string aiChatBodyToResponses(const std::string& chatBody, bool stream) {
    std::string model = jsonPeekString(chatBody, "model");
    std::string input = aiMessagesToResponsesInput(jsonRawValue(chatBody, "messages"));
    if (input.empty()) input = "[]";
    std::string out = "{\"model\":" + common::jsonStringify(model) + ",\"input\":" + input +
                      ",\"stream\":" + std::string(stream ? "true" : "false");
    std::string maxTok = jsonRawValue(chatBody, "max_tokens");
    if (!maxTok.empty() && maxTok != "0") out += ",\"max_output_tokens\":" + maxTok;
    std::string temp = jsonRawValue(chatBody, "temperature");
    if (!temp.empty()) out += ",\"temperature\":" + temp;
    std::string topP = jsonRawValue(chatBody, "top_p");
    if (!topP.empty()) out += ",\"top_p\":" + topP;
    // tools：chat 的 {"type":"function","function":{...}} → responses 的 {"type":"function",...}
    std::string toolsRaw = jsonRawValue(chatBody, "tools");
    if (!toolsRaw.empty() && toolsRaw.size() > 2 && toolsRaw[0] == '[') {
        std::vector<std::string> items = common::jsonParseArray(toolsRaw);
        std::string arr;
        for (size_t i = 0; i < items.size(); i++) {
            std::string fn = jsonRawValue(items[i], "function");
            if (fn.size() < 2 || fn[0] != '{') continue;
            if (!arr.empty()) arr += ",";
            arr += "{\"type\":\"function\"," + fn.substr(1);
        }
        if (!arr.empty()) out += ",\"tools\":[" + arr + "]";
    }
    std::string tc = jsonRawValue(chatBody, "tool_choice");
    if (!tc.empty() && tc != "\"none\"") out += ",\"tool_choice\":" + tc;
    out += "}";
    return out;
}

// 取 JSON 中某个 key 最后一次出现的整数值（Responses 的 usage 顶层字段在 attribution 之后）
static long long jsonPeekIntLast(const std::string& src, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = src.rfind(needle);
    if (p == std::string::npos) return -1;
    size_t c = src.find(':', p + needle.size());
    if (c == std::string::npos) return -1;
    size_t q = c + 1;
    while (q < src.size() && !isdigit((unsigned char)src[q]) && src[q] != '-') q++;
    if (q >= src.size()) return -1;
    return (long long)atoll(src.c_str() + q);
}

// 从 pos 之后找 "\"key\":\"...\"" 并返回反转义后的字符串
static std::string jsonStringAfter(const std::string& src, size_t pos, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = src.find(needle, pos);
    if (p == std::string::npos) return "";
    size_t c = src.find(':', p + needle.size());
    if (c == std::string::npos) return "";
    size_t v = c + 1;
    while (v < src.size() && isspace((unsigned char)src[v])) v++;
    if (v >= src.size() || src[v] != '"') return "";
    size_t e = v + 1;
    while (e < src.size()) {
        if (src[e] == '\\') { e += 2; continue; }
        if (src[e] == '"') break;
        e++;
    }
    return common::jsonUnescape(src.substr(v + 1, e - v - 1));
}

// responses 响应体 → chat.completion 形状（让下游的 content / usage 解析继续可用）
static std::string aiResponsesToChatJson(const std::string& body) {
    std::string text;
    // 标准结构：output[] 里可能先有 reasoning 项，必须精确定位 message 的 output_text
    size_t ot = body.find("\"type\":\"output_text\"");
    if (ot == std::string::npos) ot = body.find("\"output_text\"");
    if (ot != std::string::npos) {
        size_t tq = body.rfind("\"text\"", ot);
        if (tq != std::string::npos) text = jsonStringAfter(body, tq, "text");
        if (text.empty()) text = jsonStringAfter(body, ot, "text");
    }
    if (text.empty()) {
        size_t op = body.find("\"output\"");
        if (op != std::string::npos) text = jsonStringAfter(body, op, "text");
    }
    // 工具调用：responses 的 function_call 项 → chat 的 tool_calls
    std::string calls;
    {
        std::string outRaw = jsonRawValue(body, "output");
        if (!outRaw.empty() && outRaw[0] == '[') {
            std::vector<std::string> items = common::jsonParseArray(outRaw);
            for (size_t i = 0; i < items.size(); i++) {
                if (jsonPeekString(items[i], "type") != "function_call") continue;
                std::string nm = jsonPeekString(items[i], "name");
                if (nm.empty()) continue;
                std::string args = jsonPeekString(items[i], "arguments");
                std::string cid = jsonPeekString(items[i], "call_id");
                if (cid.empty()) cid = jsonPeekString(items[i], "id");
                if (!calls.empty()) calls += ",";
                calls += "{\"id\":" + common::jsonStringify(cid) + ",\"type\":\"function\",\"function\":{\"name\":" +
                         common::jsonStringify(nm) + ",\"arguments\":" + common::jsonStringify(args) + "}}";
            }
        }
    }
    if (text.empty()) {
        // 少数实现直接给顶层 output_text 字符串
        size_t p = body.find("\"output_text\"");
        if (p != std::string::npos) {
            size_t c = body.find(':', p + 13);
            if (c != std::string::npos) {
                size_t v = c + 1;
                while (v < body.size() && isspace((unsigned char)body[v])) v++;
                if (v < body.size() && body[v] == '"') {
                    size_t e = v + 1;
                    while (e < body.size()) {
                        if (body[e] == '\\') { e += 2; continue; }
                        if (body[e] == '"') break;
                        e++;
                    }
                    text = common::jsonUnescape(body.substr(v + 1, e - v - 1));
                }
            }
        }
    }
    if (text.empty()) text = jsonPeekString(body, "content");
    long long pt = jsonPeekIntLast(body, "input_tokens");
    if (pt < 0) pt = jsonPeekIntLast(body, "prompt_tokens");
    long long ct = jsonPeekIntLast(body, "output_tokens");
    if (ct < 0) ct = jsonPeekIntLast(body, "completion_tokens");
    if (pt < 0) pt = 0;
    if (ct < 0) ct = 0;
    return "{\"object\":\"chat.completion\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":" +
           common::jsonStringify(text) + (calls.empty() ? std::string() : (",\"tool_calls\":[" + calls + "]")) +
           "},\"finish_reason\":" + (calls.empty() ? "\"stop\"" : "\"tool_calls\"") + "}],\"usage\":{\"prompt_tokens\":" +
           std::to_string(pt) + ",\"completion_tokens\":" + std::to_string(ct) +
           ",\"total_tokens\":" + std::to_string(pt + ct) + "}}";
}

// Responses 流式（SSE）→ chat.completion 形状
// 转成流式请求是关键：非流式时 Cloudflare 等中转要等上游把整段想完才回首个字节，长任务会 524。
static std::string aiResponsesSseToChatJson(const std::string& raw) {
    if (raw.find("data:") == std::string::npos) return "";
    struct FCall { std::string id, callId, name, args; };
    std::vector<FCall> calls;
    std::string text, errMsg;
    long long pt = 0, ct = 0;
    std::vector<std::string> lines = common::split(raw, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = common::trim(lines[i]);
        if (line.compare(0, 5, "data:") != 0) continue;
        std::string payload = common::trim(line.substr(5));
        if (payload.empty() || payload == "[DONE]") continue;
        std::string et = jsonPeekString(payload, "type");
        if (et == "response.output_text.delta") {
            text += jsonPeekString(payload, "delta");
        } else if (et == "error" || et == "response.failed") {
            errMsg = jsonPeekString(payload, "message");
            if (errMsg.empty()) errMsg = payload.substr(0, 200);
        } else if (et == "response.output_item.added") {
            std::string item = jsonRawValue(payload, "item");
            if (jsonPeekString(item, "type") == "function_call") {
                FCall c;
                c.id = jsonPeekString(item, "id");
                c.callId = jsonPeekString(item, "call_id");
                if (c.callId.empty()) c.callId = c.id;
                c.name = jsonPeekString(item, "name");
                calls.push_back(c);
            }
        } else if (et == "response.function_call_arguments.delta") {
            std::string iid = jsonPeekString(payload, "item_id");
            std::string d = jsonPeekString(payload, "delta");
            for (size_t k = 0; k < calls.size(); k++)
                if (calls[k].id == iid || calls[k].callId == iid) { calls[k].args += d; break; }
        } else if (et == "response.function_call_arguments.done") {
            std::string iid = jsonPeekString(payload, "item_id");
            std::string full = jsonPeekString(payload, "arguments");
            for (size_t k = 0; k < calls.size(); k++)
                if (calls[k].id == iid || calls[k].callId == iid) {
                    if (!full.empty()) calls[k].args = full;
                    break;
                }
        } else if (et == "response.completed") {
            long long a = jsonPeekIntLast(payload, "input_tokens");
            long long b = jsonPeekIntLast(payload, "output_tokens");
            if (a >= 0) pt = a;
            if (b >= 0) ct = b;
        }
    }
    if (text.empty() && calls.empty() && errMsg.empty()) return "";
    if (text.empty() && calls.empty() && !errMsg.empty()) text = "⚠ 上游返回错误：" + errMsg;
    std::string callJson;
    for (size_t i = 0; i < calls.size(); i++) {
        if (calls[i].name.empty()) continue;
        if (!callJson.empty()) callJson += ",";
        callJson += "{\"id\":" + common::jsonStringify(calls[i].callId) +
                    ",\"type\":\"function\",\"function\":{\"name\":" + common::jsonStringify(calls[i].name) +
                    ",\"arguments\":" + common::jsonStringify(calls[i].args) + "}}";
    }
    return "{\"object\":\"chat.completion\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":" +
           common::jsonStringify(text) + (callJson.empty() ? std::string() : (",\"tool_calls\":[" + callJson + "]")) +
           "},\"finish_reason\":" + (callJson.empty() ? "\"stop\"" : "\"tool_calls\"") +
           "}],\"usage\":{\"prompt_tokens\":" + std::to_string(pt) + ",\"completion_tokens\":" +
           std::to_string(ct) + ",\"total_tokens\":" + std::to_string(pt + ct) + "}}";
}

// ---------- 底层 HTTP：GET/POST {baseUrl}/xxx ----------
struct AiHttp {
    bool ok = false;
    long long status = 0;
    std::string body;
    std::string err;
};
static AiHttp aiRequest(const AiCfg& cfg, const wchar_t* verb, const std::string& apiPathIn,
                        const std::string& body) {
    AiHttp r;
    std::string url = cfg.baseUrl;
    bool https = true;
    if (url.compare(0, 8, "https://") == 0) { https = true; url = url.substr(8); }
    else if (url.compare(0, 7, "http://") == 0) { https = false; url = url.substr(7); }
    std::string host = url, basePath;
    size_t sl = url.find('/');
    if (sl != std::string::npos) { host = url.substr(0, sl); basePath = url.substr(sl); }
    INTERNET_PORT port = https ? INTERNET_PORT(443) : INTERNET_PORT(80);
    size_t colon = host.find(':');
    if (colon != std::string::npos) { port = (INTERNET_PORT)atoi(host.c_str() + colon + 1); host = host.substr(0, colon); }
    std::string apiPath = basePath + apiPathIn;
    std::wstring whost(host.begin(), host.end());
    std::wstring wpath(apiPath.begin(), apiPath.end());

    HINTERNET hSession = WinHttpOpen(L"OIStation/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { r.err = "WinHttp 初始化失败"; return r; }
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); r.err = "无法连接 AI 服务"; return r; }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, verb, wpath.c_str(), NULL,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); r.err = "请求创建失败"; return r; }
    DWORD timeout = 60000;
    WinHttpSetOption(hReq, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    std::wstring hdrs = L"Content-Type: application/json\r\nAuthorization: Bearer ";
    hdrs += std::wstring(cfg.apiKey.begin(), cfg.apiKey.end());
    hdrs += L"\r\nHTTP-Referer: http://localhost\r\nX-Title: OIstation\r\nAccept: application/json\r\n";
    bool sent = false;
    if (body.empty())
        sent = WinHttpSendRequest(hReq, hdrs.c_str(), (DWORD)hdrs.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
               && WinHttpReceiveResponse(hReq, NULL);
    else
        sent = WinHttpSendRequest(hReq, hdrs.c_str(), (DWORD)hdrs.length(),
                                  (LPVOID)body.c_str(), (DWORD)body.length(), (DWORD)body.length(), 0)
               && WinHttpReceiveResponse(hReq, NULL);
    DWORD sc = 0, sz = sizeof(sc);
    if (sent && WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sz, WINHTTP_NO_HEADER_INDEX))
        r.status = sc;
    if (sent) {
        char buf[65536]; DWORD rd = 0;
        while (WinHttpReadData(hReq, buf, sizeof(buf) - 1, &rd) && rd > 0) {
            buf[rd] = '\0';
            r.body += buf;
            rd = 0;
        }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    if (!sent) { r.err = "AI 服务无响应"; return r; }
    if (r.status != 200) {
        std::string msg = jsonPeekString(r.body, "message");
        r.err = msg.empty() ? ("AI 服务返回 " + std::to_string(r.status) + "：" + r.body.substr(0, 300)) : msg;
        return r;
    }
    r.ok = true;
    return r;
}
static AiHttp aiPostChat(const AiCfg& cfg, const std::string& body) {
    if (cfg.apiMode != "responses") return aiRequest(cfg, L"POST", "/chat/completions", body);
    // Responses API 提供方：把 chat 请求体改写成 responses 请求，再把结果还原成 chat 结构
    // 用流式请求：首字节即可返回，避免长任务被 Cloudflare 判 524
    AiHttp r = aiRequest(cfg, L"POST", "/responses", aiChatBodyToResponses(body, true));
    if (r.ok) {
        std::string conv = aiResponsesSseToChatJson(r.body);
        if (conv.empty()) conv = aiResponsesToChatJson(r.body);   // 有些中转忽略 stream，直接回 JSON
        if (!conv.empty()) r.body = conv;
    }
    return r;
}

// ---------- 扫描提供方可用模型（GET /models） ----------
// 取出 JSON 中 "data":[...] 的数组文本
static std::string aiSliceDataArray(const std::string& body) {
    size_t p = body.find("\"data\"");
    if (p == std::string::npos) return "";
    size_t a = body.find('[', p);
    if (a == std::string::npos) return "";
    int depth = 0;
    bool inStr = false, esc = false;
    for (size_t i = a; i < body.size(); i++) {
        char ch = body[i];
        if (esc) { esc = false; continue; }
        if (ch == '\\') { esc = true; continue; }
        if (ch == '"') { inStr = !inStr; continue; }
        if (inStr) continue;
        if (ch == '[') depth++;
        else if (ch == ']') {
            depth--;
            if (depth == 0) return body.substr(a, i - a + 1);
        }
    }
    return "";
}

static std::string aiCacheFileOf(const std::string& providerId) {
    std::string safe;
    for (size_t i = 0; i < providerId.size(); i++) {
        char c = providerId[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '-') safe += c;
        else safe += '_';
    }
    return "./data/ai_models_cache_" + safe + ".json";
}

static bool aiIsFreeModel(const std::string& id, const std::string& promptPrice) {
    std::string low = id;
    for (size_t i = 0; i < low.size(); i++) low[i] = (char)tolower((unsigned char)low[i]);
    if (low.size() > 5 && low.substr(low.size() - 5) == ":free") return true;
    std::string p = common::trim(promptPrice);
    if (!p.empty() && (p == "0" || p == "0.0" || p == "0.00" || p == "0.000000")) return true;
    return false;
}

// 扫描单个提供方；freeOnly 的提供方只返回免费模型
static std::vector<AiScannedModel> aiScanProviderModels(const aiprov::Provider& p, bool refresh, std::string* errOut) {
    std::string cacheFile = aiCacheFileOf(p.id);
    long long now = (long long)time(NULL);
    if (!refresh) {
        std::string raw = common::readFile(cacheFile);
        if (!raw.empty()) {
            auto o = common::jsonParseObject(raw);
            long long ts = o.count("ts") ? common::stringToLongLong(o["ts"]) : 0;
            if (now - ts < 600 && o.count("list")) {
                std::vector<AiScannedModel> out;
                auto items = common::split(o["list"], ';');
                for (size_t i = 0; i < items.size(); i++) {
                    auto kv = common::split(items[i], '|');
                    if (kv.empty() || kv[0].empty()) continue;
                    AiScannedModel m;
                    m.id = kv[0];
                    m.name = kv.size() > 1 ? kv[1] : kv[0];
                    m.providerId = p.id;
                    m.freeModel = kv.size() > 4 ? (kv[4] == "1") : false;
                    m.promptPrice = kv.size() > 2 ? atof(kv[2].c_str()) : -1;
                    m.completionPrice = kv.size() > 3 ? atof(kv[3].c_str()) : -1;
                    out.push_back(m);
                }
                if (!out.empty()) return out;
            }
        }
    }
    std::vector<AiScannedModel> out;
    AiCfg cfg;
    cfg.apiKey = p.apiKey;
    cfg.baseUrl = p.baseUrl;
    cfg.model = "";
    cfg.providerId = p.id;
    AiHttp r = aiRequest(cfg, L"GET", "/models", "");
    if (!r.ok) {
        if (errOut) *errOut = (p.name.empty() ? p.id : p.name) + "：" + r.err;
        return out;
    }
    std::string arr = aiSliceDataArray(r.body);
    if (arr.empty()) {
        // 有些兼容服务直接返回数组
        if (!r.body.empty() && r.body.find('[') != std::string::npos) arr = r.body;
    }
    if (arr.empty()) { if (errOut) *errOut = "模型列表格式无法识别"; return out; }
    auto items = common::jsonParseArray(arr);
    for (size_t i = 0; i < items.size(); i++) {
        std::string id = jsonPeekString(items[i], "id");
        if (id.empty()) continue;
        std::string name = jsonPeekString(items[i], "name");
        if (name.empty()) name = id;
        std::string pp = jsonPeekString(items[i], "prompt");
        std::string cp = jsonPeekString(items[i], "completion");
        AiScannedModel m;
        m.id = id;
        m.name = name;
        m.providerId = p.id;
        m.freeModel = aiIsFreeModel(id, pp);
        m.promptPrice = pp.empty() ? -1 : atof(pp.c_str());
        m.completionPrice = cp.empty() ? -1 : atof(cp.c_str());
        if (p.freeOnly && !m.freeModel) continue;
        out.push_back(m);
    }
    // 写缓存
    std::map<std::string, std::string> o;
    o["ts"] = std::to_string(now);
    std::string list;
    for (size_t i = 0; i < out.size(); i++) {
        if (i) list += ";";
        std::string nm = common::replaceAll(out[i].name, "|", "/");
        std::string pp = out[i].promptPrice < 0 ? std::string("-1") : std::to_string(out[i].promptPrice);
        std::string cp = out[i].completionPrice < 0 ? std::string("-1") : std::to_string(out[i].completionPrice);
        list += out[i].id + "|" + nm + "|" + pp + "|" + cp + "|" + (out[i].freeModel ? "1" : "0");
    }
    o["list"] = list;
    common::writeFile(cacheFile, common::jsonStringify(o));
    return out;
}

std::vector<AiScannedModel> aiScanProvider(const std::string& providerId, bool refresh, std::string* err) {
    aiprov::Provider p;
    if (!aiprov::find(providerId, p)) {
        if (err) *err = "提供方不存在";
        return std::vector<AiScannedModel>();
    }
    return aiScanProviderModels(p, refresh, err);
}

// 供 handlers.cpp（管理页）调用的公开包装：扫描所有启用的提供方
std::vector<AiModelItem> aiScanModelsPublic(bool refresh, std::string* err) {
    std::vector<AiModelItem> out;
    std::vector<aiprov::Provider> all = aiprov::load();
    std::string lastErr;
    for (size_t i = 0; i < all.size(); i++) {
        if (!all[i].enabled) continue;
        std::string e;
        std::vector<AiScannedModel> ms = aiScanProviderModels(all[i], refresh, &e);
        if (!e.empty()) lastErr = e;
        for (size_t k = 0; k < ms.size(); k++) {
            AiModelItem it;
            it.id = ms[k].id;
            it.name = ms[k].name;
            out.push_back(it);
        }
    }
    if (out.empty() && err) *err = lastErr;
    return out;
}

// 中转自检：真发一次非流式对话，返回延迟与 token 用量
AiTestResult aiTestChat(const std::string& modelId, const std::string& prompt) {
    AiTestResult out;
    out.model = modelId;
    aiprov::Provider p;
    aiprov::ModelRef m;
    if (!aiprov::resolve(modelId, p, m)) {
        out.error = "模型未启用：" + modelId;
        return out;
    }
    out.providerName = p.name;
    if (p.apiKey.empty()) { out.error = "提供方「" + p.name + "」未配置 API Key"; return out; }
    AiCfg cfg;
    cfg.apiKey = p.apiKey;
    cfg.baseUrl = p.baseUrl;
    cfg.model = modelId;
    cfg.providerId = p.id;
    cfg.apiMode = (p.apiMode == "responses") ? "responses" : "chat";
    std::string msg = prompt.empty() ? std::string("只回一句话：中转正常") : prompt;
    if (msg.size() > 2000) msg = msg.substr(0, 2000);
    std::string body = "{\"model\":" + common::jsonStringify(modelId) +
                       ",\"messages\":[{\"role\":\"user\",\"content\":" + common::jsonStringify(msg) +
                       "}],\"max_tokens\":256,\"stream\":false}";
    DWORD t0 = GetTickCount();
    AiHttp r = aiPostChat(cfg, body);
    out.latencyMs = (int)(GetTickCount() - t0);
    if (!r.ok) { out.error = r.err; return out; }
    out.text = jsonPeekString(r.body, "content");
    out.promptTokens = jsonPeekInt(r.body, "prompt_tokens");
    out.completionTokens = jsonPeekInt(r.body, "completion_tokens");
    if (out.promptTokens < 0) out.promptTokens = 0;
    if (out.completionTokens < 0) out.completionTokens = 0;
    if (out.text.empty()) {
        // 部分模型返回 reasoning 字段而非 content
        out.text = jsonPeekString(r.body, "reasoning");
    }
    if (out.text.empty()) { out.error = "模型返回为空"; return out; }
    out.ok = true;
    return out;
}

// 管理员辅助：用站内已启用的模型做一次较长文本生成
std::string aiAssistText(const std::string& prompt, int maxTokens, std::string* err,
                         const std::string& modelId, int effort, std::string* usedModel) {
    std::vector<aiprov::ModelEntry> ents = aiprov::entries();
    if (ents.empty()) { if (err) *err = "还没有已启用的模型，请先在 AI 管理里扫描并勾选"; return ""; }
    std::string model = modelId;
    bool found = false;
    if (!model.empty())
        for (size_t i = 0; i < ents.size(); i++) if (ents[i].id == model) found = true;
    if (!found) {
        model = aiplan::testModelId();
        found = false;
        if (!model.empty())
            for (size_t i = 0; i < ents.size(); i++) if (ents[i].id == model) found = true;
        if (!found) model = ents[0].id;
    }
    if (usedModel) *usedModel = model;
    aiprov::Provider p;
    aiprov::ModelRef m;
    if (!aiprov::resolve(model, p, m)) { if (err) *err = "模型不可用：" + model; return ""; }
    if (p.apiKey.empty()) { if (err) *err = "提供方「" + p.name + "」没有配置 API Key"; return ""; }
    AiCfg cfg;
    cfg.apiKey = p.apiKey;
    cfg.baseUrl = p.baseUrl;
    cfg.model = model;
    cfg.providerId = p.id;
    cfg.apiMode = (p.apiMode == "responses") ? "responses" : "chat";
    if (maxTokens < 256) maxTokens = 256;
    if (maxTokens > 8192) maxTokens = 8192;
    if (effort < 0) effort = 0;
    if (effort > 3) effort = 3;
    std::string body = "{\"model\":" + common::jsonStringify(model) +
                       ",\"messages\":[{\"role\":\"user\",\"content\":" + common::jsonStringify(prompt) +
                       "}],\"max_tokens\":" + std::to_string(maxTokens) + ",\"temperature\":0.2,\"stream\":false" +
                       aiReasoningJson(cfg, effort) + "}";
    AiHttp r = aiPostChat(cfg, body);
    if (!r.ok) { if (err) *err = r.err; return ""; }
    std::string text = jsonPeekString(r.body, "content");
    if (text.empty()) text = jsonPeekString(r.body, "reasoning");
    if (text.empty()) { if (err) *err = "模型返回为空"; return ""; }
    return text;
}

// 某个用户可见的模型（提供方已启用 + 已挑选 + 套餐允许）
static std::vector<aiprov::ModelEntry> aiModelsForUser(const User* u) {
    std::vector<aiprov::ModelEntry> all = aiprov::entries();
    std::vector<aiprov::ModelEntry> out;
    for (size_t i = 0; i < all.size(); i++) {
        if (u && !aiplan::userAllowedModel(*u, all[i].id)) continue;
        out.push_back(all[i]);
    }
    return out;
}


// ---------- 解析 tool_calls ----------
struct AiToolCall {
    std::string id;
    std::string name;
    std::string args;
};
static std::vector<AiToolCall> aiParseToolCalls(const std::string& json) {
    std::vector<AiToolCall> out;
    size_t p = json.find("\"tool_calls\"");
    if (p == std::string::npos) return out;
    size_t arr = json.find('[', p);
    if (arr == std::string::npos) return out;
    int depth = 0;
    bool inStr = false, esc = false;
    std::string cur;
    for (size_t i = arr + 1; i < json.size(); i++) {
        char ch = json[i];
        if (esc) { cur += ch; esc = false; continue; }
        if (ch == '\\') { cur += ch; esc = true; continue; }
        if (ch == '"') { inStr = !inStr; cur += ch; continue; }
        if (inStr) { cur += ch; continue; }
        if (ch == '{') { depth++; cur += ch; continue; }
        if (ch == '}') {
            depth--; cur += ch;
            if (depth == 0) {
                AiToolCall tc;
                tc.id = jsonPeekString(cur, "id");
                tc.name = jsonPeekString(cur, "name");
                tc.args = jsonPeekString(cur, "arguments");
                if (!tc.name.empty()) out.push_back(tc);
                cur.clear();
            }
            continue;
        }
        if (ch == ']' && depth == 0) break;
        cur += ch;          // 关键：冒号、数字、逗号等也要保留，否则取不到字段
    }
    return out;
}

// ---------- 工具轮：非流式调用 ----------
static bool aiToolRound(const AiCfg& cfg, const std::string& model, const std::string& messagesJson,
                        std::string& content, std::vector<AiToolCall>& calls,
                        long long& ptok, long long& ctok, std::string& err, int effort,
                        std::string& reasoningOut, bool& truncated) {
    // 注意：思考（reasoning）与正文共用 max_tokens。以前这里是 1200，
    // 模型思考长一点就会把正文挤断，表现为「回答莫名其妙中断」。
    std::string body = "{\"model\":\"" + common::jsonEscape(model) + "\",\"messages\":" + messagesJson +
                       ",\"stream\":false,\"max_tokens\":4096,\"temperature\":0.3,"
                       "\"tools\":" + aitools::specJson() + ",\"tool_choice\":\"auto\"" +
                       aiReasoningJson(cfg, effort) + "}";
    AiHttp r = aiPostChat(cfg, body);
    if (!r.ok) { err = r.err; return false; }
    content = jsonPeekString(r.body, "content");
    reasoningOut = jsonPeekString(r.body, "reasoning_content");
    if (reasoningOut.empty()) reasoningOut = jsonPeekString(r.body, "reasoning");
    std::string fin = jsonPeekString(r.body, "finish_reason");
    truncated = (fin == "length" || fin == "max_tokens");
    calls = aiParseToolCalls(r.body);
    long long pt = jsonPeekInt(r.body, "prompt_tokens");
    long long ct = jsonPeekInt(r.body, "completion_tokens");
    if (pt > 0) ptok += pt;
    if (ct > 0) ctok += ct;
    return true;
}

// ---------- 最终流式输出 ----------
static bool llmStreamChat(SOCKET fd, DataStore* ds, const std::string& userId,
                          const std::string& model, const std::string& messagesJson,
                          long long limitPoints, long long extraTok, const std::string& task,
                          std::string* answerOut, int effort) {
    AiCfg cfg = aiCfgForModel(model, NULL);
    if (cfg.apiKey.empty()) {
        llmSendSSE(fd, "{\"error\":\"未配置 DeepSeek API Key\"}");
        return false;
    }
    double _price = aiquota::priceOf(model);
    if (_price <= 0) _price = 1.0;
    long long _maxTok = (long long)((double)limitPoints * 1000.0 / _price);
    if (_maxTok > 8192) _maxTok = 8192;
    if (_maxTok < 32) {
        llmSendSSE(fd, "{\"error\":\"额度不足，无法发起对话\",\"code\":\"QUOTA_EXCEEDED\"}");
        return false;
    }
    long long _promptEst = (long long)(messagesJson.size() / 2);
    long long _promptCost = aiquota::costOf(model, _promptEst + extraTok);
    long long _outBudget = limitPoints - _promptCost;
    if (_outBudget < 8) _outBudget = 8;
    long long _capByQuota = (long long)((double)_outBudget * 1000.0 / _price);
    if (_maxTok > _capByQuota) _maxTok = _capByQuota;
    // 不要因为额度估算把输出挤成几十个字（以前的下限是 16，截断后用户什么都看不出来）
    if (_maxTok < 1024) _maxTok = 1024;

    bool _useResp = (cfg.apiMode == "responses");
    std::string body;
    if (_useResp) {
        body = "{\"model\":\"" + common::jsonEscape(model) + "\",\"stream\":true,"
               "\"max_output_tokens\":" + std::to_string(_maxTok) + ",\"input\":" + messagesJson + "}";
    } else {
        body = "{\"model\":\"" + common::jsonEscape(model) + "\",\"stream\":true,"
               "\"max_tokens\":" + std::to_string(_maxTok) + ","
               "\"stream_options\":{\"include_usage\":true},\"messages\":" + messagesJson +
               aiReasoningJson(cfg, effort) + "}";
    }

    // 拆分 baseUrl 以走流式（复用 aiPostChat 的地址解析规则）
    std::string url = cfg.baseUrl;
    bool https = true;
    if (url.compare(0, 8, "https://") == 0) { https = true; url = url.substr(8); }
    else if (url.compare(0, 7, "http://") == 0) { https = false; url = url.substr(7); }
    std::string host = url, basePath;
    size_t sl = url.find('/');
    if (sl != std::string::npos) { host = url.substr(0, sl); basePath = url.substr(sl); }
    INTERNET_PORT port = https ? INTERNET_PORT(443) : INTERNET_PORT(80);
    size_t colon = host.find(':');
    if (colon != std::string::npos) { port = (INTERNET_PORT)atoi(host.c_str() + colon + 1); host = host.substr(0, colon); }
    std::string apiPath = basePath + (_useResp ? "/responses" : "/chat/completions");
    std::wstring whost(host.begin(), host.end());
    std::wstring wpath(apiPath.begin(), apiPath.end());

    HINTERNET hSession = NULL, hConnect = NULL, hReq = NULL;
    long long statusCode = 0;
    std::string errBody;
    std::wstring hdrs = L"Content-Type: application/json\r\nAuthorization: Bearer ";
    hdrs += std::wstring(cfg.apiKey.begin(), cfg.apiKey.end());
    hdrs += L"\r\nHTTP-Referer: http://localhost\r\nX-Title: OIstation\r\nAccept: text/event-stream\r\n";
    for (int attempt = 0; attempt < 2; attempt++) {
        hSession = WinHttpOpen(L"OIStation/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) { llmSendSSE(fd, "{\"error\":\"WinHttp 初始化失败\"}"); return false; }
        hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); hSession = NULL; llmSendSSE(fd, "{\"error\":\"无法连接 AI 服务\"}"); return false; }
        DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
        hReq = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(), NULL,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hReq) {
            WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); hConnect = NULL; hSession = NULL;
            llmSendSSE(fd, "{\"error\":\"请求创建失败\"}"); return false;
        }
        DWORD timeout = 180000;
        WinHttpSetOption(hReq, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        bool sent = WinHttpSendRequest(hReq, hdrs.c_str(), (DWORD)hdrs.length(),
                                       (LPVOID)body.c_str(), (DWORD)body.length(), (DWORD)body.length(), 0)
                    && WinHttpReceiveResponse(hReq, NULL);
        statusCode = 0;
        if (sent) {
            DWORD sc = 0, sz = sizeof(sc);
            if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sz, WINHTTP_NO_HEADER_INDEX))
                statusCode = sc;
        }
        if (sent && statusCode == 200) break;
        errBody.clear();
        if (sent) {
            char eb[2048]; DWORD rd = 0;
            while (WinHttpReadData(hReq, eb, sizeof(eb) - 1, &rd) && rd > 0) { eb[rd] = 0; errBody += eb; rd = 0; }
        }
        WinHttpCloseHandle(hReq); hReq = NULL;
        WinHttpCloseHandle(hConnect); hConnect = NULL;
        WinHttpCloseHandle(hSession); hSession = NULL;
        // 免费模型偶发 5xx / 连接抖动：自动重试一次，避免用户看到「回答突然没了」
        if (attempt == 0 && (statusCode == 0 || statusCode >= 500)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            continue;
        }
        std::string msg = jsonPeekString(errBody, "message");
        if (msg.empty()) msg = errBody.empty() ? ("AI 服务无响应 (状态码 " + std::to_string(statusCode) + ")") : errBody.substr(0, 300);
        std::map<std::string, std::string> em;
        em["error"] = msg;
        llmSendSSE(fd, common::jsonStringify(em));
        return false;
    }

    bool gotAny = false;
    bool _quotaHit = false;
    bool _lenHit = false;
    std::string _collected;
    long long _outChars = 0;
    long long prompt_tok = 0, comp_tok = 0;
    char buf[65536]; DWORD rd = 0;
    std::string acc;
    while (WinHttpReadData(hReq, buf, sizeof(buf) - 1, &rd) && rd > 0) {
        buf[rd] = '\0';
        acc += buf;
        rd = 0;
        size_t nl;
        while ((nl = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, nl);
            acc.erase(0, nl + 1);
            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
            if (line.compare(0, 5, "data:") != 0) continue;
            std::string payload = common::trim(line.substr(5));
            if (payload.empty()) continue;
            if (payload == "[DONE]") { acc.clear(); break; }
            std::string content = jsonPeekString(payload, "content");
            std::string thinking = jsonPeekString(payload, "reasoning_content");
            if (thinking.empty()) thinking = jsonPeekString(payload, "reasoning");
            if (_useResp) {
                // Responses 流式：只取文本增量和推理增量事件
                std::string evType = jsonPeekString(payload, "type");
                if (content.empty() && evType == "response.output_text.delta")
                    content = jsonPeekString(payload, "delta");
                if (thinking.empty() && (evType == "response.reasoning_text.delta" ||
                                         evType == "response.reasoning_summary_text.delta"))
                    thinking = jsonPeekString(payload, "delta");
                if (evType == "response.completed") {
                    long long _pt = jsonPeekIntLast(payload, "input_tokens");
                    long long _ct = jsonPeekIntLast(payload, "output_tokens");
                    if (_pt >= 0) prompt_tok = _pt;
                    if (_ct >= 0) comp_tok = _ct;
                }
            }
            std::string fin = jsonPeekString(payload, "finish_reason");
            if (fin == "length" || fin == "max_tokens") _lenHit = true;
            // 只有真正的错误事件才当错误：Responses 的 completed 里带有 "error":null 与 "type":"message"，
            // 以前会把消息 id 误判成错误内容。
            bool isErrEvent = (payload.find("\"error\":{") != std::string::npos) ||
                              (payload.find("\"type\":\"error\"") != std::string::npos) ||
                              (payload.find("\"type\":\"response.failed\"") != std::string::npos);
            std::string errMsg = isErrEvent ? jsonPeekString(payload, "message") : "";
            if (content.empty() && thinking.empty()) {
                if (!errMsg.empty()) {
                    std::map<std::string, std::string> em; em["error"] = errMsg;
                    llmSendSSE(fd, common::jsonStringify(em));
                    break;
                }
                long long pt = jsonPeekInt(payload, "prompt_tokens");
                long long ct = jsonPeekInt(payload, "completion_tokens");
                if (pt >= 0) prompt_tok = pt;
                if (ct >= 0) comp_tok = ct;
                continue;
            }
            gotAny = true;
            _collected += content;
            _outChars += (long long)(content.size() + thinking.size());
            {
                long long estTok = _promptEst + extraTok + (long long)((double)_outChars / 1.6);
                long long used = aiquota::costOf(model, estTok);
                if (used >= limitPoints) {
                    llmSendSSE(fd, "{\"error\":\"额度已用完，输出已停止（5 小时 / 本月额度已达上限）\",\"code\":\"QUOTA_EXCEEDED\"}");
                    _quotaHit = true;
                    break;
                }
            }
            std::string ev = "{\"message\":{";
            bool first = true;
            if (!content.empty()) { ev += "\"content\":" + common::jsonStringify(content); first = false; }
            if (!thinking.empty()) { if (!first) ev += ","; ev += "\"thinking\":" + common::jsonStringify(thinking); }
            ev += "}}";
            llmSendSSE(fd, ev);
        }
        if (_quotaHit) break;
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    if (_lenHit) {
        // 真正被上游 max_tokens 截断时明确告知，而不是默默断掉
        llmSendSSE(fd, "{\"type\":\"truncated\",\"text\":\"已达到单次输出上限，可以点「继续」接着生成。\"}");
    }
    if (!gotAny) {
        llmSendSSE(fd, "{\"error\":\"AI 未返回内容（请检查 API Key 与网络）\"}");
        return false;
    }
    long long total = prompt_tok + comp_tok + extraTok;
    if (total <= 0) total = _promptEst + extraTok + (long long)((double)_outChars / 1.6) + 8;
    long long costi = aiquota::costOf(model, total);
    if (costi > limitPoints) costi = limitPoints;
    if (costi < 0) costi = 0;
    User* uu = ds->findUserById(userId);
    long long uw = uu ? uu->aiWindowQuota : 0;
    long long um = uu ? uu->aiMonthQuota : 0;
    aiquota::Info after = aiquota::record(userId, costi, uw, um);
    aitools::logUsage(userId, model, task, prompt_tok + extraTok, comp_tok, costi);
    std::map<std::string, std::string> meta;
    meta["type"] = "meta";
    meta["cost"] = std::to_string(costi);
    meta["balance"] = std::to_string(aiquota::remaining(after));
    meta["prompt_tokens"] = std::to_string(prompt_tok + extraTok);
    meta["completion_tokens"] = std::to_string(comp_tok);
    meta["windowQuota"] = std::to_string(after.windowQuota);
    meta["windowUsed"] = std::to_string(after.windowUsed);
    meta["monthQuota"] = std::to_string(after.monthQuota);
    meta["monthUsed"] = std::to_string(after.monthUsed);
    meta["extra"] = std::to_string(after.extra);
    if (_quotaHit) meta["quotaExceeded"] = "1";
    llmSendSSE(fd, common::jsonStringify(meta));
    if (answerOut) *answerOut = _collected;
    return true;
}

// ---------- 本地回答缓存（相同题目/代码/问题直接复用，节省 token） ----------
static const char* kAiCacheFile = "./data/ai_cache.jsonl";
static std::mutex g_aiCacheMu;

static bool aiCacheGet(const std::string& key, std::string& answer) {
    std::lock_guard<std::mutex> lk(g_aiCacheMu);
    std::string raw = common::readFile(kAiCacheFile);
    if (raw.empty()) return false;
    auto lines = common::split(raw, '\n');
    long long now = (long long)time(NULL);
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) continue;
        auto o = common::jsonParseObject(lines[i]);
        if (o.empty() || !o.count("k")) continue;
        if (o["k"] != key) continue;
        long long ts = o.count("ts") ? common::stringToLongLong(o["ts"]) : 0;
        if (now - ts > 30LL * 86400) return false;
        if (!o.count("a") || o["a"].empty()) return false;
        answer = o["a"];
        return true;
    }
    return false;
}

static void aiCachePut(const std::string& key, const std::string& answer, const std::string& title) {
    if (answer.size() < 40) return;
    std::lock_guard<std::mutex> lk(g_aiCacheMu);
    std::string raw = common::readFile(kAiCacheFile);
    auto lines = common::split(raw, '\n');
    long long now = (long long)time(NULL);
    std::vector<std::string> keep;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) continue;
        auto o = common::jsonParseObject(lines[i]);
        if (o.empty()) continue;
        if (o.count("k") && o["k"] == key) continue;
        long long ts = o.count("ts") ? common::stringToLongLong(o["ts"]) : 0;
        if (now - ts > 30LL * 86400) continue;
        keep.push_back(lines[i]);
    }
    if (keep.size() >= 400) keep.erase(keep.begin(), keep.begin() + (keep.size() - 399));
    std::map<std::string, std::string> row;
    row["k"] = key;
    row["ts"] = std::to_string(now);
    row["t"] = title.size() > 120 ? title.substr(0, 120) : title;
    row["a"] = answer;
    keep.push_back(common::jsonStringify(row));
    std::string out;
    for (size_t i = 0; i < keep.size(); i++) out += keep[i] + "\n";
    common::writeFile(kAiCacheFile, out);
}

// ---------- 带触手的对话：先跑工具轮，再流式回答 ----------
static bool llmRunAgent(SOCKET fd, DataStore* ds, const std::string& userId,
                        const std::string& model, std::string messagesJson,
                        long long limitPoints, bool allowTools, const std::string& task,
                        std::string* answerOut, int effort) {
    long long extraTok = 0;
    const int MAX_ROUNDS = 4;
    for (int round = 0; allowTools && round < MAX_ROUNDS; round++) {
        std::string content;
        std::vector<AiToolCall> calls;
        long long pt = 0, ct = 0;
        std::string err, roundThink;
        bool roundTrunc = false;
        if (!aiToolRound(aiCfgForModel(model, NULL), model, messagesJson, content, calls, pt, ct, err, effort, roundThink, roundTrunc)) {
            if (!err.empty()) {
                std::map<std::string, std::string> em;
                em["error"] = err;
                llmSendSSE(fd, common::jsonStringify(em));
                return false;
            }
            break;
        }
        extraTok += pt + ct;
        if (calls.empty()) {
            if (!roundThink.empty())
                llmSendSSE(fd, "{\"message\":{\"thinking\":" + common::jsonStringify(roundThink) + "}}");
            // 模型直接给了内容：分两段推给前端，避免再花一次调用
            if (!content.empty()) {
                if (roundTrunc) {
                    // 被 max_tokens 截断（思考与正文共用预算）：先把已生成的部分上屏，
                    // 再改用流式接着写，避免用户看到一个「写到一半就没了」的答案。
                    llmSendSSE(fd, "{\"type\":\"progress\",\"text\":" + common::jsonStringify(content) + "}");
                    if (messagesJson.size() && messagesJson[messagesJson.size()-1] == ']')
                        messagesJson.erase(messagesJson.size()-1);
                    messagesJson += ",{\"role\":\"assistant\",\"content\":" + common::jsonStringify(content) + "}";
                    messagesJson += ",{\"role\":\"user\",\"content\":" +
                                    common::jsonStringify("请从上面中断的地方接着写下去，不要重复已经写过的内容，也不要重新开头。") + "}]";
                    return llmStreamChat(fd, ds, userId, model, messagesJson, limitPoints, extraTok + pt + ct,
                                         task, answerOut, effort);
                }
                // 按 UTF-8 字符边界切分，避免把一个汉字拆成两半
                size_t cut = content.size() / 2 + 1;
                while (cut > 0 && cut < content.size() && ((unsigned char)content[cut] & 0xC0) == 0x80) cut--;
                std::string first = content.substr(0, cut);
                std::string second = content.substr(cut);
                llmSendSSE(fd, "{\"message\":{\"content\":" + common::jsonStringify(first) + "}}");
                if (!second.empty())
                    llmSendSSE(fd, "{\"message\":{\"content\":" + common::jsonStringify(second) + "}}");
                long long costi = aiquota::costOf(model, extraTok + 8);
                if (costi > limitPoints) costi = limitPoints;
                User* uu = ds->findUserById(userId);
                aiquota::Info after = aiquota::record(userId, costi, uu ? uu->aiWindowQuota : 0, uu ? uu->aiMonthQuota : 0);
                aitools::logUsage(userId, model, task, extraTok, 0, costi);
                std::map<std::string, std::string> meta;
                meta["type"] = "meta";
                meta["cost"] = std::to_string(costi);
                meta["balance"] = std::to_string(aiquota::remaining(after));
                meta["windowQuota"] = std::to_string(after.windowQuota);
                meta["windowUsed"] = std::to_string(after.windowUsed);
                meta["monthQuota"] = std::to_string(after.monthQuota);
                meta["monthUsed"] = std::to_string(after.monthUsed);
                meta["extra"] = std::to_string(after.extra);
                llmSendSSE(fd, common::jsonStringify(meta));
                if (answerOut) *answerOut = content;
                return true;
            }
            break;
        }
        // 工具轮里模型真实的推理也作为工作流的一步推给前端
        if (!roundThink.empty()) {
            llmSendSSE(fd, "{\"message\":{\"thinking\":" + common::jsonStringify(roundThink) + "}}");
        }
        // 调用工具前模型说的话 = 工作过程中的间隙输出
        if (!content.empty()) {
            llmSendSSE(fd, "{\"type\":\"progress\",\"text\":" + common::jsonStringify(content) + "}");
        }
        // 有工具调用：通知前端 + 执行 + 追加结果
        std::string asstCalls;
        for (size_t i = 0; i < calls.size(); i++) {
            if (!asstCalls.empty()) asstCalls += ",";
            asstCalls += "{\"id\":\"" + common::jsonEscape(calls[i].id) + "\",\"type\":\"function\",\"function\":{"
                         "\"name\":\"" + common::jsonEscape(calls[i].name) + "\","
                         "\"arguments\":" + common::jsonStringify(calls[i].args) + "}}";
        }
        if (messagesJson.size() && messagesJson[messagesJson.size()-1] == ']')
            messagesJson.erase(messagesJson.size()-1);
        messagesJson += ",{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[" + asstCalls + "]}";

        for (size_t i = 0; i < calls.size(); i++) {
            std::map<std::string, std::string> ev;
            ev["type"] = "tool";
            ev["name"] = calls[i].name;
            ev["args"] = calls[i].args;
            llmSendSSE(fd, common::jsonStringify(ev));
            aitools::Result tr;
            if (aitools::isTool(calls[i].name))
                tr = aitools::run(userId, calls[i].name, calls[i].args);
            else
                tr.output = "未知工具：" + calls[i].name;
            std::map<std::string, std::string> done;
            done["type"] = "tool_done";
            done["name"] = calls[i].name;
            done["ok"] = tr.ok ? "1" : "0";
            std::string preview = tr.output;
            if (preview.size() > 400) preview = preview.substr(0, 400) + "...";
            done["preview"] = preview;
            llmSendSSE(fd, common::jsonStringify(done));
            messagesJson += ",{\"role\":\"tool\",\"tool_call_id\":\"" + common::jsonEscape(calls[i].id) +
                            "\",\"content\":" + common::jsonStringify(tr.output) + "}";
        }
        messagesJson += "]";
    }
    return llmStreamChat(fd, ds, userId, model, messagesJson, limitPoints, extraTok, task, answerOut, effort);
}

// ============================================================
// 开放 API：个人 API Key + OpenAI 兼容网关（/v1/...）+ 文件编辑 API（/api/ai/...）
// ============================================================
static std::string apiKeyFile() { return "./data/api_keys.json"; }

static std::vector<std::map<std::string, std::string> > apiKeysLoad() {
    std::vector<std::map<std::string, std::string> > out;
    std::string raw = common::readFile(apiKeyFile());
    std::vector<std::string> lines = common::split(raw, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        std::string l = common::trim(lines[i]);
        if (l.empty()) continue;
        auto o = common::jsonParseObject(l);
        if (!o["key"].empty()) out.push_back(o);
    }
    return out;
}
static void apiKeysSave(const std::vector<std::map<std::string, std::string> >& keys) {
    std::string s;
    for (size_t i = 0; i < keys.size(); i++) s += common::jsonStringify(keys[i]) + "\n";
    common::writeFile(apiKeyFile(), s);
}
static std::string apiKeyGen() {
    static const char* hexc = "0123456789abcdef";
    std::string t = "oi-";
    srand((unsigned)(time(NULL) ^ (unsigned)(long long)&t) ^ (unsigned)clock());
    for (int i = 0; i < 40; i++) t += hexc[rand() % 16];
    return t;
}
static std::string apiKeyMask(const std::string& k) {
    if (k.size() <= 12) return "****";
    return k.substr(0, 7) + "****" + k.substr(k.size() - 4);
}
static std::string apiKeyFromHeaders(const std::map<std::string, std::string>& headers) {
    auto it = headers.find("authorization");
    if (it != headers.end()) {
        std::string v = common::trim(it->second);
        if (v.size() > 7) {
            std::string low = v.substr(0, 7);
            for (size_t i = 0; i < low.size(); i++) low[i] = (char)tolower((unsigned char)low[i]);
            if (low == "bearer ") return common::trim(v.substr(7));
        }
        return v;
    }
    auto k = headers.find("x-api-key");
    if (k != headers.end()) return common::trim(k->second);
    return "";
}
static std::string apiQuery(const std::string& path, const std::string& key) {
    size_t q = path.find('?');
    if (q == std::string::npos) return "";
    std::vector<std::string> parts = common::split(path.substr(q + 1), '&');
    for (size_t i = 0; i < parts.size(); i++) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        if (parts[i].substr(0, eq) == key) return common::urlDecode(parts[i].substr(eq + 1));
    }
    return "";
}
// 从表单文本里取值（兼容 x-www-form-urlencoded 调用）
static std::string apiFormValue(const std::string& body, const std::string& key) {
    std::vector<std::string> parts = common::split(body, '&');
    for (size_t i = 0; i < parts.size(); i++) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        if (parts[i].substr(0, eq) == key) return common::urlDecode(parts[i].substr(eq + 1));
    }
    return "";
}
// 从 JSON 文本里取顶层数组原文（"key":[...]）
static std::string apiJsonArrayRaw(const std::string& src, const std::string& key) {
    size_t p = src.find("\"" + key + "\"");
    if (p == std::string::npos) return "";
    size_t a = src.find('[', p);
    if (a == std::string::npos) return "";
    int depth = 0;
    bool inStr = false, esc = false;
    for (size_t i = a; i < src.size(); i++) {
        char ch = src[i];
        if (esc) { esc = false; continue; }
        if (ch == '\\') { esc = true; continue; }
        if (ch == '"') { inStr = !inStr; continue; }
        if (inStr) continue;
        if (ch == '[') depth++;
        else if (ch == ']') { depth--; if (depth == 0) return src.substr(a, i - a + 1); }
    }
    return "";
}

void HttpServer::handleLLMRequest(SOCKET fd, const std::string& path, const std::map<std::string, std::string>& headers, const std::string& body) {
    // ========== 开放 API：OpenAI 兼容（/v1/...）+ 文件编辑（/api/ai/...） ==========
    // 用个人 API Key（Authorization: Bearer oi-xxx 或 x-api-key），在 /llm/keys 生成。
    if (path.rfind("/v1/", 0) == 0 || path.rfind("/api/ai/", 0) == 0) {
        std::string apiKey = apiKeyFromHeaders(headers);
        std::vector<std::map<std::string, std::string> > allKeys = apiKeysLoad();
        User* apiUser = NULL;
        int keyIdx = -1;
        if (!apiKey.empty()) {
            for (size_t i = 0; i < allKeys.size(); i++)
                if (allKeys[i]["key"] == apiKey && allKeys[i]["revoked"] != "1") {
                    apiUser = dataStore->findUserById(allKeys[i]["userId"]);
                    keyIdx = (int)i;
                    break;
                }
        }
        if (!apiUser) {
            llmSendJson(fd, 401, "{\"error\":{\"message\":\"Invalid API key：请在 /llm/keys 创建密钥（Authorization: Bearer oi-...）\",\"type\":\"invalid_request_error\",\"code\":\"invalid_api_key\"}}");
            return;
        }
        if (keyIdx >= 0) {
            allKeys[keyIdx]["lastUsed"] = common::getCurrentTimeStr();
            apiKeysSave(allKeys);
        }
        std::vector<aiprov::ModelEntry> vis = aiModelsForUser(apiUser);

        if (path == "/v1/models" || path == "/v1/models/") {
            std::string data = "[";
            for (size_t i = 0; i < vis.size(); i++) {
                if (i) data += ",";
                data += "{\"id\":" + common::jsonStringify(vis[i].id) + ",\"object\":\"model\",\"owned_by\":" +
                        common::jsonStringify(vis[i].providerName) + "}";
            }
            data += "]";
            llmSendJson(fd, 200, "{\"object\":\"list\",\"data\":" + data + "}");
            return;
        }

        // ---------- 文件编辑 API（给外部 AI / IDE 用） ----------
        if (path.rfind("/api/ai/", 0) == 0) {
            std::string op = path.substr(8);
            if (op.rfind("read", 0) == 0) {
                std::map<std::string, std::string> m;
                m["path"] = apiQuery(path, "path");
                m["root"] = apiQuery(path, "root");
                aitools::Result r = aitools::run(apiUser->id, "read_file", common::jsonStringify(m));
                llmSendJson(fd, r.ok ? 200 : 400, std::string("{\"ok\":") + (r.ok ? "true" : "false") +
                            ",\"content\":" + common::jsonStringify(r.output) + "}");
                return;
            }
            auto areq = common::jsonParseObject(body);
            if (op.rfind("write", 0) == 0 || op.rfind("edit", 0) == 0) {
                bool isEdit = (op.rfind("edit", 0) == 0);
                std::map<std::string, std::string> m;
                m["path"] = areq.count("path") ? areq["path"] : "";
                m["root"] = areq.count("root") ? areq["root"] : "workspace";
                if (isEdit) {
                    m["old_string"] = areq.count("old_string") ? areq["old_string"] : (areq.count("old") ? areq["old"] : "");
                    m["new_string"] = areq.count("new_string") ? areq["new_string"] : (areq.count("new") ? areq["new"] : "");
                    m["replace_all"] = areq.count("replace_all") ? areq["replace_all"] : "false";
                } else {
                    m["content"] = areq.count("content") ? areq["content"] : "";
                }
                aitools::Result r = aitools::run(apiUser->id, isEdit ? "edit_file" : "write_file", common::jsonStringify(m));
                llmSendJson(fd, r.ok ? 200 : 400, std::string("{\"ok\":") + (r.ok ? "true" : "false") +
                            ",\"result\":" + common::jsonStringify(r.output) + "}");
                return;
            }
            llmSendJson(fd, 404, "{\"ok\":false,\"error\":\"unknown api（支持 read/write/edit）\"}");
            return;
        }

        // ---------- 模型校验 + 额度 ----------
        auto req = common::jsonParseObject(body);
        std::string model = req.count("model") ? common::trim(req["model"]) : "";
        bool modelOk = false;
        for (size_t i = 0; i < vis.size(); i++) if (vis[i].id == model) modelOk = true;
        if (model.empty() && !vis.empty()) { model = vis[0].id; modelOk = true; }
        if (!modelOk) {
            llmSendJson(fd, 400, "{\"error\":{\"message\":\"model not allowed for this account\",\"type\":\"invalid_request_error\",\"code\":\"model_not_found\"}}");
            return;
        }
        aiquota::Info qi = aiquota::info(apiUser->id, apiUser->aiWindowQuota, apiUser->aiMonthQuota);
        if (aiquota::remaining(qi) < 1) {
            llmSendJson(fd, 402, "{\"error\":{\"message\":\"quota exceeded\",\"type\":\"insufficient_quota\"}}");
            return;
        }
        bool isResponses = (path.rfind("/v1/responses", 0) == 0);
        std::string messages = "[]";
        if (isResponses) {
            std::string instructions = req.count("instructions") ? req["instructions"] : "";
            std::string input = req.count("input") ? req["input"] : "";
            std::string mArr = "[";
            bool firstMsg = true;
            if (!instructions.empty()) {
                mArr += "{\"role\":\"system\",\"content\":" + common::jsonStringify(instructions) + "}";
                firstMsg = false;
            }
            if (!input.empty() && input[0] == '[') {
                std::vector<std::string> arr = common::jsonParseArray(input);
                for (size_t i = 0; i < arr.size(); i++) {
                    std::string role = jsonPeekString(arr[i], "role");
                    if (role == "developer") role = "system";
                    if (role.empty()) role = "user";
                    std::string text = jsonPeekString(arr[i], "content");
                    if (text.empty()) text = jsonPeekString(arr[i], "text");
                    if (text.empty()) text = jsonPeekString(arr[i], "input_text");
                    if (text.empty()) continue;
                    if (!firstMsg) mArr += ",";
                    firstMsg = false;
                    mArr += "{\"role\":" + common::jsonStringify(role) + ",\"content\":" + common::jsonStringify(text) + "}";
                }
            } else if (!input.empty()) {
                if (!firstMsg) mArr += ",";
                mArr += "{\"role\":\"user\",\"content\":" + common::jsonStringify(input) + "}";
            }
            mArr += "]";
            messages = mArr;
        } else {
            messages = req.count("messages") ? req["messages"] : "[]";
        }
        std::string upstream = "{\"model\":" + common::jsonStringify(model) + ",\"messages\":" + messages + ",\"stream\":false";
        if (req.count("temperature") && !req["temperature"].empty()) upstream += ",\"temperature\":" + req["temperature"];
        if (req.count("top_p") && !req["top_p"].empty()) upstream += ",\"top_p\":" + req["top_p"];
        if (req.count("max_tokens") && !req["max_tokens"].empty()) upstream += ",\"max_tokens\":" + req["max_tokens"];
        if (req.count("max_output_tokens") && !req["max_output_tokens"].empty()) upstream += ",\"max_tokens\":" + req["max_output_tokens"];
        if (!isResponses) {
            std::string toolsRaw = req.count("tools") ? req["tools"] : "";
            if (!toolsRaw.empty() && toolsRaw != "[]") upstream += ",\"tools\":" + toolsRaw;
            if (req.count("tool_choice") && !req["tool_choice"].empty()) {
                std::string tcv = req["tool_choice"];
                // jsonParseObject 会去掉字符串值的引号，这里补回去，否则生成非法 JSON
                if (!tcv.empty() && (tcv[0] == '{' || tcv[0] == '[')) upstream += ",\"tool_choice\":" + tcv;
                else upstream += ",\"tool_choice\":" + common::jsonStringify(tcv);
            }
        }
        upstream += "}";

        AiCfg cfg = aiCfgForModel(model, NULL);
        AiHttp r = aiPostChat(cfg, upstream);
        if (!r.ok) {
            llmSendJson(fd, 502, "{\"error\":{\"message\":" + common::jsonStringify(r.err) + ",\"type\":\"upstream_error\"}}");
            return;
        }
        long long pt = jsonPeekInt(r.body, "prompt_tokens");
        long long ct = jsonPeekInt(r.body, "completion_tokens");
        long long cost = aiquota::costOf(model, pt + ct);
        if (cost < 1) cost = 1;
        User* uu = dataStore->findUserById(apiUser->id);
        aiquota::record(apiUser->id, cost, uu ? uu->aiWindowQuota : 0, uu ? uu->aiMonthQuota : 0);
        aitools::logUsage(apiUser->id, model, isResponses ? "api-responses" : "api-chat", pt, ct, cost);

        std::string content = jsonPeekString(r.body, "content");
        std::string toolCalls = apiJsonArrayRaw(r.body, "tool_calls");
        bool wantStream = false;
        if (req.count("stream")) {
            std::string sv = req["stream"];
            for (size_t i = 0; i < sv.size(); i++) sv[i] = (char)tolower((unsigned char)sv[i]);
            wantStream = (sv == "true" || sv == "1");
        }
        long long ts = (long long)time(NULL);

        if (isResponses) {
            std::string rid = "resp_" + common::genId();
            std::string mid = "msg_" + common::genId();
            std::string usage = "{\"input_tokens\":" + std::to_string(pt) + ",\"output_tokens\":" +
                                std::to_string(ct) + ",\"total_tokens\":" + std::to_string(pt + ct) + "}";
            std::string item = "{\"id\":" + common::jsonStringify(mid) +
                               ",\"type\":\"message\",\"status\":\"completed\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":" +
                               common::jsonStringify(content) + ",\"annotations\":[]}]}";
            std::string responseObj = "{\"id\":" + common::jsonStringify(rid) +
                                      ",\"object\":\"response\",\"created_at\":" + std::to_string(ts) +
                                      ",\"status\":\"completed\",\"model\":" + common::jsonStringify(model) +
                                      ",\"output\":[" + item + "],\"output_text\":" + common::jsonStringify(content) +
                                      ",\"usage\":" + usage + "}";
            if (!wantStream) { llmSendJson(fd, 200, responseObj); return; }
            std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nConnection: close\r\nCache-Control: no-cache\r\n\r\n";
            llmSendRaw(fd, head);
            llmSendSSE(fd, "{\"type\":\"response.created\",\"response\":{\"id\":" + common::jsonStringify(rid) +
                           ",\"object\":\"response\",\"status\":\"in_progress\",\"model\":" + common::jsonStringify(model) + "}}");
            llmSendSSE(fd, "{\"type\":\"response.output_item.added\",\"output_index\":0,\"item\":{\"id\":" +
                           common::jsonStringify(mid) + ",\"type\":\"message\",\"status\":\"in_progress\",\"role\":\"assistant\",\"content\":[]}}");
            for (size_t off = 0; off < content.size();) {
                size_t n = 160;
                if (off + n > content.size()) n = content.size() - off;
                while (off + n < content.size() && ((unsigned char)content[off + n] & 0xC0) == 0x80) n--;
                if (n == 0) break;
                llmSendSSE(fd, "{\"type\":\"response.output_text.delta\",\"item_id\":" + common::jsonStringify(mid) +
                               ",\"output_index\":0,\"content_index\":0,\"delta\":" + common::jsonStringify(content.substr(off, n)) + "}");
                off += n;
            }
            llmSendSSE(fd, "{\"type\":\"response.output_text.done\",\"item_id\":" + common::jsonStringify(mid) +
                           ",\"output_index\":0,\"content_index\":0,\"text\":" + common::jsonStringify(content) + "}");
            llmSendSSE(fd, "{\"type\":\"response.completed\",\"response\":" + responseObj + "}");
            llmSendRaw(fd, "data: [DONE]\n\n");
            return;
        }

        // ---------- /v1/chat/completions ----------
        if (!wantStream) {
            if (!toolCalls.empty()) { llmSendJson(fd, 200, r.body); return; }
            std::string out = "{\"id\":\"chatcmpl-" + common::genId() + "\",\"object\":\"chat.completion\",\"created\":" +
                              std::to_string(ts) + ",\"model\":" + common::jsonStringify(model) +
                              ",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":" +
                              common::jsonStringify(content) + "},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":" +
                              std::to_string(pt) + ",\"completion_tokens\":" + std::to_string(ct) +
                              ",\"total_tokens\":" + std::to_string(pt + ct) + "}}";
            llmSendJson(fd, 200, out);
            return;
        }
        std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nConnection: close\r\nCache-Control: no-cache\r\n\r\n";
        llmSendRaw(fd, head);
        std::string cid = "chatcmpl-" + common::genId();
        std::string base = "{\"id\":" + common::jsonStringify(cid) + ",\"object\":\"chat.completion.chunk\",\"created\":" +
                           std::to_string(ts) + ",\"model\":" + common::jsonStringify(model) + ",\"choices\":[{\"index\":0,";
        llmSendSSE(fd, base + "\"delta\":{\"role\":\"assistant\"},\"finish_reason\":null}]}");
        for (size_t off = 0; off < content.size();) {
            size_t n = 160;
            if (off + n > content.size()) n = content.size() - off;
            while (off + n < content.size() && ((unsigned char)content[off + n] & 0xC0) == 0x80) n--;
            if (n == 0) break;
            llmSendSSE(fd, base + "\"delta\":{\"content\":" + common::jsonStringify(content.substr(off, n)) + "},\"finish_reason\":null}]}");
            off += n;
        }
        llmSendSSE(fd, base + "\"delta\":{},\"finish_reason\":\"" + std::string(toolCalls.empty() ? "stop" : "tool_calls") + "\"}],\"usage\":{\"prompt_tokens\":" +
                   std::to_string(pt) + ",\"completion_tokens\":" + std::to_string(ct) + ",\"total_tokens\":" + std::to_string(pt + ct) + "}}");
        llmSendRaw(fd, "data: [DONE]\n\n");
        return;
    }

    User* user = nullptr;
    auto it = headers.find("cookie");
    if (it != headers.end()) {
        std::string cookie = it->second;
        size_t pos = cookie.find("SESSION=");
        if (pos != std::string::npos) {
            size_t start = pos + 8;
            size_t end = cookie.find(";", start);
            if (end == std::string::npos) end = cookie.length();
            std::string sid = cookie.substr(start, end - start);
            Session* sess = SessionManager::instance().getSession(sid);
            if (sess) user = dataStore->findUserById(sess->userId);
        }
    }
    if (path == "/llm" || path == "/llm/") {
        std::map<std::string, std::string> h;
        h["Location"] = user ? "/llm/chat" : "/login";
        sendResponse(fd, 302, "Found", h, "");
        return;
    }
    if (path == "/llm/chat") {
        if (!user) { std::map<std::string, std::string> h; h["Location"] = "/login"; sendResponse(fd, 302, "Found", h, ""); return; }
        std::string page = common::readFile("./files/ai_chat.html");
        if (page.empty()) page = "<html><body><h1>AI Chat</h1><p>ai_chat.html missing</p></body></html>";
        llmSendHtml(fd, page);
        return;
    }
    // ---------- API 密钥管理页 ----------
    if (path == "/llm/keys" || path == "/llm/keys/") {
        if (!user) { std::map<std::string, std::string> h; h["Location"] = "/login"; sendResponse(fd, 302, "Found", h, ""); return; }
        std::string page = R"HTML(<!DOCTYPE html><html lang="zh-CN"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>API 密钥 · OI 小站</title>
<style>
:root{--bg:#eef3f8;--card:#fff;--border:#e3e9f0;--text:#2f3640;--strong:#1c232b;--muted:#8792a0;--brand:#3a86c8}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;font-size:14px;line-height:1.65}
.wrap{max-width:900px;margin:28px auto;padding:0 16px}.card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:20px 22px;box-shadow:0 1px 3px rgba(20,40,70,.05),0 4px 16px rgba(20,40,70,.05)}
h1{font-size:20px;margin:0 0 6px;color:var(--strong)}h2{font-size:15px;margin:0 0 10px;color:var(--strong)}
.sub{color:var(--muted);font-size:13px;margin-bottom:16px}.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
input{border:1px solid var(--border);border-radius:10px;padding:9px 12px;font-size:14px;outline:none;min-width:220px}
input:focus{border-color:var(--brand)}
button{border:none;border-radius:10px;padding:9px 16px;font-size:13.5px;font-weight:600;background:var(--brand);color:#fff;cursor:pointer}
button.ghost{background:#fff;color:var(--text);border:1px solid var(--border);font-weight:500}
button.danger{background:#fff;color:#e0553f;border:1px solid #f0c7bf}
table{width:100%;border-collapse:collapse;font-size:13px;margin-top:6px}th,td{text-align:left;padding:9px 8px;border-bottom:1px solid #edf1f6;vertical-align:middle}
th{color:var(--muted);font-weight:600;font-size:12.5px}code{font-family:Consolas,monospace;background:#f7fafd;border:1px solid #edf1f6;border-radius:7px;padding:2px 6px}
pre{background:#f7fafd;border:1px solid #edf1f6;border-radius:10px;padding:12px;overflow:auto;font-size:12.5px;white-space:pre-wrap;word-break:break-all}
.pill{display:inline-block;font-size:12px;border-radius:999px;padding:1px 9px;background:rgba(39,174,96,.12);color:#27ae60;font-weight:700}.pill.off{background:#eef2f6;color:#95a5a6}
.newkey{background:rgba(39,174,96,.06);border:1px solid rgba(39,174,96,.35);border-radius:12px;padding:12px 14px;margin:12px 0;display:none}
a{color:var(--brand);text-decoration:none}.mt{margin-top:18px}.note{font-size:12.5px;color:var(--muted)}
</style></head><body><div class="wrap">
<div class="card"><h1>🔑 API 密钥</h1>
<div class="sub">OI 小站提供 <b>OpenAI 兼容 API</b>，可直接接入 Chatbox / Cline / Codex 等工具。密钥只显示一次，请妥善保存。</div>
<div class="row"><input id="kname" placeholder="密钥名称（例如：我的笔记本）"/><button id="createBtn">创建密钥</button></div>
<div class="newkey" id="newKeyBox"><div style="font-size:12.5px;color:var(--muted);margin-bottom:6px">新密钥（只显示这一次）：</div><code id="newKey" style="font-size:13px"></code><br/><button class="ghost" style="margin-top:8px" id="copyKey">复制</button></div>
<table><thead><tr><th>名称</th><th>密钥</th><th>创建</th><th>最近使用</th><th>状态</th><th></th></tr></thead><tbody id="rows"></tbody></table>
<div class="note mt">已吊销的密钥保留记录，方便审计。</div></div>
<div class="card mt"><h2>怎么用</h2>
<div class="sub" style="margin-bottom:8px">Base URL 填 <code>http://127.0.0.1:8083/v1</code>（或你的域名），API Key 填上方生成的 <code>oi-...</code>。</div>
<pre># 列出可用模型
curl http://127.0.0.1:8083/v1/models -H "Authorization: Bearer oi-xxxx"

# Chat Completions（流式也支持）
curl http://127.0.0.1:8083/v1/chat/completions -H "Authorization: Bearer oi-xxxx" \
  -H "Content-Type: application/json" \
  -d '{"model":"deepseek-flash","messages":[{"role":"user","content":"你好"}]}'

# OpenAI Responses API
curl http://127.0.0.1:8083/v1/responses -H "Authorization: Bearer oi-xxxx" \
  -H "Content-Type: application/json" \
  -d '{"model":"deepseek-flash","input":"用一句话解释快速幂"}'

# 文件编辑 API（操作你的 AI 工作区 / 云盘）
curl http://127.0.0.1:8083/api/ai/read?root=drive\&path=main.cpp -H "Authorization: Bearer oi-xxxx"
curl http://127.0.0.1:8083/api/ai/edit -H "Authorization: Bearer oi-xxxx" \
  -H "Content-Type: application/json" \
  -d '{"root":"drive","path":"main.cpp","old_string":"return 0;","new_string":"return 1;"}'</pre>
<div class="note">每次调用都从你的算力点额度中扣除（与站内 AI 助手共用），可在「AI 助手」页查看余额。</div></div>
<div class="mt"><a href="/llm/chat">← 返回 AI 助手</a></div>
</div>
<script>
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
function q(url,opt){return fetch(url,Object.assign({credentials:'same-origin'},opt||{})).then(function(r){return r.json();});}
function render(list){
  var b=document.getElementById('rows'); b.innerHTML='';
  if(!list.length){ b.innerHTML='<tr><td colspan="6" style="color:var(--muted)">还没有密钥，先创建一个。</td></tr>'; return; }
  list.forEach(function(k){
    var tr=document.createElement('tr');
    tr.innerHTML='<td>'+esc(k.name||'—')+'</td><td><code>'+esc(k.mask)+'</code></td><td>'+esc(k.created||'')+'</td><td>'+esc(k.lastUsed||'—')+'</td>'
      +'<td>'+(k.revoked?'<span class="pill off">已吊销</span>':'<span class="pill">可用</span>')+'</td>'
      +'<td style="text-align:right">'+(k.revoked?'':'<button class="danger" data-k="'+esc(k.mask)+'">吊销</button>')+'</td>';
    var btn=tr.querySelector('button[data-k]');
    if(btn) btn.onclick=function(){ if(!confirm('吊销后使用该密钥的程序会立即失效，确定？')) return;
      q('/llm/api/keys',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'revoke',key:btn.getAttribute('data-k')})}).then(function(){ load(); }); };
    b.appendChild(tr);
  });
}
function load(){ q('/llm/api/keys').then(function(d){ render(d.keys||[]); }); }
document.getElementById('createBtn').onclick=function(){
  var name=document.getElementById('kname').value.trim()||'未命名';
  q('/llm/api/keys',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'create',name:name})}).then(function(d){
    if(!d.ok){ alert(d.error||'创建失败'); return; }
    document.getElementById('newKey').textContent=d.key;
    document.getElementById('newKeyBox').style.display='block';
    document.getElementById('kname').value=''; load();
  });
};
document.getElementById('copyKey').onclick=function(){ var t=document.getElementById('newKey').textContent; if(navigator.clipboard){navigator.clipboard.writeText(t);} this.textContent='已复制'; };
load();
</script></body></html>)HTML";
        llmSendHtml(fd, page);
        return;
    }
    if (!user) { llmSendJson(fd, 401, "{\"error\":\"Unauthorized\"}"); return; }

    // ---------- API 密钥增删查（页面内部使用） ----------
    if (path == "/llm/api/keys") {
        if (path.find("?") == std::string::npos) {
            // GET：列出（不返回明文）
        }
        if (body.empty()) {
            std::vector<std::map<std::string, std::string> > keys = apiKeysLoad();
            std::string arr = "[";
            bool firstKey = true;
            for (size_t i = 0; i < keys.size(); i++) {
                if (keys[i]["userId"] != user->id) continue;
                if (!firstKey) arr += ",";
                firstKey = false;
                arr += "{\"mask\":" + common::jsonStringify(apiKeyMask(keys[i]["key"])) +
                       ",\"name\":" + common::jsonStringify(keys[i].count("name") ? keys[i]["name"] : "") +
                       ",\"created\":" + common::jsonStringify(keys[i].count("created") ? keys[i]["created"] : "") +
                       ",\"lastUsed\":" + common::jsonStringify(keys[i].count("lastUsed") ? keys[i]["lastUsed"] : "") +
                       ",\"revoked\":" + std::string(keys[i]["revoked"] == "1" ? "true" : "false") + "}";
            }
            arr += "]";
            llmSendJson(fd, 200, "{\"ok\":true,\"keys\":" + arr + "}");
            return;
        }
        auto kreq = common::jsonParseObject(body);
        std::string action = kreq.count("action") ? kreq["action"] : apiFormValue(body, "action");
        std::vector<std::map<std::string, std::string> > keys = apiKeysLoad();
        if (action == "create") {
            std::map<std::string, std::string> e;
            e["key"] = apiKeyGen();
            e["userId"] = user->id;
            e["name"] = kreq.count("name") ? common::trim(kreq["name"]) : common::trim(apiFormValue(body, "name"));
            if (e["name"].empty()) e["name"] = "未命名";
            e["created"] = common::getCurrentTimeStr();
            e["lastUsed"] = "";
            e["revoked"] = "0";
            keys.push_back(e);
            apiKeysSave(keys);
            llmSendJson(fd, 200, "{\"ok\":true,\"key\":" + common::jsonStringify(e["key"]) + "}");
            return;
        }
        if (action == "revoke") {
            std::string masked = kreq.count("key") ? kreq["key"] : apiFormValue(body, "key");
            for (size_t i = 0; i < keys.size(); i++)
                if (keys[i]["userId"] == user->id && apiKeyMask(keys[i]["key"]) == masked)
                    keys[i]["revoked"] = "1";
            apiKeysSave(keys);
            llmSendJson(fd, 200, "{\"ok\":true}");
            return;
        }
        llmSendJson(fd, 400, "{\"ok\":false,\"error\":\"unknown action\"}");
        return;
    }

    if (path == "/llm/api/me") {
        aiquota::Info qi = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
        std::map<std::string, std::string> o;
        o["username"] = user->username;
        o["tokens"] = std::to_string(aiquota::remaining(qi));   // 兼容旧字段：剩余算力点
        o["isAdmin"] = adminperm::any(user) ? "true" : "false";
        o["windowQuota"] = std::to_string(qi.windowQuota);
        o["windowUsed"] = std::to_string(qi.windowUsed);
        o["monthQuota"] = std::to_string(qi.monthQuota);
        o["monthUsed"] = std::to_string(qi.monthUsed);
        o["remaining"] = std::to_string(aiquota::remaining(qi));
        o["extra"] = std::to_string(qi.extra);
        o["windowHours"] = std::to_string(aiquota::load().windowHours);
        o["planId"] = user->aiPlanId;
        aiplan::Plan pl;
        o["planName"] = aiplan::find(user->aiPlanId, pl) ? pl.name : std::string("");
        llmSendJson(fd, 200, common::jsonStringify(o));
        return;
    }
    if (path == "/llm/api/balance") {
        aiquota::Info qi = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
        llmSendJson(fd, 200, "{\"tokens\":" + std::to_string(aiquota::remaining(qi)) + "}");
        return;
    }
    if (path == "/llm/api/quota") {
        aiquota::Info qi = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
        llmSendJson(fd, 200, aiquota::jsonOf(qi));
        return;
    }
    if (path == "/llm/api/models") {
        std::vector<aiprov::ModelEntry> visible = aiModelsForUser(user);
        std::string models = "[", detail = "[", groups = "[";
        std::string lastProvider;
        for (size_t i = 0; i < visible.size(); i++) {
            const aiprov::ModelEntry& m = visible[i];
            if (i) { models += ","; detail += ","; }
            models += common::jsonStringify(m.id);
            detail += "{\"id\":" + common::jsonStringify(m.id) + ",\"name\":" + common::jsonStringify(m.name) +
                      ",\"provider\":" + common::jsonStringify(m.providerName) +
                      ",\"free\":" + std::string(m.freeModel ? "true" : "false") +
                      ",\"perK\":" + std::to_string(aiquota::priceOf(m.id)) + "}";
            if (m.providerId != lastProvider) {
                if (!lastProvider.empty()) groups += "]},";
                groups += "{\"id\":" + common::jsonStringify(m.providerId) + ",\"name\":" + common::jsonStringify(m.providerName) + ",\"models\":[";
                lastProvider = m.providerId;
            } else {
                groups += ",";
            }
            groups += "{\"id\":" + common::jsonStringify(m.id) + ",\"name\":" + common::jsonStringify(m.name) +
                      ",\"free\":" + std::string(m.freeModel ? "true" : "false") + "}";
        }
        if (!lastProvider.empty()) groups += "]}";
        models += "]"; detail += "]"; groups += "]";
        std::string def = visible.empty() ? std::string("") : visible[0].id;
        std::string out = "{\"models\":" + models + ",\"groups\":" + groups + ",\"default\":" +
                          common::jsonStringify(def) + ",\"detail\":" + detail + ",\"scanError\":\"\"}";
        llmSendJson(fd, 200, out);
        return;
    }
    if (path == "/llm/api/models/scan") {
        if (!adminperm::has(user, "ai")) { llmSendJson(fd, 403, "{\"error\":\"Forbidden\"}"); return; }
        std::string scanErr;
        std::vector<AiModelItem> ms = aiScanModelsPublic(true, &scanErr);
        std::string detail = "[";
        for (size_t i = 0; i < ms.size(); i++) {
            if (i) detail += ",";
            detail += "{\"id\":" + common::jsonStringify(ms[i].id) + ",\"name\":" +
                      common::jsonStringify(ms[i].name) + "}";
        }
        detail += "]";
        llmSendJson(fd, 200, "{\"ok\":" + std::string(ms.empty() ? "false" : "true") +
                             ",\"models\":" + detail + ",\"error\":" + common::jsonStringify(scanErr) + "}");
        return;
    }
    if (path == "/llm/api/chat") {
        auto req = common::jsonParseObject(body);
        std::string userMessage = req.count("message") ? req["message"] : "";
        std::string model = (req.count("model") && !req["model"].empty()) ? req["model"] : "";
        std::vector<aiprov::ModelEntry> visible = aiModelsForUser(user);
        if (model.empty()) model = visible.empty() ? aiCfg().model : visible[0].id;
        bool modelOk = false;
        for (size_t i = 0; i < visible.size(); i++) if (visible[i].id == model) modelOk = true;
        if (!modelOk) {
            llmSendJson(fd, 400, "{\"error\":\"这个模型当前套餐不可用（或管理员尚未启用）\",\"code\":\"MODEL_NOT_ALLOWED\"}");
            return;
        }
        std::string history = req.count("history") ? req["history"] : "[]";
        std::string task = req.count("task") ? req["task"] : "chat";
        // 模式：chat = 纯对话（不调用工具），work = 工作模式（可读写文件 / 执行命令）
        std::string mode = req.count("mode") ? common::trim(req["mode"]) : "";
        bool allowTools = true;
        if (mode == "chat") allowTools = false;
        else if (mode == "work") allowTools = true;
        else allowTools = !(req.count("tools") && req["tools"] == "0");
        // 思考强度：0 关闭 / 1 低 / 2 中 / 3 高
        int effort = req.count("effort") ? atoi(req["effort"].c_str()) : 2;
        if (effort < 0) effort = 0;
        if (effort > 3) effort = 3;
        if (userMessage.empty() && task == "chat") {
            llmSendJson(fd, 400, "{\"error\":\"消息为空\"}");
            return;
        }
        aiquota::Info _qi = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
        long long _remain = aiquota::remaining(_qi);
        if (_remain < 1) {
            std::string why = (_qi.windowQuota - _qi.windowUsed <= 0)
                ? ("5 小时额度已用完（" + std::to_string(_qi.windowUsed) + "/" + std::to_string(_qi.windowQuota) + " 算力点），请稍后再试")
                : ("本月额度已用完（" + std::to_string(_qi.monthUsed) + "/" + std::to_string(_qi.monthQuota) + " 算力点）");
            llmSendJson(fd, 402, "{\"error\":" + common::jsonStringify(why) + ",\"code\":\"QUOTA_EXCEEDED\",\"quota\":" + aiquota::jsonOf(_qi) + "}");
            return;
        }

        // ---------- 按用途组装提示词 ----------
        std::string sysText;
        std::string userText = userMessage;
        if (task == "debug") {
            std::string pid = req.count("problemId") ? common::trim(req["problemId"]) : "";
            std::string code = req.count("code") ? req["code"] : "";
            std::string filePath = req.count("filePath") ? common::trim(req["filePath"]) : "";
            std::string extra = req.count("extra") ? req["extra"] : "";
            if (code.empty() && !filePath.empty()) {
                std::string full = aitools::workspaceDir(user->id) + filePath;
                code = common::readFile(full);
            }
            sysText = "你是一名资深 OI 教练，擅长帮学生 debug 竞赛代码。"
                      "请按顺序回答：1) 这段代码最可能的错误点（结合题目要求与常见坑）；"
                      "2) 出错原因；3) 具体的修改方法（给出改后的关键代码片段）；"
                      "4) 建议补充的边界测试数据。使用中文，简洁、分点，不要客套。"
                      "如果信息不足，主动用工具列出/读取工作区文件或运行命令来验证你的判断。";
            userText = "【题目】";
            if (!pid.empty()) {
                Problem* p = dataStore->findProblemById(pid);
                if (p) {
                    userText += "题号 " + p->id + " " + p->title + "\n时间限制 " + std::to_string(p->timeLimit) +
                                "ms，内存 " + std::to_string(p->memLimit) + "MB\n";
                    std::string desc = p->description;
                    if (desc.size() > 3000) desc = desc.substr(0, 3000) + "...";
                    userText += "题面：" + desc + "\n";
                    if (!p->inputDesc.empty()) userText += "输入格式：" + p->inputDesc + "\n";
                    if (!p->outputDesc.empty()) userText += "输出格式：" + p->outputDesc + "\n";
                    std::string sample;
                    for (size_t i = 0; i < p->samples.size() && i < 3; i++) sample += p->samples[i] + "\n";
                    if (!sample.empty()) userText += "样例：" + sample;
                    if (!p->spj.empty()) userText += "\n注意：本题使用 SPJ 特殊评测。\n";
                    // 最近一次该题提交的判题结果
                    for (size_t i = dataStore->submissions.size(); i > 0; i--) {
                        Submission& sb = dataStore->submissions[i-1];
                        if (sb.userId == user->id && sb.problemId == pid) {
                            std::string jr = sb.judgeResult;
                            if (jr.size() > 800) jr = jr.substr(0, 800) + "...";
                            userText += "\n我最近一次提交的判题结果：\n" + jr + "\n";
                            break;
                        }
                    }
                } else {
                    userText += "（题号 " + pid + " 不存在）\n";
                }
            } else {
                userText += "（未指定题号，仅根据代码判断）\n";
            }
            userText += "\n【我的代码】\n```cpp\n" + code + "\n```\n";
            if (!extra.empty()) userText += "\n【补充说明】\n" + extra + "\n";
            if (!filePath.empty()) userText += "\n（代码来自工作区文件 " + filePath + "）\n";
        } else if (task == "explain") {
            std::string pid = req.count("problemId") ? common::trim(req["problemId"]) : "";
            std::string statement = req.count("statement") ? req["statement"] : "";
            std::string level = req.count("level") ? req["level"] : "普及";
            sysText = "你是一名 OI 竞赛讲师。请针对题目给出思路讲解，不要输出完整可提交代码"
                      "（关键片段最多 8 行）。结构：1) 题意抽象；2) 暴力思路与复杂度；"
                      "3) 正解思路（关键观察/不变量）；4) 复杂度分析；5) 实现与坑点；6) 可以继续练习的题型。"
                      "使用中文 Markdown，数学公式用 $...$。";
            userText = "【水平】" + level + "\n";
            if (!pid.empty()) {
                Problem* p = dataStore->findProblemById(pid);
                if (p) {
                    std::string desc = p->description;
                    if (desc.size() > 4000) desc = desc.substr(0, 4000) + "...";
                    userText += "【题目】" + p->id + " " + p->title + "\n" + desc + "\n";
                    if (!p->inputDesc.empty()) userText += "输入：" + p->inputDesc + "\n";
                    if (!p->outputDesc.empty()) userText += "输出：" + p->outputDesc + "\n";
                    if (!p->samples.empty()) userText += "样例：" + p->samples[0] + "\n";
                } else {
                    userText += "（题号 " + pid + " 不存在）\n";
                }
            }
            if (!statement.empty()) {
                if (statement.size() > 6000) statement = statement.substr(0, 6000) + "...";
                userText += "【我贴的题面】\n" + statement + "\n";
            }
            if (userMessage.empty()) userText += "\n请给出这道题的思路讲解。";
        } else {
            if (!allowTools) {
                sysText = "你是 OI 小站的 AI 助手，请用简洁、清晰、有条理的中文回答。涉及代码请用 Markdown 代码块。"
                          "当前是对话模式：不要调用任何工具，也不要声称自己执行过命令或读写过文件；"
                          "如果确实需要运行命令或处理文件，请提示用户切换到工作模式。";
            } else {
                sysText = "你是一个乐于助人的 AI 助手，请用简洁、清晰、有条理的中文回答。涉及代码请用 Markdown 代码块。"
                          "当前是工作模式：你可以使用工具在工作区读写文件、执行命令来验证想法；涉及编译运行请说明你在做什么。"
                          "工具都支持 root 参数：root=workspace 是 AI 工作区（默认），root=drive 是用户的云盘"
                          "（用户在 /drive 页面看到的文件和文件夹，可以直接读写、新建、重命名、删除）。"
                          "动手前先 list_files 看清目录，删除前确认路径。"
                          "调用工具前可以用一两句话说明接下来要做什么（这些话会显示在工作过程里），"
                          "全部完成后给出完整的结尾报告，不要只说「已完成」。";
            }
        }
        // 思考强度提示（与上游 reasoning 参数配合）
        if (effort == 0)
            sysText += "\n\n思考强度：关闭。请直接给出结论与必要说明，不要展开长篇推理。";
        else if (effort == 1)
            sysText += "\n\n思考强度：低。快速梳理要点后作答，保持简短。";
        else if (effort == 2)
            sysText += "\n\n思考强度：中。先确认关键条件与边界，再给出结论。";
        else
            sysText += "\n\n思考强度：高。请充分分析、验证边界情况与反例后再给出结论，必要时说明验证过程。";

        std::string sys = "{\"role\":\"system\",\"content\":" + common::jsonStringify(sysText) + "}";
        std::string um = "{\"role\":\"user\",\"content\":" + common::jsonStringify(userText) + "}";
        std::string messagesJson = "[" + sys;
        if (history != "[]" && history.size() > 2) {
            std::string h2 = history;
            if (h2.front() == '[' && h2.back() == ']') h2 = h2.substr(1, h2.size() - 2);
            if (!h2.empty()) messagesJson += "," + h2;
        }
        messagesJson += "," + um + "]";

        bool noHistory = (history.empty() || history == "[]" || history.size() < 3);
        bool cacheable = noHistory && (task == "explain" || task == "debug");
        std::string ckey = common::sha1("v1|" + task + "|" + model + "|" + sysText + "|" + userText);
        std::string cached;
        bool cacheHit = cacheable && aiCacheGet(ckey, cached);

        std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nConnection: close\r\nCache-Control: no-cache\r\n\r\n";
        llmSendRaw(fd, head);
        std::string answer;
        if (cacheHit) {
            // 命中本地缓存：直接回放答案（不计算力点），并标注 cache=1
            size_t chunk = 1200;
            for (size_t off = 0; off < cached.size(); off += chunk) {
                std::string part = cached.substr(off, chunk);
                llmSendSSE(fd, "{\"message\":{\"content\":" + common::jsonStringify(part) + "}}");
            }
            aiquota::Info q2 = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
            std::map<std::string, std::string> meta;
            meta["type"] = "meta";
            meta["cost"] = "0";
            meta["cache"] = "1";
            meta["balance"] = std::to_string(aiquota::remaining(q2));
            meta["prompt_tokens"] = "0";
            meta["completion_tokens"] = "0";
            meta["windowQuota"] = std::to_string(q2.windowQuota);
            meta["windowUsed"] = std::to_string(q2.windowUsed);
            meta["monthQuota"] = std::to_string(q2.monthQuota);
            meta["monthUsed"] = std::to_string(q2.monthUsed);
            meta["extra"] = std::to_string(q2.extra);
            llmSendSSE(fd, common::jsonStringify(meta));
            answer = cached;
        } else {
            llmRunAgent(fd, dataStore, user->id, model, messagesJson, _remain, allowTools, task, &answer, effort);
            if (cacheable && !answer.empty()) {
                std::string ctitle = task == "debug" ? "代码诊断缓存" : "思路讲解缓存";
                std::string cpid = req.count("problemId") ? common::trim(req["problemId"]) : "";
                if (!cpid.empty()) {
                    Problem* cp = dataStore->findProblemById(cpid);
                    if (cp) ctitle = cp->id + " " + cp->title;
                }
                aiCachePut(ckey, answer, ctitle);
            }
        }
        // 使用历史：debug / explain 每次跑完存一条，方便回看
        if ((task == "debug" || task == "explain") && !answer.empty()) {
            aitools::RunRow rr;
            rr.id = common::genId();
            rr.userId = user->id;
            rr.type = task;
            rr.time = common::getTimeStr();
            rr.question = userText;
            rr.answer = answer;
            std::string pid = req.count("problemId") ? common::trim(req["problemId"]) : "";
            rr.pid = pid;
            std::string pname;
            if (!pid.empty()) {
                Problem* pp = dataStore->findProblemById(pid);
                if (pp) pname = pp->id + " " + pp->title;
            }
            if (task == "debug") {
                rr.title = (pname.empty() ? std::string("代码诊断") : ("诊断 " + pname));
                std::string filePath = req.count("filePath") ? common::trim(req["filePath"]) : "";
                if (!filePath.empty()) rr.title += " · " + filePath;
            } else {
                rr.title = (pname.empty() ? std::string("思路讲解") : ("讲解 " + pname));
                if (pname.empty() && req.count("statement")) {
                    std::string st = common::trim(req["statement"]);
                    if (st.size() > 24) st = st.substr(0, 24) + "...";
                    rr.title += " · " + st;
                }
            }
            aitools::saveRun(rr);
        }
        return;
    }
    if (path == "/llm/api/tools") {
        aiquota::Info qi = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
        std::string out = "{\"shell\":" + std::string(aitools::shellEnabled() ? "true" : "false") +
                          ",\"workspace\":\"data/aiws/u" + user->id + "/\"" +
                          ",\"spec\":" + aitools::specJson() +
                          ",\"quota\":" + aiquota::jsonOf(qi) + "}";
        llmSendJson(fd, 200, out);
        return;
    }
    // ---------- 读取工作区文件（上传后回填文本） ----------
    if (path.compare(0, 14, "/llm/api/file?") == 0 || path == "/llm/api/file") {
        std::string name = common::urlDecode(path.substr(path.find("name=") == std::string::npos ? path.size() : path.find("name=") + 5));
        if (name.find("&") != std::string::npos) name = name.substr(0, name.find("&"));
        if (name.empty() || name.find("..") != std::string::npos || name.find(':') != std::string::npos) {
            llmSendJson(fd, 400, "{\"error\":\"bad name\"}");
            return;
        }
        std::string full = aitools::workspaceDir(user->id) + name;
        if (!common::fileExists(full)) { llmSendJson(fd, 404, "{\"error\":\"not found\"}"); return; }
        std::string c = common::readFile(full);
        if (c.size() > 300000) c = c.substr(0, 300000);
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
                           std::to_string(c.size()) + "\r\nConnection: close\r\n\r\n" + c;
        llmSendRaw(fd, resp);
        return;
    }
    // ---------- 上传文件到 AI 工作区 ----------
    if (path.compare(0, 11, "/llm/upload") == 0) {
        std::string fname = "upload.bin";
        size_t q = path.find("name=");
        if (q != std::string::npos) {
            std::string raw = path.substr(q + 5);
            size_t amp = raw.find('&');
            if (amp != std::string::npos) raw = raw.substr(0, amp);
            fname = common::urlDecode(raw);
        }
        std::string saved;
        if (body.empty()) { llmSendJson(fd, 400, "{\"error\":\"文件为空\"}"); return; }
        if (!aitools::saveUpload(user->id, fname, body, saved)) {
            llmSendJson(fd, 400, "{\"error\":\"保存失败（单文件上限 4MB）\"}");
            return;
        }
        std::map<std::string, std::string> o;
        o["ok"] = "true";
        o["path"] = saved;
        o["size"] = std::to_string(body.size());
        llmSendJson(fd, 200, common::jsonStringify(o));
        return;
    }
    if (path == "/llm/api/redeem") {
        auto req = common::jsonParseObject(body);
        std::string code = req.count("code") ? req["code"] : "";
        int amount = 0;
        if (code.empty() || !tokenManager->redeemCode(user->id, code, amount)) {
            llmSendJson(fd, 400, "{\"success\":false,\"error\":\"\u65e0\u6548\u6216\u5df2\u4f7f\u7528\u7684\u5151\u6362\u7801\"}");
            return;
        }
        // 兑换码改为直充额外算力点（新的算力点体系，Token 不再参与额度）
        aiquota::addExtra(user->id, (long long)amount);
        aiquota::Info qi = aiquota::info(user->id, user->aiWindowQuota, user->aiMonthQuota);
        std::map<std::string, std::string> o;
        o["success"] = "true";
        o["extra"] = std::to_string(qi.extra);
        o["added"] = std::to_string(amount);
        llmSendJson(fd, 200, common::jsonStringify(o));
        return;
    }
    if (path == "/llm/api/generate-code") {
        if (!adminperm::has(user, "ai")) { llmSendJson(fd, 403, "{\"error\":\"需要 AI 管理权限\"}"); return; }
        auto req = common::jsonParseObject(body);
        int amount = req.count("amount") ? common::stringToInt(req["amount"]) : 100;
        int days = req.count("days") ? common::stringToInt(req["days"]) : 30;
        if (amount < 1) amount = 1;
        std::string code = tokenManager->generateTokenCode(amount, days);
        llmSendJson(fd, 200, "{\"code\":\"" + code + "\",\"amount\":" + std::to_string(amount) + "}");
        return;
    }

    if (path == "/llm/api/list-codes") {
        if (!adminperm::has(user, "ai")) { llmSendJson(fd, 403, "{\"error\":\"需要 AI 管理权限\"}"); return; }
        llmSendJson(fd, 200, tokenManager->listCodesJson());
        return;
    }
    // ---------- 对话历史 ----------
    if (path.compare(0, 16, "/llm/api/history") == 0 && path.find("/save") == std::string::npos && path.find("/delete") == std::string::npos && path.find("/rename") == std::string::npos) {
        std::string convId;
        size_t q = path.find('?');
        if (q != std::string::npos) {
            std::string query = path.substr(q + 1);
            auto qparts = common::split(query, '&');
            for (auto& kv : qparts) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos && kv.substr(0, eq) == "id") convId = kv.substr(eq + 1);
            }
        }
        std::vector<std::map<std::string, std::string> > convs;
        std::string histFile = "./data/chat_hist.json";
        if (common::fileExists(histFile)) {
            std::string content = common::readFile(histFile);
            auto lines = common::split(content, '\n');
            for (auto& line : lines) {
                if (line.empty()) continue;
                auto obj = common::jsonParseObject(line);
                if (obj.count("userId") && obj["userId"] == user->id) {
                    if (!convId.empty()) {
                        if (obj.count("id") && obj["id"] == convId) {
                            llmSendJson(fd, 200, line);
                            return;
                        }
                    } else {
                        std::map<std::string, std::string> m;
                        m["id"] = obj.count("id") ? obj["id"] : "";
                        m["title"] = obj.count("title") ? obj["title"] : "";
                        m["time"] = obj.count("time") ? obj["time"] : "";
                        m["count"] = obj.count("count") ? obj["count"] : "0";
                        m["model"] = obj.count("model") ? obj["model"] : "";
                        convs.push_back(m);
                    }
                }
            }
        }
        if (!convId.empty()) { llmSendJson(fd, 404, "{\"error\":\"not found\"}"); return; }
        std::string arr = "[";
        for (size_t i = 0; i < convs.size(); i++) { if (i) arr += ","; arr += common::jsonStringify(convs[i]); }
        arr += "]";
        llmSendJson(fd, 200, arr);
        return;
    }
    if (path == "/llm/api/history/save") {
        auto req = common::jsonParseObject(body);
        std::string convId = req.count("id") ? req["id"] : "";
        std::string title = req.count("title") ? req["title"] : "";
        std::string messages = req.count("messages") ? req["messages"] : "[]";
        std::string convModel = req.count("model") ? req["model"] : "";
        if (convId.empty()) { llmSendJson(fd, 400, "{\"error\":\"no id\"}"); return; }
        int msgCount = 0; { size_t pp = 0; while ((pp = messages.find("\"role\"", pp)) != std::string::npos) { msgCount++; pp += 7; } }
        std::string histFile = "./data/chat_hist.json";
        std::string content = common::fileExists(histFile) ? common::readFile(histFile) : "";
        auto lines = common::split(content, '\n');
        std::string out;
        for (auto& line : lines) {
            if (line.empty()) continue;
            auto obj = common::jsonParseObject(line);
            bool sameUser = obj.count("userId") && obj["userId"] == user->id;
            bool sameId = obj.count("id") && obj["id"] == convId;
            if (sameUser && sameId) continue;
            out += line + "\n";
        }
        std::map<std::string, std::string> m;
        m["userId"] = user->id;
        m["id"] = convId;
        m["title"] = title;
        m["time"] = common::getTimeStr();
        m["count"] = std::to_string(msgCount);
        if (!convModel.empty()) m["model"] = convModel;
        m["messages"] = messages;
        out += common::jsonStringify(m) + "\n";
        common::writeFile(histFile, out);
        llmSendJson(fd, 200, "{\"ok\":\"true\"}");
        return;
    }

    if (path == "/llm/api/history/rename") {
        auto req = common::jsonParseObject(body);
        std::string convId = req.count("id") ? req["id"] : "";
        std::string title = req.count("title") ? common::trim(req["title"]) : "";
        if (convId.empty() || title.empty()) { llmSendJson(fd, 400, "{\"error\":\"参数不完整\"}"); return; }
        if (title.size() > 60) title = title.substr(0, 60);
        std::string histFile = "./data/chat_hist.json";
        if (!common::fileExists(histFile)) { llmSendJson(fd, 404, "{\"error\":\"not found\"}"); return; }
        std::string content = common::readFile(histFile);
        auto lines = common::split(content, '\n');
        std::string out;
        bool hit = false;
        for (auto& line : lines) {
            if (line.empty()) continue;
            auto obj = common::jsonParseObject(line);
            if (obj.count("userId") && obj["userId"] == user->id && obj.count("id") && obj["id"] == convId) {
                obj["title"] = title;
                out += common::jsonStringify(obj) + "\n";
                hit = true;
            } else {
                out += line + "\n";
            }
        }
        common::writeFile(histFile, out);
        llmSendJson(fd, 200, hit ? "{\"ok\":true}" : "{\"error\":\"not found\"}");
        return;
    }

    if (path == "/llm/api/history/delete") {
        auto req = common::jsonParseObject(body);
        std::string convId = req.count("id") ? req["id"] : "";
        if (convId.empty()) { llmSendJson(fd, 400, "{\"error\":\"no id\"}"); return; }
        std::string histFile = "./data/chat_hist.json";
        if (!common::fileExists(histFile)) { llmSendJson(fd, 200, "{\"ok\":\"true\"}"); return; }
        std::string content = common::readFile(histFile);
        auto lines = common::split(content, '\n');
        std::string out;
        for (auto& line : lines) {
            if (line.empty()) continue;
            auto obj = common::jsonParseObject(line);
            bool sameUser = obj.count("userId") && obj["userId"] == user->id;
            bool sameId = obj.count("id") && obj["id"] == convId;
            if (sameUser && sameId) continue;
            out += line + "\n";
        }
        common::writeFile(histFile, out);
        llmSendJson(fd, 200, "{\"ok\":\"true\"}");
        return;
    }
    llmSendJson(fd, 404, "{\"error\":\"Not found\"}");
}


// ====== \u901a\u7528\u6d88\u606f\u5904\u7406 ======
void HttpServer::handleGameMessage(const std::string& clientId, const std::map<std::string, std::string>& msgData) {
    if (gameRegistry->routeMessage(clientId, msgData)) return;
    std::string roomId = msgData.count("roomId") ? msgData.at("roomId") : "default";
    std::string action = msgData.count("action") ? msgData.at("action") : "";
    std::string gameType = msgData.count("gameType") ? msgData.at("gameType") : "default";

    if (action == "join") {
        std::string name = msgData.count("playerName") ? msgData.at("playerName") : clientId.substr(0, 8);
        roomManager->joinRoom(clientId, roomId, gameType, name);
        gameRegistry->onLockPlayerJoin(clientId, roomId, gameType, name);
        std::map<std::string, std::string> resp;
        resp["type"] = "system";
        resp["action"] = "joined";
        resp["roomId"] = roomId;
        resp["clientId"] = clientId;
        resp["playerName"] = name;
        resp["message"] = "You have joined room " + roomId;
        sendToClient(clientId, common::jsonStringify(resp));
        std::map<std::string, std::string> notify;
        notify["type"] = "system";
        notify["action"] = "player_joined";
        notify["clientId"] = clientId;
        notify["playerName"] = name;
        notify["roomId"] = roomId;
        broadcastToRoom(roomId, common::jsonStringify(notify), clientId);
        sendPlayerList(roomId);
        return;
    }

    if (action == "leave") {
        roomManager->leaveRoom(clientId);
        std::map<std::string, std::string> notify;
        notify["type"] = "system";
        notify["action"] = "player_left";
        notify["clientId"] = clientId;
        notify["roomId"] = roomId;
        gameRegistry->onPlayerLeave(clientId, roomId);
        broadcastToRoom(roomId, common::jsonStringify(notify), clientId);
        sendPlayerList(roomId);
        return;
    }

    if (action == "chat") {
        std::string text = msgData.count("message") ? msgData.at("message") : "";
        if (!text.empty()) {
            std::map<std::string, std::string> chat;
            chat["type"] = "chat";
            chat["senderId"] = clientId;
            chat["message"] = text;
            chat["timestamp"] = std::to_string(std::time(nullptr));
            broadcastToRoomIncludingSender(roomId, common::jsonStringify(chat));
        }
        return;
    }

    std::map<std::string, std::string> forward = msgData;
    forward["senderId"] = clientId;
    forward["timestamp"] = std::to_string(std::time(nullptr));
    broadcastToRoomIncludingSender(roomId, common::jsonStringify(forward));
}

void HttpServer::sendPlayerList(const std::string& roomId) {
    auto room = roomManager->getRoom(roomId);
    if (!room) return;
    std::map<std::string, std::string> data;
    data["type"] = "system";
    data["action"] = "player_list";
    data["roomId"] = roomId;
    std::string list = "[";
    for (size_t i = 0; i < room->clients.size(); ++i) {
        if (i) list += ",";
        std::string name = room->playerNames[room->clients[i]];
        size_t pos = 0;
        while ((pos = name.find('"', pos)) != std::string::npos) {
            name.replace(pos, 1, "\\\"");
            pos += 2;
        }
        pos = 0;
        while ((pos = name.find('\\', pos)) != std::string::npos) {
            name.replace(pos, 1, "\\\\");
            pos += 2;
        }
        list += "{\"id\":\"" + room->clients[i] + "\",\"name\":\"" + name + "\"}";
    }
    list += "]";
    data["players"] = list;
    broadcastToRoomIncludingSender(roomId, common::jsonStringify(data));
}


// ========== WebSocket 处理 ==========
void HttpServer::handleWebSocketHandshake(SOCKET fd, const std::map<std::string, std::string>& headers) {
    std::string wsKey;
    auto it = headers.find("sec-websocket-key");
    if (it != headers.end()) wsKey = it->second;
    else {
        it = headers.find("Sec-WebSocket-Key");
        if (it != headers.end()) wsKey = it->second;
    }
    if (wsKey.empty()) { closesocket(fd); return; }

    std::string acceptInput = wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string sha1Result = sha1(acceptInput);
    std::string acceptKey = base64Encode(sha1Result);
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n";
    int sent = send(fd, response.c_str(), (int)response.length(), 0);
    if (sent <= 0) { closesocket(fd); return; }

    std::string clientId = common::genId();
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        socketClients[fd] = clientId;
        clientSockets[clientId] = fd;
    }
    std::cout << "[WS] connected, clientId: " << clientId << std::endl;

    roomManager->joinRoom(clientId, "default", "default");

    char buffer[4096];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(fd, buffer, sizeof(buffer)-1, 0);
        if (n > 0) {
            std::string data(buffer, n);
            std::string msg = decodeWebSocketFrame(data);
            if (!msg.empty()) {
                std::cout << "[WS] message from " << clientId << ": " << msg.substr(0, 100) << (msg.length()>100?"...":"") << std::endl;
                auto msgData = common::jsonParseObject(msg);
                bool isGame = (msgData.count("type") && msgData.at("type") == "game");
                if (isGame) {
                    handleGameMessage(clientId, msgData);
                } else {
                    if (wsHandler) {
                        wsHandler(clientId, msg, [this, clientId](const std::string& reply) {
                            this->sendToClient(clientId, reply);
                        });
                    }
                }
            }
        } else if (n == 0) {
            std::cout << "[WS] client disconnected: " << clientId << std::endl;
            break;
        } else {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else {
                std::cout << "[WS] recv error: " << error << std::endl;
                break;
            }
        }
    }

    roomManager->removeClient(clientId);
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        socketClients.erase(fd);
        clientSockets.erase(clientId);
    }
    closesocket(fd);
}

// ========== HTTP 客户端处理 ==========

// Read the full HTTP request (headers + body per Content-Length).
// The previous code only did one recv() into an 8KB buffer, so any
// multipart/file body larger than ~8KB was truncated and the socket
// closed while the client was still sending -> "connection reset".
static void readHttpRequest(SOCKET fd, std::string& request) {
    char buffer[65536];
    const long long MAX_TOTAL = 8LL * 1024 * 1024 * 1024;
    while (request.size() < (size_t)MAX_TOTAL) {
        int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        request.append(buffer, n);
        size_t hdrEnd = request.find("\r\n\r\n");
        if (hdrEnd == std::string::npos) continue;
        std::string headerArea = request.substr(0, hdrEnd);
        std::string lower = headerArea;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        size_t clPos = lower.find("content-length:");
        size_t need = 0;
        if (clPos != std::string::npos) {
            size_t le = headerArea.find("\r\n", clPos);
            std::string cl = headerArea.substr(clPos + 15, le == std::string::npos ? std::string::npos : le - (clPos + 15));
            need = (size_t)common::stringToInt(common::trim(cl));
        }
        size_t bodyStart = hdrEnd + 4;
        if (request.size() >= bodyStart + need) break;
    }
}

void HttpServer::handleClient(SOCKET clientFd, const std::string& clientIp) {
    // ---------- 过速检测 & IP 封禁 ----------
    int _gate = security::gateRequest(clientIp);
    if (_gate == 403) {
        sendResponse(clientFd, 403, "Forbidden",
            std::map<std::string, std::string>(),
            "<html><body style='font-family:system-ui;text-align:center;padding:60px;'><h1>403</h1><p>\u4f60\u7684 IP \u5df2\u88ab\u5c01\u7981\uff0c\u8bf7\u7a0d\u540e\u518d\u8bd5\u3002</p></body></html>");
        closesocket(clientFd);
        return;
    }
    if (_gate == 429) {
        sendResponse(clientFd, 429, "Too Many Requests",
            std::map<std::string, std::string>(),
            "<html><body style='font-family:system-ui;text-align:center;padding:60px;'><h1>429</h1><p>\u8bf7\u6c42\u8fc7\u4e8e\u9891\u7e41\uff0c\u8bf7\u7a0d\u540e\u518d\u8bd5\u3002</p></body></html>");
        closesocket(clientFd);
        return;
    }
    std::string request;
    readHttpRequest(clientFd, request);
    if (request.empty()) { closesocket(clientFd); return; }

    if (request.find("Upgrade: websocket") != std::string::npos ||
        request.find("upgrade: websocket") != std::string::npos) {
        std::map<std::string, std::string> headers;
        std::istringstream iss(request);
        std::string line;
        std::getline(iss, line);
        while (std::getline(iss, line) && line != "\r") {
            if (line.empty() || line == "\r") break;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t pos = line.find(":");
            if (pos != std::string::npos) {
                std::string key = common::trim(line.substr(0, pos));
                std::string val = common::trim(line.substr(pos + 1));
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                headers[key] = val;
            }
        }
        std::thread([this, clientFd, headers]() {
            handleWebSocketHandshake(clientFd, headers);
        }).detach();
        return;
    }

    std::string method, path, body;
    std::map<std::string, std::string> headers;
    std::istringstream iss(request);
    std::string firstLine;
    if (!std::getline(iss, firstLine)) { closesocket(clientFd); return; }
    std::vector<std::string> parts = common::split(firstLine, ' ');
    if (parts.size() < 3) { closesocket(clientFd); return; }
    method = parts[0];
    path = parts[1];
    if (path.empty()) path = "/";

    std::string line;
    while (std::getline(iss, line) && line != "\r") {
        if (line.empty() || line == "\r") break;
        size_t pos = line.find(":");
        if (pos != std::string::npos) {
            std::string key = common::trim(line.substr(0, pos));
            std::string val = common::trim(line.substr(pos + 1));
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            headers[key] = val;
        }
    }
    size_t pos = request.find("\r\n\r\n");
    if (pos != std::string::npos) body = request.substr(pos + 4);

    // LLM 路由
    if (path.find("/llm") == 0 || path.find("/v1/") == 0 || path == "/v1" || path.find("/api/ai/") == 0) {
        handleLLMRequest(clientFd, path, headers, body);
        closesocket(clientFd);
        return;
    }

    // 静态文件路径
    std::string sessionId;
    auto _it = headers.find("cookie");
    if (_it != headers.end()) {
        size_t _p = _it->second.find("SESSION=");
        if (_p != std::string::npos) {
            size_t _e = _it->second.find(";", _p);
            if (_e == std::string::npos) _e = _it->second.length();
            sessionId = _it->second.substr(_p + 8, _e - _p - 8);
        }
    }

    auto serveStatic = [&](const std::string& baseDir, const std::string& relRaw) {
        std::string rel = common::urlDecode(relRaw);
        if (rel.find("..") != std::string::npos || rel.find("\\") != std::string::npos ||
            rel.find(":") != std::string::npos || rel.find("//") != std::string::npos) {
            sendResponse(clientFd, 403, "Forbidden", std::map<std::string, std::string>(),
                "<html><body><h1>403 Forbidden</h1></body></html>");
            closesocket(clientFd);
            return;
        }
        for (size_t _k = 0; _k < rel.size(); ++_k) if ((unsigned char)rel[_k] < 0x20) {
            sendResponse(clientFd, 403, "Forbidden", std::map<std::string, std::string>(),
                "<html><body><h1>403 Forbidden</h1></body></html>");
            closesocket(clientFd);
            return;
        }
        if (rel.empty() || rel.back() == '/') rel += "index.html";
        size_t _sl = rel.find('/');
        std::string _first = (_sl == std::string::npos) ? rel : rel.substr(0, _sl);
        bool _isNum = !_first.empty();
        for (char _c : _first) if (!(_c >= '0' && _c <= '9')) { _isNum = false; break; }
        if (_isNum) {
            std::string _uid;
            Session* _sess = SessionManager::instance().getSession(sessionId);
            if (_sess) _uid = _sess->userId;
            bool _ok = (_uid == _first);
            if (!_ok) {
                User* _u = DataStore::instance().findUserById(_uid);
                if (_u && _u->role == ROLE_ADMIN) _ok = true;
            }
            if (!_ok) {
                sendResponse(clientFd, 403, "Forbidden", std::map<std::string, std::string>(),
                    "<html><body><h1>403 Forbidden</h1></body></html>");
                closesocket(clientFd);
                return;
            }
        }
        sendFile(clientFd, baseDir + "/" + rel);
        closesocket(clientFd);
    };

    if (path.find("/files/") == 0 || path.find("/static/") == 0) {
        // 剥离 query string（如 /static/app.js?v=1），否则会去找不存在的文件名
        std::string sp = path;
        size_t sq = sp.find('?');
        if (sq != std::string::npos) sp = sp.substr(0, sq);
        if (sp.find("/files/") == 0) { serveStatic("./files", sp.substr(7)); return; }
        serveStatic("./static", sp.substr(8));
        return;
    }

    std::map<std::string, std::string> respHeaders;
    headers["x-client-ip"] = clientIp; // 注入客户端IP供处理函数使用
    std::string content = handler(method, path, body, headers, respHeaders, sessionId);
    sendResponse(clientFd, 200, "OK", respHeaders, content);
    closesocket(clientFd);
}

// ========== 主函数 ==========
void HttpServer::sendResponse(SOCKET fd, int statusCode, const std::string& statusMsg,
                              const std::map<std::string, std::string>& headers,
                              const std::string& body) {
    // X-Status：处理器可指定真实状态码（如 206 Partial Content / 416）
    std::string statusMsgEff = statusMsg;
    for (const auto& h : headers) {
        std::string key = h.first;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key == "x-status") {
            int sc = atoi(h.second.c_str());
            if (sc >= 100 && sc <= 599) {
                statusCode = sc;
                if (sc == 206) statusMsgEff = "Partial Content";
                else if (sc == 416) statusMsgEff = "Range Not Satisfiable";
                else if (sc == 200) statusMsgEff = "OK";
            }
        }
    }
    std::string response = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusMsgEff + "\r\n";
    std::string contentType = "text/html; charset=utf-8";
    auto cit = headers.find("Content-Type");
    if (cit != headers.end()) contentType = cit->second;
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    response += "Connection: close\r\n";
    for (const auto& h : headers) {
        std::string k = h.first;
        std::string key = k;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key == "content-type" || key == "content-length" || key == "connection" || key == "x-status") continue;
        response += k + ": " + h.second + "\r\n";
    }
    response += "\r\n";
    response += body;
    // 大文件（打包下载）单次 send 可能只发出部分数据，这里循环发完
    size_t sent = 0;
    while (sent < response.length()) {
        int n = send(fd, response.c_str() + sent, (int)(response.length() - sent), 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

void HttpServer::sendFile(SOCKET fd, const std::string& path) {
    if (!common::fileExists(path)) {
        sendResponse(fd, 404, "Not Found", {}, "<html><body><h1>404 Not Found</h1></body></html>");
        return;
    }
    std::string content = common::readFile(path);
    std::map<std::string, std::string> headers;
    std::string mime = getMimeType(path);
    if (mime == "text/html") mime = "text/html; charset=utf-8";
    headers["Content-Type"] = mime;
    sendResponse(fd, 200, "OK", headers, content);
}

std::string HttpServer::getMimeType(const std::string& path) {
    std::string ext = common::getFileExtension(path);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto it = g_extraMimeTypes.find("." + ext);
    if (it != g_extraMimeTypes.end()) return it->second;
    return "application/octet-stream";
}

std::string HttpServer::recvAll(SOCKET fd, int timeoutMs) {
    std::string result;
    char buffer[8192];
    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    while (true) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        int ret = select(0, &readfds, NULL, NULL, &tv);
        if (ret <= 0) break;
        int n = recv(fd, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        result += buffer;
        if (result.find("\r\n\r\n") != std::string::npos) break;
    }
    return result;
}

void HttpServer::setNonBlocking(SOCKET fd) {
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
}

void HttpServer::workerLoop() {
    while (running) {
        SOCKET clientFd = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(acceptMutex);
            if (!running) break;
            sockaddr_in clientAddr;
            int len = sizeof(clientAddr);
            clientFd = accept(serverFd, (sockaddr*)&clientAddr, &len);
        }
        if (clientFd != INVALID_SOCKET) {
            std::string clientIp;
            sockaddr_in peerAddr;
            int peerLen = sizeof(peerAddr);
            if (getpeername(clientFd, (sockaddr*)&peerAddr, &peerLen) == 0) {
                char ipBuf[64] = {0};
                const char* ipStr = inet_ntoa(peerAddr.sin_addr);
                if (ipStr) { strncpy(ipBuf, ipStr, sizeof(ipBuf) - 1); }
                clientIp = ipBuf;
            }
            std::thread([this, clientFd, clientIp]() { handleClient(clientFd, clientIp); }).detach();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void HttpServer::run() {
    if (!running) { std::cerr << "Server not started" << std::endl; return; }
    std::cout << "========================================" << std::endl;
    std::cout << "  OI Station Server (HTTP + WebSocket)" << std::endl;
    std::cout << "  Port: " << port << std::endl;
    std::cout << "  AI Model: " << OLLAMA_MODEL << " (DeepSeek)" << std::endl;
    std::cout << "  LLM URL: http://localhost:" << port << "/llm/" << std::endl;
    std::cout << "  Static files: /files/ -> ./files/" << std::endl;
    std::cout << "  WebSocket: ENABLED" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

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

    int threadCount = 8;
    for (int i = 0; i < threadCount; ++i) {
        workerThreads.emplace_back(&HttpServer::workerLoop, this);
    }
    for (auto& t : workerThreads) if (t.joinable()) t.join();
}

void HttpServer::stop() {
    running = false;
    if (serverFd != INVALID_SOCKET) { closesocket(serverFd); serverFd = INVALID_SOCKET; }
    for (auto& t : workerThreads) if (t.joinable()) t.join();
    battleTickRunning = false;
    if (battleTickThread.joinable()) battleTickThread.join();
}

bool HttpServer::parseRequest(SOCKET fd, std::string& method, std::string& path,
                              std::map<std::string, std::string>& headers,
                              std::string& body) { return true; }
void HttpServer::wsMessageLoop(SOCKET fd, const std::string& clientId) { }

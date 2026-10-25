#ifndef OI_SESSION_H
#define OI_SESSION_H

#include <string>
#include <map>
#include <ctime>

class Session {
public:
    std::string id;
    std::string userId;
    time_t expireTime;
};

class SessionManager {
public:
    static SessionManager& instance();
    
    std::string createSession(const std::string& userId);
    Session* getSession(const std::string& sessionId);
    bool destroySession(const std::string& sessionId);
    void cleanExpired();
    bool isValid(const std::string& sessionId);
    // 该用户是否有未过期的登录会话（用于「在线」状态）
    bool hasActiveSession(const std::string& userId);
    
private:
    SessionManager() {}
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    
    std::map<std::string, Session> sessions;
};

#endif
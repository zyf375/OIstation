#include "../include/session.h"
#include "../include/common.h"
#include <algorithm>

SessionManager& SessionManager::instance() {
    static SessionManager sm;
    return sm;
}

std::string SessionManager::createSession(const std::string& userId) {
    std::string sid = common::genId();
    Session s;
    s.id = sid;
    s.userId = userId;
    s.expireTime = time(NULL) + 3600 * 2; // 2小时
    sessions[sid] = s;
    return sid;
}

Session* SessionManager::getSession(const std::string& sessionId) {
    auto it = sessions.find(sessionId);
    if (it == sessions.end()) return nullptr;
    if (it->second.expireTime < time(NULL)) {
        sessions.erase(it);
        return nullptr;
    }
    return &it->second;
}

bool SessionManager::destroySession(const std::string& sessionId) {
    auto it = sessions.find(sessionId);
    if (it == sessions.end()) return false;
    sessions.erase(it);
    return true;
}

void SessionManager::cleanExpired() {
    auto it = sessions.begin();
    while (it != sessions.end()) {
        if (it->second.expireTime < time(NULL)) {
            it = sessions.erase(it);
        } else {
            ++it;
        }
    }
}

bool SessionManager::isValid(const std::string& sessionId) {
    return getSession(sessionId) != nullptr;
}

bool SessionManager::hasActiveSession(const std::string& userId) {
    time_t now = time(NULL);
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->second.userId == userId && it->second.expireTime >= now) return true;
    }
    return false;
}

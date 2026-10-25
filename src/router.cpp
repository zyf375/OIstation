#include "../include/router.h"
#include "../include/common.h"
#include "../include/session.h"
#include "../include/models.h"
#include "../include/admin_perm.h"

static User* routerCurrentUser(const std::map<std::string, std::string>& headers) {
    auto it = headers.find("cookie");
    if (it == headers.end()) return nullptr;
    const std::string& cookie = it->second;
    size_t pos = cookie.find("SESSION=");
    if (pos == std::string::npos) return nullptr;
    size_t start = pos + 8;
    size_t end = cookie.find(';', start);
    if (end == std::string::npos) end = cookie.length();
    std::string sid = cookie.substr(start, end - start);
    Session* sess = SessionManager::instance().getSession(sid);
    if (!sess) return nullptr;
    return DataStore::instance().findUserById(sess->userId);
}

Router::Router() {}

void Router::get(const std::string& path, RequestHandler handler) {
    Route route;
    route.path = path;
    route.handler = handler;
    route.isWildcard = path.find('*') != std::string::npos;
    if (route.isWildcard) {
        route.prefix = path.substr(0, path.find('*'));
    }
    getRoutes.push_back(route);
}

void Router::post(const std::string& path, RequestHandler handler) {
    Route route;
    route.path = path;
    route.handler = handler;
    route.isWildcard = path.find('*') != std::string::npos;
    if (route.isWildcard) {
        route.prefix = path.substr(0, path.find('*'));
    }
    postRoutes.push_back(route);
}

static bool wildMatch(const std::string& routePath, const std::string& requestPath) {
    size_t star = routePath.find('*');
    if (star == std::string::npos) return routePath == requestPath;
    std::string prefix = routePath.substr(0, star);
    std::string suffix = routePath.substr(star + 1);
    if (requestPath.find(prefix) != 0) return false;
    if (suffix.empty()) return true;
    if (requestPath.size() < prefix.size() + suffix.size()) return false;
    return requestPath.compare(requestPath.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool Router::matchPath(const std::string& routePath, const std::string& requestPath, std::string& matchedPath) {
    if (routePath == requestPath) {
        matchedPath = routePath;
        return true;
    }
    
    // 通配符匹配
    if (wildMatch(routePath, requestPath)) {
        matchedPath = routePath;
        return true;
    }
    
    return false;
}

RequestHandler Router::findHandler(const std::vector<Route>& routes, const std::string& path, std::string& matchedPath) {
    // 精确匹配优先
    for (const auto& route : routes) {
        if (route.path == path) {
            matchedPath = route.path;
            return route.handler;
        }
    }
    
    // 通配符匹配
    for (const auto& route : routes) {
        if (route.isWildcard && wildMatch(route.path, path)) {
            matchedPath = route.path;
            return route.handler;
        }
    }
    
    return nullptr;
}

std::string Router::route(const std::string& method, const std::string& path,
                          const std::string& body,
                          const std::map<std::string, std::string>& headers,
                          std::map<std::string, std::string>& respHeaders,
                          std::string& sessionId) {
    std::string matchedPath;
    RequestHandler handler = nullptr;
    
    // 匹配时剥离 query string（如 /a/b?x=1 → /a/b），但 handler 仍收到完整 path 自行解析
    std::string matchPath = path;
    size_t _q = path.find('?');
    if (_q != std::string::npos) matchPath = path.substr(0, _q);
    
    if (method == "GET") {
        handler = findHandler(getRoutes, matchPath, matchedPath);
    } else if (method == "POST") {
        handler = findHandler(postRoutes, matchPath, matchedPath);
    }
    
    if (handler) {
        // 管理后台统一权限闸门：按页面所属模块校验细粒度权限
        bool isAdminPath = (matchPath == "/admin" || matchPath.compare(0, 7, "/admin/") == 0);
        if (isAdminPath) {
            User* u = routerCurrentUser(headers);
            if (!u) return "<script>window.location.href='/login'</script>";
            if (!adminperm::pathAllowed(u, matchPath)) {
                return "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>权限不足</title>"
                       "<link rel='stylesheet' href='/static/style.css'></head><body>"
                       "<div class='container' style='max-width:560px;margin:60px auto;text-align:center;'>"
                       "<div style='font-size:42px;'>🔒</div><h2>权限不足</h2>"
                       "<p style='color:#7f8c8d;'>你的账号没有访问该管理页面的权限。请联系管理员分配。</p>"
                       "<a class='btn' href='/admin'>返回管理面板</a> "
                       "<a class='btn btn-outline' href='/'>返回首页</a></div></body></html>";
            }
        }
        return handler(method, path, body, headers, respHeaders, sessionId);
    }
    
    // 404
    return "<html><head><title>404 Not Found</title>"
           "<style>body{font-family:system-ui;text-align:center;padding:50px;}</style>"
           "</head><body><h1>404</h1><p>页面不存在</p>"
           "<a href='/'>返回首页</a></body></html>";
}
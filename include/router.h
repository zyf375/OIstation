#ifndef OI_ROUTER_H
#define OI_ROUTER_H

#include <map>
#include <string>
#include <vector>
#include <functional>
#include "../include/server.h"

class Router {
public:
    Router();
    
    void get(const std::string& path, RequestHandler handler);
    void post(const std::string& path, RequestHandler handler);
    std::string route(const std::string& method, const std::string& path,
                      const std::string& body,
                      const std::map<std::string, std::string>& headers,
                      std::map<std::string, std::string>& respHeaders,
                      std::string& sessionId);
    
private:
    struct Route {
        std::string path;
        RequestHandler handler;
        bool isWildcard;
        std::string prefix;
    };
    
    std::vector<Route> getRoutes;
    std::vector<Route> postRoutes;
    
    RequestHandler findHandler(const std::vector<Route>& routes, const std::string& path, std::string& matchedPath);
    bool matchPath(const std::string& routePath, const std::string& requestPath, std::string& matchedPath);
};

#endif
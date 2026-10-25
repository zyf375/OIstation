#include "../include/server.h"
#include "../include/router.h"
#include "../include/handlers.h"
#include "../include/models.h"
#include "../include/judge.h"
#include "../include/common.h"
#include "../include/security.h"
#include "../include/ai_providers.h"
#include "../include/ai_plans.h"
#include <iostream>
#include <signal.h>
#include <cstring>
#include <tlhelp32.h>
#include <windows.h>

HttpServer* g_server = nullptr;

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        std::cout << "\n正在关闭服务器..." << std::endl;
        if (g_server) {
            g_server->stop();
        }
        exit(0);
    }
    return TRUE;
}

void printBanner() {
    std::cout << "========================================" << std::endl;
    std::cout << "   ___   ___   _   _   ___   ___       " << std::endl;
    std::cout << "  |_  | |_  | | | | | |_  | |_  |      " << std::endl;
    std::cout << "    | |   | | | |_| |   | |   | |      " << std::endl;
    std::cout << "    | |   | | |  _  |   | |   | |      " << std::endl;
    std::cout << "    | |   | | | | | |   | |   | |      " << std::endl;
    std::cout << "    |_|   |_| |_| |_|   |_|   |_|      " << std::endl;
    std::cout << "                                        " << std::endl;
    std::cout << "     OI 小站 - 本地在线评测系统         " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // 设置控制台信号处理
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    SetConsoleOutputCP(CP_UTF8);

    // 关键：将工作目录切换到可执行文件所在目录，确保所有相对路径（如./temp, ./problems）都正确
    std::string exeDir = common::getExeDir();
    SetCurrentDirectoryA(exeDir.c_str());
    std::cout << "Working directory set to: " << exeDir << std::endl;

    printBanner();

    // AI 助手使用 DeepSeek 云端 API（无需本地大模型）

    // 检查评测环境
    if (!Judge::checkEnvironment()) {
        std::cerr << "警告: 评测环境配置不完整，但服务器仍可启动" << std::endl;
        std::cout << "请确保已安装 g++ 并创建了必要的目录" << std::endl;
        std::cout << std::endl;
    }

    // 创建数据目录（已在 loadAll 中创建，但提前创建也无妨）
    common::createDirectory("./data");
    common::createDirectory("./problems");
    common::createDirectory("./temp");

    // 加载数据
    std::cout << "正在加载数据..." << std::endl;
    DataStore::instance().loadAll();
    std::cout << "数据加载完成！" << std::endl;
    std::cout << "用户数: " << DataStore::instance().users.size() << std::endl;
    std::cout << "题目数: " << DataStore::instance().problems.size() << std::endl;
    std::cout << "提交数: " << DataStore::instance().submissions.size() << std::endl;
    std::cout << "帖子数: " << DataStore::instance().posts.size() << std::endl;
    std::cout << "比赛数: " << DataStore::instance().contests.size() << std::endl;
    std::cout << "题单数: " << DataStore::instance().problemSets.size() << std::endl;
    std::cout << "消息数: " << DataStore::instance().messages.size() << std::endl;
    std::cout << std::endl;

    security::loadBans();
    security::loadSecurityConfig();

    // AI 提供方 / 套餐初始化（缺失时自动生成默认配置）
    aiprov::ensureSeed();
    aiplan::ensureSeed();

    // 初始化路由
    Router router;
    Handlers::registerRoutes(router);

    // 设置端口
    int port = 8083;
    if (argc > 1) {
        port = common::stringToInt(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "无效端口号，使用默认端口 8083" << std::endl;
            port = 8083;
        }
    }

    // 启动服务器
    HttpServer server(port);
    g_server = &server;

    server.setHandler([&](const std::string& method, const std::string& path,
                          const std::string& body,
                          const std::map<std::string, std::string>& headers,
                          std::map<std::string, std::string>& respHeaders,
                          std::string& sessionId) -> std::string {
        return router.route(method, path, body, headers, respHeaders, sessionId);
    });

    if (!server.start()) {
        std::cerr << "无法启动服务器，端口 " << port << " 可能已被占用" << std::endl;
        return 1;
    }

    std::cout << "✅ 服务器启动成功！" << std::endl;
    std::cout << "🌐 访问地址: http://localhost:" << port << std::endl;
    std::cout << "👤 默认管理员: admin / admin123" << std::endl;
    std::cout << "按 Ctrl+C 停止服务器" << std::endl;
    std::cout << std::endl;

    server.run();

    // 保存数据
    std::cout << "正在保存数据..." << std::endl;
    DataStore::instance().saveAll();
    std::cout << "数据保存完成！" << std::endl;

    return 0;
}

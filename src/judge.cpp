#include "../include/judge.h"
#include "../include/common.h"
#include "../include/models.h"
#include "../include/security.h"
#include <cstring>
#include <cctype>
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <map>
#include <memory>
#include <deque>
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>

// ===== 评测线程池（线程数可在系统安全配置，控制台显示工作速度） =====
namespace {
std::mutex g_judgeMutex;
std::condition_variable g_judgeCV;
std::deque<std::string> g_judgeQueue;
std::vector<std::thread> g_judgeWorkers;
bool g_judgeStop = false;
std::atomic<int> g_judgeActive{0};
std::atomic<long long> g_judgeProcessed{0};
std::atomic<long long> g_judgeTotalMs{0};
std::atomic<long long> g_judgeDirty{0};
std::atomic<int> g_judgePeak{0};
std::atomic<bool> g_monitorStarted{false};
std::mutex g_subLockMapMutex;
std::map<std::string, std::shared_ptr<std::mutex> > g_subLocks;
std::mutex& getSubLock(const std::string& id) {
    std::lock_guard<std::mutex> lk(g_subLockMapMutex);
    std::shared_ptr<std::mutex>& sp = g_subLocks[id];
    if (!sp) sp = std::make_shared<std::mutex>();
    return *sp;
}

void judgePoolMonitor() {
    while (!g_judgeStop) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        int active = g_judgeActive.load();
        long long done = g_judgeProcessed.load();
        size_t qlen = 0;
        { std::lock_guard<std::mutex> lk(g_judgeMutex); qlen = g_judgeQueue.size(); }
        long long avg = (done > 0) ? (g_judgeTotalMs.load() / done) : 0;
        std::cout << "[评测池] 工作线程 " << active
                  << " / 队列 " << qlen << " / 已评测 " << done
                  << " / 平均耗时 " << avg << "ms" << std::endl;
    }
}


void judgePoolSaver() {
    while (!g_judgeStop) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (g_judgeDirty.exchange(0) > 0) {
            DataStore::instance().saveSubmissions();
        }
    }
}

void judgePoolWorker() {
    while (true) {
        std::string id;
        {
            std::unique_lock<std::mutex> lk(g_judgeMutex);
            g_judgeCV.wait(lk, [] { return !g_judgeQueue.empty() || g_judgeStop; });
            if (g_judgeStop && g_judgeQueue.empty()) return;
            id = g_judgeQueue.front();
            g_judgeQueue.pop_front();
        }
        auto t0 = std::chrono::steady_clock::now();
        int cur = ++g_judgeActive;
        int pk = g_judgePeak.load();
        while (cur > pk) { if (g_judgePeak.compare_exchange_weak(pk, cur)) break; }
        Submission* s = DataStore::instance().findSubmissionById(id);
        if (s) {
            Judge::judgeSubmissionLocked(*s);
            g_judgeDirty++;
        }
        auto t1 = std::chrono::steady_clock::now();
        g_judgeActive--;
        g_judgeProcessed++;
        g_judgeTotalMs += (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    }
}
}
void Judge::startPool() {
    {
        std::lock_guard<std::mutex> lk(g_judgeMutex);
        if (!g_judgeWorkers.empty()) return;
    }
    int n = security::getSecConfig().judgeThreads;
    if (n < 1) n = 1;
    if (n > 32) n = 32;
    for (int i = 0; i < n; i++) g_judgeWorkers.emplace_back(judgePoolWorker);
    if (!g_monitorStarted.exchange(true)) {
        std::thread(judgePoolMonitor).detach();
        std::thread(judgePoolSaver).detach();
    }
}

void Judge::waitIdle() {
    for (int i = 0; i < 2400; i++) {
        bool idle;
        { std::lock_guard<std::mutex> lk(g_judgeMutex); idle = g_judgeQueue.empty(); }
        if (idle && g_judgeActive.load() == 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
void Judge::recomputeAC(const std::string& problemId) {
    Problem* p = DataStore::instance().findProblemById(problemId);
    if (!p) return;
    int ac = 0;
    for (auto& s : DataStore::instance().submissions)
        if (s.problemId == problemId && s.status == AC && !s.hacked) ac++;
    p->acCount = ac;
    DataStore::instance().saveProblems();
}
void Judge::enqueue(const std::string& subId) {
    startPool();
    {
        std::lock_guard<std::mutex> lk(g_judgeMutex);
        g_judgeQueue.push_back(subId);
    }
    g_judgeCV.notify_one();
}

#include <windows.h>
#include <ctime>
#include <cctype>

#pragma comment(lib, "psapi.lib")

// 杀掉工作目录下仍存活的所有进程（防止 main.exe/输出文件被占用）
static void killProcsInDir(const std::string& workDir) {
    if (workDir.empty()) return;
    char abs[1024] = {0};
    GetFullPathNameA(workDir.c_str(), sizeof(abs), abs, NULL);
    if (abs[0] == '\0') return;
    std::string ap = std::string(abs) + "\\";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (h) {
                wchar_t exe[1024] = {0};
                DWORD sz = 1024;
                if (QueryFullProcessImageNameW(h, 0, exe, &sz)) {
                    std::wstring ws(exe);
                    std::wstring wap(abs, abs + strlen(abs));
                    if (ws.find(wap) != std::wstring::npos) {
                        TerminateProcess(h, 1);
                    }
                }
                CloseHandle(h);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

#define TEMP_DIR "./temp/"
#define PROBLEM_DIR "./problems/"

// ---------- 编译安全：提交代码静态检查 ----------
// 规则全部由管理员在「管理 → 系统安全 → 编译安全」里开关：
//   · 字符串字面量 / 注释不再参与函数与头文件检测（避免 printf("system(") 误伤）
//   · 字符串字面量单独用于「沙箱外路径」检测（盘符 / 绝对路径 / .. 上跳 / UNC）
//   · 白名单（guardAllowFuncs）优先级最高
static bool guardListHas(const std::string& list, const std::string& name) {
    std::string cur;
    for (size_t i = 0; i <= list.size(); i++) {
        char ch = (i < list.size()) ? list[i] : ',';
        if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '|') {
            if (!cur.empty()) {
                if (cur == name) return true;
                cur.clear();
            }
        } else {
            cur += (char)::tolower((unsigned char)ch);
        }
    }
    return false;
}

static std::vector<std::string> guardListItems(const std::string& list) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i <= list.size(); i++) {
        char ch = (i < list.size()) ? list[i] : ',';
        if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '|') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += (char)::tolower((unsigned char)ch);
        }
    }
    return out;
}

static bool checkDangerousCode(const std::string& code, std::string& errorMsg) {
    security::SecConfig sc = security::getSecConfig();
    if (!sc.codeGuardEnabled) return true;

    // ---- 1. 预处理：清空字符串/注释正文，收集字符串字面量；预处理指令单独留存 ----
    std::string cleaned, includeSrc;
    std::vector<std::string> literals;
    bool inString = false, inChar = false, inLine = false, inBlock = false;
    std::string curLit;
    for (size_t i = 0; i < code.size(); i++) {
        char c = code[i];
        if (inLine) { if (c == '\n') { inLine = false; cleaned += '\n'; } continue; }
        if (inBlock) { if (c == '*' && i + 1 < code.size() && code[i + 1] == '/') { inBlock = false; i++; } continue; }
        if (inString || inChar) {
            char quote = inString ? '"' : '\'';
            if (c == '\\' && i + 1 < code.size()) {
                if (inString) { curLit += c; curLit += code[i + 1]; }
                i++;
                continue;
            }
            if (c == quote) {
                if (inString && !curLit.empty()) literals.push_back(curLit);
                curLit.clear();
                inString = inChar = false;
                cleaned += quote;
                continue;
            }
            if (inString) curLit += c;
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') { inLine = true; i++; continue; }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '*') { inBlock = true; i++; continue; }
        if (c == '"') { inString = true; curLit.clear(); cleaned += '"'; continue; }
        if (c == '\'') { inChar = true; cleaned += '\''; continue; }
        cleaned += c;
    }

    std::string lower = cleaned;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // 预处理指令（#include 等）：只取以 # 开头的行，避免注释 / 字符串误判
    {
        size_t p = 0;
        while (p <= code.size()) {
            size_t e = code.find('\n', p);
            if (e == std::string::npos) e = code.size();
            std::string ln = code.substr(p, e - p);
            size_t k = 0;
            while (k < ln.size() && (ln[k] == ' ' || ln[k] == '\t')) k++;
            if (k < ln.size() && ln[k] == '#') {
                std::string l2 = ln;
                std::transform(l2.begin(), l2.end(), l2.begin(), ::tolower);
                includeSrc += l2;
                includeSrc += '\n';
            }
            if (e >= code.size()) break;
            p = e + 1;
        }
    }

    // 函数名 → 是否被调用（忽略字符串/注释；ignoreAfterDot 用于 open/read 这类成员函数同名）
    auto hasCall = [&](const std::string& fn, bool ignoreAfterDot) -> bool {
        size_t pos = 0;
        while ((pos = lower.find(fn, pos)) != std::string::npos) {
            bool ok = true;
            if (pos > 0) {
                char prev = lower[pos - 1];
                if (isalnum((unsigned char)prev) || prev == '_') ok = false;
                if (ignoreAfterDot && (prev == '.' || prev == '>')) ok = false;
            }
            size_t end = pos + fn.size();
            if (end < lower.size()) {
                char nx = lower[end];
                if (isalnum((unsigned char)nx) || nx == '_') ok = false;
            }
            size_t k = end;
            while (k < lower.size() && (lower[k] == ' ' || lower[k] == '\t' || lower[k] == '\n' || lower[k] == '\r')) k++;
            if (ok && k < lower.size() && lower[k] == '(') {
                if (guardListHas(sc.guardAllowFuncs, fn)) return false;
                return true;
            }
            pos += fn.size();
        }
        return false;
    };

    auto hasInclude = [&](const std::string& name) -> bool {
        size_t p = 0;
        while ((p = includeSrc.find("#include", p)) != std::string::npos) {
            size_t k = p + 8;
            while (k < includeSrc.size() && (includeSrc[k] == ' ' || includeSrc[k] == '\t')) k++;
            if (k < includeSrc.size() && (includeSrc[k] == '<' || includeSrc[k] == '"')) k++;
            while (k < includeSrc.size() && (includeSrc[k] == ' ' || includeSrc[k] == '\t')) k++;
            if (includeSrc.compare(k, name.size(), name) == 0) return true;
            p += 8;
        }
        return false;
    };

    auto failFunc = [&](const std::string& fn, const std::string& why) -> bool {
        errorMsg = "禁止使用 " + fn + "()\n" + why;
        return false;
    };

    // ---- 2. 危险头文件 ----
    if (sc.guardHeaders) {
        static const char* headers[] = {
            "windows.h", "unistd.h", "sys/", "winsock", "conio.h", "dos.h", "io.h",
            "fcntl.h", "process.h", "pthread.h", "signal.h", "sys/types.h",
            "sys/stat.h", "sys/time.h", "sys/wait.h", "sys/socket.h", "sys/select.h",
            "sys/mman.h", "sys/ipc.h", "sys/shm.h", "dlfcn.h", "dirent.h",
            "grp.h", "pwd.h", "winsock2.h", "ws2tcpip.h", "wincrypt.h",
            "security.h", "userenv.h", "wtsapi32.h", "psapi.h", "tlhelp32.h",
            "cstdio.h", "stdio_ext.h", "shlobj.h", "shellapi.h", "tchar.h"
        };
        for (size_t i = 0; i < sizeof(headers) / sizeof(headers[0]); i++) {
            if (hasInclude(headers[i])) {
                errorMsg = "禁止使用危险头文件: " + std::string(headers[i]) + "\n出于安全考虑，不允许包含系统级头文件。";
                return false;
            }
        }
    }
    if (!sc.guardExtraHeaders.empty()) {
        std::vector<std::string> extra = guardListItems(sc.guardExtraHeaders);
        for (size_t i = 0; i < extra.size(); i++) {
            if (hasInclude(extra[i])) {
                errorMsg = "禁止使用头文件: " + extra[i] + "（管理员配置）";
                return false;
            }
        }
    }

    // ---- 3. 进程 / 系统调用 ----
    if (sc.guardProcess) {
        static const char* funcs[] = {
            "system", "popen", "_popen", "fork", "vfork", "clone",
            "execl", "execv", "execle", "execve", "execvp", "execvpe",
            "spawnl", "spawnv", "_spawn", "posix_spawn",
            "getenv", "putenv", "setenv", "unsetenv", "clearenv",
            "kill", "raise", "abort", "chroot"
        };
        for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++)
            if (hasCall(funcs[i], false))
                return failFunc(funcs[i], "出于安全考虑，不允许使用系统调用或进程控制函数。");
    }

    // ---- 4. Windows API ----
    if (sc.guardWinApi) {
        static const char* funcs[] = {
            "createprocess", "createprocessa", "createprocessw", "winexec", "shellexecute",
            "shellexecutea", "shellexecutew",
            "regopen", "regopenkey", "regcreatekey", "regsetvalue",
            "deletefile", "deletefilea", "deletefilew", "movefile", "copyfile",
            "findfirst", "findnext", "findfirstfile", "findnextfile",
            "virtualalloc", "virtualprotect", "virtualfree",
            "writeprocessmemory", "readprocessmemory",
            "createremotethread", "createthread", "openprocess",
            "setwindowshook", "setwindowshookex",
            "getasynckeystate", "keybd_event", "mouse_event", "sendinput",
            "createfile", "createfilea", "createfilew", "mapviewoffile"
        };
        for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++)
            if (hasCall(funcs[i], false))
                return failFunc(funcs[i], "出于安全考虑，不允许使用 Windows API 危险函数。");
    }

    // ---- 5. 网络 ----
    if (sc.guardNetwork) {
        static const char* funcs[] = {
            "socket", "connect", "bind", "listen", "accept",
            "recv", "send", "recvfrom", "sendto",
            "select", "poll", "epoll_create", "epoll_ctl", "epoll_wait",
            "kqueue", "kevent", "gethostbyname", "inet_addr", "wsastartup",
            "urlopen", "curl_easy_init"
        };
        for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++)
            if (hasCall(funcs[i], false))
                return failFunc(funcs[i], "出于安全考虑，不允许联网。");
    }

    // ---- 6. 动态加载 ----
    if (sc.guardDll) {
        static const char* funcs[] = {
            "dlopen", "dlsym", "dlclose", "dlerror",
            "loadlibrary", "loadlibrarya", "loadlibraryw", "getprocaddress"
        };
        for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++)
            if (hasCall(funcs[i], false))
                return failFunc(funcs[i], "出于安全考虑，不允许动态加载库。");
    }

    // ---- 7. 文件读写 / 文件系统（freopen / fopen / fstream ...） ----
    if (sc.guardFileFuncs) {
        static const char* funcs[] = {
            "freopen", "freopen_s", "freopen64", "fopen", "fopen_s", "fopen64",
            "creat", "tmpfile", "openat", "remove", "rename", "unlink",
            "rmdir", "mkdir", "chdir", "chmod", "chown", "umask", "truncate"
        };
        for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++)
            if (hasCall(funcs[i], false))
                return failFunc(funcs[i], "评测环境不需要也不能读写文件：请直接用 stdin / cin 读入、stdout / cout 输出。");
        // open()：排除 f.open(...) 这类成员调用
        if (hasCall("open", true))
            return failFunc("open", "评测环境不需要也不能读写文件：请直接用 stdin / cin 读入、stdout / cout 输出。");
        // 文件流类型
        static const char* types[] = {
            "ifstream", "ofstream", "fstream", "filebuf",
            "basic_ifstream", "basic_ofstream", "basic_fstream",
            "mmap", "munmap", "fscanf", "fprintf", "fread", "fwrite"
        };
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
            if (guardListHas(sc.guardAllowFuncs, types[i])) continue;
            size_t pos = 0;
            const std::string nm = types[i];
            while ((pos = lower.find(nm, pos)) != std::string::npos) {
                bool ok = true;
                if (pos > 0) {
                    char prev = lower[pos - 1];
                    if (isalnum((unsigned char)prev) || prev == '_' || prev == '.') ok = false;
                }
                size_t end = pos + nm.size();
                if (end < lower.size()) {
                    char nx = lower[end];
                    if (isalnum((unsigned char)nx) || nx == '_') ok = false;
                }
                if (ok) {
                    errorMsg = "禁止使用文件流 / 文件读写函数: " + nm + "\n评测环境不需要也不能读写文件：请直接用 stdin / cin 读入、stdout / cout 输出。";
                    return false;
                }
                pos += nm.size();
            }
        }
    }

    // ---- 8. 字符串里的沙箱外路径（盘符 / 绝对路径 / .. 上跳 / UNC） ----
    if (sc.guardPathEscape) {
        for (size_t i = 0; i < literals.size(); i++) {
            std::string lit = literals[i];
            std::string ll = lit;
            std::transform(ll.begin(), ll.end(), ll.begin(), ::tolower);
            bool bad = false;
            if (ll.find("../") != std::string::npos || ll.find("..\\") != std::string::npos) bad = true;
            // 盘符：C:\ 或 D:/（排除 http:// 这类 scheme）
            for (size_t k = 1; k + 1 < ll.size() && !bad; k++) {
                if (ll[k] == ':' && (ll[k + 1] == '/' || ll[k + 1] == '\\')) {
                    if (isalpha((unsigned char)ll[k - 1]) && (k < 2 || !isalnum((unsigned char)ll[k - 2]))) bad = true;
                }
            }
            // Unix 绝对路径的关键目录
            static const char* roots[] = {
                "/etc", "/proc", "/home", "/root", "/var", "/usr", "/bin", "/sbin", "/tmp", "/dev", "/sys", "/boot"
            };
            for (size_t r = 0; r < sizeof(roots) / sizeof(roots[0]) && !bad; r++) {
                if (ll.compare(0, strlen(roots[r]), roots[r]) == 0) bad = true;
            }
            // UNC 共享 \\server\share
            if (ll.size() >= 2 && ll[0] == '\\' && ll[1] == '\\') bad = true;
            if (bad) {
                std::string shown = lit;
                if (shown.size() > 60) shown = shown.substr(0, 60) + "...";
                errorMsg = "禁止访问沙箱外的路径: \"" + shown + "\"\n评测程序只能读写自己的输入 / 输出，不能访问系统文件。";
                return false;
            }
        }
    }

    // ---- 9. 编译器扩展 ----
    if (sc.guardCompilerExt) {
        static const char* exts[] = { "#pragma comment", "__declspec", "__attribute__", "asm(", "__asm" };
        for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
            if (lower.find(exts[i]) != std::string::npos) {
                errorMsg = "禁止使用编译器扩展: " + std::string(exts[i]) + "\n不允许链接系统库或使用平台相关扩展。";
                return false;
            }
        }
    }

    // ---- 10. 管理员自定义的额外禁止函数 ----
    if (!sc.guardExtraFuncs.empty()) {
        std::vector<std::string> extra = guardListItems(sc.guardExtraFuncs);
        for (size_t i = 0; i < extra.size(); i++) {
            if (extra[i].empty()) continue;
            if (guardListHas(sc.guardAllowFuncs, extra[i])) continue;
            if (hasCall(extra[i], true)) {
                errorMsg = "禁止使用 " + extra[i] + "()\n该函数已被管理员列入禁止列表。";
                return false;
            }
            // 也支持按类型/关键字匹配（不带括号）
            size_t pos = 0;
            while ((pos = lower.find(extra[i], pos)) != std::string::npos) {
                bool ok = true;
                if (pos > 0) { char prev = lower[pos - 1]; if (isalnum((unsigned char)prev) || prev == '_') ok = false; }
                size_t end = pos + extra[i].size();
                if (end < lower.size()) { char nx = lower[end]; if (isalnum((unsigned char)nx) || nx == '_') ok = false; }
                if (ok) {
                    errorMsg = "禁止使用 " + extra[i] + "\n该标识符已被管理员列入禁止列表。";
                    return false;
                }
                pos += extra[i].size();
            }
        }
    }

    return true;
}

// ---------- 获取 g++ 路径 ----------
static std::string getGppPath() {
    // 0. 优先使用管理员配置的编译器路径
    std::string cfg = security::getSecConfig().gppPath;
    if (!cfg.empty()) {
        if (common::fileExists(cfg)) return cfg;
        // 配置了但文件不存在：仍返回该路径，让编译报错更明确
        //（若它不含盘符或分隔符，视为裸命令名如 g++）
        return cfg;
    }
    // 1. 自动搜索（支持中文路径）
    return common::autoFindGpp();
}

// ---------- 执行命令（编译用） ----------
static bool runCommandWithTimeout(const std::string& cmd, std::string& output, int timeoutMs) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    
    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    
    std::string fullCmd = "cmd /c \"" + cmd + "\"";
    std::vector<char> cmdBuffer(fullCmd.begin(), fullCmd.end());
    cmdBuffer.push_back('\0');
    
    if (!CreateProcess(NULL, &cmdBuffer[0], NULL, NULL, TRUE, 
                       CREATE_NO_WINDOW | NORMAL_PRIORITY_CLASS, 
                       NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    CloseHandle(hWrite);
    
    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        // 杀掉整棵进程树，防止子进程残留占用文件
        char _tk[128];
        snprintf(_tk, sizeof(_tk), "taskkill /F /T /PID %lu >nul 2>nul", (unsigned long)pi.dwProcessId);
        system(_tk);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hRead);
        return false;
    }
    
    char buffer[4096];
    DWORD bytesRead;
    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(hRead, NULL, 0, NULL, &available, NULL)) break;
        if (available == 0) break;
        if (!ReadFile(hRead, buffer, sizeof(buffer)-1, &bytesRead, NULL) || bytesRead == 0) break;
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// ---------- 编译 ----------
bool Judge::compileCode(const std::string& workDir, const std::string& codePath, const std::string& exePath, std::string& errorMsg, const LangConfig& lang) {
    // 1. 危险代码检测
    std::string code = common::readFile(codePath);
    if (!checkDangerousCode(code, errorMsg)) {
        return false;
    }
    
    // 解释型语言：compileCmd 为空则跳过编译
    if (lang.compileCmd.empty()) {
        return true;
    }
    
    // 2. 清理残留进程，避免 main.exe 被占用导致 Access is denied
    killProcsInDir(workDir);
    DeleteFileA(exePath.c_str());

    // 3. 按语言配置的编译命令编译
    std::string errFile = workDir + "compile.err";
    std::string cmd = lang.compileCmd;
    cmd = common::replaceAll(cmd, "{gpp}", getGppPath());
    cmd = common::replaceAll(cmd, "{src}", codePath);
    cmd = common::replaceAll(cmd, "{out}", exePath);
    cmd += " 2> \"" + errFile + "\"";
    
    std::string output;
    if (!runCommandWithTimeout(cmd, output, 30000)) {
        errorMsg = "编译超时（30秒）";
        std::string errContent = common::readFile(errFile);
        if (!errContent.empty()) {
            errorMsg += "\n" + common::toUtf8FromSystem(common::normalizeNewlines(errContent));
        }
        return false;
    }
    
    if (!common::fileExists(exePath)) {
        errorMsg = common::toUtf8FromSystem(common::normalizeNewlines(common::readFile(errFile)));
        if (errorMsg.empty()) {
            errorMsg = "编译失败，未知错误（未生成可执行文件）";
        } else {
            errorMsg += "\n[编译命令] " + cmd;
        }
        return false;
    }
    
    return true;
}

SubmitStatus Judge::runTest(const std::string& workDir, const std::string& exePath,
                           const std::string& runCmd,
                           const std::string& inputFile, const std::string& outputFile,
                           int timeLimit, int memLimit, int& usedTime, int& usedMem) {
    bool interpreted = !runCmd.empty();
    if (!interpreted && !common::fileExists(exePath)) {
                return RE;
    }
    if (!common::fileExists(inputFile)) {
                return RE;
    }

    // ========== 关键修复：设置句柄可继承 ==========
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    
    // Job Object：评测结束关闭时强制回收整棵子进程树，防止残留进程占用文件
    HANDLE hJob = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION _jeli;
    ZeroMemory(&_jeli, sizeof(_jeli));
    _jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    _jeli.BasicLimitInformation.ActiveProcessLimit = 64;
    if (hJob) SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &_jeli, sizeof(_jeli));
    
    HANDLE hInput = CreateFileA(inputFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, OPEN_EXISTING, 0, NULL);
    if (hInput == INVALID_HANDLE_VALUE) {
                return RE;
    }
    
    HANDLE hOutput = CreateFileA(outputFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, CREATE_ALWAYS, 0, NULL);
    if (hOutput == INVALID_HANDLE_VALUE) {
                CloseHandle(hInput);
        return RE;
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hInput;
    si.hStdOutput = hOutput;
    si.hStdError = hOutput;

    std::string cmdLine;
    if (interpreted) {
        cmdLine = runCmd;
    } else {
        cmdLine = "\"" + exePath + "\"";
    }
    std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back('\0');

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    if (!CreateProcess(NULL, &cmdBuffer[0], NULL, NULL, TRUE,
                       CREATE_NO_WINDOW | NORMAL_PRIORITY_CLASS | BELOW_NORMAL_PRIORITY_CLASS,
                       NULL, workDir.c_str(), &si, &pi)) {
        DWORD err = GetLastError();
                CloseHandle(hInput);
        CloseHandle(hOutput);
        if (hJob) CloseHandle(hJob);
        return RE;
    }

    CloseHandle(hInput);
    CloseHandle(hOutput);

    if (hJob) {
        HANDLE _hp = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pi.dwProcessId);
        if (_hp) { AssignProcessToJobObject(hJob, _hp); CloseHandle(_hp); }
    }

    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeLimit + 500);
    QueryPerformanceCounter(&end);
    usedTime = (int)((end.QuadPart - start.QuadPart) * 1000 / freq.QuadPart);

    if (waitResult == WAIT_TIMEOUT) {
                TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (hJob) CloseHandle(hJob);
        usedTime = timeLimit + 500;
        return TLE;
    }

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    // 关键修复：CreateProcess返回的句柄缺少查询权限，GetProcessMemoryInfo 测不准
    // 必须用 OpenProcess 重新打开带 PROCESS_QUERY_INFORMATION|PROCESS_VM_READ 的句柄来测内存
    HANDLE hMem = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pi.dwProcessId);
    PROCESS_MEMORY_COUNTERS pmc;
    usedMem = 0;
    if (hMem && GetProcessMemoryInfo(hMem, &pmc, sizeof(pmc))) {
        usedMem = pmc.PeakWorkingSetSize / (1024 * 1024);
        if (usedMem == 0) usedMem = 1;
    }
    if (hMem) CloseHandle(hMem);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
                if (hJob) CloseHandle(hJob);
        return RE;
    }
    if (hJob) CloseHandle(hJob);
    return AC;
}

bool Judge::compareOutput(const std::string& output1, const std::string& output2) {
    auto strip = [](const std::string& s) -> std::string {
        std::string result;
        for (char c : s) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                result += c;
            }
        }
        return result;
    };
    return strip(output1) == strip(output2);
}

// ---------- SPJ（Special Judge 特殊评测） ----------
// 约定：spj 程序带三个命令行参数：输入文件、标准输出文件、选手输出文件
//      退出码 0 = 通过；非 0 = 不通过。标准输出第一行作为提示信息返回给用户。
static bool compileSpj(const std::string& workDir, const std::string& spjSrc,
                       std::string& spjExe, std::string& err) {
    std::string srcPath = workDir + "spj.cpp";
    if (!common::writeFile(srcPath, spjSrc)) { err = "无法写入 SPJ 源码"; return false; }
    spjExe = workDir + "spj.exe";
    std::string errFile = workDir + "spj_build.err";
    std::string gpp = getGppPath();
    std::string cmd = "\"" + gpp + "\" -std=c++11 -O2 \"" + srcPath + "\" -o \"" + spjExe +
                      "\" -static 2> \"" + errFile + "\"";
    std::string out;
    if (!runCommandWithTimeout(cmd, out, 60000) || !common::fileExists(spjExe)) {
        std::string e = common::readFile(errFile);
        err = e.empty() ? std::string("SPJ 编译失败") : common::toUtf8FromSystem(common::normalizeNewlines(e));
        if (err.size() > 500) err = err.substr(0, 500);
        return false;
    }
    return true;
}

// 相对路径转绝对路径（SPJ 在临时目录中运行，相对路径会解析错误）
static std::string absPathOf(const std::string& p) {
    if (p.empty()) return p;
    char buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameA(p.c_str(), (DWORD)sizeof(buf), buf, NULL);
    if (n == 0 || n >= sizeof(buf)) return p;
    return std::string(buf, n);
}

static SubmitStatus runSpj(const std::string& workDir, const std::string& spjExe,
                           const std::string& inputRaw, const std::string& expectedRaw,
                           const std::string& actualRaw, std::string& msg) {
    const std::string inputFile = absPathOf(inputRaw);
    const std::string expectedFile = absPathOf(expectedRaw);
    const std::string actualFile = absPathOf(actualRaw);
    std::string outFile = workDir + "spj_msg.txt";
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    HANDLE hOut = CreateFileA(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa, CREATE_ALWAYS, 0, NULL);
    if (hOut == INVALID_HANDLE_VALUE) { msg = "SPJ 无法创建输出文件"; return RE; }
    HANDLE hIn = CreateFileA("nul", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hIn;
    si.hStdOutput = hOut;
    si.hStdError = hOut;
    std::string cmdLine = "\"" + spjExe + "\" \"" + inputFile + "\" \"" + expectedFile + "\" \"" + actualFile + "\"";
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(NULL, &buf[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                        workDir.c_str(), &si, &pi)) {
        CloseHandle(hOut);
        if (hIn != INVALID_HANDLE_VALUE) CloseHandle(hIn);
        msg = "SPJ 启动失败";
        return RE;
    }
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hOut);
    if (hIn != INVALID_HANDLE_VALUE) CloseHandle(hIn);
    msg = common::trim(common::normalizeNewlines(common::readFile(outFile)));
    size_t nl = msg.find('\n');
    if (nl != std::string::npos) msg = msg.substr(0, nl);
    if (msg.size() > 200) msg = msg.substr(0, 200);
    return (code == 0) ? AC : WA;
}

// ---------- 清理 ----------
void Judge::cleanup(const std::string& workDir) {
    if (workDir.empty()) return;
    // 评测结束子进程刚被回收，文件句柄可能尚未释放，重试并静默（避免刷屏"目录非空"）
    for (int i = 0; i < 4; i++) {
        std::string cmd = "rmdir /s /q \"" + workDir + "\" >nul 2>nul";
        int rc = system(cmd.c_str());
        if (rc == 0) return;
        Sleep(150);
    }
}

// ---------- 主评测 ----------
void Judge::judgeSubmission(Submission& sub) {
    // 记录重测前的状态，避免 AC->AC 重测重复发奖励/重复计入 AC
    SubmitStatus prevStatus = sub.status;
    sub.status = JUDGING;
    sub.judgeResult = "评测中...";
    DataStore::instance().saveSubmissions();
    
                    
    std::string workDir = TEMP_DIR + sub.id + "_" + common::genId() + "/";
    if (!common::createDirectory(workDir)) {
        sub.status = CE;
        sub.judgeResult = "无法创建临时目录: " + workDir;
        DataStore::instance().saveSubmissions();
        return;
    }
    
    // 获取语言配置（管理员可自定义编译命令/语言）
    LangConfig* lang = DataStore::instance().findLangConfig(sub.language);
    if (!lang || !lang->enabled) {
        sub.status = CE;
        sub.judgeResult = "不支持的语言: " + sub.language;
        DataStore::instance().saveSubmissions();
        cleanup(workDir);
        return;
    }
    std::string exePath = workDir + "main.exe";
    std::string codePath = workDir + "main." + lang->ext;
    if (!common::writeFile(codePath, sub.code)) {
        sub.status = CE;
        sub.judgeResult = "无法保存源代码文件";
        DataStore::instance().saveSubmissions();
        cleanup(workDir);
        return;
    }
    
    // 编译（按语言配置的编译命令；解释型语言跳过编译）
    std::string errorMsg;
    if (!compileCode(workDir, codePath, exePath, errorMsg, *lang)) {
        sub.status = CE;
        sub.judgeResult = "编译错误:\n" + errorMsg;
        sub.usedTime = 0;
        sub.usedMem = 0;
        DataStore::instance().saveSubmissions();
        cleanup(workDir);
        return;
    }
    
    // 获取题目
    Problem* problem = DataStore::instance().findProblemById(sub.problemId);
    if (!problem) {
        sub.status = WA;
        sub.judgeResult = "题目不存在 (ID: " + sub.problemId + ")";
        DataStore::instance().saveSubmissions();
        cleanup(workDir);
        return;
    }
    
    // SPJ 特别评测：编译 checker（只编译一次，所有测试点复用）
    bool spjReady = false;
    std::string spjExe, spjWarn;
    if (!problem->spj.empty()) {
        std::string spjErr;
        spjReady = compileSpj(workDir, problem->spj, spjExe, spjErr);
        if (!spjReady) spjWarn = "⚠ SPJ 编译失败，已回退标准比对：\n" + spjErr;
    }

    // 运行命令（解释型语言用解释器运行；编译型为空则直接运行 exe）
    std::string runCmd;
    if (!lang->runCmd.empty()) {
        runCmd = common::replaceAll(lang->runCmd, "{src}", codePath);
        runCmd = common::replaceAll(runCmd, "{exe}", exePath);
        runCmd = common::replaceAll(runCmd, "{gpp}", getGppPath());
    }
    std::string problemDir = std::string(PROBLEM_DIR) + problem->id + "/";
    
    // 查找测试点
    std::vector<std::string> testFiles;
    std::string searchPath = problemDir + "*.in";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string name = findData.cFileName;
            if (name.length() > 3 && name.substr(name.length() - 3) == ".in") {
                testFiles.push_back(name.substr(0, name.length() - 3));
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    
    if (testFiles.empty()) {
        sub.status = WA;
        sub.judgeResult = "没有找到测试数据 (目录: " + problemDir + ")";
        DataStore::instance().saveSubmissions();
        cleanup(workDir);
        return;
    }
    
    // 运行测试点
    int passedTests = 0;
    std::string resultDetail;
    if (!spjWarn.empty()) resultDetail += spjWarn + "\n";
    sub.usedTime = 0;
    sub.usedMem = 0;
    bool hasError = false;
    bool useSubtask = (problem->subtaskMode && !problem->subtasks.empty());
    std::vector<std::string> caseNames;
    std::vector<bool> casePass;
    std::vector<SubmitStatus> caseStatus;

    for (size_t idx = 0; idx < testFiles.size(); ++idx) {
        const std::string& testName = testFiles[idx];
        std::string inputFile = problemDir + testName + ".in";
        std::string outputFile = problemDir + testName + ".out";
        std::string userOutput = workDir + testName + ".out";
        
        int usedTime = 0, usedMem = 0;
        SubmitStatus status = runTest(workDir, exePath, runCmd, inputFile, userOutput,
                                      problem->timeLimit, problem->memLimit,
                                      usedTime, usedMem);
        
        sub.usedTime += usedTime;
        sub.usedMem = std::max(sub.usedMem, usedMem);
        
        // ========== 关键修复：等待文件写入完成 ==========
        // 子进程退出后，输出可能还在缓冲区，等待 100ms 让系统刷新
        Sleep(100);
        
        // 读取输出
        std::string expected = common::readFile(outputFile);
        std::string actual = common::readFile(userOutput);
        
                                        
        // 判定：SPJ 可用则用 checker，否则标准比对
        bool spjOk = false;
        std::string spjMsg;
        if (spjReady) {
            SubmitStatus sv = runSpj(workDir, spjExe, inputFile, outputFile, userOutput, spjMsg);
            spjOk = (sv == AC);
        } else {
            spjOk = compareOutput(expected, actual);
        }
        bool thisPass = false;
        SubmitStatus caseSt = status;
        if (status == AC) {
            if (spjOk) {
                passedTests++;
                thisPass = true;
                resultDetail += "✅ 测试点 " + testName + " 通过 (时间:" + 
                               std::to_string(usedTime) + "ms, 内存:" + 
                               std::to_string(usedMem) + "MB)\n";
            } else {
                caseSt = WA;
                sub.status = WA;
                resultDetail += "❌ 测试点 " + testName + " 答案错误" +
                                (spjReady ? ("（SPJ: " + (spjMsg.empty() ? "未通过" : spjMsg) + "）") : "") + "\n";
                hasError = true;
            }
        } else {
            caseSt = status;
            sub.status = status;
            std::string statusStr;
            switch(status) {
                case TLE: statusStr = "超时"; break;
                case MLE: statusStr = "超内存"; break;
                case RE: statusStr = "运行时错误"; break;
                default: statusStr = "未知错误";
            }
            resultDetail += "❌ 测试点 " + testName + " " + statusStr + 
                           " (时间:" + std::to_string(usedTime) + "ms, 内存:" + 
                           std::to_string(usedMem) + "MB)\n";
            hasError = true;
        }
        caseNames.push_back(testName);
        casePass.push_back(thisPass);
        caseStatus.push_back(caseSt);
        // 传统模式：第一个错误就停；子任务模式必须跑完全部测试点才能算分
        if (!thisPass && !useSubtask) break;
    }

    // ===== 计分 =====
    if (useSubtask) {
        int total = 0;
        std::string subDetail;
        for (size_t si = 0; si < problem->subtasks.size(); si++) {
            const Subtask& stk = problem->subtasks[si];
            int cnt = 0, hit = 0;
            for (size_t t = 0; t < stk.tests.size(); t++) {
                for (size_t k = 0; k < caseNames.size(); k++) {
                    if (caseNames[k] != stk.tests[t]) continue;
                    cnt++;
                    if (casePass[k]) hit++;
                    break;
                }
            }
            bool allOk = (cnt > 0 && hit == cnt);
            int got = allOk ? stk.score : 0;
            total += got;
            subDetail += std::string(allOk ? "✅ " : "❌ ") + (stk.name.empty() ? ("子任务 " + std::to_string(si + 1)) : stk.name) +
                         "（" + std::to_string(stk.score) + " 分，通过 " + std::to_string(hit) + "/" + std::to_string(cnt) +
                         (cnt == 0 ? "，无匹配测试点" : "") + "）：得 " + std::to_string(got) + " 分\n";
        }
        sub.score = total;
        if (sub.score < 0) sub.score = 0;
        if (sub.score > 100) sub.score = 100;
        resultDetail = "【子任务计分】\n" + subDetail + "\n【测试点明细】\n" + resultDetail;
    } else {
        sub.score = (int)(((double)passedTests / (testFiles.empty() ? 1 : (int)testFiles.size())) * 100.0 + 0.5);
        if (sub.score < 0) sub.score = 0;
        if (sub.score > 100) sub.score = 100;
    }
    
    bool allPass = (passedTests == (int)testFiles.size());
    if (allPass) {
        sub.status = AC;
        sub.judgeResult = "✅ 全部通过!\n" + resultDetail;
    } else if (useSubtask) {
        sub.judgeResult = "❌ 部分测试点未通过（子任务计分，本次 " + std::to_string(sub.score) + " 分）\n" + resultDetail;
    } else if (!hasError) {
        sub.status = WA;
        sub.judgeResult = "❌ 部分测试点未通过\n" + resultDetail;
    } else {
        sub.judgeResult = resultDetail;
    }
    
    if (sub.status == AC) {
        // 只有首次 AC（从非AC变成AC）才发奖励并计入 AC；重测 AC->AC 不重复奖励
        if (prevStatus != AC) {
            bool firstAC = true;
            for (const auto& s : DataStore::instance().submissions) {
                if (s.userId == sub.userId && s.problemId == sub.problemId && s.status == AC && s.id != sub.id) {
                    firstAC = false; break;
                }
            }
            if (firstAC) {
                DataStore::instance().addGold(sub.userId, 50);
                DataStore::instance().addExperience(sub.userId, 50);
            }
            problem->acCount++;
        }
    }
    problem->submitCount++;
    
    DataStore::instance().saveSubmissions();
    DataStore::instance().saveProblems();
    cleanup(workDir);
    
        }

// ---------- 自定义测试（编译+运行自定义输入） ----------
bool Judge::customRun(const std::string& langId, const std::string& code,
                      const std::string& input, int timeLimit, int memLimit,
                      std::string& output, std::string& verdict,
                      int& usedTime, int& usedMem, std::string& errorMsg) {
    LangConfig* lang = DataStore::instance().findLangConfig(langId);
    if (!lang || !lang->enabled) { errorMsg = "不支持的语言: " + langId; return false; }
    if (timeLimit <= 0) timeLimit = 2000;
    if (memLimit <= 0) memLimit = 256;

    std::string workDir = TEMP_DIR + common::genId() + "/";
    if (!common::createDirectory(workDir)) { errorMsg = "无法创建临时目录"; return false; }

    std::string codePath = workDir + "main." + lang->ext;
    if (!common::writeFile(codePath, code)) { errorMsg = "无法写入代码文件"; cleanup(workDir); return false; }

    std::string exePath = workDir + "main.exe";
    if (!compileCode(workDir, codePath, exePath, errorMsg, *lang)) {
        errorMsg = "编译错误:\n" + errorMsg;
        cleanup(workDir);
        return false;
    }

    std::string runCmd;
    if (!lang->runCmd.empty()) {
        runCmd = common::replaceAll(lang->runCmd, "{src}", codePath);
        runCmd = common::replaceAll(runCmd, "{exe}", exePath);
        runCmd = common::replaceAll(runCmd, "{gpp}", getGppPath());
    }

    std::string inFile = workDir + "custom.in";
    std::string outFile = workDir + "custom.out";
    common::writeFile(inFile, input);

    usedTime = 0; usedMem = 0;
    SubmitStatus st = runTest(workDir, exePath, runCmd, inFile, outFile, timeLimit, memLimit, usedTime, usedMem);
    output = common::readFile(outFile);

    switch (st) {
        case AC: verdict = "AC"; break;
        case TLE: verdict = "TLE"; break;
        case MLE: verdict = "MLE"; break;
        case RE: verdict = "RE"; break;
        case CE: verdict = "CE"; break;
        default: verdict = "其他错误"; break;
    }
    cleanup(workDir);
    return true;
}

// ---------- 多测试点：编译一次，运行多个输入 ----------
bool Judge::customRunMulti(const std::string& langId, const std::string& code,
                           const std::vector<std::string>& inputs, int timeLimit, int memLimit,
                           std::vector<CustomCaseResult>& results, std::string& errorMsg) {
    LangConfig* lang = DataStore::instance().findLangConfig(langId);
    if (!lang || !lang->enabled) { errorMsg = "不支持的语言: " + langId; return false; }
    if (timeLimit <= 0) timeLimit = 2000;
    if (memLimit <= 0) memLimit = 256;

    std::string workDir = TEMP_DIR + common::genId() + "/";
    if (!common::createDirectory(workDir)) { errorMsg = "无法创建临时目录"; return false; }

    std::string codePath = workDir + "main." + lang->ext;
    if (!common::writeFile(codePath, code)) { errorMsg = "无法写入代码文件"; cleanup(workDir); return false; }

    std::string exePath = workDir + "main.exe";
    if (!compileCode(workDir, codePath, exePath, errorMsg, *lang)) {
        errorMsg = "编译错误:\n" + errorMsg;
        cleanup(workDir);
        return false;
    }
    std::string runCmd;
    if (!lang->runCmd.empty()) {
        runCmd = common::replaceAll(lang->runCmd, "{src}", codePath);
        runCmd = common::replaceAll(runCmd, "{exe}", exePath);
        runCmd = common::replaceAll(runCmd, "{gpp}", getGppPath());
    }

    results.clear();
    for (size_t i = 0; i < inputs.size(); i++) {
        std::string inFile = workDir + "case" + std::to_string(i) + ".in";
        std::string outFile = workDir + "case" + std::to_string(i) + ".out";
        common::writeFile(inFile, inputs[i]);
        CustomCaseResult r;
        SubmitStatus st = runTest(workDir, exePath, runCmd, inFile, outFile, timeLimit, memLimit, r.time, r.mem);
        r.output = common::readFile(outFile);
        switch (st) {
            case AC: r.verdict = "AC"; break;
            case TLE: r.verdict = "TLE"; break;
            case MLE: r.verdict = "MLE"; break;
            case RE: r.verdict = "RE"; r.error = "运行错误（非零退出码）"; break;
            case CE: r.verdict = "CE"; break;
            default: r.verdict = "NA"; break;
        }
        results.push_back(r);
        if (st == TLE || st == MLE) break;   // 后面的点也没意义
    }
    cleanup(workDir);
    return true;
}

// ---------- 数据生成：编译生成器 + 标程，产出 n 组 .in/.out ----------
// 生成器约定：从 stdin 读入 "<caseNo> <seed>"，向 stdout 输出这一组输入。
bool Judge::generateData(Problem* problem, int count, int seed, int& made, std::string& detail) {
    made = 0;
    if (!problem) { detail = "题目不存在"; return false; }
    if (problem->generator.empty()) { detail = "还没有数据生成器"; return false; }
    if (problem->standardSolution.empty()) { detail = "还没有标程，无法生成标准输出"; return false; }
    if (count < 1) count = 1;
    if (count > 100) count = 100;
    if (seed <= 0) seed = 1;

    std::vector<std::string> genArgs;
    for (int i = 0; i < count; i++)
        genArgs.push_back(std::to_string(i + 1) + " " + std::to_string(seed + i));

    std::vector<CustomCaseResult> genRes;
    std::string err;
    if (!customRunMulti("cpp", problem->generator, genArgs, 5000, 512, genRes, err)) {
        detail = "生成器编译/运行失败：" + err;
        return false;
    }
    if ((int)genRes.size() != count) {
        std::string v = genRes.empty() ? std::string("NA") : genRes.back().verdict;
        detail = "生成器在第 " + std::to_string((int)genRes.size() + 1) + " 组数据时异常（" + v + "）";
        return false;
    }
    std::vector<std::string> inputs;
    for (int i = 0; i < count; i++) inputs.push_back(genRes[i].output);

    std::vector<CustomCaseResult> stdRes;
    if (!customRunMulti("cpp", problem->standardSolution, inputs, problem->timeLimit, problem->memLimit, stdRes, err)) {
        detail = "标程编译失败：" + err;
        return false;
    }
    if ((int)stdRes.size() != count) {
        std::string v = stdRes.empty() ? std::string("NA") : stdRes.back().verdict;
        detail = "标程在第 " + std::to_string((int)stdRes.size() + 1) + " 组数据上 " + v;
        return false;
    }

    std::string dir = std::string(PROBLEM_DIR) + problem->id + "/";
    common::createDirectory(dir);
    for (int i = 0; i < count; i++) {
        if (stdRes[i].verdict != "AC") {
            detail = "标程在第 " + std::to_string(i + 1) + " 组数据上 " + stdRes[i].verdict + "，已停止写入";
            return false;
        }
        std::string name = std::to_string(i + 1);
        common::writeFile(dir + name + ".in", inputs[i]);
        common::writeFile(dir + name + ".out", stdRes[i].output);
        made++;
    }
    detail = "已生成 " + std::to_string(made) + " 组数据（起始种子 " + std::to_string(seed) + "）";
    return true;
}

// ---------- 对决评分：编译并对题目全部测试点评分 ----------
bool Judge::duelScore(const std::string& langId, const std::string& code,
                      Problem* problem, int& passed, int& total, std::string& detail) {
    if (!problem) { detail = "题目不存在"; return false; }
    LangConfig* lang = DataStore::instance().findLangConfig(langId);
    if (!lang || !lang->enabled) { detail = "不支持的语言: " + langId; return false; }

    std::string workDir = TEMP_DIR + common::genId() + "/";
    if (!common::createDirectory(workDir)) { detail = "无法创建临时目录"; return false; }
    std::string codePath = workDir + "main." + lang->ext;
    if (!common::writeFile(codePath, code)) { detail = "无法写入代码文件"; cleanup(workDir); return false; }
    std::string exePath = workDir + "main.exe";
    std::string errorMsg;
    if (!compileCode(workDir, codePath, exePath, errorMsg, *lang)) {
        detail = "编译错误:\n" + errorMsg;
        cleanup(workDir);
        return false;
    }
    std::string runCmd;
    if (!lang->runCmd.empty()) {
        runCmd = common::replaceAll(lang->runCmd, "{src}", codePath);
        runCmd = common::replaceAll(runCmd, "{exe}", exePath);
        runCmd = common::replaceAll(runCmd, "{gpp}", getGppPath());
    }

    // 发现测试数据
    std::string problemDir = std::string(PROBLEM_DIR) + problem->id + "/";
    std::vector<std::string> testFiles;
    std::string searchPath = problemDir + "*.in";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string name = findData.cFileName;
            if (name.length() > 3 && name.substr(name.length() - 3) == ".in")
                testFiles.push_back(name.substr(0, name.length() - 3));
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    if (testFiles.empty()) { detail = "题目没有测试数据"; cleanup(workDir); return false; }

    passed = 0; total = (int)testFiles.size();
    for (size_t idx = 0; idx < testFiles.size(); ++idx) {
        std::string tn = testFiles[idx];
        std::string inputFile = problemDir + tn + ".in";
        std::string outputFile = problemDir + tn + ".out";
        std::string userOutput = workDir + tn + ".out";
        int usedTime = 0, usedMem = 0;
        SubmitStatus st = runTest(workDir, exePath, runCmd, inputFile, userOutput,
                                  problem->timeLimit, problem->memLimit, usedTime, usedMem);
        Sleep(80);
        if (st == AC && compareOutput(common::readFile(outputFile), common::readFile(userOutput))) {
            passed++;
        }
    }
    cleanup(workDir);
    return true;
}

bool Judge::checkEnvironment() {
    common::createDirectory(TEMP_DIR);
    common::createDirectory(PROBLEM_DIR);
    std::cout << "[checkEnvironment] Using g++: " << getGppPath() << std::endl;
    return true;
}
void Judge::judgeSubmissionLocked(Submission& sub) {
    std::lock_guard<std::mutex> lk(getSubLock(sub.id));
    judgeSubmission(sub);
}
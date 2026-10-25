#include "../include/ai_tools.h"
#include "../include/common.h"
#include "../include/models.h"
#include <windows.h>
#include <map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdio>

namespace aitools {

static const char* kShellFlagFile = "./data/ai_shell.json";
static const char* kLogFile = "./data/ai_log.json";

// ---------------- 配置：命令开关 ----------------
bool shellEnabled() {
    std::string raw = common::readFile(kShellFlagFile);
    if (raw.empty()) return true;   // 默认允许（管理员可关闭）
    auto o = common::jsonParseObject(raw);
    if (o.count("shell")) return o["shell"] == "1";
    return true;
}

void setShellEnabled(bool on) {
    std::map<std::string, std::string> m;
    m["shell"] = on ? "1" : "0";
    common::writeFile(kShellFlagFile, common::jsonStringify(m));
}

// ---------------- 工作区 ----------------
std::string workspaceDir(const std::string& userId) {
    std::string d = "./data/aiws/u" + userId + "/";
    common::createDirectory("./data");
    common::createDirectory("./data/aiws");
    common::createDirectory(d);
    return d;
}

// 用户云盘目录（与 /drive 页面同一个位置：./files/<id>/）
std::string driveDir(const std::string& userId) {
    common::createDirectory("./files");
    std::string d = "./files/" + userId + "/";
    common::createDirectory(d);
    return d;
}

// root: "drive" = 云盘；其它 = AI 工作区
static std::string rootDir(const std::string& userId, const std::string& root) {
    std::string r = root;
    for (size_t i = 0; i < r.size(); i++) r[i] = (char)tolower((unsigned char)r[i]);
    if (r == "drive" || r == "cloud" || r == "云盘") return driveDir(userId);
    return workspaceDir(userId);
}

// 相对路径 -> 根目录内的路径（拒绝越权）
static bool resolveRel(const std::string& relRaw, std::string& rel) {
    rel = common::trim(relRaw);
    if (rel.empty()) rel = ".";
    std::string low = rel;
    for (size_t i = 0; i < low.size(); i++) low[i] = (char)tolower((unsigned char)low[i]);
    if (rel.find("..") != std::string::npos) return false;
    if (rel.size() > 1 && rel[1] == ':') return false;      // 盘符
    if (!rel.empty() && (rel[0] == '\\' || rel[0] == '/')) return false;
    if (low.find(":/") != std::string::npos) return false;
    // 统一分隔符，避免 cmd 注入
    for (size_t i = 0; i < rel.size(); i++) if (rel[i] == '\\') rel[i] = '/';
    if (rel.find('"') != std::string::npos || rel.find('*') != std::string::npos ||
        rel.find('?') != std::string::npos || rel.find('|') != std::string::npos ||
        rel.find('<') != std::string::npos || rel.find('>') != std::string::npos) return false;
    return true;
}

// 目录占用大小
static long long dirSizeOf(const std::string& dir) {
    long long total = 0;
    WIN32_FIND_DATAA fd;
    std::string pat = dir + "*";
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) total += dirSizeOf(dir + n + "/");
        else total += ((long long)fd.nFileSizeHigh << 32) | (long long)fd.nFileSizeLow;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return total;
}

// 云盘配额（与 /drive 页面共用 data/drive.json）
static int driveCfgMB(const char* key, int def) {
    std::string cfg = common::readFile("./data/drive.json");
    if (!cfg.empty()) {
        auto o = common::jsonParseObject(cfg);
        if (o.count(key)) { int v = common::stringToInt(o[key]); if (v >= 1) return v; }
    }
    return def;
}

static bool isDirectoryPath(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string listDirNative(const std::string& dir) {
    std::string out;
    WIN32_FIND_DATAA fd;
    std::string pat = dir + "*";
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        if (!common::isValidUtf8(n)) n = common::toUtf8FromSystem(n);
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out += n + (isDir ? "/" : "") + "\n";
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

// ---------------- 危险命令拦截 ----------------
static bool commandBlocked(const std::string& cmd) {
    std::string low;
    for (size_t i = 0; i < cmd.size(); i++) low += (char)tolower((unsigned char)cmd[i]);
    static const char* bad[] = {
        "format ", "diskpart", "shutdown", "bcdedit", "reg delete", "reg add",
        "net user", "net localgroup", "net share", "rd /s", "rmdir /s",
        "del /s", "del /f", "del /q", "takeown", "icacls", "vssadmin",
        "sc delete", "sc stop", "powershell -enc", "powershell -encodedcommand",
        "cipher /w", "rm -rf", "rm -fr", "mkfs", "dd if=", "taskkill", "stop-process",
        "cd ..", "setx ", "wmic", "attrib -r", "move c:", "copy c:",
        "dfs", "netsh", "route ", "arp -", "telnet", "ftp ", "curl -o", "certutil"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        if (low.find(bad[i]) != std::string::npos) return true;
    // 禁止重定向写到工作区外（粗略：> 后跟盘符或上级目录）
    if (low.find("> c:") != std::string::npos || low.find("> d:") != std::string::npos ||
        low.find("> ..") != std::string::npos) return true;
    return false;
}

// 执行命令并捕获输出（工作目录固定为 workDir）
static bool runCapture(const std::string& cmd, const std::string& workDir, int timeoutMs,
                       std::string& output, int& exitCode) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    std::string full = "cmd /c " + cmd;
    std::vector<char> buf(full.begin(), full.end());
    buf.push_back('\0');
    if (!CreateProcessA(NULL, &buf[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                        workDir.c_str(), &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    CloseHandle(hWrite);
    DWORD wr = WaitForSingleObject(pi.hProcess, (DWORD)timeoutMs);
    if (wr == WAIT_TIMEOUT) {
        char tk[128];
        snprintf(tk, sizeof(tk), "taskkill /F /T /PID %lu >nul 2>nul", (unsigned long)pi.dwProcessId);
        system(tk);
        TerminateProcess(pi.hProcess, 1);
        exitCode = -1;
        output += "\n[命令超时被终止]";
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        exitCode = (int)code;
    }
    // 读输出
    char rd[4096];
    DWORD n = 0;
    size_t guard = 0;
    while (guard++ < 512) {
        DWORD avail = 0;
        if (!PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL)) break;
        if (avail == 0) break;
        if (!ReadFile(hRead, rd, sizeof(rd), &n, NULL) || n == 0) break;
        output.append(rd, n);
        if (output.size() > 60000) break;
    }
    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// ---------------- 工具清单 ----------------
std::string specJson() {
    std::string s;
    s += "[";
    auto add = [&](const std::string& fn, const std::string& desc, const std::string& params) {
        if (s.size() > 1) s += ",";
        s += "{\"type\":\"function\",\"function\":{\"name\":\"" + fn + "\",\"description\":\"" + desc +
             "\",\"parameters\":" + params + "}}";
    };
    const std::string rootProp =
        "\"root\":{\"type\":\"string\",\"enum\":[\"workspace\",\"drive\"],\"description\":"
        "\"workspace=AI 工作区（默认）；drive=用户云盘（/drive 里看到的文件和文件夹）\"}";
    add("run_command",
        "在用户工作区执行命令（cmd），用于编译/运行代码、查看目录等。超时默认 10 秒，不支持危险命令。"
        "root=drive 时在云盘目录里执行。",
        "{\"type\":\"object\",\"properties\":{\"cmd\":{\"type\":\"string\",\"description\":\"要执行的命令\"},"
        + rootProp + ","
        "\"timeout_sec\":{\"type\":\"integer\",\"description\":\"超时秒数（最大 20）\"}},\"required\":[\"cmd\"]}");
    add("list_files", "列出目录内容。目录名后面会带 / ；可用 root 切换工作区或云盘。",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"目录，默认 .\"}," + rootProp + "}}");
    add("read_file", "读取文件内容（工作区或云盘）",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}," + rootProp + "},\"required\":[\"path\"]}");
    add("write_file", "写入（新建或覆盖）文件内容（工作区或云盘），会自动创建父目录",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}," + rootProp + "},\"required\":[\"path\",\"content\"]}");
    add("edit_file", "精确编辑文件：把文件里的一段原文替换成新内容，不重写整个文件。"
        "old_string 必须与文件内容完全一致（含缩进/换行）；默认要求它在文件中唯一，"
        "需要批量替换时传 replace_all=true。修改前建议先用 read_file 确认原文。",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
        "\"old_string\":{\"type\":\"string\",\"description\":\"要被替换的原文（完整、逐字符一致）\"},"
        "\"new_string\":{\"type\":\"string\",\"description\":\"替换后的内容，可为空字符串表示删除\"},"
        "\"replace_all\":{\"type\":\"boolean\",\"description\":\"是否替换所有匹配（默认 false）\"},"
        + rootProp + "},\"required\":[\"path\",\"old_string\",\"new_string\"]}");
    add("make_dir", "创建目录（工作区或云盘）",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}," + rootProp + "},\"required\":[\"path\"]}");
    add("delete_file", "删除文件或整个目录（工作区或云盘），目录会递归删除，操作前请确认路径",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}," + rootProp + "},\"required\":[\"path\"]}");
    add("move_path", "重命名或移动文件/文件夹（同一个根目录内）",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"to\":{\"type\":\"string\"}," + rootProp + "},\"required\":[\"path\",\"to\"]}");
    s += "]";
    return s;
}

bool isTool(const std::string& name) {
    return name == "run_command" || name == "list_files" || name == "read_file" ||
           name == "write_file" || name == "edit_file" || name == "make_dir" ||
           name == "delete_file" || name == "move_path";
}

// ---------------- 执行 ----------------
Result run(const std::string& userId, const std::string& name, const std::string& argsJson) {
    Result r;
    auto a = common::jsonParseObject(argsJson);
    std::string path = a.count("path") ? a["path"] : ".";
    std::string root = a.count("root") ? common::trim(a["root"]) : "";
    bool onDrive = (root == "drive" || root == "cloud" || root == "云盘");

    if (name == "run_command") {
        if (!shellEnabled()) {
            r.output = "错误：管理员已禁用命令执行。";
            return r;
        }
        std::string cmd = a.count("cmd") ? common::trim(a["cmd"]) : "";
        if (cmd.empty()) { r.output = "错误：命令为空。"; return r; }
        if (commandBlocked(cmd)) {
            r.output = "已拦截：该命令在黑名单中，不允许执行。";
            return r;
        }
        int tmo = a.count("timeout_sec") ? common::stringToInt(a["timeout_sec"]) : 10;
        if (tmo < 1) tmo = 10;
        if (tmo > 20) tmo = 20;
        std::string out;
        int code = 0;
        std::string wd = onDrive ? driveDir(userId) : workspaceDir(userId);
        bool ok = runCapture(cmd, wd, tmo * 1000, out, code);
        if (!ok) { r.output = "错误：无法启动命令行。"; return r; }
        if (!common::isValidUtf8(out)) out = common::toUtf8FromSystem(out);
        if (out.size() > 8000) out = out.substr(0, 8000) + "\n...[输出已截断]";
        r.ok = (code == 0);
        r.output = "$ " + cmd + "\n" + out + "\n[退出码 " + std::to_string(code) + "]";
        if (out.empty()) r.output = "$ " + cmd + "\n(无输出)\n[退出码 " + std::to_string(code) + "]";
        return r;
    }

    std::string rel;
    if (!resolveRel(path, rel)) {
        r.output = "错误：路径越权（只允许根目录内的相对路径）。";
        return r;
    }
    std::string base = rootDir(userId, root);
    std::string full = (rel == ".") ? base : (base + rel);
    const char* whereName = onDrive ? "云盘" : "工作区";

    if (name == "list_files") {
        std::string dir = full;
        if (dir.empty() || dir[dir.size() - 1] != '/' && dir[dir.size() - 1] != '\\') dir += "/";
        if (!isDirectoryPath(dir)) { r.output = "错误：目录不存在：" + path; return r; }
        std::string out = listDirNative(dir);
        r.ok = true;
        r.output = out.empty() ? "（空目录）" : out;
        return r;
    }
    if (name == "read_file") {
        if (isDirectoryPath(full)) { r.output = "错误：这是个目录，请用 list_files。"; return r; }
        if (!common::fileExists(full)) { r.output = "错误：文件不存在：" + path; return r; }
        std::string c = common::readFile(full);
        if (!common::isValidUtf8(c)) c = common::toUtf8FromSystem(c);
        if (c.size() > 200000) c = c.substr(0, 200000) + "\n...[已截断]";
        r.ok = true;
        r.output = c;
        return r;
    }
    if (name == "write_file") {
        std::string content = a.count("content") ? a["content"] : "";
        if (content.size() > 200000) { r.output = "错误：内容过大（200KB 上限）。"; return r; }
        if (onDrive) {
            // 云盘配额校验（与 /drive 页面一致）
            int maxFileMB = driveCfgMB("maxFileMB", 10);
            long long totalCap = (long long)driveCfgMB("maxTotalMB", 100) * 1024 * 1024;
            User* u = DataStore::instance().findUserById(userId);
            if (u) {
                if (u->driveMB > 0) totalCap = (long long)u->driveMB * 1024 * 1024;
                if (u->driveBonusMB > 0 && !u->driveBonusUntil.empty() && !common::isTimeExpired(u->driveBonusUntil))
                    totalCap += (long long)u->driveBonusMB * 1024 * 1024;
            }
            if ((long long)content.size() > (long long)maxFileMB * 1024 * 1024) {
                r.output = "错误：单文件超过云盘上限 " + std::to_string(maxFileMB) + "MB。";
                return r;
            }
            long long used = dirSizeOf(base);
            long long old = 0;
            if (common::fileExists(full)) { std::string oc = common::readFile(full); old = (long long)oc.size(); }
            if (used - old + (long long)content.size() > totalCap) {
                r.output = "错误：云盘空间不足（已用 " + std::to_string(used / 1024) +
                           "KB / 上限 " + std::to_string(totalCap / 1024 / 1024) + "MB）。";
                return r;
            }
        }
        size_t sl = full.find_last_of("/\\");
        if (sl != std::string::npos) common::createDirectory(full.substr(0, sl));
        if (!common::writeFile(full, content)) { r.output = "写入失败。"; return r; }
        r.ok = true;
        r.output = "已写入 " + std::string(whereName) + " " + path + "（" + std::to_string(content.size()) + " 字节）";
        return r;
    }
    if (name == "edit_file") {
        std::string oldStr = a.count("old_string") ? a["old_string"] : "";
        std::string newStr = a.count("new_string") ? a["new_string"] : "";
        if (oldStr.empty()) { r.output = "错误：old_string 不能为空（新建文件请用 write_file）。"; return r; }
        if (isDirectoryPath(full) || !common::fileExists(full)) { r.output = "错误：文件不存在：" + path; return r; }
        std::string content = common::readFile(full);
        if (content.size() > 2 * 1024 * 1024) { r.output = "错误：文件过大（2MB 上限），请改用 run_command 处理。"; return r; }
        bool replaceAll = a.count("replace_all") && (a["replace_all"] == "true" || a["replace_all"] == "1");
        std::vector<size_t> hits;
        size_t pos = content.find(oldStr);
        while (pos != std::string::npos) {
            hits.push_back(pos);
            if (hits.size() > 1000) break;
            pos = content.find(oldStr, pos + oldStr.size());
        }
        if (hits.empty()) {
            r.output = "错误：在 " + path + " 中找不到 old_string。请先用 read_file 查看文件，"
                       "确保原文（含缩进与换行）完全一致。";
            return r;
        }
        if (hits.size() > 1 && !replaceAll) {
            r.output = "错误：old_string 在文件中出现了 " + std::to_string(hits.size()) +
                       " 次，请提供更长的唯一片段，或设置 replace_all=true。";
            return r;
        }
        std::string outContent = content;
        if (replaceAll) {
            outContent = common::replaceAll(outContent, oldStr, newStr);
        } else {
            outContent = content.substr(0, hits[0]) + newStr + content.substr(hits[0] + oldStr.size());
        }
        if (onDrive) {
            int maxFileMB = driveCfgMB("maxFileMB", 10);
            if ((long long)outContent.size() > (long long)maxFileMB * 1024 * 1024) {
                r.output = "错误：编辑后文件超过云盘单文件上限 " + std::to_string(maxFileMB) + "MB。";
                return r;
            }
        }
        if (!common::writeFile(full, outContent)) { r.output = "写入失败。"; return r; }
        r.ok = true;
        r.output = "已编辑 " + std::string(whereName) + " " + path + "（替换 " +
                   std::to_string(replaceAll ? hits.size() : 1) + " 处，文件现 " +
                   std::to_string(outContent.size()) + " 字节）";
        return r;
    }
    if (name == "make_dir") {
        r.ok = common::createDirectory(full + "/");
        r.output = r.ok ? ("已创建目录 " + std::string(whereName) + " " + path)
                        : "创建失败。";
        return r;
    }
    if (name == "move_path") {
        std::string toRaw = a.count("to") ? a["to"] : "";
        std::string toRel;
        if (!resolveRel(toRaw, toRel) || toRel == ".") { r.output = "错误：目标路径不合法。"; return r; }
        std::string toFull = base + toRel;
        size_t sl2 = toFull.find_last_of("/\\");
        if (sl2 != std::string::npos) common::createDirectory(toFull.substr(0, sl2));
        if (!common::fileExists(full) && !isDirectoryPath(full)) { r.output = "错误：源路径不存在：" + path; return r; }
        r.ok = MoveFileA(full.c_str(), toFull.c_str()) != 0;
        r.output = r.ok ? ("已移动 " + path + " → " + toRel)
                        : "移动失败（目标可能已存在）。";
        return r;
    }
    if (name == "delete_file") {
        bool isDir = isDirectoryPath(full);
        std::string cmd = isDir ? ("rmdir /s /q \"" + full + "\"") : ("del /q /f \"" + full + "\"");
        std::string out;
        int code = 0;
        runCapture(cmd, base, 8000, out, code);
        bool gone = !common::fileExists(full) && !isDirectoryPath(full);
        r.ok = gone;
        r.output = gone ? ("已删除 " + std::string(whereName) + " " + path) : "删除失败。";
        return r;
    }
    r.output = "未知工具。";
    return r;
}

bool saveUpload(const std::string& userId, const std::string& filename, const std::string& data,
                std::string& savedRel) {
    std::string name = filename;
    // 只取文件名部分，去掉路径
    size_t sl = name.find_last_of("/\\");
    if (sl != std::string::npos) name = name.substr(sl + 1);
    std::string clean;
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' || (unsigned char)c >= 0x80) clean += c;
    }
    if (clean.empty()) clean = "upload.bin";
    if (clean.size() > 80) clean = clean.substr(clean.size() - 80);
    if (data.size() > 4 * 1024 * 1024) return false;   // 单文件 4MB
    savedRel = clean;
    std::string full = workspaceDir(userId) + clean;
    return common::writeFile(full, data);
}

// ---------------- 用量日志 ----------------
void logUsage(const std::string& userId, const std::string& model, const std::string& task,
              long long promptTok, long long compTok, long long cost) {
    std::map<std::string, std::string> m;
    m["ts"] = std::to_string((long long)time(NULL));
    m["userId"] = userId;
    m["model"] = model;
    m["task"] = task.empty() ? "chat" : task;
    m["pt"] = std::to_string(promptTok);
    m["ct"] = std::to_string(compTok);
    m["cost"] = std::to_string(cost);
    std::string line = common::jsonStringify(m);
    std::string old = common::readFile(kLogFile);
    std::vector<std::string> lines = common::split(old, '\n');
    std::string out;
    int keep = 0;
    int total = 0;
    for (size_t i = 0; i < lines.size(); i++) if (!lines[i].empty()) total++;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) continue;
        keep++;
        if (total - keep >= 4000) continue;   // 只保留最近 4000 条
        out += lines[i] + "\n";
    }
    out += line + "\n";
    common::writeFile(kLogFile, out);
}

std::vector<LogRow> readUsageLog() {
    std::vector<LogRow> rows;
    std::string raw = common::readFile(kLogFile);
    if (raw.empty()) return rows;
    auto lines = common::split(raw, '\n');
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) continue;
        auto o = common::jsonParseObject(lines[i]);
        if (o.empty()) continue;
        LogRow r;
        r.ts = common::stringToLongLong(o.count("ts") ? o["ts"] : "0");
        r.userId = o.count("userId") ? o["userId"] : "";
        r.model = o.count("model") ? o["model"] : "";
        r.task = o.count("task") ? o["task"] : "chat";
        r.promptTok = common::stringToLongLong(o.count("pt") ? o["pt"] : "0");
        r.compTok = common::stringToLongLong(o.count("ct") ? o["ct"] : "0");
        r.cost = common::stringToLongLong(o.count("cost") ? o["cost"] : "0");
        rows.push_back(r);
    }
    return rows;
}


// ---------------- AI 使用历史 ----------------
static const char* kRunsFile = "./data/ai_runs.json";

void saveRun(const RunRow& r) {
    std::map<std::string, std::string> m;
    m["id"] = r.id;
    m["userId"] = r.userId;
    m["type"] = r.type;
    m["pid"] = r.pid;
    m["title"] = r.title;
    m["time"] = r.time;
    m["question"] = r.question.size() > 4000 ? r.question.substr(0, 4000) : r.question;
    m["answer"] = r.answer.size() > 12000 ? r.answer.substr(0, 12000) : r.answer;
    std::string line = common::jsonStringify(m);
    std::vector<std::string> lines = common::split(common::readFile(kRunsFile), '\n');
    std::vector<std::string> keep;
    for (size_t i = 0; i < lines.size(); i++) if (!lines[i].empty()) keep.push_back(lines[i]);
    keep.push_back(line);
    while (keep.size() > 200) keep.erase(keep.begin());
    std::string out;
    for (size_t i = 0; i < keep.size(); i++) out += keep[i] + "\n";
    common::writeFile(kRunsFile, out);
}

static RunRow runFromJson(const std::string& line) {
    RunRow r;
    auto o = common::jsonParseObject(line);
    if (o.empty()) return r;
    r.id = o.count("id") ? o["id"] : "";
    r.userId = o.count("userId") ? o["userId"] : "";
    r.type = o.count("type") ? o["type"] : "";
    r.pid = o.count("pid") ? o["pid"] : "";
    r.title = o.count("title") ? o["title"] : "";
    r.time = o.count("time") ? o["time"] : "";
    r.question = o.count("question") ? o["question"] : "";
    r.answer = o.count("answer") ? o["answer"] : "";
    return r;
}

std::vector<RunRow> readRuns(const std::string& userId, const std::string& type, int limit) {
    std::vector<RunRow> out;
    std::vector<std::string> lines = common::split(common::readFile(kRunsFile), '\n');
    for (size_t i = lines.size(); i > 0; i--) {
        if (lines[i-1].empty()) continue;
        RunRow r = runFromJson(lines[i-1]);
        if (r.id.empty()) continue;
        if (!userId.empty() && r.userId != userId) continue;
        if (!type.empty() && type != "all" && r.type != type) continue;
        out.push_back(r);
        if ((int)out.size() >= limit) break;
    }
    return out;
}

bool findRun(const std::string& id, RunRow& out) {
    std::vector<std::string> lines = common::split(common::readFile(kRunsFile), '\n');
    for (size_t i = lines.size(); i > 0; i--) {
        if (lines[i-1].empty()) continue;
        RunRow r = runFromJson(lines[i-1]);
        if (r.id == id) { out = r; return true; }
    }
    return false;
}

} // namespace aitools

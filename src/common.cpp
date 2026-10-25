#include "../include/common.h"
#include <sys/stat.h>
#include <direct.h>
#include <errno.h>
#include <psapi.h>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <vector>
#include <ctime>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <windows.h>
#include <atomic>

#pragma comment(lib, "psapi.lib")

namespace common {

// ---------- SHA-256（密码哈希用；上面的 sha1 实际是 FNV，不能用于安全场景） ----------
static inline unsigned int sha256_rotr(unsigned int x, unsigned int n) {
    return (x >> n) | (x << (32 - n));
}

std::string sha256Hex(const std::string& input) {
    static const unsigned int K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    unsigned int h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                         0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::string msg = input;
    unsigned long long bitLen = (unsigned long long)msg.size() * 8ULL;
    msg += (char)0x80;
    while (msg.size() % 64 != 56) msg += (char)0;
    for (int i = 7; i >= 0; i--) msg += (char)((bitLen >> (i * 8)) & 0xFF);
    for (size_t off = 0; off < msg.size(); off += 64) {
        unsigned int w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ((unsigned int)(unsigned char)msg[off + i * 4] << 24) |
                   ((unsigned int)(unsigned char)msg[off + i * 4 + 1] << 16) |
                   ((unsigned int)(unsigned char)msg[off + i * 4 + 2] << 8) |
                   ((unsigned int)(unsigned char)msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 64; i++) {
            unsigned int s0 = sha256_rotr(w[i-15], 7) ^ sha256_rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            unsigned int s1 = sha256_rotr(w[i-2], 17) ^ sha256_rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        unsigned int a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; i++) {
            unsigned int S1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
            unsigned int ch = (e & f) ^ ((~e) & g);
            unsigned int t1 = hh + S1 + ch + K[i] + w[i];
            unsigned int S0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
            unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            unsigned int t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    char buf[65];
    for (int i = 0; i < 8; i++) snprintf(buf + i * 8, 9, "%08x", h[i]);
    buf[64] = 0;
    return std::string(buf);
}

static const int kPwdIterations = 10000;

std::string passwordHash(const std::string& password) {
    static const char* hex = "0123456789abcdef";
    unsigned long long seed = (unsigned long long)time(NULL) ^
                              ((unsigned long long)GetTickCount() << 20) ^
                              (unsigned long long)(size_t)&password;
    unsigned long long x = seed | 1ULL;
    std::string salt;
    for (int i = 0; i < 16; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;   // xorshift64，够做盐
        salt += hex[x & 0xF];
    }
    std::string h = sha256Hex(salt + password);
    for (int i = 0; i < kPwdIterations; i++) h = sha256Hex(salt + h);
    return "sha256$" + std::to_string(kPwdIterations) + "$" + salt + "$" + h;
}

bool passwordIsHashed(const std::string& stored) {
    return stored.rfind("sha256$", 0) == 0;
}

bool passwordVerify(const std::string& password, const std::string& stored) {
    if (passwordIsHashed(stored)) {
        std::vector<std::string> parts = split(stored, '$');
        if (parts.size() < 4) return false;
        int iters = atoi(parts[1].c_str());
        if (iters < 1) iters = 1;
        const std::string& salt = parts[2];
        const std::string& want = parts[3];
        std::string h = sha256Hex(salt + password);
        for (int i = 0; i < iters; i++) h = sha256Hex(salt + h);
        return h == want;
    }
    // 旧数据：明文密码，登录成功后由调用方升级为哈希
    return stored == password;
}

// 稳定哈希（FNV-1a 64），用于缓存键；不用于安全场景
std::string sha1(const std::string& input) {
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < input.size(); i++) {
        h ^= (unsigned char)input[i];
        h *= 1099511628211ULL;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%016llx", h);
    return std::string(buf);
}


// ---------- 基础工具 ----------
std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> res;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) res.push_back(item);
    }
    if (s.length() > 0 && s[s.length() - 1] == delim) {
        // 末尾分隔符情况，不添加空串
    }
    return res;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string readFile(const std::string& path) {
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
}

bool writeFile(const std::string& path, const std::string& content) {
    std::string tmpPath = path + ".tmp";
    std::ofstream ofs(tmpPath.c_str(), std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "[writeFile] Failed to open temp file: " << tmpPath << std::endl;
        return false;
    }
    ofs << content;
    ofs.close();
    if (ofs.fail()) {
        std::cerr << "[writeFile] Failed to write content to temp file: " << tmpPath << std::endl;
        DeleteFileA(tmpPath.c_str());
        return false;
    }
    bool moved = false;
    for (int tries = 0; tries < 25; tries++) {
        if (MoveFileExA(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { moved = true; break; }
        DWORD err = GetLastError();
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) { Sleep(40); continue; }
        std::cerr << "[writeFile] Failed to move file: " << tmpPath << " to " << path << " (error code: " << err << ")" << std::endl;
        break;
    }
    if (!moved) { DeleteFileA(tmpPath.c_str()); return false; }
    return true;
}

// ---------- 跨进程数据写锁 ----------
static HANDLE g_dataMutex = NULL;
bool lockData() {
    if (!g_dataMutex) g_dataMutex = CreateMutexA(NULL, FALSE, "OIStationDataWrite");
    if (!g_dataMutex) return false;
    DWORD r = WaitForSingleObject(g_dataMutex, 60000);
    return (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
}
void unlockData() {
    if (g_dataMutex) ReleaseMutex(g_dataMutex);
}

bool fileExists(const std::string& path) {
    // 路径内部统一 UTF-8；用宽字符 API 检查以支持中文路径
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (wlen > 0) {
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);
        DWORD attr = GetFileAttributesW(wpath.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    }
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

std::string getTimeStr() {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

std::string getDateStr() {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    return std::string(buf);
}

std::string timeAddDays(int days) {
    time_t t = time(NULL) + (time_t)days * 86400;
    struct tm* tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

bool isTimeExpired(const std::string& timeStr) {
    if (timeStr.empty()) return true;
    return timeStr < getTimeStr();
}

std::string genId() {
    static std::atomic<long long> counter(0);
    static const long long rs = []() {
        srand((unsigned)(GetTickCount() ^ (unsigned)GetCurrentProcessId()));
        return (long long)(rand() % 1000);
    }();
    char buf[64];
    long long ms = (long long)GetTickCount();
    long long pid = (long long)GetCurrentProcessId();
    long long seq = counter++ % 1000000;
    snprintf(buf, sizeof(buf), "%lld%lld%03lld%06lld", ms, pid, rs, seq);
    return std::string(buf);
}

std::string urlDecode(const std::string& str) {
    std::string res;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int h = std::stoi(str.substr(i+1, 2), 0, 16);
            res += char(h);
            i += 2;
        } else if (str[i] == '+') {
            res += ' ';
        } else {
            res += str[i];
        }
    }
    return res;
}

std::string urlEncode(const std::string& str) {
    std::string res;
    for (char c : str) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            res += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            res += buf;
        }
    }
    return res;
}

std::string htmlEscape(const std::string& str) {
    std::string res;
    for (char c : str) {
        switch(c) {
            case '<': res += "&lt;"; break;
            case '>': res += "&gt;"; break;
            case '&': res += "&amp;"; break;
            case '"': res += "&quot;"; break;
            case '\'': res += "&#39;"; break;
            default: res += c;
        }
    }
    return res;
}

int stringToInt(const std::string& str) {
    if (str.empty()) return 0;
    try {
        return std::stoi(str);
    } catch (const std::exception&) {
        return 0;
    }
}

std::string intToString(int num) {
    return std::to_string(num);
}

long long stringToLongLong(const std::string& str) {
    if (str.empty()) return 0;
    try {
        return std::stoll(str);
    } catch (const std::exception&) {
        return 0;
    }
}

std::string getFileExtension(const std::string& path) {
    size_t pos = path.find_last_of('.');
    if (pos == std::string::npos) return "";
    return path.substr(pos + 1);
}

bool createDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::string dir = path;
    // 去掉末尾反斜杠
    while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) {
        dir.pop_back();
    }
    if (dir.empty()) return false;
    DWORD attr = GetFileAttributesA(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) {
        std::string parent = dir.substr(0, pos);
        if (!parent.empty()) {
            if (!createDirectory(parent)) return false;
        }
    }
    return CreateDirectoryA(dir.c_str(), NULL) != 0;
}

bool removeDirectory(const std::string& path) {
    std::string cmd = "rmdir /s /q \"" + path + "\"";
    return system(cmd.c_str()) == 0;
}

std::string getCurrentDir() {
    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    return std::string(buffer);
}

std::string getExeDir() {
    // 用宽字符 API 获取 exe 路径并转成 UTF-8，避免中文路径乱码（GetModuleFileNameA 返回 GBK）
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring wExe(buffer);
    size_t pos = wExe.find_last_of(L"\\/");
    std::wstring wDir = (pos != std::wstring::npos) ? wExe.substr(0, pos + 1) : L".\\";
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wDir.c_str(), -1, NULL, 0, NULL, NULL);
    if (ulen > 0) {
        std::string out(ulen - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wDir.c_str(), -1, &out[0], ulen, NULL, NULL);
        return out;
    }
    return ".\\";
}

// 自动查找编译器可执行文件路径（支持中文路径），找不到返回 "g++"
std::string autoFindGpp() {
    std::string exeDir = getExeDir();
    std::vector<std::string> paths = {
        exeDir + "mingw64\\bin\\g++.exe",
        exeDir + "MinGW\\bin\\g++.exe",
        exeDir + "mingw\\bin\\g++.exe",
        ".\\mingw64\\bin\\g++.exe",
        ".\\MinGW\\bin\\g++.exe",
        ".\\mingw\\bin\\g++.exe",
        "C:\\MinGW\\bin\\g++.exe",
        "C:\\msys64\\mingw64\\bin\\g++.exe",
        "g++",
        "g++.exe"
    };
    for (size_t i = 0; i < paths.size(); ++i) {
        if (fileExists(paths[i]) || paths[i] == "g++" || paths[i] == "g++.exe") {
            return paths[i];
        }
    }
    return "g++";
}

std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

// ---------- 编码/换行工具 ----------
// 将 CRLF / CR 归一化为 LF
std::string normalizeNewlines(const std::string& str) {
    std::string r;
    r.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\r') continue;  // 丢弃 \r，使 CRLF / 单独 CR 变为 LF
        r += str[i];
    }
    return r;
}

// 判断字符串是否为合法 UTF-8 编码
bool isValidUtf8(const std::string& str) {
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) { ++i; continue; }
        int n = 0;
        if ((c & 0xE0) == 0xC0) n = 1;
        else if ((c & 0xF0) == 0xE0) n = 2;
        else if ((c & 0xF8) == 0xF0) n = 3;
        else return false;
        if (i + n >= str.size()) return false;
        for (int k = 1; k <= n; ++k)
            if (((unsigned char)str[i + k] & 0xC0) != 0x80) return false;
        i += n + 1;
    }
    return true;
}

// 将子进程输出（系统 ANSI/GBK 编码）转为 UTF-8；若已是 UTF-8 则原样返回
std::string toUtf8FromSystem(const std::string& str) {
    if (str.empty()) return str;
    if (isValidUtf8(str)) return str;
    int wlen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0);
    if (wlen <= 0) return str;
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), &wstr[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wlen, NULL, 0, NULL, NULL);
    if (ulen <= 0) return str;
    std::string out(ulen, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wlen, &out[0], ulen, NULL, NULL);
    return out;
}

// ---------- JSON 工具 ----------
std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string jsonUnescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '\\' && i + 1 < s.length()) {
            char c = s[i + 1];
            switch (c) {
                case '"':  out += '"';  i++; break;
                case '\\': out += '\\'; i++; break;
                case '/':  out += '/';  i++; break;
                case 'b':  out += '\b'; i++; break;
                case 'f':  out += '\f'; i++; break;
                case 'n':  out += '\n'; i++; break;
                case 'r':  out += '\r'; i++; break;
                case 't':  out += '\t'; i++; break;
                case 'u': {
                    if (i + 5 < s.length()) {
                        std::string hex = s.substr(i + 2, 4);
                        unsigned int code = (unsigned int)strtoul(hex.c_str(), NULL, 16);
                        i += 5;
                        // 代理对（emoji 等增补平面字符）
                        if (code >= 0xD800 && code <= 0xDBFF && i + 6 < s.length() &&
                            s[i + 1] == '\\' && s[i + 2] == 'u') {
                            std::string loHex = s.substr(i + 3, 4);
                            unsigned int lo = (unsigned int)strtoul(loHex.c_str(), NULL, 16);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                code = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                                i += 6;
                            }
                        }
                        // 按 UTF-8 编码输出（之前直接截成单字节，中文会变乱码）
                        if (code < 0x80) {
                            out += (char)code;
                        } else if (code < 0x800) {
                            out += (char)(0xC0 | (code >> 6));
                            out += (char)(0x80 | (code & 0x3F));
                        } else if (code < 0x10000) {
                            out += (char)(0xE0 | (code >> 12));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        } else {
                            out += (char)(0xF0 | (code >> 18));
                            out += (char)(0x80 | ((code >> 12) & 0x3F));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                    } else {
                        out += s[i];
                    }
                    break;
                }
                default:
                    out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string jsonStringify(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string jsonStringify(int value) {
    return std::to_string(value);
}

std::string jsonStringify(bool value) {
    return value ? "true" : "false";
}

std::string jsonStringify(const std::vector<std::string>& arr) {
    std::string out = "[";
    for (size_t i = 0; i < arr.size(); ++i) {
        out += jsonStringify(arr[i]);
        if (i + 1 < arr.size()) out += ",";
    }
    out += "]";
    return out;
}

std::string jsonStringify(const std::map<std::string, std::string>& obj) {
    std::string out = "{";
    size_t count = 0;
    for (const auto& kv : obj) {
        out += jsonStringify(kv.first) + ":" + jsonStringify(kv.second);
        if (++count < obj.size()) out += ",";
    }
    out += "}";
    return out;
}

// ---------- 增强版 JSON 对象解析 ----------
std::map<std::string, std::string> jsonParseObject(const std::string& json) {
    std::map<std::string, std::string> result;
    std::string s = trim(json);
    if (s.empty() || s[0] != '{' || s[s.size()-1] != '}') return result;
    
    s = s.substr(1, s.size() - 2);
    if (s.empty()) return result;
    
    int brace = 0, bracket = 0;
    bool inString = false, escape = false;
    std::string current;
    
    for (char c : s) {
        if (escape) {
            current += c;
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            current += c;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            current += c;
            continue;
        }
        if (!inString) {
            if (c == '{') brace++;
            else if (c == '}') brace--;
            else if (c == '[') bracket++;
            else if (c == ']') bracket--;
        }
        if (c == ',' && brace == 0 && bracket == 0 && !inString) {
            if (!current.empty()) {
                size_t pos = current.find(':');
                if (pos != std::string::npos) {
                    std::string key = trim(current.substr(0, pos));
                    std::string value = trim(current.substr(pos + 1));
                    if (key.size() >= 2 && key[0] == '"' && key[key.size()-1] == '"') {
                        key = jsonUnescape(key.substr(1, key.size()-2));
                    }
                    if (value.size() >= 2 && value[0] == '"' && value[value.size()-1] == '"') {
                        value = jsonUnescape(value.substr(1, value.size()-2));
                    } else if (value == "true") {
                        value = "1";
                    } else if (value == "false") {
                        value = "0";
                    } else if (value == "null") {
                        value = "";
                    }
                    result[key] = value;
                }
                current.clear();
            }
        } else {
            current += c;
        }
    }
    // 处理最后一个键值对
    if (!current.empty()) {
        size_t pos = current.find(':');
        if (pos != std::string::npos) {
            std::string key = trim(current.substr(0, pos));
            std::string value = trim(current.substr(pos + 1));
            if (key.size() >= 2 && key[0] == '"' && key[key.size()-1] == '"') {
                key = jsonUnescape(key.substr(1, key.size()-2));
            }
            if (value.size() >= 2 && value[0] == '"' && value[value.size()-1] == '"') {
                value = jsonUnescape(value.substr(1, value.size()-2));
            } else if (value == "true") {
                value = "1";
            } else if (value == "false") {
                value = "0";
            } else if (value == "null") {
                value = "";
            }
            result[key] = value;
        }
    }
    return result;
}

// ---------- 增强版 JSON 数组解析 ----------
std::vector<std::string> jsonParseArray(const std::string& json) {
    std::vector<std::string> result;
    std::string s = trim(json);
    if (s.empty() || s[0] != '[' || s[s.size()-1] != ']') return result;
    s = s.substr(1, s.size() - 2);
    if (s.empty()) return result;
    
    int brace = 0, bracket = 0;
    bool inString = false, escape = false;
    std::string current;
    
    for (char c : s) {
        if (escape) {
            current += c;
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            current += c;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            current += c;
            continue;
        }
        if (!inString) {
            if (c == '{') brace++;
            else if (c == '}') brace--;
            else if (c == '[') bracket++;
            else if (c == ']') bracket--;
        }
        if (c == ',' && brace == 0 && bracket == 0 && !inString) {
            if (!current.empty()) {
                std::string val = trim(current);
                if (val.size() >= 2 && val[0] == '"' && val[val.size()-1] == '"') {
                    val = jsonUnescape(val.substr(1, val.size()-2));
                }
                result.push_back(val);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        std::string val = trim(current);
        if (val.size() >= 2 && val[0] == '"' && val[val.size()-1] == '"') {
            val = jsonUnescape(val.substr(1, val.size()-2));
        }
        result.push_back(val);
    }
    return result;
}

// ---------- 时间工具 ----------
time_t parseTime(const std::string& timeStr) {
    struct tm tm = {};
    int year, month, day, hour, min, sec;
    if (sscanf(timeStr.c_str(), "%d-%d-%d %d:%d:%d",
               &year, &month, &day, &hour, &min, &sec) == 6) {
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;
        return mktime(&tm);
    }
    return 0;
}

std::string getCurrentTimeStr() {
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

// ---------- Markdown 渲染 ----------

// 转义行内 HTML 敏感字符
static std::string mdEscape(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        switch (c) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            default: r += c;
        }
    }
    return r;
}

// 行内元素解析：代码、加粗、斜体、删除线、链接、图片、公式
static std::string mdInline(const std::string& src) {
    std::string out;
    size_t i = 0, n = src.size();
    while (i < n) {
        // 行内代码 `...`
        if (src[i] == '`') {
            size_t j = src.find('`', i + 1);
            if (j != std::string::npos) {
                out += "<code>" + mdEscape(src.substr(i + 1, j - i - 1)) + "</code>";
                i = j + 1;
                continue;
            }
        }
        // 行内公式 $...$  （保留内容，前端 KaTeX 渲染）
        if (src[i] == '$') {
            size_t j = src.find('$', i + 1);
            if (j != std::string::npos) {
                std::string latex = src.substr(i + 1, j - i - 1);
                out += "<span class='md-math' data-latex=\"" + mdEscape(latex) + "\">" + mdEscape(latex) + "</span>";
                i = j + 1;
                continue;
            }
        }
        // 图片 ![alt](url)
        if (src.compare(i, 2, "![") == 0) {
            size_t cb = src.find(']', i + 2);
            if (cb != std::string::npos && cb + 1 < n && src[cb + 1] == '(') {
                size_t ce = src.find(')', cb + 2);
                if (ce != std::string::npos) {
                    std::string alt = mdEscape(src.substr(i + 2, cb - i - 2));
                    std::string url = src.substr(cb + 2, ce - cb - 2);
                    out += "<img src=\"" + mdEscape(url) + "\" alt=\"" + alt + "\" style='max-width:100%;'/>";
                    i = ce + 1;
                    continue;
                }
            }
        }
        // 链接 [text](url)
        if (src[i] == '[') {
            // 颜色 BB 码 [color=red]文字[/color] / [color=#ff0000] / [color=rgb(255,0,0)]
            if (src.compare(i, 7, "[color=") == 0) {
                size_t cb = src.find(']', i + 7);
                if (cb != std::string::npos) {
                    std::string col = src.substr(i + 7, cb - i - 7);
                    size_t close = src.find("[/color]", cb + 1);
                    if (close != std::string::npos) {
                        // 校验颜色值只允许安全字符，防 CSS 注入
                        bool okc = !col.empty();
                        for (size_t z = 0; okc && z < col.size(); z++) {
                            char cc = col[z];
                            if (!(isalnum((unsigned char)cc) || cc == '#' || cc == ',' || cc == '%' || cc == '(' || cc == ')' || cc == '-' || cc == '.' || cc == ' ')) okc = false;
                        }
                        if (okc) {
                            out += "<span style='color:" + mdEscape(col) + "'>" + mdInline(src.substr(cb + 1, close - cb - 1)) + "</span>";
                            i = close + 8;
                            continue;
                        }
                    }
                }
            }
            size_t cb = src.find(']', i + 1);
            if (cb != std::string::npos && cb + 1 < n && src[cb + 1] == '(') {
                size_t ce = src.find(')', cb + 2);
                if (ce != std::string::npos) {
                    std::string text = mdInline(src.substr(i + 1, cb - i - 1));
                    std::string url = src.substr(cb + 2, ce - cb - 2);
                    // 仅允许 http/https/mailto，防止 javascript: 注入
                    std::string lurl = url;
                    bool ok = (lurl.compare(0, 7, "http://") == 0 || lurl.compare(0, 8, "https://") == 0 || lurl.compare(0, 7, "mailto:") == 0 || lurl.compare(0, 1, "#") == 0);
                    if (ok) out += "<a href=\"" + mdEscape(lurl) + "\" target='_blank' rel='noopener'>" + text + "</a>";
                    else out += text;
                    i = ce + 1;
                    continue;
                }
            }
        }
        // 加粗 **text**
        if (src.compare(i, 2, "**") == 0) {
            size_t j = src.find("**", i + 2);
            if (j != std::string::npos && j > i + 2) {
                out += "<strong>" + mdInline(src.substr(i + 2, j - i - 2)) + "</strong>";
                i = j + 2;
                continue;
            }
        }
        // 斜体 *text*
        if (src[i] == '*' && i + 1 < n && src[i + 1] != '*') {
            size_t j = src.find('*', i + 1);
            if (j != std::string::npos && j > i + 1) {
                out += "<em>" + mdInline(src.substr(i + 1, j - i - 1)) + "</em>";
                i = j + 1;
                continue;
            }
        }
        // 删除线 ~~text~~
        if (src.compare(i, 2, "~~") == 0) {
            size_t j = src.find("~~", i + 2);
            if (j != std::string::npos && j > i + 2) {
                out += "<del>" + mdInline(src.substr(i + 2, j - i - 2)) + "</del>";
                i = j + 2;
                continue;
            }
        }
        // @提及：@用户名 → 个人主页链接（支持中文用户名，遇空白/标点截断）
        if (src[i] == '@') {
            size_t k = i + 1;
            std::string nm;
            while (k < n && nm.size() < 60) {
                unsigned char c = (unsigned char)src[k];
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
                if (c < 0x80) {
                    if (!(isalnum(c) || c == '_' || c == '.' || c == '-')) break;
                } else if (src.compare(k, 3, "\xEF\xBC\x8C") == 0 || src.compare(k, 3, "\xE3\x80\x82") == 0 ||
                           src.compare(k, 3, "\xEF\xBC\x9A") == 0 || src.compare(k, 3, "\xEF\xBC\x9B") == 0 ||
                           src.compare(k, 3, "\xEF\xBC\x81") == 0 || src.compare(k, 3, "\xEF\xBC\x9F") == 0 ||
                           src.compare(k, 3, "\xEF\xBC\x89") == 0 || src.compare(k, 3, "\xE3\x80\x81") == 0 ||
                           src.compare(k, 3, "\xE3\x80\x90") == 0 || src.compare(k, 3, "\xE3\x80\x8B") == 0) {
                    break;
                }
                nm += src[k];
                k++;
            }
            if (!nm.empty()) {
                out += "<a class='mention' href='/user/" + urlEncode(nm) + "'>@" + mdEscape(nm) + "</a>";
                i = k;
                continue;
            }
        }
        out += src[i];
        i++;
    }
    return out;
}

// 判断一行是否是表格分隔行（如 |---|---| 或 --- | ---）
static bool isTableSeparator(const std::string& line) {
    std::string t = line;
    // 去首尾空格
    size_t b = t.find_first_not_of(" \t");
    if (b == std::string::npos) return false;
    size_t e = t.find_last_not_of(" \t");
    t = t.substr(b, e - b + 1);
    if (!t.empty() && t[0] == '|') t = t.substr(1);
    if (!t.empty() && t[t.size() - 1] == '|') t = t.substr(0, t.size() - 1);
    // 用 | 分割，每段应是 :--- / ---: / --- 组合
    std::string seg;
    bool hasDash = false;
    for (size_t i = 0; i <= t.size(); i++) {
        char c = (i < t.size()) ? t[i] : '|';
        if (c == '|') {
            // 校验 seg
            std::string s = seg;
            while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.erase(0, 1);
            while (!s.empty() && (s[s.size() - 1] == ' ' || s[s.size() - 1] == '\t')) s.erase(s.size() - 1);
            if (s.empty()) { seg.clear(); continue; }
            if (s[0] == ':') s = s.substr(1);
            if (!s.empty() && s[s.size() - 1] == ':') s = s.substr(0, s.size() - 1);
            if (s.empty()) { seg.clear(); continue; }
            for (size_t k = 0; k < s.size(); k++) if (s[k] == '-') hasDash = true;
            seg.clear();
        } else {
            seg += c;
        }
    }
    return hasDash;
}

// 解析表格行，返回 <tr> 内的单元格
static std::string mdTableRow(const std::string& line, bool header) {
    std::string t = line;
    size_t b = t.find_first_not_of(" \t");
    if (b == std::string::npos) b = 0;
    size_t e = t.find_last_not_of(" \t");
    t = t.substr(b, e - b + 1);
    if (!t.empty() && t[0] == '|') t = t.substr(1);
    if (!t.empty() && t[t.size() - 1] == '|') t = t.substr(0, t.size() - 1);
    std::string out;
    size_t i = 0;
    while (i <= t.size()) {
        size_t j = t.find('|', i);
        std::string cell = (j == std::string::npos) ? t.substr(i) : t.substr(i, j - i);
        // 去首尾空格
        size_t cb = cell.find_first_not_of(" \t");
        if (cb == std::string::npos) cell = ""; else {
            size_t ce = cell.find_last_not_of(" \t");
            cell = cell.substr(cb, ce - cb + 1);
        }
        out += header ? "<th>" : "<td>";
        out += mdInline(cell);
        out += header ? "</th>" : "</td>";
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return out;
}

std::string markdownToHtml(const std::string& md) {
    // 按行解析块级元素
    std::istringstream iss(md);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(iss, line)) lines.push_back(line);

    std::string out;
    bool inCode = false;
    std::string codeLang, codeBuf;
    bool inList = false;       // 是否处于无序/有序列表
    bool inQuote = false;
    std::vector<std::string> tableBuf;
    std::string paraBuf;

    auto flushPara = [&]() {
        if (!paraBuf.empty()) {
            out += "<p>" + mdInline(paraBuf) + "</p>\n";
            paraBuf.clear();
        }
    };

    for (size_t li = 0; li < lines.size(); li++) {
        std::string raw = lines[li];
        std::string t = raw;
        // 去行尾空白
        size_t te = t.find_last_not_of(" \t\r");
        t = (te == std::string::npos) ? "" : t.substr(0, te + 1);

        // 代码块结束
        if (inCode) {
            if (t.compare(0, 3, "```") == 0) {
                out += "<pre><code" + (codeLang.empty() ? "" : " class='lang-" + codeLang + "'") + ">" + mdEscape(codeBuf) + "</code></pre>\n";
                codeBuf.clear(); codeLang.clear(); inCode = false;
            } else {
                codeBuf += raw + "\n";
            }
            continue;
        }
        // 代码块开始
        if (t.compare(0, 3, "```") == 0) {
            flushPara();
            if (inList) { out += "</ul>"; inList = false; }
            if (inQuote) { out += "</blockquote>"; inQuote = false; }
            codeLang = t.substr(3);
            // 去空格
            size_t lb = codeLang.find_first_not_of(" \t");
            if (lb == std::string::npos) codeLang = ""; else codeLang = codeLang.substr(lb);
            inCode = true;
            continue;
        }

        // 空行：结束段落/列表
        if (t.empty()) {
            flushPara();
            if (inList) { out += "</ul>"; inList = false; }
            if (inQuote) { out += "</blockquote>"; inQuote = false; }
            continue;
        }

        // 标题
        size_t h = 0;
        while (h < t.size() && h < 6 && t[h] == '#') h++;
        if (h > 0 && h <= 6 && (h == t.size() || t[h] == ' ')) {
            flushPara();
            if (inList) { out += "</ul>"; inList = false; }
            if (inQuote) { out += "</blockquote>"; inQuote = false; }
            std::string body = (h < t.size()) ? t.substr(h + 1) : "";
            out += "<h" + std::to_string(h) + ">" + mdInline(body) + "</h" + std::to_string(h) + ">\n";
            continue;
        }

        // 表格：若当前是分隔行且上一行是表格数据
        if (!tableBuf.empty() && isTableSeparator(t)) {
            flushPara();
            out += "<table><thead><tr>" + mdTableRow(tableBuf.back(), true) + "</tr></thead><tbody>";
            tableBuf.clear();
            // 后续数据行直到空行
            size_t j = li + 1;
            while (j < lines.size()) {
                std::string nr = lines[j];
                size_t ne = nr.find_last_not_of(" \t\r");
                std::string nt = (ne == std::string::npos) ? "" : nr.substr(0, ne + 1);
                if (nt.empty()) break;
                if (nt[0] == '|' || nt.find('|') != std::string::npos) {
                    out += "<tr>" + mdTableRow(nt, false) + "</tr>";
                    j++;
                } else break;
            }
            out += "</tbody></table>\n";
            li = j - 1;
            continue;
        }
        // 表格数据行（缓存，等待下一行判定）
        if ((!t.empty() && t[0] == '|' && t.find('|') != std::string::npos) ||
            (t.find('|') != std::string::npos && t.find('|') != t.size() - 1 && t.find('|') != 0 && t[0] != '-')) {
            if (tableBuf.empty()) flushPara();
            if (inList) { out += "</ul>"; inList = false; }
            if (inQuote) { out += "</blockquote>"; inQuote = false; }
            tableBuf.push_back(t);
            continue;
        }

        // 列表
        if ((t.size() >= 2 && (t[0] == '-' || t[0] == '+' || t[0] == '*') && t[1] == ' ')) {
            flushPara();
            if (!inList) { out += "<ul>"; inList = true; }
            out += "<li>" + mdInline(t.substr(2)) + "</li>\n";
            continue;
        }
        // 有序列表
        {
            size_t di = 0;
            while (di < t.size() && t[di] >= '0' && t[di] <= '9') di++;
            if (di > 0 && di < t.size() && t[di] == '.' && di + 1 < t.size() && t[di + 1] == ' ') {
                flushPara();
                if (!inList) { out += "<ol>"; inList = true; }
                out += "<li>" + mdInline(t.substr(di + 2)) + "</li>\n";
                continue;
            }
        }

        // 引用
        if (t[0] == '>') {
            flushPara();
            if (inList) { out += "</ul>"; inList = false; }
            if (!inQuote) { out += "<blockquote>"; inQuote = true; }
            out += mdInline(t.substr(1)) + "\n";
            continue;
        }

        // 分割线
        if ((t == "---" || t == "***" || t == "___") && !inList) {
            flushPara();
            out += "<hr/>\n";
            continue;
        }

        // 普通段落行
        flushPara();
        if (inList) { out += "</ul>"; inList = false; }
        if (inQuote) { out += "</blockquote>"; inQuote = false; }
        paraBuf += (paraBuf.empty() ? "" : " ") + raw;
    }
    flushPara();
    if (inCode) out += "<pre><code>" + mdEscape(codeBuf) + "</code></pre>\n";
    if (inList) out += "</ul>\n";
    if (inQuote) out += "</blockquote>\n";
    return out;
}

// ---------- 图片处理 ----------
std::string getImageMimeType(const std::string& filePath) {
    std::string ext = getFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "bmp") return "image/bmp";
    if (ext == "webp") return "image/webp";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    return "application/octet-stream";
}

static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64Encode(const std::string& data) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (size_t idx = 0; idx < data.length(); idx++) {
        char_array_3[i++] = data[idx];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

std::string base64Decode(const std::string& encoded_string) {
    size_t in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_];
        in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; i < 3; i++)
                ret += char_array_3[i];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; j < i - 1; j++)
            ret += char_array_3[j];
    }

    return ret;
}

std::string imageToBase64(const std::string& filePath) {
    if (!fileExists(filePath)) return "";
    std::string data = readFile(filePath);
    if (data.empty()) return "";
    std::string base64 = base64Encode(data);
    std::string mimeType = getImageMimeType(filePath);
    return "data:" + mimeType + ";base64," + base64;
}

bool saveBase64Image(const std::string& base64Data, const std::string& outputPath) {
    size_t commaPos = base64Data.find(',');
    if (commaPos == std::string::npos) return false;
    std::string encoded = base64Data.substr(commaPos + 1);
    std::string decoded = base64Decode(encoded);
    if (decoded.empty()) return false;
    return writeFile(outputPath, decoded);
}

std::string generateImageName(const std::string& extension) {
    return genId() + "." + extension;
}

bool isBase64Image(const std::string& str) {
    return str.find("data:image/") == 0 && str.find(";base64,") != std::string::npos;
}
// ---------- LLM pricing ----------
double llmAutoMultiplier(const std::string& model) {
    std::string s = model;
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
    size_t p = s.find('b');
    if (p == std::string::npos) return 1.0;
    size_t q = p;
    while (q > 0 && (isdigit((unsigned char)s[q-1]) || s[q-1] == '.')) q--;
    if (q == p) return 1.0;
    std::string num = s.substr(q, p - q);
    if (num.empty()) return 1.0;
    double b = atof(num.c_str());
    double f = (b < 1.0) ? 1.0 : b;
    if (f <= 0) f = 1.0;
    return f;
}
double llmModelMultiplier(const std::string& model) {
    std::string cfg = readFile("./data/llm_pricing.json");
    if (!cfg.empty()) {
        auto o = jsonParseObject(cfg);
        if (o.count(model)) {
            double v = atof(o[model].c_str());
            if (v >= 0.5) return v;
        }
    }
    return llmAutoMultiplier(model);
}
void llmSetModelMultiplier(const std::string& model, double mult) {
    auto o = jsonParseObject(readFile("./data/llm_pricing.json"));
    if (mult < 0.5) mult = 0.5;
    char b[32]; snprintf(b, sizeof(b), "%.2f", mult);
    o[model] = b;
    writeFile("./data/llm_pricing.json", jsonStringify(o));
}
void llmRemoveModelMultiplier(const std::string& model) {
    auto o = jsonParseObject(readFile("./data/llm_pricing.json"));
    o.erase(model);
    writeFile("./data/llm_pricing.json", jsonStringify(o));
}
std::string llmPricingJson() {
    return readFile("./data/llm_pricing.json");
}


} // namespace common
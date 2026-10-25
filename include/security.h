#ifndef OI_SECURITY_H
#define OI_SECURITY_H

#include <string>
#include <vector>
#include <map>

// Security module: IP ban / over-speed detection / simple captcha
namespace security {

// ---------- IP ban ----------
bool isIpBanned(const std::string& ip);
void banIp(const std::string& ip);
void unbanIp(const std::string& ip);
std::vector<std::string> bannedIps();
void loadBans();
void saveBans();

// ---------- Security thresholds (admin-configurable) ----------
struct SecConfig {
    int ratePerSec    = 30;  // max requests per second
    int burstThreshold = 100; // requests in window that triggers auto-ban
    int windowSec     = 5;   // frequency window (seconds)
    int autoBanSec    = 30;  // auto-ban duration (seconds)
    int captchaMinSec  = 1;   // min seconds before captcha answer accepted (anti-script)
    int captchaExpireSec = 300; // captcha expiry (seconds)
    int captchaStrict  = 1;   // 0 weak, 1 normal, 2 strict
    int captchaEnabled = 1;   // master switch: 1 on, 0 off
    int capLogin   = 1;  int capRegister = 1;  int capSubmit  = 1;
    int capBlog    = 1;  int capDiscuss = 1;  int capTask    = 1;  int capShop = 1;
  int capMessage = 0;
  int capGroup = 1;
  int capBenben  = 1;   // captcha on 犇犇 (feed) publish

  // ===== Form length limits (admin-configurable) =====
  int maxBenben    = 500;   // 犇犇内容
  int maxBlog      = 10000; // 博客内容
  int maxBlogTitle = 100;   // 博客/讨论标题
  int maxDiscuss   = 10000; // 讨论内容
  int maxComment   = 2000;  // 评论
  int maxTaskTitle = 100;   // 任务标题
  int maxTaskDesc  = 5000;  // 任务描述
  int maxTaskMsg   = 2000;  // 任务私密消息
  int maxMessage   = 2000;  // 私信/群聊消息
  int maxGroupName = 50;    // 群聊名称
  int maxBio       = 500;   // 个人简介
  int maxUsername  = 20;    // 用户名
  int maxCode      = 100000; // 提交代码长度

  // ===== Publish frequency limits (seconds between posts) =====
  int blogIntervalSec    = 5;  // 博客发布最小间隔(秒)，0=关闭
  int discussIntervalSec = 3;  // 讨论发布最小间隔(秒)，0=关闭
  int submitIntervalSec  = 10; // 代码提交最小间隔(秒)，0=关闭
  int benbenIntervalSec  = 5;  // 犇犇发布最小间隔(秒)，0=关闭
  int registerIntervalSec = 60; // 同一IP注册最小间隔(秒)，0=关闭

  // ===== 用户出题限制 (admin-configurable) =====
  int maxProblemId        = 20;    // 题号长度限制
  int maxProblemTitle     = 100;   // 题目名称长度限制
  int maxProblemDesc      = 20000; // 题面(描述)长度限制
  int maxProblemSample    = 2000;  // 样例长度限制
  int maxUserProblems     = 10;    // 每个用户最多出题数
  int maxUserProblemTime  = 2000;  // 普通用户出题时间限制上限(ms)
  int maxUserProblemMem   = 256;   // 普通用户出题内存限制上限(MB)

  // ===== 自定义测试(试运行)限制 (admin-configurable) =====
  int maxCustomTestTime   = 2000;    // 自定义测试时间限制上限(ms)
  int maxCustomTestMem    = 256;     // 自定义测试内存限制上限(MB)
  int maxCustomTestCode   = 50000;   // 自定义测试代码长度
  int maxCustomTestData   = 100000;  // 自定义测试输入数据长度
  int customTestIntervalSec = 5;     // 自定义测试提交间隔(秒, 0=不限)

  // ===== Hack重测 (admin-configurable) =====
  int hackRetestEnabled = 1;         // Hack数据通过标程检测后加入正式数据并重测所有AC提交
  int hackIntervalSec = 5;           // Hack提交间隔(秒, 0=不限)
  int judgeThreads = 4;              // 评测线程数

  // ===== 编译器(g++)路径 (admin-configurable; 空=自动搜索) =====
  std::string gppPath     = "";    // 例如 D:\...\MinGW64\bin\g++.exe

  // ===== 编译安全：提交代码静态检查 (admin-configurable) =====
  int codeGuardEnabled   = 1;   // 总开关：0 = 完全跳过静态检查（只保留编译）
  int guardHeaders       = 1;   // 危险头文件（windows.h / sys/ / winsock ...）
  int guardProcess       = 1;   // 进程与系统调用（system / fork / exec ...）
  int guardWinApi        = 1;   // Windows API（CreateProcess / VirtualAlloc ...）
  int guardNetwork       = 1;   // 网络（socket / connect / send ...）
  int guardDll           = 1;   // 动态加载（dlopen / LoadLibrary ...）
  int guardFileFuncs     = 1;   // 文件读写（freopen / fopen / ifstream / ofstream ...）
  int guardPathEscape    = 1;   // 字符串字面量里的绝对路径 / .. 上跳
  int guardCompilerExt   = 1;   // 编译器扩展（#pragma comment / __declspec / __attribute__）
  std::string guardExtraFuncs;   // 额外禁止的函数（逗号/空格/换行分隔，管理员自定义）
  std::string guardAllowFuncs;   // 例外放行的函数（优先于所有禁止列表）
  std::string guardExtraHeaders; // 额外禁止的头文件（逗号/空格/换行分隔）
};

SecConfig getSecConfig();
void setSecConfig(const SecConfig& c);
void loadSecurityConfig();

// ---------- Request gate ----------
// returns 0 = allow, 403 = banned, 429 = rate limited
int gateRequest(const std::string& ip);
void pruneAutoBans();

// ---------- Frequency / ban info (for admin visualization) ----------
struct FreqInfo { std::string ip; int count; int rate; };
std::vector<FreqInfo> getFrequencyTop(int n);

struct BanInfo { std::string ip; bool autoBan; long long until; };
std::vector<BanInfo> getBansInfo();

// ---------- Simple captcha ----------
// Generate an arithmetic captcha, returns question text (e.g. "3 + 5 = ?"),
// fills `token`. Token is single-use and expires.
std::string createCaptcha(std::string& token);
// Verify captcha (removes on success, single-use)
bool verifyCaptcha(const std::string& token, const std::string& answer);
// Risk score 0-100 from request headers (device-state detection)
int assessRisk(const std::map<std::string, std::string>& headers);
// HTML snippet for a captcha: hidden token + question + answer input
std::string captchaFields();
// Adaptive captcha based on request headers / risk
std::string captchaFields(const std::map<std::string, std::string>& headers);
// Per-feature captcha control (feature: login/register/submit/blog/discuss/task/shop)
bool captchaFor(const std::string& feature);
std::string captchaFieldsFor(const std::string& feature, const std::map<std::string, std::string>& headers = std::map<std::string, std::string>());
bool verifyCaptchaFor(const std::string& token, const std::string& answer, const std::string& feature);
// JSON 版验证码（给 IDE / API 用）：{"required":true,"token":"..","question":"3 + 5 = ?"}
std::string captchaJsonFor(const std::string& feature);

// ---------- Per-action throttle ----------
// Returns true if the action for `key` should be blocked (last action < minSec ago).
// minSec <= 0 disables throttling.
bool throttleAction(const std::string& key, int minSec);

// ---------- IP user activity (for admin ban panel) ----------
struct IpActivity { std::string username; long long time; };
// Record that `username` was recently active from `ip` (deduped, most-recent first)
void recordIpActivity(const std::string& ip, const std::string& username);
// Return up to maxN recent usernames active on `ip` (most recent first)
std::vector<IpActivity> ipActivity(const std::string& ip, int maxN);

}

#endif

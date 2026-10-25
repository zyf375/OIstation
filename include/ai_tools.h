#ifndef OI_AI_TOOLS_H
#define OI_AI_TOOLS_H

#include <string>
#include <vector>

// ================= AI 触手（function calling 工具） =================
// 每个用户一个独立工作区：./data/aiws/u<用户id>/
// 工具只能在这个工作区内读写文件；命令也在该目录下执行（带危险命令拦截 + 超时）。
namespace aitools {

struct Result {
    bool ok = false;
    std::string output;   // 回给模型的内容（纯文本）
};

// 给 DeepSeek 的 tools 清单（OpenAI 兼容格式）
std::string specJson();

// 是否启用命令执行（管理员可在 /admin/ai 关闭；关闭后 run_command 直接拒绝）
bool shellEnabled();
void setShellEnabled(bool on);

// 用户工作区目录（末尾带 /），不存在会自动创建
std::string workspaceDir(const std::string& userId);

// 用户云盘目录（与 /drive 页面同一位置：./files/<id>/，末尾带 /）
std::string driveDir(const std::string& userId);

// 是否为本模块支持的工具
bool isTool(const std::string& name);

// 执行工具。argsJson 是模型给的对象 JSON。userId 用于确定工作区。
Result run(const std::string& userId, const std::string& name, const std::string& argsJson);

// 上传文件到工作区
bool saveUpload(const std::string& userId, const std::string& filename,
                const std::string& data, std::string& savedRel);

// 用量日志（供「额度用量分析」页面聚合）
void logUsage(const std::string& userId, const std::string& model, const std::string& task,
              long long promptTok, long long compTok, long long cost);
struct LogRow {
    long long ts = 0;
    std::string userId, model, task;
    long long promptTok = 0, compTok = 0, cost = 0;
};
std::vector<LogRow> readUsageLog();

// ---------- AI 使用历史（debug / explain 等任务） ----------
struct RunRow {
    std::string id;
    std::string userId;
    std::string type;        // debug / explain
    std::string pid;         // 关联题号（可空）
    std::string title;       // 列表标题
    std::string time;
    std::string question;    // 提问摘要（截断）
    std::string answer;      // 完整回答（截断）
};
void saveRun(const RunRow& r);
// 按用户取最近记录（userId 为空 = 全站，仅管理员用）
std::vector<RunRow> readRuns(const std::string& userId, const std::string& type, int limit);
bool findRun(const std::string& id, RunRow& out);

} // namespace aitools

#endif

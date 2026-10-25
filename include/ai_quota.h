#ifndef OI_AI_QUOTA_H
#define OI_AI_QUOTA_H

#include <string>
#include <vector>

// ================= AI 额度（算力点） =================
// 计费单位：算力点。单价 = 算力点 / 千 token。
// 双重额度：滚动的 5 小时窗口额度 + 自然月额度，任一用尽即停止输出。
namespace aiquota {

struct Price {
    std::string model;
    double perK = 1.0;      // 算力点 / 千 token
};

struct Config {
    int windowHours = 5;        // 窗口长度（小时）
    long long defWindow = 100;  // 默认 5 小时额度（算力点）
    long long defMonth = 1000;  // 默认月额度（算力点）
    double defPerK = 1.0;       // 默认单价（算力点/千token）
    std::vector<Price> prices;  // 按模型覆盖单价
};

Config load();
void save(const Config& cfg);
double priceOf(const std::string& model);
long long costOf(const std::string& model, long long tokens);   // 向上取整

struct Info {
    long long windowQuota = 0;
    long long windowUsed = 0;
    long long monthQuota = 0;
    long long monthUsed = 0;
    long long extra = 0;      // 额外算力点余额（商店购买，用完即没，不随月份重置）
};

// userWin/userMonth 为 0 时使用全局默认值
Info info(const std::string& userId, long long userWin, long long userMonth);

// 批量查询（后台列表用）：只读一次配置文件与用量文件，避免每行都读盘
struct InfoQuery {
    std::string userId;
    long long windowQuota = 0;
    long long monthQuota = 0;
};
std::vector<Info> infoMany(const std::vector<InfoQuery>& qs);
long long remaining(const Info& i);
std::string jsonOf(const Info& i);

// 记录消耗；返回记录后的最新额度信息
Info record(const std::string& userId, long long cost, long long userWin, long long userMonth);
// 增加本月额度（商城/兑换码的 Token 折算成算力点）
void addBonusMonth(const std::string& userId, long long amount);
// 增加额外算力点余额（商店购买；不随月份重置，用完即没）
void addExtra(const std::string& userId, long long amount);
// 重置某人本月用量（管理用）
void resetMonth(const std::string& userId);

} // namespace aiquota

#endif

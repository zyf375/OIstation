#ifndef OI_AI_PLANS_H
#define OI_AI_PLANS_H

#include <string>
#include <vector>

struct User;

// ================= 用户套餐 =================
// 数据文件 data/ai_plans.json，每行一个套餐。
// 额度按套餐走：把套餐额度写入用户的 aiWindowQuota / aiMonthQuota，
// 这样所有既有额度逻辑（滚动窗口 + 月度）无需改动即可生效。
// models 为空 = 可用全部已启用模型；否则只允许列出的模型 id。
namespace aiplan {

struct Plan {
    std::string id;
    std::string name;
    std::string desc;
    long long windowQuota = 0;  // 5 小时窗口额度（算力点）
    long long monthQuota = 0;   // 月度额度（算力点）
    double price = 0;           // 售价（元/月，0 = 不展示价格）
    std::vector<std::string> models;
};

std::vector<Plan> load();
void save(const std::vector<Plan>& list);
bool find(const std::string& id, Plan& out);

bool allowedModel(const std::string& planId, const std::string& modelId);
bool userAllowedModel(const User& u, const std::string& modelId);
bool anyPlan();

// 把套餐额度与 id 写入用户（调用方负责 saveUsers）
bool applyPlan(User& u, const std::string& planId);
void ensureSeed();   // 首次运行创建 free / member / pro 三个套餐

// 新用户默认套餐（空 = 不自动分配，沿用全局默认额度）
std::string defaultPlanId();
void setDefaultPlanId(const std::string& id);

// 管理后台「中转测试」使用的模型（空 = 自动挑第一个免费模型，优先 OpenRouter）
std::string testModelId();
void setTestModelId(const std::string& id);

} // namespace aiplan

#endif

#ifndef OI_AI_MODELS_H
#define OI_AI_MODELS_H

#include <string>
#include <vector>

struct AiModelItem {
    std::string id;
    std::string name;
};

// 扫描结果（带是否免费 / 单价信息，供管理端挑选模型）
struct AiScannedModel {
    std::string id;
    std::string name;
    std::string providerId;
    bool freeModel = false;
    double promptPrice = -1;      // 美元 / 百万 token，-1 表示未知
    double completionPrice = -1;
};

// 扫描某个提供方真实可用模型（GET {baseUrl}/models，带 10 分钟缓存）
// refresh=true 强制重新扫描。freeOnly 的提供方只返回免费模型。
std::vector<AiScannedModel> aiScanProvider(const std::string& providerId, bool refresh, std::string* err);

// 兼容旧接口：扫描全部启用的提供方
std::vector<AiModelItem> aiScanModelsPublic(bool refresh, std::string* err);

// 中转自检：由服务端用指定模型实际发一次对话（Key 只在服务端使用）
struct AiTestResult {
    bool ok = false;
    std::string model;
    std::string providerName;
    std::string text;
    std::string error;
    int latencyMs = 0;
    long long promptTokens = 0;
    long long completionTokens = 0;
};
AiTestResult aiTestChat(const std::string& modelId, const std::string& prompt);

// 管理员辅助：用站内已启用模型做一次较长的纯文本生成（用于自动写数据生成器等）
// modelId 为空 = 自动挑选中转测试模型 / 第一个可用模型；effort = 0 跟随默认，1..3 = 低/中/高
std::string aiAssistText(const std::string& prompt, int maxTokens, std::string* err,
                         const std::string& modelId = std::string(), int effort = 0,
                         std::string* usedModel = NULL);

#endif

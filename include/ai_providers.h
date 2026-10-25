#ifndef OI_AI_PROVIDERS_H
#define OI_AI_PROVIDERS_H

#include <string>
#include <vector>

// ================= 多模型提供方 =================
// 数据文件 data/ai_providers.json：每行一个提供方（扁平 JSON）
// 字段：id / name / kind / baseUrl / apiKey / apiMode / enabled / freeOnly / models
// models 为「已启用模型」白名单：id|name;id|name（空 = 尚未挑选）
// API Key 只存在于服务端文件，任何页面 / 接口都只返回掩码。
namespace aiprov {

struct ModelRef {
    std::string id;
    std::string name;
};

struct Provider {
    std::string id;
    std::string name;
    std::string kind;      // deepseek / openrouter / openai-compatible
    std::string baseUrl;
    std::string apiKey;
    std::string apiMode;   // chat = /chat/completions（默认）；responses = /responses
    bool enabled = true;
    bool freeOnly = false; // 只允许免费模型（OpenRouter 等）
    std::vector<ModelRef> models;
};

// 展示用：模型 + 所属提供方
struct ModelEntry {
    std::string id;
    std::string name;
    std::string providerId;
    std::string providerName;
    bool freeModel = false;
};

std::vector<Provider> load();
void save(const std::vector<Provider>& list);

bool find(const std::string& id, Provider& out);
// 在所有「已启用提供方 + 已挑选模型」中解析模型
bool resolve(const std::string& modelId, Provider& p, ModelRef& m);
std::vector<ModelEntry> entries();

std::string maskKey(const std::string& key);
std::string modelsField(const std::vector<ModelRef>& models);
std::vector<ModelRef> parseModels(const std::string& field);

// 首次运行迁移：如果没有任何提供方，则从 data/ai.json 生成 DeepSeek 提供方
void ensureSeed();

} // namespace aiprov

#endif

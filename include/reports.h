#ifndef OI_REPORTS_H
#define OI_REPORTS_H

#include <string>
#include <vector>

// ================= 社区举报 =================
// 数据文件 data/reports.json，每行一条举报记录（避免改动 posts.json 结构）。
// status: pending（待处理） / handled（已处理） / ignored（已忽略）
namespace reports {

struct Report {
    std::string id;
    std::string targetType;    // post | comment
    std::string targetId;      // 帖子 id 或评论 id
    std::string postId;        // 评论所属帖子（post 类型时与 targetId 相同）
    std::string title;         // 被举报内容摘要
    std::string snippet;       // 内容片段
    std::string authorId;
    std::string authorName;
    std::string reason;        // 广告 / 辱骂 / 无意义 / 抄袭 / 政治敏感 / 其他
    std::string detail;
    std::string reporterId;
    std::string reporterName;
    std::string time;
    std::string status;
    std::string handledBy;
    std::string handledTime;
    std::string result;
};

std::vector<Report> load();
void save(const std::vector<Report>& list);
bool add(const Report& r);
bool find(const std::string& id, Report& out);
bool updateStatus(const std::string& id, const std::string& status,
                  const std::string& by, const std::string& result);
int pendingCount();
// 某人对同一目标的待处理举报数（防刷）
int pendingCountOf(const std::string& reporterId, const std::string& targetId);
// 删除目标内容后，把相关举报统一标记为 handled
void markTargetHandled(const std::string& targetId, const std::string& by, const std::string& result);

} // namespace reports

#endif

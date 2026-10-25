#ifndef OI_ADMIN_PERM_H
#define OI_ADMIN_PERM_H

#include <string>
#include <vector>

struct User;

// ================= 细粒度管理权限 =================
// 权限位只授予“管理后台”的能力，不影响普通用户功能。
// ROLE_ADMIN 始终拥有全部权限；用户可额外获得若干权限位。
namespace adminperm {

std::vector<std::string> all();
std::string label(const std::string& perm);
std::string desc(const std::string& perm);

bool has(const User* u, const std::string& perm);
bool any(const User* u);

// 根据 /admin/... 路径判断该用户是否有权访问
bool pathAllowed(const User* u, const std::string& path);

// 生成权限中文名列表（用于展示）
std::string summary(const User* u);

} // namespace adminperm

#endif

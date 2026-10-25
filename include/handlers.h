#ifndef OI_HANDLERS_H
#define OI_HANDLERS_H

#include "../include/server.h"
#include "../include/router.h"
#include "../include/models.h"
#include <string>
#include <map>

class Handlers {
public:
    static void registerRoutes(Router& router);
    // 在文件顶部，所有函数之前添加
    static void calculateRatingChanges(Contest& contest);
    // 页面处理器
    static std::string handleRoot(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId);
    
    static std::string handleLogin(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);
    
    static std::string handleRegister(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId);
    
    static std::string handleLogout(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId);
    
    static std::string handleProblems(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId);
    
    static std::string handleProblemDetail(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    
    static std::string handleSubmit(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);
    
    static std::string handleStatus(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);
    
    static std::string handleSubmissionDetail(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId);
    static std::string handleSubmissionHack(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    static std::string handleSubmissionHackPage(const std::string& method, const std::string& path,
                                                const std::string& body,
                                                const std::map<std::string, std::string>& headers,
                                                std::map<std::string, std::string>& respHeaders,
                                                std::string& sessionId);
    
    static std::string handleBlog(const std::string& method, const std::string& path,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers,
                                 std::map<std::string, std::string>& respHeaders,
                                 std::string& sessionId);
    
    static std::string handleBlogNew(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId);
    
    static std::string handlePostDetail(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);

    // ---------- 题解 ----------
    static std::string handleProblemSolutions(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId);
    static std::string handleSolutionNew(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    static std::string handleSolutionResubmit(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId);
    static std::string handleAdminSolutions(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    static std::string handleAdminSolutionAudit(const std::string& method, const std::string& path,
                                                const std::string& body,
                                                const std::map<std::string, std::string>& headers,
                                                std::map<std::string, std::string>& respHeaders,
                                                std::string& sessionId);
    static std::string handleProblemDiscussPost(const std::string& method, const std::string& path,
                                                const std::string& body,
                                                const std::map<std::string, std::string>& headers,
                                                std::map<std::string, std::string>& respHeaders,
                                                std::string& sessionId);
    static std::string handleProblemToggleSolution(const std::string& method, const std::string& path,
                                                   const std::string& body,
                                                   const std::map<std::string, std::string>& headers,
                                                   std::map<std::string, std::string>& respHeaders,
                                                   std::string& sessionId);
    
    static std::string handlePostComment(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    
    static std::string handlePostLike(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handlePostVote(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    
    static std::string handleDiscuss(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId);
    
    static std::string handleDiscussNew(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    
    static std::string handleBenben(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);
    
    static std::string handleCheckin(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId);
    
    static std::string handleContests(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    
    static std::string handleContestDetail(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    
    static std::string handleContestJoin(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    
    static std::string handleProblemSets(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    
    static std::string handleProblemSetDetail(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId);
    
    static std::string handleMessagesSearch(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);

    static std::string handleMessages(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    
    static std::string handleSendMessage(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    
    static std::string handleDeleteMessage(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    static std::string handleMessageUpload(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    static std::string handleMessageFile(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleUserSuggest(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleGroupNew(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleGroupAction(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleRename(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleContactAdd(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleAdminSystem(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    
    static std::string handleUserProfile(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    
    static std::string handleUserEdit(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    
    // 新增功能
    static std::string handleUserSettings(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    
    static std::string handleUploadImage(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    
    static std::string handleGetImage(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId);
    
    // 管理员
    static std::string handleAdmin(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);
    
    static std::string handleAdminUsers(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    
    static std::string handleAdminDeleteUser(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    
    static std::string handleAdminProblemImport(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId);
    static std::string handleAdminProblems(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    
    // 用户出题（任何登录用户，时间/内存限制设有上限）
    static std::string handleCustomTest(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    // IDE 模式自测（CodeMirror + 多测试点）
    static std::string handleIdeTest(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    // IDE ↔ 云盘：列目录 / 读文件 / 保存回云盘 / 取提交验证码
    static std::string handleIdeDrive(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId);
    static std::string handleIdeRead(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleIdeSave(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleCaptchaApi(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleMyProblems(const std::string& method, const std::string& path, const std::string& body, const std::map<std::string,std::string>& headers, std::map<std::string,std::string>& respHeaders, std::string& sessionId);
 static std::string handleContestLock(const std::string& method, const std::string& path,
  const std::string& body,
  const std::map<std::string, std::string>& headers,
  std::map<std::string, std::string>& respHeaders,
  std::string& sessionId);
 static std::string handleAdminRejudge(const std::string& method, const std::string& path,
  const std::string& body,
  const std::map<std::string, std::string>& headers,
  std::map<std::string, std::string>& respHeaders,
  std::string& sessionId);
 static std::string handleAdminRejudgeSingle(const std::string& method, const std::string& path,
   const std::string& body,
   const std::map<std::string,std::string>& headers,
   std::map<std::string,std::string>& respHeaders,
   std::string& sessionId);

    static std::string handleUserAddProblem(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    static std::string handleDuelLobby(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    static std::string handleDuelDetail(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    static std::string handleDuelAction(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);

    // 编译语言管理（编辑编译命令 / 添加语言）
    static std::string handleAdminLanguages(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);

    static std::string handleAdminAddProblem(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    
    static std::string handleAdminDeleteProblem(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId);
    
    static std::string handleAdminEditProblem(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId);
    
    static std::string handleAdminToggleProblem(const std::string& method, const std::string& path,
                                                const std::string& body,
                                                const std::map<std::string, std::string>& headers,
                                                std::map<std::string, std::string>& respHeaders,
                                                std::string& sessionId);
    
    static std::string handleAdminPosts(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    
    static std::string handleAdminDeletePost(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    
    static std::string handleAdminEditPost(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    
    static std::string handleAdminContests(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    
    static std::string handleAdminAddContest(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    
    static std::string handleAdminEditContest(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId);
    
    static std::string handleAdminAddProblemSet(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId);
    
    static std::string handleAdminAnnouncement(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId);
    
    static std::string handleAdminPermissions(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId);
    static std::string handleAdminPerms(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);

    static std::string handleAdminBan(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);

    // coin shop
    static std::string handleShop(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId);
    static std::string handleShopBuy(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleAdminShop(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    static std::string handleAdminShopAdd(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    static std::string handleAdminShopEdit(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId);
    static std::string handleAdminShopToggle(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId);
    static std::string handleAdminShopDelete(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId);

    // 全站排行榜
    static std::string handleRank(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);

    // 悬赏任务
    static std::string handleTasks(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);
    static std::string handleTaskNew(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleTaskDetail(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleTaskAction(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleTaskAccept(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleTaskSubmit(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleTaskConfirm(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    static std::string handleTaskReject(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleTaskClose(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    static std::string handleTaskMsg(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleAdminOrders(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    static std::string handleAdminOrderUpdate(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId);

    // 管理员发放金币
    static std::string handleAdminSendGold(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId);

    static std::string handleAdminSendTokens(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId);
    static std::string handleAdminUserPlan(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId);

    // AI 管理
    static std::string handleAdminAI(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleAdminAIProviders(const std::string& method, const std::string& path,
                                              const std::string& body,
                                              const std::map<std::string, std::string>& headers,
                                              std::map<std::string, std::string>& respHeaders,
                                              std::string& sessionId);
    static std::string handleAdminAIPlans(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    // 提供方页面上的「小窗扫描」与中转测试（均返回 JSON，Key 只在服务端）
    static std::string handleAdminProviderScanApi(const std::string& method, const std::string& path,
                                                  const std::string& body,
                                                  const std::map<std::string, std::string>& headers,
                                                  std::map<std::string, std::string>& respHeaders,
                                                  std::string& sessionId);
    static std::string handleAdminProviderModels(const std::string& method, const std::string& path,
                                                 const std::string& body,
                                                 const std::map<std::string, std::string>& headers,
                                                 std::map<std::string, std::string>& respHeaders,
                                                 std::string& sessionId);
    static std::string handleAdminAiTest(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);

    // 社区举报
    static std::string handleReport(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId);
    static std::string handleAdminReports(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    // 可视化自测（IDE 模式）：JSON 接口 + 题目样例
    static std::string handleCustomTestApi(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId);
    static std::string handleProblemSamples(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);

    // �� + ��K�pn
    static std::string handleDrive(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId);
    static std::string handleDriveMkdir(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    static std::string handleDriveRename(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    static std::string handleDriveUpload(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleDriveDownload(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    static std::string handleDriveDelete(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleAdminDrive(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    static std::string handleDriveShare(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    // drive: raw preview (Range), zip download, move, tree json, search json
    static std::string handleDriveRaw(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId);
    static std::string handleDriveZip(const std::string& method, const std::string& path,
                                      const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      std::map<std::string, std::string>& respHeaders,
                                      std::string& sessionId);
    static std::string handleDriveMove(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    static std::string handleDriveTree(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    static std::string handleDriveSearch(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    static std::string handleDriveCopy(const std::string& method, const std::string& path,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::map<std::string, std::string>& respHeaders,
                                       std::string& sessionId);
    static std::string handleDriveNewFile(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    static std::string handleDriveProps(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId);
    static std::string handleDriveShortcut(const std::string& method, const std::string& path,
                                           const std::string& body,
                                           const std::map<std::string, std::string>& headers,
                                           std::map<std::string, std::string>& respHeaders,
                                           std::string& sessionId);
    static std::string handleMessagesPoll(const std::string& method, const std::string& path,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers,
                                          std::map<std::string, std::string>& respHeaders,
                                          std::string& sessionId);
    static std::string handleMessagesTyping(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
    static std::string handleAbout(const std::string& method, const std::string& path,
                                     const std::string& body,
                                     const std::map<std::string, std::string>& headers,
                                     std::map<std::string, std::string>& respHeaders,
                                     std::string& sessionId);
    static std::string handleShare(const std::string& method, const std::string& path,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   std::map<std::string, std::string>& respHeaders,
                                   std::string& sessionId);
    static std::string handleAdminProblemData(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId);
    // 管理员重置用户密码（加密存储）
    static std::string handleAdminUserPassword(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId);
    // 用站内模型根据标程 / 数据范围 / 输入格式自动写数据生成器
    static std::string handleAdminProblemGenAi(const std::string& method, const std::string& path,
                                               const std::string& body,
                                               const std::map<std::string, std::string>& headers,
                                               std::map<std::string, std::string>& respHeaders,
                                               std::string& sessionId);

public:
    static User* getCurrentUser(const std::string& sessionId);
    static std::string renderTemplate(const std::string& title, const std::string& content, User* user);
    static std::string renderAuthPage(const std::string& title, const std::string& subtitle, const std::string& content);
    static std::string getNav(User* user);
    static std::string getTopBar(User* user);
    // 后台统一顶部导航（按权限过滤）
    static std::string adminNavbar(User* user);
    static std::string getStatusText(SubmitStatus status);
    static bool isAdmin(User* user);
    static bool canManageProblems(User* user);
    static bool isProblemOwner(Problem* p, User* u);
    static std::string problemQuickList();
    static std::string parseFormData(const std::string& body, const std::string& key);
    static std::string renderPagination(const std::string& baseUrl, int currentPage, int totalPages);
    static std::string renderPaginationKeepParams(const std::string& baseUrl, int currentPage, int totalPages, const std::string& params);
    static std::string getContestStatus(const Contest& contest);
    static std::string buildContestRanking(Contest* contest);
    static std::string getRatingBadge(int rating);
    static std::string buildTeamPanel(Contest* contest, User* user);
    static bool isContestResultHidden(const Submission* sub, User* viewer);
    static std::string handleContestTeam(const std::string& method, const std::string& path,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         std::map<std::string, std::string>& respHeaders,
                                         std::string& sessionId);
    static std::string handleContestComment(const std::string& method, const std::string& path,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::map<std::string, std::string>& respHeaders,
                                            std::string& sessionId);
};

#endif
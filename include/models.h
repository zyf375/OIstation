#ifndef OI_MODELS_H
#define OI_MODELS_H

#include <string>
#include <vector>
#include <map>

enum UserRole { ROLE_USER, ROLE_ADMIN, ROLE_PROBLEM_MANAGER };

struct User {
    std::string id;
    std::string username;
    std::string password;
    UserRole role;
    std::string registerTime;
    int checkinDays;
    std::string lastCheckin;
    std::string avatar;      // Base64 头像
    std::string bio;
    int rating;              // 积分
    int maxRating;           // 历史最高积分
    int ratedMatches;        // 参加过的 Rated 比赛次数
    int tokens;              // 剩余Token 数（用于 LLM 对话）
    int experience;          // 经验值（驱动等级）
    long long gold;                // 金币
    long long aiWindowQuota = 0;   // AI 5 小时额度（算力点，0=用全局默认）
    long long aiMonthQuota = 0;    // AI 月额度（算力点，0=用全局默认）
    std::string aiPlanId;          // AI 套餐 id（空=不受套餐限制，用全局默认）
    std::vector<std::string> adminPerms; // 细粒度管理权限（空=按角色默认）
    bool codePublic = true; // 提交代码是否对他人公开（管理员始终可见）

    int effCoeff = 100;      // \u57fa\u7840\u6536\u76ca\u7cfb\u6570(\u767e\u5206\u6bd4,\u9ed8\u8ba4100)
    int driveMB = 0;         // \u4e2a\u4eba\u4e91\u76d8\u57fa\u7840\u914d\u989dMB(<=0\u4f7f\u7528\u5168\u5c40\u9ed8\u8ba4)
    int driveBonusMB = 0;    // \u4e91\u76d8\u4e34\u65f6\u589e\u5bb9MB
    std::string driveBonusUntil; // \u4e91\u76d8\u589e\u5bb9\u5230\u671f\u65f6\u95f4
    int coeffBonusPct = 0;   // \u6536\u76ca\u7cfb\u6570\u4e34\u65f6\u52a0\u6210%
    std::string coeffUntil;  // \u6536\u76ca\u7cfb\u6570\u52a0\u6210\u5230\u671f\u65f6\u95f4
    int renameCards = 0;     // \u6539\u540d\u5361\u5269\u4f59\u5f20\u6570
    std::vector<std::string> contacts;   // \u8054\u7cfb\u4eba\u5217\u8868(\u7528\u6237id)
    std::vector<std::string> ratingHistory; // \u6bcf\u6761: "time|delta|rating|label"
};

// 金币商城商品
struct ShopItem {
    std::string id;
    std::string name;
    std::string description;
    int price;               // 价格（金币）
    int stock;               // 库存（-1 表示无限）
    bool onSale;             // 是否上架
    int tokenReward;         // 购买后赠送的 Token 数（0 = 普通商品）
    int type;                // 商品类型：0=虚拟(自动发货), 1=实体(需管理员处理)
    std::string icon;        // 图标/emoji

    int effectType = 0;      // \u6548\u679c\u7c7b\u578b:0=\u65e0,1=\u6539\u540d\u5361,2=\u4e91\u76d8\u589e\u5bb9\u5361,3=\u6536\u76ca\u7cfb\u6570\u5361
    int effectValue = 0;     // \u6548\u679c\u6570\u503c(1:\u5f20\u6570,2:MB,3:%)
    int durationDays = 0;    // \u6709\u6548\u671f(\u5929), \u6539\u540d\u5361\u4e3a0(\u4e00\u6b21\u6027)
    std::string createTime;
};

// 编译语言配置（管理员可编辑编译命令、添加语言）
struct LangConfig {
    std::string name;        // 显示名，如 "C++"
    std::string id;          // 内部标识（提交时 language 的值）
    std::string ext;         // 源文件扩展名（不含点），如 cpp / py
    std::string compileCmd;  // 编译命令模板：{gpp}=编译器路径, {src}=源文件, {out}=输出exe；空=解释型(跳过编译)
    std::string runCmd;      // 运行命令模板：{src}=源文件；空=编译型直接运行 exe
    bool enabled;            // 是否可选/可用
};

enum OrderStatus { ORDER_PENDING = 0, ORDER_FULFILLED = 1, ORDER_CANCELLED = 2 };

struct Order {
    std::string id;
    std::string shopItemId;
    std::string itemName;
    std::string icon;
    std::string userId;
    std::string username;
    int quantity;
    int price;            // 实付总价（金币）
    std::string contact;  // 收货/联系信息
    int status;           // OrderStatus
    std::string time;
};

enum SubmitStatus {
    PENDING = 0,
    JUDGING = 1,
    AC = 2,
    WA = 3,
    TLE = 4,
    MLE = 5,
    CE = 6,
    RE = 7
};

struct Submission {
    std::string id;
    std::string problemId;
    std::string userId;
    std::string code;
    std::string language;
    SubmitStatus status;
    int usedTime;
    int usedMem;
    std::string submitTime;
    std::string judgeResult;
    // Hack（有标程的题可被Hack，Hack成功后该题对该用户归零）
    bool hacked = false;
    std::string hackedBy;
    std::string hackInput;
    std::string hackTime;
    // 比赛锁题/Hack（锁题前他人不可见、不可Hack；锁题后公开可Hack）
    bool locked = false;
    std::string contestId;
    // 分数(0-100，按通过用例占比计算，供重测筛选)
    int score = 0;
};

enum PostType { BLOG = 0, DISCUSSION = 1, BENBEN = 2, SOLUTION = 3 };

struct Comment {
    std::string id;
    std::string authorId;
    std::string content;
    std::string createTime;
    std::string parentId;                  // 楼中楼：父评论 id（空=直接回复楼主）
    std::vector<std::string> likedBy;      // 点赞用户
};

struct Post {
    std::string id;
    std::string authorId;
    PostType type;
    std::string title;
    std::string content;
    std::string createTime;
    std::vector<Comment> comments;
    int views;
    int likes;

    std::string problemId;                        // 关联题目（SOLUTION 类型用）
    std::string auditStatus;                      // 题解审核状态: pending/approved/rejected/removed（空=不审核）
    std::string rejectReason;                     // 驳回原因
    std::string reviewerId;                       // 审核人
    std::string auditTime;                        // 审核时间
    bool rewardPaid;                              // 是否已发过审核通过奖励（防重复 +50）

    // ---- 洛谷式论坛（v1.5.0） ----
    std::string board = "academic";                       // 板块: academic/water/site/team
    std::vector<std::string> tags;                        // 标签: 求助/讨论/经验/题解/公告
    bool pinned = false;                                  // 置顶
    bool essence = false;                                 // 精华
    bool locked = false;                                  // 锁定（不可回复）
    std::string lastReplyTime;                            // 最后回复时间
    std::vector<std::string> favoritedBy;                 // 收藏用户
    std::string editTime;                                 // 最后编辑时间

    std::vector<std::string> likedBy;                     // \u70b9\u8d5e\u7528\u6237
    std::string pollTitle;                                // \u6295\u7968\u6807\u9898(\u7a7a=\u65e0\u6295\u7968)
    std::vector<std::string> pollOptions;                 // \u6295\u7968\u9009\u9879
    std::vector<std::vector<std::string> > pollVotes;     // \u6bcf\u4e2a\u9009\u9879\u7684\u6295\u7968\u8005
};

struct ContestProblem {
    std::string problemId;
    int score;
};

// 组队赛队伍
struct Team {
    std::string id;
    std::string name;
    std::string leaderId;                 // 队长
    std::vector<std::string> memberIds;   // 队员（不含队长）
    std::vector<std::string> pendingInvites;    // 待确认邀请 (userId)
    std::vector<std::string> pendingApplications; // 待审批申请 (userId)
};

// 排名用到的每用户/每队每题的比赛状态
struct ContestProg {
    int attempts = 0;     // 提交次数
    bool solved = false;  // 是否 AC
    int acTimeMin = 0;    // AC 时刻距比赛开始分钟数（ICPC 罚时 / OI 记录）
    int maxScore = 0;     // 最优得分（OI/IOI）
    std::string acSubId;  // AC 提交 id
};

struct Contest {
    std::string id;
    std::string title;
    std::string description;
    std::string startTime;
    std::string endTime;
    std::vector<ContestProblem> problems;
    std::vector<std::string> participants;
    std::vector<std::string> registrants;
    // 比赛题目提交记录，用于计算罚时
    std::map<std::string, std::map<std::string, SubmitStatus>> userProblemStatus; // userId -> problemId -> status
    bool isRated;                    // 是否为 Rated 比赛
    bool isFinished;                 // 是否已结束 Rating
    std::map<std::string, int> ratingChanges;  // userId -> rating变化
    // ---- 赛制 / 组队（v1.4.3） ----
    std::string format = "icpc";     // icpc / oi / ioi
    bool isTeam = false;             // 是否为组队赛
    std::vector<Team> teams;         // 队伍（组队赛时使用）
    // 赛时反馈留言
    struct ContestComment {
        std::string userId;
        std::string username;
        std::string time;
        std::string content;
    };
    std::vector<ContestComment> comments;
};

struct ProblemSet {
    std::string id;
    std::string name;
    std::string description;
    std::string authorId;
    std::vector<std::string> problemIds;
    std::string createTime;
};

struct Message {
    std::string id;
    std::string fromId;
    std::string toId;
    std::string content;
    std::string sendTime;
    bool read;

    std::vector<std::string> readBy;      // \u5df2\u8bfb\u7528\u6237(\u7fa4\u804a)

    // 附件（私信/群聊，可选）
    std::string file;       // 相对路径，如 msg/xxx_代码.cpp
    std::string fileName;   // 原始文件名
    long long fileSize;     // 字节数

    Message() : read(false), fileSize(0) {}
};
struct GroupChat {
    std::string id;
    std::string name;
    std::string ownerId;
    std::string createTime;
    std::vector<std::string> memberIds;
};


// 子任务（捆绑测试）：该组内全部测试点通过才拿满分
struct Subtask {
    std::string name;
    int score;
    std::vector<std::string> tests; // 测试点编号（.in 的文件名去扩展名）
    Subtask() : score(0) {}
};

struct Problem {
    std::string id;
    std::string title;
    std::string description;
    std::string inputDesc;
    std::string outputDesc;
    std::vector<std::string> samples;
    int timeLimit;
    int memLimit;
    std::string author;
    std::string createTime;
    int acCount;
    int submitCount;
    bool hidden;        // 是否隐藏
    std::string image;  // 题目图片（Base64）
    std::string standardSolution; // 标程（验证数据/Hack校验用）
    std::string spj;              // 特殊评测（Special Judge）程序源码（非空=SPJ 题）
    bool solutionClosed = false;  // 是否关闭题解入口
    std::vector<std::string> comments; // 题目讨论（评论，每项=JSON:{user,uid,time,content}）

    // ===== 出题 → 数据 → 评测 一体化 =====
    std::string dataRange;        // 数据范围 / 约束说明（Markdown，供生成器与 AI 使用）
    std::string generator;        // 数据生成器源码（约定：读入 caseNo seed，向 stdout 输出一组输入）
    bool subtaskMode = false;     // 是否启用子任务（捆绑测试）计分
    std::vector<Subtask> subtasks; // 子任务定义
};

// 悬赏任务
enum TaskStatus { TASK_OPEN = 0, TASK_ACCEPTED = 1, TASK_COMPLETED = 2, TASK_DONE = 3, TASK_CANCELLED = 4 };

// 任务帮助者状态
enum TaskHelperState { TH_ACCEPTED = 0, TH_SUBMITTED = 1, TH_REJECTED = 2, TH_CONFIRMED = 3 };

struct TaskHelper {
    std::string userId;       // 帮助者
    int state;                // TaskHelperState
    std::string submitTime;   // 最近提交时间
    std::string submitNote;   // 提交说明
};

// 任务内的私密留言（发布者与某帮助者之间）
struct TaskMessage {
    std::string id;
    std::string fromId;       // 发送者
    std::string toId;         // 接收者（发布者 id 或帮助者 id）
    std::string content;
    std::string sendTime;
};

struct TaskItem {
    std::string id;
    std::string title;
    std::string description;
    std::string authorId;      // 发布者
    int reward;                // 悬赏金币
    int status;                // TaskStatus
    std::string acceptorId;    // 兼容旧数据（迁移为 helpers）
    std::string createTime;
    std::vector<TaskHelper> helpers;       // 帮助者列表
    std::vector<TaskMessage> taskMessages; // 任务内私密留言
};

// ============ 1v1 对决 ============
struct DuelProblem { std::string problemId; int points; };

// 对决玩家状态
// 对决单次提交（含代码，供Hack定位）
struct DuelSubmission {
    std::string id;       // 提交ID
    std::string problemId;
    int score = 0;
    int pass = 0;
    int total = 0;
    std::string code;
    std::string time;
    bool isHack = false;  // 是否Hack记录
    bool hacked = false;  // 该条提交是否被Hack归零
    std::string hackInfo; // Hack详情（成功/失败）
};

struct DuelPlayer {
    std::string userId;
    std::map<std::string,int> bestScore;    // problemId -> 最佳得分
    std::map<std::string,int> bestPass;     // problemId -> 通过用例数
    std::map<std::string,int> totalCases;   // problemId -> 总用例数
    std::map<std::string,std::string> lockedCode; // problemId -> 锁定源码
    std::map<std::string,std::string> bestCode; // problemId -> 最佳得分对应源码
    std::map<std::string,bool> locked;      // problemId -> 是否已锁定
    std::map<std::string,int> lockedScore;  // problemId -> 锁定分
    std::map<std::string,bool> hacked;      // problemId -> 是否被Hack归零
    std::map<std::string,std::string> lastCode; // problemId -> 最近一次提交代码
    std::map<std::string,std::vector<std::string>> submitLog; // problemId -> 提交历史("通过/总(得分)")
    std::vector<DuelSubmission> submissions; // 所有提交（含代码）
    int hackCount = 0;
};

// 对决房间
struct Duel {
    std::string id;
    std::string status;      // waiting / coding / hack / finished
    std::string mode;        // "1v1"
    std::string playerA;     // userId
    std::string playerB;     // userId
    std::string winner;      // userId 或 ""(平局)
    std::vector<DuelProblem> problems;
    DuelPlayer pa, pb;
    long long codingEndMs = 0;
    long long hackEndMs = 0;
    int hackMinutes = 5; // Hack阶段时长(分钟)
    long long createTimeMs = 0;
    std::string language = "cpp";
    std::vector<std::string> log;
};

// ============ 试卷（选择题 + 填空题） ============
// 一道题：type = "choice"（选择） / "fill"（填空）
struct PaperQuestion {
    std::string type = "choice";
    std::string stem;                     // 题干（支持 Markdown/LaTeX）
    std::vector<std::string> options;     // 选项（选择题，A/B/C/D...）
    bool multi = false;                   // 选择题是否多选
    std::string answer;                   // 选择题: "A" / "AB"；填空题: 多个空用 | 分隔，每空可写多个答案用 , 分隔
    int score = 5;                        // 分值
    std::string analysis;                 // 解析
};

struct Paper {
    std::string id;
    std::string title;
    std::string description;
    std::string authorId;
    std::string createTime;
    std::string startTime;                // 可空
    std::string endTime;                  // 可空
    bool hidden = false;
    int timeLimit = 0;                    // 限时（分钟，0=不限）
    std::vector<PaperQuestion> questions;
};

struct PaperSubmission {
    std::string id;
    std::string paperId;
    std::string userId;
    std::string username;
    std::string time;
    int total = 0;                        // 总得分
    int fullScore = 0;                    // 满分
    bool objective = true;                // 全客观题（自动批改）
    std::vector<std::string> answers;     // 每题作答
    std::vector<int> scores;              // 每题得分
};

// 分页结构
struct PageInfo {
    int page;
    int perPage;
    int total;
    int totalPages;
};

// 数据管理器
class DataStore {
public:
    static DataStore& instance();

    void loadAll();
    void saveAll();
    void saveUsers();
    void saveProblems();
    void saveSubmissions();
    void savePosts();
    void saveContests();
    void saveProblemSets();
    void saveMessages();
    void saveGroups();
    void saveAnnouncement();
    void saveShops();
    void saveLanguages();
    void saveTasks();
    void saveOrders();
    void saveDuels();
    void savePapers();
    void savePaperSubs();

    // 数据集合
    std::vector<User> users;
    std::vector<Problem> problems;
    std::vector<Submission> submissions;
    std::vector<Post> posts;
    std::vector<Contest> contests;
    std::vector<ProblemSet> problemSets;
    std::vector<Message> messages;
    std::vector<GroupChat> groups;         // \u7fa4\u804a
    std::vector<ShopItem> shops;         // 商城商品
    std::vector<LangConfig> languages;   // 编译语言配置
    std::vector<TaskItem> tasks;         // 悬赏任务
    std::vector<Order> orders;           // 购买记录（订单）
    std::vector<Duel> duels;             // 1v1 对决房间
    std::vector<Paper> papers;           // 试卷
    std::vector<PaperSubmission> paperSubs;  // 试卷作答记录
    std::string announcement;

    // 查询函数
    User* findUserById(const std::string& id);
    User* findUserByName(const std::string& name);
    Problem* findProblemById(const std::string& id);
    Submission* findSubmissionById(const std::string& id);
    Post* findPostById(const std::string& id);
    Comment* findCommentInPost(const std::string& postId, const std::string& commentId);
    Contest* findContestById(const std::string& id);
    ProblemSet* findProblemSetById(const std::string& id);
    Message* findMessageById(const std::string& id);
    GroupChat* findGroupById(const std::string& id);
    ShopItem* findShopItemById(const std::string& id);
    LangConfig* findLangConfig(const std::string& id);
    TaskItem* findTaskById(const std::string& id);
    Order* findOrderById(const std::string& id);
    Duel* findDuelById(const std::string& id);
    Paper* findPaperById(const std::string& id);

    // 统计函数
    int getProblemACCount(const std::string& problemId);
    int getProblemSubmitCount(const std::string& problemId);
    int getUserACCount(const std::string& userId);
    int getUserSubmitCount(const std::string& userId);

    // Token 操作
    bool addTokens(const std::string& userId, int amount);
    bool useTokens(const std::string& userId, int amount);
    int getTokens(const std::string& userId);

    // 金币 / 等级系统
    bool addGold(const std::string& userId, long long amount);
    bool useGold(const std::string& userId, long long amount);
    long long getGold(const std::string& userId);
    bool addExperience(const std::string& userId, int amount);
    int getExperience(const std::string& userId);
    static int calcLevel(int exp);
    static int levelBaseExp(int level);
    static std::string getLevelBadge(int level);

    // 权限检查
    bool canManageProblems(User* user);

private:
    DataStore() {}
    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;

    std::string getDataDir() const;

    // JSON 序列化
    std::string userToJson(const User& u);
    User userFromJson(const std::string& json);
    std::string problemToJson(const Problem& p);
    Problem problemFromJson(const std::string& json);
    std::string submissionToJson(const Submission& s);
    Submission submissionFromJson(const std::string& json);
    std::string postToJson(const Post& p);
    Post postFromJson(const std::string& json);
    std::string contestToJson(const Contest& c);
    Contest contestFromJson(const std::string& json);
    std::string problemSetToJson(const ProblemSet& ps);
    ProblemSet problemSetFromJson(const std::string& json);
    std::string messageToJson(const Message& m);
    Message messageFromJson(const std::string& json);
    std::string announcementToJson(const std::string& content);
    std::string announcementFromJson(const std::string& json);
    std::string shopToJson(const ShopItem& s);
    std::string langToJson(const LangConfig& l);
    std::string groupToJson(const GroupChat& g);
    GroupChat groupFromJson(const std::string& json);
    ShopItem shopFromJson(const std::string& json);
    LangConfig langFromJson(const std::string& json);
    std::string taskToJson(const TaskItem& t);
    TaskItem taskFromJson(const std::string& json);
    std::string orderToJson(const Order& o);
    Order orderFromJson(const std::string& json);
    std::string duelToJson(const Duel& d);
    Duel duelFromJson(const std::string& json);
    std::string paperToJson(const Paper& p);
    Paper paperFromJson(const std::string& json);
    std::string paperSubToJson(const PaperSubmission& ps);
    PaperSubmission paperSubFromJson(const std::string& json);
};

#endif

#ifndef OI_JUDGE_H
#define OI_JUDGE_H

#include <string>
#include <vector>
#include <windows.h>
#include "../include/models.h"

class Judge {
public:
    // 评测提交
    static void judgeSubmission(Submission& sub);
  static void judgeSubmissionLocked(Submission& sub);
  static void waitIdle();
  static void recomputeAC(const std::string& problemId);
    
    // 检查评测环境
    static bool checkEnvironment();
    // 自定义测试：编译+运行自定义输入，返回输出/状态/时间/内存
    static bool customRun(const std::string& langId, const std::string& code,
                          const std::string& input, int timeLimit, int memLimit,
                          std::string& output, std::string& verdict,
                          int& usedTime, int& usedMem, std::string& errorMsg);
    // 多测试点：只编译一次，依次运行
    struct CustomCaseResult {
        std::string output;
        std::string verdict;
        std::string error;
        int time = 0;
        int mem = 0;
    };
    static bool customRunMulti(const std::string& langId, const std::string& code,
                               const std::vector<std::string>& inputs, int timeLimit, int memLimit,
                               std::vector<CustomCaseResult>& results, std::string& errorMsg);
    // 评测线程池
    static void startPool();
    static void enqueue(const std::string& subId);

    // 对决评分：编译代码并对题目全部测试点评分
    static bool duelScore(const std::string& langId, const std::string& code,
                          Problem* problem, int& passed, int& total, std::string& detail);

    // 数据生成：生成器 + 标程 → problems/<id>/1..n.in/.out
    static bool generateData(Problem* problem, int count, int seed, int& made, std::string& detail);

    
private:
    // 编译代码
    static bool compileCode(const std::string& workDir, const std::string& codePath,
                           const std::string& exePath, std::string& errorMsg, const LangConfig& lang);
    
    // 运行测试点
    static SubmitStatus runTest(const std::string& workDir, const std::string& exePath,
                               const std::string& runCmd,
                               const std::string& inputFile, const std::string& outputFile,
                               int timeLimit, int memLimit, int& usedTime, int& usedMem);
    
    // 比对输出
    static bool compareOutput(const std::string& output1, const std::string& output2);
    static SubmitStatus runTest(const std::string& workDir, const std::string& exePath,
                           const std::string& inputFile, const std::string& outputFile,
                           int timeLimit, int memLimit, int& usedTime, int& usedMem,
                           std::string& extraInfo);
    // 清理临时文件
    static void cleanup(const std::string& workDir);
    
    // 执行命令并获取输出
    static bool executeCommand(const std::string& cmd, std::string& output, int timeoutMs);
};

#endif
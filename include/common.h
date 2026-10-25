#ifndef OI_COMMON_H
#define OI_COMMON_H

#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <windows.h>
#include <direct.h>

namespace common {// 在 common 命名空间中添加
std::string sha1(const std::string& input);

// ---------- 密码哈希（SHA-256 + 随机盐 + 迭代） ----------
std::string sha256Hex(const std::string& input);
std::string passwordHash(const std::string& password);              // 生成 "sha256$迭代$盐$哈希"
bool passwordIsHashed(const std::string& stored);
bool passwordVerify(const std::string& password, const std::string& stored); // 兼容旧的明文记录
// ---------- 时间工具 ----------
// 比较两个时间字符串 (格式: YYYY-MM-DD HH:MM:SS)
int compareTime(const std::string& t1, const std::string& t2);

// 判断当前时间是否在指定时间之前
bool isBeforeNow(const std::string& timeStr);

// 判断当前时间是否在指定时间之后
bool isAfterNow(const std::string& timeStr);

// 判断当前时间是否在指定区间内
bool isBetween(const std::string& start, const std::string& end);
// ---------- 时间工具 ----------
time_t parseTime(const std::string& timeStr);
std::string getCurrentTimeStr();
// ---------- 基础工具 ----------
std::vector<std::string> split(const std::string& s, char delim);
std::string trim(const std::string& s);
std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& content);
// 跨进程数据写锁（多实例共享数据文件时防互相覆盖丢失）
bool lockData();
void unlockData();
bool fileExists(const std::string& path);
std::string getTimeStr();
std::string getDateStr();
std::string timeAddDays(int days);
bool isTimeExpired(const std::string& timeStr);
std::string genId();
std::string urlDecode(const std::string& str);
std::string urlEncode(const std::string& str);
std::string htmlEscape(const std::string& str);
int stringToInt(const std::string& str);
long long stringToLongLong(const std::string& str);
std::string intToString(int num);
std::string getFileExtension(const std::string& path);
bool createDirectory(const std::string& path);
bool removeDirectory(const std::string& path);
std::string getCurrentDir();
std::string getExeDir();
// 自动查找编译器可执行文件路径（支持中文路径），找不到返回 "g++"
std::string autoFindGpp();
std::string replaceAll(std::string str, const std::string& from, const std::string& to);
std::string normalizeNewlines(const std::string& str);
bool isValidUtf8(const std::string& str);
std::string toUtf8FromSystem(const std::string& str);

// ---------- JSON 工具 ----------
std::string jsonStringify(const std::string& value);
std::string jsonStringify(int value);
std::string jsonStringify(bool value);
std::string jsonStringify(const std::vector<std::string>& arr);
std::string jsonStringify(const std::map<std::string, std::string>& obj);
std::map<std::string, std::string> jsonParseObject(const std::string& json);
std::vector<std::string> jsonParseArray(const std::string& json);
std::string jsonEscape(const std::string& s);
std::string jsonUnescape(const std::string& s);

// ---------- Markdown 渲染 ----------
std::string markdownToHtml(const std::string& md);

// ---------- 图片处理 ----------
// 检测图片格式并转换为Base64
std::string imageToBase64(const std::string& filePath);
// 检测图片格式并返回MIME类型
std::string getImageMimeType(const std::string& filePath);
// 保存Base64图片到文件
bool saveBase64Image(const std::string& base64Data, const std::string& outputPath);
// 生成唯一图片名
std::string generateImageName(const std::string& extension);
// 判断字符串是否为Base64图片
bool isBase64Image(const std::string& str);
// Base64解码
std::string base64Decode(const std::string& base64);
// Base64编码
std::string base64Encode(const std::string& data);
// ---------- LLM pricing ----------
double llmAutoMultiplier(const std::string& model);
double llmModelMultiplier(const std::string& model);
void llmSetModelMultiplier(const std::string& model, double mult);
void llmRemoveModelMultiplier(const std::string& model);
std::string llmPricingJson();
}

#endif
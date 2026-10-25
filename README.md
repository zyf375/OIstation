# OI 小站（OIstation）

一个用 C++11 从零写成的自建在线评测系统（Online Judge）：WinSock 手写 HTTP 服务，**无第三方运行时、无外部框架依赖**，编译产物是单个 `OIstation.exe`。

集成题目评测、竞赛、题单、试卷、社区、任务悬赏、商城、云盘、IDE 与 AI 助手，适合本地部署或小团体使用。

- 开发者：**zyf**
- 服务端口：**8083**
- 版本：**v1.18.0**

---

## 目录

1. [快速开始](#快速开始)
2. [目录结构](#目录结构)
3. [启动与访问](#启动与访问)
4. [账号与密码](#账号与密码)
5. [数据与配置](#数据与配置)
6. [功能特性](#功能特性)
7. [AI 助手配置](#ai-助手配置)
8. [管理后台](#管理后台)
9. [常见问题](#常见问题)
10. [安全注意事项](#安全注意事项)
11. [版本日志](#版本日志)

---

## 快速开始

### 从源码构建（Windows）

需要 MinGW-w64（g++ 9+）。在源码根目录执行：

```powershell
g++ -std=c++11 src\*.cpp -o OIstation.exe -lwinhttp -lws2_32 -static -lpsapi -fexec-charset=UTF-8
```

编译约 60~75 秒。建议用 `Start-Process` 后台编译，把 stderr 重定向到文件再检查：

```powershell
Start-Process -FilePath 'D:\mingw64\bin\g++.exe' `
  -ArgumentList '-std=c++11','src\*.cpp','-o','OIstation.build.exe','-lwinhttp','-lws2_32','-static','-lpsapi','-fexec-charset=UTF-8' `
  -WorkingDirectory 'E:\LL\OIstation' `
  -RedirectStandardError 'E:\LL\OIstation\build.err' -RedirectStandardOutput 'E:\LL\OIstation\build.out'
```

更新部署时注意顺序：**先停服务 → 再覆盖 exe → 再启动**，否则文件被占用会覆盖失败。

### 部署目录

```
OIstation/
├── OIstation.exe      # 服务程序
├── data/              # 系统数据（必备，每行一个 JSON 对象）
├── files/             # 云盘文件（每用户一个子目录）
├── problems/          # 题目测试数据（每题一个子目录，*.in / *.out）
├── temp/              # 评测临时目录（自动创建）
├── static/            # 样式与前端脚本
└── problems/          # 题目
```

---

## 目录结构

| 目录 | 说明 |
| --- | --- |
| `src/` | 服务端源码（handlers / server / judge / security / models …） |
| `include/` | 头文件 |
| `static/` | `style.css`、`app.js`、`drive.js`、CodeMirror / KaTeX 等本地依赖 |
| `data/` | 全部运行期数据（JSON Lines） |
| `problems/` | 测试数据，评测时按 `*.in` 找测试点、读同名 `*.out` 比对 |
| `files/` | 用户云盘 |
| `temp/` | 编译与运行的临时工作目录 |
| `tools/` | 运维脚本（登录、压测、数据生成器等） |

---

## 启动与访问

```powershell
cmd /c "OIstation.exe > run.log 2>&1"
```

后台启动推荐：

```powershell
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
  CommandLine = 'cmd /c E:\LL\OIstation\OIstation.exe > E:\LL\OIstation\run.log 2>&1'
  CurrentDirectory = 'E:\LL\OIstation'
}
```

浏览器访问 `http://127.0.0.1:8083/`。

---

## 账号与密码

- 初始管理员：`admin` / `20121202` —— **部署后请立即修改**。
- 密码不以明文保存：`sha256$10000$<salt>$<hash>`（SHA-256 + 16 位随机盐 + 10000 次迭代）。
- 旧明文密码在启动时自动幂等迁移为哈希；登录成功也会兜底升级。
- 管理员可在 `/admin/users` 直接重置任意用户密码。

---

## 数据与配置

所有数据位于 `data/`，格式为 **JSON Lines**（每行一个对象），不能整体 `JSON.parse`：

| 文件 | 内容 |
| --- | --- |
| `users.json` | 用户（含哈希密码、套餐、云盘配额、`codePublic`） |
| `problems.json` | 题目（题面、标程、数据范围、子任务、生成器） |
| `submissions.json` | 提交记录与评测结果 |
| `contests.json` / `papers.json` / `problemsets.json` | 竞赛、试卷、题单 |
| `posts.json` / `messages.json` / `groups.json` | 社区、私信、群聊 |
| `ai_providers.json` | AI 提供方（**含 API Key，切勿提交到仓库**） |
| `security.json` | 安全与编译安全配置（**不提交**） |
| `system.json` | 功能开关 |

> **修改数据前请先停服务或改完立即重启**：运行中的服务会把内存数据写回，覆盖手工修改。改动前建议先备份文件。

---

## 功能特性

### 评测

- C++11（可扩展多语言），每题独立时间 / 内存限制
- **子任务计分**：同一子任务内全部测试点通过才拿该组分，整题 = 各组之和；关闭时按通过比例计分
- **数据生成器**：程序从 stdin 读 `caseNo seed`，向 stdout 输出一组输入；系统用标程跑出 `.out`，一键产出 `1.in/1.out … n.in/n.out`
- **AI 写生成器工作台**：可选模型、思考强度、输出上限与附加要求，生成结果可直接填入并保存
- **编译安全**：提交代码在编译前静态检查（危险头文件 / 系统调用 / Windows API / 网络 / 动态加载 / **文件读写** / **沙箱外路径** / 编译器扩展），规则可在后台按类开关，支持自定义禁止清单与白名单
- Hack 工作台：隐藏他人源码（仍可提交攻击数据），通过标程校验后可加入正式数据并重测

### 社区与内容

- 博客、讨论区、犇犇（动态）、私信与群聊、评论、举报
- 题目讨论、题解（可申请公开代码）
- **代码可见性**：用户可设置 `codePublic`，他人是否可见自己的提交代码；作者本人与管理员始终可见

### 云盘

- 文件 / 文件夹上传（拖拽、目录上传）、批量下载 zip、移动 / 复制 / 重命名 / 删除
- 文本、图片、音视频、PDF 在线预览（Markdown / JSON / CSV 格式化）
- 分享链接、只读 / 可编辑协作、文件锁
- 与出题打通：`@problem/<id>/<file>` 作用域，IDE 可直接编辑 `std.cpp` / `generator.cpp`
- 与题目数据页打通：从云盘导入、导出本题数据到 `problem-<id>/`

### IDE

- CodeMirror 5 本地化，行号、语法高亮、括号匹配、Tab 缩进、Ctrl+Enter 运行
- 自定义测试点、期望输出对比、按题目/语言本地保存

### 其他

- 竞赛、题单、试卷、任务悬赏、商城与订单
- 金币与算力点体系、签到
- 小游戏（五子棋、斗地主、大逃杀等）

---

## AI 助手配置

在 `/admin/ai` 添加提供方：

| 字段 | 说明 |
| --- | --- |
| `baseUrl` | 例如 `https://api.openrouter.ai/api/v1` |
| `apiKey` | 仅存服务端，页面只显示掩码 |
| `apiMode` | `chat`（`/chat/completions`）或 `responses`（`/responses`） |
| `models` | 勾选的模型白名单，`id|name` 形式 |

- 同一 Base URL 下不同中转只开放一种端点，所以「接口协议」做成提供方级字段。
- `responses` 模式下服务端做双向转换，工具轮、开放 API、自检三条路径无需改动。
- Responses 提供方的上游请求**一律流式**，避免 Cloudflare 100 秒首字节超时（524）。
- 开放 API：到 `/llm/keys` 创建 Key，调用会计入算力点额度。

---

## 管理后台

`/admin` 汇总入口：

- 用户与权限（9 个细粒度权限位，`ROLE_ADMIN` 始终全权）
- 题目与评测、题解审核、社区内容、比赛与题单、试卷、商城与订单
- AI 管理（提供方、模型、套餐、用量、测试）
- 系统安全：功能开关、过速阈值、验证码、表单长度、频率限制、IP 封禁、**编译安全**
- 云盘配额、公告、日志

---

## 常见问题

**端口 8083 被占用**
```powershell
Get-Process OIstation -ErrorAction SilentlyContinue | Stop-Process -Force
```

**编译提示找不到 g++**
在 `/admin/system` 的编译器路径填写完整 `g++.exe` 路径，或把 MinGW 的 `bin` 加入 PATH。

**提交直接 CE，提示「禁止使用 freopen()」**
这是编译安全模块的预期行为：评测环境不需要读写文件，用 `stdin` / `stdout` 即可。确实需要放行时，在 `/admin/system` 的「编译安全 → 白名单」里填 `freopen`。

**页面上的中文是乱码**
确认文件为 UTF-8 编码；`static/` 下的前端资源与 `data/` 数据必须是 UTF-8。

**修改数据没生效**
服务运行时会覆盖手工改动，先停服务再改，或改完立即重启。

---

## 安全注意事项

- 部署在内网或加反向代理 + HTTPS，不要直接暴露 8083。
- 立即修改初始管理员密码，开启 IP 封禁与过速防护。
- `data/ai_providers.json`、`data/security.json`、`data/api_keys.json` 含密钥，**不要提交到任何仓库**（本项目 `.gitignore` 已排除）。
- 评测机与编译安全规则建议保持默认开启；关闭总开关等于只做编译不做检查。

---

## 版本日志

### v1.18.0
- **编译安全并入系统安全**：总开关 + 8 类规则开关，支持自定义禁止函数 / 头文件、白名单
- 拦截 `freopen` / `fopen` / `ifstream` / `ofstream` 等文件读写与 `C:\`、`../`、`/etc` 等沙箱外路径
- 静态检查不再误伤字符串与注释内容；`#include` 只从预处理行判定
- 保留 `gppPath` 等既有配置，不再被安全表单覆盖

### v1.17.2
- 修复云盘文本预览「黑底黑字」（`.drv-pv-pre` 未设背景）
- 题目数据页导入改为文件选择框 + 云盘浏览弹窗；上传 `std.cpp` / `generator.cpp` 自动同步题目
- AI 数据生成器改为弹窗工作台（模型 / 思考强度 / 输出上限 / 附加要求）
- 样式表加版本号，避免浏览器缓存旧 CSS

### v1.17.1
- AI 提供方可切换 **Chat Completions / Responses** 协议，修复 Responses 下 524
- 修复举报后「没有这个帖子」
- 新增 3 套试卷
- 用户可设置提交代码是否公开，管理员始终可见
- 批量删除测试点；出题 ↔ 云盘 ↔ IDE 打通；子任务评测
- 密码加密存储（SHA-256 + 盐 + 迭代）与管理员重置密码

### v1.17.0
- 多提供方 AI 管理（扫描模型、白名单、套餐与额度、测试中转）

### v1.16.0
- AI 回答缓存、IDE 自定义测试、统一用户查找、细粒度权限

### v1.15.0
- 云盘 v3（右键菜单、属性、协作编辑、文件锁、共享快捷方式）

### v1.14.0
- 博客 / 讨论 / 犇犇体验优化、通知与消息中心

### v1.13.0
- 任务悬赏、商城订单、金币体系

---

## 关于

OIstation 是一个自用向的轻量 OJ，目标是「单文件部署 + 无外部依赖 + 出题到评测全流程闭环」。
问题与建议：GitHub Issues。

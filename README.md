# AI-Native Mini Harness

**你只写 Markdown 意图文档，AI 完成全部：实现功能 → 生成测试 → 编译运行 → 评审判决。**

---

## 核心理念

传统测试框架需要程序员手写测试用例，而 AI-Native Harness 把整个研发链路交给 AI：

```
传统模式                              本项目的模式
════════                               ════════════
程序员写代码                             程序员写 intent.md
程序员写测试                     ──▶      AI 读意图 → 改代码
程序员编译运行                            AI 自动生成测试
程序员看结果                              AI 编译（失败自动修复）
程序员判断通过/失败                        AI 运行测试 → 判决 PASS/FAIL
                                      AI 输出报告（含代码 diff）
```

你只需要维护 `intent.md`，增删改查全部用自然语言描述。

---

## 执行链路

```
intent.md
├── # 实现需求（可选）    ──→  Phase 0: AI 按需求修改源码
│                                  ├── 源码编译失败？→ AI 自动修复（最多2次）
│                                  └── 通过 → 继续
│
└── # 测试意图           ──→  Phase 1: AI 生成测试代码
                              Phase 2: gcc 编译测试
                                  ├── 编译失败？→ AI 自动修复（最多2次）
                                  └── 通过 → 继续
                              Phase 3: 运行测试，捕获 stdout
                              Phase 4: AI 评审
                                  ├── 判决 VERDICT: PASS / FAIL
                                  └── 代码质量审查
                                        ↓
                                 report.md + report.json
```

---

## 项目结构

```
mini_harness/
├── intent.md          ← 你唯一需要编辑的文件（意图文档）
├── main.cpp           ← AI Agent 引擎（C++17）
├── CMakeLists.txt
├── run.bash           ← 一键编译脚本
├── remote_engine.c    ← 示例：远控发动机模块
├── remote_window.c    ← 示例：远控车门模块
└── README.md
```

---

## 环境要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| **Linux** | 任意 | 当前仅支持 Linux |
| **gcc** | ≥ 7 | 编译 C 源码和测试 |
| **cmake** | ≥ 3.10 | 构建系统 |
| **libcurl** | 任意 | HTTP 调用 Ollama API |
| **Ollama** | 任意 | 本地大模型服务 |
| **DeepSeek-R1** | 任意 | 推荐模型（需在 Ollama 中拉取） |

### 安装依赖

```bash
# Ubuntu / Debian
sudo apt install build-essential cmake libcurl4-openssl-dev

# 安装 Ollama（参考 https://ollama.com）
curl -fsSL https://ollama.com/install.sh | sh

# 拉取模型
ollama pull deepseek-r1
```

> **注意：** 确保 Ollama 服务正在运行。本项目默认连接 `localhost:11434`，可修改 `main.cpp` 中 `CURLOPT_URL` 的值。

---

## 快速开始

```bash
git clone <your-repo-url>
cd mini_harness

# 编辑意图文档（描述你要实现什么、测什么）
vim intent.md

# 一键编译 + 运行
chmod +x run.bash
./run.bash
```

运行完成后，目录下生成：
- `report.md` — 人类可读报告
- `report.json` — 机器可读结构化数据

### 指定源码目录

```bash
# 方式1：命令行参数
./build/mini_harness /path/to/your/c/source

# 方式2：环境变量
export HARNESS_SRC_DIR=/path/to/your/c/source
./build/mini_harness
```

---

## intent.md 编写指南

`intent.md` 使用 Markdown，分为两个板块：

### `# 实现需求`（可选）

描述需要新增或修改的功能。AI 会据此**自动修改源码**，输出 **unified diff**，然后测试验证。

```markdown
# 实现需求

## 新增功能
- 每笔远控执行前判断网关状态（CheckGatewayOnline），网关离线则直接返回失败
- 网关离线时 MQTT 上报结果码 -3

## 修改项
- 将 StopEngine 的返回值改为 -99
```

> AI 会在终端和报告中输出 **diff -u** 格式的差异，让你一目了然改了什么。

### `# 测试意图`（必填）

描述测试重点、边界条件、需要关注的场景。

```markdown
# 测试意图

## 通用规则
- 验证正常输入（cmd=0, cmd=1）
- 验证异常输入（cmd=-1, cmd=99）
- 每个测试步骤用 printf 输出 [PASS] / [FAIL]

## 特定模块重点关注
- StartEngine 前置条件检查链是否完整
- 非法命令是否返回 -1 且不触发 CAN 指令
```

**如果 `# 实现需求` 为空**，AI 只生成测试，不修改源码。

---

## 报告示例

### 终端输出

```
╔══════════════════════════════════╗
║   AI-Native Mini Harness v3     ║
╚══════════════════════════════════╝

[INTENT] 已加载 intent.md
[SCAN] 找到 2 个 .c 文件

━━━ [1/2] remote_engine.c ━━━
  [0/4] AI 按意图实现功能...
  [0/4] AI 修改了源码
  ── Diff ──────────────────────────────
  --- a/remote_engine.c
  +++ b/remote_engine.c
  @@ -1,5 +1,12 @@
   #include "remote_window.h"
  
  +static bool CheckGatewayOnline(void) {
  +    return true;  // TODO: 替换为实际网关检查逻辑
  +}
  +
   int StartEngine(int cmd) {
  +    if (!CheckGatewayOnline()) {
  +        printf("网关离线，拒绝执行\n");
  +        return -3;
  +    }
       printf("Engine %s done\n", cmd == 1 ? "started" : "stopped");
  ───────────────────────────────────────
  [0/4] 验证编译...通过
  [1/4] AI 生成测试代码...
  [2/4] 编译测试...通过
  [3/4] 运行测试...
  [4/4] AI 评审...
  ── ✅ PASS (AI实现: OK) ──

━━━ [2/2] remote_window.c ━━━
  [0/4] AI 修改了源码
  [0/4] 验证编译...通过
  [1/4] AI 生成测试代码...
  [2/4] 编译测试...通过
  [3/4] 运行测试...
  [4/4] AI 评审...
  ── ✅ PASS (AI实现: OK) ──

[REPORT] 生成报告...
[OK] report.md
[OK] report.json

[DONE] 2/2 通过
```

### report.md 内容

报告中包含每个文件的：

| 章节 | 内容 |
|------|------|
| Phase 0 | 源码修改前后的完整 diff |
| Phase 1 | AI 自动生成的测试代码 |
| Phase 2 | 编译结果（含修复次数） |
| Phase 3 | 测试执行输出 |
| Phase 4 | AI 判决 + 代码审查意见 |

---

## 工作原理

### 自愈机制

每个 Phase 如果编译失败，AI 会自动重试修复：

```
AI 输出代码 ──→ gcc 编译
                   │
             ┌─────┴─────┐
            通过          失败
                         │
                   编译错误喂给 AI
                         │
                   AI 输出修复后代码
                         │
                   gcc 编译（再试）
                   （最多重试 2 次）
```

### AI 模型配置

默认使用 `deepseek-r1`，Ollama API 地址 `http://localhost:11434`。如需修改，编辑 `main.cpp`：

```cpp
// 第 107 行：修改模型名
"{\"model\":\"deepseek-r1\",\"prompt\":\""

// 第 109 行：修改 API 地址
curl_easy_setopt(c, CURLOPT_URL, "http://localhost:11434/api/generate");
```

---

## 适用场景

- 车载嵌入式 C 代码的自动化回归测试
- 接手遗留代码时快速验证行为
- 需求变更后的自动化实现 + 验证
- CI/CD 流水线中的 AI 质量门禁

---

## License

MIT

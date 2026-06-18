# AI-Native Mini Harness — 自演化架构

**你只写 Markdown 意图文档，AI 完成全部：实现功能 → 生成测试 → 编译运行 → 评审判决 → 自我演化。**

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
                                      ↑
                                      AI 可修改 Harness 自身（自演化）
```

你只需要维护 `intent.md`，增删改查 + 自我演化全部用自然语言描述。

---

## 项目结构

```
mini_harness/
├── intent.md              ← 你唯一需要编辑的文件
├── CMakeLists.txt
├── run.bash               ← 一键编译 + 运行
├── README.md
│
├── include/
│   └── harness_engine.h   ← 引擎头文件（AI 可演化）
│
├── src/
│   ├── main.cpp           ← 轻薄 bootstrap（调度层）
│   └── harness_engine.cpp ← 核心引擎（AI 可演化：Prompt / 管线 / 工具）
│
├── examples/              ← 示例被测模块
│   ├── remote_engine.c
│   └── remote_window.c
│
└── build/                 ← 构建输出
    ├── mini_harness
    ├── report.md
    └── report.json
```

---

## 自演化机制

```
intent.md 写入 "改进 Prompt_Judge 的评审维度"
        │
        ▼
  bootstrap (src/main.cpp)             ← 轻薄稳定层
        │
        ▼
  engine (src/harness_engine.cpp)       ← 可被 AI 修改
  engine (include/harness_engine.h)     ← 可被 AI 修改
        │
        ├── AI 读取意图，修改 harness 自身源码
        ├── AI 读取意图，修改 examples/ 下被测 .c 文件
        │
        ▼
  检测到 harness 自身变更
        │
        ▼
  cmake + make 重新构建
        │
        ▼
  exec 新二进制 —— 演化完成
```

**关键设计：**
- `src/main.cpp` — 轻薄 bootstrap，负责调度、路径解析、反循环保护
- `src/harness_engine.cpp` — 核心引擎，Prompt 模板 / 管线逻辑 / 工具函数
- `include/harness_engine.h` — 头文件，常量 / 结构体 / 接口声明
- 每次运行完毕，若 harness 自身源码被修改，自动 `cmake && make && execv`

---

## 执行链路

```
intent.md
├── # 实现需求
│   ├── ## 新增功能         ──→  Phase 0: AI 按需求修改源码
│   ├── ## 新建文件         ──→  AI 生成新文件（路径相对项目根目录）
│   ├── ## 删除文件         ──→  删除指定文件（路径相对项目根目录）
│   │
│   └── 对 harness 自身     ──→  AI 修改 src/ 和 include/ 下源码
│                                  ↓
│                            检测变更 → 重新构建 → exec 新版本
│
└── # 测试意图              ──→  Phase 1: AI 生成测试代码
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

## 环境要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| **Linux** | 任意 | 当前仅支持 Linux |
| **gcc / g++** | ≥ 7 | 编译 C/C++ 源码 |
| **cmake** | ≥ 3.10 | 构建系统 |
| **libcurl** | 任意 | HTTP 调用 Ollama API |
| **Ollama** | 任意 | 本地大模型服务 |
| **DeepSeek-R1** | 任意 | 推荐模型 |

### 安装依赖

```bash
# Ubuntu / Debian
sudo apt install build-essential cmake libcurl4-openssl-dev

# 安装 Ollama
curl -fsSL https://ollama.com/install.sh | sh

# 拉取模型
ollama pull deepseek-r1
```

---

## 快速开始

```bash
git clone <your-repo-url>
cd mini_harness

# 编辑意图文档
vim intent.md

# 一键编译 + 运行
chmod +x run.bash
./run.bash
```

运行完成后，`build/` 目录下生成：
- `report.md` — 人类可读报告
- `report.json` — 机器可读结构化数据

### 手动指定源码目录

```bash
# 方式1：命令行参数
./build/mini_harness /path/to/your/c/source

# 方式2：环境变量
export HARNESS_SRC_DIR=/path/to/your/c/source
./build/mini_harness
```

---

## intent.md 编写指南

### `# 实现需求`

#### `## 新增功能`
描述需要新增或修改的代码功能，AI 会修改现有源码：

```markdown
## 新增功能
- 每笔远控执行前判断 CheckGatewayOnline，离线时上报 -3
- StopEngine 增加安全状态检查
```

#### `## 新建文件`
让 AI 创建全新文件，路径相对于项目根目录：

```markdown
## 新建文件
- examples/remote_aircon.c: 实现远程空调控制，包含温度设置和模式切换
- include/common_types.h: 定义共享的枚举类型和状态码
```

#### `## 删除文件`
删除不再需要的文件，路径相对于项目根目录：

```markdown
## 删除文件
- examples/deprecated_module.c
- include/old_header.h
```

---

### 自演化示例

修改 harness 自身行为：

```markdown
## 新增功能
- 在 Prompt_Judge 的评审维度中增加"性能考量"
- 将 MAX_RETRY 从 2 改为 3
- 在 Phase 4 之后增加 Phase 5：自动格式化代码
```

AI 会修改 `src/harness_engine.cpp` 和 `include/harness_engine.h`，然后自动重新构建并启动新版本。

**反循环保护**：新实例启动时检测 `intent.md` 是否已处理过，若未变化则跳过修改直接运行测试。

---

## 工作原理

### 路径解析

Harness 启动时自动检测项目结构：

```
从 build/ 运行
    │
    ├── 检测 ../CMakeLists.txt 存在 → projDir = ../
    ├── 默认 srcDir = ../examples/
    └── buildDir = ./
```

### 自愈机制

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
                   （最多重试 MAX_RETRY 次）
```

### 自演化机制

```
harness 运行时修改 src/harness_engine.cpp
              │
              ▼
     检测到自身源码变更
              │
              ▼
     cd build && cmake .. && make
              │
              ▼
     execv 新二进制（替换当前进程）
              │
              ▼
     新实例启动，读取 intent.md
              │
     检测 intent 未变化 → 跳过修改，仅运行测试
```

### AI 模型配置

在 `include/harness_engine.h` 中修改常量（这也是自演化的一部分）：

```cpp
constexpr const char* OLLAMA_URL   = "http://localhost:11434/api/generate";
constexpr const char* OLLAMA_MODEL = "deepseek-r1";
constexpr int         MAX_RETRY    = 2;
```

---

## 适用场景

- 车载嵌入式 C 代码的自动化回归测试
- 接手遗留代码时快速验证行为
- 需求变更后的自动化实现 + 验证
- CI/CD 流水线中的 AI 质量门禁
- **AI 辅助的工具链自我演化**

---

## License

MIT

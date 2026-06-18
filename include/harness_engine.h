#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// ═══════════════════ 可演化常量 ═══════════════════
constexpr const char* OLLAMA_URL   = "http://localhost:11434/api/generate";
constexpr const char* OLLAMA_MODEL = "deepseek-r1";
constexpr int         MAX_RETRY    = 2;

// ═══════════════════ 处理结果 ═══════════════════
struct Result {
    std::string file;
    std::string srcBefore;
    std::string srcAfter;
    std::string codeDiff;
    bool        codeModified   = false;
    bool        codeCompiled   = false;
    std::string codeCompileErr;
    int         codeFixRetries = 0;
    std::string aiTestCode;
    bool        testCompiled   = false;
    std::string testCompileErr;
    int         testFixRetries = 0;
    std::string testOutput;
    std::string verdict;
    bool        passed         = false;
    std::string codeReview;
    bool        isHarnessFile  = false;
    bool        isNewFile      = false;
};

// ═══════════════════ 文件收集 ═══════════════════
// 收集被测 .c 文件（examples/ 目录）
std::vector<fs::path> CollectSourceFiles(const fs::path& dir);

// 收集 harness 自身文件（src/ 和 include/ 目录）
std::vector<fs::path> CollectHarnessFiles(const fs::path& projDir);

// ═══════════════════ 意图解析 ═══════════════════
// projDir: 项目根目录（intent.md 所在）
std::string ReadIntent(const std::string& projDir);

// 提取代码修改意图，排除 ## 新建文件 / ## 删除文件
std::string ExtractCodeIntent(const std::string& intent);

// 解析 "## 新建文件" → { 相对路径, 需求描述 }
std::vector<std::pair<std::string, std::string>> ParseNewFiles(const std::string& intent);

// 解析 "## 删除文件" → 相对路径列表
std::vector<std::string> ParseDeleteFiles(const std::string& intent);

// ═══════════════════ 工具函数 ═══════════════════
std::string ReadFile(const std::string& path);
void        WriteFile(const std::string& path, const std::string& content);
std::string ExecCapture(const std::string& cmd);
std::string EscapeJson(const std::string& s);
std::string ExtractCodeBlock(const std::string& aiText);
std::string ExtractResponse(const std::string& raw);
std::string AskOllama(const std::string& prompt);
std::string GenerateDiff(const std::string& before,
                         const std::string& after,
                         const std::string& filename);

// ═══════════════════ 编译 ═══════════════════
std::pair<bool, std::string> CompileC(const std::string& srcFile,
                                       const std::string& exeFile,
                                       const std::string& includeDir);

// ═══════════════════ Prompt 模板（可被 AI 演化） ═══════════════════
std::string Prompt_Implement(const std::string& srcCode,
                             const std::string& intent,
                             const std::string& srcPath);

std::string Prompt_FixCode(const std::string& code,
                           const std::string& compileErr);

std::string Prompt_Generate(const std::string& srcCode,
                            const std::string& intent,
                            const std::string& srcPath);

std::string Prompt_FixTest(const std::string& testCode,
                           const std::string& compileErr);

std::string Prompt_Judge(const std::string& srcCode,
                         const std::string& testOutput);

std::string Prompt_Review(const std::string& srcCode);

std::string Prompt_CreateFile(const std::string& fileName,
                              const std::string& description,
                              const std::string& intent);

// ═══════════════════ 管线处理 ═══════════════════
Result ProcessSourceFile(const fs::path& fpath,
                         const std::string& intent,
                         bool isHarness);

Result GenerateNewFile(const fs::path& projDir,
                       const std::string& relPath,
                       const std::string& description,
                       const std::string& intent);

// ═══════════════════ 报告 ═══════════════════
void WriteReportMd(const std::vector<Result>& rs, const std::string& path);
void WriteReportJson(const std::vector<Result>& rs, const std::string& path);

// ═══════════════════ 自构建 ═══════════════════
bool HarnessFilesModified(const std::vector<Result>& results);

// buildDir: 构建输出目录（如 build/）
// projDir:  项目根目录（CMakeLists.txt, intent.md 所在）
void SelfRebuildAndExec(const std::string& buildDir,
                        const std::string& projDir,
                        int argc, char* argv[]);

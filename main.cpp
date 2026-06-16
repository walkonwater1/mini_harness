#include <curl/curl.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ═══════════════════ curl 回调 ═══════════════════
static size_t WriteCallback(void* contents, size_t sz, size_t nm, void* userp)
{
    ((std::string*)userp)->append((char*)contents, sz * nm);
    return sz * nm;
}

// ═══════════════════ 文件读写 ═══════════════════
static std::string ReadFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
        throw std::runtime_error("cannot open: " + path);
    std::stringstream buf;
    buf << ifs.rdbuf();
    return buf.str();
}

static void WriteFile(const std::string& path, const std::string& content)
{
    std::ofstream of(path);
    of << content;
}

static std::string ReadIntent(const std::string& dir)
{
    fs::path p = fs::path(dir) / "intent.md";
    if (fs::exists(p)) return ReadFile(p.string());

    return std::string(
        "## 测试意图\n"
        "对每个 C 源文件：\n"
        "1. 调用公开入口函数，验证正常路径\n"
        "2. 传入异常参数，验证错误处理\n"
        "3. 用 printf 输出每个测试步骤和 PASS/FAIL\n");
}

// ═══════════════════ JSON 工具 ═══════════════════
static std::string EscapeJson(const std::string& s)
{
    std::string o; o.reserve(s.size() * 2);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:   o += c;
        }
    }
    return o;
}

static std::string ExtractResponse(const std::string& raw)
{
    auto p = raw.find("\"error\":\"");
    if (p != std::string::npos) {
        size_t s = p + 9, e = raw.find('"', s);
        throw std::runtime_error("Ollama: " + raw.substr(s, e - s));
    }
    p = raw.find("\"response\":\"");
    if (p == std::string::npos)
        throw std::runtime_error("no response field");
    size_t i = p + 12;
    std::string r;
    while (i < raw.size()) {
        char c = raw[i];
        if (c == '\\') {
            if (++i >= raw.size()) break;
            switch (raw[i]) {
                case 'n': r += '\n'; break;
                case 'r': r += '\r'; break;
                case 't': r += '\t'; break;
                case '"': r += '"';  break;
                case '\\':r += '\\'; break;
                default:  r += raw[i];
            }
            ++i;
        } else if (c == '"') break;
        else { r += c; ++i; }
    }
    return r;
}

// ═══════════════════ Ollama 调用 ═══════════════════
static std::string AskOllama(const std::string& prompt)
{
    CURL* c = curl_easy_init();
    if (!c) throw std::runtime_error("curl init failed");

    std::string resp;
    std::string body = "{\"model\":\"deepseek-r1\",\"prompt\":\""
                     + EscapeJson(prompt) + "\",\"stream\":false}";

    curl_easy_setopt(c, CURLOPT_URL, "http://localhost:11434/api/generate");
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());

    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);

    CURLcode r = curl_easy_perform(c);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (r != CURLE_OK) throw std::runtime_error(curl_easy_strerror(r));
    return ExtractResponse(resp);
}

// ═══════════════════ 代码块提取 ═══════════════════
static std::string ExtractCodeBlock(const std::string& aiText)
{
    auto p = aiText.find("```c\n");
    if (p != std::string::npos) {
        size_t start = p + 5;
        size_t end   = aiText.find("\n```", start);
        if (end != std::string::npos) return aiText.substr(start, end - start);
    }
    p = aiText.find("```\n");
    if (p != std::string::npos) {
        size_t start = p + 4;
        size_t end   = aiText.find("\n```", start);
        if (end != std::string::npos) return aiText.substr(start, end - start);
    }
    return aiText;
}

// ═══════════════════ 命令执行 ═══════════════════
static std::string ExecCapture(const std::string& cmd)
{
    std::string out;
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) return "[ERROR] popen failed";
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);
    return out;
}

// ═══════════════════ 收集 .c 文件 ═══════════════════
static std::vector<fs::path> CollectCFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return files;
    for (auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".c")
            files.push_back(e.path());
    return files;
}

// ═══════════════════ Prompt 构建 ═══════════════════

// ── Phase 0: 按意图实现新功能 ──
static std::string Prompt_Implement(const std::string& srcCode,
                                    const std::string& intent,
                                    const std::string& srcPath)
{
    std::stringstream p;
    p << "你是资深车载 C 语言软件工程师。请根据需求意图修改以下源代码。\n\n"

      << "## 需求意图（来自 intent.md）\n"
      << intent << "\n\n"

      << "## 当前源码：" << srcPath << "\n"
      << "```c\n" << srcCode << "\n```\n\n"

      << "## 要求\n"
      << "1. 如果意图中提出了新增功能或修改要求，请实现它们\n"
      << "2. 如果意图中没有明确的代码修改要求，直接原样返回当前代码\n"
      << "3. 保持代码风格与现有代码一致（相同的命名、缩进、注释风格）\n"
      << "4. 输出完整的修改后 C 代码，放在 ```c ``` 代码块中\n"
      << "5. 不要在代码块外输出多余的解释，代码即回答\n";
    return p.str();
}

// ── Phase 0 fix: 修改后的代码编译失败，AI 修复 ──
static std::string Prompt_FixCode(const std::string& code,
                                  const std::string& compileErr)
{
    std::stringstream p;
    p << "编译以下 C 源码时报错，请修复。\n\n"
      << "## 编译错误\n```\n" << compileErr << "\n```\n\n"
      << "## 源码\n```c\n" << code << "\n```\n\n"
      << "只输出修复后的完整 C 代码，放在 ```c ``` 代码块中。\n";
    return p.str();
}

// ── Phase 1: 生成测试代码 ──
static std::string Prompt_Generate(const std::string& srcCode,
                                    const std::string& intent,
                                    const std::string& srcPath)
{
    std::stringstream p;
    p << "你是 C 语言测试工程师。请根据以下信息为被测源码生成一个可编译运行的 C 语言测试程序。\n\n"

      << "## 测试意图（来自 intent.md）\n"
      << intent << "\n\n"

      << "## 被测源码：" << srcPath << "\n"
      << "```c\n" << srcCode << "\n```\n\n"

      << "## 要求\n"
      << "1. 生成为一个完整的 .c 文件，开头必须写 #define main _orig_main 然后 #include 被测源文件，最后 #undef main 用自己的 main\n"
      << "2. 调用被测模块的公开入口函数，测试正常输入和异常输入\n"
      << "3. 每个测试步骤用 printf 输出 \"[TEST] 描述...\" 和 \"[PASS]\" 或 \"[FAIL] 原因\"\n"
      << "4. 测试结束后 printf 输出 \"测试通过: N, 测试失败: M\"\n"
      << "5. 只输出 C 代码，放在 ```c ``` 代码块中，不要多余的说明\n";
    return p.str();
}

// ── Phase 2 fix: 测试代码编译失败，AI 修复 ──
static std::string Prompt_FixTest(const std::string& testCode,
                                   const std::string& compileErr)
{
    std::stringstream p;
    p << "编译以下测试代码时报错，请修复。\n\n"
      << "## 编译错误\n```\n" << compileErr << "\n```\n\n"
      << "## 测试代码\n```c\n" << testCode << "\n```\n\n"
      << "只输出修复后的完整 C 代码，放在 ```c ``` 代码块中。\n";
    return p.str();
}

// ── Phase 4: AI 判决 ──
static std::string Prompt_Judge(const std::string& srcCode,
                                 const std::string& testOutput)
{
    std::stringstream p;
    p << "你是资深车载软件测试评审专家。以下是测试执行结果，请给出评审意见。\n\n"
      << "## 被测源码\n```c\n" << srcCode << "\n```\n\n"
      << "## 测试输出\n```\n" << testOutput << "\n```\n\n"
      << "请用中文回答，包含：\n"
      << "1. 判决（最后一行必须是 VERDICT: PASS 或 VERDICT: FAIL）\n"
      << "2. 如果 FAIL，说明哪些测试失败及原因\n"
      << "3. 对代码质量的简要点评\n";
    return p.str();
}

// ── Phase 4: AI 代码审查 ──
static std::string Prompt_Review(const std::string& srcCode)
{
    std::stringstream p;
    p << "你是资深车载软件工程师，请审查以下 C 代码。\n\n"
      << "```c\n" << srcCode << "\n```\n\n"
      << "从以下角度分析，用中文简洁回答：\n"
      << "1. 整体功能\n"
      << "2. 函数调用关系\n"
      << "3. 潜在问题和改进建议\n";
    return p.str();
}

// ═══════════════════ 单文件结果 ═══════════════════
struct Result
{
    std::string file;

    // Phase 0: AI 实现新功能
    std::string srcBefore;       // 修改前的源码
    std::string srcAfter;        // 修改后的源码
    bool        codeModified = false;
    bool        codeCompiled = false;
    std::string codeCompileErr;
    int         codeFixRetries = 0;

    // Phase 1-2: AI 生成 + 编译测试
    std::string aiTestCode;
    bool        testCompiled = false;
    std::string testCompileErr;
    int         testFixRetries = 0;

    // Phase 3: 测试执行
    std::string testOutput;

    // Phase 4: AI 判决 + 审查
    std::string verdict;
    bool        passed = false;
    std::string codeReview;
};

// ═══════════════════ 报告生成 ═══════════════════
static void WriteReportMd(const std::vector<Result>& rs, const std::string& path)
{
    int passCnt = 0, failCnt = 0;
    for (auto& r : rs) r.passed ? ++passCnt : ++failCnt;

    std::ofstream md(path);
    md << "# AI-Native Harness 报告\n\n";
    md << "> 生成时间：" << std::time(nullptr) << "\n\n";

    md << "## 总览\n\n";
    md << "| 文件 | AI实现 | AI测试 | 判决 |\n";
    md << "|------|--------|--------|------|\n";
    for (auto& r : rs) {
        std::string impl = r.codeModified
            ? (r.codeCompiled ? "已修改并编译通过" : "修改但编译失败(重试" + std::to_string(r.codeFixRetries) + "次)")
            : "未修改（无需新增功能）";
        std::string test = r.testCompiled
            ? "编译通过" + std::string(r.testFixRetries > 0 ? "(" + std::to_string(r.testFixRetries) + "次修复)" : "")
            : "编译失败(" + std::to_string(r.testFixRetries) + "次)";
        md << "| " << r.file
           << " | " << impl
           << " | " << test
           << " | " << (r.passed ? "✅ PASS" : "❌ FAIL") << " |\n";
    }
    md << "\n> **判定: " << passCnt << " 通过, " << failCnt << " 失败**\n\n---\n\n";

    for (auto& r : rs) {
        md << "## " << r.file << "\n\n";

        if (r.codeModified) {
            md << "### 📝 Phase 0: AI 修改了源码\n\n";
            md << "**修改前:**\n\n";
            md << "```c\n" << r.srcBefore << "\n```\n\n";
            md << "**修改后:**\n\n";
            md << "```c\n" << r.srcAfter << "\n```\n\n";
            md << "**编译:** "
               << (r.codeCompiled ? "通过" + std::string(r.codeFixRetries > 0 ? "（修复 " + std::to_string(r.codeFixRetries) + " 次）" : "") : "失败")
               << "\n\n";
            if (!r.codeCompiled)
                md << "```\n" << r.codeCompileErr << "\n```\n\n";
        } else {
            md << "### 📝 Phase 0: 无需修改源码\n\n";
        }

        md << "### 🤖 Phase 1: AI 生成的测试代码\n\n";
        md << "```c\n" << r.aiTestCode << "\n```\n\n";

        md << "### 📋 Phase 2: 编译结果\n\n";
        if (r.testCompiled)
            md << "编译通过" << (r.testFixRetries > 0 ? "（经 " + std::to_string(r.testFixRetries) + " 次修复）" : "") << "\n\n";
        else
            md << "```\n" << r.testCompileErr << "\n```\n\n";

        md << "### 🧪 Phase 3: 测试执行输出\n\n";
        md << "```\n" << r.testOutput << "\n```\n\n";

        md << "### ⚖️ Phase 4: AI 评审意见\n\n";
        md << r.verdict << "\n\n";

        md << "### 🔍 Phase 4: AI 代码审查\n\n";
        md << r.codeReview << "\n\n";
    }

    std::cout << "[OK] " << path << std::endl;
}

static void WriteReportJson(const std::vector<Result>& rs, const std::string& path)
{
    int passCnt = 0, failCnt = 0;
    for (auto& r : rs) r.passed ? ++passCnt : ++failCnt;

    std::ofstream js(path);
    js << "{\n  \"generated_at\": " << std::time(nullptr) << ",\n";
    js << "  \"pass\": " << passCnt << ",\n";
    js << "  \"fail\": " << failCnt << ",\n";
    js << "  \"files\": [\n";

    for (size_t i = 0; i < rs.size(); ++i) {
        auto& r = rs[i];
        js << "    {\n";
        js << "      \"file\": \"" << EscapeJson(r.file) << "\",\n";
        js << "      \"phase0_modified\": " << (r.codeModified ? "true" : "false") << ",\n";
        js << "      \"phase0_code_compiled\": " << (r.codeCompiled ? "true" : "false") << ",\n";
        js << "      \"phase0_fix_retries\": " << r.codeFixRetries << ",\n";
        js << "      \"phase0_src_before\": \"" << EscapeJson(r.srcBefore) << "\",\n";
        js << "      \"phase0_src_after\": \"" << EscapeJson(r.srcAfter) << "\",\n";
        js << "      \"phase0_compile_error\": \"" << EscapeJson(r.codeCompileErr) << "\",\n";
        js << "      \"phase1_test_code\": \"" << EscapeJson(r.aiTestCode) << "\",\n";
        js << "      \"phase2_compiled\": " << (r.testCompiled ? "true" : "false") << ",\n";
        js << "      \"phase2_fix_retries\": " << r.testFixRetries << ",\n";
        js << "      \"phase2_compile_error\": \"" << EscapeJson(r.testCompileErr) << "\",\n";
        js << "      \"phase3_test_output\": \"" << EscapeJson(r.testOutput) << "\",\n";
        js << "      \"phase4_verdict\": \"" << EscapeJson(r.verdict) << "\",\n";
        js << "      \"phase4_passed\": " << (r.passed ? "true" : "false") << ",\n";
        js << "      \"phase4_code_review\": \"" << EscapeJson(r.codeReview) << "\"\n";
        js << "    }";
        if (i + 1 < rs.size()) js << ",";
        js << "\n";
    }

    js << "  ]\n}\n";
    std::cout << "[OK] " << path << std::endl;
}

// ═══════════════════ 辅助：编译一个 C 文件 ═══════════════════
// 返回 {success, output}，成功时 output 为空
static std::pair<bool, std::string> CompileC(const std::string& srcFile,
                                              const std::string& exeFile,
                                              const std::string& includeDir)
{
    std::string cmd = "gcc " + srcFile + " -o " + exeFile + " -I" + includeDir;
    std::string out = ExecCapture(cmd);
    return {out.empty() && fs::exists(exeFile), out};
}

// ═══════════════════ 主流程 ═══════════════════
int main(int argc, char* argv[])
{
    try {
        // ── 解析参数 ──
        fs::path srcDir;
        if (argc > 1)      srcDir = argv[1];
        else if (auto* e = std::getenv("HARNESS_SRC_DIR")) srcDir = e;
        else               srcDir = fs::current_path();

        std::cout << "╔══════════════════════════════════╗\n";
        std::cout << "║   AI-Native Mini Harness v3     ║\n";
        std::cout << "╚══════════════════════════════════╝\n\n";

        // ── 读取意图文档 ──
        std::string intent = ReadIntent(srcDir.string());
        std::cout << "[INTENT] 已加载 intent.md\n";

        // ── 扫描源文件 ──
        auto files = CollectCFiles(srcDir);
        if (files.empty()) {
            std::cerr << "[ERR] 没有找到 .c 文件" << std::endl;
            return 1;
        }
        std::cout << "[SCAN] 找到 " << files.size() << " 个 .c 文件\n";

        // ── 逐文件处理 ──
        std::vector<Result> results;

        for (size_t i = 0; i < files.size(); ++i) {
            auto fpath = files[i];
            auto fname = fpath.filename().string();
            std::string origSrc = ReadFile(fpath.string());

            std::cout << "\n━━━ [" << (i+1) << "/" << files.size()
                      << "] " << fname << " ━━━\n";

            Result r;
            r.file      = fname;
            r.srcBefore = origSrc;

            // ═══════════ Phase 0: AI 按意图实现新功能 ═══════════
            std::cout << "  [0/4] AI 按意图实现功能..." << std::endl;
            {
                std::string implPrompt = Prompt_Implement(origSrc, intent, fname);
                std::string aiImplText = AskOllama(implPrompt);
                r.srcAfter = ExtractCodeBlock(aiImplText);

                r.codeModified = (r.srcAfter != origSrc);
                if (r.codeModified)
                    std::cout << "  [0/4] AI 修改了源码" << std::endl;
                else
                    std::cout << "  [0/4] 无需修改源码" << std::endl;
            }

            // 编译修改后的源码 → 失败则 AI 修复
            if (r.codeModified) {
                const int MAX_RETRY = 2;
                for (int retry = 0; retry <= MAX_RETRY; ++retry) {
                    r.codeFixRetries = retry;
                    std::cout << "  [0/4] 验证编译"
                              << (retry > 0 ? " (重试 " + std::to_string(retry) + ")" : "")
                              << "..." << std::endl;

                    std::string tmpSrc = "/tmp/_ai_src_" + fname;
                    WriteFile(tmpSrc, r.srcAfter);

                    auto [ok, err] = CompileC(tmpSrc, "/tmp/_ai_src_exe",
                                               fpath.parent_path().string());
                    if (ok) {
                        r.codeCompiled = true;
                        break;
                    }

                    r.codeCompileErr = err;
                    if (retry < MAX_RETRY) {
                        std::cout << "  [!] 编译失败，AI 修复源码..." << std::endl;
                        try {
                            std::string fixPrompt = Prompt_FixCode(r.srcAfter, err);
                            r.srcAfter = ExtractCodeBlock(AskOllama(fixPrompt));
                        } catch (const std::exception& e) {
                            r.codeCompileErr += "\n[FIX] " + std::string(e.what());
                            break;
                        }
                    }
                }
            } else {
                r.codeCompiled = true;
            }

            // 后续阶段使用最终版本的源码（AI修改过的 或 原始的）
            std::string finalSrc = r.codeModified ? r.srcAfter : origSrc;

            // ═══════════ Phase 1: AI 生成测试代码 ═══════════
            std::cout << "  [1/4] AI 生成测试代码..." << std::endl;
            {
                std::string genPrompt = Prompt_Generate(finalSrc, intent, fname);
                r.aiTestCode = ExtractCodeBlock(AskOllama(genPrompt));
            }

            // ═══════════ Phase 2: 编译测试 → 失败则 AI 修复 ═══════════
            r.testCompiled = false;
            const int MAX_RETRY = 2;
            for (int retry = 0; retry <= MAX_RETRY; ++retry) {
                r.testFixRetries = retry;
                std::cout << "  [2/4] 编译测试"
                          << (retry > 0 ? " (重试 " + std::to_string(retry) + ")" : "")
                          << "..." << std::endl;

                std::string tmpC = "/tmp/_ai_test_" + fname;
                WriteFile(tmpC, r.aiTestCode);

                auto [ok, err] = CompileC(tmpC, "/tmp/_ai_test_exe",
                                           fpath.parent_path().string());
                if (ok) {
                    r.testCompiled = true;
                    break;
                }

                r.testCompileErr = err;
                if (retry < MAX_RETRY) {
                    std::cout << "  [!] 编译失败，AI 修复测试..." << std::endl;
                    try {
                        std::string fixPrompt = Prompt_FixTest(r.aiTestCode, err);
                        r.aiTestCode = ExtractCodeBlock(AskOllama(fixPrompt));
                    } catch (const std::exception& e) {
                        r.testCompileErr += "\n[FIX] " + std::string(e.what());
                        break;
                    }
                }
            }

            // ═══════════ Phase 3: 运行测试 ═══════════
            if (r.testCompiled) {
                std::cout << "  [3/4] 运行测试..." << std::endl;
                r.testOutput = ExecCapture("/tmp/_ai_test_exe");
            } else {
                r.testOutput = "[SKIP] 测试编译失败";
            }

            // ═══════════ Phase 4: AI 判决 + 代码审查 ═══════════
            std::cout << "  [4/4] AI 评审..." << std::endl;
            {
                std::string jPrompt = Prompt_Judge(finalSrc, r.testOutput);
                r.verdict = AskOllama(jPrompt);
                r.passed = (r.verdict.find("VERDICT: PASS") != std::string::npos);
            }
            {
                r.codeReview = AskOllama(Prompt_Review(finalSrc));
            }

            // 简要总结
            std::cout << "  ── " << (r.passed ? "✅ PASS" : "❌ FAIL");
            if (r.codeModified)
                std::cout << " (AI实现: " << (r.codeCompiled ? "OK" : "FAIL") << ")";
            std::cout << " ──" << std::endl;

            results.push_back(r);
        }

        // ── 写报告 ──
        std::cout << "\n[REPORT] 生成报告..." << std::endl;
        WriteReportMd(results, "report.md");
        WriteReportJson(results, "report.json");

        int passCnt = 0;
        for (auto& r : results) if (r.passed) ++passCnt;
        std::cout << "\n[DONE] " << passCnt << "/" << results.size()
                  << " 通过" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

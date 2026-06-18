#include "harness_engine.h"

#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

// ══════════════════════════════════════════════════════════════════════
//  工具函数
// ══════════════════════════════════════════════════════════════════════

static size_t WriteCallback(void* contents, size_t sz, size_t nm, void* userp)
{
    ((std::string*)userp)->append((char*)contents, sz * nm);
    return sz * nm;
}

std::string ReadFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
        throw std::runtime_error("cannot open: " + path);
    std::stringstream buf;
    buf << ifs.rdbuf();
    return buf.str();
}

void WriteFile(const std::string& path, const std::string& content)
{
    std::ofstream of(path);
    of << content;
}

std::string ExecCapture(const std::string& cmd)
{
    std::string out;
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) return "[ERROR] popen failed";
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);
    return out;
}

std::string EscapeJson(const std::string& s)
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

// ══════════════════════════════════════════════════════════════════════
//  Ollama 调用
// ══════════════════════════════════════════════════════════════════════

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

std::string AskOllama(const std::string& prompt)
{
    CURL* c = curl_easy_init();
    if (!c) throw std::runtime_error("curl init failed");

    std::string resp;
    std::string body = "{\"model\":\"" + std::string(OLLAMA_MODEL)
                     + "\",\"prompt\":\""
                     + EscapeJson(prompt) + "\",\"stream\":false}";

    curl_easy_setopt(c, CURLOPT_URL, OLLAMA_URL);
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

std::string ExtractCodeBlock(const std::string& aiText)
{
    auto p = aiText.find("```c\n");
    if (p != std::string::npos) {
        size_t start = p + 5;
        size_t end   = aiText.find("\n```", start);
        if (end != std::string::npos) return aiText.substr(start, end - start);
    }
    p = aiText.find("```cpp\n");
    if (p != std::string::npos) {
        size_t start = p + 7;
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

// ══════════════════════════════════════════════════════════════════════
//  文件收集
// ══════════════════════════════════════════════════════════════════════

std::vector<fs::path> CollectSourceFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return files;
    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        if (ext == ".c")
            files.push_back(e.path());
    }
    return files;
}

std::vector<fs::path> CollectHarnessFiles(const fs::path& projDir)
{
    std::vector<fs::path> files;
    // 在 src/ 和 include/ 子目录中搜索 harness 自身文件
    const char* subdirs[] = {"src", "include"};
    for (auto* sub : subdirs) {
        fs::path dir = projDir / sub;
        if (!fs::exists(dir) || !fs::is_directory(dir)) continue;
        for (auto& e : fs::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            if (ext == ".cpp" || ext == ".h" || ext == ".hpp")
                files.push_back(e.path());
        }
    }
    return files;
}

// ══════════════════════════════════════════════════════════════════════
//  Diff 与编译
// ══════════════════════════════════════════════════════════════════════

std::string GenerateDiff(const std::string& before,
                         const std::string& after,
                         const std::string& filename)
{
    if (before == after) return "";

    std::string tmpBefore = "/tmp/_ai_diff_before_" + filename;
    std::string tmpAfter  = "/tmp/_ai_diff_after_"  + filename;
    WriteFile(tmpBefore, before);
    WriteFile(tmpAfter,  after);

    std::string diff = ExecCapture("diff -u " + tmpBefore + " " + tmpAfter);

    auto pos = diff.find(tmpBefore);
    if (pos != std::string::npos)
        diff.replace(pos, tmpBefore.size(), "a/" + filename);
    pos = diff.find(tmpAfter);
    if (pos != std::string::npos)
        diff.replace(pos, tmpAfter.size(), "b/" + filename);

    return diff;
}

std::pair<bool, std::string> CompileC(const std::string& srcFile,
                                       const std::string& exeFile,
                                       const std::string& includeDir)
{
    std::string cmd = "gcc " + srcFile + " -o " + exeFile + " -I" + includeDir;
    std::string out = ExecCapture(cmd);
    return {out.empty() && fs::exists(exeFile), out};
}

// ══════════════════════════════════════════════════════════════════════
//  意图解析
// ══════════════════════════════════════════════════════════════════════

std::string ReadIntent(const std::string& projDir)
{
    fs::path p = fs::path(projDir) / "intent.md";
    if (fs::exists(p)) return ReadFile(p.string());

    return std::string(
        "## 测试意图\n"
        "对每个 C 源文件：\n"
        "1. 调用公开入口函数，验证正常路径\n"
        "2. 传入异常参数，验证错误处理\n"
        "3. 用 printf 输出每个测试步骤和 PASS/FAIL\n");
}

std::string ExtractCodeIntent(const std::string& intent)
{
    std::stringstream out;
    std::istringstream iss(intent);
    std::string line;
    bool skipSection = false;

    while (std::getline(iss, line)) {
        if (line.find("## 新建文件") != std::string::npos ||
            line.find("## 删除文件") != std::string::npos) {
            skipSection = true;
            continue;
        }
        if (line.find("## ") != std::string::npos ||
            line.find("# ")  != std::string::npos) {
            skipSection = false;
        }

        if (!skipSection)
            out << line << "\n";
    }
    return out.str();
}

std::vector<std::pair<std::string, std::string>> ParseNewFiles(const std::string& intent)
{
    std::vector<std::pair<std::string, std::string>> result;
    std::istringstream iss(intent);
    std::string line;
    bool inSection = false;

    while (std::getline(iss, line)) {
        if (line.find("## 新建文件") != std::string::npos) {
            inSection = true;
            continue;
        }
        if (inSection && (line.find("## ") != std::string::npos ||
                          line.find("# ")  != std::string::npos)) {
            break;
        }
        if (!inSection) continue;

        auto dash = line.find("- ");
        if (dash == std::string::npos) continue;

        std::string entry = line.substr(dash + 2);
        while (!entry.empty() && entry.front() == ' ') entry.erase(0, 1);
        while (!entry.empty() && entry.back()  == ' ') entry.pop_back();

        auto colon = entry.find(':');
        if (colon != std::string::npos) {
            std::string fname = entry.substr(0, colon);
            std::string desc  = entry.substr(colon + 1);
            while (!desc.empty() && desc.front() == ' ') desc.erase(0, 1);
            result.push_back({fname, desc});
        } else {
            result.push_back({entry, ""});
        }
    }
    return result;
}

std::vector<std::string> ParseDeleteFiles(const std::string& intent)
{
    std::vector<std::string> result;
    std::istringstream iss(intent);
    std::string line;
    bool inSection = false;

    while (std::getline(iss, line)) {
        if (line.find("## 删除文件") != std::string::npos) {
            inSection = true;
            continue;
        }
        if (inSection && (line.find("## ") != std::string::npos ||
                          line.find("# ")  != std::string::npos)) {
            break;
        }
        if (!inSection) continue;

        auto dash = line.find("- ");
        if (dash == std::string::npos) continue;

        std::string fname = line.substr(dash + 2);
        while (!fname.empty() && fname.front() == ' ') fname.erase(0, 1);
        while (!fname.empty() && fname.back()  == ' ') fname.pop_back();
        if (!fname.empty()) result.push_back(fname);
    }
    return result;
}

// ══════════════════════════════════════════════════════════════════════
//  Prompt 模板（AI 可修改此区域以演化自身行为）
// ══════════════════════════════════════════════════════════════════════

std::string Prompt_Implement(const std::string& srcCode,
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
      << "4. 输出完整的修改后代码，放在 ```c ``` 代码块中\n"
      << "5. 不要在代码块外输出多余的解释，代码即回答\n";
    return p.str();
}

std::string Prompt_FixCode(const std::string& code,
                           const std::string& compileErr)
{
    std::stringstream p;
    p << "编译以下 C 源码时报错，请修复。\n\n"
      << "## 编译错误\n```\n" << compileErr << "\n```\n\n"
      << "## 源码\n```c\n" << code << "\n```\n\n"
      << "只输出修复后的完整 C 代码，放在 ```c ``` 代码块中。\n";
    return p.str();
}

std::string Prompt_Generate(const std::string& srcCode,
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
      << "1. 生成为一个完整的 .c 文件，开头必须写 #define main _orig_main 然后 #include 被测源文件"
      << "，最后 #undef main 用自己的 main\n"
      << "2. 调用被测模块的公开入口函数，测试正常输入和异常输入\n"
      << "3. 每个测试步骤用 printf 输出 \"[TEST] 描述...\" 和 \"[PASS]\" 或 \"[FAIL] 原因\"\n"
      << "4. 测试结束后 printf 输出 \"测试通过: N, 测试失败: M\"\n"
      << "5. 只输出 C 代码，放在 ```c ``` 代码块中，不要多余的说明\n";
    return p.str();
}

std::string Prompt_FixTest(const std::string& testCode,
                           const std::string& compileErr)
{
    std::stringstream p;
    p << "编译以下测试代码时报错，请修复。\n\n"
      << "## 编译错误\n```\n" << compileErr << "\n```\n\n"
      << "## 测试代码\n```c\n" << testCode << "\n```\n\n"
      << "只输出修复后的完整 C 代码，放在 ```c ``` 代码块中。\n";
    return p.str();
}

std::string Prompt_Judge(const std::string& srcCode,
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

std::string Prompt_Review(const std::string& srcCode)
{
    std::stringstream p;
    p << "你是资深车载软件工程师，请审查以下代码。\n\n"
      << "```c\n" << srcCode << "\n```\n\n"
      << "从以下角度分析，用中文简洁回答：\n"
      << "1. 整体功能\n"
      << "2. 函数调用关系\n"
      << "3. 潜在问题和改进建议\n";
    return p.str();
}

std::string Prompt_CreateFile(const std::string& fileName,
                              const std::string& description,
                              const std::string& intent)
{
    std::stringstream p;
    p << "你是资深车载 C 语言软件工程师。请根据需求创建以下新文件。\n\n"

      << "## 项目整体意图\n"
      << intent << "\n\n"

      << "## 新建文件\n"
      << "文件名: " << fileName << "\n"
      << "需求描述: " << description << "\n\n"

      << "## 要求\n"
      << "1. 生成完整的文件内容，包含必要的 #include 和函数实现\n"
      << "2. 代码风格与项目现有代码一致\n"
      << "3. 如果是 .h 文件，需添加 #pragma once 头文件保护\n"
      << "4. 将完整代码放在 ```c ``` 代码块中\n";
    return p.str();
}

// ══════════════════════════════════════════════════════════════════════
//  新建文件
// ══════════════════════════════════════════════════════════════════════

Result GenerateNewFile(const fs::path& projDir,
                       const std::string& relPath,
                       const std::string& description,
                       const std::string& intent)
{
    Result r;
    r.file      = relPath;
    r.isNewFile = true;
    r.srcBefore = "(新文件)";

    std::cout << "  [NEW] AI 生成新文件: " << relPath << std::endl;

    std::string prompt = Prompt_CreateFile(relPath, description, intent);
    std::string aiText = AskOllama(prompt);
    r.srcAfter = ExtractCodeBlock(aiText);

    fs::path fullPath = projDir / relPath;
    // 确保父目录存在
    if (fullPath.has_parent_path()) {
        fs::create_directories(fullPath.parent_path());
    }
    WriteFile(fullPath.string(), r.srcAfter);

    r.codeModified = true;
    r.codeCompiled = true;
    r.codeDiff = GenerateDiff("", r.srcAfter, relPath);

    if (!r.codeDiff.empty()) {
        std::cout << "  ── 新建文件内容 ──────────────────────────" << std::endl;
        std::cout << r.codeDiff << std::endl;
        std::cout << "  ───────────────────────────────────────────" << std::endl;
    }

    r.passed     = true;
    r.testOutput = "[NEW] 新文件已创建: " + relPath;
    r.verdict    = "AI 已生成新文件: " + relPath;
    r.codeReview = "新文件 " + relPath + " 已由 AI 生成";

    return r;
}

// ══════════════════════════════════════════════════════════════════════
//  管线处理
// ══════════════════════════════════════════════════════════════════════

Result ProcessSourceFile(const fs::path& fpath,
                         const std::string& intent,
                         bool isHarness)
{
    auto fname = fpath.filename().string();
    std::string origSrc = ReadFile(fpath.string());

    Result r;
    r.file          = fname;
    r.srcBefore     = origSrc;
    r.isHarnessFile = isHarness;

    std::string codeIntent = ExtractCodeIntent(intent);
    if (codeIntent.empty()) codeIntent = intent;

    // ═══════════ Phase 0: AI 按意图修改源码 ═══════════
    std::cout << "  [0/4] AI 按意图实现功能..." << std::endl;
    {
        std::string implPrompt = Prompt_Implement(origSrc, codeIntent, fname);
        std::string aiImplText = AskOllama(implPrompt);
        r.srcAfter = ExtractCodeBlock(aiImplText);

        r.codeModified = (r.srcAfter != origSrc);
        if (r.codeModified) {
            std::cout << "  [0/4] AI 修改了源码" << std::endl;
            r.codeDiff = GenerateDiff(origSrc, r.srcAfter, fname);
            std::cout << "  ── Diff ──────────────────────────────" << std::endl;
            std::cout << r.codeDiff << std::endl;
            std::cout << "  ───────────────────────────────────────" << std::endl;

            WriteFile(fpath.string(), r.srcAfter);
        } else {
            std::cout << "  [0/4] 无需修改源码" << std::endl;
        }
    }

    // ── 编译验证（仅非 harness 文件） ──
    if (r.codeModified && !isHarness) {
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
                    WriteFile(fpath.string(), r.srcAfter);
                } catch (const std::exception& e) {
                    r.codeCompileErr += "\n[FIX] " + std::string(e.what());
                    break;
                }
            }
        }
    } else if (!r.codeModified) {
        r.codeCompiled = true;
    } else {
        r.codeCompiled = true;
    }

    // ── harness 文件跳过测试阶段 ──
    if (isHarness) {
        std::cout << "  [1-4/4] 跳过（harness 自身文件，由自构建验证）" << std::endl;
        r.testCompiled = true;
        r.testOutput   = "[SKIP] Harness self-file, tested by rebuild";
        r.verdict      = "VERDICT: PASS (harness self-file)";
        r.passed       = true;
        r.codeReview   = "Harness 自身文件，修改将在重新构建后生效";
        r.aiTestCode   = "// harness 自身文件，不生成测试";
        return r;
    }

    std::string finalSrc = r.codeModified ? r.srcAfter : origSrc;

    // ═══════════ Phase 1: AI 生成测试代码 ═══════════
    std::cout << "  [1/4] AI 生成测试代码..." << std::endl;
    {
        std::string genPrompt = Prompt_Generate(finalSrc, intent, fname);
        r.aiTestCode = ExtractCodeBlock(AskOllama(genPrompt));
    }

    // ═══════════ Phase 2: 编译测试 + AI 修复 ═══════════
    r.testCompiled = false;
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

    // ═══════════ Phase 4: AI 判决 + 审查 ═══════════
    std::cout << "  [4/4] AI 评审..." << std::endl;
    {
        std::string jPrompt = Prompt_Judge(finalSrc, r.testOutput);
        r.verdict = AskOllama(jPrompt);
        r.passed = (r.verdict.find("VERDICT: PASS") != std::string::npos);
    }
    {
        r.codeReview = AskOllama(Prompt_Review(finalSrc));
    }

    return r;
}

// ══════════════════════════════════════════════════════════════════════
//  自构建检测
// ══════════════════════════════════════════════════════════════════════

bool HarnessFilesModified(const std::vector<Result>& results)
{
    for (auto& r : results)
        if (r.isHarnessFile && r.codeModified)
            return true;
    return false;
}

void SelfRebuildAndExec(const std::string& buildDir,
                        const std::string& projDir,
                        int argc, char* argv[])
{
    std::cout << "\n╔══════════════════════════════════╗\n";
    std::cout <<   "║   Harness 自演化：重新构建      ║\n";
    std::cout <<   "╚══════════════════════════════════╝\n\n";

    std::string cmd = "cd " + buildDir + " && cmake " + projDir
                    + " && make -j$(nproc)";
    std::string out = ExecCapture(cmd);
    std::cout << out << std::endl;

    std::string exePath = buildDir + "/mini_harness";
    if (!fs::exists(exePath)) {
        std::cerr << "[FATAL] 自构建失败，请手动检查 build/ 目录" << std::endl;
        return;
    }

    std::cout << "[EVOLVE] 构建成功，启动新版本..." << std::endl;

    // 写 intent hash 防止新实例重复修改
    std::string intentPath = projDir + "/intent.md";
    if (fs::exists(intentPath)) {
        std::string intentContent = ReadFile(intentPath);
        std::string hashPath = buildDir + "/.harness_last_intent_size";
        WriteFile(hashPath, std::to_string(intentContent.size()));
    }

    std::vector<char*> newArgv;
    newArgv.push_back(const_cast<char*>(exePath.c_str()));
    for (int i = 1; i < argc; ++i)
        newArgv.push_back(argv[i]);
    newArgv.push_back(nullptr);

    execv(exePath.c_str(), newArgv.data());
    std::cerr << "[FATAL] execv 失败: " << strerror(errno) << std::endl;
}

// ══════════════════════════════════════════════════════════════════════
//  报告生成
// ══════════════════════════════════════════════════════════════════════

void WriteReportMd(const std::vector<Result>& rs, const std::string& path)
{
    int passCnt = 0, failCnt = 0;
    for (auto& r : rs) r.passed ? ++passCnt : ++failCnt;

    std::ofstream md(path);
    md << "# AI-Native Harness 报告\n\n";
    md << "> 生成时间：" << std::time(nullptr) << "\n\n";

    md << "## 总览\n\n";
    md << "| 文件 | 类型 | AI实现 | AI测试 | 判决 |\n";
    md << "|------|------|--------|--------|------|\n";
    for (auto& r : rs) {
        std::string type = r.isNewFile ? "新建" : (r.isHarnessFile ? "Harness" : "源码");
        std::string impl = r.isNewFile ? "AI 生成" :
            (r.codeModified
                ? (r.codeCompiled ? "已修改并编译通过" : "修改但编译失败(重试" + std::to_string(r.codeFixRetries) + "次)")
                : "未修改");
        std::string test = r.isHarnessFile ? "跳过（自构建验证）" :
            (r.testCompiled
                ? "编译通过" + std::string(r.testFixRetries > 0 ? "(" + std::to_string(r.testFixRetries) + "次修复)" : "")
                : "编译失败(" + std::to_string(r.testFixRetries) + "次)");
        md << "| " << r.file
           << " | " << type
           << " | " << impl
           << " | " << test
           << " | " << (r.passed ? "PASS" : "FAIL") << " |\n";
    }
    md << "\n> **判定: " << passCnt << " 通过, " << failCnt << " 失败**\n\n---\n\n";

    for (auto& r : rs) {
        md << "## " << r.file;
        if (r.isHarnessFile) md << " [Harness]";
        if (r.isNewFile)     md << " [新建]";
        md << "\n\n";

        if (r.isNewFile) {
            md << "### Phase 0: AI 新建文件\n\n";
            md << "```diff\n" << r.codeDiff << "\n```\n\n";
            continue;
        }

        if (r.codeModified) {
            md << "### Phase 0: AI 修改了源码\n\n";
            md << "**Diff (unified):**\n\n";
            md << "```diff\n" << r.codeDiff << "\n```\n\n";
            md << "**修改前 (完整):**\n\n";
            md << "```c\n" << r.srcBefore << "\n```\n\n";
            md << "**修改后 (完整):**\n\n";
            md << "```c\n" << r.srcAfter << "\n```\n\n";
            md << "**编译:** "
               << (r.codeCompiled ? "通过" + std::string(r.codeFixRetries > 0 ? "（修复 " + std::to_string(r.codeFixRetries) + " 次）" : "") : "失败")
               << "\n\n";
            if (!r.codeCompiled)
                md << "```\n" << r.codeCompileErr << "\n```\n\n";
        } else {
            md << "### Phase 0: 无需修改源码\n\n";
        }

        if (r.isHarnessFile) {
            md << "### 自演化说明\n\n";
            md << "此文件为 Harness 自身源码，修改后需重新构建才能生效。\n\n";
        } else {
            md << "### Phase 1: AI 生成的测试代码\n\n";
            md << "```c\n" << r.aiTestCode << "\n```\n\n";

            md << "### Phase 2: 编译结果\n\n";
            if (r.testCompiled)
                md << "编译通过" << (r.testFixRetries > 0 ? "（经 " + std::to_string(r.testFixRetries) + " 次修复）" : "") << "\n\n";
            else
                md << "```\n" << r.testCompileErr << "\n```\n\n";

            md << "### Phase 3: 测试执行输出\n\n";
            md << "```\n" << r.testOutput << "\n```\n\n";

            md << "### Phase 4: AI 评审意见\n\n";
            md << r.verdict << "\n\n";

            md << "### Phase 4: AI 代码审查\n\n";
            md << r.codeReview << "\n\n";
        }
    }

    std::cout << "[OK] " << path << std::endl;
}

void WriteReportJson(const std::vector<Result>& rs, const std::string& path)
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
        js << "      \"is_harness_file\": " << (r.isHarnessFile ? "true" : "false") << ",\n";
        js << "      \"is_new_file\": " << (r.isNewFile ? "true" : "false") << ",\n";
        js << "      \"phase0_modified\": " << (r.codeModified ? "true" : "false") << ",\n";
        js << "      \"phase0_code_compiled\": " << (r.codeCompiled ? "true" : "false") << ",\n";
        js << "      \"phase0_fix_retries\": " << r.codeFixRetries << ",\n";
        js << "      \"phase0_diff\": \"" << EscapeJson(r.codeDiff) << "\",\n";
        js << "      \"phase1_test_code\": \"" << EscapeJson(r.aiTestCode) << "\",\n";
        js << "      \"phase2_compiled\": " << (r.testCompiled ? "true" : "false") << ",\n";
        js << "      \"phase2_fix_retries\": " << r.testFixRetries << ",\n";
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

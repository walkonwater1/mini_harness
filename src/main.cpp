#include "harness_engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

// ══════════════════════════════════════════════════════════════════════
//  轻薄 Bootstrap —— 项目根目录 / 源码目录分离
// ══════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[])
{
    try {
        // ── 检测项目根目录（CMakeLists.txt, intent.md 所在） ──
        fs::path cwd     = fs::current_path();
        fs::path projDir;
        fs::path buildDir = cwd;

        // 从 build/ 运行时自动找到项目根目录
        if (fs::exists(cwd / ".." / "CMakeLists.txt")) {
            projDir = fs::canonical(cwd / "..");
        } else if (fs::exists(cwd / "CMakeLists.txt")) {
            projDir = cwd;
        } else {
            projDir = cwd; // fallback
        }

        // ── 被测源码目录（.c 文件所在） ──
        fs::path srcDir;
        if (argc > 1)      srcDir = argv[1];
        else if (auto* e = std::getenv("HARNESS_SRC_DIR")) srcDir = e;
        else               srcDir = projDir / "examples";

        std::cout << "╔══════════════════════════════════╗\n";
        std::cout << "║   AI-Native Mini Harness v4     ║\n";
        std::cout << "║   Self-Evolving Architecture    ║\n";
        std::cout << "╚══════════════════════════════════╝\n\n";

        std::cout << "[PATH] projDir  = " << projDir.string() << "\n";
        std::cout << "[PATH] srcDir   = " << srcDir.string() << "\n";
        std::cout << "[PATH] buildDir = " << buildDir.string() << "\n\n";

        // ── 反循环保护：检测 intent 是否已处理过 ──
        {
            std::string hashPath   = buildDir.string() + "/.harness_last_intent_size";
            std::string intentPath = projDir.string() + "/intent.md";
            if (fs::exists(hashPath) && fs::exists(intentPath)) {
                try {
                    std::string lastSize = ReadFile(hashPath);
                    std::string curSize  = std::to_string(ReadFile(intentPath).size());
                    if (lastSize == curSize) {
                        std::cout << "[GUARD] intent.md 未变化，跳过代码修改阶段\n";
                        std::cout << "[GUARD] 仅运行测试...\n\n";

                        auto files = CollectSourceFiles(srcDir);
                        std::vector<Result> results;
                        std::string intent = ReadIntent(projDir.string());
                        for (auto& f : files) {
                            std::cout << "━━━ " << f.filename().string() << " ━━━\n";
                            Result r;
                            r.file      = f.filename().string();
                            r.srcBefore = ReadFile(f.string());
                            r.srcAfter  = r.srcBefore;
                            r.codeModified = false;
                            r.codeCompiled = true;
                            r.isHarnessFile = false;

                            std::cout << "  [1/4] AI 生成测试代码..." << std::endl;
                            r.aiTestCode = ExtractCodeBlock(
                                AskOllama(Prompt_Generate(r.srcBefore, intent, r.file)));

                            r.testCompiled = false;
                            for (int retry = 0; retry <= MAX_RETRY; ++retry) {
                                r.testFixRetries = retry;
                                std::cout << "  [2/4] 编译测试"
                                          << (retry > 0 ? " (重试 " + std::to_string(retry) + ")" : "")
                                          << "..." << std::endl;
                                std::string tmpC = "/tmp/_ai_test_" + r.file;
                                WriteFile(tmpC, r.aiTestCode);
                                auto [ok, err] = CompileC(tmpC, "/tmp/_ai_test_exe",
                                                           f.parent_path().string());
                                if (ok) { r.testCompiled = true; break; }
                                r.testCompileErr = err;
                                if (retry < MAX_RETRY) {
                                    std::cout << "  [!] AI 修复测试..." << std::endl;
                                    r.aiTestCode = ExtractCodeBlock(
                                        AskOllama(Prompt_FixTest(r.aiTestCode, err)));
                                }
                            }

                            if (r.testCompiled) {
                                std::cout << "  [3/4] 运行测试..." << std::endl;
                                r.testOutput = ExecCapture("/tmp/_ai_test_exe");
                            } else {
                                r.testOutput = "[SKIP] 测试编译失败";
                            }

                            std::cout << "  [4/4] AI 评审..." << std::endl;
                            r.verdict = AskOllama(Prompt_Judge(r.srcBefore, r.testOutput));
                            r.passed = (r.verdict.find("VERDICT: PASS") != std::string::npos);
                            r.codeReview = AskOllama(Prompt_Review(r.srcBefore));

                            std::cout << "  ── " << (r.passed ? "PASS" : "FAIL") << " ──\n";
                            results.push_back(r);
                        }

                        std::cout << "\n[REPORT] 生成报告..." << std::endl;
                        WriteReportMd(results, "report.md");
                        WriteReportJson(results, "report.json");
                        int passCnt = 0;
                        for (auto& r : results) if (r.passed) ++passCnt;
                        std::cout << "\n[DONE] " << passCnt << "/" << results.size() << " 通过" << std::endl;
                        return 0;
                    }
                } catch (...) { /* ignore corrupt state */ }
            }
        }

        // ── 读取意图文档 ──
        std::string intent = ReadIntent(projDir.string());
        std::cout << "[INTENT] 已加载 intent.md\n";

        // ── 解析文件级别操作 ──
        auto newFiles    = ParseNewFiles(intent);
        auto deleteFiles = ParseDeleteFiles(intent);

        if (!newFiles.empty())
            std::cout << "[INTENT] 检测到 " << newFiles.size() << " 个新建文件请求\n";
        if (!deleteFiles.empty())
            std::cout << "[INTENT] 检测到 " << deleteFiles.size() << " 个删除文件请求\n";

        // ── 收集文件 ──
        auto srcFiles     = CollectSourceFiles(srcDir);     // examples/*.c
        auto harnessFiles = CollectHarnessFiles(projDir);   // src/*.cpp, include/*.h

        int totalFiles = srcFiles.size() + harnessFiles.size() + newFiles.size();
        std::cout << "[SCAN] " << srcFiles.size() << " 个被测 .c 文件 (examples/), "
                  << harnessFiles.size() << " 个 harness 文件 (src/ + include/)\n";

        std::vector<Result> results;

        // ── Step 1: 删除文件（路径相对 projDir） ──
        for (auto& relPath : deleteFiles) {
            fs::path target = projDir / relPath;
            if (fs::exists(target)) {
                std::cout << "\n━━━ 删除: " << relPath << " ━━━\n";
                fs::remove(target);
                std::cout << "  [DEL] 已删除 " << relPath << std::endl;

                Result r;
                r.file         = relPath;
                r.srcBefore    = "(已删除)";
                r.codeModified = true;
                r.codeCompiled = true;
                r.passed       = true;
                r.verdict      = "VERDICT: PASS (文件已删除)";
                r.codeReview   = "文件 " + relPath + " 已按意图删除";
                results.push_back(r);
            } else {
                std::cout << "\n━━━ 删除: " << relPath << " ━━━\n";
                std::cout << "  [DEL] 文件不存在，跳过" << std::endl;
            }
        }

        // ── Step 2: 处理被测 .c 文件（完整管线） ──
        for (size_t i = 0; i < srcFiles.size(); ++i) {
            auto fpath = srcFiles[i];
            std::cout << "\n━━━ [" << (i + 1) << "/" << totalFiles
                      << "] examples/" << fpath.filename().string() << " ━━━\n";

            Result r = ProcessSourceFile(fpath, intent, false);

            std::cout << "  ── " << (r.passed ? "PASS" : "FAIL");
            if (r.codeModified)
                std::cout << " (AI实现: " << (r.codeCompiled ? "OK" : "FAIL") << ")";
            std::cout << " ──" << std::endl;

            results.push_back(r);
        }

        // ── Step 3: 处理 harness 自身文件（仅 Phase 0） ──
        for (size_t i = 0; i < harnessFiles.size(); ++i) {
            auto fpath = harnessFiles[i];
            size_t idx = srcFiles.size() + i + 1;
            // 显示相对于 projDir 的路径
            std::string displayPath = fpath.string();
            auto projStr = projDir.string();
            if (displayPath.find(projStr) == 0)
                displayPath = displayPath.substr(projStr.size() + 1);

            std::cout << "\n━━━ [" << idx << "/" << totalFiles
                      << "] " << displayPath << " ━━━\n";

            Result r = ProcessSourceFile(fpath, intent, true);

            std::cout << "  ── " << (r.codeModified ? "已修改" : "未修改") << " ──" << std::endl;
            results.push_back(r);
        }

        // ── Step 4: 新建文件（路径相对 projDir） ──
        for (auto& [relPath, desc] : newFiles) {
            size_t idx = srcFiles.size() + harnessFiles.size() + 1;
            std::cout << "\n━━━ [" << idx << "/" << totalFiles
                      << "] " << relPath << " [新建] ━━━\n";

            Result r = GenerateNewFile(projDir, relPath, desc, intent);
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

        // ── 自演化：harness 自身文件被修改 → 重新构建并 exec ──
        if (HarnessFilesModified(results)) {
            std::cout << "\n[HARNESS] 检测到自身源码变更，启动自演化...\n";
            SelfRebuildAndExec(buildDir.string(), projDir.string(), argc, argv);
        }

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

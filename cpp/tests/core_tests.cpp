#include "brands.hpp"
#include "core.hpp"
#include "official_install.hpp"
#include "skill_category.hpp"
#include "text_scroll.hpp"
#include <iostream>
#include <thread>
using namespace ad;
int network_tests(Models &);
int brand_tests();
int model_state_tests(Store &, Models &, Adapters &, const fs::path &);
int adapter_tests(Store &, Models &, Adapters &, const fs::path &);
int discovery_tests(Store &, Models &, Adapters &, const fs::path &);
int compatibility_tests(Store &, Models &, Adapters &, const fs::path &);
int removal_tests(const fs::path &);
int desktop_install_tests();
J desktop_install_audit();
static int passed = 0;
static void check(bool value, const char *label) {
    if (!value)
        throw std::runtime_error(std::string("FAIL: ") + label);
    ++passed;
}
template <class Fn> static void rejects(Fn f, const char *label) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, label);
}
static const std::string CONTENT =
    "---\nname: test-skill\ndescription: \"测试中文搜索与代码审查的可复用工作流程\"\n---\n\n# Test "
    "Skill\n\n1. Read supplied input.\n2. Verify and report the result.\n";
int main(int argc, char **argv) {
    bool fixtures = argc == 1 || (argc > 1 && (std::string(argv[1]) == "--verify-official-install" ||
                                               std::string(argv[1]) == "--verify-claude-repair" ||
                                               std::string(argv[1]) == "--verify-qwen-install"));
    SetEnvironmentVariableW(L"AGENTDOCK_TEST_SCOPES", fixtures ? L"1" : nullptr);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc == 2 && std::string(argv[1]) == "--audit-desktop-detection") {
        std::cout << desktop_install_audit().dump(2) << std::endl;
        CoUninitialize();
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--verify-qwen-install") {
        try {
            auto base = fs::absolute(path(argv[2])).lexically_normal();
            if (!inside(base, fs::current_path() / ".development") || fs::exists(base))
                throw std::runtime_error("Use a new project .development directory");
            Store st(base / "data");
            Models models(st);
            Adapters adapters(st, models);
            auto target = fs::absolute(st.root / "installation-tests/qwen");
            auto plan = adapters.installation_plan("qwen", false, target);
            auto result = adapters.manage_installation("qwen", false, target);
            auto cli = target / "runtime/qwen.cmd";
            adapters.locate("qwen", cli);
            run_install_plan(
                {{"tool", pathstr(cli)}, {"args", J::array({"--version"})}, {"env", plan["env"]}},
                base / "version", adapters.cancel_install);
            auto launch = adapters.launch("qwen", "", false, {"--version"});
            atomic_write(base / "verification.json", J{{"platform", windows_platform()},
                                                       {"compatibility", agent_compatibility("qwen", cli)},
                                                       {"launch", launch},
                                                       {"result", result},
                                                       {"executable", pathstr(cli)}}
                                                         .dump(2));
            adapters.manage_installation("qwen", true, target);
            if (fs::exists(cli))
                throw std::runtime_error("Project Qwen cleanup failed");
            std::cout << "PASS official Qwen install/version/manager launch/uninstall in project\n";
            CoUninitialize();
            return 0;
        } catch (const std::exception &e) {
            std::cerr << e.what() << "\n";
            CoUninitialize();
            return 1;
        }
    }
    if (argc == 3 && (std::string(argv[1]) == "--verify-claude-repair" ||
                      std::string(argv[1]) == "--verify-claude-install")) {
        try {
            auto base = fs::absolute(path(argv[2])).lexically_normal();
            if (!inside(base, fs::current_path() / ".development") || fs::exists(base))
                throw std::runtime_error("Use a new project .development directory");
            Store st(base / "data");
            Models models(st);
            Adapters adapters(st, models);
            J protectedFiles = J::array();
            for (auto id : {"codex", "harness"}) {
                auto exe = adapters.executable(id);
                if (!exe.empty())
                    protectedFiles.push_back(
                        {{"path", pathstr(exe)}, {"sha256", sha(read(exe, 1024ull * 1024 * 1024))}});
            }
            auto target = fs::absolute(st.root / "installation-tests/claude");
            if (std::string(argv[1]) == "--verify-claude-repair") {
                auto broken = adapters.installation_plan("claude", false, target);
                broken["args"].push_back("--ignore-scripts=true");
                // Omit the native optional package to reproduce the official documented failure.
                auto &args = broken["args"];
                args.erase(std::remove(args.begin(), args.end(), J("--include=optional")), args.end());
                args.push_back("--omit=optional");
                args.push_back("--fetch-retries=1");
                args.push_back("--fetch-timeout=30000");
                broken["timeout_ms"] = 180000;
                fs::create_directories(target / "temp");
                run_install_plan(broken, base / "reproduce", adapters.cancel_install);
                auto health = agent_compatibility("claude", target / "runtime/claude.cmd");
                if (health.value("compatible", true) || health.value("code", "") != "invalid-pe")
                    throw std::runtime_error("Expected Windows-incompatible npm placeholder");
                atomic_write(base / "broken-health.json", health.dump(2));
                adapters.manage_installation("claude", false, target);
                auto healthy = agent_compatibility("claude", target / "runtime/claude.cmd");
                if (!healthy.value("compatible", false))
                    throw std::runtime_error(healthy.dump());
                atomic_write(base / "repaired-health.json", healthy.dump(2));
                adapters.locate("claude", target / "runtime/claude.cmd");
                adapters.launch("claude", "", false, {"--version"});
                adapters.manage_installation("claude", true, target);
            } else {
                std::cout << adapters.installation_plan("claude").dump(2) << std::endl;
                auto result = adapters.manage_installation("claude");
                auto exe = adapters.executable("claude");
                auto health = agent_compatibility("claude", exe);
                if (!health.value("compatible", false))
                    throw std::runtime_error(health.dump());
                // Use the manager's actual launch code, not just the installer invocation.
                adapters.launch("claude", "", false, {"--version"});
                atomic_write(base / "installed-health.json", J{{"platform", windows_platform()},
                                                               {"health", health},
                                                               {"result", result},
                                                               {"executable", pathstr(exe)}}
                                                                 .dump(2));
            }
            for (auto &file : protectedFiles)
                if (sha(read(path(file["path"]), 1024ull * 1024 * 1024)) != file["sha256"].get<std::string>())
                    throw std::runtime_error("Protected host agent changed");
            atomic_write(base / "protected-agents.json", protectedFiles.dump(2));
            std::cout << "PASS Claude install/launch compatibility; host Codex and Harness unchanged\n";
            CoUninitialize();
            return 0;
        } catch (const std::exception &e) {
            std::cerr << e.what() << "\n";
            CoUninitialize();
            return 1;
        }
    }
    if (argc == 3 && std::string(argv[1]) == "--verify-official-install") {
        int result = 0;
        try {
            auto base = fs::absolute(path(argv[2])).lexically_normal();
            if (!inside(base, fs::current_path() / ".development") ||
                (fs::exists(base) &&
                 !fs::is_regular_file(base / "data/installation-tests/codex/.agentdock-install-test.json")))
                throw std::runtime_error("Use a new or previously owned project .development test directory");
            auto hostCodex = find_exe("codex"), hostHarness = find_exe("dsh");
            auto codexHash = sha(read(hostCodex, 512 * 1024 * 1024)), harnessHash = sha(read(hostHarness));
            Store st(base / "data");
            Models models(st);
            Adapters adapters(st, models);
            auto target = fs::absolute(st.root / "installation-tests/codex").lexically_normal();
            auto plan = adapters.installation_plan("codex", false, target);
            std::cout << plan.dump(2) << "\n";
            adapters.manage_installation("codex", false, target);
            auto cli = target / "runtime/codex.cmd";
            if (!fs::is_regular_file(cli))
                throw std::runtime_error("Project CLI missing");
            run_install_plan({{"tool", pathstr(cli)},
                              {"args", J::array({"--version"})},
                              {"method", "test-version"},
                              {"env", plan["env"]}},
                             base / "version", adapters.cancel_install);
            adapters.manage_installation("codex", true, target);
            if (fs::exists(cli) || sha(read(hostCodex, 512 * 1024 * 1024)) != codexHash ||
                sha(read(hostHarness)) != harnessHash)
                throw std::runtime_error("Host isolation check failed");
            auto clean = plan;
            clean["args"] = J::array({"cache", "clean", "--force", "--cache", pathstr(target / "cache")});
            run_install_plan(clean, base / "cache-clean", adapters.cancel_install);
            std::cout << "PASS official npm install/version/uninstall in project; host Codex and Harness "
                         "unchanged\n";
        } catch (const std::exception &e) {
            std::cerr << e.what() << "\n";
            result = 1;
        }
        CoUninitialize();
        return result;
    }
    if (argc == 3 &&
        (std::string(argv[1]) == "--verify-managed" || std::string(argv[1]) == "--verify-managed-npm")) {
        int result = 0;
        try {
            auto base = fs::absolute(path(argv[2])).lexically_normal();
            if (!inside(base, fs::current_path() / ".development") || fs::exists(base))
                throw std::runtime_error("Use a new directory under this project's .development");
            Store st(base / "data");
            Models models(st);
            Adapters adapters(st, models);
            auto original = find_exe("codex");
            if (original.empty())
                throw std::runtime_error("No host Codex to compare");
            auto before = sha(read(original, 512 * 1024 * 1024));
            if (std::string(argv[1]) == "--verify-managed-npm")
                adapters.install("codex");
            else {
                fs::create_directories(base / "source");
                fs::copy_file(original, base / "source/codex.exe");
                adapters.install("codex", base / "source", "codex.exe");
            }
            if (!inside(adapters.executable("codex"), base / "data/agents/codex"))
                throw std::runtime_error("Managed executable escaped project test");
            std::cout << adapters.launch("codex", "", false, {"--version"}) << "\n";
            auto installed = adapters.installation("codex");
            std::cout << installed.dump(2) << "\n";
            adapters.uninstall("codex");
            if (fs::exists(base / "data/agents/codex") || !fs::exists(original) ||
                sha(read(original, 512 * 1024 * 1024)) != before)
                throw std::runtime_error("Uninstall isolation failed");
            std::cout << "PASS project-only install, launch, uninstall; host Codex unchanged\n";
        } catch (const std::exception &e) {
            std::cerr << e.what() << "\n";
            result = 1;
        }
        CoUninitialize();
        return result;
    }
    if (argc == 2 && std::string(argv[1]) == "--verify-installed-cli") {
        auto root = fs::temp_directory_path() / wide("agentdock-launch-" + uuid());
        int result = 0;
        try {
            Store st(root);
            Models models(st);
            Adapters adapters(st, models);
            for (auto id : {"codex", "harness"}) {
                std::cout << id << ": " << pathstr(adapters.executable(id)) << "\n";
                std::cout << adapters.launch(id, "", false, {"--version"}) << "\n";
            }
        } catch (const std::exception &e) {
            std::cerr << e.what() << "\n";
            result = 1;
        }
        if (inside(root, fs::temp_directory_path()))
            fs::remove_all(root);
        CoUninitialize();
        return result;
    }
    if (argc == 3 &&
        (std::string(argv[1]) == "--prepare-ui" || std::string(argv[1]) == "--prepare-detection-ui" ||
         std::string(argv[1]) == "--prepare-compat-ui" || std::string(argv[1]) == "--prepare-removal-ui")) {
        auto p = path(argv[2]);
        Store st(p / "data");
        st.setting("ui_fixture", true);
        fs::create_directories(p / "project");
        auto text = CONTENT;
        auto pos = text.find("test-skill");
        text.replace(pos, 10, "direct-ready");
        atomic_write(p / "source/direct-ready/SKILL.md", text);
        atomic_write(p / "source/direct-ready/references/example.txt", "QA attachment, no commands.");
        std::string longText =
            "---\nname: long-scroll\ndescription: 检查长文档的滚轮和滑块拖动\n---\n# Long scroll\n";
        for (int i = 1; i <= 300; ++i)
            longText += "ROW_" + std::to_string(i) + " · 检查长文本滚动，保留当前阅读位置。\n";
        atomic_write(p / "source/long-scroll/SKILL.md", longText);
        st.scan({{"UI fixture", p / "source"}});
        auto rows = st.list("code-review");
        if (!rows["items"].empty())
            st.toggle(rows["items"][0]["id"], "favorite", true);
        st.setting("project", pathstr(p / "project"));
        st.setting("deploy_agent", "cursor");
        st.setting("deploy_scope", "project");
        if (std::string(argv[1]) == "--prepare-removal-ui") {
            auto cli = p / "mock-home/AppData/Local/cursor-agent";
            atomic_write(cli / "agent.cmd",
                         "@echo off\r\nrem AGENTDOCK_SYNTHETIC_TEST_FIXTURE\r\nexit /b 0\r\n");
            atomic_write(cli / "cursor-agent.cmd", "@echo off\r\nrem AGENTDOCK_SYNTHETIC_TEST_FIXTURE\r\nrem "
                                                   "cursor-agent versions\r\nexit /b 0\r\n");
            atomic_write(cli / "versions/1.0.0/payload.bin", "UI fixture; never execute");
            st.setting("agent_location:cursor", {{"cli", pathstr(cli / "agent.cmd")}, {"manual_cli", true}});
            auto extension = p / "mock-home/.vscode/extensions/rooveterinaryinc.roo-cline-3.1.0";
            atomic_write(
                extension / "package.json",
                R"({"publisher":"RooVeterinaryInc","name":"roo-cline","main":"dist/extension.js","agentdockSyntheticFixture":true})");
            atomic_write(extension / "dist/extension.js", "// UI fixture");
            st.setting("agent_location:roo", {{"extension", pathstr(extension)}, {"manual_extension", true}});
        }
        if (std::string(argv[1]) == "--prepare-compat-ui") {
            auto cli = p / "broken-cli/claude.exe";
            atomic_write(cli, "#!/bin/sh\necho native binary not installed\n");
            st.setting("agent_location:claude", {{"cli", pathstr(cli)}, {"manual_cli", true}});
        }
        if (std::string(argv[1]) == "--prepare-detection-ui") {
            wchar_t own[32768]{};
            GetModuleFileNameW(nullptr, own, 32768);
            auto app = p / path("custom-drive/自定义安装/Cursor/Cursor.exe");
            fs::create_directories(app.parent_path());
            fs::copy_file(fs::path(own).parent_path() / "discovery_fixture.exe", app,
                          fs::copy_options::overwrite_existing);
            atomic_write(app.parent_path() / "resources/app/product.json",
                         R"({"nameShort":"Cursor","applicationName":"cursor"})");
            st.setting("agent_location:cursor", {{"desktop", pathstr(app)}, {"manual_desktop", true}});
            for (int i = 0; i < 3000; ++i)
                atomic_write(st.root / "performance-fixture" / (std::to_string(i) + ".txt"),
                             "local navigation fixture");
        }
        CoUninitialize();
        return 0;
    }
    auto root = fs::temp_directory_path() / wide("agentdock-native-test-" + uuid());
    int rc = 0;
    try {
        fs::create_directories(root);
        SetEnvironmentVariableW(L"CODEX_HOME", (root / "home/.codex").c_str());
        SetEnvironmentVariableW(L"CLAUDE_CONFIG_DIR", (root / "home/.claude").c_str());
        SetEnvironmentVariableW(L"DSH_HOME", (root / "home/.dsh").c_str());
        for (auto name :
             {L"CLINE_DIR", L"CLINE_DATA_DIR", L"CURSOR_CONFIG_DIR", L"OPENCODE_CONFIG", L"XDG_CONFIG_HOME",
              L"CRUSH_GLOBAL_CONFIG", L"COPILOT_HOME", L"COPILOT_PROVIDERS_CONFIG"})
            SetEnvironmentVariableW(name, nullptr);
        {
            Store st(root / "data");
            Models models(st);
            Adapters adapters(st, models, root / "home");
            atomic_write(root / "native-warning.cmd",
                         "@echo off\r\necho harmless notice 1>&2\r\nexit /b 0\r\n");
            run_install_plan({{"tool", pathstr(root / "native-warning.cmd")},
                              {"args", J::array()},
                              {"method", "fixture"},
                              {"env", J::object()}},
                             root / "warning-job", adapters.cancel_install);
            check(fs::is_regular_file(root / "warning-job/output.log"),
                  "native stderr notice does not turn successful installer into failure");
            atomic_write(root / "native-failure.cmd", "@echo off\r\nexit /b 7\r\n");
            rejects(
                [&] {
                    run_install_plan({{"tool", pathstr(root / "native-failure.cmd")},
                                      {"args", J::array()},
                                      {"method", "fixture"},
                                      {"env", J::object()}},
                                     root / "failure-job", adapters.cancel_install);
                },
                "official installer nonzero exit remains a failure");
            // Detached launcher + long-lived child: cancellation must stop the child as well.
            atomic_write(root / "cancel-child.ps1",
                         "[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'child.pid'),[string]$PID)\n"
                         "Start-Sleep -Seconds 30\n"
                         "[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'unexpected-finish'),'bad')\n");
            atomic_write(root / "cancel-launcher.cmd",
                         "@echo off\r\nstart \"\" /b powershell.exe -NoLogo -NoProfile "
                         "-ExecutionPolicy Bypass -File \"%~dp0cancel-child.ps1\"\r\nexit /b 0\r\n");
            bool cancelled = false;
            ULONGLONG requested = 0;
            std::thread canceller([&] {
                auto start = GetTickCount64();
                while (!fs::is_regular_file(root / "child.pid") && GetTickCount64() - start < 8000)
                    Sleep(20);
                requested = GetTickCount64();
                adapters.cancel_install = true;
            });
            try {
                run_install_plan({{"tool", pathstr(root / "cancel-launcher.cmd")},
                                  {"args", J::array()},
                                  {"method", "fixture"},
                                  {"env", J::object()}},
                                 root / "cancel-job", adapters.cancel_install);
            } catch (const InstallationCancelled &) {
                cancelled = true;
            } catch (...) {
                canceller.join();
                throw;
            }
            canceller.join();
            check(cancelled && GetTickCount64() - requested < 2000, "installation responds to cancel");
            check(fs::is_regular_file(root / "child.pid"), "child actually started before cancellation");
            auto childPid = (DWORD)std::stoul(read(root / "child.pid"));
            auto child = OpenProcess(SYNCHRONIZE, FALSE, childPid);
            check(!child || WaitForSingleObject(child, 0) == WAIT_OBJECT_0,
                  "cancellation waits until owned child exits");
            if (child)
                CloseHandle(child);
            check(!fs::exists(root / "unexpected-finish"), "cancelled child cannot finish installation");
            cancelled = false;
            try {
                run_install_plan({{"tool", pathstr(root / "native-warning.cmd")},
                                  {"args", J::array()},
                                  {"method", "fixture"},
                                  {"env", J::object()}},
                                 root / "pre-cancel-job", adapters.cancel_install);
            } catch (const InstallationCancelled &) {
                cancelled = true;
            }
            check(cancelled && !fs::exists(root / "pre-cancel-job"),
                  "early cancellation cannot be reset or start a process");
            adapters.cancel_install = false;
            auto npmFixture = root / "npm-shim";
            atomic_write(
                npmFixture / "gemini.cmd",
                "@echo off\r\nnode \"%~dp0node_modules\\@google\\gemini-cli\\dist\\index.js\" %*\r\n");
            check(!valid_agent_executable("gemini", npmFixture / "gemini.cmd"),
                  "orphaned npm shim is not a usable CLI");
            atomic_write(npmFixture / "node_modules/@google/gemini-cli/package.json",
                         R"({"name":"@google/gemini-cli","bin":{"gemini":"dist/index.js"}})");
            check(!valid_agent_executable("gemini", npmFixture / "gemini.cmd"),
                  "npm manifest without executable payload is not an installation");
            atomic_write(npmFixture / "node_modules/@google/gemini-cli/dist/index.js", "// fixture");
            check(valid_agent_executable("gemini", npmFixture / "gemini.cmd"),
                  "complete npm shim and payload are accepted");
            check(valid_agent_executable("gemini", root / "native-warning.cmd"),
                  "independent custom launch scripts remain supported");
            check(
                find_exe(pathstr(npmFixture / "gemini.cmd"), [](const fs::path &) { return false; }).empty(),
                "CLI search honors candidate validation");
            for (auto &a : adapters.catalog)
                if (a["id"] == "gemini")
                    a["cli"] = "agentdock-no-such-gemini-fixture";
            atomic_write(root / "home/.gemini/settings.json", "{}");
            atomic_write(root / "home/.gemini/skills/retained/SKILL.md", CONTENT);
            for (auto &a : adapters.list())
                if (a["id"] == "gemini") {
                    check(!a["detected"].get<bool>() && !a["installation"]["installed"].get<bool>(),
                          "retained Gemini settings and skills never count as installed");
                    check(a["retained_data"].get<bool>() && a["status"] == "未安装 · 保留配置",
                          "retained data is explicitly distinguished from installation");
                }
            check(fs::is_regular_file(root / "home/.gemini/skills/retained/SKILL.md"),
                  "presence detection preserves user skills");
            for (auto &a : adapters.catalog)
                if (a["id"] == "gemini")
                    a["cli"] = "gemini";
            for (auto id : {"codex", "harness", "gemini", "qwen", "cline", "codebuddy", "copilot", "amp",
                            "opencode", "crush", "factory"}) {
                auto plan = adapters.installation_plan(id);
                check(plan["args"][0] == "install" && plan["args"][1] == "--global",
                      "official npm installation uses global defaults");
                check(plan["args"].dump().find("--prefix") == std::string::npos &&
                          plan["args"].dump().find("--location") == std::string::npos,
                      "production installation has no custom directory");
            }
            auto target = fs::absolute(st.root / "installation-tests/codex").lexically_normal();
            auto isolated = adapters.installation_plan("codex", false, target);
            check(isolated["args"].dump().find("--prefix") != std::string::npos,
                  "test installer redirects only into project prefix");
            rejects([&] { adapters.installation_plan("codex", true, target); },
                    "test uninstall requires explicit test ownership marker");
            rejects([&] { adapters.installation_plan("harness", true, root / "host-like-folder"); },
                    "test uninstall cannot escape dedicated test root");
            rejects(
                [&] {
                    adapters.installation_plan("workbuddy", true,
                                               fs::absolute(st.root / "installation-tests/workbuddy"));
                },
                "isolated tests cannot execute native host uninstaller");
            TextScroll metric{300, 20, 0};
            check(metric.maximum() == 280 && metric.fromPosition(10000, 400) == 280,
                  "long text thumb reaches bottom");
            check(metric.thumb(400) == 400.f * 20 / 300 && metric.fromPosition(-50, 400) == 0,
                  "thumb ratio and upper clamp");
            metric.first = 140;
            check(std::abs(metric.position(400) - (400 - metric.thumb(400)) / 2) < .01f,
                  "thumb tracks viewport");
            check(TextScroll{3, 20, 0}.maximum() == 0 && TextScroll{3, 20, 0}.thumb(400) == 400,
                  "short editor has no scrolling");
            check(TextScroll{100000, 20, 0}.thumb(400) == 24, "huge skill keeps draggable minimum thumb");
            check(classify_skill("csv-analysis", "") == "数据分析" &&
                      classify_skill("security-audit", "") == "安全审查",
                  "skill auto classification");
            check(classify_skill("code-review", "", "自定义分类") == "自定义分类",
                  "manual categories preserved");
            check(display_path(path("C:/test/mixed\\path")).find('/') == std::string::npos,
                  "Windows path separator consistency");
            auto portable = root / "portable-source";
            atomic_write(portable / "bin/tool.cmd", "@echo off\r\necho fixture\r\n");
            atomic_write(portable / "resources/data.txt", "preserve source");
            rejects([&] { adapters.uninstall("workbuddy"); }, "external installations cannot be uninstalled");
            rejects([&] { adapters.install("workbuddy", portable, "../outside.cmd"); },
                    "portable entry traversal refused");
            auto installed = adapters.install("workbuddy", portable, "bin/tool.cmd");
            check(installed["ready"] == true &&
                      inside(adapters.executable("workbuddy"), adapters.managed_root("workbuddy")),
                  "portable installation selects owned executable");
            check(read(adapters.managed_root("workbuddy") / "portable/resources/data.txt") ==
                      "preserve source",
                  "portable dependencies copied");
            rejects([&] { adapters.install("workbuddy", portable, "bin/tool.cmd"); },
                    "existing managed install is not overwritten");
            adapters.uninstall("workbuddy");
            check(!fs::exists(adapters.managed_root("workbuddy")) &&
                      read(portable / "resources/data.txt") == "preserve source",
                  "uninstall removes only owned copy");
            fs::create_directories(adapters.managed_root("workbuddy"));
            atomic_write(adapters.managed_root("workbuddy") / "user-file.txt", "untouched");
            rejects([&] { adapters.uninstall("workbuddy"); }, "unmarked installation directories protected");
            check(read(adapters.managed_root("workbuddy") / "user-file.txt") == "untouched",
                  "unmarked directory retained");
            check(resource(brand_resources.at("claude")).find("#D97757") != std::string::npos,
                  "packaged brand color");
            check(resource(brand_resources.at("workbuddy")).find("viewBox=\"0 0 40 40\"") !=
                      std::string::npos,
                  "packaged official WorkBuddy mark");
            check(st.list()["total"] == 4, "four templates");
            check(metadata(CONTENT)["name"] == "test-skill", "frontmatter");
            auto sparse = skill_quality(CONTENT);
            check(sparse["valid"] == true && !sparse["suggestions"].empty(),
                  "valid sparse skill has actionable quality suggestions");
            check(skill_quality("not a skill")["valid"] == false, "quality report identifies invalid format");
            check(metadata(CONTENT)["description"] == "测试中文搜索与代码审查的可复用工作流程",
                  "quality checks do not rewrite metadata");
            rejects([] { metadata("# no header"); }, "missing frontmatter");
            rejects(
                [] {
                    metadata("---\nname: ../escape\ndescription: test\n---\nThis should never be saved or "
                             "deployed.");
                },
                "traversal name");
            rejects(
                [] {
                    metadata("---\nname: invalid--name\ndescription: test\n---\nThis should never be saved "
                             "or deployed.");
                },
                "consecutive hyphens");
            rejects(
                [] {
                    metadata("---\nname: test\nname: test2\ndescription: test\n---\nThis should never be "
                             "saved or deployed.");
                },
                "duplicate frontmatter");
            check(metadata("---\nname: folded\ndescription: >\n  A reusable workflow\n  for "
                           "analysis.\n---\nRead supplied material and report evidence.")["description"] ==
                      "A reusable workflow for analysis.",
                  "folded scalar");
            auto sk = st.save({{"content", CONTENT}});
            st.save({{"content", CONTENT}});
            check(st.list("中文搜索")["total"] == 2, "Chinese trigram search");
            check(st.list("中文")["total"] == 2, "Chinese short search");
            check(st.list("%_")["total"] == 0, "literal LIKE");
            check(st.list("\" OR x")["total"] == 0, "FTS quoting");
            check(st.stats()["saved_bytes"].get<int64_t>() > 0, "dedup");
            st.save({{"id", sk["id"]}, {"content", CONTENT + "\nAnother revision.\n"}});
            check(st.skill(sk["id"])["revisions"].size() == 2, "revisions");
            check(st.revision(sk["id"], sha(CONTENT)) == CONTENT, "revision retrieval");
            auto ext = root / "external/sample";
            atomic_write(ext / "SKILL.md", CONTENT);
            atomic_write(ext / "references/spec.txt", "Keep resource");
            check(st.scan({{"Test", root / "external"}})["changed"] == 1, "scan discovery");
            check(st.scan({{"Test", root / "external"}})["changed"] == 0, "incremental scan");
            auto external = st.list("", "external")["items"][0];
            rejects([&] { st.save({{"id", external["id"]}, {"content", CONTENT}}); }, "external read only");
            check(read(ext / "SKILL.md") == CONTENT, "external unchanged");
            auto externalId = external["id"].get<std::string>();
            auto preflight = st.deployment_check(externalId, "cursor_user", root / "direct-target");
            check(preflight["files"] == 2 && !preflight["already"].get<bool>() &&
                      !fs::exists(root / "direct-target"),
                  "deployment preflight is read only");
            auto direct = st.deploy(externalId, "cursor_user", root / "direct-target");
            check(read(direct / "references/spec.txt") == "Keep resource",
                  "direct external deployment retains attachments");
            check(st.list("", "managed")["total"] == 6, "direct deployment needs no import");
            st.toggle_deployment(externalId, "cursor_user", false);
            check(!fs::exists(direct) && read(ext / "SKILL.md") == CONTENT,
                  "pause only deployed external copy");
            st.toggle_deployment(externalId, "cursor_user", true);
            check(fs::exists(direct), "restore external deployed copy");
            check(!st.deployments().empty(), "deployment record listing");
            atomic_write(direct / "SKILL.md", "external target change");
            rejects([&] { st.toggle_deployment(externalId, "cursor_user", false); },
                    "per-copy edit conflict protection");
            check(st.skill(externalId)["managed"] == 0, "external origin remains external");
            auto imported = st.import_skill(ext);
            auto id = imported["id"].get<std::string>();
            rejects([&] { st.deployment_check(externalId, "cursor_user", root / "direct-target"); },
                    "preflight detects external target edits");
            auto dest = st.deploy(id, "codex_user", root / "target");
            check(read(dest / "references/spec.txt") == "Keep resource", "attachment deployment");
            st.toggle(id, "enabled", false);
            check(!fs::exists(dest), "disable deployment");
            st.toggle(id, "enabled", true);
            check(fs::exists(dest), "restore deployment");
            atomic_write(dest / "SKILL.md", "external edits");
            rejects([&] { st.deploy(id, "codex_user", root / "target"); }, "deployment conflict");
            rejects([&] { st.toggle(id, "enabled", false); }, "disable conflict");
            check(read(dest / "SKILL.md") == "external edits", "external edits retained");
            auto collision = root / "collision/test-skill";
            atomic_write(collision / "SKILL.md", "original");
            rejects([&] { st.deploy(id, "other_user", collision.parent_path()); }, "unmanaged collision");
            check(read(collision / "SKILL.md") == "original", "unmanaged retained");
            st.export_zip(id, root / "export.zip");
            auto zip = read(root / "export.zip");
            check(zip.substr(0, 4) == "PK\x03\x04", "ZIP signature");
            check(zip.find("test-skill/references/spec.txt") != std::string::npos, "ZIP resources");
            check(zip.substr(zip.size() - 22, 4) == "PK\x05\x06", "ZIP central record");
            auto invalid = root / "invalid";
            atomic_write(invalid / "SKILL.md", CONTENT);
            atomic_write(invalid / "huge.txt", std::string(20 * 1024 * 1024, 'x'));
            rejects([&] { st.import_skill(invalid); }, "bundle limit");
            auto enc = protect("fake-unit-key");
            check(enc != "fake-unit-key" && protect(enc, true) == "fake-unit-key", "DPAPI roundtrip");
            rejects([] { endpoint("http://example.com/v1"); }, "remote HTTP");
            rejects([] { endpoint("https://u:p@example.com/v1"); }, "URL credentials");
            rejects([] { endpoint("https://example.com/v1?x=1"); }, "URL query");
            rejects([] { endpoint("https://example.com/v1#x"); }, "URL fragment");
            check(endpoint("http://localhost:8000/v1/") == "http://localhost:8000/v1", "endpoint normalize");
            J data = {{"name", "Test"},
                      {"base_url", "https://example.com/v1"},
                      {"protocol", "responses"},
                      {"model", "test-model"},
                      {"key_env", "TEST_NATIVE_API_KEY"},
                      {"api_key", "fake-unit-key"}};
            auto provider = models.save(data);
            auto pid = provider["id"].get<std::string>();
            auto copy = models.duplicate(pid);
            auto [copied, copiedKey] = models.get(copy["id"]);
            check(copy["id"] != pid && copied["base_url"] == provider["base_url"] &&
                      copied["model"] == provider["model"],
                  "duplicate preserves model configuration");
            check(copiedKey.empty() && copied["key_env"] == "" && copied["status"] == "untested",
                  "duplicate does not copy credentials or success status");
            check(models.get(pid).first["name"] == provider["name"], "duplicate preserves source connection");
            models.remove(copy["id"]);
            check(models.get(pid).second == "fake-unit-key", "encrypted key read");
            check(models.list().dump().find("fake-unit-key") == std::string::npos, "no public plaintext");
            provider["base_url"] = "https://another.example/v1";
            models.save(provider);
            check(models.get(pid).second.empty(), "clear key on host change");
            provider["base_url"] = "https://example.com/v1";
            models.save(provider);
            check(adapters.target("harness") == root / "home/.dsh/skills", "DSH_HOME");
            check(adapters.configs("harness")[0] == root / "home/.dsh/profiles/web/cordis.patch.yml",
                  "Harness web profile");
            rejects([&] { adapters.target("workbuddy"); }, "WorkBuddy no guessed CodeBuddy directory");
            check(adapters.target("codebuddy") == root / "home/.codebuddy/skills", "CodeBuddy separate");
            check(adapters.catalog.size() == 21, "21 tools");
            std::string original =
                "# Keep comment\nmodel = \"before\" # inline comment\nmodel_provider = "
                "\"before_provider\"\n\n[projects.\"C:/工作\"]\ntrust_level = \"trusted\"\n";
            auto config = adapters.configs("codex")[0];
            atomic_write(config, original);
            auto preview = adapters.preview("codex", pid);
            check(preview["content"].get<std::string>().find("fake-unit-key") == std::string::npos,
                  "redacted preview");
            adapters.apply(preview["ticket"]);
            auto merged = read(config);
            check(merged.find("# Keep comment") != std::string::npos &&
                      merged.find("# inline comment") != std::string::npos &&
                      merged.find("trust_level = \"trusted\"") != std::string::npos,
                  "TOML preservation");
            check(merged.find("wire_api = \"responses\"") != std::string::npos, "Codex Responses");
            auto configBackup = [&] {
                for (auto &b : adapters.backups())
                    if (b["path"] == pathstr(config))
                        return b["id"].get<std::string>();
                throw std::runtime_error("missing config backup");
            };
            auto backup = configBackup();
            auto repeat = adapters.preview("codex", pid);
            auto backupCount = adapters.backups().size();
            adapters.apply(repeat["ticket"]);
            check(read(config) == merged, "idempotent merge");
            check(adapters.backups().size() == backupCount,
                  "unchanged configuration does not duplicate backups");
            adapters.restore(backup);
            check(read(config) == original, "exact backup restore");
            auto stale = adapters.preview("codex", pid);
            atomic_write(config, original + "# concurrent edit\n");
            rejects([&] { adapters.apply(stale["ticket"]); }, "stale preview");
            check(read(config).find("concurrent edit") != std::string::npos, "concurrent edit preserved");
            std::string fragment;
            auto unicode =
                Adapters::merge_codex("# 中文\nmodel = \"旧模型\" # 注释\n", provider, "KEY", fragment);
            check(unicode.find("# 注释") != std::string::npos, "TOML Unicode offsets");
            rejects([&] { Adapters::merge_codex("model = [broken", provider, "KEY", fragment); },
                    "invalid TOML");
            auto harness = adapters.preview("harness", pid);
            check(harness["can_apply"].get<bool>() &&
                      harness["content"].get<std::string>().find("responses") != std::string::npos,
                  "Harness native route preview");
            adapters.apply(harness["ticket"]);
            check(!adapters.model_state("harness")["models"].empty(), "Harness native route applied");
            rejects([&] { adapters.preview("claude", pid); }, "Agent protocol mismatch");
            auto anthropic = provider;
            anthropic.erase("id");
            anthropic["protocol"] = "anthropic";
            auto cp = models.save(anthropic);
            auto cc = adapters.configs("claude")[0];
            atomic_write(
                cc, J{{"permissions", {{"deny", J::array({"Read(.env)"})}}}, {"env", {{"SOMETHING", "keep"}}}}
                        .dump());
            auto ca = adapters.preview("claude", cp["id"]);
            adapters.apply(ca["ticket"]);
            auto claude = J::parse(read(cc));
            check(claude["env"]["SOMETHING"] == "keep" && claude["permissions"]["deny"][0] == "Read(.env)",
                  "Claude preserve settings");
            check(claude["env"]["ANTHROPIC_BASE_URL"] == "https://example.com", "Claude base URL");
            auto body = Models::payload(provider, "sys", "prompt", 128);
            check(body["store"] == false && body["max_output_tokens"] == 128, "Responses payload");
            check(Models::payload(cp, "sys", "prompt", 128)["system"] == "sys", "Messages payload");
            auto chat = provider;
            chat["protocol"] = "chat";
            chat["token_field"] = "max_completion_tokens";
            check(Models::payload(chat, "s", "p", 128).contains("max_completion_tokens"),
                  "output cap compatibility");
            check(Models::extract({{"output", J::array({{{"content", J::array({{{"type", "output_text"},
                                                                                {"text", "result"}}})}}})}},
                                  "responses") == "result",
                  "Responses text");
            check(Models::extract({{"content", J::array({{{"type", "text"}, {"text", "result"}}})}},
                                  "anthropic") == "result",
                  "Anthropic text");
            rejects([] { Models::extract(J::object(), "chat"); }, "empty response error");
            rejects([&] { models.generate(pid, "bad"); }, "prompt limit");
            auto catalog = resource_json(100);
            check(catalog["presets"].size() == 31, "31 provider presets");
            for (auto &p : catalog["presets"])
                if (!p["base_url"].get<std::string>().empty())
                    endpoint(p["base_url"]);
            passed += discovery_tests(st, models, adapters, root);
            passed += compatibility_tests(st, models, adapters, root);
            passed += removal_tests(root);
            passed += desktop_install_tests();
            passed += model_state_tests(st, models, adapters, root);
            passed += adapter_tests(st, models, adapters, root);
            passed += network_tests(models);
            passed += brand_tests();
            std::cout << "PASS " << passed << " native assertions\n";
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nEvidence: " << pathstr(root) << "\n";
        rc = 1;
    }
    if (!rc && inside(root, fs::temp_directory_path()))
        fs::remove_all(root);
    CoUninitialize();
    return rc;
}

#include "official_install.hpp"
#include "discovery.hpp"
#include "removal.hpp"
#include <algorithm>
#include <cwchar>
#include <fstream>
#include <set>
#include <shellapi.h>
namespace ad {
bool registration_matches_executable(const J &record, const fs::path &exe) {
    if (exe.empty())
        return true;
    auto paths = record.value("paths", J::object());
    auto desktop = path(paths.value("desktop", "")), command = path(paths.value("cli", ""));
    auto name = wide(record.value("name", ""));
    std::transform(name.begin(), name.end(), name.begin(), towlower);
    if (record.value("id", "") == "cursor" && !desktop.empty() && name.find(L"cli") == std::wstring::npos)
        return false;
    if (!command.empty() && _wcsicmp(command.c_str(), exe.c_str()) == 0)
        return true;
    auto root = path(record.value("location", ""));
    if (root.is_absolute() && inside(exe, root))
        return true;
    return !desktop.empty() && inside(exe, desktop.parent_path()) &&
           _wcsicmp(desktop.c_str(), exe.c_str()) != 0;
}
namespace {
bool samePath(fs::path a, fs::path b) {
    a = a.lexically_normal().make_preferred();
    b = b.lexically_normal().make_preferred();
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
const std::map<std::string, std::string> npmPackages = {{"codex", "@openai/codex"},
                                                        {"claude", "@anthropic-ai/claude-code"},
                                                        {"harness", "@deepseek-ai/dsh"},
                                                        {"gemini", "@google/gemini-cli"},
                                                        {"qwen", "@qwen-code/qwen-code"},
                                                        {"cline", "cline"},
                                                        {"codebuddy", "@tencent-ai/codebuddy-code"},
                                                        {"copilot", "@github/copilot"},
                                                        {"amp", "@ampcode/cli"},
                                                        {"opencode", "opencode-ai"},
                                                        {"crush", "@charmland/crush"},
                                                        {"factory", "droid"}};
const std::map<std::string, std::string> wingetPackages = {{"workbuddy", "Tencent.WorkBuddy"},
                                                           {"windsurf", "Codeium.Windsurf"},
                                                           {"trae", "ByteDance.Trae"},
                                                           {"kiro", "Amazon.Kiro"}};
J nativeApp(const std::string &id, const fs::path &exe) {
    J found;
    int count = 0;
    for (auto &record : registered_agents())
        if (record["id"] == id) {
            if (record.value("uninstall", "").empty())
                continue;
            auto paths = record.value("paths", J::object());
            auto desktop = path(paths.value("desktop", ""));
            auto command = path(paths.value("cli", ""));
            std::error_code ec;
            if (!fs::is_regular_file(desktop, ec) && !fs::is_regular_file(command, ec))
                continue;
            // Never use an unrelated desktop uninstaller for a separately installed native CLI.
            if (!registration_matches_executable(record, exe))
                continue;
            auto location = path(record.value("location", ""));
            if (!exe.empty() && !location.empty() && inside(exe, location))
                return record;
            found = record;
            ++count;
        }
    return count == 1 ? found : J();
}
bool bundled(const fs::path &exe) {
    auto s = display_path(exe);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s.find("\\openai\\codex\\bin\\") != std::string::npos ||
           s.find("\\resources\\runtime\\cli\\bin\\dsh.cmd") != std::string::npos;
}
bool npmEntry(const fs::path &exe, const std::string &package) {
    if (exe.empty())
        return false;
    std::error_code ec;
    return fs::is_regular_file(exe.parent_path() / "node_modules" / path(package) / "package.json", ec);
}
J command(const std::string &tool, const J &args, const std::string &method) {
    auto exe = find_exe(tool);
    if (exe.empty())
        throw std::runtime_error("未找到 " + tool + "，请先安装对应的官方运行环境后重试");
    return {{"tool", pathstr(exe)}, {"args", args}, {"method", method}, {"env", J::object()}};
}
J nativeUninstall(const J &record) {
    auto raw = wide(record.value("uninstall", ""));
    if (raw.empty())
        throw std::runtime_error("未找到官方卸载入口，已停止操作");
    int argc = 0;
    auto argv = CommandLineToArgvW(raw.c_str(), &argc);
    if (!argv || argc < 1) {
        if (argv)
            LocalFree(argv);
        throw std::runtime_error("官方卸载记录无效");
    }
    fs::path exe = argv[0];
    J args = J::array();
    for (int i = 1; i < argc; ++i)
        args.push_back(utf8(argv[i]));
    LocalFree(argv);
    if (!exe.is_absolute())
        exe = find_exe(pathstr(exe.stem()));
    auto lower = exe.filename().wstring();
    std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
    if (exe.empty() || !fs::is_regular_file(exe) || !lower.ends_with(L".exe") || lower == L"cmd.exe" ||
        lower == L"powershell.exe" || lower == L"pwsh.exe")
        throw std::runtime_error("无法确认官方卸载程序，已停止操作");
    if (lower == L"msiexec.exe")
        for (auto &arg : args) {
            auto v = arg.get<std::string>();
            if (v == "/I" || v == "/i")
                arg = "/X";
            else if (v.size() > 2 && (v.starts_with("/I{") || v.starts_with("/i{")))
                arg = "/X" + v.substr(2);
        }
    return {{"tool", pathstr(exe)}, {"args", args}, {"method", "native-uninstall"}, {"env", J::object()}};
}
} // namespace
void refresh_installed_apps() {
    refresh_registered_agents();
}
bool valid_agent_executable(const std::string &id, const fs::path &exe) {
    if (!allowed_agent_path(exe))
        return false;
    // A leftover npm shim is not an installation. Never execute the shim to inspect it.
    if (!npmPackages.count(id) || bundled(exe))
        return true;
    auto extension = exe.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension != L".cmd" && extension != L".bat")
        return true;
    try {
        auto text = read(exe, 128 * 1024);
        std::replace(text.begin(), text.end(), '\\', '/');
        auto marker = "node_modules/" + npmPackages.at(id) + "/";
        if (text.find(marker) == std::string::npos)
            return true; // Keep independent vendor/custom launch scripts supported.
        auto root = exe.parent_path() / "node_modules" / path(npmPackages.at(id));
        auto package = J::parse(read(root / "package.json", 1024 * 1024));
        if (package.value("name", "") != npmPackages.at(id) || !package.contains("bin"))
            return false;
        auto bin = package["bin"];
        if (bin.is_object()) {
            auto name = pathstr(exe.stem());
            if (!bin.contains(name))
                return false;
            bin = bin[name];
        }
        if (!bin.is_string() || bin.get<std::string>().empty())
            return false;
        auto entry = (root / path(bin.get<std::string>())).lexically_normal();
        std::error_code ec;
        return inside(entry, root) && fs::is_regular_file(entry, ec);
    } catch (...) {
        return false;
    }
}
fs::path npm_agent_target(const std::string &id, const fs::path &exe) {
    if (!npmPackages.count(id) || !npmEntry(exe, npmPackages.at(id)) || bundled(exe))
        return {};
    try {
        auto root = exe.parent_path() / "node_modules" / path(npmPackages.at(id));
        auto package = J::parse(read(root / "package.json", 1024 * 1024));
        if (package.value("name", "") != npmPackages.at(id))
            return {};
        auto bin = package.at("bin");
        if (bin.is_object())
            bin = bin.at(pathstr(exe.stem()));
        auto target = (root / path(bin.get<std::string>())).lexically_normal();
        return inside(target, root) ? target : fs::path();
    } catch (...) {
        return {};
    }
}
J official_install_info(const std::string &id, const J &agent, const fs::path &exe) {
    auto native = nativeApp(id, exe);
    bool installed = !exe.empty() || !native.is_null();
    bool canRemove = false;
    if (!native.is_null())
        canRemove = !native.value("uninstall", "").empty();
    if (npmPackages.count(id) && npmEntry(exe, npmPackages.at(id)))
        canRemove = true;
    if (id == "kimi" || id == "openhands" || id == "roo")
        canRemove = installed;
    if (wingetPackages.count(id) && installed)
        canRemove = true;
    auto nativeClaude = path(env("USERPROFILE")) / ".local/bin/claude.exe";
    if (id == "claude" && !exe.empty() && samePath(exe, nativeClaude))
        canRemove = true;
    auto compatibility = agent_compatibility(id, exe);
    return {{"installed", installed},
            {"can_install", compatibility.value("compatible", false) ||
                                compatibility.value("repairable", false) ||
                                compatibility.value("code", "") == "not-installed"},
            {"compatibility", compatibility},
            {"repairable", installed && compatibility.value("repairable", false)},
            {"can_uninstall", canRemove},
            {"managed", false},
            {"docs", agent.value("install_docs", agent["docs"].get<std::string>())}};
}
bool npm_agent_executable(const std::string &id, const fs::path &exe) {
    if (!npmPackages.count(id) || !npmEntry(exe, npmPackages.at(id)) || !valid_agent_executable(id, exe))
        return false;
    try {
        auto root = exe.parent_path() / "node_modules" / path(npmPackages.at(id));
        auto package = J::parse(read(root / "package.json", 1024 * 1024));
        auto version = package.value("version", "");
        if (version.empty() || !isdigit((unsigned char)version[0]) || version.find('.') == std::string::npos)
            return false;
        auto bin = package["bin"];
        if (bin.is_object())
            bin = bin.at(pathstr(exe.stem()));
        auto target = (root / path(bin.get<std::string>())).lexically_normal();
        if (!inside(target, root))
            return false;
        std::ifstream input(target, std::ios::binary);
        char header[2]{};
        return input.read(header, 2) &&
               ((header[0] == '#' && header[1] == '!') || (header[0] == 'M' && header[1] == 'Z'));
    } catch (...) {
        return false;
    }
}
J Adapters::installation_plan(const std::string &id, bool remove, const fs::path &testRoot, bool thorough,
                              const std::string &variant, const fs::path &directory) {
    auto agent = item(id);
    if (remove && id == "claude" &&
        (variant == "desktop" ||
         (variant.empty() && executable(id).empty() && !desktop_executable(id).empty()))) {
        if (!testRoot.empty())
            throw std::runtime_error("隔离 npm 测试不能卸载桌面应用");
        auto desktop = desktop_executable(id);
        for (auto &record : registered_agents()) {
            if (record.value("id", "") != id || desktop.empty())
                continue;
            auto recorded = path(record.value("paths", J::object()).value("desktop", ""));
            if (recorded.empty() || !samePath(recorded, desktop))
                continue;
            J plan;
            if (record.value("package_family", "") == "Claude_pzs8sxrjxfjjc") {
                plan = {
                    {"tool", display_path(path(env("SystemRoot")) /
                                          "System32/WindowsPowerShell/v1.0/powershell.exe")},
                    {"method", "desktop-uninstall"},
                    {"env", {{"AGENTDOCK_DESKTOP_PACKAGE_ROOT", record.at("location")}}},
                    {"args", J::array({"-NoProfile", "-Command",
                                       "$ErrorActionPreference='Stop'; $packages=@(Get-AppxPackage | "
                                       "Where-Object { $_.PackageFamilyName -eq 'Claude_pzs8sxrjxfjjc' -and "
                                       "$_.InstallLocation -eq $env:AGENTDOCK_DESKTOP_PACKAGE_ROOT }); "
                                       "if($packages.Count -ne 1){throw 'Claude package could not be "
                                       "uniquely identified'}; Remove-AppxPackage -Package "
                                       "$packages[0].PackageFullName; exit 0"})}};
            } else if (!record.value("uninstall", "").empty())
                plan = nativeUninstall(record);
            else
                continue;
            plan["variant"] = "desktop";
            plan["remove"] = true;
            plan["agent"] = id;
            return plan;
        }
        throw std::runtime_error("未找到与此 Claude 桌面程序对应的官方卸载入口，原文件已保留");
    }
    if (!remove && variant == "desktop") {
        if (!testRoot.empty())
            throw std::runtime_error("系统桌面安装不能使用 npm 测试目录覆盖");
        auto desktop = desktop_install_plan(id, directory);
        if (desktop.value("method", "") == "desktop-winget" && desktop.value("tool", "").empty())
            throw std::runtime_error("桌面安装需要 Windows 应用安装程序（winget），请先安装或更新该官方组件");
        return desktop;
    }
    if (!remove && variant == "cli" && (id == "workbuddy" || id == "windsurf" || id == "trae" || id == "roo"))
        throw std::runtime_error("此条目不是独立 CLI，请使用对应的桌面或扩展安装方式");
    J plan;
    bool testing = !testRoot.empty();
    if (testing) {
        auto required = fs::absolute(store.root / "installation-tests" / path(id)).lexically_normal();
        if (!testRoot.is_absolute() || testRoot.lexically_normal() != required ||
            !inside(required, store.root / "installation-tests"))
            throw std::runtime_error("测试安装只能位于当前测试数据目录的 installation-tests 下");
        check_path(testRoot);
        if (!npmPackages.count(id))
            throw std::runtime_error("该安装方式不支持项目内隔离验证，禁止执行系统安装或卸载");
        if (remove) {
            auto file = testRoot / ".agentdock-install-test.json";
            if (!fs::is_regular_file(file) || J::parse(read(file)).value("agent", "") != id ||
                J::parse(read(file)).value("owner", "") != "AgentDock test")
                throw std::runtime_error("缺少隔离安装测试记录，拒绝卸载");
        }
    }
    auto current = testing ? fs::path() : executable(id);
    if (remove && !testing && (id == "cursor" || id == "roo")) {
        if (!samePath(home, path(env("USERPROFILE"))) && !current.empty() && !inside(current, home))
            current.clear();
        auto legacy = installation(id);
        auto saved = store.setting("agent_location:roo");
        auto preferred = saved.is_object() ? path(saved.value("extension", "")) : fs::path();
        auto cleanup = removal_files(id, home, current, preferred, thorough);
        J steps = J::array();
        if (legacy.value("managed", false))
            steps.push_back({{"method", "legacy-owned"}, {"agent", id}});
        if (id == "roo") {
            std::set<std::wstring> roots;
            for (auto &entry : cleanup["files"])
                if (!entry.value("data", false)) {
                    auto root = path(entry["path"]).parent_path();
                    auto normalized = root.lexically_normal().make_preferred().wstring();
                    std::transform(normalized.begin(), normalized.end(), normalized.begin(), towlower);
                    if (!roots.insert(normalized).second)
                        continue;
                    auto editor = root.parent_path().filename().wstring();
                    std::transform(editor.begin(), editor.end(), editor.begin(), towlower);
                    std::string name = editor == L".vscode"            ? "code"
                                       : editor == L".vscode-insiders" ? "code-insiders"
                                       : editor == L".cursor"          ? "cursor"
                                       : editor == L".windsurf"        ? "windsurf"
                                                                       : "";
                    auto host = name.empty() ? fs::path() : find_exe(name);
                    if (root.parent_path().filename() == "data") {
                        for (auto command : {"code.cmd", "cursor.cmd", "windsurf.cmd"}) {
                            auto candidate = root.parent_path().parent_path() / "bin" / command;
                            if (fs::is_regular_file(candidate)) {
                                host = candidate;
                                break;
                            }
                        }
                    }
                    if (!host.empty()) {
                        auto portable = host.parent_path().parent_path() / "data/extensions";
                        if (fs::is_directory(portable) && !samePath(portable, root))
                            continue;
                        // Isolated test homes must never invoke the real machine's editor.
                        if (!samePath(home, path(env("USERPROFILE"))) && !inside(host, home))
                            continue;
                        steps.push_back(
                            {{"tool", pathstr(host)},
                             {"args", J::array({"--extensions-dir", pathstr(root), "--uninstall-extension",
                                                "RooVeterinaryInc.roo-cline"})},
                             {"env", J::object()},
                             {"method", "vscode-extension"},
                             {"optional", true}});
                    }
                }
        }
        if (id == "cursor") {
            if (thorough && !current.empty() && agent_compatibility(id, current).value("compatible", false))
                steps.push_back({{"tool", pathstr(current)},
                                 {"args", J::array({"logout"})},
                                 {"env", J::object()},
                                 {"method", "cursor-logout"},
                                 {"remove", true},
                                 {"timeout_ms", 30000}});
            auto desktop = desktop_executable(id);
            if (!samePath(home, path(env("USERPROFILE"))) && !desktop.empty() && !inside(desktop, home))
                desktop.clear();
            if (!desktop.empty()) {
                bool found = false;
                auto layout = cursor_desktop_layout(desktop);
                for (auto &record : registered_agents()) {
                    auto recorded = path(record.value("paths", J::object()).value("desktop", ""));
                    if (record.value("id", "") == "cursor" && !recorded.empty() &&
                        samePath(recorded, desktop) && !record.value("uninstall", "").empty()) {
                        try {
                            steps.push_back(nativeUninstall(record));
                            found = true;
                        } catch (...) {
                            // A stale registry command must not hide an intact on-disk uninstaller.
                        }
                        break;
                    }
                }
                if (!found && !layout.empty()) {
                    auto uninstaller = layout.value("uninstaller", "");
                    if (!uninstaller.empty())
                        steps.push_back(
                            {{"tool", uninstaller},
                             {"args", J::array({"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"})},
                             {"method", "native-uninstall"},
                             {"env", J::object()}});
                    found = true;
                }
                if (!found)
                    throw std::runtime_error(
                        "无法核实 Cursor 桌面安装来源，请重新定位 Cursor.exe 或修复官方安装后重试");
                if (!layout.empty())
                    append_cursor_desktop_files(cleanup, desktop);
            }
        }
        auto projectSetting = store.setting("project");
        if (projectSetting.is_string() && !projectSetting.get<std::string>().empty()) {
            auto project = path(projectSetting.get<std::string>());
            auto guarded = cleanup["files"];
            if (legacy.value("managed", false))
                guarded.push_back({{"path", pathstr(managed_root(id))}, {"data", false}});
            for (auto &entry : guarded) {
                auto candidate = path(entry["path"]);
                if (samePath(candidate, project) || inside(project, candidate) ||
                    (thorough && entry.value("data", false) && inside(candidate, project)))
                    throw std::runtime_error("清理范围与当前项目重叠，已停止以保留项目：" +
                                             display_path(candidate));
            }
        }
        bool program = !steps.empty() || !cleanup["archives"].empty();
        for (auto &entry : cleanup["files"])
            program |= !entry.value("data", false);
        if (!program && !thorough && !legacy.value("user_data_removable", false))
            throw std::runtime_error("没有找到可卸载的程序文件，请先刷新检测");
        return {{"agent", id},        {"remove", true}, {"method", "verified-removal"},
                {"cleanup", cleanup}, {"steps", steps}, {"env", J::object()}};
    }
    auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
    auto nativeClaude = home / ".local/bin/claude.exe";
    bool claudeNative =
        id == "claude" && !testing && (current.empty() || !npmEntry(current, npmPackages.at(id)));
    if (!remove) {
        auto check = agent_compatibility(id, {});
        if (check.value("code", "") != "not-installed" && !check.value("compatible", false))
            throw std::runtime_error(check.value("message", "系统不兼容"));
    }
    if (remove && !testing) {
        auto legacy = installation(id);
        if (legacy.value("managed", false))
            return {{"method", "legacy-owned"}, {"agent", id}, {"remove", true}, {"test_root", ""}};
        auto native = nativeApp(id, current);
        if (!native.is_null() &&
            (bundled(current) || !npmPackages.count(id) || !npmEntry(current, npmPackages.at(id))))
            return nativeUninstall(native);
    }
    if (claudeNative) {
        if (remove) {
            if (!samePath(current, nativeClaude))
                throw std::runtime_error("未确认此 Claude Code 的官方卸载来源，已停止操作");
            auto versions = home / ".local/share/claude";
            check_path(nativeClaude);
            check_path(versions);
            plan = {{"tool", pathstr(ps)},
                    {"args",
                     J::array({"-NoLogo", "-NoProfile", "-Command",
                               "$ErrorActionPreference='Stop'; foreach($p in "
                               "@($env:AGENTDOCK_CLAUDE_BIN,$env:AGENTDOCK_CLAUDE_VERSIONS)){if(Test-Path "
                               "-LiteralPath $p){Remove-Item -LiteralPath $p -Recurse -Force}}; exit 0"})},
                    {"method", "claude-native-remove"},
                    {"env",
                     {{"AGENTDOCK_CLAUDE_BIN", pathstr(nativeClaude)},
                      {"AGENTDOCK_CLAUDE_VERSIONS", pathstr(versions)}}}};
        } else {
            plan = {{"tool", pathstr(ps)},
                    {"args", J::array({"-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                                       "$ErrorActionPreference='Stop'; Import-Module (Join-Path $PSHOME "
                                       "'Modules/Microsoft.PowerShell.Utility') -Force; & "
                                       "([scriptblock]::Create((Invoke-RestMethod "
                                       "'https://claude.ai/install.ps1'))) latest; if($LASTEXITCODE){exit "
                                       "$LASTEXITCODE}; exit 0"})},
                    {"method", "claude-native"},
                    {"env",
                     {{"PROCESSOR_ARCHITECTURE",
                       windows_platform()["architecture"] == "arm64" ? "ARM64" : "AMD64"}}}};
        }
    } else if (npmPackages.count(id)) {
        if (remove && !testing && !npmEntry(current, npmPackages.at(id)))
            throw std::runtime_error("当前 CLI 不是已确认的 npm 安装；不会用 npm 卸载桌面应用附带的命令");
        J args = J::array({remove ? "uninstall" : "install", "--global"});
        if (testing) {
            args.push_back("--prefix");
            args.push_back(pathstr(testRoot / "runtime"));
            args.push_back("--cache");
            args.push_back(pathstr(testRoot / "cache"));
        } else if (remove || npmEntry(current, npmPackages.at(id))) {
            args.push_back("--prefix");
            args.push_back(pathstr(current.parent_path()));
        }
        args.push_back(remove ? npmPackages.at(id) : npmPackages.at(id) + "@latest");
        args.push_back("--no-audit");
        args.push_back("--no-fund");
        args.push_back("--update-notifier=false");
        if (!remove) {
            args.push_back("--os=win32");
            args.push_back("--cpu=" + windows_platform().value("architecture", "x64"));
            // Command-local overrides: never rewrite the user's global npm configuration.
            args.push_back("--include=optional");
            args.push_back("--ignore-scripts=false");
            args.push_back("--allow-scripts=" + npmPackages.at(id));
        }
        plan = command("npm", args, "npm");
        plan["env"]["npm_config_platform"] = "win32";
        plan["env"]["npm_config_arch"] = windows_platform().value("architecture", "x64");
        plan["env"]["npm_config_os"] = "win32";
        plan["env"]["npm_config_cpu"] = windows_platform().value("architecture", "x64");
        if (!remove) {
            auto node = path(plan["tool"]).parent_path() / "node.exe";
            if (!fs::is_regular_file(node))
                node = find_exe("node");
            auto runtime = node_compatibility(id, node, windows_platform());
            if (node.empty() || !binary_compatibility(node, windows_platform()).value("compatible", false))
                throw std::runtime_error("Node.js 的程序架构或启动文件不兼容，请安装匹配系统的官方 Node.js");
            auto arch = binary_compatibility(node, windows_platform()).value("architecture", "");
            if (arch != windows_platform().value("architecture", ""))
                throw std::runtime_error(
                    "请安装与 Windows 架构一致的 Node.js，以免 npm 下载错误架构的 Agent");
            if (runtime.value("code", "") == "node-version" || runtime.value("code", "") == "node-missing")
                throw std::runtime_error(runtime.value("message", "Node.js 不兼容"));
        }
    } else if (id == "kiro" && variant == "cli" && !remove) {
        if (windows_platform().value("build", 0) < 22000)
            throw std::runtime_error("Kiro CLI 的官方 Windows 安装需要 Windows 11");
        plan = {{"tool", display_path(ps)},
                {"method", "official-script"},
                {"env", J::object()},
                {"args",
                 J::array({"-NoProfile", "-Command",
                           "$ErrorActionPreference='Stop'; irm 'https://cli.kiro.dev/install.ps1' | iex"})}};
    } else if (wingetPackages.count(id)) {
        J args = J::array({remove ? "uninstall" : "install", "--id", wingetPackages.at(id), "--exact",
                           "--source", "winget", "--accept-source-agreements", "--disable-interactivity"});
        if (!remove) {
            args.push_back("--accept-package-agreements");
            args.push_back("--no-upgrade");
        }
        plan = command("winget", args, "winget");
    } else if (id == "roo") {
        J args = J::array();
        auto existing = installation(id).value("extension", "");
        if (!existing.empty()) {
            args.push_back("--extensions-dir");
            args.push_back(pathstr(path(existing).parent_path()));
        }
        args.push_back(remove ? "--uninstall-extension" : "--install-extension");
        args.push_back("RooVeterinaryInc.roo-cline");
        plan = command("code", args, "vscode-extension");
    } else if (id == "kimi" || id == "openhands") {
        auto name = id == "kimi" ? "kimi-cli" : "openhands";
        J args = J::array({"tool", remove ? "uninstall" : "install", name});
        if (!remove && id == "openhands") {
            args.push_back("--python");
            args.push_back("3.12");
        }
        plan = command("uv", args, "uv");
    } else if (id == "cursor" && !remove) {
        auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
        plan = {
            {"tool", pathstr(ps)},
            {"args",
             J::array({"-NoLogo", "-NoProfile", "-Command",
                       "$ErrorActionPreference='Stop'; irm 'https://cursor.com/install?win32=true' | iex"})},
            {"method", "official-script"},
            {"env", J::object()}};
    } else
        throw std::runtime_error("未找到此安装来源的官方自动卸载入口，已停止操作");
    plan["agent"] = id;
    plan["remove"] = remove;
    plan["test_root"] = testing ? pathstr(testRoot) : "";
    plan["env"]["PATH"] = utf8(path(plan["tool"]).parent_path().wstring() + L";" + cli_search_path());
    if (testing) {
        plan["env"]["TEMP"] = pathstr(testRoot / "temp");
        plan["env"]["TMP"] = pathstr(testRoot / "temp");
    }
    return plan;
}
J Adapters::manage_installation(const std::string &id, bool remove, const fs::path &testRoot, bool thorough,
                                const std::string &variant, const fs::path &directory) {
    auto plan = installation_plan(id, remove, testRoot, thorough, variant, directory);
    if (plan.value("method", "") == "legacy-owned") {
        uninstall(id);
        return {{"message", "已卸载旧版管理器副本"}};
    }
    // Production always delegates to the official tool. The test-only override never reaches native
    // installers.
    if (!testRoot.empty()) {
        fs::create_directories(testRoot / "temp");
        if (!remove)
            atomic_write(testRoot / ".agentdock-install-test.json",
                         J{{"agent", id}, {"owner", "AgentDock test"}}.dump());
    }
    auto work = store.root / "install-jobs" / wide(uuid());
    auto effectiveLog = work / "output.log";
    fs::create_directories(work);
    if (plan.value("method", "") == "verified-removal") {
        atomic_write(work / "plan.json", plan.dump(2));
        try {
            validate_removal_files(plan["cleanup"]);
            int index = 0;
            for (auto &step : plan["steps"]) {
                try {
                    if (step.value("method", "") == "legacy-owned")
                        uninstall(id);
                    else
                        run_install_plan(step, work / std::to_string(index++), cancel_install);
                } catch (...) {
                    if (!step.value("optional", false))
                        throw;
                }
            }
            execute_removal_files(plan["cleanup"]);
            if (thorough) {
                auto profile =
                    store.root / "agent-profiles" / (id == "roo" ? "roo-code-settings.json" : "cursor.json");
                check_path(profile);
                std::error_code ec;
                fs::remove(profile, ec);
                if (ec)
                    throw std::runtime_error("管理器中的 Agent 配置未能清理：" + display_path(profile));
                store.setting("agent_binding:" + id, nullptr);
                std::lock_guard lock(store.mutex);
                store.db.run("UPDATE deployments SET active=0 WHERE agent=?", J::array({id}));
            }
            store.setting("agent_location:" + id, nullptr);
            refresh_installed_apps();
            auto remaining = installation(id);
            if (samePath(home, path(env("USERPROFILE"))) && remaining.value("installed", false))
                throw std::runtime_error("仍检测到其他安装副本，请重新检测并查看剩余路径");
        } catch (...) {
            refresh_installed_apps();
            throw;
        }
        store.event("agent", thorough ? "已卸载并清理用户数据" : "已卸载并清理程序文件", id);
        return {{"message",
                 thorough ? "已卸载并清理用户数据，项目与工作树保留" : "已卸载并清理程序文件，用户数据保留"},
                {"log", display_path(work / "plan.json")}};
    }
    try {
        try {
            run_install_plan(plan, work, cancel_install);
        } catch (const InstallationCancelled &) {
            throw;
        } catch (...) {
            // The official native download can be unavailable on a network where npm works.
            // Only fall back if it did not leave a usable native install; use the same verified npm path.
            if (plan.value("method", "") != "claude-native" ||
                agent_compatibility(id, home / ".local/bin/claude.exe").value("compatible", false))
                throw;
            auto args = J::array({"install", "--global", "@anthropic-ai/claude-code@latest",
                                  "--include=optional", "--ignore-scripts=false", "--no-audit", "--no-fund",
                                  "--update-notifier=false", "--allow-scripts=@anthropic-ai/claude-code"});
            plan = command("npm", args, "npm");
            auto architecture = windows_platform().value("architecture", "x64");
            plan["args"].push_back("--os=win32");
            plan["args"].push_back("--cpu=" + architecture);
            plan["env"]["npm_config_platform"] = "win32";
            plan["env"]["npm_config_arch"] = architecture;
            plan["env"]["npm_config_os"] = "win32";
            plan["env"]["npm_config_cpu"] = architecture;
            plan["env"]["PATH"] = utf8(path(plan["tool"]).parent_path().wstring() + L";" + cli_search_path());
            auto node = path(plan["tool"]).parent_path() / "node.exe";
            if (!fs::is_regular_file(node))
                node = find_exe("node");
            auto runtime = node_compatibility(id, node, windows_platform());
            if (!runtime.value("compatible", false) ||
                runtime.value("architecture", "") != windows_platform().value("architecture", ""))
                throw std::runtime_error("原生安装未完成，npm 备用安装需要匹配系统架构的 Node.js");
            plan["agent"] = id;
            plan["remove"] = false;
            run_install_plan(plan, work / "npm-fallback", cancel_install);
            effectiveLog = work / "npm-fallback/output.log";
        }
        if (remove && plan.value("variant", "") == "desktop") {
            auto location = store.setting("agent_location:" + id);
            if (location.is_object()) {
                location["desktop"] = "";
                location["manual_desktop"] = false;
                store.setting("agent_location:" + id, location);
            }
            refresh_installed_apps();
            if (!desktop_executable(id).empty())
                throw std::runtime_error("仍检测到桌面安装副本，请查看实际路径与卸载日志");
        }
        if (!remove) {
            refresh_cli_paths();
            if (plan.value("variant", "") == "desktop") {
                refresh_installed_apps();
                auto selected = plan.value("directory", "");
                auto desktop = selected.empty()
                                   ? desktop_executable(id)
                                   : path(paths_from_folder(id, path(selected)).value("desktop", ""));
                if (desktop.empty() || !verified_disk_desktop(id, desktop))
                    throw std::runtime_error("官方安装器已退出，但未在所选位置找到该桌面程序，请查看日志：" +
                                             display_path(work));
                auto health = binary_compatibility(desktop, windows_platform());
                if (!health.value("compatible", false))
                    throw std::runtime_error("安装后的桌面程序不兼容：" + health.value("message", "") +
                                             "；日志：" + display_path(work));
                auto location = store.setting("agent_location:" + id);
                if (!location.is_object())
                    location = J::object();
                location["desktop"] = display_path(desktop);
                location["manual_desktop"] = false;
                store.setting("agent_location:" + id, location);
                store.setting("desktop_directory:" + id, selected);
                store.event("agent", "桌面版安装已验证", id);
                return {{"message", "桌面版已安装 · " + display_path(desktop)}, {"log", display_path(work)}};
            }
            auto cli = !testRoot.empty() ? testRoot / "runtime" / path(item(id).value("cli", "") + ".cmd")
                       : plan.value("method", "") == "claude-native" ? home / ".local/bin/claude.exe"
                                                                     : executable(id);
            // Desktop-only installers and editor extensions have no CLI to version-check.
            if (!item(id).value("cli", "").empty() &&
                (npmPackages.count(id) || id == "cursor" || id == "kimi" || id == "openhands" ||
                 (id == "kiro" && variant == "cli"))) {
                auto health = agent_compatibility(id, cli);
                if (!health.value("compatible", false))
                    throw std::runtime_error("安装校验未通过：" + health.value("message", "未找到启动文件") +
                                             "；日志：" + display_path(work / "output.log"));
                run_install_plan({{"tool", pathstr(cli)},
                                  {"args", J::array({"--version"})},
                                  {"method", "verify-version"},
                                  {"env", plan["env"]},
                                  {"timeout_ms", 30000}},
                                 work / "verify", cancel_install);
                if (testRoot.empty() && plan.value("method", "") == "claude-native") {
                    auto location = store.setting("agent_location:" + id);
                    if (!location.is_object())
                        location = J::object();
                    location["cli"] = pathstr(cli);
                    location["manual_cli"] = true;
                    store.setting("agent_location:" + id, location);
                }
            }
        }
    } catch (const InstallationCancelled &) {
        refresh_installed_apps();
        store.event("agent", "安装任务已取消", id);
        return {{"cancelled", true},
                {"message", "安装已取消，已刷新本机状态"},
                {"log", pathstr(work / "output.log")}};
    } catch (...) {
        refresh_installed_apps();
        throw;
    }
    refresh_installed_apps();
    store.event("agent", remove ? "官方卸载命令已完成" : "官方安装命令已完成", id);
    return {{"message", remove ? "官方卸载命令已完成" : "官方安装命令已完成"},
            {"log", pathstr(effectiveLog)}};
}
void run_install_plan(const J &plan, const fs::path &work, std::atomic<bool> &cancel) {
    bool cancellable = !plan.value("remove", false) && plan.value("method", "") != "native-uninstall";
    if (cancellable && cancel.load())
        throw InstallationCancelled();
    fs::create_directories(work);
    if (plan.value("method", "") == "desktop-download") {
        atomic_write(work / "plan.json", plan.dump(2));
        run_desktop_install(plan, work, cancel);
        return;
    }
    if (plan.value("method", "") == "native-uninstall") {
        auto quote = [](const std::wstring &s) {
            std::wstring out = L"\"";
            size_t slashes = 0;
            for (wchar_t c : s) {
                if (c == L'\\') {
                    ++slashes;
                    continue;
                }
                if (c == L'\"')
                    out.append(slashes * 2 + 1, L'\\');
                else
                    out.append(slashes, L'\\');
                slashes = 0;
                out += c;
            }
            out.append(slashes * 2, L'\\');
            return out + L"\"";
        };
        auto exe = wide(plan["tool"]);
        std::wstring arguments;
        for (auto &a : plan["args"]) {
            if (!arguments.empty())
                arguments += ' ';
            arguments += quote(wide(a));
        }
        auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        SHELLEXECUTEINFOW info{sizeof info};
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        info.lpFile = exe.c_str();
        info.lpParameters = arguments.c_str();
        info.nShow = SW_SHOWNORMAL;
        BOOL ok = ShellExecuteExW(&info);
        if (!ok && GetLastError() == ERROR_ELEVATION_REQUIRED) {
            info.lpVerb = L"runas";
            ok = ShellExecuteExW(&info);
        }
        if (SUCCEEDED(com))
            CoUninitialize();
        if (!ok)
            throw std::runtime_error("官方卸载程序未启动或已取消");
        if (info.hProcess) {
            WaitForSingleObject(info.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(info.hProcess, &exitCode);
            CloseHandle(info.hProcess);
            if (exitCode && exitCode != 3010)
                throw std::runtime_error("官方卸载程序未完成（" + std::to_string(exitCode) + "）");
        }
        return;
    }
    auto script = work / "run.ps1", data = work / "plan.json", log = work / "output.log";
    atomic_write(data, plan.dump());
    atomic_write(script,
                 "$ErrorActionPreference='Stop'\n$env:PSModulePath=(Join-Path $PSHOME "
                 "'Modules')+';'+$env:PSModulePath\ntry {\n$p=Get-Content -LiteralPath (Join-Path "
                 "$PSScriptRoot 'plan.json') -Raw -Encoding UTF8 | ConvertFrom-Json\nforeach($v in "
                 "$p.env.PSObject.Properties){[Environment]::SetEnvironmentVariable($v.Name,[string]$"
                 "v.Value,'Process')}\n$a=@($p.args); $tool=[string]$p.tool\nif(-not(Test-Path -LiteralPath "
                 "$tool -PathType Leaf)){throw 'Official tool not "
                 "found'}\n$global:LASTEXITCODE=$null\n$ErrorActionPreference='Continue'\n& $tool @a *> "
                 "(Join-Path $PSScriptRoot "
                 "'output.log')\n$code=$LASTEXITCODE\n$ErrorActionPreference='Stop'\nif($null -eq "
                 "$code){throw 'Official tool did not start'}\nif($code){exit $code}\n} "
                 "catch {$_ | Out-File -Append (Join-Path $PSScriptRoot 'output.log');exit 1}\n");
    auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
    std::wstring commandLine = L"\"" + ps.wstring() +
                               L"\" -NoLogo -NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() +
                               L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        throw std::runtime_error("无法创建安装进程组");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        CloseHandle(job);
        throw std::runtime_error("无法设置安装进程组");
    }
    if (!CreateProcessW(ps.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, work.c_str(), &si, &pi)) {
        CloseHandle(job);
        throw std::runtime_error("无法运行官方安装工具");
    }
    // Assign before resuming: even a very early cancellation owns the entire child tree.
    if (!AssignProcessToJobObject(job, pi.hProcess) || ResumeThread(pi.hThread) == (DWORD)-1) {
        TerminateProcess(pi.hProcess, ERROR_PROCESS_ABORTED);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        throw std::runtime_error("无法托管安装进程，已停止启动");
    }
    CloseHandle(pi.hThread);
    auto started = GetTickCount64();
    bool cancelled = false, timedOut = false;
    for (;;) {
        // Include children which outlive their launcher (npm / native installation scripts).
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION state{};
        if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &state, sizeof state,
                                       nullptr)) {
            CloseHandle(job);
            CloseHandle(pi.hProcess);
            throw std::runtime_error("无法读取安装进程状态");
        }
        if (!state.ActiveProcesses)
            break;
        cancelled = cancellable && cancel.load();
        timedOut = GetTickCount64() - started > plan.value("timeout_ms", 1800000);
        if (cancelled || timedOut) {
            if (!TerminateJobObject(job, ERROR_CANCELLED)) {
                CloseHandle(job);
                CloseHandle(pi.hProcess);
                throw std::runtime_error("无法中断安装进程，请查看任务状态");
            }
            // Wait for *all* owned children before releasing UI / allowing another install.
            while (QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &state, sizeof state,
                                             nullptr) &&
                   state.ActiveProcesses)
                Sleep(20);
            break;
        }
        Sleep(50);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    if (cancelled)
        throw InstallationCancelled();
    if (timedOut)
        throw std::runtime_error(
            std::string(plan.value("remove", false) ? "卸载/清理工具超时" : "安装工具超时") +
            "，已结束任务；日志：" + display_path(log));
    if (code)
        throw std::runtime_error(
            std::string(plan.value("remove", false) ? "卸载/清理工具未完成（" : "官方安装工具未完成（") +
            std::to_string(code) + "），日志：" + display_path(log));
}
} // namespace ad

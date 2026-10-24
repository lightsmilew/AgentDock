#include "core.hpp"
#include "discovery.hpp"
#include "official_install.hpp"
#include "removal.hpp"
#include <algorithm>
#include <cwchar>
namespace ad {
namespace {
const std::map<std::string, std::pair<std::string, std::string>> packages = {
    {"codex", {"@openai/codex", "https://learn.chatgpt.com/docs/enterprise/windows-deployment"}},
    {"claude", {"@anthropic-ai/claude-code", "https://code.claude.com/docs/en/setup"}},
    {"harness", {"@deepseek-ai/dsh", "https://github.com/deepseek-ai/deepseek-harness"}},
    {"gemini", {"@google/gemini-cli", "https://geminicli.com/docs/get-started/installation/"}},
    {"qwen", {"@qwen-code/qwen-code", "https://qwenlm.github.io/qwen-code-docs/en/users/quickstart/"}},
    {"cline", {"cline", "https://docs.cline.bot/getting-started/installing-cline"}},
    {"codebuddy", {"@tencent-ai/codebuddy-code", "https://www.codebuddy.ai/docs/cli/installation"}},
    {"copilot", {"@github/copilot", "https://docs.github.com/en/copilot/get-started/cli-quickstart"}},
    {"amp", {"@ampcode/cli", "https://ampcode.com/news/npm-package-changes"}},
    {"opencode", {"opencode-ai", "https://opencode.ai/docs/"}},
    {"crush", {"@charmland/crush", "https://github.com/charmbracelet/crush/blob/main/README.md"}}};
J manifest(const fs::path &root, const std::string &id) {
    if (!fs::exists(root / ".agentdock-install.json"))
        return J();
    check_path(root);
    auto data = J::parse(read(root / ".agentdock-install.json", 65536));
    if (data.value("owner", "") != "AgentDock" || data.value("agent", "") != id ||
        data.value("schema", 0) != 1)
        throw std::runtime_error("安装目录没有有效的 AgentDock 所有权记录，拒绝改动");
    return data;
}
fs::path entryPath(const fs::path &root, const std::string &entry) {
    auto relative = path(entry);
    auto result = (root / relative).lexically_normal();
    if (relative.empty() || relative.is_absolute() || !inside(result, root))
        throw std::runtime_error("程序入口必须位于此安装目录内");
    check_path(result);
    if (!fs::is_regular_file(result))
        throw std::runtime_error("未找到安装后的程序入口");
    auto extension = result.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension != L".exe" && extension != L".cmd" && extension != L".bat")
        throw std::runtime_error("程序入口必须是 exe、cmd 或 bat");
    return result;
}
void removeOwned(const fs::path &root, const std::string &id) {
    if (manifest(root, id).is_null())
        throw std::runtime_error("此目录不属于管理器，拒绝卸载");
    // Reject junctions/symlinks before recursive deletion; never follow them into host installs.
    for (auto &file : fs::recursive_directory_iterator(root))
        check_path(file.path());
    fs::remove_all(root);
}
void npmInstall(const fs::path &root, const fs::path &npm, const std::string &package,
                std::atomic<bool> &cancel, const fs::path &log) {
    auto script = root / "install.ps1";
    atomic_write(script,
                 "$ErrorActionPreference='Stop'\ntry {\n$a=@(ConvertFrom-Json $env:AD_INSTALL_ARGS)\n& "
                 "$env:AD_INSTALL_TOOL @a *> $env:AD_INSTALL_LOG\nif ($LASTEXITCODE) { exit $LASTEXITCODE "
                 "}\n} catch { $_ | Out-File -Append $env:AD_INSTALL_LOG; exit 1 }\n");
    std::map<std::wstring, std::wstring> vars;
    auto block = GetEnvironmentStringsW();
    if (!block)
        throw std::runtime_error("无法读取安装环境");
    for (auto s = block; *s; s += wcslen(s) + 1) {
        std::wstring v = s;
        auto at = v.find('=', v.front() == '=' ? 1 : 0);
        if (at != std::wstring::npos)
            vars[v.substr(0, at)] = v.substr(at + 1);
    }
    FreeEnvironmentStringsW(block);
    // Remove case variants before setting environment names (Windows names are case-insensitive).
    auto set = [&](const std::wstring &name, const std::wstring &value) {
        for (auto it = vars.begin(); it != vars.end();)
            if (_wcsicmp(it->first.c_str(), name.c_str()) == 0)
                it = vars.erase(it);
            else
                ++it;
        vars[name] = value;
    };
    set(L"PATH", npm.parent_path().wstring() + L";" + cli_search_path());
    set(L"AD_INSTALL_TOOL", npm.wstring());
    set(L"AD_INSTALL_LOG", log.wstring());
    set(L"AD_INSTALL_ARGS", wide(J::array({"install", "--global", "--prefix", pathstr(root / "runtime"),
                                           "--cache", pathstr(root / "cache"), "--no-audit", "--no-fund",
                                           "--update-notifier=false", package + "@latest"})
                                     .dump()));
    fs::create_directories(root / "temp");
    set(L"TEMP", (root / "temp").wstring());
    set(L"TMP", (root / "temp").wstring());
    std::wstring environment;
    for (auto &[k, v] : vars) {
        environment += k + L"=" + v;
        environment += '\0';
    }
    environment += '\0';
    auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
    std::wstring command = L"\"" + ps.wstring() + L"\" -NoLogo -NoProfile -ExecutionPolicy Bypass -File \"" +
                           script.wstring() + L"\"";
    auto job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        throw std::runtime_error("无法建立安装任务");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        CloseHandle(job);
        throw std::runtime_error("无法限制安装任务范围");
    }
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(ps.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(),
                        root.c_str(), &si, &process)) {
        CloseHandle(job);
        throw std::runtime_error("无法启动 npm 安装");
    }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        throw std::runtime_error("无法隔离安装子进程");
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    auto started = GetTickCount64();
    bool interrupted = false;
    while (WaitForSingleObject(process.hProcess, 100) != WAIT_OBJECT_0)
        if (cancel || GetTickCount64() - started > 600000) {
            interrupted = true;
            TerminateJobObject(job, 1);
            WaitForSingleObject(process.hProcess, 5000);
            break;
        }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    if (interrupted)
        throw std::runtime_error("安装已取消或超时；日志：" + pathstr(log));
    if (code)
        throw std::runtime_error("npm 安装失败，请查看日志：" + pathstr(log));
}
} // namespace
fs::path Adapters::managed_root(const std::string &id) {
    item(id);
    auto base = fs::absolute(store.root / "agents").lexically_normal();
    auto root = (base / path(id)).lexically_normal();
    if (!inside(root, base) || root == base)
        throw std::runtime_error("无效的 Agent 安装目录");
    check_path(root);
    return root;
}
J Adapters::installation(const std::string &id) {
    auto a = item(id);
    auto root = managed_root(id);
    auto data = manifest(root, id);
    auto cli = executable(id);
    J out = official_install_info(id, a, cli);
    auto desktop = desktop_executable(id);
    out["desktop_executable"] = display_path(desktop);
    out["cli_installed"] = !cli.empty();
    auto desktopHealth = desktop.empty() ? J::object() : binary_compatibility(desktop, windows_platform());
    out["desktop_compatibility"] = desktopHealth;
    if (!desktop.empty())
        out["installed"] = true;
    if (id == "claude" && !desktop.empty()) {
        try {
            installation_plan(id, true, {}, false, "desktop");
            out["can_uninstall"] = true;
        } catch (...) {
        }
    }
    if (id == "cursor") {
        auto cleanup = removal_files(id, home, cli, {}, false);
        bool removable = !cleanup["files"].empty() || !cursor_desktop_layout(desktop).empty();
        for (auto &record : registered_agents()) {
            auto recorded = path(record.value("paths", J::object()).value("desktop", ""));
            if (record.value("id", "") == "cursor" && !desktop.empty() && !recorded.empty() &&
                _wcsicmp(fs::absolute(recorded).lexically_normal().make_preferred().c_str(),
                         fs::absolute(desktop).lexically_normal().make_preferred().c_str()) == 0 &&
                !record.value("uninstall", "").empty())
                removable = true;
        }
        out["can_uninstall"] = removable;
        out["installed"] = out.value("installed", false) || !cleanup["files"].empty();
        out["program_residue"] = !cleanup["files"].empty() && cli.empty() && desktop.empty();
    }
    if (id == "roo") {
        auto saved = store.setting("agent_location:roo");
        auto extension =
            roo_extension(home, saved.is_object() ? path(saved.value("extension", "")) : fs::path());
        out["extension"] = display_path(extension);
        auto versions = roo_extension_directories(home, saved.is_object() ? path(saved.value("extension", ""))
                                                                          : fs::path());
        out["installed"] = !versions.empty();
        bool active = !extension.empty();
        if (extension.empty() && !versions.empty()) {
            extension = versions.front();
            out["extension"] = display_path(extension);
        }
        auto host = find_exe("code");
        out["host_executable"] = display_path(host);
        out["can_uninstall"] =
            !versions.empty() ||
            !removal_files(id, home, {}, saved.is_object() ? path(saved.value("extension", "")) : fs::path(),
                           false)["archives"]
                 .empty();
        out["program_residue"] = !active && !versions.empty();
    }
    if (id == "cursor" || id == "roo") {
        try {
            auto saved = store.setting("agent_location:roo");
            auto cleanup = removal_files(
                id, home, cli, saved.is_object() ? path(saved.value("extension", "")) : fs::path(), true);
            bool data = false;
            for (auto &file : cleanup["files"])
                data |= file.value("data", false);
            data |= fs::is_regular_file(store.root / "agent-profiles" /
                                        (id == "roo" ? "roo-code-settings.json" : "cursor.json"));
            out["user_data_removable"] = data;
            out["can_uninstall"] = out.value("can_uninstall", false) || data;
        } catch (...) {
            out["user_data_removable"] = false;
        }
    }
    out.update({{"managed", !data.is_null()},
                {"root", display_path(root)},
                {"npm", packages.count(id) != 0},
                {"docs", a.value("install_docs", a["docs"].get<std::string>())},
                {"package", ""}});
    if (packages.count(id)) {
        out["package"] = packages.at(id).first;
        out["docs"] = packages.at(id).second;
    }
    if (!data.is_null()) {
        out["can_uninstall"] = true;
        out["method"] = data.value("method", "");
        out["version"] = data.value("version", "");
        out["entry"] = data.value("entry", "");
        out["ready"] = data.value("ready", false);
    }
    auto choices = agent_install_choices(id, home);
    bool available = false;
    for (auto &choice : choices) {
        bool present = choice["id"] == "desktop"     ? !desktop.empty()
                       : choice["id"] == "extension" ? out.value("installed", false)
                                                     : !cli.empty();
        choice["installed"] = present;
        choice["repairable"] = choice["id"] == "desktop"
                                   ? present && !desktopHealth.value("compatible", true) &&
                                         desktopHealth.value("repairable", false)
                                   : choice["id"] == "cli" && out.value("repairable", false);
        available |= choice.value("repairable", false);
        available |= !present;
    }
    out["install_choices"] = choices;
    out["can_add_variant"] = available;
    return out;
}
fs::path Adapters::executable(const std::string &id) {
    auto root = managed_root(id);
    auto data = manifest(root, id);
    if (!data.is_null() && data.value("ready", false))
        try {
            auto entry = entryPath(root, data.value("entry", ""));
            if (allowed_agent_path(entry))
                return entry;
        } catch (...) {
        }
    auto location = store.setting("agent_location:" + id);
    fs::path saved;
    bool savedValid = false;
    if (location.is_object()) {
        saved = path(location.value("cli", ""));
        if (!saved.empty() && !allowed_agent_path(saved)) {
            location["cli"] = "";
            location["manual_cli"] = false;
            store.setting("agent_location:" + id, location);
        }
        std::error_code ec;
        savedValid = !saved.empty() && fs::is_regular_file(saved, ec) &&
                     (location.value("manual_cli", false) ? valid_agent_executable(id, saved)
                                                          : verified_disk_cli(id, saved));
        if (savedValid && location.value("manual_cli", false))
            return saved;
    }
    auto found = find_exe(item(id)["cli"], [&](const fs::path &p) { return valid_agent_executable(id, p); });
    if (!found.empty() && !agent_compatibility(id, found).value("compatible", false)) {
        // A stale/wrong-architecture PATH entry must not hide a working installation later in PATH.
        auto compatible = find_exe(item(id)["cli"], [&](const fs::path &p) {
            return valid_agent_executable(id, p) && agent_compatibility(id, p).value("compatible", false);
        });
        if (!compatible.empty())
            found = compatible;
    }
    if (found.empty() && id == "cursor")
        found = find_exe("cursor-agent");
    if (!found.empty())
        return found;
    auto registered = path(registered_agent_paths(id).value("cli", ""));
    return registered.empty() && savedValid ? saved : registered;
}
J Adapters::install(const std::string &id, const fs::path &portable, const std::string &entry) {
    auto info = installation(id);
    auto root = managed_root(id);
    cancel_install = false;
    if (fs::exists(root))
        throw std::runtime_error("专用目录已存在，请先卸载管理器副本；不会覆盖原文件");
    auto method = portable.empty() ? "npm" : "portable";
    if (portable.empty() && !packages.count(id))
        throw std::runtime_error("此工具请使用官方便携版；没有采用未经核实的安装命令");
    fs::path source;
    if (!portable.empty()) {
        source = fs::absolute(portable).lexically_normal();
        check_path(source);
        if (!fs::is_directory(source) || inside(root, source) || inside(source, root))
            throw std::runtime_error("请选择独立的完整便携版目录");
        entryPath(source, entry);
    }
    auto npm = portable.empty() ? find_exe("npm") : fs::path();
    if (portable.empty() && npm.empty())
        throw std::runtime_error("未找到 npm，请先安装官方 Node.js 后重新检测");
    fs::create_directories(root);
    J data = {{"owner", "AgentDock"}, {"schema", 1}, {"agent", id}, {"method", method}, {"ready", false}};
    atomic_write(root / ".agentdock-install.json", data.dump());
    try {
        std::string selected;
        if (portable.empty()) {
            auto log = store.root / "install-logs" / (id + ".log");
            fs::create_directories(log.parent_path());
            npmInstall(root, npm, packages.at(id).first, cancel_install, log);
            selected = "runtime/" + item(id)["cli"].get<std::string>() + ".cmd";
            if (!fs::is_regular_file(root / path(selected)))
                selected = "runtime/" + item(id)["cli"].get<std::string>() + ".exe";
            auto versionFile = root / "runtime/node_modules" / path(packages.at(id).first) / "package.json";
            data["version"] = J::parse(read(versionFile)).value("version", "");
            fs::remove_all(root / "cache");
            fs::remove_all(root / "temp");
            fs::remove(root / "install.ps1");
        } else {
            uint64_t bytes = 0;
            size_t count = 0;
            for (auto &file : fs::recursive_directory_iterator(source)) {
                if (cancel_install)
                    throw std::runtime_error("安装已取消");
                check_path(file.path());
                auto target = root / "portable" / fs::relative(file.path(), source);
                check_path(target);
                if (file.is_directory())
                    fs::create_directories(target);
                else if (file.is_regular_file()) {
                    bytes += file.file_size();
                    if (++count > 100000 || bytes > 8ull * 1024 * 1024 * 1024)
                        throw std::runtime_error("便携版超过 8 GiB 或 10 万个文件");
                    fs::create_directories(target.parent_path());
                    fs::copy_file(file.path(), target);
                }
            }
            selected = pathstr(fs::path("portable") / path(entry));
        }
        entryPath(root, selected);
        data["entry"] = selected;
        data["ready"] = true;
        atomic_write(root / ".agentdock-install.json", data.dump(2));
        store.event("agent", "已安装专用副本", id);
        return data;
    } catch (...) {
        try {
            removeOwned(root, id);
        } catch (...) {
        }
        throw;
    }
}
void Adapters::uninstall(const std::string &id) {
    auto root = managed_root(id);
    removeOwned(root, id);
    store.event("agent", "已卸载管理器专用副本", id + "；原工具配置和外部安装保留");
}
} // namespace ad

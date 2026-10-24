#include "core.hpp"
#include "official_install.hpp"
#include <algorithm>
#include <cwchar>

namespace ad {
J Adapters::launch_plan(const std::string &id, const std::string &requested) {
    auto agent = item(id);
    auto executable = this->executable(id);
    if (executable.empty())
        throw std::runtime_error("未找到 " + agent.value("cli", "") +
                                 " CLI，请安装后刷新；新安装的工具无需重启管理器");
    auto health = agent_compatibility(id, executable);
    if (!health.value("compatible", false))
        throw std::runtime_error(health.value("message", "启动文件不兼容") + "；位置：" +
                                 display_path(executable));
    auto project = store.setting("project");
    auto cwd = project.is_string() && !project.get<std::string>().empty() ? path(project) : home;
    if (!fs::is_directory(cwd))
        throw std::runtime_error("工作目录已不存在，请在设置中重新选择项目目录");
    J plan = {{"executable", pathstr(executable)},
              {"cwd", pathstr(cwd)},
              {"arguments", J::array()},
              {"environment", J::object()},
              {"unset", J::array()}};
    if (id == "harness")
        plan["arguments"].push_back("web");
    if (id == "cline")
        plan["arguments"].push_back("--tui");
    if (id == "cursor") {
        auto profile = store.root / "agent-profiles/cursor.json";
        if (fs::is_regular_file(profile)) {
            auto selected = parse_jsonc(read(profile, 2 * 1024 * 1024)).value("model", "");
            if (!selected.empty()) {
                plan["arguments"].push_back("--model");
                plan["arguments"].push_back(selected);
            }
        }
        return plan;
    }
    auto binding = store.setting("agent_binding:" + id);
    auto pid = binding.is_object() ? binding.value("provider", "") : "";
    if (!requested.empty() && requested != pid)
        throw std::runtime_error("所选连接尚未应用到此 Agent，请先预览并应用配置后启动");
    if (pid.empty())
        return plan;
    J p;
    std::string key;
    try {
        auto connection = models.get(pid);
        p = std::move(connection.first);
        key = std::move(connection.second);
    } catch (...) {
        if (!requested.empty())
            throw;
        plan["warning"] = "连接已删除，本次按原工具配置启动，未注入管理器凭据";
        return plan;
    }
    if (binding.value("revision", "") != p.value("revision", "")) {
        SecureZeroMemory(key.data(), key.size());
        if (!requested.empty())
            throw std::runtime_error("模型连接在应用后已更新，请先重新应用配置");
        plan["warning"] = "连接已更新，本次按原工具配置启动，未注入管理器凭据；需要使用新连接时请重新应用";
        return plan;
    }
    if (p["protocol"] == "anthropic") {
        auto base = p["base_url"].get<std::string>();
        if (base.ends_with("/v1"))
            base.resize(base.size() - 3);
        p["base_url"] = base;
    }
    auto model = binding.value("model", p.value("model", ""));
    auto var = p.value("key_env", "");
    if (var.empty()) {
        var = "AGENTDOCK_" + pid + "_API_KEY";
        std::transform(var.begin(), var.end(), var.begin(),
                       [](unsigned char c) { return c == '-' ? '_' : (char)toupper(c); });
    }
    auto &variables = plan["environment"];
    variables[var] = key;
    if (id == "claude") {
        auto base = p["base_url"].get<std::string>();
        if (base.ends_with("/v1"))
            base.resize(base.size() - 3);
        bool bearer = p.value("auth", "") == "bearer";
        plan["unset"].push_back(bearer ? "ANTHROPIC_API_KEY" : "ANTHROPIC_AUTH_TOKEN");
        variables[bearer ? "ANTHROPIC_AUTH_TOKEN" : "ANTHROPIC_API_KEY"] = key;
        variables["ANTHROPIC_BASE_URL"] = base;
    } else if (id == "copilot") {
        auto registry = env("COPILOT_PROVIDERS_CONFIG").empty()
                            ? configs(id)[0].parent_path() / "providers.json"
                            : path(env("COPILOT_PROVIDERS_CONFIG"));
        if (fs::is_regular_file(registry)) {
            auto r = parse_jsonc(read(registry, 2 * 1024 * 1024));
            if (!r.value("providers", J::object()).empty() || !r.value("models", J::object()).empty())
                throw std::runtime_error(
                    "Copilot 原生 providers.json 覆盖环境变量，请先在原工具整理模型路由");
        }
        for (auto field :
             {"COPILOT_PROVIDER_BEARER_TOKEN", "COPILOT_PROVIDER_AZURE_API_VERSION",
              "COPILOT_PROVIDER_MODEL_ID", "COPILOT_PROVIDER_WIRE_MODEL", "COPILOT_PROVIDER_WIRE_API"})
            plan["unset"].push_back(field);
        variables["COPILOT_PROVIDER_TYPE"] = p["protocol"] == "anthropic" ? "anthropic" : "openai";
        variables["COPILOT_PROVIDER_BASE_URL"] = p["base_url"];
        variables["COPILOT_PROVIDER_API_KEY"] = key;
        variables["COPILOT_MODEL"] = model;
    } else if (id == "openhands") {
        variables["LLM_API_KEY"] = key;
        variables["LLM_MODEL"] = (p["protocol"] == "anthropic" ? "anthropic/" : "openai/") + model;
        variables["LLM_BASE_URL"] = p["base_url"];
        plan["arguments"].push_back("--override-with-envs");
    }
    SecureZeroMemory(key.data(), key.size());
    // Avoid overriding a newer native selection with a stale manager candidate.
    // These tools read their default and model catalog from the applied files.
    return plan;
}
std::string Adapters::launch(const std::string &id, const std::string &pid, bool interactive,
                             const std::vector<std::string> &arguments) {
    auto plan = launch_plan(id, pid);
    if (!arguments.empty())
        plan["arguments"] = arguments;
    struct Less {
        bool operator()(const std::wstring &a, const std::wstring &b) const {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        }
    };
    std::map<std::wstring, std::wstring, Less> variables;
    auto block = GetEnvironmentStringsW();
    if (!block)
        throw std::runtime_error("无法读取启动环境");
    for (auto s = block; *s; s += wcslen(s) + 1) {
        std::wstring v = s;
        auto at = v.find('=', v[0] == '=' ? 1 : 0);
        if (at != std::wstring::npos)
            variables[v.substr(0, at)] = v.substr(at + 1);
    }
    FreeEnvironmentStringsW(block);
    variables[L"PATH"] = path(plan["executable"]).parent_path().wstring() + L";" + cli_search_path();
    for (auto &name : plan["unset"])
        variables.erase(wide(name));
    for (auto &[name, value] : plan["environment"].items()) {
        variables[wide(name)] = wide(value);
        auto &secret = value.get_ref<std::string &>();
        SecureZeroMemory(secret.data(), secret.size());
    }
    variables[L"AGENTDOCK_LAUNCH_COMMAND"] = wide(plan["executable"]);
    variables[L"AGENTDOCK_LAUNCH_ARGUMENTS"] = wide(plan["arguments"].dump());
    std::wstring environment;
    for (auto &[name, value] : variables) {
        environment += name + L"=" + value;
        environment += '\0';
    }
    environment += '\0';
    auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
    // Fixed script. Paths and arguments remain data, including quotes, $, &, and spaces.
    std::wstring command =
        L"\"" + ps.wstring() + L"\" -NoLogo -NoProfile " +
        (interactive ? std::wstring(L"-NoExit ") : std::wstring()) +
        L"-Command \"$ErrorActionPreference = 'Stop'; $env:PSModulePath=(Join-Path $PSHOME "
        L"'Modules')+';'+$env:PSModulePath; try { $launchArgs = @(ConvertFrom-Json -InputObject "
        L"$env:AGENTDOCK_LAUNCH_ARGUMENTS); & "
        L"$env:AGENTDOCK_LAUNCH_COMMAND @launchArgs } catch { Write-Host $_ -ForegroundColor Red; " +
        (interactive ? std::wstring() : std::wstring(L"exit 1; ")) + L"}" +
        (interactive ? std::wstring() : std::wstring(L"; if ($LASTEXITCODE) { exit $LASTEXITCODE }")) + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    auto cwd = path(plan["cwd"]);
    BOOL ok =
        CreateProcessW(ps.c_str(), command.data(), nullptr, nullptr, FALSE,
                       (interactive ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW) | CREATE_UNICODE_ENVIRONMENT,
                       environment.data(), cwd.c_str(), &si, &pi);
    auto error = GetLastError();
    SecureZeroMemory(environment.data(), environment.size() * sizeof(wchar_t));
    for (auto &[name, value] : variables)
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    if (!ok)
        throw std::runtime_error("CLI 启动失败（Windows " + std::to_string(error) +
                                 "），请检查终端与工作目录");
    CloseHandle(pi.hThread);
    if (!interactive) {
        auto wait = WaitForSingleObject(pi.hProcess, 15000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        if (wait != WAIT_OBJECT_0 || code)
            throw std::runtime_error("CLI 检查失败");
    } else
        CloseHandle(pi.hProcess);
    auto warning = plan.value("warning", "");
    if (!warning.empty())
        store.event("warning", "CLI 按原配置启动", warning);
    return warning.empty() ? (interactive ? "已打开 CLI 终端" : "CLI 启动检查通过") : warning;
}
} // namespace ad

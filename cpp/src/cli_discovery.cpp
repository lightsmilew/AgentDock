#include "core.hpp"
#include "discovery.hpp"
#include <algorithm>
#include <cwctype>
#include <set>
#include <sstream>

namespace ad {
namespace {
std::wstring registry(HKEY scope, const std::wstring &key, const wchar_t *name, DWORD view = 0) {
    DWORD bytes = 0;
    if (RegGetValueW(scope, key.c_str(), name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | view, nullptr, nullptr,
                     &bytes) != ERROR_SUCCESS ||
        bytes > 1024 * 1024)
        return {};
    std::wstring value(bytes / sizeof(wchar_t), 0);
    if (RegGetValueW(scope, key.c_str(), name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | view, nullptr,
                     value.data(), &bytes) != ERROR_SUCCESS)
        return {};
    value.resize(wcslen(value.c_str()));
    return value;
}
std::wstring expanded(std::wstring value) {
    auto n = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (!n || n > 32768)
        return value;
    std::wstring out(n, 0);
    ExpandEnvironmentStringsW(value.c_str(), out.data(), n);
    out.resize(n - 1);
    return out;
}
bool regular(const fs::path &p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
std::atomic<uint64_t> pathGeneration{0};
std::vector<fs::path> searchDirectories() {
    static thread_local std::vector<fs::path> cached;
    static thread_local ULONGLONG time = 0;
    static thread_local std::string key;
    auto next = env("PATH") + env("USERPROFILE") + env("LOCALAPPDATA") + env("APPDATA") +
                env("NPM_CONFIG_PREFIX") + env("npm_config_prefix") + env("PNPM_HOME") +
                env("UV_TOOL_BIN_DIR") + env("NPM_CONFIG_USERCONFIG") + env("QWEN_INSTALL_BIN_DIR") +
                env("QWEN_INSTALL_ROOT") + std::to_string(pathGeneration.load());
    if (time && GetTickCount64() - time < 2000 && next == key)
        return cached;
    std::vector<fs::path> out;
    std::set<std::wstring> seen;
    auto add = [&](std::wstring value) {
        value = expanded(value);
        if (value.size() > 1 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (value.empty())
            return;
        fs::path p(value);
        if (!p.is_absolute())
            return;
        auto folded = p.lexically_normal().wstring();
        std::transform(folded.begin(), folded.end(), folded.begin(), towlower);
        if (seen.insert(folded).second)
            out.push_back(p);
    };
    auto paths = wide(env("PATH")) + L";" + registry(HKEY_CURRENT_USER, L"Environment", L"Path") + L";" +
                 registry(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", L"Path");
    std::wistringstream input(paths);
    std::wstring directory;
    while (std::getline(input, directory, L';'))
        add(directory);
    auto user = path(env("USERPROFILE")), local = path(env("LOCALAPPDATA"));
    for (auto relative : {L"AppData\\Roaming\\npm", L".local\\bin", L".bun\\bin", L".cargo\\bin"})
        add((user / relative).wstring());
    for (auto variable :
         {"PNPM_HOME", "NPM_CONFIG_PREFIX", "npm_config_prefix", "UV_TOOL_BIN_DIR", "QWEN_INSTALL_BIN_DIR"})
        if (!env(variable).empty())
            add(wide(env(variable)));
    if (!env("APPDATA").empty())
        add((path(env("APPDATA")) / "npm").wstring());
    if (!local.empty()) {
        add((local / "pnpm").wstring());
        add((local / "Programs/OpenAI/Codex/bin").wstring());
        add((local / "cursor-agent").wstring());
        add((local / "qwen-code/bin").wstring());
    }
    if (!env("QWEN_INSTALL_ROOT").empty())
        add((path(env("QWEN_INSTALL_ROOT")) / "bin").wstring());
    if (!env("ProgramFiles").empty())
        add((path(env("ProgramFiles")) / "nodejs").wstring());
    auto npmrc = env("NPM_CONFIG_USERCONFIG").empty() ? user / ".npmrc" : path(env("NPM_CONFIG_USERCONFIG"));
    if (regular(npmrc))
        try {
            std::istringstream lines(read(npmrc, 256 * 1024));
            std::string line;
            while (std::getline(lines, line)) {
                auto at = line.find('=');
                if (at != std::string::npos && trim(line.substr(0, at)) == "prefix") {
                    auto value = trim(line.substr(at + 1));
                    // npmrc expands ${NAME}; only inspect prefix, never expose credentials.
                    size_t start = 0;
                    while ((start = value.find("${", start)) != std::string::npos) {
                        auto end = value.find('}', start + 2);
                        if (end == std::string::npos)
                            break;
                        auto variable = value.substr(start + 2, end - start - 2);
                        auto replacement = env(variable);
                        if (!replacement.empty()) {
                            value.replace(start, end - start + 1, replacement);
                            start += replacement.size();
                        } else
                            start = end + 1;
                    }
                    add(wide(value));
                }
            }
        } catch (...) {
        }
    cached = out;
    key = next;
    time = GetTickCount64();
    return out;
}
} // namespace
void refresh_cli_paths() {
    ++pathGeneration;
}
std::wstring cli_search_path() {
    std::wstring result;
    for (auto &p : searchDirectories()) {
        if (!result.empty())
            result += ';';
        result += p.wstring();
    }
    return result;
}
fs::path find_exe(const std::string &name, const std::function<bool(const fs::path &)> &accept) {
    auto available = [&](const fs::path &p) {
        return regular(p) && allowed_agent_path(p) && (!accept || accept(p));
    };
    if (name.empty() || name.find_first_of("\r\n") != std::string::npos)
        return {};
    auto requested = path(name);
    if (requested.is_absolute())
        return available(requested) ? requested : fs::path();
    if (name.find_first_of("/\\:") != std::string::npos)
        return {};
    for (auto &directory : searchDirectories())
        for (auto extension : {L".exe", L".cmd", L".bat"}) {
            auto candidate = directory / (wide(name) + extension);
            if (available(candidate))
                return candidate.lexically_normal();
        }
    auto local = path(env("LOCALAPPDATA"));
    if (name == "codex" && !local.empty()) {
        auto root = local / "OpenAI/Codex/bin";
        std::vector<std::pair<fs::file_time_type, fs::path>> versions;
        std::error_code ec;
        if (fs::is_directory(root, ec))
            for (auto &entry : fs::directory_iterator(root, ec)) {
                auto candidate = entry.path() / "codex.exe";
                if (available(candidate)) {
                    auto modified = fs::last_write_time(candidate, ec);
                    if (!ec)
                        versions.emplace_back(modified, candidate);
                }
                if (versions.size() >= 64)
                    break;
            }
        std::sort(versions.rbegin(), versions.rend());
        if (!versions.empty())
            return versions[0].second;
    }
    if (name == "dsh") {
        auto installed = path(registered_agent_paths("harness").value("cli", ""));
        if (!installed.empty() && available(installed))
            return installed;
        for (auto &root : {local / "Programs/DeepSeek Harness", local / "DeepSeek Harness",
                           path(env("ProgramFiles")) / "DeepSeek Harness"}) {
            auto candidate = root / "resources/runtime/cli/bin/dsh.cmd";
            if (available(candidate))
                return candidate;
        }
    }
    for (auto scope : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
        for (auto view : {RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY})
            for (auto extension : {L".exe", L".cmd"}) {
                auto registered = expanded(registry(
                    scope,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + wide(name) + extension,
                    nullptr, view));
                if (registered.size() > 1 && registered.front() == L'"' && registered.back() == L'"')
                    registered = registered.substr(1, registered.size() - 2);
                if (!registered.empty() && available(fs::path(registered)))
                    return fs::path(registered);
            }
    return {};
}
} // namespace ad

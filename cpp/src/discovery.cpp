#include "discovery.hpp"
#include "official_install.hpp"
#include "removal.hpp"
#include <algorithm>
#include <appmodel.h>
#include <cwctype>
#include <fstream>
#include <set>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <winver.h>
#include <wrl/client.h>
namespace ad {
namespace {
using Microsoft::WRL::ComPtr;
const std::map<std::string, std::vector<std::wstring>> names = {
    {"codex", {L"Codex"}},
    {"claude", {L"Claude Code", L"Claude"}},
    {"harness", {L"DeepSeek Harness"}},
    {"cursor", {L"Cursor"}},
    {"gemini", {L"Gemini CLI"}},
    {"opencode", {L"OpenCode"}},
    {"workbuddy", {L"WorkBuddy"}},
    {"codebuddy", {L"CodeBuddy Code", L"CodeBuddy"}},
    {"cline", {L"Cline"}},
    {"windsurf", {L"Windsurf"}},
    {"qwen", {L"Qwen Code"}},
    {"kimi", {L"Kimi CLI"}},
    {"copilot", {L"GitHub Copilot CLI", L"GitHub Copilot"}},
    {"amp", {L"Amp CLI"}},
    {"factory", {L"Factory Droid"}},
    {"openhands", {L"OpenHands"}},
    {"kiro", {L"Kiro"}},
    {"crush", {L"Crush"}},
    {"trae", {L"Trae"}},
    {"zcode", {L"ZCode"}},
    {"vscode", {L"Microsoft Visual Studio Code", L"Visual Studio Code"}}};
const std::map<std::string, std::vector<std::wstring>> desktopFiles = {
    {"codex", {L"ChatGPT.exe", L"Codex.exe"}},
    {"claude", {L"Claude.exe"}},
    {"harness", {L"DeepSeek Harness.exe"}},
    {"cursor", {L"Cursor.exe"}},
    {"opencode", {L"OpenCode.exe"}},
    {"workbuddy", {L"WorkBuddy.exe"}},
    {"codebuddy", {L"CodeBuddy.exe"}},
    {"windsurf", {L"Windsurf.exe"}},
    {"kiro", {L"Kiro.exe"}},
    {"trae", {L"Trae.exe"}},
    {"zcode", {L"ZCode.exe"}},
    {"vscode", {L"Code.exe"}}};
std::mutex registryMutex;
J records = J::array();
ULONGLONG recordsTime = 0;
std::wstring lower(std::wstring v) {
    std::transform(v.begin(), v.end(), v.begin(), towlower);
    return v;
}
bool present(const fs::path &p) {
    std::error_code ec;
    return fs::exists(p, ec);
}
bool file(const fs::path &p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}
std::wstring expand(const std::wstring &s) {
    auto n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (!n || n > 32768)
        return s;
    std::wstring result(n, 0);
    ExpandEnvironmentStringsW(s.c_str(), result.data(), n);
    result.resize(n - 1);
    return result;
}
std::wstring value(HKEY key, const wchar_t *name) {
    DWORD bytes = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &bytes) ||
        !bytes || bytes > 65536)
        return {};
    std::wstring out(bytes / sizeof(wchar_t), 0);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, out.data(), &bytes))
        return {};
    out.resize(wcslen(out.c_str()));
    return expand(out);
}
std::string identity(const std::wstring &display) {
    auto v = lower(display);
    for (auto &[id, aliases] : names)
        for (auto alias : aliases) {
            alias = lower(alias);
            if (v.starts_with(alias) &&
                (v.size() == alias.size() || v[alias.size()] == L' ' || v[alias.size()] == L'('))
                return id;
        }
    return {};
}
fs::path iconPath(std::wstring s) {
    s = expand(s);
    if (!s.empty() && s[0] == L'"') {
        auto end = s.find(L'"', 1);
        return end == std::wstring::npos ? fs::path() : fs::path(s.substr(1, end - 1));
    }
    auto comma = s.find_last_of(L',');
    if (comma != std::wstring::npos)
        s.resize(comma);
    while (!s.empty() && iswspace(s.back()))
        s.pop_back();
    return fs::path(s);
}
fs::path commandPath(const std::wstring &raw) {
    // Unquoted paths containing spaces are common in Inno Setup uninstall records.
    auto text = expand(raw), folded = lower(text);
    if (!text.empty() && text.front() != L'"') {
        auto end = folded.find(L".exe");
        if (end != std::wstring::npos)
            return fs::path(text.substr(0, end + 4));
    }
    int count = 0;
    auto args = CommandLineToArgvW(text.c_str(), &count);
    fs::path out = args && count ? fs::path(args[0]) : fs::path();
    if (args)
        LocalFree(args);
    return out;
}
std::string cli(const std::string &id) {
    static const J catalog = resource_json(100)["agents"];
    for (const auto &agent : catalog)
        if (agent["id"] == id)
            return agent.value("cli", "");
    return {};
}
bool desktopName(const std::string &id, const fs::path &p) {
    auto it = desktopFiles.find(id);
    if (it != desktopFiles.end())
        for (auto &name : it->second)
            if (lower(p.filename().wstring()) == lower(name))
                return true;
    return false;
}
bool discoverableCli(const std::string &id, const fs::path &p) {
    if (!valid_agent_executable(id, p))
        return false;
    if (lower(p.extension().wstring()) == L".exe") {
        std::ifstream input(p, std::ios::binary);
        char header[2]{};
        return input.read(header, 2) && header[0] == 'M' && header[1] == 'Z';
    }
    if (npm_agent_executable(id, p))
        return true;
    auto name = lower(p.wstring());
    if (id == "harness" && name.find(L"\\resources\\runtime\\cli\\bin\\dsh.cmd") != std::wstring::npos)
        return true;
    if (id == "cursor") {
        auto folder = p.parent_path();
        return file(folder / "cursor-agent.exe") || file(folder / "agent.exe");
    }
    return false;
}
void merge(J &result, const J &part) {
    for (auto key : {"desktop", "cli"})
        if (result.value(key, "").empty() && !part.value(key, "").empty() && file(path(part[key])))
            result[key] = part[key];
    if (!result.value("desktop", "").empty() &&
        lower(wide(result.value("desktop", ""))) == lower(wide(result.value("cli", ""))))
        result["cli"] = "";
}
std::vector<fs::path> packageRoots(const wchar_t *family) {
    // The Store identity is independent of the display name and the current install drive/version.
    UINT32 count = 0, length = 0;
    if (GetPackagesByPackageFamily(family, &count, nullptr, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER ||
        !count || count > 256 || !length || length > 1024 * 1024)
        return {};
    std::vector<PWSTR> packages(count);
    std::vector<wchar_t> buffer(length);
    if (GetPackagesByPackageFamily(family, &count, packages.data(), &length, buffer.data()) != ERROR_SUCCESS)
        return {};
    std::vector<fs::path> roots;
    for (auto name : packages) {
        UINT32 size = 0;
        if (GetPackagePathByFullName(name, &size, nullptr) != ERROR_INSUFFICIENT_BUFFER || !size ||
            size > 32768)
            continue;
        std::vector<wchar_t> folder(size);
        if (GetPackagePathByFullName(name, &size, folder.data()) == ERROR_SUCCESS)
            roots.emplace_back(folder.data());
    }
    return roots;
}
std::vector<fs::path> codexPackageRoots() {
    return packageRoots(L"OpenAI.Codex_2p2nqsd0c76g0");
}
bool branded(const std::string &id, const fs::path &p) {
    if (!allowed_agent_path(p))
        return false;
    if (id == "codex" && lower(p.filename().wstring()) == L"chatgpt.exe") {
        for (auto &root : codexPackageRoots())
            if (inside(p, root))
                return true;
        return false;
    }
    // Disk searches have weaker provenance than a user choice or a registered application.
    DWORD unused = 0;
    auto size = GetFileVersionInfoSizeW(p.c_str(), &unused);
    if (!size || size > 1024 * 1024)
        return false;
    std::vector<BYTE> bytes(size);
    if (!GetFileVersionInfoW(p.c_str(), 0, size, bytes.data()))
        return false;
    struct Language {
        WORD language, codepage;
    } *languages = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(bytes.data(), L"\\VarFileInfo\\Translation", (void **)&languages, &length))
        return false;
    for (UINT i = 0; i < length / sizeof(Language); ++i) {
        wchar_t key[100];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\ProductName", languages[i].language,
                   languages[i].codepage);
        wchar_t *product = nullptr;
        UINT chars = 0;
        if (VerQueryValueW(bytes.data(), key, (void **)&product, &chars) && product && chars &&
            identity(product) == id && !(id == "claude" && lower(product).starts_with(L"claude code")) &&
            !(id == "codex" && lower(product).find(L"cli") != std::wstring::npos))
            return true;
    }
    return false;
}
} // namespace
bool verified_disk_cli(const std::string &id, const fs::path &p) {
    return discoverableCli(id, p) && !(desktopName(id, p) && branded(id, p));
}
bool is_roo_extension(const fs::path &root) {
    if (!allowed_agent_path(root))
        return false;
    try {
        auto p = J::parse(read(root / "package.json", 1024 * 1024));
        if (env("AGENTDOCK_TEST_SCOPES") != "1" && p.value("agentdockSyntheticFixture", false))
            return false;
        if (lower(wide(p.value("publisher", ""))) != L"rooveterinaryinc" ||
            p.value("name", "") != "roo-cline")
            return false;
        auto entry = (root / path(p.value("main", ""))).lexically_normal();
        return inside(entry, root) && file(entry);
    } catch (...) {
        return false;
    }
}
fs::path roo_extension(const fs::path &home, const fs::path &preferred) {
    if (!preferred.empty() && is_roo_extension(preferred))
        return preferred;
    for (auto &candidate : roo_extension_directories(home, preferred))
        if (is_roo_extension(candidate))
            return candidate;
    return {};
}
J paths_from_folder(const std::string &id, const fs::path &root, const fs::path &entry) {
    J result = {{"desktop", ""}, {"cli", ""}};
    if (!allowed_agent_path(root) || (!entry.empty() && !allowed_agent_path(entry)))
        return result;
    if (file(entry) && desktopName(id, entry))
        result["desktop"] = pathstr(entry);
    if (desktopFiles.count(id))
        for (auto &name : desktopFiles.at(id))
            for (const auto &dir : {root, root / "app"})
                if (result["desktop"] == "" && file(dir / name) &&
                    (id != "codex" && id != "claude" && id != "opencode" || !entry.empty() ||
                     present(dir / "resources/app.asar") || present(dir / "resources/app")))
                    result["desktop"] = pathstr(dir / name);
    auto command = cli(id);
    if (!command.empty()) {
        std::vector<fs::path> dirs = {root, root / "bin", root / "resources/app/bin"};
        if (id == "harness")
            dirs.push_back(root / "resources/runtime/cli/bin");
        // Cursor editor's cursor.cmd launches the IDE; it is never the independent agent CLI.
        std::vector<std::string> commands{command};
        if (id == "cursor")
            commands.push_back("cursor-agent");
        for (auto &command : commands)
            for (auto &dir : dirs)
                for (auto ext : {L".exe", L".cmd", L".bat"}) {
                    auto candidate = dir / (wide(command) + ext);
                    if (id == "codex" && result["desktop"] != "" && desktopName(id, candidate) &&
                        lower(candidate.parent_path().wstring()) ==
                            lower(path(result["desktop"]).parent_path().wstring()))
                        continue; // The package's legacy Codex.exe launcher is not the standalone CLI.

                    if (result["cli"] == "" &&
                        lower(candidate.wstring()) != lower(wide(result["desktop"].get<std::string>())) &&
                        file(candidate) && valid_agent_executable(id, candidate))
                        result["cli"] = pathstr(candidate);
                }
    }
    return result;
}
J paths_from_registration(const std::string &id, const J &record) {
    J result = {{"desktop", ""}, {"cli", ""}};
    std::vector<fs::path> roots;
    auto location = path(record.value("location", ""));
    if (location.is_absolute())
        roots.push_back(location);
    auto entry = iconPath(wide(record.value("icon", "")));
    if (file(entry)) {
        roots.push_back(entry.parent_path());
        merge(result, paths_from_folder(id, entry.parent_path(), entry));
    }
    auto uninstall = commandPath(wide(record.value("uninstall", "")));
    // Never infer an install root from msiexec / a missing uninstall record.
    if (file(uninstall) && lower(uninstall.filename().wstring()) != L"msiexec.exe") {
        auto dir = uninstall.parent_path();
        roots.push_back(dir);
        if (lower(dir.filename().wstring()) == L"installer")
            roots.push_back(dir.parent_path());
    }
    for (auto &root : roots)
        merge(result, paths_from_folder(id, root));
    auto name = lower(wide(record.value("name", "")));
    if (file(entry) && (name.find(L"cli") != std::wstring::npos || name.starts_with(L"claude code")) &&
        lower(entry.stem().wstring()) == lower(wide(cli(id))) && valid_agent_executable(id, entry)) {
        result["cli"] = pathstr(entry);
        result["desktop"] = "";
    }
    return result;
}
void refresh_registered_agents() {
    std::lock_guard lock(registryMutex);
    recordsTime = 0;
    refresh_cli_paths();
}
J registered_agents() {
    std::lock_guard lock(registryMutex);
    if (recordsTime && GetTickCount64() - recordsTime < 30000)
        return records;
    records = J::array();
    for (auto scope : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
        for (auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
            HKEY root = nullptr;
            if (RegOpenKeyExW(scope, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                              KEY_READ | view, &root))
                continue;
            for (DWORD i = 0; i < 8192; ++i) {
                wchar_t key[512];
                DWORD length = 512;
                auto rc = RegEnumKeyExW(root, i, key, &length, nullptr, nullptr, nullptr, nullptr);
                if (rc == ERROR_NO_MORE_ITEMS)
                    break;
                if (rc)
                    continue;
                HKEY child = nullptr;
                if (RegOpenKeyExW(root, key, 0, KEY_READ | view, &child))
                    continue;
                auto name = value(child, L"DisplayName");
                auto id = identity(name);
                if (!id.empty()) {
                    J record = {{"id", id},
                                {"name", utf8(name)},
                                {"location", utf8(value(child, L"InstallLocation"))},
                                {"icon", utf8(value(child, L"DisplayIcon"))},
                                {"uninstall", utf8(value(child, L"UninstallString"))}};
                    record["paths"] = paths_from_registration(id, record);
                    if (std::find(records.begin(), records.end(), record) == records.end())
                        records.push_back(record);
                }
                RegCloseKey(child);
            }
            RegCloseKey(root);
        }
    // App Paths can point anywhere, including another disk; query both registry views.
    for (auto &[id, files] : desktopFiles)
        for (auto &name : files)
            for (auto scope : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
                for (auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
                    HKEY key = nullptr;
                    auto sub = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + name;
                    if (RegOpenKeyExW(scope, sub.c_str(), 0, KEY_READ | view, &key))
                        continue;
                    auto target = iconPath(value(key, nullptr));
                    RegCloseKey(key);
                    if (file(target))
                        records.push_back({{"id", id},
                                           {"name", utf8(target.stem().wstring())},
                                           {"paths", paths_from_folder(id, target.parent_path(), target)}});
                }
    // Shortcuts cover per-user and all-user installs which do not register InstallLocation.
    for (auto folder :
         {FOLDERID_Programs, FOLDERID_CommonPrograms, FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
        PWSTR folderPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_DONT_VERIFY, nullptr, &folderPath)))
            continue;
        fs::path base(folderPath);
        CoTaskMemFree(folderPath);
        std::error_code ec;
        for (fs::recursive_directory_iterator i(base, fs::directory_options::skip_permission_denied, ec), end;
             i != end; i.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            auto attributes = GetFileAttributesW(i->path().c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                i.disable_recursion_pending();
                continue;
            }
            if (i.depth() >= 5)
                i.disable_recursion_pending();
            if (lower(i->path().extension().wstring()) != L".lnk")
                continue;
            auto id = identity(i->path().stem().wstring());
            if (id.empty())
                continue;
            ComPtr<IShellLinkW> link;
            ComPtr<IPersistFile> persistent;
            if (FAILED(
                    CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
                FAILED(link.As(&persistent)) || FAILED(persistent->Load(i->path().c_str(), STGM_READ)))
                continue;
            wchar_t target[32768]{};
            if (SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH)) && file(fs::path(target)))
                records.push_back(
                    {{"id", id},
                     {"name", utf8(i->path().stem().wstring())},
                     {"paths", paths_from_folder(id, fs::path(target).parent_path(), fs::path(target))}});
        }
    }
    for (auto &root : codexPackageRoots()) {
        auto paths = paths_from_folder("codex", root / "app");
        if (!paths.value("desktop", "").empty())
            records.push_back({{"id", "codex"},
                               {"name", "Codex desktop (Microsoft Store)"},
                               {"location", display_path(root)},
                               {"paths", paths}});
    }
    for (auto &root : packageRoots(L"Claude_pzs8sxrjxfjjc")) {
        for (auto &folder : {root / "app", root}) {
            auto paths = paths_from_folder("claude", folder);
            if (!paths.value("desktop", "").empty()) {
                records.push_back({{"id", "claude"},
                                   {"name", "Claude desktop (MSIX)"},
                                   {"package_family", "Claude_pzs8sxrjxfjjc"},
                                   {"location", display_path(root)},
                                   {"paths", paths}});
                break;
            }
        }
    }
    // Missing shortcuts/uninstall entries must not hide an otherwise intact standard install.
    for (auto &[id, files] : desktopFiles)
        for (auto &name : files)
            for (auto base : {path(env("LOCALAPPDATA")) / "Programs", path(env("LOCALAPPDATA")),
                              path(env("ProgramFiles")), path(env("ProgramFiles(x86)"))}) {
                if (!base.is_absolute())
                    continue;
                auto exe = base / fs::path(name).stem() / name;
                if (file(exe) && branded(id, exe))
                    records.push_back({{"id", id},
                                       {"name", utf8(fs::path(name).stem().wstring())},
                                       {"paths", paths_from_folder(id, exe.parent_path(), exe)}});
            }
    // Read only actual image paths, so a running custom-drive desktop can also be discovered.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{sizeof entry};
        std::set<std::wstring> seen;
        if (Process32FirstW(snapshot, &entry))
            do {
                for (auto &[id, files] : desktopFiles)
                    for (auto &name : files) {
                        if (lower(entry.szExeFile) != lower(name))
                            continue;
                        HANDLE process =
                            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                        wchar_t image[32768]{};
                        DWORD size = 32768;
                        bool queried = process && QueryFullProcessImageNameW(process, 0, image, &size);
                        if (process)
                            CloseHandle(process);
                        if (!queried || !seen.insert(lower(image)).second)
                            continue;
                        fs::path exe(image);
                        if (file(exe) && branded(id, exe))
                            records.push_back({{"id", id},
                                               {"name", utf8(fs::path(name).stem().wstring())},
                                               {"paths", paths_from_folder(id, exe.parent_path(), exe)}});
                    }
            } while (Process32NextW(snapshot, &entry));
        CloseHandle(snapshot);
    }
    recordsTime = GetTickCount64();
    return records;
}
J registered_agent_paths(const std::string &id) {
    J result = {{"desktop", ""}, {"cli", ""}};
    for (auto &record : registered_agents())
        if (record["id"] == id) {
            auto paths = record["paths"];
            for (auto key : {"desktop", "cli"})
                if (!paths.value(key, "").empty() && !allowed_agent_path(path(paths[key])))
                    paths[key] = "";
            merge(result, paths);
        }
    return result;
}
std::vector<fs::path> local_search_roots() {
    std::vector<fs::path> roots;
    wchar_t drives[512]{};
    if (!GetLogicalDriveStringsW(512, drives))
        return roots;
    for (auto p = drives; *p; p += wcslen(p) + 1)
        if (GetDriveTypeW(p) == DRIVE_FIXED || GetDriveTypeW(p) == DRIVE_REMOVABLE)
            roots.emplace_back(p);
    return roots;
}
J search_agent_files(const J &catalog, const std::vector<fs::path> &roots, std::atomic<bool> &cancel) {
    J found = J::object();
    std::map<std::wstring, std::vector<std::pair<std::string, bool>>> wanted;
    for (auto &agent : catalog) {
        auto id = agent["id"].get<std::string>(), command = agent.value("cli", "");
        if (!command.empty())
            for (auto ext : {L".exe", L".cmd", L".bat"})
                wanted[lower(wide(command) + ext)].push_back({id, false});
        if (id == "cursor")
            for (auto ext : {L".exe", L".cmd", L".bat"})
                wanted[std::wstring(L"cursor-agent") + ext].push_back({id, false});
        if (desktopFiles.count(id))
            for (auto &name : desktopFiles.at(id))
                wanted[lower(name)].push_back({id, true});
    }
    size_t visited = 0;
    for (const auto &dir : roots) {
        if (cancel)
            break;
        if (!allowed_agent_path(dir))
            continue;
        auto attrs = GetFileAttributesW(dir.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        std::error_code ec;
        // Depth-first iteration keeps memory proportional to depth rather than disk contents.
        for (fs::recursive_directory_iterator i(dir, fs::directory_options::skip_permission_denied, ec), end;
             i != end && !cancel; i.increment(ec)) {
            if (++visited % 512 == 0)
                Sleep(1);
            if (ec) {
                ec.clear();
                continue;
            }
            auto p = i->path();
            auto attributes = GetFileAttributesW(p.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                i.disable_recursion_pending();
                continue;
            }
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!allowed_agent_path(p)) {
                    i.disable_recursion_pending();
                    continue;
                }
                auto leaf = lower(p.filename().wstring());
                if (leaf.starts_with(L"rooveterinaryinc.roo-cline-") && is_roo_extension(p))
                    found["roo"] = {{"extension", pathstr(p)}};
                if (leaf == L"$recycle.bin" || leaf == L"system volume information")
                    i.disable_recursion_pending();
                continue;
            }
            auto match = wanted.find(lower(p.filename().wstring()));
            if (match == wanted.end())
                continue;
            if (!allowed_agent_path(p))
                continue;
            for (auto &[id, desktop] : match->second) {
                if (desktop && !branded(id, p))
                    continue;
                if (!desktop &&
                    (!discoverableCli(id, p) || (desktopName(id, p) && branded(id, p)) ||
                     (id == "cursor" && lower(p.wstring()).find(L"cursor") == std::wstring::npos &&
                      !branded(id, p))))
                    continue;
                auto key = desktop ? "desktop" : "cli";
                if (!found.contains(id))
                    found[id] = J::object();
                if (!found[id].contains(key))
                    found[id][key] = pathstr(p);
            }
        }
    }
    return found;
}
bool verified_disk_desktop(const std::string &id, const fs::path &p) {
    return file(p) && desktopName(id, p) && branded(id, p);
}
fs::path Adapters::desktop_executable(const std::string &id) {
    auto location = store.setting("agent_location:" + id);
    auto candidate = location.is_object() ? path(location.value("desktop", "")) : fs::path();
    if (!candidate.empty() && !allowed_agent_path(candidate)) {
        location["desktop"] = "";
        location["manual_desktop"] = false;
        store.setting("agent_location:" + id, location);
    }
    bool saved = file(candidate) && allowed_agent_path(candidate) && desktopName(id, candidate) &&
                 (location.value("manual_desktop", false) || branded(id, candidate));
    auto selected = store.setting("desktop_directory:" + id);
    bool selectedInstall =
        selected.is_string() && !selected.get<std::string>().empty() && inside(candidate, path(selected));
    if (saved && (location.value("manual_desktop", false) || selectedInstall))
        return candidate;
    auto registered = path(registered_agent_paths(id).value("desktop", ""));
    return registered.empty() && saved ? candidate : registered;
}
void Adapters::locate(const std::string &id, const fs::path &p) {
    item(id);
    if (id == "roo") {
        if (!p.is_absolute() || !is_roo_extension(p))
            throw std::runtime_error("请选择包含完整 Roo Code 扩展的目录");
        store.setting("agent_location:roo", {{"extension", pathstr(p)}, {"manual_extension", true}});
        return;
    }
    if (!p.is_absolute() || !file(p) || !allowed_agent_path(p))
        throw std::runtime_error("请选择实际存在的程序文件");
    auto result = paths_from_folder(id, p.parent_path(), p);
    auto extension = lower(p.extension().wstring());
    bool isDesktop =
             desktopName(id, p) && (id != "claude" && id != "codex" && id != "opencode" || branded(id, p)),
         isCli = (extension == L".exe" || extension == L".cmd" || extension == L".bat") &&
                 (lower(p.stem().wstring()) == lower(wide(cli(id))) ||
                  (id == "cursor" && lower(p.stem().wstring()) == L"cursor-agent")) &&
                 valid_agent_executable(id, p);
    if (!isDesktop && !isCli)
        throw std::runtime_error("所选文件不是此 Agent 的桌面程序或命令入口");
    if (isCli && !isDesktop) {
        result["cli"] = pathstr(p);
        if (result.value("desktop", "") == pathstr(p))
            result["desktop"] = "";
    }
    auto old = store.setting("agent_location:" + id);
    if (old.is_object()) {
        for (auto key : {"cli", "desktop"}) {
            result[std::string("manual_") + key] = old.value(std::string("manual_") + key, false);
            if (result.value(key, "").empty())
                result[key] = old.value(key, "");
        }
    }
    result[isDesktop ? "manual_desktop" : "manual_cli"] = true;
    store.setting("agent_location:" + id, result);
}
J Adapters::search_installations(std::atomic<bool> &cancel, const std::vector<fs::path> &roots) {
    auto found = search_agent_files(catalog, roots.empty() ? local_search_roots() : roots, cancel);
    for (auto &[id, result] : found.items()) {
        auto old = store.setting("agent_location:" + id);
        if (old.is_object())
            for (auto key : {"cli", "desktop", "extension"}) {
                auto saved = old.value(key, "");
                auto manual = old.value(std::string("manual_") + key, false);
                bool valid =
                    allowed_agent_path(path(saved)) &&
                    (std::string(key) == "extension" ? is_roo_extension(path(saved)) : file(path(saved)));
                if (valid && (manual || !result.contains(key))) {
                    result[key] = saved;
                    result[std::string("manual_") + key] = manual;
                }
            }
        store.setting("agent_location:" + id, result);
    }
    return found;
}
} // namespace ad

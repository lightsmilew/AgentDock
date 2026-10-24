#include "removal.hpp"
#include "discovery.hpp"
#include "official_install.hpp"
#include <algorithm>
#include <set>
#include <tlhelp32.h>
#include <tuple>
namespace ad {
namespace {
const std::string rooId = "rooveterinaryinc.roo-cline";
std::string folded(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}
std::wstring key(fs::path p) {
    auto s = fs::absolute(p).lexically_normal().make_preferred().wstring();
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}
bool present(const fs::path &p) {
    std::error_code ec;
    return fs::exists(p, ec);
}
bool rooPackage(const fs::path &p) {
    try {
        auto m = J::parse(read(p / "package.json", 1024 * 1024));
        return folded(m.value("publisher", "")) == "rooveterinaryinc" && m.value("name", "") == "roo-cline";
    } catch (...) {
        return false;
    }
}
void add(std::vector<fs::path> &out, fs::path p) {
    if (!p.is_absolute())
        return;
    p = p.lexically_normal();
    for (auto &old : out)
        if (key(old) == key(p))
            return;
    out.push_back(p);
}
fs::path local(const fs::path &home, const char *variable, const char *fallback) {
    return key(home) == key(path(env("USERPROFILE"))) && !env(variable).empty() ? path(env(variable))
                                                                                : home / fallback;
}
std::vector<fs::path> editorUsers(const fs::path &home, const fs::path &preferred = {}) {
    std::vector<fs::path> roots;
    for (auto name : {"Code", "Code - Insiders", "Cursor", "Windsurf", "VSCodium", "Trae", "Kiro"})
        add(roots, local(home, "APPDATA", "AppData/Roaming") / name / "User");
    if (!preferred.empty() && preferred.parent_path().parent_path().filename() == "data")
        add(roots, preferred.parent_path().parent_path() / "user-data/User");
    bool realHome = key(home) == key(path(env("USERPROFILE")));
    if (realHome)
        for (auto &record : registered_agents()) {
            auto id = record.value("id", "");
            if (id != "vscode" && id != "cursor" && id != "windsurf" && id != "trae" && id != "kiro")
                continue;
            auto exe = path(record.value("paths", J::object()).value("desktop", ""));
            if (!exe.empty())
                add(roots, exe.parent_path() / "data/user-data/User");
        }
    auto code = find_exe("code");
    if (!code.empty() && (realHome || inside(code, home)))
        add(roots, code.parent_path().parent_path() / "data/user-data/User");
    auto originals = roots;
    for (auto &root : originals) {
        std::error_code ec;
        int n = 0;
        for (fs::directory_iterator i(root / "profiles", fs::directory_options::skip_permission_denied, ec),
             end;
             i != end && n++ < 256; i.increment(ec)) {
            if (ec)
                break;
            if (i->is_directory(ec))
                add(roots, i->path());
        }
    }
    return roots;
}
bool rooEntry(const J &entry) {
    return entry.is_object() && entry.contains("identifier") && entry["identifier"].is_object() &&
           folded(entry["identifier"].value("id", "")) == rooId;
}
void safeTree(const fs::path &p) {
    if (!p.is_absolute() || p == p.root_path() || p.parent_path() == p.root_path())
        throw std::runtime_error("拒绝清理系统根目录：" + display_path(p));
    check_path(p);
    if (!present(p))
        return;
    std::error_code ec;
    if (!fs::is_directory(p, ec))
        return;
    uint64_t n = 0;
    for (fs::recursive_directory_iterator i(p, fs::directory_options::none, ec), end; i != end;
         i.increment(ec)) {
        if (ec || ++n > 1000000)
            throw std::runtime_error("无法完整核对清理目录：" + display_path(p));
        auto a = GetFileAttributesW(i->path().c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("清理目录含链接或不可访问文件，已停止：" + display_path(i->path()));
    }
    if (ec)
        throw std::runtime_error("清理目录不可访问：" + display_path(p));
}
bool rooSecret(std::string k) {
    if (folded(k) == rooId)
        return true;
    if (!k.starts_with("secret://"))
        return false;
    try {
        return folded(J::parse(k.substr(9)).value("extensionId", "")) == rooId;
    } catch (...) {
        return false;
    }
}
void clearDatabase(const fs::path &p) {
    if (!present(p))
        return;
    check_path(p);
    DB db(p);
    if (db.query("SELECT name FROM sqlite_master WHERE type='table' AND name='ItemTable'").empty())
        return;
    db.exec("PRAGMA secure_delete=ON");
    Tx transaction(db);
    for (auto &row : db.query("SELECT key FROM ItemTable"))
        if (rooSecret(row["key"].get<std::string>()))
            db.run("DELETE FROM ItemTable WHERE key=?", J::array({row["key"]}));
    transaction.commit();
}
} // namespace
std::vector<fs::path> roo_extension_directories(const fs::path &home, const fs::path &preferred) {
    std::vector<fs::path> roots, out;
    if (!preferred.empty()) {
        if (rooPackage(preferred))
            add(out, preferred);
        add(roots, preferred.parent_path());
    }
    for (auto name : {".vscode", ".vscode-insiders", ".cursor", ".windsurf", ".vscode-oss", ".trae", ".kiro"})
        add(roots, home / name / "extensions");
    for (auto &user : editorUsers(home, preferred))
        if (user.parent_path().filename() == "user-data")
            add(roots, user.parent_path().parent_path() / "extensions");
    for (auto &root : roots) {
        std::error_code ec;
        int n = 0;
        for (fs::directory_iterator i(root, fs::directory_options::skip_permission_denied, ec), end;
             i != end && n++ < 5000; i.increment(ec)) {
            if (ec)
                break;
            if (folded(pathstr(i->path().filename())).starts_with(rooId + "-") && rooPackage(i->path()))
                add(out, i->path());
        }
    }
    return out;
}
J removal_files(const std::string &id, const fs::path &home, const fs::path &cli, const fs::path &preferred,
                bool full) {
    J result = {{"agent", id},
                {"full", full},
                {"require_closed_editors", full && key(home) == key(path(env("USERPROFILE")))},
                {"files", J::array()},
                {"registries", J::array()},
                {"settings", J::array()},
                {"databases", J::array()}};
    result["archives"] = J::array();
    auto append = [&](const fs::path &p, const std::string &kind, bool data = false) {
        if (!present(p))
            return;
        auto absolute = fs::absolute(p).lexically_normal();
        for (auto &old : result["files"])
            if (key(path(old["path"])) == key(absolute))
                return;
        result["files"].push_back({{"path", display_path(absolute)}, {"kind", kind}, {"data", data}});
    };
    if (id == "cursor") {
        std::vector<fs::path> roots;
        add(roots, local(home, "LOCALAPPDATA", "AppData/Local") / "cursor-agent");
        if (!cli.empty() && (key(home) == key(path(env("USERPROFILE"))) || inside(cli, home))) {
            auto candidate = cli.parent_path();
            if (present(candidate / "versions") && present(candidate / "cursor-agent.cmd"))
                add(roots, candidate);
        }
        auto officialRoot = local(home, "LOCALAPPDATA", "AppData/Local") / "cursor-agent";
        for (auto &root : roots)
            if (present(root) && (key(root) == key(officialRoot) ||
                                  (present(root / "versions") &&
                                   (present(root / "cursor-agent.cmd") ||
                                    present(root / "cursor-agent.exe") || present(root / "agent.exe")))))
                append(root, "cursor-cli");
        if (full) {
            auto folder = home / ".cursor";
            std::error_code ec;
            for (fs::directory_iterator i(folder, fs::directory_options::skip_permission_denied, ec), end;
                 i != end; i.increment(ec)) {
                if (ec)
                    break;
                auto leaf = folded(pathstr(i->path().filename()));
                if (leaf != "worktrees")
                    append(i->path(), "cursor-data", true);
            }
            append(local(home, "APPDATA", "AppData/Roaming") / "Cursor", "cursor-data", true);
            append(local(home, "LOCALAPPDATA", "AppData/Local") / "Cursor", "cursor-data", true);
        }
    } else if (id == "roo") {
        std::vector<fs::path> registries;
        for (auto &folder : roo_extension_directories(home, preferred)) {
            append(folder, "roo-extension");
            add(registries, folder.parent_path() / "extensions.json");
            add(registries, folder.parent_path() / ".obsolete");
        }
        for (auto &user : editorUsers(home, preferred)) {
            add(registries, user / "extensions.json");
            if (user.filename() == "User") {
                auto cache = user.parent_path() / "CachedExtensionVSIXs";
                std::error_code ec;
                int n = 0;
                for (fs::directory_iterator i(cache, fs::directory_options::skip_permission_denied, ec), end;
                     i != end && n++ < 5000; i.increment(ec)) {
                    if (ec)
                        break;
                    if (i->is_regular_file(ec) &&
                        folded(pathstr(i->path().filename())).starts_with(rooId + "-"))
                        result["archives"].push_back(display_path(i->path()));
                }
            }
            if (!full)
                continue;
            append(user / "globalStorage" / rooId, "roo-data", true);
            for (auto name : {"state.vscdb", "state.vscdb.backup"})
                if (present(user / "globalStorage" / name))
                    result["databases"].push_back(display_path(user / "globalStorage" / name));
            auto settings = user / "settings.json";
            if (present(settings)) {
                result["settings"].push_back(display_path(settings));
                auto doc = parse_jsonc(read(settings, 8 * 1024 * 1024));
                auto custom = doc.value("roo-cline.customStoragePath", "");
                if (!custom.empty()) {
                    auto base = path(custom);
                    if (!base.is_absolute() || base == base.root_path() || key(base) == key(home))
                        throw std::runtime_error("Roo 自定义存储路径不是独立目录，无法自动彻底清理");
                    for (auto name : {"tasks", "settings", "cache"})
                        append(base / name, "roo-data", true);
                }
            }
        }
        if (full)
            append(home / ".roo", "roo-data", true);
        for (auto &p : registries)
            if (present(p))
                result["registries"].push_back(display_path(p));
    }
    return result;
}
J cursor_desktop_layout(const fs::path &desktop) {
    try {
        if (!desktop.is_absolute() || !verified_disk_desktop("cursor", desktop))
            return J::object();
        check_path(desktop);
        auto root = desktop.parent_path().lexically_normal();
        auto product = J::parse(read(root / "resources/app/product.json", 1024 * 1024));
        if (folded(product.value("nameShort", "")) != "cursor" ||
            folded(product.value("applicationName", "")) != "cursor")
            return J::object();
        auto uninstaller = [&](const fs::path &folder) -> fs::path {
            auto exe = folder / "unins000.exe";
            check_path(exe);
            check_path(folder / "unins000.dat");
            return fs::is_regular_file(folder / "unins000.dat") &&
                           binary_compatibility(exe, windows_platform()).value("compatible", false)
                       ? exe
                       : fs::path();
        };
        auto uninstall = uninstaller(root);
        if (uninstall.empty() && root.filename() == "_")
            uninstall = uninstaller(root.parent_path());
        return {{"bundle", display_path(root)},
                {"root", display_path(uninstall.empty() ? root : uninstall.parent_path())},
                {"uninstaller", display_path(uninstall)}};
    } catch (...) {
        return J::object();
    }
}
void append_cursor_desktop_files(J &plan, const fs::path &desktop) {
    auto layout = cursor_desktop_layout(desktop);
    if (layout.empty())
        throw std::runtime_error("无法核实 Cursor 桌面程序与资源文件，已停止清理，请重新定位安装目录");
    auto root = path(layout["root"]), bundle = path(layout["bundle"]);
    // Only known application payloads are selected; unrelated files in a custom/shared folder survive.
    static const std::set<std::string> names = {"cursor.exe",
                                                "resources",
                                                "locales",
                                                "bin",
                                                "chrome_100_percent.pak",
                                                "chrome_200_percent.pak",
                                                "chrome_crashpad_handler.exe",
                                                "crashpad_handler.exe",
                                                "d3dcompiler_47.dll",
                                                "ffmpeg.dll",
                                                "icudtl.dat",
                                                "libegl.dll",
                                                "libglesv2.dll",
                                                "resources.pak",
                                                "snapshot_blob.bin",
                                                "v8_context_snapshot.bin",
                                                "vk_swiftshader.dll",
                                                "vk_swiftshader_icd.json",
                                                "vulkan-1.dll",
                                                "notification_helper.exe",
                                                "license",
                                                "license.txt",
                                                "license.rtf",
                                                "licenses.chromium.html"};
    std::error_code ec;
    for (fs::directory_iterator i(bundle, ec), end; i != end; i.increment(ec)) {
        if (ec)
            throw std::runtime_error("无法读取 Cursor 安装目录");
        auto name = folded(pathstr(i->path().filename()));
        bool data = name == "data";
        if ((!data && !names.count(name)) || (data && !plan.value("full", false)))
            continue;
        plan["files"].push_back(
            {{"path", display_path(i->path())}, {"kind", "cursor-desktop"}, {"data", data}});
    }
    plan["empty_dirs"] = J::array({display_path(bundle)});
    if (!layout.value("uninstaller", "").empty())
        for (auto name : {"unins000.exe", "unins000.dat", "unins000.msg"})
            if (present(root / name))
                plan["files"].push_back(
                    {{"path", display_path(root / name)}, {"kind", "cursor-desktop"}, {"data", false}});
    if (key(root) != key(bundle)) {
        if (plan.value("full", false) && present(root / "data"))
            plan["files"].push_back(
                {{"path", display_path(root / "data")}, {"kind", "cursor-desktop"}, {"data", true}});
        plan["empty_dirs"].push_back(display_path(root));
    }
}
void validate_removal_files(const J &plan) {
    if (plan.value("require_closed_editors", false)) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            throw std::runtime_error("无法检查编辑器运行状态");
        PROCESSENTRY32W process{sizeof process};
        bool running = false;
        if (Process32FirstW(snapshot, &process))
            do {
                auto name = folded(utf8(process.szExeFile));
                if (name == "code.exe" || name == "code - insiders.exe" || name == "cursor.exe" ||
                    name == "windsurf.exe" || name == "vscodium.exe" || name == "trae.exe" ||
                    name == "kiro.exe") {
                    running = true;
                    break;
                }
            } while (Process32NextW(snapshot, &process));
        CloseHandle(snapshot);
        if (running)
            throw std::runtime_error("彻底清理前请先关闭编辑器，避免其缓存恢复已删除的配置或凭据");
    }
    // Verify every absolute target and every nested junction before deleting any of them.
    for (auto &entry : plan["files"]) {
        auto p = path(entry["path"]);
        safeTree(p);
        if (entry.value("kind", "") == "roo-extension" && present(p) && !rooPackage(p))
            throw std::runtime_error("扩展身份已变化，已停止清理：" + display_path(p));
    }
    for (auto group : {"registries", "settings", "databases", "archives"})
        for (auto &file : plan[group])
            check_path(path(file));
}
void execute_removal_files(const J &plan) {
    validate_removal_files(plan);
    std::vector<std::tuple<fs::path, std::string, std::string>> updates;
    for (auto &file : plan["registries"]) {
        auto p = path(file);
        if (!present(p))
            continue;
        auto raw = read(p, 8 * 1024 * 1024), original = raw;
        auto json = J::parse(raw);
        if (json.is_array()) {
            json.erase(std::remove_if(json.begin(), json.end(), rooEntry), json.end());
        } else if (json.is_object() && p.filename() == ".obsolete") {
            for (auto i = json.begin(); i != json.end();) {
                if (folded(i.key()).starts_with(rooId + "-"))
                    i = json.erase(i);
                else
                    ++i;
            }
        } else
            throw std::runtime_error("扩展登记文件格式异常：" + display_path(p));
        if (json != J::parse(original))
            updates.emplace_back(p, json.dump(2) + "\n", sha(original));
    }
    for (auto &entry : plan["files"]) {
        if (entry.value("data", false))
            continue;
        auto p = path(entry["path"]);
        std::error_code ec;
        if (entry.value("kind", "") == "roo-extension" && present(p)) {
            // Preserve identity on failure so locked/incomplete versions can be removed on retry.
            for (fs::directory_iterator i(p, ec), end; !ec && i != end; i.increment(ec)) {
                if (i->path().filename() != "package.json")
                    fs::remove_all(i->path(), ec);
                if (ec)
                    break;
            }
            if (!ec)
                fs::remove(p / "package.json", ec);
            if (!ec)
                fs::remove(p, ec);
        } else
            fs::remove_all(p, ec);
        if (ec || present(p))
            throw std::runtime_error("文件仍被占用或无法删除，请关闭对应 Agent/编辑器后重试：" +
                                     display_path(p));
    }
    for (auto &[p, content, hash] : updates) {
        if (sha(read(p, 8 * 1024 * 1024)) != hash)
            throw std::runtime_error("扩展登记刚被编辑器更新，请关闭编辑器后重试：" + display_path(p));
        atomic_write(p, content);
    }
    if (!plan["archives"].empty()) {
        auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
        auto script =
            "$ErrorActionPreference='Stop'; Add-Type -AssemblyName "
            "System.IO.Compression,System.IO.Compression.FileSystem; "
            "foreach($p in (ConvertFrom-Json $env:AGENTDOCK_ROO_ARCHIVES)){ $match=$false; $z=$null; try { "
            "$z=[IO.Compression.ZipFile]::OpenRead($p); $e=$z.GetEntry('extension/package.json'); if($e -and "
            "$e.Length -lt 1048576){$r=[IO.StreamReader]::new($e.Open()); try{$m=($r.ReadToEnd() | "
            "ConvertFrom-Json); $match=($m.publisher -ieq 'RooVeterinaryInc' -and $m.name -ceq "
            "'roo-cline')}finally{$r.Dispose()}} } catch { Write-Output 'Skipped unverified cached archive' "
            "} finally { if($z){$z.Dispose()} }; if($match){Remove-Item -LiteralPath $p -Force} }; exit 0";
        auto work = fs::temp_directory_path() / (L"agentdock-roo-cache-" + wide(uuid()));
        std::atomic<bool> cancel{false};
        run_install_plan({{"tool", pathstr(ps)},
                          {"args", J::array({"-NoProfile", "-Command", script})},
                          {"env", {{"AGENTDOCK_ROO_ARCHIVES", plan["archives"].dump()}}},
                          {"remove", true},
                          {"timeout_ms", 30000}},
                         work, cancel);
    }
    // Data is cleared only after all program files have been successfully removed.
    if (plan.value("full", false)) {
        for (auto &file : plan["databases"])
            try {
                clearDatabase(path(file));
            } catch (const std::exception &e) {
                throw std::runtime_error(std::string("Roo 状态/凭据清理失败：") + e.what());
            }
        for (auto &file : plan["settings"]) {
            auto p = path(file);
            auto raw = read(p, 8 * 1024 * 1024);
            auto originalHash = sha(raw);
            auto doc = parse_jsonc(raw);
            std::vector<std::string> keys;
            for (auto i = doc.begin(); i != doc.end(); ++i)
                if (i.key().starts_with("roo-cline."))
                    keys.push_back(i.key());
            for (auto &k : keys)
                raw = erase_json_member(raw, k);
            if (!keys.empty()) {
                if (sha(read(p, 8 * 1024 * 1024)) != originalHash)
                    throw std::runtime_error("编辑器配置已被其他程序更新，请关闭后重试：" + display_path(p));
                atomic_write(p, raw);
            }
        }
        for (auto &entry : plan["files"])
            if (entry.value("data", false)) {
                auto p = path(entry["path"]);
                safeTree(p);
                std::error_code ec;
                fs::remove_all(p, ec);
                if (ec || present(p))
                    throw std::runtime_error("用户数据未能清理，请关闭编辑器后重试：" + display_path(p));
            }
    }
    for (auto &folder : plan.value("empty_dirs", J::array())) {
        auto p = path(folder);
        check_path(p);
        std::error_code ec;
        if (fs::is_directory(p, ec) && fs::is_empty(p, ec))
            fs::remove(p, ec);
        if (ec)
            throw std::runtime_error("无法清理空的安装目录：" + display_path(p));
    }
}
} // namespace ad

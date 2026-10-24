#include "discovery.hpp"
#include "official_install.hpp"
#include <algorithm>
#include <fstream>

namespace ad {
namespace {
const std::map<std::string, std::pair<std::string, std::string>> desktops = {
    {"codex", {"9PLM9XGG6VKS", ""}},
    {"claude", {"Anthropic.Claude", "Claude"}},
    {"cursor", {"Anysphere.Cursor", "cursor"}},
    {"harness", {"", "DeepSeek Harness"}},
    {"opencode", {"SST.OpenCodeDesktop", "OpenCode"}},
    {"workbuddy", {"Tencent.WorkBuddy", "WorkBuddy"}},
    {"windsurf", {"Codeium.Windsurf", "Windsurf"}},
    {"kiro", {"Amazon.Kiro", "Kiro"}},
    {"trae", {"ByteDance.Trae", "Trae"}},
    {"zcode", {"", "ZCode"}}};
bool desktopOnly(const std::string &id) {
    return id == "workbuddy" || id == "windsurf" || id == "trae" || id == "zcode";
}
fs::path destination(const fs::path &input) {
    if (!input.is_absolute())
        throw std::runtime_error("请选择桌面版的完整安装路径");
    auto p = input.lexically_normal();
    auto raw = pathstr(p);
    auto same = [&](const fs::path &other) {
        return !other.empty() && _wcsicmp(p.make_preferred().c_str(), other.lexically_normal().c_str()) == 0;
    };
    if (raw.find_first_of("\r\n\"<>|?*") != std::string::npos || raw.find(':', 2) != std::string::npos ||
        same(p.root_path()) || same(path(env("USERPROFILE"))) || same(path(env("ProgramFiles"))) ||
        same(path(env("LOCALAPPDATA"))) || same(path(env("ProgramFiles(x86)"))))
        throw std::runtime_error("请为此桌面应用选择独立的安装目录");
    auto windows = path(env("SystemRoot"));
    if (same(windows) || inside(p, windows))
        throw std::runtime_error("桌面应用不能安装在 Windows 系统目录内");
    check_path(p);
    return p;
}
} // namespace
J agent_install_choices(const std::string &id, const fs::path &home) {
    J result = J::array();
    auto desktop = desktops.find(id);
    if (desktop != desktops.end()) {
        auto local = home == path(env("USERPROFILE")) && !env("LOCALAPPDATA").empty()
                         ? path(env("LOCALAPPDATA"))
                         : home / "AppData/Local";
        auto folder = local / "Programs" / path(desktop->second.second);
        if (id == "opencode")
            folder = local / "OpenCode";
        result.push_back({{"id", "desktop"},
                          {"name", "桌面版"},
                          {"custom_directory", id != "codex" && id != "claude"},
                          {"directory", id == "codex" || id == "claude" ? "" : display_path(folder)}});
    }
    if (!desktopOnly(id))
        result.push_back({{"id", id == "roo" ? "extension" : "cli"},
                          {"name", id == "roo" ? "VS Code 扩展" : "CLI 命令行"},
                          {"custom_directory", false},
                          {"directory", ""}});
    return result;
}
J desktop_install_plan(const std::string &id, const fs::path &directory, const J &platform) {
    auto desktop = desktops.find(id);
    if (desktop == desktops.end())
        throw std::runtime_error("此 Agent 提供的是 CLI 或编辑器扩展，没有对应的 Windows 桌面安装项");
    auto host = platform.empty() ? windows_platform() : platform;
    auto arch = host.value("architecture", "");
    if (arch != "x64" && arch != "arm64")
        throw std::runtime_error("该桌面版需要 64 位 Windows");
    J plan = {{"agent", id}, {"variant", "desktop"}, {"remove", false}, {"env", J::object()}};
    fs::path target;
    if (id == "codex" || id == "claude") {
        if (!directory.empty())
            throw std::runtime_error("此桌面版通过官方 Store / MSIX 安装，由 Windows 管理安装目录");
    } else {
        target = destination(directory);
        // A shared nonempty folder must never become an installer's uninstall root.
        std::error_code ec;
        if (fs::is_directory(target, ec) && !fs::is_empty(target, ec)) {
            auto existing = path(paths_from_folder(id, target).value("desktop", ""));
            if (existing.empty() || !verified_disk_desktop(id, existing))
                throw std::runtime_error("所选目录已有其他文件，请选择空目录或该 Agent 已有的安装目录");
        }
        plan["directory"] = display_path(target);
    }
    if (id == "zcode") {
        plan["method"] = "desktop-download";
        plan["url"] = "https://zcode.z.ai/cn/docs/install";
        plan["architecture"] = arch;
    } else if (id == "harness") {
        if (arch == "arm64" && host.value("build", 0) < 22000)
            throw std::runtime_error(
                "Harness 桌面版当前提供 x64 安装器，ARM64 需要 Windows 11 的 x64 兼容支持");
        plan["method"] = "desktop-download";
        plan["url"] = "https://download.deepseek.com/desktop/dsh-latest-windows-x64.exe";
        plan["architecture"] = "x64";
    } else {
        auto tool = find_exe("winget");
        J args = J::array({"install", "--id", desktop->second.first, "--exact", "--source",
                           id == "codex" ? "msstore" : "winget", "--accept-source-agreements",
                           "--accept-package-agreements", "--disable-interactivity", "--silent"});
        if (id == "claude") {
            args.push_back("--installer-type");
            args.push_back("msix");
        } else if (id != "codex") {
            args.push_back("--location");
            args.push_back(display_path(target));
        }
        plan["method"] = "desktop-winget";
        plan["tool"] = display_path(tool);
        plan["args"] = args;
    }
    return plan;
}
void run_desktop_install(const J &plan, const fs::path &work, std::atomic<bool> &cancel) {
    bool zcode =
        plan.value("agent", "") == "zcode" && plan.value("url", "") == "https://zcode.z.ai/cn/docs/install";
    bool harness =
        plan.value("agent", "") == "harness" &&
        plan.value("url", "") == "https://download.deepseek.com/desktop/dsh-latest-windows-x64.exe";
    if (!harness && !zcode)
        throw std::runtime_error("桌面安装来源未通过核实");
    if (zcode && plan.value("architecture", "") != "x64" && plan.value("architecture", "") != "arm64")
        throw std::runtime_error("ZCode Windows 架构不受支持");
    auto target = destination(path(plan.at("directory")));
    auto setup = fs::absolute(work / "desktop-setup.exe").lexically_normal();
    check_path(setup);
    auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
    auto environment = J{{"AGENTDOCK_DESKTOP_SETUP", display_path(setup)},
                         {"AGENTDOCK_DESKTOP_URL", plan["url"]},
                         {"AGENTDOCK_DESKTOP_ARCH", plan.value("architecture", "")},
                         {"AGENTDOCK_DESKTOP_PRODUCT", zcode ? "zcode" : "harness"},
                         {"AGENTDOCK_DESKTOP_DIRECTORY", display_path(target)}};
    try {
        run_install_plan(
            {{"tool", display_path(ps)},
             {"env", environment},
             {"args",
              J::array({"-NoProfile", "-Command",
                        "$ErrorActionPreference='Stop'; "
                        "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; "
                        "$client=New-Object Net.WebClient; try "
                        "{ $url=$env:AGENTDOCK_DESKTOP_URL; if($env:AGENTDOCK_DESKTOP_PRODUCT -eq 'zcode'){"
                        "$html=$client.DownloadString($url); $arch=$env:AGENTDOCK_DESKTOP_ARCH; "
                        "$pattern='https://cdn-zcode\\.z\\.ai/zcode/electron/releases/[0-9]+(?:\\.[0-9]+){2}/"
                        "windows-'+$arch+'/ZCode-[0-9]+(?:\\.[0-9]+){2}-win-'+$arch+'\\.exe'; "
                        "$match=[regex]::Match($html,$pattern); if(!$match.Success){throw 'Official ZCode "
                        "download link not found'}; "
                        "$url=$match.Value }; $client.DownloadFile($url,$env:AGENTDOCK_DESKTOP_SETUP)} "
                        "finally {$client.Dispose()}; exit 0"})}},
            work / "download", cancel);
        auto health = binary_compatibility(setup, windows_platform());
        if (!health.value("compatible", false))
            throw std::runtime_error("桌面安装包不兼容或下载不完整：" + health.value("message", ""));
        std::ifstream input(setup, std::ios::binary);
        std::string prefix(1024 * 1024, '\0');
        input.read(prefix.data(), prefix.size());
        prefix.resize((size_t)input.gcount());
        if (prefix.find("NullsoftInst") == std::string::npos)
            throw std::runtime_error("官方桌面安装器格式已变化，已停止执行，请使用官方安装界面");
        input.close();
        if (zcode)
            run_install_plan(
                {{"tool", display_path(ps)},
                 {"env", environment},
                 {"args",
                  J::array({"-NoProfile", "-Command",
                            "$ErrorActionPreference='Stop'; Import-Module (Join-Path $PSHOME "
                            "'Modules/Microsoft.PowerShell.Security') -Force; "
                            "$signature=Get-AuthenticodeSignature -LiteralPath $env:AGENTDOCK_DESKTOP_SETUP; "
                            "if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject "
                            "-notmatch 'O=北京智谱华章科技股份有限公司(?:,|$)'){throw 'ZCode installer "
                            "signature is not trusted'}; exit 0"})}},
                work / "signature", cancel);
        // NSIS /D must be the final, unquoted argument. Values travel as environment data, never code.
        run_install_plan(
            {{"tool", display_path(ps)},
             {"env", environment},
             {"args",
              J::array(
                  {"-NoProfile", "-Command",
                   "$ErrorActionPreference='Stop'; $i=New-Object Diagnostics.ProcessStartInfo; "
                   "$i.FileName=$env:AGENTDOCK_DESKTOP_SETUP; $i.Arguments='/S "
                   "/D='+$env:AGENTDOCK_DESKTOP_DIRECTORY; "
                   "$i.UseShellExecute=$false; $i.CreateNoWindow=$true; $p=[Diagnostics.Process]::Start($i); "
                   "$p.WaitForExit(); exit $p.ExitCode"})}},
            work / "install", cancel);
    } catch (...) {
        std::error_code ec;
        fs::remove(setup, ec);
        throw;
    }
    std::error_code ec;
    fs::remove(setup, ec);
}
} // namespace ad

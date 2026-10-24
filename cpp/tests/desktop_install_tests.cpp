#include "core.hpp"
#include "discovery.hpp"
#include "official_install.hpp"
#include <algorithm>
#include <appmodel.h>
using namespace ad;
J desktop_install_audit() {
    J out = {{"records", J::array()}, {"packages", J::array()}};
    auto requestedCursor = path(env("AGENTDOCK_AUDIT_CURSOR_EXE"));
    if (!requestedCursor.empty()) {
        auto auditRoot = fs::path(BRAND_SOURCE_FILE).parent_path().parent_path().parent_path() /
                         ".development/cursor-path-audits" / wide(uuid());
        Store store(auditRoot);
        Models models(store);
        Adapters adapters(store, models);
        std::atomic<bool> cancel{false};
        auto search = search_agent_files(adapters.catalog, {requestedCursor.parent_path()}, cancel);
        adapters.locate("cursor", requestedCursor);
        auto installation = adapters.installation("cursor");
        out["cursor"] = {{"path", display_path(requestedCursor)},
                         {"allowed", allowed_agent_path(requestedCursor)},
                         {"verified", verified_disk_desktop("cursor", requestedCursor)},
                         {"compatibility", binary_compatibility(requestedCursor, windows_platform())},
                         {"folder", paths_from_folder("cursor", requestedCursor.parent_path())},
                         {"disk_search", search},
                         {"selected_desktop", display_path(adapters.desktop_executable("cursor"))},
                         {"desktop_installed", installation["install_choices"][0].value("installed", false)}};
    }
    refresh_installed_apps();
    for (auto &record : registered_agents())
        if (record.value("id", "") == "codex")
            out["records"].push_back(record);
    UINT32 count = 0, length = 0;
    auto rc = GetPackagesByPackageFamily(L"OpenAI.Codex_2p2nqsd0c76g0", &count, nullptr, &length, nullptr);
    out["family_status"] = rc;
    if (rc == ERROR_INSUFFICIENT_BUFFER && count && length) {
        std::vector<PWSTR> names(count);
        std::vector<wchar_t> bytes(length);
        if (GetPackagesByPackageFamily(L"OpenAI.Codex_2p2nqsd0c76g0", &count, names.data(), &length,
                                       bytes.data()) == ERROR_SUCCESS)
            for (auto name : names) {
                UINT32 size = 0;
                auto status = GetPackagePathByFullName(name, &size, nullptr);
                J item = {{"name", utf8(name)}, {"path_status", status}, {"path_size", size}};
                if (size && size < 32768) {
                    std::vector<wchar_t> folder(size);
                    item["result"] = GetPackagePathByFullName(name, &size, folder.data());
                    fs::path root(folder.data());
                    item["path"] = display_path(root);
                    auto exe = root / "app/ChatGPT.exe";
                    item["exists"] = fs::is_regular_file(exe);
                    item["verified"] = verified_disk_desktop("codex", exe);
                    item["folder_paths"] = paths_from_folder("codex", root / "app");
                }
                out["packages"].push_back(item);
            }
    }
    return out;
}
int desktop_install_tests() {
    auto workspace = fs::path(BRAND_SOURCE_FILE).parent_path().parent_path().parent_path();
    auto root = workspace / ".development/desktop-install/tests" / wide(uuid());
    auto home = root / "home";
    int count = 0;
    auto check = [&](bool yes, const char *message) {
        if (!yes)
            throw std::runtime_error(std::string("FAIL desktop install: ") + message);
        ++count;
    };
    auto rejects = [&](auto callback, const char *message) {
        bool rejected = false;
        try {
            callback();
        } catch (...) {
            rejected = true;
        }
        check(rejected, message);
    };
    for (auto id : {"codex", "claude", "cursor", "harness", "opencode", "kiro"}) {
        auto choices = agent_install_choices(id, home);
        check(choices.size() == 2 && choices[0]["id"] == "desktop" && choices[1]["id"] == "cli",
              "desktop and CLI products expose separate choices");
    }
    for (auto id : {"gemini", "qwen", "kimi", "copilot", "codebuddy", "factory", "crush", "amp", "openhands",
                    "cline"}) {
        auto choices = agent_install_choices(id, home);
        check(choices.size() == 1 && choices[0]["id"] == "cli",
              "CLI products never offer an unrelated desktop client");
    }
    for (auto id : {"workbuddy", "windsurf", "trae", "zcode"}) {
        auto choices = agent_install_choices(id, home);
        check(choices.size() == 1 && choices[0]["id"] == "desktop",
              "desktop-only products do not advertise a separate agent CLI");
    }
    check(agent_install_choices("roo", home)[0]["id"] == "extension",
          "Roo is an editor extension, not a desktop executable");
    auto selected = home / path("用户选择 $() & (路径)/Cursor application");
    auto cursor = desktop_install_plan("cursor", selected);
    auto args = cursor["args"];
    auto at = std::find(args.begin(), args.end(), "--location");
    check(at != args.end() && ++at != args.end() && path(*at) == selected,
          "chosen path travels as one argument without shell interpolation");
    check(cursor["variant"] == "desktop" &&
              std::find(args.begin(), args.end(), "Anysphere.Cursor") != args.end(),
          "desktop choice installs the desktop package rather than Cursor agent script");
    auto codex = desktop_install_plan("codex", {});
    check(std::find(codex["args"].begin(), codex["args"].end(), "msstore") != codex["args"].end(),
          "Codex uses official Store distribution");
    rejects([&] { desktop_install_plan("codex", selected); },
            "MSIX cannot silently ignore a custom directory");
    auto claude = desktop_install_plan("claude", {});
    check(std::find(claude["args"].begin(), claude["args"].end(), "Anthropic.Claude") !=
                  claude["args"].end() &&
              std::find(claude["args"].begin(), claude["args"].end(), "msix") != claude["args"].end(),
          "Claude Code desktop installs official Claude Desktop MSIX independently of CLI");
    rejects([&] { desktop_install_plan("claude", selected); },
            "Claude MSIX cannot accept a fictitious custom directory");
    auto zcode = desktop_install_plan("zcode", home / "ZCode");
    check(zcode["method"] == "desktop-download" && zcode["url"] == "https://zcode.z.ai/cn/docs/install" &&
              zcode["architecture"] == windows_platform()["architecture"],
          "ZCode resolves native architecture package from official download page");
    rejects([&] { desktop_install_plan("gemini", selected); }, "CLI-only desktop request is rejected");
    rejects([&] { desktop_install_plan("cursor", path("relative/path")); },
            "relative desktop paths are rejected");
    rejects([&] { desktop_install_plan("cursor", selected.root_path()); },
            "volume root cannot become an uninstall root");
    rejects([&] { desktop_install_plan("cursor", path(env("SystemRoot")) / "Agent"); },
            "system folders are rejected");
    rejects([&] { desktop_install_plan("cursor", selected, {{"architecture", "x86"}}); },
            "unsupported Windows architecture is rejected");
    auto shared = root / "shared";
    atomic_write(shared / "project.cpp", "preserve");
    rejects([&] { desktop_install_plan("cursor", shared); },
            "nonempty project/shared directory cannot become an installer root");
    check(read(shared / "project.cpp") == "preserve", "path validation never changes existing files");
    auto harness = desktop_install_plan("harness", root / "Harness");
    check(harness["method"] == "desktop-download" &&
              harness["url"] == "https://download.deepseek.com/desktop/dsh-latest-windows-x64.exe",
          "Harness uses verified official desktop download");
    std::atomic<bool> cancel{true};
    bool cancelled = false;
    try {
        run_install_plan(harness, root / "cancelled", cancel);
    } catch (const InstallationCancelled &) {
        cancelled = true;
    }
    check(cancelled && !fs::exists(root / "cancelled"),
          "desktop cancellation before start does not download or launch an installer");
    Store store(root / "data");
    Models models(store);
    Adapters adapters(store, models, home);
    rejects(
        [&] { adapters.installation_plan("cursor", false, root / "override", false, "desktop", selected); },
        "desktop install never accepts npm isolation override");
    rejects([&] { adapters.installation_plan("windsurf", false, {}, false, "cli"); },
            "editor shell command is not installed as a standalone agent CLI");
    wchar_t own[32768]{};
    GetModuleFileNameW(nullptr, own, 32768);
    auto fixture = fs::path(own).parent_path() / "discovery_fixture.exe";
    auto tools = root / "tools";
    fs::create_directories(tools);
    fs::copy_file(fixture, tools / "winget.exe");
    auto unrelated = root / "Other app/ChatGPT.exe";
    fs::create_directories(unrelated.parent_path());
    fs::copy_file(fixture, unrelated);
    check(!verified_disk_desktop("codex", unrelated),
          "a generic ChatGPT filename is not a verified Codex Store installation");
    UINT32 packageCount = 0, packageLength = 0;
    auto packageStatus = GetPackagesByPackageFamily(L"OpenAI.Codex_2p2nqsd0c76g0", &packageCount, nullptr,
                                                    &packageLength, nullptr);
    if (packageStatus == ERROR_INSUFFICIENT_BUFFER && packageCount) {
        refresh_installed_apps();
        auto codexDesktop = path(registered_agent_paths("codex").value("desktop", ""));
        check(!codexDesktop.empty() && verified_disk_desktop("codex", codexDesktop),
              "installed Codex MSIX desktop is detected without uninstall registry or shortcut");
        auto info = adapters.installation("codex");
        check(info["install_choices"][0].value("installed", false),
              "Codex desktop variant reflects the real installed package");
        if (info.value("cli_installed", false))
            check(!info.value("can_add_variant", true),
                  "both installed Codex variants disable the install action");
    }
    {
        auto oldLocal = env("LOCALAPPDATA");
        auto local = root / "default-path-detection";
        fs::create_directories(local / "Programs/Cursor");
        fs::copy_file(fixture, local / "Programs/Cursor/Cursor.exe");
        SetEnvironmentVariableW(L"LOCALAPPDATA", local.c_str());
        refresh_installed_apps();
        bool found = false;
        for (auto &record : registered_agents())
            found |= record.value("id", "") == "cursor" &&
                     path(record.value("paths", J::object()).value("desktop", "")) ==
                         local / "Programs/Cursor/Cursor.exe";
        SetEnvironmentVariableW(L"LOCALAPPDATA", wide(oldLocal).c_str());
        refresh_installed_apps();
        check(found, "default desktop folder is detected without Windows uninstall records");
    }
    struct Environment {
        std::string oldPath = env("PATH"), marker = env("AGENTDOCK_DESKTOP_FIXTURE_ROOT");
        ~Environment() {
            SetEnvironmentVariableW(L"PATH", wide(oldPath).c_str());
            SetEnvironmentVariableW(L"AGENTDOCK_DESKTOP_FIXTURE_ROOT",
                                    marker.empty() ? nullptr : wide(marker).c_str());
            refresh_cli_paths();
        }
    } restore;
    SetEnvironmentVariableW(L"PATH", (tools.wstring() + L";" + wide(restore.oldPath)).c_str());
    SetEnvironmentVariableW(L"AGENTDOCK_DESKTOP_FIXTURE_ROOT", root.c_str());
    refresh_cli_paths();
    auto cli = home / "commands/agent.cmd";
    atomic_write(cli, "@echo off\r\nexit /b 74\r\n");
    store.setting("agent_location:cursor", {{"cli", display_path(cli)}, {"manual_cli", true}});
    auto installed = adapters.manage_installation("cursor", false, {}, false, "desktop", selected);
    check(fs::is_regular_file(selected / "Cursor.exe") &&
              installed["message"].get<std::string>().find("桌面版") != std::string::npos,
          "desktop installer integration installs into the selected project directory");
    check(path(store.setting("desktop_directory:cursor")) == selected &&
              adapters.desktop_executable("cursor") == selected / "Cursor.exe",
          "successful desktop install saves and detects the chosen location");
    check(adapters.executable("cursor") == cli && read(cli).find("74") != std::string::npos,
          "desktop install preserves existing CLI and never invokes its version command");
    auto installedInfo = adapters.installation("cursor");
    check(!installedInfo.value("can_add_variant", true),
          "installed desktop and CLI disable redundant install action");
    adapters.manage_installation("cursor", true);
    check(!fs::exists(selected / "Cursor.exe") && fs::exists(cli),
          "project fixture can be uninstalled without touching host agents or unrelated CLI");
    return count;
}

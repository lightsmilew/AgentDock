#include "core.hpp"
#include "discovery.hpp"
#include "official_install.hpp"
#include <iostream>
using namespace ad;
int discovery_tests(Store &st, Models &models, Adapters &adapters, const fs::path &root) {
    int passed = 0;
    auto check = [&](bool value, const char *label) {
        if (!value)
            throw std::runtime_error(std::string("FAIL discovery: ") + label);
        ++passed;
    };
    auto base = root / path("discovery/自定义磁盘/软件工具/有空格的目录");
    atomic_write(base / "Cursor.exe", "fixture, never execute");
    atomic_write(base / "resources/app/bin/cursor.cmd", "@echo off\r\nexit /b 0\r\n");
    auto byLocation = paths_from_registration("cursor", {{"location", pathstr(base)}});
    J desktopRegistration = {{"id", "claude"},
                             {"name", "Claude"},
                             {"location", pathstr(base)},
                             {"paths", {{"desktop", pathstr(base / "Claude.exe")}, {"cli", ""}}}};
    check(!registration_matches_executable(desktopRegistration, root / "separate-cli/claude.exe"),
          "separate CLI never uses unrelated desktop uninstaller");
    auto bundledRegistration = desktopRegistration;
    bundledRegistration["paths"]["cli"] = pathstr(base / "bin/claude.cmd");
    check(registration_matches_executable(bundledRegistration, base / "bin/claude.cmd"),
          "bundled CLI retains association with its own registered application");
    desktopRegistration["id"] = "cursor";
    desktopRegistration["name"] = "Cursor";
    check(!registration_matches_executable(desktopRegistration, base / "agent.exe"),
          "Cursor editor cannot uninstall separately distributed agent CLI");
    check(path(byLocation["desktop"]) == base / "Cursor.exe", "custom install root on arbitrary volume");
    check(byLocation["cli"] == "", "Cursor editor shell command is not the independent agent CLI");
    auto editor = base / "case-insensitive-editor";
    atomic_write(editor / "Codex.exe", "fixture desktop, never executed");
    auto editorPaths = paths_from_registration("codex", {{"icon", pathstr(editor / "Codex.exe")}});
    check(editorPaths["cli"] == "", "desktop and CLI path comparison respects Windows case-insensitivity");
    auto byIcon = paths_from_registration("cursor", {{"icon", "\"" + pathstr(base / "Cursor.exe") + "\",0"}});
    check(byIcon["desktop"] == byLocation["desktop"],
          "quoted DisplayIcon handles spaces, Unicode and icon index");
    atomic_write(base / "unins000.exe", "fixture, never execute");
    auto byUninstall =
        paths_from_registration("cursor", {{"uninstall", pathstr(base / "unins000.exe") + " /S"}});
    check(byUninstall["desktop"] == byLocation["desktop"],
          "missing InstallLocation falls back to uninstall root");
    check(paths_from_registration("cursor", {{"location", pathstr(root / "missing")}})["desktop"] == "",
          "nonexistent locations do not imply installation");
    for (auto &agent : adapters.catalog) {
        auto command = agent.value("cli", ""), id = agent["id"].get<std::string>();
        if (command.empty())
            continue;
        auto folder = base / path(id);
        atomic_write(folder / "bin" / path(command + ".cmd"), "@echo off\r\necho fixture\r\n");
        auto result = paths_from_folder(id, folder);
        check(path(result["cli"]) == folder / "bin" / path(command + ".cmd"),
              "each CLI adapter can resolve a custom installation root");
    }
    for (auto &[id, filename] : std::map<std::string, std::string>{{"workbuddy", "WorkBuddy.exe"},
                                                                   {"windsurf", "Windsurf.exe"},
                                                                   {"trae", "Trae.exe"},
                                                                   {"kiro", "Kiro.exe"},
                                                                   {"harness", "DeepSeek Harness.exe"}}) {
        auto folder = base / path(id + "-desktop");
        atomic_write(folder / path(filename), "fixture, never execute");
        check(path(paths_from_registration(id, {{"location", pathstr(folder)}})["desktop"]) ==
                  folder / path(filename),
              "desktop adapters resolve arbitrary install folders");
    }
    auto harness = base / "harness-bundle";
    atomic_write(harness / "resources/runtime/cli/bin/dsh.cmd", "@echo off\r\nexit /b 0\r\n");
    check(path(paths_from_registration("harness", {{"location", pathstr(harness)}})["cli"]) ==
              harness / "resources/runtime/cli/bin/dsh.cmd",
          "Harness bundled CLI follows actual installation root");
    adapters.locate("cursor", base / "Cursor.exe");
    check(adapters.desktop_executable("cursor") == base / "Cursor.exe",
          "manual location overrides default paths");
    check(st.setting("agent_location:cursor").value("manual_desktop", false), "manual choice is persisted");
    check(st.setting("agent_location:cursor")["cli"] == "", "manual editor choice never becomes agent CLI");
    auto install = adapters.installation("cursor");
    check(install.value("installed", false), "custom desktop appears installed without a CLI");
    bool rejected = false;
    try {
        adapters.locate("cursor", base / "unins000.exe");
    } catch (...) {
        rejected = true;
    }
    check(rejected, "manual location rejects unrelated executables");
    for (auto id : {"claude", "codex", "opencode"}) {
        auto nativeCli = base / path(id) / path(std::string(id) + ".exe");
        atomic_write(nativeCli, "fixture native CLI, never executed");
        adapters.locate(id, nativeCli);
        check(adapters.executable(id) == nativeCli, "native CLI location is not mistaken for desktop app");
        check(st.setting(std::string("agent_location:") + id).value("manual_cli", false),
              "manual native CLI selection takes precedence over PATH");
        st.setting(std::string("agent_location:") + id, nullptr);
    }
    auto gemini = base / "manual-cli/gemini.cmd";
    atomic_write(gemini, "@echo off\r\nexit /b 0\r\n");
    adapters.locate("gemini", gemini);
    check(adapters.executable("gemini") == gemini, "manual CLI works outside PATH");
    fs::remove(gemini);
    auto original = adapters.item("gemini")["cli"];
    for (auto &a : adapters.catalog)
        if (a["id"] == "gemini")
            a["cli"] = "agentdock-no-such-fixture";
    check(adapters.executable("gemini") != gemini, "deleted cached CLI is invalidated");
    for (auto &a : adapters.catalog)
        if (a["id"] == "gemini")
            a["cli"] = original;
    std::atomic<bool> cancel{false};
    auto search = root / "discovery/disk-search", portable = search / "arbitrary/deep/folder";
    wchar_t own[32768]{};
    GetModuleFileNameW(nullptr, own, 32768);
    fs::create_directories(portable);
    fs::copy_file(fs::path(own).parent_path() / "discovery_fixture.exe", portable / "Cursor.exe");
    atomic_write(search / "wrong-brand/Cursor.exe", "not an app");
    atomic_write(search / "odd/path/qwen.cmd",
                 "@echo off\r\nnode \"%~dp0node_modules\\@qwen-code\\qwen-code\\cli.js\" %*\r\n");
    atomic_write(search / "odd/path/node_modules/@qwen-code/qwen-code/package.json",
                 R"({"name":"@qwen-code/qwen-code","version":"1.0.0","bin":{"qwen":"cli.js"}})");
    atomic_write(search / "odd/path/node_modules/@qwen-code/qwen-code/cli.js",
                 "#!/usr/bin/env node\n// fixture");
    atomic_write(search / "unverified/amp.cmd", "@echo off\r\necho unrelated script\r\n");
    auto found = search_agent_files(adapters.catalog, {search}, cancel);
    check(path(found["cursor"]["desktop"]) == portable / "Cursor.exe",
          "disk search finds branded portable app at arbitrary depth");
    check(path(found["qwen"]["cli"]) == search / "odd/path/qwen.cmd",
          "disk search finds CLI outside known folders");
    check(!found.contains("amp"), "disk search does not treat an unrelated same-name script as installed");
    auto mock = search / "unverified/gemini.cmd";
    atomic_write(mock, "@echo off\r\nnode \"%~dp0node_modules\\@google\\gemini-cli\\cli.js\" %*\r\n");
    atomic_write(mock.parent_path() / "node_modules/@google/gemini-cli/package.json",
                 R"({"name":"@google/gemini-cli","bin":{"gemini":"cli.js"}})");
    atomic_write(mock.parent_path() / "node_modules/@google/gemini-cli/cli.js", "// empty test fixture");
    check(!verified_disk_cli("gemini", mock), "incomplete mock npm package is not an installed CLI");
    st.setting("agent_location:gemini", {{"cli", pathstr(mock)}});
    check(adapters.executable("gemini") != mock, "cached automatic candidates are revalidated");
    st.setting("agent_location:gemini", nullptr);
    adapters.search_installations(cancel, {search});
    check(adapters.desktop_executable("cursor") == base / "Cursor.exe",
          "automatic search preserves explicit manual location");
    cancel = true;
    check(search_agent_files(adapters.catalog, {search}, cancel).empty(),
          "pre-cancelled disk search does not traverse");
    cancel = false;
    auto cachedSize = st.stats()["data_bytes"];
    atomic_write(st.root / "storage-test/sample.bin", std::string(65536, 's'));
    check(st.stats()["data_bytes"] == cachedSize, "summary reads cached storage rather than traversing disk");
    auto measured = st.measure_storage(cancel);
    check(measured >= 65536 && st.stats().value("storage_ready", false),
          "background measurement publishes actual storage");
    atomic_write(st.root / "storage-test/extra.bin", std::string(32768, 's'));
    check(st.stats()["data_bytes"] == measured, "filesystem changes do not force synchronous summary scans");
    check(st.measure_storage(cancel) >= measured + 32768,
          "explicit background recount updates storage cache");
    auto extension = search / "portable-code/data/extensions/rooveterinaryinc.roo-cline-1.0.0";
    atomic_write(extension / "package.json",
                 R"({"publisher":"RooVeterinaryInc","name":"roo-cline","main":"dist/extension.js"})");
    check(!is_roo_extension(extension), "incomplete extension folder is not an installation");
    atomic_write(extension / "dist/extension.js", "// fixture");
    check(is_roo_extension(extension), "Roo portable extension requires actual executable payload");
    adapters.locate("roo", extension);
    check(adapters.installation("roo").value("installed", false),
          "Roo can be located in a custom extensions directory");
    check(search_agent_files(adapters.catalog, {search}, cancel).contains("roo"),
          "full search finds portable Roo extension without PATH");
    auto preferredExtension = base / "preferred-roo";
    atomic_write(preferredExtension / "package.json",
                 R"({"publisher":"RooVeterinaryInc","name":"roo-cline","main":"dist/extension.js"})");
    atomic_write(preferredExtension / "dist/extension.js", "// fixture");
    adapters.locate("roo", preferredExtension);
    adapters.search_installations(cancel, {search});
    check(path(st.setting("agent_location:roo")["extension"]) == preferredExtension,
          "automatic search preserves explicitly selected extension directory");
    st.setting("agent_location:roo", nullptr);
    auto oldUV = env("UV_TOOL_BIN_DIR"), oldNpmrc = env("NPM_CONFIG_USERCONFIG"),
         oldPrefix = env("AGENTDOCK_TEST_PREFIX");
    struct Restore {
        std::string uv, npmrc, prefix;
        ~Restore() {
            SetEnvironmentVariableW(L"UV_TOOL_BIN_DIR", uv.empty() ? nullptr : wide(uv).c_str());
            SetEnvironmentVariableW(L"NPM_CONFIG_USERCONFIG", npmrc.empty() ? nullptr : wide(npmrc).c_str());
            SetEnvironmentVariableW(L"AGENTDOCK_TEST_PREFIX",
                                    prefix.empty() ? nullptr : wide(prefix).c_str());
            refresh_cli_paths();
        }
    } restore{oldUV, oldNpmrc, oldPrefix};
    auto uvBin = base / "custom-uv-bin";
    atomic_write(uvBin / "agentdock-uv-fixture.cmd", "@echo off\r\nexit /b 0\r\n");
    SetEnvironmentVariableW(L"UV_TOOL_BIN_DIR", uvBin.c_str());
    check(find_exe("agentdock-uv-fixture") == uvBin / "agentdock-uv-fixture.cmd",
          "uv custom tool bin directory is detected");
    auto npmBin = base / "custom-npm-prefix", npmrc = base / "custom.npmrc";
    atomic_write(npmBin / "agentdock-npm-fixture.cmd", "@echo off\r\nexit /b 0\r\n");
    atomic_write(npmrc, "prefix=\"${AGENTDOCK_TEST_PREFIX}\"\n");
    SetEnvironmentVariableW(L"NPM_CONFIG_USERCONFIG", npmrc.c_str());
    SetEnvironmentVariableW(L"AGENTDOCK_TEST_PREFIX", npmBin.c_str());
    check(find_exe("agentdock-npm-fixture") == npmBin / "agentdock-npm-fixture.cmd",
          "custom npmrc and expanded prefix locate CLI");
    auto oldQwen = env("QWEN_INSTALL_BIN_DIR");
    auto qwenBin = base / "standalone-qwen-bin";
    atomic_write(qwenBin / "agentdock-qwen-fixture.cmd", "@echo off\r\nexit /b 0\r\n");
    SetEnvironmentVariableW(L"QWEN_INSTALL_BIN_DIR", qwenBin.c_str());
    auto qwenFound = find_exe("agentdock-qwen-fixture");
    SetEnvironmentVariableW(L"QWEN_INSTALL_BIN_DIR", oldQwen.empty() ? nullptr : wide(oldQwen).c_str());
    check(qwenFound == qwenBin / "agentdock-qwen-fixture.cmd",
          "Qwen official standalone custom binary directory is detected before PATH refresh");
    st.setting("agent_location:cursor", nullptr);
    st.setting("agent_location:gemini", nullptr);
    st.setting("agent_location:qwen", nullptr);
    std::cout << "Discovery coverage: custom roots, registration fallbacks, CLI/desktop separation, "
                 "portable search, cancellation, cached storage\n";
    return passed;
}

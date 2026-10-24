#include "discovery.hpp"
#include "official_install.hpp"
#include <cstring>
using namespace ad;
int compatibility_tests(Store &store, Models &, Adapters &adapters, const fs::path &root) {
    int count = 0;
    auto check = [&](bool value, const char *label) {
        if (!value)
            throw std::runtime_error(std::string("FAIL compatibility: ") + label);
        ++count;
    };
    auto host = J{{"architecture", "x64"}, {"major", 10}, {"minor", 0}, {"build", 22631}, {"avx", true}};
    auto base = root / "compatibility";
    auto pe = [&](WORD machine) {
        std::string bytes(1024, '\0');
        IMAGE_DOS_HEADER dos{};
        dos.e_magic = IMAGE_DOS_SIGNATURE;
        dos.e_lfanew = 128;
        std::memcpy(bytes.data(), &dos, sizeof dos);
        IMAGE_NT_HEADERS64 nt{};
        nt.Signature = IMAGE_NT_SIGNATURE;
        nt.FileHeader.Machine = machine;
        nt.FileHeader.NumberOfSections = 1;
        nt.FileHeader.SizeOfOptionalHeader = sizeof nt.OptionalHeader;
        nt.FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE;
        nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt.OptionalHeader.MajorSubsystemVersion = 6;
        std::memcpy(bytes.data() + 128, &nt, sizeof nt);
        return bytes;
    };
    auto exe = base / "claude.exe";
    atomic_write(exe, "#!/bin/sh\necho native binary not installed\n");
    check(binary_compatibility(exe, host)["code"] == "invalid-pe",
          "shell placeholder is not a Windows executable");
    atomic_write(exe, "MZtruncated");
    check(!binary_compatibility(exe, host)["compatible"].get<bool>(),
          "MZ alone never proves executable validity");
    auto data = pe(IMAGE_FILE_MACHINE_AMD64);
    atomic_write(exe, data);
    check(binary_compatibility(exe, host)["compatible"], "complete x64 PE header accepted");
    atomic_write(exe, pe(IMAGE_FILE_MACHINE_ARM64));
    check(binary_compatibility(exe, host)["code"] == "architecture", "ARM download rejected on x64 Windows");
    auto arm = host;
    arm["architecture"] = "arm64";
    check(binary_compatibility(exe, arm)["compatible"], "ARM native image accepted on ARM Windows");
    atomic_write(exe, data);
    check(binary_compatibility(exe, arm)["compatible"], "Windows 11 ARM supports x64 emulation");
    arm["build"] = 19045;
    check(!binary_compatibility(exe, arm)["compatible"].get<bool>(),
          "Windows 10 ARM never assumes x64 emulation");
    auto old = host;
    old["build"] = 17134;
    check(agent_compatibility("claude", exe, old)["code"] == "unsupported-host",
          "Claude minimum Windows release enforced");
    old = host;
    old["avx"] = false;
    check(!agent_compatibility("claude", exe, old)["repairable"].get<bool>(),
          "unsupported CPU cannot be repaired by arbitrary downgrade");
    auto oldArm = host;
    oldArm["architecture"] = "arm64";
    check(agent_compatibility("claude", exe, oldArm)["code"] == "architecture",
          "older Windows ARM uses native Claude rather than unsupported x64 CPU emulation");
    IMAGE_SECTION_HEADER section{};
    section.PointerToRawData = 1000;
    section.SizeOfRawData = 1000;
    std::memcpy(data.data() + 128 + sizeof(IMAGE_NT_HEADERS64), &section, sizeof section);
    atomic_write(exe, data);
    check(binary_compatibility(exe, host)["code"] == "invalid-pe", "truncated raw section rejected");
    data = pe(IMAGE_FILE_MACHINE_AMD64);
    WORD newer = 11;
    std::memcpy(data.data() + 128 + 24 + 48, &newer, sizeof newer);
    atomic_write(exe, data);
    check(binary_compatibility(exe, host)["code"] == "windows-version",
          "PE subsystem OS requirement checked");
    auto npm = base / "npm";
    atomic_write(npm / "claude.cmd",
                 "@echo off\r\n\"%~dp0node_modules/@anthropic-ai/claude-code/bin/claude.exe\" %*\r\n");
    atomic_write(
        npm / "node_modules/@anthropic-ai/claude-code/package.json",
        R"({"name":"@anthropic-ai/claude-code","version":"2.1.309","bin":{"claude":"bin/claude.exe"}})");
    atomic_write(npm / "node_modules/@anthropic-ai/claude-code/bin/claude.exe", "#!/bin/sh\necho missing\n");
    auto health = agent_compatibility("claude", npm / "claude.cmd", host);
    check(health["code"] == "invalid-pe" && health["repairable"],
          "valid npm shim cannot conceal broken native payload");
    auto previous = store.setting("agent_location:claude");
    store.setting("agent_location:claude", {{"cli", pathstr(npm / "claude.cmd")}, {"manual_cli", true}});
    bool rejected = false;
    try {
        adapters.launch_plan("claude");
    } catch (...) {
        rejected = true;
    }
    check(rejected, "launch blocks invalid payload before Windows error dialog");
    check(adapters.installation("claude")["repairable"], "broken install exposes inline repair action");
    auto plan = adapters.installation_plan("claude");
    check(plan["args"].dump().find("--include=optional") != std::string::npos &&
              plan["args"].dump().find("--ignore-scripts=false") != std::string::npos,
          "repair overrides skipped native dependencies and postinstall");
    check(std::find(plan["args"].begin(), plan["args"].end(),
                    J("--allow-scripts=@anthropic-ai/claude-code")) != plan["args"].end(),
          "new npm script policy allows only the requested official package");
    check(plan["env"]["npm_config_arch"] == windows_platform()["architecture"] &&
              plan["env"]["npm_config_cpu"] == windows_platform()["architecture"] &&
              plan["env"]["npm_config_os"] == "win32" &&
              plan["args"].dump().find("--os=win32") != std::string::npos,
          "npm overrides stale host architecture settings for this command only");
    check(std::find(plan["args"].begin(), plan["args"].end(), J(pathstr(npm))) != plan["args"].end(),
          "repair targets the detected npm installation prefix");
    store.setting("agent_location:claude", previous);
    auto cliPath = env("PATH");
    auto bad = base / "wrong-architecture", good = base / "working-cli";
    atomic_write(bad / "claude.exe", pe(IMAGE_FILE_MACHINE_ARM64));
    atomic_write(good / "claude.cmd", "@echo off\r\nexit /b 0\r\n");
    store.setting("agent_location:claude", nullptr);
    SetEnvironmentVariableW(L"PATH", (bad.wstring() + L";" + good.wstring() + L";" + wide(cliPath)).c_str());
    check(adapters.executable("claude").lexically_normal() == (good / "claude.cmd").lexically_normal(),
          "compatible CLI later in PATH bypasses wrong-architecture entry");
    SetEnvironmentVariableW(L"PATH", wide(cliPath).c_str());
    auto native = adapters.home / ".local/bin/claude.exe";
    atomic_write(native, pe(IMAGE_FILE_MACHINE_AMD64));
    store.setting("agent_location:claude", {{"cli", pathstr(native)}, {"manual_cli", true}});
    auto nativePlan = adapters.installation_plan("claude");
    check(nativePlan["method"] == "claude-native" && nativePlan["env"]["PROCESSOR_ARCHITECTURE"] == "AMD64",
          "official native installer selects actual OS architecture");
    auto removePlan = adapters.installation_plan("claude", true);
    check(removePlan["method"] == "claude-native-remove" &&
              path(removePlan["env"]["AGENTDOCK_CLAUDE_BIN"]).lexically_normal() == native.lexically_normal(),
          "native uninstall targets only confirmed official binary and version directory");
    store.setting("agent_location:claude", previous);
    wchar_t own[32768]{};
    GetModuleFileNameW(nullptr, own, 32768);
    auto oldNode = base / "old-node.exe";
    fs::copy_file(fs::path(own).parent_path() / "discovery_fixture.exe", oldNode);
    check(node_compatibility("qwen", oldNode, host)["code"] == "node-version",
          "Node version read from PE resource without executing it");
    auto oldModules = env("PSModulePath");
    SetEnvironmentVariableW(L"PSModulePath", (base / "no-modules").c_str());
    auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
    std::atomic<bool> cancel{false};
    try {
        run_install_plan(
            {{"tool", pathstr(ps)},
             {"args",
              J::array({"-NoProfile", "-Command",
                        "Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility') -Force; "
                        "if((Get-FileHash -LiteralPath (Join-Path $PSHOME 'powershell.exe')).Hash.Length -ne "
                        "64){exit 2}; exit 0"})},
             {"env", J::object()}},
            base / "modules", cancel);
    } catch (...) {
        SetEnvironmentVariableW(L"PSModulePath", wide(oldModules).c_str());
        throw;
    }
    SetEnvironmentVariableW(L"PSModulePath", wide(oldModules).c_str());
    check(true, "native installer restores Windows PowerShell module availability");
    auto isolated =
        adapters.installation_plan("claude", false, fs::absolute(store.root / "installation-tests/claude"));
    check(isolated["method"] == "npm", "isolated test never invokes native system installer");
    check(windows_platform().value("build", 0) > 0,
          "real Windows version queried without manifest virtualization");
    check(windows_platform().value("architecture_source", "") == "IsWow64Process2",
          "native OS architecture is distinguished from an emulated manager process");
    struct RestoreScope {
        ~RestoreScope() {
            SetEnvironmentVariableW(L"AGENTDOCK_TEST_SCOPES", L"1");
        }
    } restoreScope;
    SetEnvironmentVariableW(L"AGENTDOCK_TEST_SCOPES", nullptr);
    check(!allowed_agent_path(exe), "production rejects abandoned compatibility test executables");
    check(!agent_compatibility("claude", exe, host).value("compatible", true),
          "test executable cannot become a production launch target");
    check(!valid_agent_executable("claude", npm / "claude.cmd"),
          "test npm shim cannot appear as an installed CLI");
    store.setting("agent_location:claude", {{"cli", pathstr(exe)}, {"manual_cli", true}});
    check(adapters.executable("claude") != exe,
          "manual path cache cannot bypass production fixture isolation");
    check(store.setting("agent_location:claude").value("cli", "").empty(),
          "fixture path is cleared without changing real program files or user settings");
    store.setting("agent_location:claude", previous);
    auto productionPath = fs::path(own).parent_path() / "discovery_fixture.exe";
    check(!allowed_agent_path(productionPath),
          "PE fixture identity is blocked even outside temporary test directories");
    check(allowed_agent_path(ps), "real Windows tools remain available in production");
    auto workspace = fs::path(BRAND_SOURCE_FILE).parent_path().parent_path().parent_path();
    auto pathTestRoot = workspace / ".development/path-recognition" / wide(uuid());
    auto realProgram = pathTestRoot / path("自定义安装/powershell.exe");
    fs::create_directories(realProgram.parent_path());
    fs::copy_file(ps, realProgram);
    check(allowed_agent_path(realProgram.parent_path()),
          "development folder name or old ancestor marker cannot hide real installations");
    check(allowed_agent_path(realProgram) &&
              binary_compatibility(realProgram, windows_platform()).value("compatible", false),
          "real Windows executable remains discoverable beneath development directories");
    auto markedScript = realProgram.parent_path() / "agent.cmd";
    atomic_write(markedScript, "@echo off\r\nrem AGENTDOCK_SYNTHETIC_TEST_FIXTURE\r\nexit /b 0\r\n");
    check(!allowed_agent_path(markedScript),
          "only explicitly marked synthetic CLI is filtered in a shared development directory");
    auto ordinaryScript = realProgram.parent_path() / "custom-agent.cmd";
    atomic_write(ordinaryScript, "@echo off\r\nexit /b 0\r\n");
    check(allowed_agent_path(ordinaryScript), "unmarked user launch scripts remain supported");
    auto extension = pathTestRoot / "roo";
    atomic_write(extension / "dist/extension.js", "// regression test");
    auto metadata =
        J{{"publisher", "RooVeterinaryInc"}, {"name", "roo-cline"}, {"main", "dist/extension.js"}};
    atomic_write(extension / "package.json", metadata.dump());
    check(is_roo_extension(extension), "unmarked extension is not rejected by its development directory");
    metadata["agentdockSyntheticFixture"] = true;
    atomic_write(extension / "package.json", metadata.dump());
    check(!is_roo_extension(extension),
          "explicit synthetic extension is filtered without hiding nearby real programs");
    if (inside(pathTestRoot, workspace / ".development/path-recognition"))
        fs::remove_all(pathTestRoot);
    std::atomic<bool> noCancel{false};
    check(search_agent_files(adapters.catalog, {base}, noCancel).empty(),
          "production disk scans cannot restore previously filtered test paths");
    return count;
}

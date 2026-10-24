#include "discovery.hpp"
#include "official_install.hpp"
#include "removal.hpp"
using namespace ad;
int removal_tests(const fs::path &root) {
    (void)root;
    auto workspace = fs::path(BRAND_SOURCE_FILE).parent_path().parent_path().parent_path();
    auto base = workspace / ".development/removal-tests" / wide(uuid()), home = base / "home";
    int count = 0;
    auto check = [&](bool value, const char *message) {
        if (!value)
            throw std::runtime_error(std::string("FAIL removal: ") + message + " [" + display_path(base) +
                                     "]");
        ++count;
    };
    Store store(base / "data");
    Models models(store);
    Adapters adapters(store, models, home);
    auto cli = home / "AppData/Local/cursor-agent";
    auto createCursor = [&] {
        atomic_write(cli / "agent.cmd", "@echo off\r\nrem AGENTDOCK_SYNTHETIC_TEST_FIXTURE\r\nexit /b 0\r\n");
        atomic_write(cli / "cursor-agent.cmd", "@echo off\r\nrem AGENTDOCK_SYNTHETIC_TEST_FIXTURE\r\nrem "
                                               "cursor-agent versions\r\nexit /b 0\r\n");
        for (auto version : {"1.0.0", "1.0.0.previous", "1.0.1.new"})
            atomic_write(cli / "versions" / version / "payload.bin", "installed-program");
        adapters.locate("cursor", cli / "agent.cmd");
    };
    createCursor();
    atomic_write(home / ".cursor/cli-config.json", "{\"model\":\"retained\"}");
    atomic_write(home / ".cursor/skills/own/SKILL.md", "retain this skill");
    atomic_write(home / ".cursor/worktrees/repo/source.cpp", "uncommitted code");
    atomic_write(home / "AppData/Roaming/Cursor/User/settings.json", "{}");
    check(adapters.installation("cursor")["can_uninstall"],
          "official Cursor CLI is removable without registry uninstaller");
    store.setting("project", pathstr(cli));
    bool programOverlapRejected = false;
    try {
        adapters.installation_plan("cursor", true);
    } catch (...) {
        programOverlapRejected = true;
    }
    check(programOverlapRejected && fs::exists(cli / "agent.cmd"),
          "ordinary uninstall also protects a project inside its program target");
    store.setting("project", nullptr);
    adapters.manage_installation("cursor", true);
    check(!fs::exists(cli), "Cursor launchers and old/staged versions are fully removed");
    check(fs::is_regular_file(home / ".cursor/skills/own/SKILL.md"),
          "ordinary uninstall preserves Skill and settings");
    check(adapters.executable("cursor") != cli / "agent.cmd" &&
              adapters.executable("cursor") != cli / "cursor-agent.cmd",
          "removed CLI is not rediscovered from saved path");
    check(adapters.installation("cursor").value("user_data_removable", false),
          "retained user data exposes cleanup action");
    store.db.run("INSERT INTO deployments VALUES(?,?,?,?,1)",
                 {"fixture-skill", "cursor", "fixture-path", "hash"});
    adapters.manage_installation("cursor", true, {}, true);
    check(store.db.query("SELECT active FROM deployments WHERE agent='cursor'")[0]["active"] == 0,
          "full cleanup clears stale deployment state without removing source skills");
    check(!fs::exists(home / ".cursor/skills") && !fs::exists(home / ".cursor/cli-config.json"),
          "opt-in cleanup clears Cursor user data");
    check(fs::is_regular_file(home / ".cursor/worktrees/repo/source.cpp"),
          "cleanup preserves worktrees and source files");
    check(!fs::exists(home / "AppData/Roaming/Cursor"), "opt-in cleanup removes editor user data and caches");
    createCursor();
    fs::remove(cli / "agent.cmd");
    fs::remove(cli / "cursor-agent.cmd");
    check(adapters.installation("cursor")["can_uninstall"],
          "partially removed official CLI remains cleanable");
    adapters.manage_installation("cursor", true);
    check(!fs::exists(cli), "partial Cursor installation residue is removed");
    wchar_t own[32768]{};
    GetModuleFileNameW(nullptr, own, 32768);
    auto fixture = fs::path(own).parent_path() / "discovery_fixture.exe";
    auto createDesktop = [&](const fs::path &exe) {
        fs::create_directories(exe.parent_path());
        fs::copy_file(fixture, exe, fs::copy_options::overwrite_existing);
        atomic_write(exe.parent_path() / "resources/app/product.json",
                     R"({"nameShort":"Cursor","applicationName":"cursor"})");
        atomic_write(exe.parent_path() / "resources/app.bin", "program residue");
    };
    auto desktop = home / "Apps/Arbitrary editor folder/Cursor.exe";
    createDesktop(desktop);
    atomic_write(desktop.parent_path() / "data/user-data/config.json", "portable data");
    adapters.locate("cursor", desktop);
    check(adapters.installation("cursor")["can_uninstall"],
          "unregistered Cursor desktop in arbitrary folder enables uninstall");
    auto desktopPlan = adapters.installation_plan("cursor", true);
    check(desktopPlan["steps"].empty() && !desktopPlan["cleanup"]["files"].empty(),
          "missing registry and uninstaller use verified desktop payload removal");
    adapters.manage_installation("cursor", true);
    check(!fs::exists(desktop) && !fs::exists(desktop.parent_path() / "resources"),
          "unregistered desktop is removed through full adapter workflow");
    check(fs::exists(desktop.parent_path() / "data/user-data/config.json"),
          "ordinary desktop cleanup preserves portable user data");
    createDesktop(desktop);
    adapters.locate("cursor", desktop);
    adapters.manage_installation("cursor", true, {}, true);
    check(!fs::exists(desktop.parent_path()),
          "thorough desktop cleanup removes empty dedicated installation directory");
    auto shared = home / "Shared/Tools/Cursor.exe";
    atomic_write(shared, "unverified filename is not an application identity");
    check(cursor_desktop_layout(shared).empty(), "unverified Cursor filename cannot authorize removal");
    createDesktop(shared);
    atomic_write(shared.parent_path() / "project/source.cpp", "keep source");
    atomic_write(shared.parent_path() / "notes.txt", "keep notes");
    atomic_write(shared.parent_path() / "OtherEditor.exe", "keep sibling app");
    auto sharedPlan = removal_files("cursor", home, {}, {}, false);
    append_cursor_desktop_files(sharedPlan, shared);
    execute_removal_files(sharedPlan);
    check(!fs::exists(shared) && fs::exists(shared.parent_path() / "project/source.cpp") &&
              fs::exists(shared.parent_path() / "notes.txt") &&
              fs::exists(shared.parent_path() / "OtherEditor.exe"),
          "verified desktop cleanup preserves unrelated files in shared parent");
    auto staged = home / "Apps/Updater interrupted/_/Cursor.exe";
    createDesktop(staged);
    auto installerRoot = staged.parent_path().parent_path();
    fs::copy_file(fixture, installerRoot / "unins000.exe");
    atomic_write(installerRoot / "unins000.dat", "isolated uninstall-record fixture; never executed");
    adapters.locate("cursor", staged);
    auto stagedPlan = adapters.installation_plan("cursor", true);
    check(stagedPlan["steps"].size() == 1 &&
              path(stagedPlan["steps"][0]["tool"]) == installerRoot / "unins000.exe",
          "interrupted updater layout finds parent uninstaller without registry");
    fs::remove(installerRoot / "unins000.dat");
    check(cursor_desktop_layout(staged).value("uninstaller", "").empty(),
          "uninstaller without its Inno data is never invoked");
    fs::remove(installerRoot / "unins000.exe");
    fs::copy_file(fixture, staged.parent_path() / "unins000.exe");
    atomic_write(staged.parent_path() / "unins000.dat", "isolated fixture; never executed");
    auto nativePlan = adapters.installation_plan("cursor", true);
    check(nativePlan["steps"].size() == 1 && nativePlan["steps"][0]["method"] == "native-uninstall",
          "on-disk uninstaller is preferred when Windows Installed Apps entry is missing");
    check(nativePlan["steps"][0]["args"] == J::array({"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"}),
          "recovered Inno uninstall is silent and cannot restart Windows");
    fs::remove(staged.parent_path() / "unins000.exe");
    fs::remove(staged.parent_path() / "unins000.dat");
    atomic_write(staged.parent_path() / "resources/app/product.json",
                 R"({"nameShort":"Other editor","applicationName":"other"})");
    check(cursor_desktop_layout(staged).empty(), "mismatched product manifest rejects fallback cleanup");
    bool refused = false;
    try {
        adapters.installation_plan("cursor", true);
    } catch (...) {
        refused = true;
    }
    check(refused && fs::exists(staged), "unverified bundle remains untouched");
    store.setting("agent_location:cursor", nullptr);
    auto legacySource = home / "legacy-source";
    atomic_write(legacySource / "cli.cmd", "@echo off\r\nexit /b 0\r\n");
    adapters.install("cursor", legacySource, "cli.cmd");
    check(adapters.installation_plan("cursor", true)["method"] == "verified-removal",
          "legacy manager-owned install uses the same optional-data cleanup confirmation");
    adapters.manage_installation("cursor", true, {}, true);
    check(!fs::exists(adapters.managed_root("cursor")) && fs::exists(legacySource / "cli.cmd"),
          "legacy cleanup removes only the owned installation and preserves import source");
    auto extensions = home / ".vscode/extensions";
    auto current = extensions / "rooveterinaryinc.roo-cline-3.1.0";
    auto older = extensions / "rooveterinaryinc.roo-cline-3.0.0";
    auto otherEditor = home / ".cursor/extensions/rooveterinaryinc.roo-cline-3.2.0";
    auto createExtension = [&](const fs::path &p, bool complete = true) {
        atomic_write(
            p / "package.json",
            R"({"publisher":"RooVeterinaryInc","name":"roo-cline","main":"dist/extension.js","agentdockSyntheticFixture":true})");
        if (complete)
            atomic_write(p / "dist/extension.js", "// extension fixture");
    };
    createExtension(current);
    createExtension(older, false);
    createExtension(otherEditor);
    auto unrelated = extensions / "other.extension-1.0.0";
    atomic_write(unrelated / "package.json", R"({"publisher":"other","name":"extension"})");
    atomic_write(
        extensions / "extensions.json",
        R"([{"identifier":{"id":"RooVeterinaryInc.roo-cline"}},{"identifier":{"id":"other.extension"}}])");
    atomic_write(extensions / ".obsolete",
                 R"({"rooveterinaryinc.roo-cline-3.0.0":true,"other.extension-1.0.0":true})");
    auto user = home / "AppData/Roaming/Code/User";
    atomic_write(user / "globalStorage/rooveterinaryinc.roo-cline/tasks/one/history.json",
                 "private Roo history");
    atomic_write(user / "globalStorage/other.extension/data.json", "keep other extension data");
    atomic_write(user / "settings.json",
                 "{\n// keep this comment\n\"editor.fontSize\": 15,\n\"roo-cline.customStoragePath\": "
                 "\"\",\n\"roo-cline.autoImportSettingsPath\": \"fixture\",\n}\n");
    auto cache = user.parent_path() / "CachedExtensionVSIXs";
    fs::create_directories(cache);
    auto ownArchive = cache / "rooveterinaryinc.roo-cline-3.0.0.vsix";
    auto unrelatedArchive = cache / "rooveterinaryinc.roo-cline-lookalike.vsix";
    auto ps = path(env("SystemRoot")) / "System32/WindowsPowerShell/v1.0/powershell.exe";
    std::atomic<bool> cancel{false};
    auto createZip = [&](const fs::path &file, const std::string &manifest) {
        run_install_plan(
            {{"tool", pathstr(ps)},
             {"args",
              J::array(
                  {"-NoProfile", "-Command",
                   "$ErrorActionPreference='Stop'; Add-Type -AssemblyName "
                   "System.IO.Compression,System.IO.Compression.FileSystem; "
                   "$z=[IO.Compression.ZipFile]::Open($env:ZIPFILE,[IO.Compression.ZipArchiveMode]::Create); "
                   "try{$e=$z.CreateEntry('extension/package.json'); $w=[IO.StreamWriter]::new($e.Open()); "
                   "try{$w.Write($env:MANIFEST_JSON)}finally{$w.Dispose()}}finally{$z.Dispose()}; exit 0"})},
             {"env", {{"ZIPFILE", pathstr(file)}, {"MANIFEST_JSON", manifest}}}},
            base / wide(uuid()), cancel);
    };
    createZip(ownArchive, R"({"publisher":"RooVeterinaryInc","name":"roo-cline"})");
    createZip(unrelatedArchive, R"({"publisher":"other","name":"extension"})");
    for (auto name : {"state.vscdb", "state.vscdb.backup"}) {
        DB db(user / "globalStorage" / name);
        db.exec("CREATE TABLE ItemTable(key TEXT PRIMARY KEY,value BLOB)");
        db.run("INSERT INTO ItemTable VALUES(?,?)", {"rooveterinaryinc.roo-cline", "private-state"});
        db.run("INSERT INTO ItemTable VALUES(?,?)",
               {R"(secret://{"extensionId":"RooVeterinaryInc.roo-cline","key":"apiKey"})", "encrypted"});
        db.run("INSERT INTO ItemTable VALUES(?,?)",
               {R"(secret://{"extensionId":"other.extension","key":"rooveterinaryinc.roo-cline"})",
                "keep-encrypted"});
    }
    adapters.locate("roo", current);
    check(adapters.installation("roo")["can_uninstall"],
          "Roo can be removed independently of host CLI availability");
    adapters.manage_installation("roo", true);
    check(!fs::exists(ownArchive) && fs::is_regular_file(unrelatedArchive),
          "VSIX cache cleanup verifies embedded package identity before deleting");
    check(!fs::exists(current) && !fs::exists(older) && !fs::exists(otherEditor),
          "all Roo versions and incomplete packages are removed across editors");
    check(fs::is_regular_file(unrelated / "package.json"), "other extensions are untouched");
    check(J::parse(read(extensions / "extensions.json")).size() == 1 &&
              J::parse(read(extensions / ".obsolete")).size() == 1,
          "only Roo extension registry entries are removed");
    check(fs::exists(user / "globalStorage/rooveterinaryinc.roo-cline"),
          "ordinary Roo uninstall preserves task history");
    check(!adapters.installation("roo")["installed"].get<bool>(),
          "obsolete Roo versions no longer cause false installation detection");
    auto custom = home / "custom-roo-storage";
    for (auto folder : {"tasks", "settings", "cache"})
        atomic_write(custom / folder / "data.json", "Roo data");
    atomic_write(custom / "source.cpp", "unrelated source file");
    atomic_write(user / "settings.json",
                 patch_json(read(user / "settings.json"), "roo-cline.customStoragePath", pathstr(custom)));
    store.setting("project", pathstr(custom));
    bool overlapRejected = false;
    try {
        adapters.installation_plan("roo", true, {}, true);
    } catch (...) {
        overlapRejected = true;
    }
    check(overlapRejected && fs::exists(custom / "source.cpp"),
          "data cleanup refuses overlap with the current project");
    store.setting("project", nullptr);
    adapters.manage_installation("roo", true, {}, true);
    check(!fs::exists(custom / "tasks") && !fs::exists(custom / "settings") &&
              !fs::exists(custom / "cache") && fs::exists(custom / "source.cpp"),
          "custom Roo storage removes only official data subdirectories");
    check(!fs::exists(user / "globalStorage/rooveterinaryinc.roo-cline"),
          "full cleanup removes Roo history and settings folder");
    check(fs::exists(user / "globalStorage/other.extension/data.json"),
          "full cleanup preserves other extensions' user data");
    for (auto name : {"state.vscdb", "state.vscdb.backup"}) {
        DB db(user / "globalStorage" / name);
        check(db.query("SELECT key FROM ItemTable").size() == 1,
              "shared database removes only exact Roo state and secret keys including backup");
    }
    auto settings = read(user / "settings.json");
    check(parse_jsonc(settings)["editor.fontSize"] == 15 &&
              !parse_jsonc(settings).contains("roo-cline.autoImportSettingsPath") &&
              settings.find("keep this comment") != std::string::npos,
          "JSONC cleanup preserves unrelated values and comments");
    for (auto source : {"{\"a\":1,\"remove\":2}", "{\"remove\":2,\"a\":1,}", "{\"remove\":2}"})
        check(!parse_jsonc(erase_json_member(source, "remove")).contains("remove"),
              "JSONC member deletion preserves valid commas");
    createExtension(current);
    HANDLE locked = CreateFileW((current / "dist/extension.js").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    bool rejected = false;
    try {
        execute_removal_files(removal_files("roo", home, {}, current, false));
    } catch (...) {
        rejected = true;
    }
    CloseHandle(locked);
    check(rejected && fs::is_regular_file(current / "package.json"),
          "locked-file removal reports failure and preserves manifest for retry");
    adapters.locate("roo", current);
    adapters.manage_installation("roo", true);
    check(!fs::exists(current), "retry after releasing lock completes cleanup");
    auto fake = home / ".vscode/extensions/rooveterinaryinc.roo-cline-unrelated";
    atomic_write(fake / "package.json", R"({"publisher":"other","name":"extension"})");
    execute_removal_files(removal_files("roo", home, {}, {}, false));
    check(fs::is_regular_file(fake / "package.json"),
          "lookalike folder names never authorize deleting another package");
    createExtension(current);
    auto protectedSource = base / "protected-source", junction = current / "linked-project";
    atomic_write(protectedSource / "source.cpp", "protected code");
    run_install_plan({{"tool", pathstr(ps)},
                      {"args", J::array({"-NoProfile", "-Command",
                                         "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path "
                                         "$env:LINK_PATH -Target $env:TARGET_PATH | Out-Null; exit 0"})},
                      {"env", {{"LINK_PATH", pathstr(junction)}, {"TARGET_PATH", pathstr(protectedSource)}}}},
                     base / "junction", cancel);
    bool junctionRejected = false;
    try {
        execute_removal_files(removal_files("roo", home, {}, current, false));
    } catch (...) {
        junctionRejected = true;
    }
    check(junctionRejected && read(protectedSource / "source.cpp") == "protected code" &&
              fs::exists(current / "package.json"),
          "junction escape is rejected before any program or project files are deleted");
    check(RemoveDirectoryW(junction.c_str()) != FALSE,
          "fixture junction removed without traversing its target");
    return count;
}

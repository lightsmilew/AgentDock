#include "core.hpp"
#define TOML_EXCEPTIONS 1
#include "toml.hpp"
using namespace ad;
int adapter_tests(Store &store, Models &models, Adapters &adapters, const fs::path &root) {
    int count = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok)
            throw std::runtime_error(why);
        ++count;
    };
    auto rejects = [&](auto action, const char *why) {
        bool rejected = false;
        try {
            action();
        } catch (...) {
            rejected = true;
        }
        check(rejected, why);
    };
    auto json = std::string("{\n// preserve comment\n\"editor\":{\"font\":14},\n\"model\":\"old\",\n}\n");
    auto edited = patch_json(json, "model", "new");
    check(parse_jsonc(edited)["model"] == "new" && edited.find("// preserve comment") != std::string::npos,
          "JSONC value patch preserves comments");
    edited = patch_json(edited, "next", J{{"nested", J::array({true, 1, "é"})}});
    check(parse_jsonc(edited)["next"]["nested"][0] == true && parse_jsonc(edited)["editor"]["font"] == 14,
          "JSONC insertion after trailing comma");
    check(parse_jsonc(patch_json("{}", "model", "a"))["model"] == "a", "JSONC empty object insertion");
    auto yaml = std::string(
        "# 保留中文注释\n- id: other\n  config:\n    enabled: true\n    retries: 7\n- id: llm-pi-ai\n  "
        "config:\n    flag: false\n    providers:\n      original:\n        models: [{id: keep}]\n      "
        "agentdock-route:\n        timeout: 99\n        models:\n          - id: before\n- id: "
        "agent-default-model\n  config:\n    provider: original\n    model: keep\n");
    J route = {{"baseURL", "https://gateway.example/v1"},
               {"api", "openai-completions"},
               {"apiKeyEnv", "TEST_KEY"},
               {"models", J::array({{{"id", "alpha"}}, {{"id", "beta"}}})}};
    auto patched = patch_harness(yaml, "agentdock-route", route);
    patched = patch_harness_default(patched, "agentdock-route", "beta");
    check(patched.find("enabled: true") != std::string::npos &&
              patched.find("retries: 7") != std::string::npos &&
              patched.find("original:") != std::string::npos &&
              patched.find("timeout: 99") != std::string::npos,
          "Harness preserves typed values and unrelated routes");
    check(patch_harness_default(patch_harness(patched, "agentdock-route", route), "agentdock-route",
                                "beta") == patched,
          "Harness repeated apply is idempotent");
    auto hp = adapters.configs("harness")[0];
    atomic_write(hp, patched);
    auto state = adapters.model_state("harness");
    check(state["warnings"].empty() && state["models"].size() == 4,
          "Harness patched catalog and default read correctly");
    rejects([&] { patch_harness("- id: llm-pi-ai\n  config: {providers: []}\n", "x", route); },
            "Harness invalid provider shape rejected");
    auto provider = models.save({{"name", "Adapter fixtures"},
                                 {"vendor", "custom"},
                                 {"base_url", "https://example.test/v1"},
                                 {"protocol", "chat"},
                                 {"model", "alpha"},
                                 {"models", J::array({"alpha", "beta"})},
                                 {"api_key", "fixture-api-key"},
                                 {"context_window", 32768}});
    auto pid = provider["id"].get<std::string>();
    auto zcodeFile = adapters.configs("zcode")[0];
    auto zcodePreview = adapters.preview("zcode", pid);
    check(zcodePreview["can_apply"] &&
              zcodePreview["content"].get<std::string>().find("fixture-api-key") == std::string::npos,
          "ZCode config preview hides native API key");
    adapters.apply(zcodePreview["ticket"]);
    auto zcodeDoc = J::parse(read(zcodeFile));
    auto zcodeRoute = "agentdock-" + pid;
    check(zcodeDoc["schemaVersion"] == 1 &&
              zcodeDoc["config"]["defaultModelSelection"]["providerId"] == zcodeRoute,
          "ZCode writes current versioned personal config and default provider");
    check(
        zcodeDoc["config"]["providerConfigRules"]["providerRules"][0]["config"]["personalModelIds"].size() ==
            2,
        "ZCode writes complete supported model list");
    check(adapters.model_state("zcode")["models"].size() == 2 &&
              adapters.model_state("zcode").dump().find("fixture-api-key") == std::string::npos,
          "ZCode reads configured models without exposing credentials");
    auto defaultPreview = adapters.preview_default("zcode", J::array({zcodeRoute, "beta"}).dump());
    adapters.apply(defaultPreview["ticket"]);
    check(J::parse(read(zcodeFile))["config"]["defaultModelSelection"]["modelId"] == "beta",
          "ZCode default model change writes documented primary runtime schema");
    auto other =
        J{{"providerId", "other"}, {"enabled", true}, {"config", {{"personalModelIds", J::array({"keep"})}}}};
    zcodeDoc["config"]["providerConfigRules"]["providerRules"].push_back(other);
    atomic_write(zcodeFile, zcodeDoc.dump());
    auto again = adapters.preview("zcode", pid);
    adapters.apply(again["ticket"]);
    check(J::parse(read(zcodeFile))["config"]["providerConfigRules"]["providerRules"].size() == 2,
          "ZCode merges its own route without duplicating or removing other providers");
    auto future = zcodeDoc;
    future["schemaVersion"] = 2;
    rejects([&] { merge_zcode(future.dump(), provider, "fixture-api-key"); },
            "ZCode refuses unknown future schemas");
    rejects([&] { select_zcode_model(read(zcodeFile), J::array({"missing", "model"}).dump()); },
            "ZCode rejects selecting a model outside enabled configured providers");
    auto generated = fs::path(BRAND_SOURCE_FILE).parent_path().parent_path().parent_path() /
                     ".development/zcode-research/generated-test-config.json";
    atomic_write(generated, read(zcodeFile));
    auto openCodeJsonc = adapters.configs("opencode")[0].parent_path() / "opencode.jsonc";
    atomic_write(openCodeJsonc, "{\n// keep my UI settings\n\"permission\":{\"bash\":\"ask\"},\n}\n");
    check(adapters.configs("opencode")[0] == openCodeJsonc, "OpenCode uses existing JSONC location");
    auto ocPreview = adapters.preview("opencode", pid);
    adapters.apply(ocPreview["ticket"]);
    check(read(openCodeJsonc).find("// keep my UI settings") != std::string::npos &&
              parse_jsonc(read(openCodeJsonc))["permission"]["bash"] == "ask",
          "OpenCode writes preserve root comments and permissions");
    for (auto id : {"harness", "cline", "factory", "kimi", "roo", "crush", "copilot", "openhands"}) {
        auto preview = adapters.preview(id, pid, "beta");
        check(preview["can_apply"] == true && preview.dump().find("fixture-api-key") == std::string::npos,
              "New adapter preview redacts credentials");
        adapters.apply(preview["ticket"]);
        auto value = adapters.model_state(id);
        check((std::string(id) == "crush" || value["warnings"].empty()) && !value["models"].empty() &&
                  value.dump().find("fixture-api-key") == std::string::npos,
              (std::string("Applied model inventory: ") + id + " " + value["warnings"].dump()).c_str());
        auto again = adapters.preview(id, pid, "alpha");
        adapters.apply(again["ticket"]);
        if (id == std::string("kimi")) {
            auto t = toml::parse(read(adapters.configs(id)[0]));
            check(t["models"].as_table()->size() >= 2, "Kimi repeat merges model tables");
        }
        if (id == std::string("cline")) {
            auto c = parse_jsonc(read(adapters.configs(id)[0]));
            check(c["providers"].size() == 2, "Cline stable routes preserve model list");
        }
    }
    auto anthropic = provider;
    anthropic.erase("id");
    anthropic["protocol"] = "anthropic";
    anthropic["auth"] = "x-api-key";
    anthropic["api_key"] = "fixture-api-key";
    anthropic = models.save(anthropic);
    auto claude = adapters.preview("claude", anthropic["id"], "beta");
    adapters.apply(claude["ticket"]);
    auto settings = parse_jsonc(read(adapters.configs("claude")[0]));
    check(settings["availableModels"].size() >= 2 &&
              settings["env"]["ANTHROPIC_API_KEY"] == "fixture-api-key" &&
              claude.dump().find("fixture-api-key") == std::string::npos,
          "Claude multi-model list and credential routing with redacted preview");
    auto defClaude = adapters.preview_default("claude", "alpha");
    adapters.apply(defClaude["ticket"]);
    check(parse_jsonc(read(adapters.configs("claude")[0]))["env"]["ANTHROPIC_MODEL"] == "alpha",
          "Claude default updates higher-precedence model env too");
    auto clineAnthropic = adapters.preview("cline", anthropic["id"]);
    adapters.apply(clineAnthropic["ticket"]);
    auto routes = parse_jsonc(read(adapters.configs("cline")[0]));
    auto clineRoute = routes["lastUsedProvider"].get<std::string>();
    check(routes["providers"][clineRoute]["settings"]["baseUrl"] == "https://example.test" &&
              models.get(anthropic["id"]).first["base_url"] == "https://example.test/v1",
          "Native Anthropic SDK base URL excludes duplicated v1 without altering service connection");
    auto resetCline = adapters.preview("cline", pid);
    adapters.apply(resetCline["ticket"]);
    models.remove(anthropic["id"]);
    auto codex = provider;
    codex.erase("id");
    codex["protocol"] = "responses";
    codex = models.save(codex);
    auto codex1 = adapters.preview("codex", codex["id"], "beta");
    adapters.apply(codex1["ticket"]);
    auto routeName = "agentdock_" + codex["id"].get<std::string>();
    auto codex2 = codex;
    codex2.erase("id");
    codex2["base_url"] = "https://second.example/v1";
    codex2 = models.save(codex2);
    auto second = adapters.preview("codex", codex2["id"]);
    adapters.apply(second["ticket"]);
    auto selectOriginal = adapters.preview_default("codex", J::array({routeName, "beta"}).dump());
    adapters.apply(selectOriginal["ticket"]);
    auto config = toml::parse(read(adapters.configs("codex")[0]));
    check(config["model_provider"].value_or(std::string()) == routeName &&
              store.setting("agent_binding:codex")["provider"] == codex["id"],
          "Codex default model switches provider and credential binding together");
    auto profile = adapters.configs("codex")[0].parent_path() /
                   ("agentdock_" + codex["id"].get<std::string>().substr(0, 12) + "_" +
                    sha("beta").substr(0, 10) + ".config.toml");
    check(toml::parse(read(profile))["model_provider"].value_or(std::string()) == routeName,
          "Independent Codex profile pins provider");
    models.remove(codex["id"]);
    models.remove(codex2["id"]);
    auto registry = adapters.configs("copilot")[0].parent_path() / "providers.json";
    atomic_write(registry, R"({"providers":{"existing":{}},"models":{}})");
    rejects([&] { adapters.preview("copilot", pid); }, "Copilot native registry precedence is respected");
    fs::remove(registry);
    auto stale = adapters.preview("roo", pid);
    auto codeSettings = root / "home/AppData/Roaming/Code/User/settings.json";
    auto before = read(adapters.configs("roo")[0]);
    atomic_write(codeSettings, read(codeSettings) + "\n// concurrent\n");
    rejects([&] { adapters.apply(stale["ticket"]); }, "Companion configuration stale detection");
    check(read(adapters.configs("roo")[0]) == before, "No first file changed when companion hash mismatches");
    auto cline = adapters.model_state("cline");
    auto selected = cline["models"][1]["selection"].get<std::string>();
    auto def = adapters.preview_default("cline", selected);
    adapters.apply(def["ticket"]);
    check(parse_jsonc(read(adapters.configs("cline")[0]))["lastUsedProvider"] == selected,
          "Default changes real Cline route");
    auto bin = root / "CLI tools & fixtures";
    fs::create_directories(bin);
    auto marker = root / "cli-result.txt";
    atomic_write(bin / "cline.cmd", "@echo off\r\n>\"%AGENTDOCK_TEST_RESULT%\" echo "
                                    "\"%CD%\"\r\n>>\"%AGENTDOCK_TEST_RESULT%\" echo %1\r\nexit /b 0\r\n");
    atomic_write(bin / "agent.cmd", "@echo off\r\nexit /b 0\r\n");
    auto oldPath = env("PATH");
    SetEnvironmentVariableW(L"PATH", (bin.wstring() + L";" + wide(oldPath)).c_str());
    SetEnvironmentVariableW(L"AGENTDOCK_TEST_RESULT", marker.c_str());
    auto project = root / "project's $data & files";
    fs::create_directories(project);
    auto prior = store.setting("project");
    store.setting("project", pathstr(project));
    auto cursorDefault = adapters.preview_default("cursor", "qa-cursor-model");
    adapters.apply(cursorDefault["ticket"]);
    check(!adapters.model_state("cursor")["models"].empty(), "Cursor model launch profile is inventoried");
    check(adapters.launch_plan("cursor")["arguments"] == J::array({"--model", "qa-cursor-model"}),
          "Cursor uses official CLI model argument without guessing private config schema");
    auto plan = adapters.launch_plan("cline");
    check(plan["executable"] == pathstr(bin / "cline.cmd") && plan["arguments"] == J::array({"--tui"}),
          "CLI executable and interactive arguments resolved");
    adapters.launch("cline", "", false);
    auto result = read(marker);
    check(result.starts_with("\"") &&
              fs::equivalent(path(result.substr(1, result.find('"', 1) - 1)), project) &&
              result.find("--tui") != std::string::npos,
          "Actual PowerShell invokes CMD shim with safe working directory and args");
    rejects([&] { adapters.launch_plan("cline", "unapplied"); },
            "CLI does not silently launch unconfigured provider");
    auto editedProvider = provider;
    editedProvider["base_url"] = "https://changed.example/v1";
    models.save(editedProvider);
    check(adapters.launch_plan("cline")["environment"].empty() &&
              adapters.launch_plan("cline").contains("warning"),
          "Edited connection launches native CLI without sending new credentials to old endpoint");
    store.setting("project", pathstr(root / "missing-project"));
    rejects([&] { adapters.launch_plan("cline"); }, "CLI stale working directory rejected");
    store.setting("project", prior);
    SetEnvironmentVariableW(L"PATH", wide(oldPath).c_str());
    SetEnvironmentVariableW(L"AGENTDOCK_TEST_RESULT", nullptr);
    models.remove(pid);
    return count;
}

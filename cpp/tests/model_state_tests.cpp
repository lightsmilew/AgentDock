#include "core.hpp"
#include <iostream>
using namespace ad;
int model_state_tests(Store &store, Models &models, Adapters &adapters, const fs::path &root) {
    int count = 0;
    auto check = [&](bool value, const char *label) {
        if (!value)
            throw std::runtime_error(label);
        ++count;
    };
    auto provider = models.save({{"name", "Multi-model fixture"},
                                 {"vendor", "custom"},
                                 {"base_url", "https://example.org/v1"},
                                 {"protocol", "chat"},
                                 {"model", "alpha"},
                                 {"models", J::array({"alpha", "beta", "beta", " gamma "})},
                                 {"key_env", "AD_TEST_MODELS_KEY"}});
    check(provider["models"] == J::array({"alpha", "beta", "gamma"}),
          "unique model IDs preserve declared order");
    auto id = provider["id"].get<std::string>();
    auto responses = provider;
    responses.erase("id");
    responses["protocol"] = "responses";
    responses = models.save(responses);
    auto preview = adapters.preview("codex", responses["id"], "beta");
    adapters.apply(preview["ticket"]);
    auto configured = adapters.model_state("codex");
    check(configured["models"].size() >= 4 && configured["models"][0]["id"] == "beta",
          "Codex current model and model profiles");
    check(models.get(responses["id"]).first["model"] == "alpha",
          "Agent choice does not change connection default");
    auto qwen = adapters.configs("qwen")[0];
    atomic_write(
        qwen,
        R"({"customSetting":"keep","modelProviders":{"openai":[{"id":"other","baseUrl":"https://other.example/v1","envKey":"OTHER_KEY"}]}})");
    auto qp = adapters.preview("qwen", id, "beta");
    adapters.apply(qp["ticket"]);
    auto q = J::parse(read(qwen));
    check(q["customSetting"] == "keep" && q["modelProviders"]["openai"].size() == 4,
          "Qwen merges all models and preserves existing settings");
    check(q["model"]["name"] == "beta" && q["security"]["auth"]["selectedType"] == "openai",
          "Qwen default and auth route");
    check(adapters.model_state("qwen")["models"].size() == 5,
          "Qwen inventories current and configured models");
    auto oc = adapters.configs("opencode")[0];
    atomic_write(oc, R"({"permission":{"bash":"ask"},"provider":{"existing":{"models":{"old":{}}}}})");
    auto op = adapters.preview("opencode", id, "gamma");
    adapters.apply(op["ticket"]);
    auto o = J::parse(read(oc));
    check(o["permission"]["bash"] == "ask" && o["provider"].contains("existing"),
          "OpenCode preserves unrelated configuration");
    check(adapters.model_state("opencode")["models"].size() == 5,
          "OpenCode inventories all configured models");
    auto cb = adapters.configs("codebuddy")[0];
    atomic_write(cb,
                 R"({"models":[{"id":"old","apiKey":"sensitive-original-key"}],"availableModels":["old"]})");
    auto cp = adapters.preview("codebuddy", id);
    adapters.apply(cp["ticket"]);
    auto c = J::parse(read(cb));
    check(c["models"].size() == 4 && c["models"][0]["apiKey"] == "sensitive-original-key",
          "CodeBuddy merges without rewriting existing model");
    check(c["availableModels"] == J::array({"old", "alpha", "beta", "gamma"}),
          "CodeBuddy retains and extends visible model list");
    auto cbBefore = read(cb);
    auto repeated = adapters.preview("codebuddy", id);
    adapters.apply(repeated["ticket"]);
    check(read(cb) == cbBefore, "CodeBuddy repeated import is idempotent");
    auto state = adapters.model_state("workbuddy");
    check(state["models"].size() == 4 && state.dump().find("sensitive-original-key") == std::string::npos,
          "WorkBuddy legacy inventory never exposes keys");
    auto kimi = adapters.configs("kimi")[0];
    atomic_write(kimi, "default_model = \"alias\"\n[providers.p]\napi_key = "
                       "\"hidden-kimi-key\"\n[models.alias]\nprovider = \"p\"\nmodel = \"test-model\"\n");
    auto ks = adapters.model_state("kimi");
    check(ks["models"].size() == 1 && ks["models"][0]["id"] == "test-model" &&
              ks.dump().find("hidden-kimi-key") == std::string::npos,
          "Kimi TOML model inventory is redacted");
    auto harness = adapters.configs("harness")[0];
    atomic_write(
        harness,
        "- id: llm-pi-ai\n  config:\n    providers:\n      custom:\n        apiKey: private-harness-key\n    "
        "    models:\n          - &model\n            id: first\n          - *model\n");
    auto hs = adapters.model_state("harness");
    check(hs["models"].size() == 2 && hs.dump().find("private-harness-key") == std::string::npos,
          "Harness YAML parser handles aliases without credentials in output");
    atomic_write(harness, "- &loop\n  id: llm-pi-ai\n  config: *loop\n");
    check(!adapters.model_state("harness")["warnings"].empty(),
          "Harness recursive aliases are rejected safely");
    auto factory = adapters.configs("factory")[0];
    atomic_write(
        factory,
        R"({"model":"custom:sample","customModels":[{"model":"sample","provider":"openai","apiKey":"factory-private"}]})");
    auto fs1 = adapters.model_state("factory");
    check(fs1["models"].size() == 2 && fs1.dump().find("factory-private") == std::string::npos,
          "Factory current and custom model inventory");
    auto catalog = resource_json(100);
    for (auto &agent : catalog["agents"])
        check(adapters.model_state(agent["id"]).contains("models"),
              "Every adapter supplies an explicit model inventory state");
    models.remove(id);
    models.remove(responses["id"]);
    return count;
}

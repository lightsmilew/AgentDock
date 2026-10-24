#include "core.hpp"
#define TOML_EXCEPTIONS 1
#include "toml.hpp"
#include "yaml.h"
#include <algorithm>
#include <set>

namespace ad {
namespace {
J yamlValue(yaml_document_t &doc, yaml_node_t *node, std::set<yaml_node_t *> &stack, size_t &budget,
            int depth = 0) {
    if (!node || depth > 32 || stack.contains(node) || ++budget > 30000)
        throw std::runtime_error("YAML 引用循环或层级过深");
    stack.insert(node);
    J out;
    if (node->type == YAML_SCALAR_NODE)
        out = std::string((char *)node->data.scalar.value, node->data.scalar.length);
    else if (node->type == YAML_SEQUENCE_NODE) {
        out = J::array();
        for (auto it = node->data.sequence.items.start; it != node->data.sequence.items.top; ++it)
            out.push_back(yamlValue(doc, yaml_document_get_node(&doc, *it), stack, budget, depth + 1));
    } else if (node->type == YAML_MAPPING_NODE) {
        out = J::object();
        for (auto it = node->data.mapping.pairs.start; it != node->data.mapping.pairs.top; ++it) {
            auto key = yamlValue(doc, yaml_document_get_node(&doc, it->key), stack, budget, depth + 1);
            if (!key.is_string())
                throw std::runtime_error("YAML 使用了不支持的复合键");
            auto name = key.get<std::string>();
            if (out.contains(name))
                throw std::runtime_error("YAML 存在重复键");
            out[name] = yamlValue(doc, yaml_document_get_node(&doc, it->value), stack, budget, depth + 1);
        }
    }
    stack.erase(node);
    return out;
}
J parseYaml(const std::string &text) {
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        throw std::runtime_error("无法初始化 YAML 解析器");
    yaml_parser_set_input_string(&parser, (const unsigned char *)text.data(), text.size());
    yaml_document_t document;
    bool loaded = yaml_parser_load(&parser, &document) != 0;
    yaml_parser_delete(&parser);
    if (!loaded)
        throw std::runtime_error("YAML 配置无效");
    struct Cleanup {
        yaml_document_t *p;
        ~Cleanup() {
            yaml_document_delete(p);
        }
    } cleanup{&document};
    std::set<yaml_node_t *> stack;
    size_t budget = 0;
    return yamlValue(document, yaml_document_get_root_node(&document), stack, budget);
}
std::string str(const J &j, const char *key) {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : "";
}
} // namespace
J Adapters::model_state(const std::string &id) {
    auto agent = item(id);
    J result = {{"models", J::array()},
                {"sources", J::array()},
                {"warnings", J::array()},
                {"note", "显示公开配置中明确声明的模型；会话参数、账号目录和管理策略可能覆盖配置。"}};
    auto paths = configs(id);
    if (id == "codex" && fs::is_directory(paths[0].parent_path())) {
        std::vector<fs::path> profiles;
        for (auto &entry : fs::directory_iterator(paths[0].parent_path()))
            if (entry.is_regular_file() && pathstr(entry.path().filename()).ends_with(".config.toml"))
                profiles.push_back(entry.path());
        std::sort(profiles.begin(), profiles.end());
        paths.insert(paths.end(), profiles.begin(), profiles.end());
    }
    if (id == "copilot" || id == "openhands" || id == "cursor")
        paths.push_back(store.root / "agent-profiles" / (id + ".json"));
    auto project = store.setting("project");
    // These project locations and precedence are documented by the respective tools.
    if (project.is_string() && !project.get<std::string>().empty()) {
        auto p = path(project);
        if (id == "codex")
            paths.push_back(p / ".codex/config.toml");
        if (id == "claude") {
            paths.push_back(p / ".claude/settings.json");
            paths.push_back(p / ".claude/settings.local.json");
        }
        if (id == "qwen" || id == "gemini")
            paths.push_back(p / (id == "qwen" ? ".qwen/settings.json" : ".gemini/settings.json"));
        if (id == "codebuddy") {
            paths.push_back(p / ".codebuddy/models.json");
            paths.push_back(p / ".codebuddy/settings.json");
        }
        if (id == "opencode") {
            paths.push_back(p / "opencode.json");
            paths.push_back(p / "opencode.jsonc");
        }
    }
    if (id == "opencode") {
        auto base = env("XDG_CONFIG_HOME").empty() ? home / ".config" : path(env("XDG_CONFIG_HOME"));
        paths.insert(paths.begin(), base / "opencode/opencode.jsonc");
        if (!env("OPENCODE_CONFIG").empty())
            paths.push_back(path(env("OPENCODE_CONFIG")));
    }
    auto add = [&](std::string model, const std::string &provider, const fs::path &source,
                   const std::string &role) {
        if (model.empty())
            return false;
        if (model.size() > 400)
            model.resize(400);
        result["models"].push_back(
            {{"id", model}, {"provider", provider}, {"source", pathstr(source)}, {"role", role}});
        if (id == "codex" && !provider.empty())
            result["models"].back()["selection"] = J::array({provider, model}).dump();
        return true;
    };
    std::set<fs::path> seen;
    for (auto &file : paths) {
        if (!seen.insert(file).second || !fs::is_regular_file(file))
            continue;
        result["sources"].push_back(pathstr(file));
        try {
            check_path(file);
            auto raw = read(file, 2 * 1024 * 1024);
            if (id == "codex" || id == "kimi") {
                auto doc = toml::parse(raw);
                if (id == "codex") {
                    add(doc["model"].value_or(std::string()), doc["model_provider"].value_or(std::string()),
                        file,
                        pathstr(file.filename()).ends_with(".config.toml")
                            ? "Profile · " + pathstr(file.stem())
                            : "默认模型");
                    if (auto profiles = doc["profiles"].as_table())
                        for (auto &[name, node] : *profiles)
                            if (auto profile = node.as_table())
                                add((*profile)["model"].value_or(std::string()),
                                    (*profile)["model_provider"].value_or(std::string()), file,
                                    "旧 Profile（新版不读取）· " + std::string(name.str()));
                } else if (auto models = doc["models"].as_table()) {
                    auto selected = doc["default_model"].value_or(std::string());
                    for (auto &[name, node] : *models)
                        if (auto model = node.as_table())
                            if (add((*model)["model"].value_or(std::string()),
                                    (*model)["provider"].value_or(std::string()), file,
                                    std::string(name.str()) == selected ? "默认模型" : "已配置"))
                                result["models"].back()["selection"] = std::string(name.str());
                }
                continue;
            }
            if (id == "harness") {
                auto entries = parseYaml(raw);
                if (!entries.is_array())
                    throw std::runtime_error("Profile 不是插件列表，未推断结构");
                for (auto &entry : entries) {
                    auto plugin = str(entry, "id");
                    if (!entry.contains("config") || !entry["config"].is_object())
                        continue;
                    auto &config = entry["config"];
                    if (plugin == "agent-default-model" &&
                        add(str(config, "model"), str(config, "provider"), file, "默认模型"))
                        result["models"].back()["selection"] =
                            J::array({str(config, "provider"), str(config, "model")}).dump();
                    if (plugin == "llm-pi-ai" && config.contains("providers") &&
                        config["providers"].is_object())
                        for (auto &[provider, settings] : config["providers"].items())
                            if (settings.contains("models") && settings["models"].is_array())
                                for (auto &m : settings["models"]) {
                                    if (add(str(m, "id"), provider, file, "Profile 模型"))
                                        result["models"].back()["selection"] =
                                            J::array({provider, str(m, "id")}).dump();
                                }
                    if (plugin == "llm-deepseek" && config.contains("models") && config["models"].is_array())
                        for (auto &m : config["models"])
                            add(str(m, "id"), "deepseek", file, "Profile 模型");
                }
                continue;
            }
            auto doc = parse_jsonc(raw);
            if (!doc.is_object())
                throw std::runtime_error("配置根节点不是对象");
            if (id == "zcode") {
                for (auto row : zcode_model_rows(doc)) {
                    row["source"] = pathstr(file);
                    result["models"].push_back(row);
                }
            } else if (id == "claude") {
                add(str(doc, "model"), "", file, "默认模型");
                if (doc.contains("availableModels") && doc["availableModels"].is_array())
                    for (auto &m : doc["availableModels"])
                        if (m.is_string())
                            add(m, "", file, "允许选择");
                auto variables = doc.value("env", J::object());
                for (auto key : {"ANTHROPIC_MODEL", "ANTHROPIC_DEFAULT_OPUS_MODEL",
                                 "ANTHROPIC_DEFAULT_SONNET_MODEL", "ANTHROPIC_DEFAULT_HAIKU_MODEL"})
                    add(str(variables, key), "env", file, key);
            } else if (id == "gemini" || id == "qwen") {
                add(str(doc.value("model", J::object()), "name"), "", file, "默认模型");
                if (id == "qwen" && doc.contains("modelProviders") && doc["modelProviders"].is_object())
                    for (auto &[provider, models] : doc["modelProviders"].items())
                        if (models.is_array())
                            for (auto &m : models)
                                add(str(m, "id"), provider, file, "已配置");
            } else if (id == "cursor") {
                if (file.parent_path() == store.root / "agent-profiles") {
                    auto selected = str(doc, "model");
                    for (auto &m : doc.value("models", J::array()))
                        if (m.is_string())
                            add(m, "Cursor CLI", file,
                                m == selected ? "管理器启动默认" : "启动备选（未验证账号）");
                } else {
                    auto m = doc.value("model", J::object());
                    add(str(m, "id"), "Cursor CLI", file, "原配置声明；账号决定可用性");
                }
            } else if (id == "opencode") {
                add(str(doc, "model"), "", file, "默认模型");
                add(str(doc, "small_model"), "", file, "轻量任务");
                auto providers = doc.value("provider", J::object());
                if (providers.is_object())
                    for (auto &[provider, settings] : providers.items()) {
                        auto models = settings.value("models", J::object());
                        if (models.is_object())
                            for (auto &[model, ignored] : models.items()) {
                                add(model, provider, file, "已配置");
                                result["models"].back()["selection"] = provider + "/" + model;
                            }
                    }
            } else if (id == "codebuddy" || id == "workbuddy") {
                add(str(doc, "model"), "", file, "默认模型");
                auto models = doc.value("models", J::array());
                if (models.is_array())
                    for (auto &m : models)
                        add(str(m, "id"), str(m, "vendor"), file,
                            id == "workbuddy" ? "官方兼容的 models.json" : "自定义模型");
            } else if (id == "factory") {
                add(str(doc, "model"), "", file, "默认模型");
                auto models = doc.value("customModels", J::array());
                if (models.is_array())
                    for (auto &m : models) {
                        if (add(str(m, "model"), str(m, "provider"), file, "自定义模型"))
                            result["models"].back()["selection"] = "custom:" + str(m, "model");
                    }
            } else if (id == "cline") {
                auto selected = str(doc, "lastUsedProvider");
                auto providers = doc.value("providers", J::object());
                if (providers.is_object())
                    for (auto &[name, entry] : providers.items()) {
                        auto settings = entry.value("settings", J::object());
                        if (add(str(settings, "model"), name, file, name == selected ? "默认模型" : "已配置"))
                            result["models"].back()["selection"] = name;
                    }
            } else if (id == "kiro") {
                add(str(doc, "chat.defaultModel"), "Kiro", file, "默认模型");
            } else if (id == "roo") {
                auto profiles = doc.value("providerProfiles", J::object());
                auto selected = str(profiles, "currentApiConfigName");
                auto list = profiles.value("apiConfigs", J::object());
                if (list.is_object())
                    for (auto &[name, m] : list.items()) {
                        auto model = str(m, "apiModelId");
                        if (model.empty())
                            model = str(m, "openAiModelId");
                        if (add(model, str(m, "apiProvider"), file,
                                name == selected ? "导入默认项" : "导入模型"))
                            result["models"].back()["selection"] = name;
                    }
            } else if (id == "copilot" || id == "openhands") {
                bool managed = file.parent_path() == store.root / "agent-profiles";
                if (managed) {
                    auto selected = str(doc, "model");
                    auto ids = doc.value("models", J::array());
                    for (auto &m : ids)
                        if (m.is_string())
                            add(m, str(doc, "provider"), file, m == selected ? "管理器启动默认" : "启动档案");
                } else if (id == "copilot")
                    add(str(doc, "model"), "Copilot", file, "原工具默认");
                else {
                    auto llm = doc.value("llm", J::object());
                    add(str(llm, "model"), "OpenHands", file, "原工具默认");
                }
            } else if (id == "crush") {
                result["warnings"].push_back(
                    "Crush 新版采用可执行 crushrc；本工具不执行该文件。旧 JSON 仅作静态读取。");
                auto models = doc.value("models", J::object());
                if (models.is_object())
                    for (auto &[role, m] : models.items()) {
                        if (add(str(m, "model"), str(m, "provider"), file,
                                role == "large"   ? "默认模型"
                                : role == "small" ? "轻量任务"
                                                  : role))
                            result["models"].back()["selection"] =
                                J::array({str(m, "provider"), str(m, "model")}).dump();
                    }
                auto providers = doc.value("providers", J::object());
                if (providers.is_object())
                    for (auto &[name, provider] : providers.items()) {
                        auto list = provider.value("models", J::array());
                        if (list.is_array())
                            for (auto &m : list)
                                if (add(str(m, "id"), name, file, "已配置"))
                                    result["models"].back()["selection"] =
                                        J::array({name, str(m, "id")}).dump();
                    }
            }
        } catch (...) {
            // Parser errors may contain credential-bearing input. Report only the path.
            result["warnings"].push_back("无法安全读取 " + pathstr(file) + "，未修改文件。");
        }
    }
    if (result["models"].empty())
        result["note"] = "未在公开配置中读到模型。内置/"
                         "账号模型及运行中的会话选择需在原工具确认；这里不会以服务商预设冒充已配置模型。";
    if (id == "workbuddy")
        result["warnings"].push_back(
            "此处仅显示官方仍兼容的旧 models.json；新版界面保存的其他模型应在 WorkBuddy 设置 → 模型中确认。");
    return result;
}
} // namespace ad

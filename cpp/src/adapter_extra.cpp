#include "core.hpp"
#define TOML_EXCEPTIONS 1
#include "toml.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace ad {
namespace {
std::string jsonSource(std::string source, bool trailing = true) {
    bool quoted = false, escape = false;
    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (quoted) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                quoted = false;
            continue;
        }
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            source[i++] = ' ';
            source[i] = ' ';
            while (i + 1 < source.size() && source[i + 1] != '\n')
                source[++i] = ' ';
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            source[i++] = ' ';
            source[i] = ' ';
            bool closed = false;
            while (i + 1 < source.size()) {
                ++i;
                if (source[i] == '*' && i + 1 < source.size() && source[i + 1] == '/') {
                    source[i++] = ' ';
                    source[i] = ' ';
                    closed = true;
                    break;
                }
                if (source[i] != '\n' && source[i] != '\r')
                    source[i] = ' ';
            }
            if (!closed)
                throw std::runtime_error("JSON 注释未闭合");
        }
    }
    if (trailing) {
        quoted = escape = false;
        for (size_t i = 0; i < source.size(); ++i) {
            char c = source[i];
            if (quoted) {
                if (escape)
                    escape = false;
                else if (c == '\\')
                    escape = true;
                else if (c == '"')
                    quoted = false;
                continue;
            }
            if (c == '"') {
                quoted = true;
                continue;
            }
            if (c == ',') {
                size_t j = i + 1;
                while (j < source.size() && isspace((unsigned char)source[j]))
                    ++j;
                if (j < source.size() && (source[j] == '}' || source[j] == ']'))
                    source[i] = ' ';
            }
        }
    }
    return source;
}
size_t stringEnd(const std::string &s, size_t i) {
    bool escape = false;
    for (++i; i < s.size(); ++i) {
        if (escape)
            escape = false;
        else if (s[i] == '\\')
            escape = true;
        else if (s[i] == '"')
            return i + 1;
    }
    throw std::runtime_error("JSON 字符串未闭合");
}
size_t tomlOffset(const std::string &s, toml::source_position pos) {
    size_t i = 0;
    for (uint32_t line = 1; line < pos.line; ++line) {
        auto next = s.find('\n', i);
        if (next == std::string::npos)
            throw std::runtime_error("TOML 定位失败");
        i = next + 1;
    }
    for (uint32_t col = 1; col < pos.column && i < s.size(); ++col) {
        ++i;
        while (i < s.size() && ((unsigned char)s[i] & 0xc0) == 0x80)
            ++i;
    }
    return i;
}
J readObject(const fs::path &p) {
    auto value = fs::exists(p) ? parse_jsonc(read(p, 2 * 1024 * 1024)) : J::object();
    if (!value.is_object())
        throw std::runtime_error("配置根节点不是对象");
    return value;
}
J writeEntry(const fs::path &p, const std::string &content) {
    check_path(p);
    return {{"path", pathstr(p)},
            {"content", content},
            {"hash", sha(fs::exists(p) ? read(p, 2 * 1024 * 1024) : "")}};
}
std::string isoNow() {
    SYSTEMTIME t;
    GetSystemTime(&t);
    char b[40];
    snprintf(b, sizeof b, "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    return b;
}
std::string patchTable(std::string source, const std::string &section, const std::string &name,
                       const std::string &field, const J &value) {
    auto doc = toml::parse(source);
    auto node = doc[section][name][field].node();
    if (node) {
        if (!node->is_value())
            throw std::runtime_error("Kimi 字段格式无效");
        auto span = node->source();
        auto a = tomlOffset(source, span.begin), b = tomlOffset(source, span.end);
        source.replace(a, b - a, value.dump());
    } else {
        auto table = doc[section][name].as_table();
        auto line = field + " = " + value.dump() + "\n";
        if (table) {
            auto at = tomlOffset(source, table->source().begin);
            auto end = source.find('\n', at);
            if (end == std::string::npos)
                source += '\n', end = source.size() - 1;
            source.insert(end + 1, line);
        } else
            source += "\n[" + section + "." + J(name).dump() + "]\n" + line;
    }
    (void)toml::parse(source);
    return source;
}

} // namespace
J parse_jsonc(const std::string &source) {
    return J::parse(jsonSource(source));
}
static std::string modify_json_member(const std::string &original, const std::string &key, const J &value,
                                      bool erase) {
    std::string raw = original.empty() ? "{}\n" : original;
    auto clean = jsonSource(raw);
    auto doc = J::parse(clean);
    if (!doc.is_object())
        throw std::runtime_error("配置根节点不是 JSON 对象");
    size_t i = clean.find('{') + 1;
    while (i < clean.size()) {
        while (i < clean.size() && (isspace((unsigned char)clean[i]) || clean[i] == ','))
            ++i;
        if (i >= clean.size() || clean[i] == '}')
            break;
        size_t keyStart = i, keyEnd = stringEnd(clean, i);
        auto current = J::parse(clean.substr(keyStart, keyEnd - keyStart)).get<std::string>();
        i = keyEnd;
        while (i < clean.size() && isspace((unsigned char)clean[i]))
            ++i;
        if (i >= clean.size() || clean[i++] != ':')
            throw std::runtime_error("JSON 键值分隔无效");
        while (i < clean.size() && isspace((unsigned char)clean[i]))
            ++i;
        size_t first = i;
        if (clean[i] == '"')
            i = stringEnd(clean, i);
        else if (clean[i] == '{' || clean[i] == '[') {
            int depth = 0;
            do {
                if (clean[i] == '"') {
                    i = stringEnd(clean, i);
                    continue;
                }
                if (clean[i] == '{' || clean[i] == '[')
                    ++depth;
                else if (clean[i] == '}' || clean[i] == ']')
                    --depth;
                ++i;
            } while (i < clean.size() && depth);
        } else
            while (i < clean.size() && clean[i] != ',' && clean[i] != '}')
                ++i;
        size_t last = i;
        while (last > first && isspace((unsigned char)clean[last - 1]))
            --last;
        if (current == key) {
            if (erase) {
                auto begin = keyStart, end = last;
                while (end < clean.size() && isspace((unsigned char)clean[end]))
                    ++end;
                if (end < clean.size() && clean[end] == ',')
                    ++end;
                else {
                    while (begin && isspace((unsigned char)clean[begin - 1]))
                        --begin;
                    if (begin && clean[begin - 1] == ',')
                        --begin;
                    end = last;
                }
                raw.erase(begin, end - begin);
            } else
                raw.replace(first, last - first, value.dump(-1, ' ', false));
            parse_jsonc(raw);
            return raw;
        }
    }
    if (erase)
        return raw;
    auto end = clean.find_last_of('}');
    auto commentsOnly = jsonSource(raw, false);
    size_t before = end;
    while (before && isspace((unsigned char)commentsOnly[before - 1]))
        --before;
    bool comma = before && commentsOnly[before - 1] == ',';
    raw.insert(end, (doc.empty() || comma ? "\n" : ",\n") + std::string("  ") + J(key).dump() + ": " +
                        value.dump(-1, ' ', false) + "\n");
    parse_jsonc(raw);
    return raw;
}
std::string patch_json(const std::string &original, const std::string &key, const J &value) {
    return modify_json_member(original, key, value, false);
}
std::string erase_json_member(const std::string &original, const std::string &key) {
    auto output = original;
    while (parse_jsonc(output).contains(key))
        output = modify_json_member(output, key, J(), true);
    return output;
}
std::string patch_toml(const std::string &original, const std::string &key, const J &value) {
    auto s = original;
    auto doc = toml::parse(s);
    auto node = doc.get(key);
    if (node) {
        if (!node->is_value())
            throw std::runtime_error("已有 TOML 字段类型异常");
        auto pos = node->source();
        auto first = tomlOffset(s, pos.begin), last = tomlOffset(s, pos.end);
        s.replace(first, last - first, value.dump(-1, ' ', false));
    } else
        s = key + " = " + value.dump(-1, ' ', false) + "\n" + s;
    (void)toml::parse(s);
    return s;
}
J Adapters::capabilities(const std::string &id) {
    item(id);
    bool native = id == "codex" || id == "claude" || id == "opencode" || id == "qwen" || id == "codebuddy" ||
                  id == "workbuddy" || id == "factory" || id == "cline" || id == "kimi" || id == "roo" ||
                  id == "harness" || id == "crush" || id == "zcode";
    bool launch = id == "copilot" || id == "openhands";
    bool select = id == "cursor" || id == "gemini" || id == "kiro";
    return {
        {"models", native   ? "native"
                   : launch ? "launch-profile"
                            : "account"},
        {"configure", native || launch},
        {"default", (native && id != "workbuddy" && id != "codebuddy") || launch || select},
        {"note",
         id == "cursor" ? "默认项与备选 ID 保存为管理器启动档案；通过 agent --model 生效，账号决定可用模型。"
         : id == "workbuddy" || id == "codebuddy"
             ? "可添加自定义模型列表；默认模型请在原工具 /model 或模型设置中选择。"
         : id == "zcode"   ? "支持 ZCode 3.16 的供应商、模型列表与默认项；旧配置请先在新版完成迁移。"
         : id == "harness" ? "模型列表与默认项写入 Web profile；既有会话保留其模型。"
         : id == "roo"     ? "通过 Roo 官方自动导入写入 VS Code；重启扩展后生效，模式绑定可能覆盖默认项。"
         : id == "crush"   ? "写入仍兼容的 crush.json；新版 crushrc 可覆盖它，管理器不会执行该脚本。"
         : id == "kimi" ? "适配旧版 Python Kimi CLI；项目已归档，新 Kimi Code 使用独立配置，不可套用旧格式。"
         : native       ? "模型与默认项写入已核实的原生配置。"
         : launch       ? "保存启动配置；从本管理器启动 CLI 时应用 API、协议和默认模型。"
         : select       ? "支持默认模型选择；可用模型由工具或账号决定。"
                        : "官方未公开可写的本地模型配置；请在原工具设置模型与 API，管理器不推断私有格式。"}};
}
J Adapters::extra_preview(const std::string &id, const J &connection, const std::string &var) {
    if (id != "factory" && id != "workbuddy" && id != "cline" && id != "copilot" && id != "openhands" &&
        id != "kimi" && id != "roo" && id != "harness" && id != "crush" && id != "zcode")
        return nullptr;
    auto p = connection;
    if (p["protocol"] == "anthropic") {
        auto base = p["base_url"].get<std::string>();
        if (base.ends_with("/v1"))
            base.resize(base.size() - 3);
        p["base_url"] = base;
    }
    auto file = configs(id).at(0);
    J object;
    std::string output;
    J companion = J::array();
    J display = {{"default", p["model"]},
                 {"models", p["models"]},
                 {"base_url", p["base_url"]},
                 {"protocol", p["protocol"]},
                 {"key_environment", var}};
    std::string note = "保留其他配置；应用前备份。";
    auto uid = p["id"].get<std::string>(), model = p["model"].get<std::string>();
    if ((id == "copilot" || id == "openhands") &&
        (p.value("vendor", "") == "azure" || p.value("auth", "") == "api-key"))
        throw std::runtime_error("此 Agent 的 Azure 接入还需要 API 版本或部署配置，请使用原工具官方 Azure "
                                 "设置；不会当作普通 OpenAI 连接写入");
    if (id == "zcode") {
        if (!fs::exists(file) && fs::exists(home / ".zcode/v2/config.json"))
            throw std::runtime_error("检测到旧版 ZCode 配置，请先打开新版 ZCode 完成模型迁移，再重新预览");
        auto [saved, key] = models.get(uid);
        output = merge_zcode(fs::exists(file) ? read(file, 2 * 1024 * 1024) : "", p, key);
        SecureZeroMemory(key.data(), key.size());
        note += " 按 ZCode 3.16 schemaVersion 1 "
                "合并个人供应商并设置默认项；保留其他路由和模型规则。原生文件含明文 API "
                "Key，预览隐藏密钥；运行中的会话可能保留原模型。";
    } else if (id == "harness") {
        J config = {{"apiKeyEnv", var},
                    {"baseURL", p["base_url"]},
                    {"api", p["protocol"] == "anthropic"   ? "anthropic-messages"
                            : p["protocol"] == "responses" ? "openai-responses"
                                                           : "openai-completions"},
                    {"models", J::array()}};
        for (auto &m : p["models"])
            config["models"].push_back({{"id", m}});
        auto route = "agentdock-" + uid.substr(0, 12);
        output = patch_harness(fs::exists(file) ? read(file, 2 * 1024 * 1024) : "", route, config);
        output = patch_harness_default(output, route, model);
        note = "合并 Web profile 的 llm-pi-ai 路由及 agent-default-model "
               "默认项，保留其他插件；密钥通过环境变量注入。运行中的会话保留原模型。";
    } else if (id == "crush") {
        object = readObject(file);
        auto route = "agentdock-" + uid;
        auto &provider = object["providers"][route];
        if (provider.is_null())
            provider = J::object();
        provider["name"] = p["name"];
        provider["type"] = p["protocol"] == "anthropic" ? "anthropic" : "openai";
        provider["base_url"] = p["base_url"];
        provider["api_key"] = "$" + var;
        provider["models"] = J::array();
        for (auto &m : p["models"])
            provider["models"].push_back({{"id", m}, {"name", m}});
        auto &selected = object["models"]["large"];
        if (selected.is_null())
            selected = J::object();
        selected["provider"] = route;
        selected["model"] = model;
        output = object.dump(2, ' ', false) + "\n";
        note = "写入 Crush 官方兼容的旧 JSON 格式，large 为默认主模型；保留其他任务模型。新版 crushrc "
               "可覆盖它，管理器不执行该脚本。";
    } else if (id == "copilot" || id == "openhands") {
        // CLI environment overrides are official. The manager profile is not presented as a native account
        // model registry.
        file = store.root / "agent-profiles" / (id + ".json");
        output = J{{"provider", uid},
                   {"model", model},
                   {"models", p["models"]},
                   {"base_url", p["base_url"]},
                   {"protocol", p["protocol"]}}
                     .dump(2, ' ', false) +
                 "\n";
        note =
            "配置保存到管理器启动档案；启动 CLI 时按官方环境变量覆盖默认模型与 API。不改动原工具账号目录。";
        if (id == "copilot") {
            auto registry = env("COPILOT_PROVIDERS_CONFIG").empty()
                                ? configs(id)[0].parent_path() / "providers.json"
                                : path(env("COPILOT_PROVIDERS_CONFIG"));
            if (fs::is_regular_file(registry)) {
                auto r = readObject(registry);
                if (!r.value("providers", J::object()).empty() || !r.value("models", J::object()).empty())
                    throw std::runtime_error(
                        "Copilot providers.json 已声明路由，会覆盖官方环境变量接入；请在 Copilot help "
                        "providers 中配置该注册表后再使用，未覆盖原配置");
            }
        }
    } else if (id == "factory") {
        object = readObject(file);
        auto &list = object["customModels"];
        if (list.is_null())
            list = J::array();
        if (!list.is_array())
            throw std::runtime_error("customModels 不是数组");
        for (auto &m : p["models"]) {
            std::string name = "AgentDock-" + uid.substr(0, 8) + "-" + sha(m.get<std::string>()).substr(0, 8);
            J entry = {{"model", m},
                       {"displayName", name},
                       {"baseUrl", p["base_url"]},
                       {"apiKey", "${" + var + "}"},
                       {"provider", p["protocol"] == "anthropic"   ? "anthropic"
                                    : p["protocol"] == "responses" ? "openai"
                                                                   : "generic-chat-completion-api"}};
            if (p["protocol"] == "anthropic" && p.value("auth", "") == "bearer")
                entry["authMode"] = "bearer";
            auto it = std::find_if(list.begin(), list.end(), [&](const J &old) {
                return old.is_object() && old.value("displayName", "") == name;
            });
            if (it == list.end())
                list.push_back(entry);
            else
                it->update(entry);
        }
        object["model"] = "custom:" + model;
        output = object.dump(2, ' ', false) + "\n";
        note += " 工具按 ${环境变量} 读取密钥，从本管理器启动可自动注入。旧 Droid 版本的 custom ID "
                "规则可能不同，请升级或在原工具确认。";
    } else if (id == "workbuddy") {
        object = readObject(file);
        auto &list = object["models"];
        if (list.is_null())
            list = J::array();
        if (!list.is_array())
            throw std::runtime_error("models 不是数组");
        for (auto &m : p["models"]) {
            J entry = {{"id", m},
                       {"name", m},
                       {"vendor", p["vendor"]},
                       {"apiKey", "${" + var + "}"},
                       {"url", p["base_url"].get<std::string>() +
                                   (p["protocol"] == "chat" ? "/chat/completions" : "")}};
            if (p["protocol"] == "responses")
                entry["api"] = "openai-responses";
            if (p["protocol"] == "anthropic")
                throw std::runtime_error(
                    "WorkBuddy 此格式尚未核实 Anthropic 协议写法，请使用 Chat/Responses 连接");
            auto it = std::find_if(list.begin(), list.end(), [&](const J &old) {
                return old.is_object() && old.value("id", "") == m.get<std::string>();
            });
            if (it != list.end() && it->value("apiKey", "") != entry["apiKey"].get<std::string>())
                throw std::runtime_error("已有同名模型使用其他凭据，未覆盖");
            if (it == list.end())
                list.push_back(entry);
            else
                it->update(entry);
        }
        output = object.dump(2, ' ', false) + "\n";
        note += " WorkBuddy 官方兼容 models.json，模型列表可写；默认模型需在 WorkBuddy "
                "中选择，管理器不能替代账号设置。";
    } else if (id == "cline") {
        object = readObject(file);
        if (!object.empty() &&
            (object.value("version", 0) != 1 || !object.value("providers", J::object()).is_object()))
            throw std::runtime_error("检测到非当前版本 Cline 配置，未改写");
        object["version"] = 1;
        if (!object.contains("modes"))
            object["modes"] = J::object();
        auto [settings, key] = models.get(uid);
        for (auto &m : p["models"]) {
            auto route = "agentdock-" + uid.substr(0, 12) + "-" + sha(m.get<std::string>()).substr(0, 10);
            J config = {{"provider", route},
                        {"model", m},
                        {"baseUrl", p["base_url"]},
                        {"protocol", p["protocol"] == "anthropic"   ? "anthropic"
                                     : p["protocol"] == "responses" ? "openai-responses"
                                                                    : "openai-chat"},
                        {"client", p["protocol"] == "anthropic"   ? "anthropic"
                                   : p["protocol"] == "responses" ? "openai"
                                                                  : "openai-compatible"}};
            if (!key.empty())
                config["apiKey"] = key;
            auto &saved = object["providers"][route];
            if (!saved.is_object() || saved.value("settings", J::object()) != config) {
                if (!saved.is_object())
                    saved = J::object();
                saved["settings"] = config;
                saved["tokenSource"] = "manual";
                saved["updatedAt"] = isoNow();
            }
            if (m == p["model"])
                object["lastUsedProvider"] = route;
        }
        SecureZeroMemory(key.data(), key.size());
        output = object.dump(2, ' ', false) + "\n";
        note += " Cline 当前 providers.json "
                "为本地明文凭据格式；密钥不出现在此预览。每个模型单独路由，lastUsedProvider 设置默认项。";
    } else if (id == "kimi") {
        auto before = fs::exists(file) ? read(file, 2 * 1024 * 1024) : "";
        auto doc = toml::parse(before);
        auto context = p.value("context_window", 0);
        if (context <= 0)
            throw std::runtime_error(
                "Kimi 自定义模型要求 max_context_size，请先在连接高级设置填写上下文上限");
        auto [settings, key] = models.get(uid);
        auto provider = "agentdock_" + uid;
        auto selected = provider + "_" + sha(model).substr(0, 10);
        output = patch_toml(before, "default_model", selected);
        output = patchTable(output, "providers", provider, "type",
                            p["protocol"] == "anthropic"   ? "anthropic"
                            : p["protocol"] == "responses" ? "openai_responses"
                                                           : "openai_legacy");
        output = patchTable(output, "providers", provider, "base_url", p["base_url"]);
        output = patchTable(output, "providers", provider, "api_key", key);
        for (auto &m : p["models"]) {
            auto alias = provider + "_" + sha(m.get<std::string>()).substr(0, 10);
            output = patchTable(output, "models", alias, "provider", provider);
            output = patchTable(output, "models", alias, "model", m);
            output = patchTable(output, "models", alias, "max_context_size", context);
        }
        SecureZeroMemory(key.data(), key.size());
        (void)toml::parse(output);
        note += " 适配 MoonshotAI/kimi-cli 公开的 openai_legacy / openai_responses / anthropic "
                "格式；要求填写真实上下文容量，原生文件含明文凭据。";
    } else if (id == "roo") {
        if (p["protocol"] == "responses")
            throw std::runtime_error("Roo 此导入格式尚未核实 Responses 开关，请使用 Chat/Anthropic");
        object = readObject(file);
        auto &profiles = object["providerProfiles"];
        if (profiles.is_null())
            profiles = J::object();
        auto [settings, key] = models.get(uid);
        for (auto &m : p["models"]) {
            auto name = "AgentDock-" + uid.substr(0, 8) + "-" + sha(m.get<std::string>()).substr(0, 10);
            J entry = {{"id", name}, {"apiProvider", p["protocol"] == "anthropic" ? "anthropic" : "openai"}};
            if (p["protocol"] == "anthropic") {
                entry["apiKey"] = key;
                entry["anthropicBaseUrl"] = p["base_url"];
                entry["apiModelId"] = m;
            } else {
                entry["openAiApiKey"] = key;
                entry["openAiBaseUrl"] = p["base_url"];
                entry["openAiModelId"] = m;
            }
            profiles["apiConfigs"][name] = entry;
            if (m == p["model"])
                profiles["currentApiConfigName"] = name;
        }
        SecureZeroMemory(key.data(), key.size());
        if (!object.contains("globalSettings"))
            object["globalSettings"] = J::object();
        output = object.dump(2, ' ', false) + "\n";
        auto roaming = home == path(env("USERPROFILE")) && !env("APPDATA").empty() ? path(env("APPDATA"))
                                                                                   : home / "AppData/Roaming";
        auto host = roaming / "Code/User/settings.json";
        auto source = fs::exists(host) ? read(host, 2 * 1024 * 1024) : "";
        companion.push_back(
            writeEntry(host, patch_json(source, "roo-cline.autoImportSettingsPath", pathstr(file))));
        note += " 使用 Roo 官方自动导入，写入 VS Code 的 autoImportSettingsPath；重启 Roo "
                "扩展后生效。导入文件按 Roo 格式含明文密钥，不显示在预览中。";
    }
    return {{"path", pathstr(file)},
            {"content", output},
            {"display", display.dump(2, ' ', false)},
            {"note", note},
            {"writes", companion}};
}
J Adapters::preview_default(const std::string &id, const std::string &model) {
    if (trim(model).empty() || model.size() > 400)
        throw std::runtime_error("请填写有效模型 ID");
    auto paths = configs(id);
    if (paths.empty())
        throw std::runtime_error("此工具没有公开的默认模型配置位置");
    auto file = paths[0];
    auto raw = fs::exists(file) ? read(file, 2 * 1024 * 1024) : "";
    std::string text;
    J binding = store.setting("agent_binding:" + id);
    bool launchProfile =
        (id == "copilot" || id == "openhands") && binding.is_object() && binding.contains("provider");
    if (launchProfile) {
        file = store.root / "agent-profiles" / (id + ".json");
        raw = read(file, 2 * 1024 * 1024);
        auto doc = readObject(file);
        auto ids = doc.value("models", J::array());
        if (std::find(ids.begin(), ids.end(), model) == ids.end())
            throw std::runtime_error("此模型不在启动配置中");
        text = patch_json(raw, "model", model);
    } else if (id == "zcode") {
        text = select_zcode_model(raw, model);
    } else if (id == "harness") {
        auto selection = J::parse(model);
        if (!selection.is_array() || selection.size() != 2)
            throw std::runtime_error("Harness 模型路由无效");
        text = patch_harness_default(raw, selection[0], selection[1]);
    } else if (id == "kimi") {
        auto doc = toml::parse(raw);
        if (!doc["models"][model])
            throw std::runtime_error("该 Kimi 模型别名未配置");
        text = patch_toml(raw, "default_model", model);
    } else if (id == "kiro")
        text = patch_json(raw, "chat.defaultModel", model);
    else if (id == "cursor") {
        // The CLI documents --model; native model object fields are not fully specified.
        file = store.root / "agent-profiles/cursor.json";
        raw = fs::is_regular_file(file) ? read(file, 2 * 1024 * 1024) : "";
        auto object = raw.empty() ? J::object() : parse_jsonc(raw);
        auto ids = object.value("models", J::array());
        if (!ids.is_array())
            throw std::runtime_error("Cursor 启动档案模型列表无效");
        if (std::find(ids.begin(), ids.end(), model) == ids.end())
            ids.push_back(model);
        object["models"] = ids;
        object["model"] = model;
        text = object.dump(2, ' ', false) + "\n";
    } else if (id == "gemini") {
        auto object = readObject(file);
        auto selected = object.value("model", J::object());
        selected["name"] = model;
        text = patch_json(raw, "model", selected);
    } else if (id == "claude") {
        auto doc = readObject(file);
        auto vars = doc.value("env", J::object());
        if (vars.contains("ANTHROPIC_MODEL"))
            vars["ANTHROPIC_MODEL"] = model;
        text = patch_json(patch_json(raw, "model", model), "env", vars);
    } else if (id == "copilot" || id == "factory")
        text = patch_json(raw, "model", model);
    else if (id == "opencode")
        text = patch_json(raw, "model", model);
    else if (id == "qwen") {
        auto doc = readObject(file);
        auto selected = doc.value("model", J::object());
        selected["name"] = model;
        text = patch_json(raw, "model", selected);
    } else if (id == "cline") {
        auto doc = readObject(file);
        if (!doc["providers"].contains(model))
            throw std::runtime_error("Cline 路由不存在");
        text = patch_json(raw, "lastUsedProvider", model);
    } else if (id == "roo") {
        auto doc = readObject(file);
        auto profiles = doc.value("providerProfiles", J::object());
        if (!profiles["apiConfigs"].contains(model))
            throw std::runtime_error("Roo 配置不存在");
        profiles["currentApiConfigName"] = model;
        text = patch_json(raw, "providerProfiles", profiles);
    } else if (id == "crush") {
        auto selection = J::parse(model);
        if (!selection.is_array() || selection.size() != 2)
            throw std::runtime_error("Crush 模型路由无效");
        auto doc = readObject(file);
        auto selected = doc.value("models", J::object());
        selected["large"]["provider"] = selection[0];
        selected["large"]["model"] = selection[1];
        text = patch_json(raw, "models", selected);
    } else if (id == "codex") {
        if (model.starts_with("[")) {
            auto selection = J::parse(model);
            if (!selection.is_array() || selection.size() != 2)
                throw std::runtime_error("Codex 模型路由无效");
            text = patch_toml(patch_toml(raw, "model", selection[1]), "model_provider", selection[0]);
        } else
            text = patch_toml(raw, "model", model);
    } else
        throw std::runtime_error("此工具的默认模型尚无独立可写入口，请使用模型连接配置");
    auto ticket = uuid();
    previews[ticket] = {{"path", pathstr(file)}, {"hash", sha(raw)}, {"content", text},
                        {"created", now()},      {"apply", true},    {"agent", id}};
    if (launchProfile) {
        previews[ticket]["provider"] = binding["provider"];
        previews[ticket]["model"] = model;
        previews[ticket]["revision"] = binding.value("revision", "");
    }
    if (id == "codex" && model.starts_with("[")) {
        auto selection = J::parse(model);
        bool found = false;
        for (auto &p : models.list()) {
            auto route = "agentdock_" + p["id"].get<std::string>();
            std::replace(route.begin(), route.end(), '-', '_');
            if (route == selection[0].get<std::string>()) {
                previews[ticket]["provider"] = p["id"];
                previews[ticket]["model"] = selection[1];
                previews[ticket]["revision"] = p.value("revision", "");
                found = true;
                break;
            }
        }
        if (!found)
            previews[ticket]["unbind"] = true;
    }
    return {{"ticket", ticket},
            {"path", pathstr(file)},
            {"content", J{{"default_model", model}}.dump(2, ' ', false)},
            {"can_apply", true},
            {"note", id == "cursor" ? "保存管理器启动默认项与备选 ID，通过官方 agent --model "
                                      "生效；不猜测原生 model 对象内部字段，账号决定可用性。"
                                    : "仅修改默认模型；账号或运行中会话可能覆盖此设置，不修改认证与权限。"}};
}
} // namespace ad

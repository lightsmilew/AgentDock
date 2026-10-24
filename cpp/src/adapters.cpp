#include "core.hpp"
#define TOML_EXCEPTIONS 1
#include "toml.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace ad {
Adapters::Adapters(Store &s, Models &m, fs::path h)
    : store(s), models(m), home(h.empty() ? path(env("USERPROFILE")) : h),
      catalog(resource_json(100)["agents"]) {}
J Adapters::item(const std::string &id) {
    for (auto &a : catalog)
        if (a["id"] == id)
            return a;
    throw std::runtime_error("未知 Agent 工具");
}
fs::path Adapters::target(const std::string &id, bool project) {
    auto a = item(id);
    std::string rel = a[project ? "project_skills" : "skills"];
    if (rel.empty())
        throw std::runtime_error(project ? "此工具没有已核实的项目目录，请使用官方导入功能"
                                         : "此工具没有已核实的用户目录，请使用官方导入功能");
    if (project) {
        auto v = store.setting("project");
        if (!v.is_string() || v.get<std::string>().empty() || !fs::is_directory(path(v)))
            throw std::runtime_error("请先在设置中选择有效的项目目录");
        return path(v) / path(rel);
    }
    if (id == "harness")
        return (env("DSH_HOME").empty() ? home / ".dsh" : path(env("DSH_HOME"))) / "skills";
    if (id == "claude" && !env("CLAUDE_CONFIG_DIR").empty())
        return path(env("CLAUDE_CONFIG_DIR")) / "skills";
    if (id == "opencode" && !env("XDG_CONFIG_HOME").empty())
        return path(env("XDG_CONFIG_HOME")) / "opencode/skills";
    return home / path(rel);
}
std::vector<fs::path> Adapters::configs(const std::string &id) {
    if (id == "zcode") {
        auto root = env("ZCODE_DATA_BASE_DIR").empty() ? home : path(env("ZCODE_DATA_BASE_DIR"));
        auto current = env("ZCODE_PERSONAL_PROVIDER_CONFIG_FILE").empty()
                           ? root / ".zcode/v2/provider_config.json"
                           : path(env("ZCODE_PERSONAL_PROVIDER_CONFIG_FILE"));
        return {current, root / ".zcode/v2/config.json"};
    }
    if (id == "codex")
        return {(env("CODEX_HOME").empty() ? home / ".codex" : path(env("CODEX_HOME"))) / "config.toml"};
    if (id == "claude")
        return {(env("CLAUDE_CONFIG_DIR").empty() ? home / ".claude" : path(env("CLAUDE_CONFIG_DIR"))) /
                "settings.json"};
    if (id == "cursor" && !env("CURSOR_CONFIG_DIR").empty())
        return {path(env("CURSOR_CONFIG_DIR")) / "cli-config.json"};
    if (id == "opencode") {
        if (!env("OPENCODE_CONFIG").empty())
            return {path(env("OPENCODE_CONFIG"))};
        auto root =
            (env("XDG_CONFIG_HOME").empty() ? home / ".config" : path(env("XDG_CONFIG_HOME"))) / "opencode";
        return {root / (fs::is_regular_file(root / "opencode.jsonc") ? "opencode.jsonc" : "opencode.json")};
    }
    if (id == "cline") {
        auto root = env("CLINE_DATA_DIR").empty()
                        ? (env("CLINE_DIR").empty() ? home / ".cline" : path(env("CLINE_DIR"))) / "data"
                        : path(env("CLINE_DATA_DIR"));
        return {root / "settings/providers.json"};
    }
    if (id == "copilot")
        return {(env("COPILOT_HOME").empty() ? home / ".copilot" : path(env("COPILOT_HOME"))) /
                "settings.json"};
    if (id == "kiro")
        return {home / ".kiro/settings/cli.json"};
    if (id == "openhands")
        return {home / ".openhands/agent_settings.json"};
    if (id == "roo")
        return {store.root / "agent-profiles/roo-code-settings.json"};
    if (id == "crush")
        return {(env("CRUSH_GLOBAL_CONFIG").empty()
                     ? (env("XDG_CONFIG_HOME").empty() ? home / ".config" : path(env("XDG_CONFIG_HOME"))) /
                           "crush"
                     : path(env("CRUSH_GLOBAL_CONFIG"))) /
                "crush.json"};
    if (id == "harness") {
        auto root = env("DSH_HOME").empty() ? home / ".dsh" : path(env("DSH_HOME"));
        std::vector<fs::path> paths;
        auto base = root / "profiles";
        if (fs::is_directory(base)) {
            for (auto &e : fs::directory_iterator(base))
                if (e.is_directory() && fs::is_regular_file(e.path() / "cordis.patch.yml"))
                    paths.push_back(e.path() / "cordis.patch.yml");
        }
        std::sort(paths.begin(), paths.end());
        auto web = base / "web/cordis.patch.yml";
        paths.erase(std::remove(paths.begin(), paths.end(), web), paths.end());
        paths.insert(paths.begin(), web);
        return paths;
    }
    std::vector<fs::path> out;
    auto config_item = item(id);
    for (auto &r : config_item["configs"])
        out.push_back(home / path(r));
    return out;
}
J Adapters::list() {
    J out = J::array();
    for (auto a : catalog) {
        std::string id = a["id"], cli = a["cli"];
        auto exe = executable(id);
        auto paths = configs(id);
        bool config = false, skills = false;
        J pathsJ = J::array();
        for (auto &p : paths) {
            pathsJ.push_back(display_path(p));
            std::error_code ec;
            config |= id != "workbuddy" && fs::is_regular_file(p, ec);
        }
        std::string targetPath;
        try {
            auto p = target(id);
            std::error_code ec;
            skills = fs::is_directory(p, ec);
            if (skills)
                targetPath = display_path(p);
        } catch (...) {
        }
        a["executable"] = display_path(exe);
        a["installation"] = installation(id);
        a["desktop_executable"] = a["installation"].value("desktop_executable", "");
        a["detected"] = a["installation"].value("installed", false);
        bool cliProduct = false;
        for (auto &choice : a["installation"].value("install_choices", J::array()))
            cliProduct |= choice["id"] == "cli";
        bool desktopPresent = !a["desktop_executable"].get<std::string>().empty();
        a["status"] =
            a["installation"].value("program_residue", false) ? "程序残留 · 可清理"
            : id == "roo" && a["detected"].get<bool>()        ? "扩展已安装"
            : desktopPresent && !cliProduct                   ? "桌面已安装"
            : desktopPresent && !exe.empty() && a["installation"]["compatibility"].value("compatible", false)
                ? "桌面与 CLI 已安装"
            : !exe.empty()
                ? (a["installation"]["compatibility"].value("compatible", false) ? "CLI 可用" : "CLI 不兼容")
            : a["detected"].get<bool>() ? (cli.empty() ? "桌面已安装" : "桌面已安装 · CLI 未安装")
            : config || skills          ? "未安装 · 保留配置"
                                        : "尚未安装";
        a["retained_data"] = config || skills;
        a["skill_path"] = targetPath;
        a["config_paths"] = pathsJ;
        a["config_exists"] = config;
        a["capabilities"] = capabilities(id);
        a["native_apply"] = a["capabilities"]["configure"];
        out.push_back(a);
    }
    return out;
}
std::vector<std::pair<std::string, fs::path>> Adapters::roots() {
    std::vector<std::pair<std::string, fs::path>> out;
    std::set<std::wstring> seen;
    auto add = [&](std::string name, fs::path p) {
        auto key = fs::absolute(p).lexically_normal().wstring();
        std::transform(key.begin(), key.end(), key.begin(), towlower);
        if (seen.insert(key).second)
            out.emplace_back(name, p);
    };
    for (auto &a : catalog) {
        auto id = a["id"].get<std::string>(), name = a["name"].get<std::string>();
        if (!a["skills"].get<std::string>().empty())
            add(name, target(id));
        if (store.setting("project").is_string() && !store.setting("project").get<std::string>().empty() &&
            !a["project_skills"].get<std::string>().empty()) {
            try {
                add(name + " · 项目", target(id, true));
            } catch (...) {
            }
        }
    }
    add("共享 Skills", home / ".agents/skills");
    add("共享 Skills", home / ".config/agents/skills");
    add("Codex 旧版", home / ".codex/skills");
    add("Codex 系统", home / ".codex/skills/.system");
    add("WorkBuddy 旧版目录", home / ".workbuddy/skills");
    return out;
}
static size_t offset(const std::string &s, toml::source_position p) {
    size_t i = 0;
    for (uint32_t line = 1; line < p.line; ++line) {
        auto n = s.find('\n', i);
        if (n == std::string::npos)
            throw std::runtime_error("无法定位 TOML 字段");
        i = n + 1;
    }
    for (uint32_t col = 1; col < p.column && i < s.size(); ++col) {
        unsigned char c = s[i++];
        while (i < s.size() && ((unsigned char)s[i] & 0xc0) == 0x80)
            ++i;
    }
    return i;
}
std::string Adapters::merge_codex(const std::string &original, const J &p, const std::string &var,
                                  std::string &display) {
    std::string s = original;
    if (s.compare(0, 3, "\xef\xbb\xbf") == 0)
        s.erase(0, 3);
    toml::table doc;
    try {
        doc = toml::parse(s);
    } catch (...) {
        throw std::runtime_error("原 Codex TOML 配置无效，未写入任何内容");
    }
    std::string pid = "agentdock_" + p["id"].get<std::string>();
    std::replace(pid.begin(), pid.end(), '-', '_');
    J fields = {
        {"name", p["name"]}, {"base_url", p["base_url"]}, {"env_key", var}, {"wire_api", "responses"}};
    struct Edit {
        size_t a, b;
        std::string text;
    };
    std::vector<Edit> edits;
    std::string prefix;
    auto replace = [&](toml::node *node, const std::string &key, const J &value, bool top) {
        if (node) {
            if (!node->is_string())
                throw std::runtime_error("已有 Codex 字段类型异常，未写入配置");
            auto src = node->source();
            edits.push_back({offset(s, src.begin), offset(s, src.end), value.dump(-1, ' ', false)});
        } else if (top)
            prefix += key + " = " + value.dump(-1, ' ', false) + "\n";
        else
            throw std::runtime_error(
                "已有同名 AgentDock provider 缺少字段，请新建模型连接后应用以保留原配置");
    };
    replace(doc.get("model"), "model", p["model"], true);
    replace(doc.get("model_provider"), "model_provider", pid, true);
    auto prov = doc["model_providers"][pid].as_table();
    std::string fragment = "[model_providers." + pid + "]\n";
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        fragment += it.key() + " = " + it.value().dump(-1, ' ', false) + "\n";
        if (prov)
            replace(prov->get(it.key()), it.key(), it.value(), false);
    }
    if (prov) {
        for (const char *key : {"experimental_bearer_token", "http_headers", "env_http_headers"})
            if (prov->contains(key))
                throw std::runtime_error("同名 provider 有额外认证设置，请使用新连接 ID 以保留原配置");
    }
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.a > b.a; });
    for (auto &e : edits)
        s.replace(e.a, e.b - e.a, e.text);
    s = prefix + s;
    if (!prov)
        s += "\n\n" + fragment;
    try {
        (void)toml::parse(s);
    } catch (...) {
        throw std::runtime_error("合并结果未通过 TOML 校验，未写入任何内容");
    }
    display = "model = " + p["model"].dump(-1, ' ', false) + "\nmodel_provider = " + J(pid).dump() + "\n\n" +
              fragment;
    return s;
}
J Adapters::preview(const std::string &id, const std::string &pid, const std::string &selectedModel) {
    std::lock_guard lock(store.mutex);
    auto a = item(id);
    auto [p, key] = models.get(pid);
    SecureZeroMemory(key.data(), key.size());
    if (!selectedModel.empty()) {
        auto ids = p.value("models", J::array());
        if (std::find(ids.begin(), ids.end(), selectedModel) == ids.end())
            throw std::runtime_error("所选模型不属于此连接");
        p["model"] = selectedModel;
    }
    auto protos = a["protocols"];
    if (!protos.empty() && std::find(protos.begin(), protos.end(), p["protocol"]) == protos.end())
        throw std::runtime_error("协议不兼容，请选择此 Agent 支持的模型协议");
    if (p.value("model", "").empty())
        throw std::runtime_error("请先填写模型 ID");
    if (id == "codex" && p.value("auth", "bearer") != "bearer")
        throw std::runtime_error("Codex env_key 使用 Bearer 认证；其他认证请在原工具按官方文档配置");
    if (p["protocol"] != "anthropic" && p.value("auth", "bearer") != "bearer")
        throw std::runtime_error(
            "此 Agent 的兼容接口使用 Bearer 认证；当前连接的认证头不兼容，请使用匹配的连接或原工具专用设置");
    if (p["protocol"] == "anthropic" && p.value("auth", "") != "x-api-key" && id != "claude" &&
        id != "factory")
        throw std::runtime_error("此 Agent 的 Anthropic 接入要求 x-api-key，请先更正连接认证方式");
    std::string var = p.value("key_env", "");
    if (var.empty()) {
        var = "AGENTDOCK_" + pid + "_API_KEY";
        std::transform(var.begin(), var.end(), var.begin(),
                       [](unsigned char c) { return c == '-' ? '_' : (char)toupper(c); });
    }
    auto paths = configs(id);
    fs::path file = paths.empty() ? fs::path() : paths[0];
    std::string raw;
    if (!file.empty() && fs::exists(file))
        raw = read(file, 2 * 1024 * 1024);
    std::string text, display, note;
    J extra = extra_preview(id, p, var);
    bool can = id == "codex" || id == "claude" || id == "qwen" || id == "opencode" ||
               (id == "codebuddy" && p["protocol"] != "anthropic");
    if (!extra.is_null()) {
        file = path(extra["path"]);
        raw = fs::exists(file) ? read(file, 2 * 1024 * 1024) : "";
        text = extra["content"];
        display = extra["display"];
        note = extra["note"];
        can = extra.value("apply", true);
    } else if (id == "codex") {
        text = merge_codex(raw, p, var, display);
        note = "只修改所列字段并保留其他 TOML 与注释。密钥通过本工具启动 CLI "
               "时注入；独立启动需设置对应环境变量。";
    } else if (id == "claude") {
        auto obj = raw.empty() ? J::object() : parse_jsonc(raw);
        if (!obj.is_object() || (obj.contains("env") && !obj["env"].is_object()))
            throw std::runtime_error("Claude 配置无法安全合并");
        std::string base = p["base_url"];
        if (base.size() >= 3 && base.substr(base.size() - 3) == "/v1")
            base.resize(base.size() - 3);
        obj["model"] = p["model"];
        if (!obj.contains("env"))
            obj["env"] = J::object();
        obj["env"]["ANTHROPIC_BASE_URL"] = base;
        auto &allowed = obj["availableModels"];
        if (allowed.is_null())
            allowed = J::array();
        if (!allowed.is_array())
            throw std::runtime_error("Claude availableModels 不是数组");
        for (auto &model : p["models"])
            if (std::find(allowed.begin(), allowed.end(), model) == allowed.end())
                allowed.push_back(model);
        obj["env"]["ANTHROPIC_MODEL"] = p["model"];
        auto [connection, credential] = models.get(pid);
        if (!credential.empty()) {
            bool bearer = p.value("auth", "") == "bearer";
            obj["env"].erase(bearer ? "ANTHROPIC_API_KEY" : "ANTHROPIC_AUTH_TOKEN");
            obj["env"][bearer ? "ANTHROPIC_AUTH_TOKEN" : "ANTHROPIC_API_KEY"] = credential;
            SecureZeroMemory(credential.data(), credential.size());
        } else if (obj["env"].contains("ANTHROPIC_API_KEY") || obj["env"].contains("ANTHROPIC_AUTH_TOKEN"))
            throw std::runtime_error(
                "原 Claude 设置含认证字段，但当前连接没有凭据；请先填写密钥，避免沿用错误的认证");
        text = obj.dump(2, ' ', false) + "\n";
        display = J{{"model", p["model"]},
                    {"availableModels", p["models"]},
                    {"env", {{"ANTHROPIC_BASE_URL", base}, {"ANTHROPIC_MODEL", p["model"]}}}}
                      .dump(2, ' ', false);
        note = "合并可选模型并设置默认项；保留权限与其他设置。按 Claude 官方 settings.env "
               "格式写入认证时原生文件含明文凭据（预览已隐藏）；项目、管理策略与登录态仍可能覆盖。";
    } else if (id == "qwen" || id == "opencode" || (id == "codebuddy" && can)) {
        if (p.value("auth", "bearer") != "bearer")
            throw std::runtime_error("此适配器仅支持已核实的 Bearer 认证；请使用原工具设置其他认证");
        auto obj = raw.empty() ? J::object() : parse_jsonc(raw);
        if (!obj.is_object())
            throw std::runtime_error("已有配置不是 JSON 对象");
        auto ids = p.value("models", J::array({p["model"]}));
        if (id == "opencode") {
            auto key = "agentdock_" + pid;
            auto &provider = obj["provider"];
            if (provider.is_null())
                provider = J::object();
            if (!provider.is_object())
                throw std::runtime_error("原 provider 类型不兼容");
            auto &entry = provider[key];
            if (entry.is_null())
                entry = J::object();
            if (!entry.is_object())
                throw std::runtime_error("同名 provider 类型不兼容");
            entry["npm"] = "@ai-sdk/openai-compatible";
            entry["name"] = p["name"];
            if (!entry.contains("options"))
                entry["options"] = J::object();
            entry["options"]["baseURL"] = p["base_url"];
            entry["options"]["apiKey"] = "{env:" + var + "}";
            if (!entry.contains("models"))
                entry["models"] = J::object();
            for (auto &m : ids) {
                auto &slot = entry["models"][m.get<std::string>()];
                if (slot.is_null())
                    slot = {{"name", m}};
            }
            obj["model"] = key + "/" + p["model"].get<std::string>();
            display = J{{"model", obj["model"]},
                        {"provider",
                         {{key,
                           {{"npm", entry["npm"]},
                            {"options", {{"baseURL", p["base_url"]}, {"apiKey", "{env:" + var + "}"}}},
                            {"models", ids}}}}}}
                          .dump(2, ' ', false);
        } else if (id == "qwen") {
            auto &bucket = obj["modelProviders"]["openai"];
            if (bucket.is_null())
                bucket = J::array();
            if (!bucket.is_array())
                throw std::runtime_error("原 Qwen modelProviders.openai 不是官方数组结构");
            for (auto &m : ids) {
                J entry = {{"id", m},
                           {"envKey", var},
                           {"baseUrl", p["base_url"]},
                           {"wireApi", p["protocol"] == "responses" ? "responses" : "chat-completions"}};
                for (auto &old : bucket)
                    if (old.is_object() && old.value("id", "") == m.get<std::string>() &&
                        old.value("baseUrl", "") != p["base_url"].get<std::string>())
                        throw std::runtime_error(
                            "Qwen 已有同名模型使用其他端点；请在 Qwen /model 中选择路由，避免错误切换");
                auto it = std::find_if(bucket.begin(), bucket.end(), [&](const J &old) {
                    return old.is_object() && old.value("id", "") == m.get<std::string>() &&
                           old.value("baseUrl", "") == p["base_url"].get<std::string>() &&
                           old.value("wireApi", "chat-completions") == entry["wireApi"].get<std::string>();
                });
                if (it == bucket.end())
                    bucket.push_back(entry);
                else
                    it->update(entry);
            }
            obj["model"]["name"] = p["model"];
            obj["security"]["auth"]["selectedType"] = "openai";
            display = J{
                {"model", {{"name", p["model"]}}},
                {"modelProviders", {{"openai", ids}}},
                {"baseUrl", p["base_url"]},
                {"envKey",
                 var}}.dump(2, ' ', false);
        } else {
            auto &list = obj["models"];
            if (list.is_null())
                list = J::array();
            if (!list.is_array())
                throw std::runtime_error("原 models 不是数组");
            J added = J::array();
            for (auto &m : ids) {
                auto it = std::find_if(list.begin(), list.end(), [&](const J &old) {
                    return old.is_object() && old.value("id", "") == m.get<std::string>();
                });
                J entry = {{"id", m},
                           {"name", m},
                           {"vendor", p["vendor"]},
                           {"apiKey", "${" + var + "}"},
                           {"url", p["base_url"].get<std::string>() +
                                       (p["protocol"] == "responses" ? "" : "/chat/completions")}};
                if (p["protocol"] == "responses")
                    entry["api"] = "openai-responses";
                if (it != list.end()) {
                    bool matches = it->value("apiKey", "") == entry["apiKey"].get<std::string>() &&
                                   it->value("url", "") == entry["url"].get<std::string>() &&
                                   it->value("api", "") == entry.value("api", "");
                    if (!matches)
                        throw std::runtime_error(
                            "原 models.json 已有同名且接入参数不同的模型；为保留原设置，未修改配置");
                } else
                    list.push_back(entry);
                added.push_back(entry);
            }
            if (obj.contains("availableModels") && !obj["availableModels"].empty()) {
                if (!obj["availableModels"].is_array())
                    throw std::runtime_error("原 availableModels 不是数组");
                for (auto &m : ids)
                    if (std::find(obj["availableModels"].begin(), obj["availableModels"].end(), m) ==
                        obj["availableModels"].end())
                        obj["availableModels"].push_back(m);
            }
            display =
                J{{"models", added}, {"availableModels", obj.value("availableModels", J::array())}}.dump(
                    2, ' ', false);
        }
        text = id == "opencode"
                   ? patch_json(patch_json(raw, "provider", obj["provider"]), "model", obj["model"])
                   : obj.dump(2, ' ', false) + "\n";
        note = "合并所选连接的完整模型列表，保留其他设置。凭据仅写入环境变量引用；请从本工具启动 CLI "
               "或在原工具运行环境设置变量。项目配置可覆盖用户配置。";
        if (id == "codebuddy")
            note += "这里只追加自定义模型，并保留原筛选项、加入新增模型；默认选择保持原值，请在原工具用 "
                    "/model 选择。";
    } else if (id == "harness") {
        std::string api = p["protocol"] == "responses"   ? "openai-responses"
                          : p["protocol"] == "anthropic" ? "anthropic-messages"
                                                         : "openai-completions";
        text = "- id: llm-pi-ai\n  config:\n    providers:\n      agentdock-" + pid.substr(0, 12) +
               ":\n        apiKeyEnv: " + var + "\n        api: " + api +
               "\n        baseURL: " + p["base_url"].dump(-1, ' ', false) + "\n        models:\n";
        for (auto &m : p["models"])
            text += "          - id: " + m.dump(-1, ' ', false) + "\n";
        display = text;
        note = "官方 profile 片段。默认 Web profile 是 profiles/web；请在 Harness "
               "模型页面添加，或手动合并到实际 profile，不能覆盖整份配置。";
    } else {
        display = text = J{{"provider", p["name"]}, {"base_url", p["base_url"]}, {"model", p["model"]},
                           {"models", p["models"]}, {"protocol", p["protocol"]}, {"api_key_environment", var}}
                             .dump(2, ' ', false);
        note = a["note"].get<std::string>() + " 这是接入参数，不是原生配置文件；请在官方设置中填写。";
    }
    J writes = J::array();
    if (!file.empty())
        writes.push_back({{"path", pathstr(file)}, {"content", text}, {"hash", sha(raw)}});
    if (extra.is_object() && extra.contains("writes"))
        for (auto &w : extra["writes"])
            writes.push_back(w);
    if (id == "codex") {
        for (auto &model : p["models"]) {
            auto name = "agentdock_" + pid.substr(0, 12) + "_" + sha(model.get<std::string>()).substr(0, 10);
            auto profile = file.parent_path() / (name + ".config.toml");
            auto before = fs::exists(profile) ? read(profile, 2 * 1024 * 1024) : "";
            auto after = patch_toml(before, "model", model);
            auto route = "agentdock_" + pid;
            std::replace(route.begin(), route.end(), '-', '_');
            after = patch_toml(after, "model_provider", route);
            writes.push_back({{"path", pathstr(profile)}, {"content", after}, {"hash", sha(before)}});
        }
        note += " 多个模型使用独立 .config.toml profile 文件，可通过 codex --profile 选择。";
    }
    for (auto it = previews.begin(); it != previews.end();)
        if (now() - it->second["created"].get<double>() > 600)
            it = previews.erase(it);
        else
            ++it;
    auto ticket = uuid();
    previews[ticket] = {{"path", pathstr(file)}, {"hash", sha(raw)},
                        {"content", text},       {"created", now()},
                        {"apply", can},          {"agent", id},
                        {"writes", writes},      {"provider", pid},
                        {"model", p["model"]},   {"revision", p.value("revision", "")}};
    return {{"ticket", ticket}, {"path", pathstr(file)}, {"content", display},
            {"can_apply", can}, {"note", note},          {"env", var}};
}
void Adapters::apply(const std::string &ticket) {
    std::lock_guard lock(store.mutex);
    auto it = previews.find(ticket);
    if (it == previews.end())
        throw std::runtime_error("预览已失效，请重新预览");
    auto p = it->second;
    previews.erase(it);
    if (!p["apply"].get<bool>() || now() - p["created"].get<double>() > 600)
        throw std::runtime_error("预览已过期或此工具仅支持参数导出");
    auto writes =
        p.value("writes", J::array({{{"path", p["path"]}, {"hash", p["hash"]}, {"content", p["content"]}}}));
    J before = J::array();
    J changed = J::array();
    for (auto &w : writes) {
        auto dest = path(w["path"]);
        check_path(dest);
        bool existed = fs::exists(dest);
        auto text = existed ? read(dest, 2 * 1024 * 1024) : "";
        if (sha(text) != w["hash"].get<std::string>())
            throw std::runtime_error("配置文件已变化，请重新预览后再应用");
        if (text == w["content"].get<std::string>())
            continue;
        before.push_back({{"path", w["path"]}, {"content", text}, {"existed", existed}});
        changed.push_back(w);
    }
    writes = std::move(changed);
    for (size_t i = 0; i < writes.size(); ++i) {
        auto &old = before[i];
        auto &w = writes[i];
        auto backup = store.root / "backups" /
                      (std::to_string((uint64_t)(now() * 1000000)) + "-" + uuid().substr(0, 6) + "-" +
                       pathstr(path(w["path"]).filename()));
        atomic_write(backup, old["content"]);
        atomic_write(
            path(pathstr(backup) + ".meta.json"),
            J{{"path", w["path"]}, {"existed", old["existed"]}, {"applied_hash", sha(w["content"])}}.dump());
    }
    size_t done = 0;
    try {
        for (auto &w : writes) {
            atomic_write(path(w["path"]), w["content"]);
            ++done;
        }
    } catch (...) {
        for (size_t i = 0; i < done; ++i) {
            try {
                if (before[i]["existed"].get<bool>())
                    atomic_write(path(before[i]["path"]), before[i]["content"]);
                else
                    fs::remove(path(before[i]["path"]));
            } catch (...) {
                store.event("error", "恢复配置失败", "请在设置中查看配置备份");
            }
        }
        throw;
    }
    if (p.contains("provider"))
        store.setting(
            "agent_binding:" + p["agent"].get<std::string>(),
            {{"provider", p["provider"]}, {"model", p["model"]}, {"revision", p.value("revision", "")}});
    else if (p.value("unbind", false))
        store.setting("agent_binding:" + p["agent"].get<std::string>(), nullptr);
    store.event("config", "已应用模型配置", item(p["agent"])["name"].get<std::string>() + " · 原配置已备份");
}
J Adapters::backups() {
    std::vector<fs::path> files;
    auto dir = store.root / "backups";
    if (fs::is_directory(dir))
        for (auto &e : fs::directory_iterator(dir)) {
            auto name = pathstr(e.path().filename());
            if (name.size() > 10 && name.substr(name.size() - 10) == ".meta.json")
                files.push_back(e.path());
        }
    std::sort(files.rbegin(), files.rend());
    J out = J::array();
    for (auto &p : files) {
        try {
            auto info = J::parse(read(p, 2 * 1024 * 1024));
            auto id = pathstr(p.filename());
            id.resize(id.size() - 10);
            out.push_back({{"id", id}, {"path", info["path"]}});
        } catch (...) {
        }
        if (out.size() == 30)
            break;
    }
    return out;
}
void Adapters::restore(const std::string &id) {
    std::lock_guard lock(store.mutex);
    if (id.empty() || path(id).filename() != path(id))
        throw std::runtime_error("备份 ID 无效");
    auto backup = store.root / "backups" / path(id);
    check_path(backup);
    auto meta = J::parse(read(path(pathstr(backup) + ".meta.json"), 2 * 1024 * 1024));
    auto dest = path(meta["path"]);
    check_path(dest);
    auto current = fs::exists(dest) ? read(dest, 2 * 1024 * 1024) : std::string();
    if (sha(current) != meta["applied_hash"].get<std::string>())
        throw std::runtime_error("配置在应用后已被修改，为保留您的改动已停止恢复");
    if (meta["existed"].get<bool>())
        atomic_write(dest, read(backup, 2 * 1024 * 1024));
    else
        fs::remove(dest);
    store.event("config", "已恢复原配置", pathstr(dest.filename()));
}
} // namespace ad

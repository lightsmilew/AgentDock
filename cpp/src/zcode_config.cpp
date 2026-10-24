#include "core.hpp"
#include <algorithm>

namespace ad {
namespace {
J document(const std::string &raw) {
    auto doc = raw.empty()
                   ? J{{"schemaVersion", 1},
                       {"config",
                        {{"providerConfigRules", {{"providerRules", J::array()}}},
                         {"modelConfigRules",
                          {{"providerModelRules", J::array()}, {"manualProviderModelRules", J::array()}}}}}}
                   : J::parse(raw);
    if (!doc.is_object() || doc.value("schemaVersion", 0) != 1 || !doc.contains("config") ||
        !doc["config"].is_object() || !doc["config"].contains("providerConfigRules") ||
        !doc["config"]["providerConfigRules"].is_object() ||
        !doc["config"]["providerConfigRules"].contains("providerRules") ||
        !doc["config"]["providerConfigRules"]["providerRules"].is_array() ||
        !doc["config"].contains("modelConfigRules") || !doc["config"]["modelConfigRules"].is_object())
        throw std::runtime_error("ZCode 模型配置格式不兼容，请先在新版 ZCode 中完成配置迁移");
    for (auto key : {"providerModelRules", "manualProviderModelRules"})
        if (!doc["config"]["modelConfigRules"].contains(key) ||
            !doc["config"]["modelConfigRules"][key].is_array())
            throw std::runtime_error("ZCode 模型规则格式不兼容，原文件已保留");
    return doc;
}
} // namespace
std::string merge_zcode(const std::string &raw, const J &connection, const std::string &key) {
    if (key.empty())
        throw std::runtime_error("ZCode 的 API Key 接入需要填写密钥");
    auto doc = document(raw);
    auto route = "agentdock-" + connection.at("id").get<std::string>();
    auto &config = doc["config"];
    auto &providers = config["providerConfigRules"]["providerRules"];
    auto it = std::find_if(providers.begin(), providers.end(),
                           [&](const J &p) { return p.is_object() && p.value("providerId", "") == route; });
    if (it == providers.end()) {
        providers.push_back({{"providerId", route}, {"config", J::object()}});
        it = providers.end() - 1;
    }
    if (!(*it)["config"].is_object())
        throw std::runtime_error("原 ZCode 供应商配置不是对象");
    (*it)["providerName"] = connection.at("name");
    (*it)["enabled"] = true;
    auto &provider = (*it)["config"];
    provider["group"] = "standard-personal";
    provider["access"] = {{"type", "api-key"}, {"apiKey", key}};
    auto protocol = connection.at("protocol").get<std::string>();
    if (protocol != "chat" && protocol != "responses" && protocol != "anthropic")
        throw std::runtime_error("ZCode 接口协议不兼容");
    provider["api"] = {{"type", protocol == "responses"   ? "openai-responses"
                                : protocol == "anthropic" ? "anthropic-messages"
                                                          : "openai-chat-completions"},
                       {"baseUrl", connection.at("base_url")}};
    auto ids = provider.value("personalModelIds", J::array());
    if (!ids.is_array())
        throw std::runtime_error("ZCode 原模型列表不是数组");
    for (auto &id : connection.at("models"))
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    provider["personalModelIds"] = ids;
    provider["modelOrder"] = ids;
    auto order = config.value("providerOrder", J::array());
    if (!order.is_array())
        throw std::runtime_error("ZCode 供应商顺序格式不兼容");
    if (std::find(order.begin(), order.end(), route) == order.end())
        order.push_back(route);
    config["providerOrder"] = order;
    config["defaultModelSelection"] = {{"providerId", route}, {"modelId", connection.at("model")}};
    return doc.dump(2, ' ', false) + "\n";
}
J zcode_model_rows(const J &doc) {
    J rows = J::array();
    if (doc.contains("schemaVersion")) {
        auto validated = document(doc.dump());
        auto &config = validated["config"];
        auto selected = config.value("defaultModelSelection", J::object());
        for (auto &provider : config["providerConfigRules"]["providerRules"]) {
            if (!provider.is_object() || !provider.value("enabled", true))
                continue;
            auto route = provider.value("providerId", "");
            auto value = provider.value("config", J::object());
            for (auto &id : value.value("personalModelIds", J::array()))
                if (id.is_string())
                    rows.push_back({{"id", id},
                                    {"provider", route},
                                    {"selection", J::array({route, id}).dump()},
                                    {"role", selected.value("providerId", "") == route &&
                                                     selected.value("modelId", "") == id.get<std::string>()
                                                 ? "默认模型"
                                                 : "已配置"}});
        }
    } else if (doc.contains("provider") && doc["provider"].is_object()) {
        auto selected = doc.value("model", J::object());
        if (selected.is_object() && selected.contains("main"))
            selected = selected["main"];
        for (auto &[route, provider] : doc["provider"].items()) {
            if (!provider.is_object() || !provider.value("enabled", true))
                continue;
            auto models = provider.value("models", J::object());
            if (models.is_object())
                for (auto &[id, value] : models.items())
                    if (!value.is_object() || !value.value("deleted", false))
                        rows.push_back({{"id", id}, {"provider", route}, {"role", "旧版配置 · 请先迁移"}});
        }
    }
    return rows;
}
std::string select_zcode_model(const std::string &raw, const std::string &selection) {
    auto doc = document(raw);
    auto chosen = J::parse(selection);
    if (!chosen.is_array() || chosen.size() != 2 || !chosen[0].is_string() || !chosen[1].is_string())
        throw std::runtime_error("ZCode 模型选择无效");
    bool found = false;
    for (auto &row : zcode_model_rows(doc))
        found |= row["selection"] == selection;
    if (!found)
        throw std::runtime_error("此模型未在 ZCode 已启用的供应商中配置");
    doc["config"]["defaultModelSelection"] = {{"providerId", chosen[0]}, {"modelId", chosen[1]}};
    return doc.dump(2, ' ', false) + "\n";
}
} // namespace ad

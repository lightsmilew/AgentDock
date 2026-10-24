#include "core.hpp"
#include "yaml.h"
#include <functional>
#include <set>
namespace ad {
namespace {
struct Document {
    yaml_document_t doc{};
    Document(const std::string &s) {
        yaml_parser_t parser;
        if (!yaml_parser_initialize(&parser))
            throw std::runtime_error("YAML 初始化失败");
        yaml_parser_set_input_string(&parser, (const unsigned char *)s.data(), s.size());
        bool ok = yaml_parser_load(&parser, &doc) != 0;
        yaml_parser_delete(&parser);
        if (!ok)
            throw std::runtime_error("Harness YAML 无效，未修改");
    }
    ~Document() {
        yaml_document_delete(&doc);
    }
    yaml_node_t *node(int id) {
        return yaml_document_get_node(&doc, id);
    }
};
std::string scalar(yaml_node_t *n) {
    return n && n->type == YAML_SCALAR_NODE ? std::string((char *)n->data.scalar.value, n->data.scalar.length)
                                            : "";
}
size_t byteIndex(const std::string &s, size_t index) {
    size_t at = 0;
    while (index-- && at < s.size()) {
        ++at;
        while (at < s.size() && ((unsigned char)s[at] & 0xc0) == 0x80)
            ++at;
    }
    return at;
}
yaml_node_t *field(Document &d, yaml_node_t *n, const std::string &key) {
    if (!n || n->type != YAML_MAPPING_NODE)
        return nullptr;
    std::set<std::string> seen;
    yaml_node_t *result = nullptr;
    for (auto p = n->data.mapping.pairs.start; p != n->data.mapping.pairs.top; ++p) {
        auto k = scalar(d.node(p->key));
        if (k == "<<" || !seen.insert(k).second)
            throw std::runtime_error("Harness 使用 YAML 合并键或重复键，请在原工具整理后再配置");
        if (k == key)
            result = d.node(p->value);
    }
    return result;
}
} // namespace
static std::string patch_plugin(const std::string &input, const std::string &pluginId, const J &settings,
                                const std::vector<std::string> &prefix = {"config"}) {
    J config = settings;
    for (size_t i = prefix.size(); i > 1; --i)
        config = J{{prefix[i - 1], config}};
    auto source = input;
    if (trim(source).empty())
        return "- id: " + pluginId + "\n  config: " + config.dump() + "\n";
    auto update = [&](const std::vector<std::string> &keys, const J &value) {
        Document d(source);
        auto root = yaml_document_get_root_node(&d.doc);
        if (!root || root->type != YAML_SEQUENCE_NODE)
            throw std::runtime_error("Harness profile 不是插件列表");
        yaml_node_t *plugin = nullptr;
        for (auto p = root->data.sequence.items.start; p != root->data.sequence.items.top; ++p) {
            auto n = d.node(*p);
            if (scalar(field(d, n, "id")) == pluginId) {
                if (plugin)
                    throw std::runtime_error("Harness 有重复 llm-pi-ai 项");
                plugin = n;
            }
        }
        if (!plugin) {
            if (source.back() != '\n')
                source += '\n';
            source += "- id: " + pluginId + "\n  config: " + config.dump() + "\n";
            return;
        }
        auto node = plugin;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (node->type != YAML_MAPPING_NODE)
                throw std::runtime_error("Harness 配置节点不是映射");
            auto next = field(d, node, keys[i]);
            if (!next) {
                J nested = value;
                for (size_t j = keys.size() - 1; j > i; --j)
                    nested = J{{keys[j], nested}};
                if (node->data.mapping.style == YAML_FLOW_MAPPING_STYLE) {
                    auto at = byteIndex(source, node->start_mark.index) + 1;
                    source.insert(
                        at, J(keys[i]).dump() + ": " + nested.dump() +
                                (node->data.mapping.pairs.start == node->data.mapping.pairs.top ? "" : ", "));
                } else {
                    auto first = node->data.mapping.pairs.start;
                    if (first == node->data.mapping.pairs.top)
                        throw std::runtime_error("Harness 空映射格式异常");
                    auto key = d.node(first->key);
                    auto indent = std::string(key->start_mark.column, ' ');
                    source.insert(byteIndex(source, key->start_mark.index),
                                  J(keys[i]).dump() + ": " + nested.dump() + "\n" + indent);
                }
                return;
            }
            if (next->start_mark.index <= node->start_mark.index ||
                next->end_mark.index > node->end_mark.index)
                throw std::runtime_error("Harness 此字段引用了共享 YAML 节点，请在原工具编辑");
            if (i + 1 == keys.size()) {
                // Anchored/aliased shared nodes can point outside this branch; refuse them.
                if (next->start_mark.index <= node->start_mark.index ||
                    next->end_mark.index > node->end_mark.index)
                    throw std::runtime_error("Harness 此字段引用了共享 YAML 节点，请在原工具编辑");
                auto start = byteIndex(source, next->start_mark.index);
                size_t end = byteIndex(source, next->end_mark.index);
                while (end > start && isspace((unsigned char)source[end - 1]))
                    --end;
                source.replace(start, end - start, value.dump());
                return;
            }
            node = next;
        }
    };
    for (auto &[key, value] : settings.items()) {
        auto keys = prefix;
        keys.push_back(key);
        update(keys, value);
    }
    Document checked(source);
    return source;
}
std::string patch_harness(const std::string &input, const std::string &route, const J &settings) {
    return patch_plugin(input, "llm-pi-ai", settings, {"config", "providers", route});
}
std::string patch_harness_default(const std::string &input, const std::string &route,
                                  const std::string &model) {
    return patch_plugin(input, "agent-default-model", {{"provider", route}, {"model", model}});
}
} // namespace ad

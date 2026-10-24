#pragma once
#include "core.hpp"
#include <algorithm>
namespace ad {
struct SkillCategory {
    const char *name, *icon;
    unsigned ink, fill;
};
inline const std::vector<SkillCategory> &skill_categories() {
    static const std::vector<SkillCategory> rows = {
        {"开发效率", "code", 0x3878DC, 0xEAF2FF},   {"数据分析", "chart", 0x168D96, 0xE5F6F5},
        {"文档写作", "write", 0x8B61C8, 0xF2EBFC},  {"设计创作", "palette", 0xCE6390, 0xFCECF3},
        {"自动化", "workflow", 0xD88C30, 0xFFF3DF}, {"研究检索", "research", 0x5570CF, 0xEDF0FC},
        {"安全审查", "shield", 0x32876D, 0xE8F5EE}, {"通用工具", "tool", 0x637996, 0xEDF2F8}};
    return rows;
}
inline std::string classify_skill(const std::string &name, const std::string &description,
                                  const std::string &requested = "") {
    if (!requested.empty() && requested != "通用工具")
        return requested;
    auto text = wide(name + " " + description);
    std::transform(text.begin(), text.end(), text.begin(), towlower);
    const std::vector<std::pair<const char *, std::vector<std::wstring>>> rules = {
        {"安全审查", {L"security", L"vulnerability", L"pentest", L"安全", L"漏洞", L"审计"}},
        {"数据分析",
         {L"spreadsheet", L"excel", L"csv", L"sql", L"数据分析", L"统计", L"可视化", L"data-analysis"}},
        {"设计创作", {L"design", L"figma", L"image", L"illustrat", L"设计", L"绘图", L"配色", L"视频"}},
        {"研究检索", {L"research", L"search", L"literature", L"文献", L"检索", L"调研", L"研究"}},
        {"文档写作",
         {L"document", L"writing", L"markdown", L"docx", L"pdf", L"ppt", L"写作", L"文档", L"翻译", L"演示"}},
        {"开发效率",
         {L"code", L"debug", L"git", L"test", L"refactor", L"代码", L"开发", L"测试", L"编程", L"构建"}},
        {"自动化",
         {L"automat", L"workflow", L"schedule", L"browser", L"自动化", L"批量", L"工作流", L"定时"}}};
    for (auto &[category, words] : rules)
        for (auto &word : words)
            if (text.find(word) != std::wstring::npos)
                return category;
    return "通用工具";
}
inline SkillCategory skill_visual(const std::string &category) {
    if (category == "内容创作")
        return skill_categories()[2];
    if (category == "知识管理")
        return skill_categories()[5];
    for (auto &row : skill_categories())
        if (category == row.name)
            return row;
    return skill_categories().back();
}
} // namespace ad

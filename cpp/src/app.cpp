#include "official_install.hpp"
#include "skill_category.hpp"
#include "ui.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ad {
static std::string s(const J &j, const char *k) {
    return j.value(k, "");
}
static std::string fold(std::string v) {
    for (char &c : v)
        if ((unsigned char)c < 128)
            c = (char)tolower(c);
    return v;
}
static std::string timeText(double when) {
    time_t t = (time_t)when;
    tm local{};
    localtime_s(&local, &t);
    char b[48];
    strftime(b, sizeof b, "%m-%d %H:%M", &local);
    return b;
}
static const char *pages[] = {"工作空间", "Skill 工具库", "Agent 管理", "模型服务", "Skill 生成器", "设置"};
static const char *glyphs[] = {"home", "skills", "agent", "cloud", "generate", "settings"};
void App::start() {
    SetEnvironmentVariableW(L"AGENTDOCK_TEST_SCOPES", store.setting("ui_fixture") == true ? L"1" : nullptr);
    navigationTrace = env("AGENTDOCK_NAV_TRACE");
    auto options = store.setting("generator_options");
    if (options.is_object())
        generationOptions = options;
    auto gen = store.setting("generator");
    if (gen.is_object()) {
        prompt = gen.value("prompt", "");
        draft = gen.value("draft", "");
        genProvider = gen.value("provider", "");
    }
    agents = adapters.catalog;
    for (auto &a : agents) {
        a["detected"] = false;
        a["status"] = "等待检测";
        a["executable"] = "";
        a["desktop_executable"] = "";
        a["skill_path"] = "";
        a["native_apply"] = adapters.capabilities(a["id"])["configure"];
        a["installation"] = {{"pending", true}};
    }
    backupRows = J::array();
    presets = resource_json(100)["presets"];
    refresh();
    open(store.setting("ui_fixture") == true ? "AgentDock-QA · 测试数据" : "AgentDock · 本地管理器", 1220,
         820);
    requestRefresh();
    cancelJob = [this] {
        models.cancel();
        store.cancel_scan = true;
        adapters.cancel_install = true;
    };
}
void App::refresh() {
    stats = store.stats();
    stats["last_scan"] = store.setting("last_scan");
    auto project = store.setting("project");
    projectPath = project.is_string() ? project.get<std::string>() : "";
    recentRows = store.list("", "managed", "all", 4)["items"];
    if (!globalQuery.empty())
        globalSkills = store.list(globalQuery, "all", "all", 50)["items"];
    providers = models.list();
    skillRows = store.list(query,
                           scope == 1   ? "managed"
                           : scope == 2 ? "external"
                           : scope == 3 ? "favorites"
                                        : "all",
                           category, 50, offset);
    if (offset > 0 && skillRows["items"].empty()) {
        offset = 0;
        skillRows = store.list(query,
                               scope == 1   ? "managed"
                               : scope == 2 ? "external"
                               : scope == 3 ? "favorites"
                                            : "all",
                               category);
    }
    invalidate();
    if (hwnd)
        requestRefresh();
}
void App::navigate(int p) {
    if (!navigationTrace.empty()) {
        QueryPerformanceCounter(&navigationStart);
        navigationPaintPending = true;
    }
    dismissPopup();
    if (page == 4) {
        prompt = value(110);
        draft = value(111);
    }
    if (!globalQuery.empty()) {
        globalQuery.clear();
        value(90, "");
    }
    hideEdits();
    page = p;
    scroll = 0;
    scrollMax = 0;
    selection.clear();
    if (edits.count(100))
        value(100, page == 1 ? query : page == 2 ? agentQuery : page == 3 ? modelQuery : "");
    invalidate();
    if (navigationPaintPending) {
        LARGE_INTEGER end{}, frequency{};
        QueryPerformanceCounter(&end);
        QueryPerformanceFrequency(&frequency);
        navigationHandlerMs = (end.QuadPart - navigationStart.QuadPart) * 1000.0 / frequency.QuadPart;
    }
}
void App::draw() {
    caption(true);
    edit(90, box(std::max(290.f, width / 2 - 150), 13, 300, 30), "", false, false, false,
         "搜索 Skill / Agent / 模型", true);
    if (!globalQuery.empty())
        button(
            "", box(std::max(290.f, width / 2 - 150) + 308, 13, 30, 30),
            [this] {
                value(90, "");
                changed(-2);
                SetFocus(edits[90]);
            },
            3, "close");
    rect(box(0, 56, 210, height - 56), WHITE);
    line(209.5f, 56, 209.5f, height, LINE, 1);
    for (int i = 0; i < 6; ++i) {
        float y = i == 5 ? height - 77 : 80 + i * 47;
        Rect r = box(14, y, 182, 39);
        if (page == i)
            rect(r, SOFT, 11);
        icon(glyphs[i], 29, y + 9, 21, page == i ? BLUE : BODY);
        text(pages[i], box(62, y, 120, 39), 13, page == i ? BLUE : BODY, page == i);
        hit(r, [this, i] { navigate(i); });
    }
    text("v1.0.0", box(25, height - 31, 180, 19), 10, MUTED);
    float x = 242, y = 84, w = width - x - 34;
    if (!globalQuery.empty()) {
        searchResults(x, y, w);
        return;
    }
    if (page == 0) {
        button("新建 Skill", box(x, y, 112, 36), [this] { skillDialog(); }, 1, "add");
        button("扫描本机", box(x + 124, y, 112, 36), [this] { scan(); }, 0, "scan", !busy());
        button("导入 Skill", box(x + 248, y, 112, 36), [this] { importDialog(); }, 0, "folder");
        button("生成 Skill", box(x + 372, y, 112, 36), [this] { navigate(4); }, 0, "generate");
        overview(x, y + 52, w);
    } else if (page == 1) {
        button("部署记录", box(width - 366, y, 112, 36), [this] { deploymentDialog(); }, 0);
        button("导入", box(width - 242, y, 88, 36), [this] { importDialog(); }, 0, "folder");
        button("新建 Skill", box(width - 140, y, 106, 36), [this] { skillDialog(); }, 1, "add");
        skills(x, y, w);
    } else if (page == 2) {
        button(
            searchingDisks ? "搜索中…" : "重新检测", box(width - 145, y, 111, 36),
            [this] { detectionMenu(); }, 0, "scan");
        agentsPage(x, y, w);
    } else if (page == 3) {
        button("添加连接", box(width - 145, y, 111, 36), [this] { providerDialog(); }, 1, "add");
        modelsPage(x, y, w);
    } else if (page == 4)
        generatePage(x, 82, w);
    else
        settings(x, 82, w);
}
void App::overview(float x, float start, float w) {
    beginScroll(box(x, start, w, height - start - 24));
    float y = start - scroll;
    float cw = (w - 36) / 4;
    int detected = 0;
    for (auto &a : agents)
        if (a["detected"].get<bool>())
            ++detected;
    std::string labels[] = {"Skills", "Agent", "模型连接", "本地数据"},
                values[] = {std::to_string(stats["total"].get<int>()),
                            std::to_string(detected) + " / " + std::to_string(agents.size()),
                            std::to_string(providers.size()),
                            stats.value("storage_ready", false) ? bytes(stats["data_bytes"]) : "统计中"};
    for (int i = 0; i < 4; ++i) {
        float xx = x + i * (cw + 12);
        rect(box(xx, y, cw, 100), WHITE, 13, LINE);
        icon(glyphs[i == 3 ? 5 : i + 1], xx + 16, y + 17, 20);
        text(labels[i], box(xx + 47, y + 13, cw - 55, 26), 12, MUTED);
        text(values[i], box(xx + 18, y + 46, cw - 36, 39), 26, INK, true);
    }
    y += 124;
    text("Agent", box(x, y, 150, 30), 15, INK, true);
    button("查看全部", box(x + w - 86, y, 86, 28), [this] { navigate(2); }, 3);
    y += 43;
    int cols = w < 880 ? 3 : 4;
    float aw = (w - 12 * (cols - 1)) / cols;
    for (int i = 0; i < std::min(8, (int)agents.size()); ++i) {
        int row = i / cols, col = i % cols;
        float xx = x + col * (aw + 12), yy = y + row * 87;
        auto a = agents[i];
        rect(box(xx, yy, aw, 75), WHITE, 12, LINE);
        brand(a["id"], box(xx + 15, yy + 17, 39, 39));
        text(a["name"], box(xx + 66, yy + 12, aw - 78, 27), 12, INK, true);
        text(a["detected"].get<bool>() ? "已发现" : "尚未检测到", box(xx + 66, yy + 40, aw - 78, 20), 10,
             a["detected"].get<bool>() ? BLUE : MUTED);
        hit(box(xx, yy, aw, 75), [this, a] { agentDialog(a["id"]); });
    }
    y += ((std::min(8, (int)agents.size()) + cols - 1) / cols) * 87 + 14;
    float left = w * .57f;
    text("最近 Skill", box(x, y, left, 30), 15, INK, true);
    text("动态", box(x + left + 20, y, w - left - 20, 30), 15, INK, true);
    y += 42;
    rect(box(x, y, left, 236), WHITE, 13, LINE);
    auto rows = recentRows;
    int i = 0;
    for (auto &sk : rows) {
        float yy = y + 10 + i * 54;
        auto visual = skill_visual(s(sk, "category"));
        rect(box(x + 12, yy + 8, 30, 32), visual.fill, 9);
        icon(visual.icon, x + 17, yy + 14, 20, visual.ink);
        text(sk["title"], box(x + 51, yy, left - 70, 28), 12, INK, true);
        text(sk["description"], box(x + 51, yy + 25, left - 70, 21), 11, MUTED);
        hit(box(x, yy, left, 50), [this, sk] { skillDialog(sk["id"]); });
        ++i;
    }
    rect(box(x + left + 20, y, w - left - 20, 236), WHITE, 13, LINE);
    i = 0;
    for (auto &e : stats["events"]) {
        if (i >= 4)
            break;
        float xx = x + left + 36, yy = y + 14 + i * 53;
        rect(box(xx, yy + 8, 5, 5), BLUE, 2);
        text(e["title"], box(xx + 18, yy, w - left - 66, 25), 11, BODY);
        text(timeText(e["created"]), box(xx + 18, yy + 24, w - left - 66, 18), 10, MUTED);
        ++i;
    }
    if (!i)
        text("暂无操作记录", box(x + left + 40, y + 20, w - left - 60, 40), 12, MUTED);
    y += 252;
    endScroll(y - (start - scroll));
}
void App::skills(float x, float start, float w) {
    edit(100, box(x, start, 320, 36), query, false, false, false, "搜索 Skill", true);
    const char *tabs[] = {"全部", "工具库", "已发现", "收藏"};
    for (int i = 0; i < 4; ++i)
        button(
            tabs[i], box(x + i * 68, start + 46, 62, 32),
            [this, i] {
                scope = i;
                offset = 0;
                scroll = 0;
                selection.clear();
                refresh();
            },
            scope == i ? 2 : 3);
    button(
        category == "all" ? "全部分类" : category, box(x + 288, start + 46, 150, 32),
        [this] {
            std::vector<Choice> items = {{"all", "全部分类", "", ""}};
            for (auto &c : skillRows["categories"])
                items.push_back(
                    {c["category"], c["category"], std::to_string(c["count"].get<int>()) + " 个 Skill", ""});
            picker("选择分类", items, [this](std::string v) {
                category = v;
                offset = 0;
                scroll = 0;
                refresh();
            });
        },
        5, "down");
    float top = start + 94;
    beginScroll(box(x, top, w, height - top - 86));
    float y = top - scroll;
    for (auto &sk : skillRows["items"]) {
        bool selected = selection == s(sk, "id");
        rect(box(x, y, w, 84), selected ? 0xF0F6FF : WHITE, 12, selected ? BLUE : LINE);
        auto visual = skill_visual(s(sk, "category"));
        rect(box(x + 15, y + 19, 43, 43), visual.fill, 12);
        icon(visual.icon, x + 24, y + 28, 25, visual.ink);
        text(sk["title"], box(x + 74, y + 10, w - 285, 28), 14, INK, true);
        text(sk["description"], box(x + 74, y + 38, w - 285, 24), 12, MUTED);
        text(s(sk, "category") + " · " + (sk["managed"] != 0 ? "工具库" : "外部只读") +
                 (sk["enabled"] == 0 ? " · 已停用" : ""),
             box(x + w - 275, y + 26, 150, 24), 11, MUTED);
        hit(box(x, y, w - 112, 84), [this, sk] {
            selection = sk["id"];
            invalidate();
        });
        button(
            "", box(x + w - 104, y + 25, 32, 32),
            [this, sk] {
                store.toggle(sk["id"], "favorite", sk["favorite"] == 0);
                refresh();
            },
            sk["favorite"] != 0 ? 2 : 3, sk["favorite"] != 0 ? "star-fill" : "star");
        button("打开", box(x + w - 65, y + 25, 50, 32), [this, sk] { skillDialog(sk["id"]); }, 3);
        y += 94;
    }
    if (skillRows["items"].empty()) {
        rect(box(x, top, w, 220), WHITE, 13, LINE);
        icon("skills", x + w / 2 - 15, top + 41, 30, MUTED);
        text("没有匹配的 Skill", box(x + 20, top + 89, w - 40, 30), 16, INK, true, false, 1);
        text("尝试扫描本机、导入文件夹，或创建新 Skill。", box(x + 20, top + 124, w - 40, 30), 12, MUTED,
             false, false, 1);
    }
    endScroll(y - (top - scroll));
    float fy = height - 68;
    int total = skillRows["total"];
    text("共 " + std::to_string(total) + " 项 · " + std::to_string(offset / 50 + 1) + " / " +
             std::to_string(std::max(1, (total + 49) / 50)) + " 页",
         box(x, fy, 185, 34), 11, MUTED);
    button(
        "", box(x + 184, fy, 32, 32),
        [this] {
            offset = std::max(0, offset - 50);
            scroll = 0;
            selection.clear();
            refresh();
        },
        0, "prev", offset > 0);
    button(
        "", box(x + 224, fy, 32, 32),
        [this] {
            offset += 50;
            scroll = 0;
            selection.clear();
            refresh();
        },
        0, "next", offset + 50 < total);
    button(
        "导出", box(x + w - 280, fy, 80, 34), [this] { exportSkill(selection); }, 0, "", !selection.empty());
    button(
        "部署", box(x + w - 188, fy, 80, 34), [this] { deployDialog(selection); }, 0, "", !selection.empty());
    button(
        "编辑", box(x + w - 96, fy, 96, 34), [this] { skillDialog(selection); }, 1, "", !selection.empty());
}
void App::agentsPage(float x, float start, float w) {
    edit(100, box(x, start, 270, 36), agentQuery, false, false, false, "搜索 Agent", true);
    button(
        detectedOnly ? "仅显示已检测" : "显示全部", box(x + 284, start, 130, 36),
        [this] {
            detectedOnly = !detectedOnly;
            scroll = 0;
            invalidate();
        },
        detectedOnly ? 2 : 0);
    text(std::to_string(agents.size()) + " 个适配器", box(x + 430, start, 140, 36), 12, MUTED);
    float top = start + 52, y = top - scroll;
    beginScroll(box(x, top, w, height - top - 24));
    int n = 0;
    for (auto &a : agents) {
        if (detectedOnly && !a["detected"].get<bool>())
            continue;
        if (!agentQuery.empty() &&
            fold(s(a, "name") + s(a, "vendor")).find(fold(agentQuery)) == std::string::npos)
            continue;
        rect(box(x, y, w, 96), WHITE, 13, LINE);
        brand(a["id"], box(x + 18, y + 22, 44, 44));
        auto health = a["installation"].value("compatibility", J::object());
        text(a["name"], box(x + 80, y + 13, 240, 28), 15, INK, true);
        text(a["status"], box(x + 80, y + 44, 240, 23), 11,
             !health.value("message", "").empty() ? RED
             : a["detected"].get<bool>()          ? GREEN
                                                  : MUTED);
        bool retainedOnly = !a["detected"].get<bool>() && a.value("retained_data", false);
        text(retainedOnly                    ? "已保留本地数据"
             : a["native_apply"].get<bool>() ? "配置预览 · 备份 · 应用"
             : s(a, "skills").empty()        ? "官方导入 / 设置"
                                             : "原生 Skill 目录",
             box(x + 340, y + 15, w - 620, 24), 11, BLUE);
        text(!health.value("message", "").empty() ? health.value("message", "")
             : retainedOnly                       ? "用户配置与 Skill 可在重新安装后继续使用"
             : s(a, "skill_path").empty()         ? s(a, "note")
                                                  : s(a, "skill_path"),
             box(x + 340, y + 44, w - 620, 25), 11, MUTED);
        button(
            expandedAgent == a["id"].get<std::string>() ? "收起" : "管理", box(x + w - 94, y + 27, 72, 42),
            [this, a] { agentDialog(a["id"]); }, 0);
        auto install = a["installation"];
        bool installing = installingAgent == a["id"].get<std::string>() && busy();
        button(
            installing ? (cancellingInstall ? "取消中" : "取消") : "安装", box(x + w - 246, y + 27, 64, 42),
            [this, a] { installationAction(a["id"]); }, 2, "",
            installing ? !cancellingInstall
                       : !busy() && !install.value("pending", false) &&
                             (install.value("repairable", false) || install.value("can_add_variant", false)));
        button(
            !install.value("installed", false) && install.value("user_data_removable", false) ? "清理"
                                                                                              : "卸载",
            box(x + w - 170, y + 27, 64, 42), [this, a] { installationAction(a["id"], true); }, 0, "",
            !busy() && install.value("can_uninstall", false));
        y += 108;
        if (expandedAgent == a["id"].get<std::string>())
            y += agentPanel(x, y, w) + 14;
        ++n;
    }
    if (!n)
        text("没有匹配的 Agent", box(x, top + 40, w, 50), 14, MUTED, false, false, 1);
    endScroll(y - (top - scroll));
}
float App::agentPanel(float x, float start, float w) {
    auto a = adapters.item(expandedAgent);
    J installed = J::object();
    for (auto &agent : agents)
        if (agent["id"] == expandedAgent) {
            a = agent;
            installed = agent["installation"];
            break;
        }
    auto capability = adapters.capabilities(expandedAgent);
    auto configured = agentState.value("models", J::array());
    agentConfiguredOffset =
        std::clamp(agentConfiguredOffset, 0, std::max(0, ((int)configured.size() - 1) / 6 * 6));
    struct ProgramRow {
        std::string label, value, kind;
    };
    std::vector<ProgramRow> paths;
    if (expandedAgent == "roo") {
        paths.push_back({"宿主编辑器", installed.value("host_executable", ""), "host"});
        paths.push_back({"Roo Code 扩展", installed.value("extension", ""), "extension"});
    } else {
        for (auto &choice : installed.value("install_choices", J::array())) {
            if (choice["id"] == "cli")
                paths.push_back({"CLI 命令行", a.value("executable", ""), "cli"});
            if (choice["id"] == "desktop")
                paths.push_back({"桌面程序", a.value("desktop_executable", ""), "desktop"});
        }
    }
    auto health = installed.value("compatibility", J::object());
    auto desktopHealth = installed.value("desktop_compatibility", J::object());
    if (health.value("message", "").empty() && !desktopHealth.value("message", "").empty())
        health = desktopHealth;
    bool unhealthy = !health.value("message", "").empty();
    float programHeight = 64 + paths.size() * 60.f + (unhealthy ? 56 : 0);
    rect(box(x, start, w, programHeight), WHITE, 13, LINE);
    text("程序与路径", box(x + 20, start + 14, w - 150, 30), 14, INK, true);
    auto docs = a["docs"].get<std::string>();
    button("官方文档", box(x + w - 120, start + 12, 100, 36), [this, docs] { browse(docs); }, 0);
    float y = start + 58;
    for (auto &row : paths) {
        text(row.label, box(x + 20, y, w - 250, 20), 11, MUTED);
        auto missing = row.kind == "extension" ? "尚未安装扩展"
                       : row.kind == "host"    ? "未找到 VS Code 命令"
                                               : "尚未安装";
        text(row.value.empty() ? missing : row.value, box(x + 20, y + 22, w - 255, 24), 12,
             row.value.empty() ? MUTED : BODY);
        if (row.kind == "cli") {
            if (!row.value.empty())
                button(
                    "启动 CLI", box(x + w - 220, y + 5, 92, 38),
                    [this] { notice(adapters.launch(expandedAgent)); }, 2, "",
                    health.value("compatible", false));
        } else if (row.kind != "host" && !row.value.empty())
            button(
                row.kind == "extension" ? "打开目录" : "打开应用", box(x + w - 220, y + 5, 92, 38),
                [this, row] {
                    if (row.kind == "desktop") {
                        auto executable = path(row.value);
                        if (!allowed_agent_path(executable))
                            throw std::runtime_error("检测到测试文件，请重新检测并安装正式版本");
                        auto compatibility = binary_compatibility(executable, windows_platform());
                        if (!compatibility.value("compatible", false))
                            throw std::runtime_error(compatibility.value("message", "桌面启动文件不兼容"));
                    }
                    browse(row.value);
                },
                2);
        if (row.kind != "host")
            button(
                row.kind == "extension" ? "定位扩展" : "定位程序", box(x + w - 116, y + 5, 96, 38),
                [this] { locateAgent(expandedAgent); }, 0);
        y += 60;
    }
    if (unhealthy) {
        text(health.value("message", ""), box(x + 20, y, w - 40, 48), 11, RED, false, true);
        y += 56;
    }
    y = start + programHeight + 14;
    auto warnings = agentState.value("warnings", J::array());
    int last = std::min((int)configured.size(), agentConfiguredOffset + 6);
    bool editable = capability.value("configure", false) || capability.value("default", false);
    float modelHeight = 16 + 38 + 40 +
                        (configured.empty() ? (editable ? 40 : 0) : (last - agentConfiguredOffset) * 60.f) +
                        (configured.size() > 6 ? 40 : 0) + warnings.size() * 44.f + (editable ? 68 : 0) +
                        (agentPreview.is_object() && agentPreview.contains("content") ? 386 : 0) + 12;
    rect(box(x, y, w, modelHeight), WHITE, 13, LINE);
    y += 16;
    text("模型配置", box(x + 20, y, w - 150, 28), 14, INK, true);
    button("刷新配置", box(x + w - 120, y - 3, 100, 36), [this] { loadAgentState(expandedAgent); }, 0);
    y += 38;
    text(capability.value("note", ""), box(x + 20, y, w - 40, 34), 11, MUTED, false, true);
    y += 40;
    if (configured.empty() && editable) {
        text(agentState.value("loading", false) ? "正在读取配置…" : "尚无已配置的模型",
             box(x + 20, y, w - 40, 30), 12, MUTED);
        y += 40;
    }
    for (int i = agentConfiguredOffset; i < last; ++i) {
        auto row = configured[i];
        line(x + 20, y, x + w - 20, y, LINE, 1);
        text(row["id"], box(x + 20, y + 7, w - 360, 24), 13, INK, true);
        text(row["source"], box(x + 20, y + 31, w - 150, 22), 10, MUTED);
        text(row["role"].get<std::string>() + " · " + row["provider"].get<std::string>(),
             box(x + w - 330, y + 8, 190, 24), 10, BLUE, false, false, 2);
        if (capability.value("default", false))
            button(
                "设为默认", box(x + w - 122, y + 8, 102, 34),
                [this, row] {
                    agentPreview = adapters.preview_default(
                        expandedAgent, row.value("selection", row["id"].get<std::string>()));
                    invalidate();
                },
                0);
        y += 60;
    }
    if (configured.size() > 6) {
        button(
            "上一页", box(x + 20, y, 82, 32),
            [this] {
                agentConfiguredOffset -= 6;
                invalidate();
            },
            0, "", agentConfiguredOffset > 0);
        button(
            "下一页", box(x + 112, y, 82, 32),
            [this] {
                agentConfiguredOffset += 6;
                invalidate();
            },
            0, "", last < (int)configured.size());
        y += 40;
    }
    for (auto &warning : warnings) {
        text(warning, box(x + 20, y, w - 40, 38), 11, BODY, false, true);
        y += 44;
    }
    if (editable) {
        line(x + 20, y, x + w - 20, y, LINE, 1);
        y += 16;
    }
    if (capability.value("configure", false)) {
        std::vector<Choice> candidates;
        std::string selectedName = "选择连接与默认模型";
        J selected;
        for (auto &p : providers) {
            auto protocols = a["protocols"];
            if (!protocols.empty() &&
                std::find(protocols.begin(), protocols.end(), p["protocol"]) == protocols.end())
                continue;
            for (auto &model : p.value("models", J::array())) {
                auto value = J::array({p["id"], model}).dump();
                candidates.push_back({value, model,
                                      p["name"].get<std::string>() + " · " + p["protocol"].get<std::string>(),
                                      p["vendor"]});
                if (p["id"] == agentProvider && model == agentModel)
                    selectedName = p["name"].get<std::string>() + " · " + model.get<std::string>();
            }
        }
        if (candidates.empty()) {
            text("尚未添加兼容的模型连接", box(x + 20, y, w - 165, 40), 12, MUTED);
            button("添加连接", box(x + w - 124, y, 104, 40), [this] { navigate(3); }, 2);
        } else {
            button(
                selectedName, box(x + 20, y, w - 160, 40),
                [this, candidates] {
                    picker("模型连接", candidates, [this](std::string value) {
                        auto choice = J::parse(value);
                        agentProvider = choice[0];
                        agentModel = choice[1];
                        agentPreview = J();
                        invalidate();
                    });
                },
                5, "down");
            button(
                "预览配置", box(x + w - 128, y, 108, 40),
                [this] {
                    agentPreview = adapters.preview(expandedAgent, agentProvider, agentModel);
                    invalidate();
                },
                1, "", !agentProvider.empty());
        }
        y += 52;
    } else if (capability.value("default", false)) {
        edit(102, box(x + 20, y, w - 160, 40), agentDefaultInput, false, false, false,
             "输入原工具支持的模型 ID");
        button(
            "预览默认项", box(x + w - 128, y, 108, 40),
            [this] {
                agentPreview = adapters.preview_default(expandedAgent, value(102));
                invalidate();
            },
            1);
        y += 52;
    }
    if (agentPreview.is_object() && agentPreview.contains("content")) {
        text(agentPreview["note"], box(x + 20, y, w - 40, 62), 11, BODY, false, true);
        y += 72;
        text(agentPreview["path"], box(x + 20, y, w - 40, 24), 11, MUTED);
        y += 34;
        auto textContent = wide(agentPreview["content"]);
        if (textContent.size() > 2400) {
            textContent.resize(2400);
            textContent += L"\n…复制参数可查看完整内容";
        }
        rect(box(x + 20, y, w - 40, 220), WHITE, 10, LINE);
        text(utf8(textContent), box(x + 34, y + 10, w - 68, 200), 11, BODY, false, true);
        y += 232;
        button("复制参数", box(x + 20, y, 112, 34), [this] { clipboard(agentPreview["content"]); }, 0);
        if (agentPreview["can_apply"].get<bool>())
            button(
                "备份并应用", box(x + 144, y, 128, 34),
                [this] {
                    adapters.apply(agentPreview["ticket"]);
                    agentPreview = J();
                    loadAgentState(expandedAgent);
                    refresh();
                    notice("配置已备份并应用");
                },
                1);
        y += 48;
    }
    return y + 12 - start;
}
void App::modelsPage(float x, float start, float w) {
    edit(100, box(x, start, 270, 36), modelQuery, false, false, false, "搜索服务商或连接", true);
    const std::pair<const char *, const char *> tabs[] = {{"all", "全部"},       {"cn", "国内服务"},
                                                          {"intl", "国际服务"},  {"gateway", "聚合平台"},
                                                          {"local", "本地服务"}, {"custom", "自定义"}};
    for (int i = 0; i < 6; ++i) {
        auto tab = tabs[i];
        button(
            tab.second, box(x + i * 100, start + 46, 92, 32),
            [this, tab] {
                presetGroup = tab.first;
                scroll = 0;
                invalidate();
            },
            presetGroup == tab.first ? 2 : 3);
    }
    float top = start + 94, y = top - scroll;
    beginScroll(box(x, top, w, height - top - 24));
    text("我的连接", box(x, y, w, 28), 15, INK, true);
    y += 38;
    int num = 0;
    for (auto &p : providers) {
        if (!modelQuery.empty() &&
            fold(s(p, "name") + s(p, "vendor") + s(p, "model") + p.value("models", J::array()).dump())
                    .find(fold(modelQuery)) == std::string::npos)
            continue;
        rect(box(x, y, w, 84), WHITE, 13, LINE);
        brand(p["vendor"], box(x + 17, y + 21, 40, 40));
        text(p["name"], box(x + 74, y + 10, 225, 26), 14, INK, true);
        text(s(p, "model").empty() ? "模型列表未填写"
                                   : s(p, "model") + " · 共 " +
                                         std::to_string(p.value("models", J::array()).size()) + " 个模型",
             box(x + 74, y + 40, 225, 23), 11, MUTED);
        text(s(p, "protocol") + " · " + (p["has_key"].get<bool>() ? "已配置凭据" : "无密钥 / 环境变量"),
             box(x + 316, y + 12, w - 650, 25), 11, BODY);
        text(s(p, "status") == "connected" ? "最近连接成功 · " + std::to_string(p.value("latency", 0)) + " ms"
                                           : "尚未测试",
             box(x + 316, y + 41, w - 650, 23), 11, s(p, "status") == "connected" ? GREEN : MUTED);
        button(
            "测试", box(x + w - 304, y + 26, 62, 32),
            [this, p] {
                job([this, p] { return models.test(p["id"]); },
                    [this](J r) {
                        refresh();
                        notice("连接成功 · " + std::to_string(r["latency"].get<int>()) + " ms");
                    },
                    "测试模型连接");
            },
            2, "", !busy());
        button(
            "复制", box(x + w - 232, y + 26, 62, 32),
            [this, p] {
                auto copy = models.duplicate(p["id"]);
                refresh();
                providerDialog(copy["vendor"], copy["id"]);
                notice("已复制连接配置，请为副本设置凭据");
            },
            0);
        button(
            "编辑", box(x + w - 158, y + 26, 62, 32), [this, p] { providerDialog(p["vendor"], p["id"]); }, 0);
        button(
            "删除", box(x + w - 86, y + 26, 62, 32),
            [this, p] {
                if (confirm("删除模型连接", "删除此连接和本工具保存的密钥？已有 Agent 配置不会自动更改。")) {
                    models.remove(p["id"]);
                    refresh();
                }
            },
            4);
        y += 96;
        ++num;
    }
    if (!num) {
        rect(box(x, y, w, 70), WHITE, 13, LINE);
        text("添加一个连接，用于测试、生成 Skill 和配置 Agent。", box(x + 18, y + 10, w - 36, 50), 12, MUTED);
        y += 84;
    }
    y += 13;
    text("添加服务商", box(x, y, w, 28), 15, INK, true);
    text("可编辑区域端点、协议和模型 ID", box(x + w - 280, y, 280, 28), 11, MUTED, false, false, 2);
    y += 43;
    int cols = w < 850 ? 3 : 4;
    float cw = (w - 12 * (cols - 1)) / cols;
    int i = 0;
    for (auto &p : presets) {
        if (presetGroup != "all" && p["group"] != presetGroup)
            continue;
        if (!modelQuery.empty() &&
            fold(s(p, "name") + s(p, "id")).find(fold(modelQuery)) == std::string::npos)
            continue;
        float xx = x + (i % cols) * (cw + 12), yy = y + (i / cols) * 81;
        rect(box(xx, yy, cw, 69), WHITE, 12, LINE);
        brand(p["id"], box(xx + 13, yy + 17, 33, 33));
        text(p["name"], box(xx + 58, yy + 9, cw - 68, 27), 12, INK, true);
        text(p["protocol"], box(xx + 58, yy + 36, cw - 68, 21), 10, MUTED);
        hit(box(xx, yy, cw, 69), [this, p] { providerDialog(p["id"]); });
        ++i;
    }
    y += ((i + cols - 1) / cols) * 81;
    endScroll(y - (top - scroll));
}
void App::generationMenu() {
    popup({{"improve", "完善当前草稿", "补充步骤、输出、验证和示例", ""},
           {"quality", "质量检查", "不调用模型", ""},
           {"options", "生成设置", "语言、深度与补充要求", ""},
           {"restore", "恢复上一版", "保留当前内容以便切回", ""}},
          [this](std::string action) {
              if (action == "improve")
                  runGenerate(true);
              else if (action == "quality")
                  qualityDialog();
              else if (action == "options")
                  generationSettings();
              else {
                  auto previous = store.setting("generator_previous");
                  if (!previous.is_string() || previous.get<std::string>().empty())
                      throw std::runtime_error("还没有上一版草稿");
                  auto current = value(111);
                  draft = previous;
                  store.setting("generator_previous", current);
                  value(111, draft);
                  changed(111);
                  notice("已恢复上一版草稿");
              }
          });
}
void App::generatePage(float x, float start, float w) {
    float left = std::max(310.f, w * .39f), gap = 18, right = x + left + gap;
    rect(box(x, start, left, height - start - 24), WHITE, 14, LINE);
    rect(box(right, start, w - left - gap, height - start - 24), WHITE, 14, LINE);
    std::string name = "选择模型连接";
    for (auto &p : providers)
        if (p["id"] == genProvider)
            name = s(p, "name") + " · " + s(p, "model");
    button(
        name, box(x + 18, start + 18, left - 36, 36),
        [this] {
            std::vector<Choice> options;
            for (auto &p : providers)
                options.push_back({p["id"], p["name"], p["model"], p["vendor"]});
            if (options.empty()) {
                navigate(3);
                notice("请先添加模型连接");
                return;
            }
            popup(std::move(options), [this](std::string id) {
                genProvider = id;
                changed(110);
                invalidate();
            });
        },
        5, "down", !busy());
    edit(110, box(x + 18, start + 70, left - 36, height - start - 158), prompt, true, false, false,
         "描述这个 Skill 要完成什么、需要哪些输入、期望怎样的输出。");
    button(
        "生成 Skill", box(x + 18, height - 70, left - 36, 38), [this] { runGenerate(); }, 1, "generate",
        !busy() && !genProvider.empty());
    text("SKILL.md", box(right + 18, start + 14, w - left - gap - 130, 32), 14, INK, true);
    button("更多", box(x + w - 90, start + 16, 72, 29), [this] { generationMenu(); }, 3, "", !busy());
    Rect preview = box(right + 18, start + 58, w - left - gap - 36, height - start - 146);
    if (busy()) {
        if (edits.count(111))
            ShowWindow(edits[111], SW_HIDE);
        activity(preview);
    } else
        edit(111, preview, draft, true);
    button(
        "保存 Skill", box(x + w - 146, height - 70, 128, 38),
        [this] {
            draft = value(111);
            store.save({{"content", draft}, {"source", "LLM 生成"}});
            refresh();
            notice("Skill 已保存，可直接部署");
        },
        1, "", !busy());
}
void App::settings(float x, float start, float w) {
    beginScroll(box(x, start, w, height - start - 24));
    float y = start - scroll;
    rect(box(x, y, w, 178), WHITE, 14, LINE);
    icon("folder", x + 20, y + 20, 24);
    text("工作范围", box(x + 58, y + 14, w - 80, 32), 16, INK, true);
    text(projectPath.empty() ? "用户目录 · 尚未选择项目" : projectPath, box(x + 20, y + 57, w - 40, 30), 13,
         BODY);
    text(stats["last_scan"].is_number() ? "最近扫描 · " + timeText(stats["last_scan"])
                                        : "选择项目后，可发现项目 Skills，并使用项目级部署。",
         box(x + 20, y + 91, w - 40, 25), 11, MUTED);
    button(
        "选择项目", box(x + 20, y + 128, 112, 34),
        [this] {
            auto dir = choose(true);
            if (!dir.empty()) {
                store.setting("project", pathstr(dir));
                refresh();
                notice("项目目录已保存");
            }
        },
        1, "folder");
    button(
        "打开目录", box(x + 144, y + 128, 112, 34), [this] { browse(projectPath); }, 0, "",
        !projectPath.empty());
    button(
        "清除项目", box(x + 268, y + 128, 112, 34),
        [this] {
            store.setting("project", "");
            store.setting("deploy_scope", "user");
            refresh();
            notice("已切换到用户级工作范围");
        },
        3, "", !projectPath.empty());
    y += 196;
    float cw = (w - 18) / 2;
    rect(box(x, y, cw, 211), WHITE, 14, LINE);
    text("数据与存储", box(x + 20, y + 14, cw - 40, 30), 16, INK, true);
    text(pathstr(store.root), box(x + 20, y + 53, cw - 40, 46), 12, BODY, false, true);
    text("占用 " + bytes(stats["data_bytes"]) + " · 去重节省 " + bytes(stats["saved_bytes"]),
         box(x + 20, y + 105, cw - 40, 24), 12, MUTED);
    button("打开数据目录", box(x + 20, y + 157, 140, 34), [this] { browse(pathstr(store.root)); }, 0);
    button("复制路径", box(x + 172, y + 157, 100, 34), [this] { clipboard(pathstr(store.root)); }, 3);
    float rx = x + cw + 18;
    rect(box(rx, y, cw, 211), WHITE, 14, LINE);
    text("部署与草稿", box(rx + 20, y + 14, cw - 40, 30), 16, INK, true);
    auto aid = store.setting("deploy_agent");
    std::string name = "尚未部署";
    if (aid.is_string() && !aid.get<std::string>().empty()) {
        try {
            name = adapters.item(aid)["name"];
        } catch (...) {
        }
    }
    text("最近 Agent · " + name, box(rx + 20, y + 52, cw - 40, 26), 12, BODY);
    auto scope = store.setting("deploy_scope");
    text("默认范围 · " + std::string(scope == "project" ? "项目级" : "用户级"),
         box(rx + 20, y + 84, cw - 40, 26), 12, BODY);
    text("生成需求与草稿自动保存；可恢复生成前一版。", box(rx + 20, y + 119, cw - 40, 36), 11, MUTED, false,
         true);
    button(
        "重置部署偏好", box(rx + 20, y + 157, 148, 34),
        [this] {
            store.setting("deploy_agent", "codex");
            store.setting("deploy_scope", "user");
            invalidate();
            notice("默认部署已重置为 Codex · 用户级");
        },
        0);
    y += 233;
    rect(box(x, y, w, 100), WHITE, 14, LINE);
    text("凭据与配置保护", box(x + 20, y + 12, w - 40, 28), 15, INK, true);
    text("API Key 由当前 Windows 用户加密保存。应用 Agent 配置前自动备份；原文件有外部改动时会停止覆盖。",
         box(x + 20, y + 48, w - 40, 38), 12, BODY, false, true);
    y += 121;
    text("配置备份", box(x, y, w, 30), 16, INK, true);
    text(std::to_string(backupRows.size()) + " 份 · 最近 30 份", box(x + w - 200, y, 200, 30), 11, MUTED,
         false, false, 2);
    y += 43;
    if (backupRows.empty()) {
        rect(box(x, y, w, 77), WHITE, 12, LINE);
        text("应用配置后，备份会显示在这里。", box(x + 20, y + 17, w - 40, 38), 12, MUTED);
        y += 92;
    }
    for (auto &b : backupRows) {
        rect(box(x, y, w, 76), WHITE, 12, LINE);
        text(b["id"], box(x + 18, y + 10, w - 150, 25), 12, INK, true);
        text(b["path"], box(x + 18, y + 40, w - 150, 22), 11, MUTED);
        button(
            "恢复", box(x + w - 102, y + 22, 80, 32),
            [this, b] {
                if (confirm("恢复配置", "恢复备份中的配置？若应用后存在外部修改，将停止恢复。")) {
                    adapters.restore(b["id"]);
                    refresh();
                    notice("已恢复原配置");
                }
            },
            0);
        y += 88;
    }
    rect(box(x, y, w, 82), SOFT, 12);
    text("快捷键", box(x + 18, y + 10, w - 36, 25), 13, INK, true);
    text("Ctrl K  全局搜索     Ctrl F  当前页搜索     Ctrl 1–6  切换页面     F5  刷新",
         box(x + 18, y + 39, w - 36, 26), 11, BODY);
    y += 100;
    endScroll(y - (start - scroll));
}
void App::changed(int id) {
    if (id == 102) {
        agentDefaultInput = value(102);
        return;
    }
    if (id == 110 || id == 111) {
        SetTimer(hwnd, 7, 800, nullptr);
        return;
    }
    if (id == -3) {
        prompt = value(110);
        draft = value(111);
        store.setting("generator", {{"prompt", prompt}, {"draft", draft}, {"provider", genProvider}});
        return;
    }
    if (id == 90) {
        SetTimer(hwnd, 6, 120, nullptr);
        return;
    }
    if (id == -2) {
        auto next = trim(value(90));
        if (globalQuery.empty() && !next.empty() && page == 4) {
            prompt = value(110);
            draft = value(111);
        }
        globalQuery = next;
        scroll = 0;
        if (!globalQuery.empty()) {
            for (auto &[key, control] : edits)
                if (key != 90)
                    ShowWindow(control, SW_HIDE);
            globalSkills = store.list(globalQuery, "all", "all", 50)["items"];
        }
        invalidate();
        return;
    }

    if (id == 100) {
        SetTimer(hwnd, 4, 160, nullptr);
        return;
    }
    if (id == -1) {
        auto v = value(100);
        if (page == 1) {
            query = v;
            offset = 0;
            selection.clear();
            skillRows = store.list(query,
                                   scope == 1   ? "managed"
                                   : scope == 2 ? "external"
                                   : scope == 3 ? "favorites"
                                                : "all",
                                   category, 50, offset);
        } else if (page == 2)
            agentQuery = v;
        else if (page == 3)
            modelQuery = v;
        scroll = 0;
        invalidate();
    }
}
void App::key(WPARAM w) {
    if (w == VK_ESCAPE && (!globalQuery.empty() || !trim(value(90)).empty())) {
        value(90, "");
        changed(-2);
        return;
    }
    if (w == VK_RETURN && GetFocus() == edits[90] && trim(value(90)) != globalQuery) {
        changed(-2);
        UpdateWindow(hwnd);
    }
    if (w == VK_RETURN && !globalQuery.empty() && firstSearchResult) {
        auto action = firstSearchResult;
        action();
        return;
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) && w >= '1' && w <= '6') {
        navigate(int(w - '1'));
        return;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'K')
        commandPalette();
    else if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'F') {
        if (edits.count(100) && IsWindowVisible(edits[100]))
            SetFocus(edits[100]);
    } else if (w == VK_F5)
        guard([this] {
            refresh();
            notice("正在后台刷新");
        });
}
bool App::beforeClose() {
    if (page == 4) {
        prompt = value(110);
        draft = value(111);
    }
    store.setting("generator", {{"prompt", prompt}, {"draft", draft}, {"provider", genProvider}});
    for (auto &d : dialogs)
        if (d->hwnd) {
            d->close();
            if (d->hwnd && !d->closing)
                return false;
        }
    return true;
}
void App::collect() {
    for (auto &dialog : dialogs) {
        auto &c = dialog->children;
        c.erase(std::remove_if(c.begin(), c.end(), [](auto &p) { return !p->hwnd; }), c.end());
    }
    dialogs.erase(std::remove_if(dialogs.begin(), dialogs.end(), [](auto &p) { return !p->hwnd; }),
                  dialogs.end());
}
void App::scan() {
    cancelJob = [this] {
        models.cancel();
        store.cancel_scan = true;
    };
    job([this] { return store.scan(adapters.roots()); },
        [this](J r) {
            refresh();
            std::ostringstream message;
            message << "扫描完成 · 更新 " << r["changed"] << " 个 Skill · " << std::fixed
                    << std::setprecision(1) << r["ms"].get<double>() << " ms";
            notice(message.str());
            if (!r["warnings"].empty()) {
                std::string details;
                for (auto &warning : r["warnings"])
                    details += warning.get<std::string>() + "\n";
                auto &f =
                    form("扫描提示", 720, 530, {{"detail", "跳过的文件", details, "", true, false, true}});
                f.actions = {{"关闭", [](Form &f) { f.close(); }, 0}};
            }
        },
        "扫描本地 Skills");
}
Form &App::form(const std::string &title, int w, int h, std::vector<Field> fields, HWND parent) {
    auto f = std::make_unique<Form>();
    f->fields = std::move(fields);
    f->cancelJob = [this] {
        models.cancel();
        store.cancel_scan = true;
    };
    auto &ref = *f;
    dialogs.push_back(std::move(f));
    ref.open(title, w, h, parent ? parent : (GetActiveWindow() ? GetActiveWindow() : hwnd), true);
    return ref;
}
void App::picker(const std::string &title, std::vector<Choice> items,
                 std::function<void(std::string)> selected, HWND parent) {
    Window *target = this;
    if (parent && parent != hwnd) {
        for (auto &dialog : dialogs)
            if (dialog->hwnd == parent) {
                target = dialog.get();
                break;
            }
    }
    target->popup(std::move(items), std::move(selected));
}
void App::commandPalette() {
    if (edits.count(90)) {
        SetFocus(edits[90]);
        SendMessageW(edits[90], EM_SETSEL, 0, -1);
    }
}
void App::searchResults(float x, float start, float w) {
    text("搜索结果", box(x, start, w, 36), 25, INK, true);
    float top = start + 58, y = top - scroll;
    firstSearchResult = {};
    beginScroll(box(x, top, w, height - top - 24));
    int count = 0;
    auto heading = [&](const std::string &label) {
        text(label, box(x, y, w, 28), 14, INK, true);
        y += 39;
    };
    auto row = [&](const std::string &name, const std::string &note, const std::string &brandName,
                   std::function<void()> action, std::function<void()> deploy = {},
                   const std::string &categoryName = "") {
        if (!firstSearchResult)
            firstSearchResult = action;
        rect(box(x, y, w, 78), WHITE, 12, LINE);
        if (brandName.empty()) {
            auto visual = skill_visual(categoryName);
            rect(box(x + 13, y + 18, 38, 42), visual.fill, 11);
            icon(visual.icon, x + 20, y + 27, 24, visual.ink);
        } else
            brand(brandName, box(x + 15, y + 20, 37, 37));
        text(name, box(x + 68, y + 10, w - 280, 27), 14, INK, true);
        text(note, box(x + 68, y + 39, w - 280, 24), 11, MUTED);
        hit(box(x, y, w - 200, 78), action);
        button("打开", box(x + w - 92, y + 23, 72, 32), action, 0);
        if (deploy)
            button("部署", box(x + w - 177, y + 23, 72, 32), deploy, 2);
        y += 89;
        ++count;
    };
    if (!globalSkills.empty()) {
        heading("Skills · " + std::to_string(globalSkills.size()));
        for (auto &sk : globalSkills) {
            auto id = sk["id"].get<std::string>();
            row(
                sk["title"], sk["description"], "", [this, id] { skillDialog(id); },
                [this, id] { deployDialog(id); }, s(sk, "category"));
        }
    }
    auto q = fold(globalQuery);
    bool group = false;
    for (auto &a : agents) {
        if (fold(s(a, "name") + " " + s(a, "id") + " " + s(a, "vendor") + " " + s(a, "note")).find(q) ==
            std::string::npos)
            continue;
        if (!group) {
            heading("Agent");
            group = true;
        }
        auto id = s(a, "id");
        row(a["name"], a["note"], id, [this, id] { agentDialog(id); });
    }
    group = false;
    for (auto &p : providers) {
        if (fold(s(p, "name") + " " + s(p, "model") + " " + s(p, "vendor") +
                 p.value("models", J::array()).dump())
                .find(q) == std::string::npos)
            continue;
        if (!group) {
            heading("已保存的模型连接");
            group = true;
        }
        auto id = s(p, "id"), vendor = s(p, "vendor");
        row(p["name"], p["model"], vendor, [this, id, vendor] { providerDialog(vendor, id); });
    }
    group = false;
    for (auto &p : presets) {
        if (fold(s(p, "name") + " " + s(p, "id")).find(q) == std::string::npos)
            continue;
        if (!group) {
            heading("添加模型服务");
            group = true;
        }
        auto id = s(p, "id");
        row(p["name"], s(p, "base_url"), id, [this, id] { providerDialog(id); });
    }
    if (!count) {
        rect(box(x, y, w, 150), WHITE, 14, LINE);
        text("没有匹配结果", box(x, y + 30, w, 36), 17, INK, true, false, 1);
        text("尝试名称的一部分、英文名称或中文关键词。", box(x, y + 73, w, 30), 12, MUTED, false, false, 1);
        y += 165;
    }
    endScroll(y - (top - scroll));
}
void App::runGenerate(bool improve) {
    prompt = value(110);
    draft = value(111);
    if (genProvider.empty())
        throw std::runtime_error("请先选择模型连接");
    auto provider = genProvider, description = prompt;
    auto options = generationOptions;
    if (improve) {
        if (trim(draft).empty())
            throw std::runtime_error("请先生成或粘贴需要完善的草稿");
        options["existing"] = draft;
        if (trim(description).size() < 5)
            description = "保留原始用途，完善描述、工作流程、输出要求、验证和示例。";
    }
    store.setting("generator_previous", draft);
    store.setting("generator", {{"prompt", prompt}, {"draft", draft}, {"provider", genProvider}});
    job([this, provider, description, options] { return models.generate(provider, description, options); },
        [this](J r) {
            draft = r["content"];
            if (edits.count(111))
                value(111, draft);
            store.setting("generator", {{"prompt", prompt}, {"draft", draft}, {"provider", genProvider}});
            if (!r["valid"].get<bool>())
                notice("已保留草稿，格式需修正：" + s(r, "error"));
            else if (r["quality"]["suggestions"].empty())
                notice("草稿已生成，请检查内容后保存；格式与基础结构检查通过");
            else
                notice("草稿已生成 · " + std::to_string(r["quality"]["suggestions"].size()) +
                       " 项改进建议，点击“质量检查”查看");
        },
        improve ? "完善 Skill 草稿" : "生成 Skill 草稿");
}
} // namespace ad

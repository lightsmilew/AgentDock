#include "skill_category.hpp"
#include "ui.hpp"
#include <algorithm>
#include <sstream>

namespace ad {
void App::generationSettings() {
    Field language{"language", "输出语言", generationOptions.value("language", "zh")};
    language.options = {{"zh", "中文"}, {"en", "English"}};
    Field detail{"detail", "内容深度", generationOptions.value("detail", "detailed")};
    detail.options = {{"detailed", "详细 · 含决策条件、示例和验证"},
                      {"compact", "精简 · 保留关键流程与验收"}};
    Field tokens{"tokens", "输出 Token 上限", generationOptions.value("tokens", "8192")};
    tokens.options = {{"2048", "2048"}, {"4096", "4096"}, {"8192", "8192"}, {"12000", "12000"}};
    auto &f = form(
        "Skill 生成设置", 780, 650,
        {language,
         detail,
         tokens,
         {"context", "补充背景与约束", generationOptions.value("context", ""),
          "可填写目标 Agent、可用工具、运行环境、输入字段、输出模板及不可更改的要求。不要填写密钥。", true}});
    f.help = "每次生成或完善仅发起一次模型请求；上限须符合所选模型能力，实际计费由服务商决定。";
    f.actions = {{"取消", [](Form &f) { f.close(); }, 0},
                 {"保存设置",
                  [this](Form &f) {
                      generationOptions = {{"language", f.get("language")},
                                           {"detail", f.get("detail")},
                                           {"tokens", f.get("tokens")},
                                           {"context", f.get("context")}};
                      store.setting("generator_options", generationOptions);
                      f.close();
                      invalidate();
                      notice("生成设置已保存");
                  },
                  1}};
}
void App::qualityDialog() {
    auto report = skill_quality(value(111));
    std::string findings = report["valid"].get<bool>() ? "格式检查通过" : "格式需要修正";
    if (report["valid"].get<bool>())
        findings += "\n描述：" + std::to_string(report["description_chars"].get<int>()) + " 字符；正文：" +
                    std::to_string(report["body_chars"].get<int>()) + " 字符。";
    findings += "\n\n";
    for (auto &item : report["suggestions"])
        findings += "• " + item.get<std::string>() + "\n\n";
    if (report["suggestions"].empty())
        findings += "未发现基础结构问题。仍需结合实际任务检查事实、工具和步骤是否正确。";
    auto &f =
        form("Skill 质量检查", 780, 600, {{"report", "检查结果与改进建议", findings, "", true, false, true}});
    f.help = "本地启发式检查，无模型调用。建议用于发现遗漏，短描述或短正文不等于无效 Skill。";
    f.actions = {{"关闭", [](Form &f) { f.close(); }, 0},
                 {"完善当前草稿",
                  [this](Form &f) {
                      f.close();
                      runGenerate(true);
                  },
                  1}};
}
void App::deploymentDialog() {
    auto rows = store.deployments();
    if (rows.empty()) {
        notice("暂无部署记录；选择任意 Skill 后可直接部署。");
        return;
    }
    std::vector<Choice> items;
    for (size_t i = 0; i < rows.size(); ++i) {
        auto &row = rows[i];
        items.push_back(
            {std::to_string(i), row["title"].get<std::string>() + " · " + row["agent"].get<std::string>(),
             std::string(row["active"] != 0 ? "已启用 · " : "已暂停 · ") + row["path"].get<std::string>(),
             ""});
    }
    picker("部署记录", items, [this, rows](std::string index) {
        auto row = rows.at(std::stoul(index));
        bool active = row["active"] != 0;
        auto id = row["skill_id"].get<std::string>(), agent = row["agent"].get<std::string>();
        auto &f = form("部署副本", 800, 400,
                       {{"skill", "Skill", row["title"], "", false, false, true},
                        {"agent", "Agent / 范围", agent, "", false, false, true},
                        {"target", "部署位置", row["path"], "", false, false, true}});
        f.help = "只管理这份部署副本，原始 Skill 文件保持不变；外部修改会阻止覆盖。";
        f.actions = {{"关闭", [](Form &f) { f.close(); }, 0},
                     {"复制路径", [row](Form &f) { f.clipboard(row["path"]); }, 0}};
        if (active)
            f.actions.push_back({"打开目录", [row](Form &f) { f.browse(row["path"]); }, 0});
        f.actions.push_back({active ? "暂停副本" : "恢复副本",
                             [this, id, agent, active](Form &f) {
                                 store.toggle_deployment(id, agent, !active);
                                 f.close();
                                 refresh();
                                 notice(active ? "部署副本已暂停" : "部署副本已恢复");
                             },
                             1});
    });
}
void App::importDialog() {
    auto &f = form("导入 Skill", 620, 390, {});
    f.importChoices = true;
    f.subtitle = "选择完整 Skill 文件夹或 SKILL.md，脚本和参考资料会一并保留。";
    f.help = "导入只复制文件；不会运行 Skill 中的任何指令或脚本。";
    f.onChange = [this](Form &f, const std::string &selected) {
        auto p = path(selected);
        if (!p.empty()) {
            auto sk = store.import_skill(p);
            f.close();
            refresh();
            skillDialog(sk["id"]);
        }
    };
    f.actions = {{"关闭", [](Form &f) { f.close(); }, 0}};
}
void App::skillDialog(const std::string &id, const std::string &initial) {
    J sk = id.empty() ? J::object() : store.skill(id);
    bool external = !id.empty() && sk["managed"] == 0;
    std::string content =
        initial.empty()
            ? sk.value(
                  "content",
                  std::string("---\nname: my-skill\ndescription: 描述此 Skill 的用途和触发场景\n---\n\n# "
                              "Skill 名称\n\n## 输入与前置条件\n\n确认用户目标及必要输入。\n\n## "
                              "工作流程\n\n1. 阅读用户提供的资料。\n2. 使用可复现的方法处理任务。\n3. "
                              "检查结果并报告依据和限制。\n\n## 输出规范\n\n给出清晰、可检查的结果。\n"))
            : initial;
    auto title = sk.value("title", "");
    auto categoryName = sk.value("category", id.empty() ? "" : "通用工具");
    std::vector<Field> fields = {
        {"title", "显示标题", title, "可留空，使用 name", false, false, external},
        {"category", "分类", categoryName, "例如：开发效率", false, false, external},
        {"content", external ? "外部 Skill · 只读" : "SKILL.md", content, "", true, false, external}};
    if (!external) {
        fields[1].options.push_back({"", "自动分类"});
        for (auto &category : skill_categories())
            fields[1].options.push_back({category.name, category.name});
    }
    auto &f = form(external ? "查看外部 Skill" : id.empty() ? "新建 Skill" : "编辑 Skill", 860, 730, fields);
    f.help = external ? "源文件保持只读，可直接部署；需要编辑时再导入工具库。"
                      : "保存后请重新部署以更新目标目录。Ctrl+S 保存；附件与旧版本保留。";
    f.canClose = [content, title, categoryName, external](Form &f) {
        if (external || !f.dirty)
            return true;
        if (f.get("content") == content && f.get("title") == title && f.get("category") == categoryName)
            return true;
        return f.confirm("未保存的修改", "关闭并放弃未保存的编辑？");
    };
    f.actions.push_back({"关闭", [](Form &f) { f.close(); }, 0});
    if (!id.empty()) {
        f.actions.insert(f.actions.begin(), {"导出", [this, id](Form &f) { exportSkill(id, &f); }, 0});
        if (!external) {
            f.actions.insert(f.actions.begin(),
                             {"历史版本",
                              [this, id](Form &f) {
                                  auto sk = store.skill(id);
                                  std::vector<Choice> rows;
                                  for (auto &r : sk["revisions"])
                                      rows.push_back(
                                          {r["hash"],
                                           std::string("版本 ") + r["hash"].get<std::string>().substr(0, 12),
                                           "加载到编辑器，保存后生效", ""});
                                  picker(
                                      "Skill 历史版本", rows,
                                      [this, id, &f](std::string h) {
                                          f.set("content", store.revision(id, h));
                                          f.dirty = true;
                                      },
                                      f.hwnd);
                              },
                              0});
            f.actions.insert(
                f.actions.begin(),
                {sk["enabled"] != 0 ? "停用" : "启用",
                 [this, id](Form &f) {
                     auto sk = store.skill(id);
                     if (f.confirm(sk["enabled"] != 0 ? "停用 Skill" : "启用 Skill",
                                   sk["enabled"] != 0
                                       ? "将暂停此 Skill 已由管理器部署的副本，外部修改过的目录会停止处理。"
                                       : "将恢复已暂停的部署；同名目标目录会阻止恢复。")) {
                         store.toggle(id, "enabled", sk["enabled"] == 0);
                         for (auto &action : f.actions)
                             if (action.label == "停用" || action.label == "启用")
                                 action.label = store.skill(id)["enabled"] != 0 ? "停用" : "启用";
                         refresh();
                         f.notice("已更新 Skill 状态");
                     }
                 },
                 0});
        }
    }
    if (!id.empty())
        f.actions.insert(f.actions.begin(), {"部署", [this, id](Form &) { deployDialog(id); }, 0});
    if (external) {
        auto origin = sk["origin"].get<std::string>();
        f.actions.insert(f.actions.begin(),
                         {"源目录", [origin](Form &f) { f.browse(pathstr(path(origin).parent_path())); }, 0});
        f.actions.push_back({"导入工具库",
                             [this, origin](Form &f) {
                                 auto sk = store.import_skill(path(origin));
                                 f.close();
                                 refresh();
                                 skillDialog(sk["id"]);
                             },
                             1});
    } else
        f.actions.push_back({"保存",
                             [this, id](Form &f) {
                                 J data = {{"content", f.get("content")},
                                           {"title", f.get("title")},
                                           {"category", f.get("category")}};
                                 if (!id.empty())
                                     data["id"] = id;
                                 store.save(data);
                                 f.dirty = false;
                                 f.close();
                                 refresh();
                                 notice("Skill 已保存");
                             },
                             1});
}
void App::deployDialog(const std::string &id) {
    auto sk = store.skill(id);
    if (sk["enabled"] == 0)
        throw std::runtime_error("请先启用 Skill");
    std::vector<std::pair<std::string, std::string>> options;
    for (auto &a : agents)
        options.emplace_back(a["id"], a["name"]);
    auto lastAgent = store.setting("deploy_agent");
    Field agent{"agent", "Agent", lastAgent.is_string() ? lastAgent.get<std::string>() : "codex"};
    agent.options = options;
    auto lastScope = store.setting("deploy_scope");
    auto project = store.setting("project");
    std::string preferred = lastScope.is_string() ? lastScope.get<std::string>()
                            : project.is_string() && !project.get<std::string>().empty() ? "project"
                                                                                         : "user";
    if (preferred == "project" && (!project.is_string() || project.get<std::string>().empty()))
        preferred = "user";
    Field scopeField{"scope", "部署范围", preferred};
    scopeField.options = {{"user", "用户级"}, {"project", "项目级"}};
    auto &f = form("部署 " + sk["title"].get<std::string>(), 800, 510,
                   {agent, scopeField, {"target", "目标目录", "", "", false, false, true}});
    auto update = [this, &f, sk, id] {
        auto aid = f.get("agent");
        auto a = adapters.item(aid);
        bool manual =
            a["skills"].get<std::string>().empty() && a["project_skills"].get<std::string>().empty();
        bool canDeploy = true;
        if (manual) {
            f.set("target", "通过官方技能页面上传 ZIP");
            f.help = "官方导入：导出完整 ZIP，再到原工具的技能页面上传。";
        } else
            try {
                auto project = f.get("scope") == "project";
                auto target = adapters.target(aid, project);
                f.set("target", pathstr(target / path(sk["name"])));
                auto key = aid + (project ? "_project" : "_user");
                for (auto &d : sk["deployments"])
                    if (d["agent"] == aid && path(d["path"]) == target / path(sk["name"]))
                        key = aid;
                auto check = store.deployment_check(id, key, target);
                f.help = check["already"].get<bool>()
                             ? "此 Skill 已位于目标目录，无需重复复制。"
                             : "检查通过 · 共 " + std::to_string(check["files"].get<int>()) +
                                   " 个文件，保留附件与源目录。";
            } catch (const std::exception &e) {
                canDeploy = false;
                f.set("target", "");
                f.help = std::string("暂不可部署 · ") + e.what();
            }
        if (!f.actions.empty()) {
            f.actions.back().label = manual ? "导出 ZIP" : "部署";
            f.actions.back().enabled = canDeploy;
        }
        f.invalidate();
    };
    f.onChange = [update](Form &, const std::string &key) {
        if (key == "agent" || key == "scope")
            update();
    };
    f.actions = {{"取消", [](Form &f) { f.close(); }, 0},
                 {"复制路径", [](Form &f) { f.clipboard(f.get("target")); }, 0},
                 {"官方文档", [this](Form &f) { f.browse(adapters.item(f.get("agent"))["docs"]); }, 0},
                 {"部署",
                  [this, id](Form &f) {
                      auto aid = f.get("agent");
                      auto a = adapters.item(aid);
                      if (a["skills"].get<std::string>().empty() &&
                          a["project_skills"].get<std::string>().empty()) {
                          exportSkill(id, &f);
                          return;
                      }
                      auto project = f.get("scope") == "project";
                      auto dest = adapters.target(aid, project);
                      auto key = aid + (project ? "_project" : "_user");
                      auto current = store.skill(id);
                      for (auto &d : current["deployments"]) {
                          if (d["agent"] == aid && path(d["path"]) == dest / path(current["name"]))
                              key = aid;
                      }
                      bool already =
                          current["managed"] == 0 && fs::exists(dest / path(current["name"])) &&
                          fs::equivalent(path(current["origin"]).parent_path(), dest / path(current["name"]));
                      store.deploy(id, key, dest);
                      store.setting("deploy_agent", aid);
                      store.setting("deploy_scope", project ? "project" : "user");
                      f.close();
                      refresh();
                      notice(already ? "此 Skill 已在目标 Agent 中，无需重复复制" : "Skill 已部署");
                  },
                  1}};
    update();
}
void App::exportSkill(const std::string &id, Window *parent) {
    auto &win = parent ? *parent : *this;
    auto sk = store.skill(id);
    auto p = win.choose(false, true, sk["name"].get<std::string>() + ".zip");
    if (!p.empty()) {
        store.export_zip(id, p);
        win.notice("已导出完整 Skill 包");
    }
}
void App::agentDialog(const std::string &id) {
    if (page != 2)
        navigate(2);
    if (expandedAgent == id) {
        expandedAgent.clear();
        agentPreview = J();
        invalidate();
        return;
    }
    expandedAgent = id;
    loadAgentState(id);
    agentPreview = J();
    agentProvider.clear();
    agentModel.clear();
    agentDefaultInput.clear();
    value(102, "");
    agentCandidateOffset = 0;
    agentConfiguredOffset = 0;
    agentQuery.clear();
    value(100, "");
    int index = 0;
    for (auto &a : agents) {
        if (a["id"] == id)
            break;
        if (!detectedOnly || a["detected"].get<bool>())
            ++index;
    }
    scroll = index * 108.f;
    invalidate();
}
void App::installationAction(const std::string &id, bool remove) {
    if (!remove && installingAgent == id && busy()) {
        adapters.cancel_install = true;
        cancellingInstall = true;
        invalidate();
        return;
    }
    if (busy()) {
        notice("已有安装任务正在执行");
        return;
    }
    auto agent = adapters.item(id);
    // Resolve the official operation first, so unavailable methods never display a misleading confirmation.
    bool chooseClaudeRemoval = remove && id == "claude" && !adapters.desktop_executable(id).empty();
    auto initialPlan = remove && !chooseClaudeRemoval ? adapters.installation_plan(id, true) : J::object();
    auto execute = [this, id, remove, agent](bool thorough, const std::string &variant = "",
                                             const fs::path &directory = fs::path()) {
        if (store.setting("ui_fixture") == true) {
            notice("测试窗口仅预览安装选项，不执行本机安装或卸载");
            return;
        }
        adapters.installation_plan(id, remove, {}, thorough, variant, directory);
        adapters.cancel_install = false;
        installingAgent = remove ? "" : id;
        cancellingInstall = false;
        cancelJob = [this] {
            models.cancel();
            store.cancel_scan = true;
            adapters.cancel_install = true;
        };
        job(
            [this, id, remove, thorough, variant, directory] {
                try {
                    return adapters.manage_installation(id, remove, {}, thorough, variant, directory);
                } catch (const std::exception &e) {
                    return J{{"error", e.what()}};
                }
            },
            [this](J result) {
                installingAgent.clear();
                cancellingInstall = false;
                refresh();
                if (result.contains("error"))
                    error(result["error"]);
                else
                    notice(result.value("message", "操作已完成"));
            },
            (remove ? "卸载 " : "安装 ") + agent["name"].get<std::string>());
    };
    if (chooseClaudeRemoval) {
        auto info = adapters.installation(id);
        Field version{"variant", "卸载版本", "desktop"};
        version.fullWidth = true;
        for (auto &choice : info["install_choices"])
            if (choice.value("installed", false))
                version.options.push_back({choice["id"], choice["name"]});
        auto &f = form("卸载 Claude Code", 680, 320, {version});
        f.brandName = id;
        f.subtitle = "仅卸载所选版本；桌面版的数据保留行为以官方卸载器为准。";
        f.actions = {{"返回", [](Form &form) { form.close(); }, 0},
                     {"确认卸载",
                      [execute](Form &form) {
                          auto variant = form.get("variant");
                          form.dirty = false;
                          form.close();
                          execute(false, variant);
                      },
                      1}};
        f.canClose = [](Form &) { return true; };
        return;
    }
    if (!remove) {
        auto info = adapters.installation(id);
        auto choices = info["install_choices"];
        if (choices.size() == 1 && choices[0]["id"] != "desktop") {
            execute(false, choices[0]["id"]);
            return;
        }
        std::string selected = choices[0]["id"];
        for (auto &choice : choices)
            if (!choice.value("installed", false) || choice.value("repairable", false)) {
                selected = choice["id"];
                break;
            }
        if (info.value("repairable", false))
            selected = "cli";
        Field version{"variant", "安装版本", selected};
        version.fullWidth = true;
        if (choices.size() == 1)
            version.section = -1;
        for (auto &choice : choices)
            version.options.push_back(
                {choice["id"],
                 choice["name"].get<std::string>() + (choice.value("installed", false) ? " · 已安装" : "")});
        auto saved = store.setting("desktop_directory:" + id);
        Field directory{"directory", "桌面版安装路径",
                        saved.is_string() ? saved.get<std::string>() : choices[0].value("directory", "")};
        if (directory.initial.empty())
            directory.initial = choices[0].value("directory", "");
        directory.directory = true;
        directory.fullWidth = true;
        directory.height = 40;
        auto &f = form("安装 " + agent["name"].get<std::string>(), 680, choices.size() > 1 ? 432 : 364,
                       {version, directory});
        f.columns = 2;
        f.brandName = id;
        f.subtitle =
            choices.size() > 1 ? "选择桌面版或 CLI，按各自的官方方式安装。" : "设置桌面程序的安装位置。";
        auto update = [choices](Form &form) {
            auto chosen = form.get("variant");
            for (auto &choice : choices)
                if (choice["id"] == chosen) {
                    bool desktop = chosen == "desktop", custom = choice.value("custom_directory", false);
                    form.fields[1].section = desktop ? 0 : -1;
                    form.fields[1].readonly = !custom;
                    if (desktop && !custom)
                        form.set("directory", "Windows 管理的应用目录");
                    form.help =
                        desktop ? (custom ? "此路径用于桌面程序安装。"
                                          : "官方 Store / MSIX 安装由 Windows 管理位置，不能指定普通文件夹。")
                                : "CLI 使用官方命令与默认目录安装。";
                    for (auto &action : form.actions)
                        if (action.label == "开始安装")
                            action.enabled =
                                !choice.value("installed", false) || choice.value("repairable", false);
                }
            form.invalidate();
        };
        f.actions = {{"返回", [](Form &form) { form.close(); }, 0},
                     {"开始安装",
                      [this, id, choices, execute](Form &form) {
                          auto variant = form.get("variant");
                          bool custom = false;
                          for (auto &choice : choices)
                              if (choice["id"] == variant)
                                  custom = choice.value("custom_directory", false);
                          auto directory =
                              variant == "desktop" && custom ? path(trim(form.get("directory"))) : fs::path();
                          adapters.installation_plan(id, false, {}, false, variant, directory);
                          form.dirty = false;
                          form.close();
                          execute(false, variant, directory);
                      },
                      1}};
        f.onChange = [update](Form &form, const std::string &) { update(form); };
        f.canClose = [](Form &) { return true; };
        update(f);
        return;
    }
    if (remove && initialPlan.value("method", "") == "verified-removal") {
        Field full{"thorough", "彻底清理用户数据", "false",
                   "清除配置、凭据、历史、缓存和 Skill；保留项目与工作树。"};
        full.checkbox = true;
        Field preview{"paths", "本次卸载范围", "", "", true, false, true};
        preview.height = 220;
        auto &f = form("卸载 " + agent["name"].get<std::string>(), 760, 610, {full, preview});
        f.brandName = id;
        f.subtitle = "移除已确认的程序与旧版本；用户数据默认保留。请先关闭对应编辑器或 Agent。";
        auto update = [this, id](Form &form) {
            auto plan = adapters.installation_plan(id, true, {}, form.get("thorough") == "true");
            std::string paths;
            for (auto &step : plan["steps"]) {
                auto method = step.value("method", "");
                if (method == "cursor-logout")
                    paths += "退出 Cursor CLI 的本地登录\n";
                else if (method == "legacy-owned")
                    paths += "移除管理器安装的旧版副本\n";
                else
                    paths += "使用对应的官方卸载入口，并复查程序残留\n";
            }
            for (auto &entry : plan["cleanup"]["files"])
                paths += (entry.value("data", false) ? "用户数据  " : "程序文件  ") +
                         entry["path"].get<std::string>() + "\n";
            for (auto &file : plan["cleanup"]["archives"])
                paths += "核对扩展身份后清理安装缓存  " + file.get<std::string>() + "\n";
            if (form.get("thorough") == "true") {
                for (auto &file : plan["cleanup"]["databases"])
                    paths += "仅清理 Roo 的状态和凭据  " + file.get<std::string>() + "\n";
                for (auto &file : plan["cleanup"]["settings"])
                    paths += "仅移除 Roo 配置项  " + file.get<std::string>() + "\n";
            }
            form.set("paths", paths);
            bool program = !plan["steps"].empty() || !plan["cleanup"]["archives"].empty();
            for (auto &entry : plan["cleanup"]["files"])
                program |= !entry.value("data", false);
            for (auto &action : form.actions)
                if (action.label == "确认卸载")
                    action.enabled = program || form.get("thorough") == "true";
            form.help = form.get("thorough") == "true" ? "已勾选：永久删除列出的用户数据（含插件）。项目、工"
                                                         "作树、模型服务连接和管理器 Skill 库保留。"
                                                       : "未勾选：保留配置、登录状态、历史与 Skill。";
        };
        f.onChange = [update](Form &form, const std::string &key) {
            if (key == "thorough") {
                try {
                    update(form);
                } catch (const std::exception &e) {
                    form.set("thorough", "false");
                    update(form);
                    form.help = std::string("无法启用彻底清理：") + e.what();
                }
            }
        };
        f.canClose = [](Form &) { return true; };
        f.actions = {{"返回", [](Form &form) { form.close(); }, 0},
                     {"确认卸载",
                      [execute](Form &form) {
                          bool full = form.get("thorough") == "true";
                          form.dirty = false;
                          form.close();
                          execute(full);
                      },
                      1}};
        update(f);
        return;
    }
    if (remove && !confirm("卸载 " + agent["name"].get<std::string>(),
                           "使用官方卸载方式移除 " + agent["name"].get<std::string>() + "？"))
        return;
    execute(false);
}
void App::providerDialog(const std::string &vendor, const std::string &id) {
    J p = J::object();
    for (auto &preset : presets)
        if (preset["id"] == vendor)
            p = preset;
    if (!id.empty())
        for (auto &row : providers)
            if (row["id"] == id)
                p.update(row);
    Field protocol{"protocol", "接口协议", p.value("protocol", "chat")};
    protocol.options = {{"chat", "OpenAI Chat Completions"},
                        {"responses", "OpenAI Responses"},
                        {"anthropic", "Anthropic Messages"}};
    Field auth{"auth", "认证方式",
               p.value("auth", vendor == "azure"                        ? "api-key"
                               : p.value("protocol", "") == "anthropic" ? "x-api-key"
                                                                        : "bearer")};
    auth.options = {{"bearer", "Authorization · Bearer"},
                    {"x-api-key", "x-api-key · Anthropic"},
                    {"api-key", "api-key · Azure"}};
    Field token{"token_field", "输出上限字段",
                p.value("token_field", vendor == "openai" ? "max_completion_tokens" : "max_tokens")};
    token.options = {{"max_tokens", "max_tokens"},
                     {"max_completion_tokens", "max_completion_tokens"},
                     {"none", "不发送（服务自行控制）"}};
    J ids = p.value("models", J::array());
    if (ids.empty() && !p.value("model", "").empty())
        ids.push_back(p["model"]);
    std::string modelLines;
    for (auto &m : ids)
        modelLines += m.get<std::string>() + "\n";
    Field list{"models", "模型列表", modelLines, "填写多个模型 ID，或点击“保存并获取模型”读取服务端列表。",
               true};
    list.fullWidth = true;
    list.modelTable = true;
    list.height = 164;
    Field apiKey{"api_key", "API Key",
                 "",        !id.empty() && p.value("has_key", false) ? "留空保留已有密钥" : "可选，加密保存",
                 false,     true};
    Field environment{"key_env", "密钥环境变量", p.value("key_env", ""), "可选"};
    environment.section = 1;
    auth.section = 1;
    token.section = 1;
    Field context{"context_window", "上下文上限（tokens）", std::to_string(p.value("context_window", 0)),
                  "0 表示未指定；Kimi 自定义模型必填"};
    context.section = 1;
    Field endpoint{"base_url", "Base URL", p.value("base_url", ""), "API 基础地址"};
    endpoint.fullWidth = true;
    std::vector<Field> fields = {{"name", "连接名称", p.value("name", "")},
                                 protocol,
                                 endpoint,
                                 list,
                                 {"model", "默认模型 ID", p.value("model", ""), "留空使用列表第一项"},
                                 apiKey,
                                 environment,
                                 auth,
                                 token,
                                 context};
    auto &f = form(id.empty() ? "添加模型连接" : "编辑模型连接", 840, 800, fields);
    f.columns = 2;
    f.sections = {"连接与模型", "高级认证"};
    for (auto &field : f.fields)
        if (field.key == "model")
            field.section = -1;
    for (auto &field : f.fields)
        if (field.key == "api_key")
            field.fullWidth = true;
    f.brandName = vendor;
    f.subtitle = p.value("name", vendor);
    f.help = "HTTPS 远程服务；本地模型允许 HTTP。更换服务主机时会清除旧密钥。";
    auto collect = [vendor, id](Form &f) {
        J data = {{"name", f.get("name")},
                  {"vendor", vendor},
                  {"base_url", f.get("base_url")},
                  {"protocol", f.get("protocol")},
                  {"model", f.get("model")},
                  {"key_env", f.get("key_env")},
                  {"api_key", f.get("api_key")},
                  {"auth", f.get("auth")},
                  {"token_field", f.get("token_field")}};
        J ids = J::array();
        auto context = trim(f.get("context_window"));
        if (!context.empty() && context.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("上下文上限必须为整数");
        data["context_window"] = context.empty() ? 0 : std::stoi(context);
        std::istringstream lines(f.get("models"));
        std::string line;
        while (std::getline(lines, line))
            if (!trim(line).empty())
                ids.push_back(trim(line));
        data["models"] = ids;
        if (!id.empty())
            data["id"] = id;
        return data;
    };
    auto baseline = std::make_shared<J>(collect(f));
    (*baseline)["api_key"] = "";
    f.canClose = [baseline, collect](Form &f) {
        if (!f.dirty || collect(f) == *baseline)
            return true;
        return f.confirm("未保存的设置", "关闭并放弃未保存的连接设置？");
    };
    f.actions = {{"取消", [](Form &f) { f.close(); }, 0}};
    if (!p.value("docs", "").empty())
        f.actions.push_back({"官方文档", [p](Form &f) { f.browse(p["docs"]); }, 0});
    if (!id.empty())
        f.actions.push_back({"清除密钥", [](Form &) {}, 0}); // Replaced below with a typed callback.
    if (!id.empty())
        f.actions.back().fn = [this, id](Form &f) {
            if (f.confirm("清除已存密钥", "清除此连接的 DPAPI 密钥？环境变量仍可用于认证。")) {
                auto [data, key] = models.get(id);
                SecureZeroMemory(key.data(), key.size());
                data["clear_key"] = true;
                models.save(data);
                f.set("api_key", "");
                refresh();
                f.notice("已清除本工具保存的密钥");
            }
        };
    f.actions.push_back({"保存并获取模型", {}, 0});
    // New connections retain their generated ID for subsequent save/discover operations.
    auto savedId = std::make_shared<std::string>(id);
    f.actions.back().fn = [this, collect, savedId, baseline](Form &f) {
        auto data = collect(f);
        if (!savedId->empty())
            data["id"] = *savedId;
        auto saved = models.save(data);
        *savedId = saved["id"];
        f.set("api_key", "");
        *baseline = collect(f);
        f.dirty = false;
        refresh();
        auto id = *savedId;
        f.job([this, id] { return models.discover(id); },
              [this, &f, id, baseline, collect](J rows) {
                  auto [p, key] = models.get(id);
                  SecureZeroMemory(key.data(), key.size());
                  J merged = p.value("models", J::array());
                  for (auto &name : rows)
                      if (std::find(merged.begin(), merged.end(), name) == merged.end())
                          merged.push_back(name);
                  p["models"] = merged;
                  if (p.value("model", "").empty() && !merged.empty())
                      p["model"] = merged[0];
                  auto saved = models.save(p);
                  std::string lines;
                  for (auto &name : merged)
                      lines += name.get<std::string>() + "\n";
                  f.set("models", lines);
                  f.set("model", saved["model"]);
                  *baseline = collect(f);
                  f.dirty = false;
                  refresh();
                  f.notice("已获取并保存 " + std::to_string(rows.size()) + " 个模型，列表已在当前窗口更新");
              },
              "获取模型列表");
    };
    f.actions.push_back({"保存",
                         [this, collect, savedId](Form &f) {
                             auto data = collect(f);
                             if (!savedId->empty())
                                 data["id"] = *savedId;
                             models.save(data);
                             f.set("api_key", "");
                             f.dirty = false;
                             f.close();
                             refresh();
                             notice("模型连接已保存");
                         },
                         1});
    f.onChange = [vendor](Form &f, const std::string &key) {
        if (key == "protocol") {
            auto proto = f.get("protocol");
            f.set("auth", vendor == "azure" ? "api-key" : proto == "anthropic" ? "x-api-key" : "bearer");
        }
    };
}
void App::configDialog(const std::string &aid, const std::string &pid) {
    auto info = adapters.preview(aid, pid);
    auto &f = form("模型配置预览", 860, 660,
                   {{"path", "配置位置", info["path"], "", false, false, true},
                    {"fragment", "修改字段 / 接入参数", info["content"], "", true, false, true}});
    f.brandName = aid;
    f.subtitle = info["note"];
    f.actions = {{"关闭", [](Form &f) { f.close(); }, 0},
                 {"复制参数", [info](Form &f) { f.clipboard(info["content"]); }, 0}};
    if (info["can_apply"].get<bool>())
        f.actions.push_back({"备份并应用",
                             [this, info](Form &f) {
                                 adapters.apply(info["ticket"]);
                                 f.close();
                                 refresh();
                                 notice("配置已备份并应用");
                             },
                             1});
}
} // namespace ad

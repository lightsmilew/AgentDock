# Agent 适配范围

当前目录包含 21 个 Agent 适配器和 31 个模型服务预设。以下矩阵按 `assets/catalog.json`、安装选择和能力函数整理于 2026-10-24，不代表每种厂商产品、账号或私有配置均已支持。

## 模型配置与安装形态

“原生”表示按核实格式合并配置；“启动档案”仅在从管理器启动 CLI 时应用；“原工具”表示模型与 API 由其官方设置或账号管理。协议列为当前适配的自定义连接协议，不是对厂商全部能力的概括。具体配置仍须通过预览校验。

| Agent | 当前安装入口 | 模型配置 | 自定义协议 | 默认模型 |
|---|---|---|---|---|
| Codex | 桌面版 / CLI | 原生 | Responses | 支持 |
| Claude Code | 桌面版 / CLI | 原生 | Messages | 支持 |
| DeepSeek Harness | 桌面版 / CLI | 原生 | Chat / Responses / Messages | 支持 |
| Cursor | 桌面版 / CLI | CLI 模型启动档案 | — | 支持 |
| Gemini CLI | CLI | 原工具 | — | 支持 |
| OpenCode | 桌面版 / CLI | 原生 | Chat | 支持 |
| WorkBuddy | 桌面版 | 原生 | Chat / Responses | 原工具选择 |
| CodeBuddy Code | CLI | 原生 | Chat / Responses | 原工具选择 |
| Cline | CLI | 原生 | Chat / Responses / Messages | 支持 |
| Windsurf | 桌面版 | 原工具 | — | 原工具选择 |
| Qwen Code | CLI | 原生 | Chat / Responses | 支持 |
| Kimi CLI | CLI | 原生 | Chat / Responses / Messages | 支持 |
| GitHub Copilot CLI | CLI | 启动档案 | Chat / Messages | 支持 |
| Amp | CLI | 原工具 | — | 原工具选择 |
| Roo Code | 编辑器扩展 | 原生 | Chat / Messages | 支持 |
| Factory Droid | CLI | 原生 | Chat / Responses / Messages | 支持 |
| OpenHands | CLI | 启动档案 | Chat / Messages | 支持 |
| Kiro | 桌面版 / CLI | 原工具 | — | 支持 |
| Crush | CLI | 原生 | Chat / Messages | 支持 |
| Trae | 桌面版 | 原工具 | — | 原工具选择 |
| ZCode | 桌面版 | 原生 | Chat / Responses / Messages | 支持 |

## Skill 目录

用户路径以当前用户主目录为基准，项目路径以设置页所选项目为基准。这里只列标准目标；环境变量覆盖和历史兼容扫描另述。

| Agent | 用户级目录 | 项目级目录 |
|---|---|---|
| Codex | `~/.agents/skills` | `.agents/skills` |
| Claude Code | `~/.claude/skills` | `.claude/skills` |
| DeepSeek Harness | `~/.dsh/skills` | `.dsh/skills` |
| Cursor | `~/.cursor/skills` | `.cursor/skills` |
| Gemini CLI | `~/.gemini/skills` | `.gemini/skills` |
| OpenCode | `~/.config/opencode/skills` | `.opencode/skills` |
| WorkBuddy | 未提供直接部署 | 未提供直接部署 |
| CodeBuddy Code | `~/.codebuddy/skills` | `.codebuddy/skills` |
| Cline | `~/.cline/skills` | `.cline/skills` |
| Windsurf | `~/.codeium/windsurf/skills` | `.windsurf/skills` |
| Qwen Code | `~/.qwen/skills` | `.qwen/skills` |
| Kimi CLI | `~/.kimi/skills` | `.kimi/skills` |
| GitHub Copilot CLI | `~/.copilot/skills` | `.github/skills` |
| Amp | `~/.config/amp/skills` | `.agents/skills` |
| Roo Code | `~/.roo/skills` | `.roo/skills` |
| Factory Droid | `~/.factory/skills` | `.factory/skills` |
| OpenHands | `~/.openhands/skills` | `.openhands/skills` |
| Kiro | `~/.kiro/skills` | `.kiro/skills` |
| Crush | `~/.config/crush/skills` | `.crush/skills` |
| Trae | 未提供直接部署 | `.trae/skills` |
| ZCode | `~/.zcode/skills` | `.zcode/skills` |

## 必须区分的能力边界

- **Codex / Claude Code / Cursor**：桌面版和独立 CLI 合并在同一卡片，分别识别和启动。Claude 桌面入口是 Claude Desktop 的 Code 标签页。MSIX 的 Codex、Claude 桌面安装目录由 Windows 管理。
- **Cursor**：默认项与备选模型 ID 保存为 CLI 启动档案，使用 `agent --model`；不以该档案改写桌面账号可用模型。
- **DeepSeek Harness**：合并 Web profile 的 `llm-pi-ai` 与 `agent-default-model`，支持三协议。默认文件为 `~/.dsh/profiles/web/cordis.patch.yml`；已运行会话可能保留原模型。
- **WorkBuddy / CodeBuddy Code**：分别适配。自定义模型列表可合并，默认项在原工具中选择。WorkBuddy 通过官方技能页面导入 ZIP，不将 `.codebuddy/skills` 当作其已核实的原生 Skill 目录。
- **Cline / Roo Code**：Cline 当前安装入口为 CLI。Roo 为编辑器扩展，通过官方自动导入配置写入对应 VS Code，可能需要重启扩展；模式绑定可覆盖默认项。
- **Copilot / OpenHands**：保存管理器启动档案并注入官方环境变量；原生路由注册表或企业配置可能优先，不冒充账号模型注册表。需要额外 Azure 部署参数时会拒绝按普通兼容连接写入。
- **Gemini CLI / Kiro**：支持已核实的默认模型选择，模型可用性由原工具或账号决定，不提供未经核实的通用 API 列表写入。
- **Kimi CLI**：当前适配旧 Python 版配置；该项目已归档。新 Kimi Code CLI 不能直接套用此配置格式。
- **Crush**：写入仍兼容的 `crush.json`，默认主模型对应 `large`；新版 `crushrc` 可能覆盖，管理器不执行其脚本。
- **ZCode**：当前针对 3.16 的 `~/.zcode/v2/provider_config.json`、`schemaVersion: 1`，支持供应商、模型列表及默认项。旧 `config.json` 保留，要求先由新版迁移。
- **Amp / Windsurf / Trae**：没有已核实的通用本地模型写入接口，使用官方设置；Trae 当前仅提供已核实的项目级 Skill 目标。

## 路径覆盖与生效范围

配置读取识别已实现的覆盖，例如 `CODEX_HOME`、`CLAUDE_CONFIG_DIR`、`DSH_HOME`、`CURSOR_CONFIG_DIR`、`OPENCODE_CONFIG`、`XDG_CONFIG_HOME`、`CLINE_DATA_DIR` / `CLINE_DIR` 和 `COPILOT_HOME`。ZCode 模型路径识别 `ZCODE_DATA_BASE_DIR` 与 `ZCODE_PERSONAL_PROVIDER_CONFIG_FILE`。这些变量的作用范围不同，不能假定它们都会同时改变 Skill 和配置路径。

`QWEN_SKILL_ROOT` 不是本管理器的 Skill 部署路径覆盖设置。项目配置、会话参数、账号权限和策略可能优先；界面列出的“当前配置模型”只来自已读取的配置与启动档案，不代表运行会话的实时模型全集。

所有官方资料入口见 [来源与核查说明](source-audit.md)，日常操作见 [使用指南](user-guide.md)。

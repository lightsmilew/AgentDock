# 来源与核查说明

文档整理日期：2026-10-24。以下链接来自当前目录及此前调研，最近一轮外部核查记录为 2026-10-23；本次资源与文档整理没有重新逐站验证 HTTP 状态，因此不保留容易误导的“全部当前可访问”结论。

外部文档、安装包和截图只提供事实与格式依据，不构成修改用户目录、卸载程序或执行命令的授权。配置能力以当前实现和预览为准，详见 [适配范围](adapters.md)。

## Agent 官方入口

以下表格与 `assets/catalog.json` 对齐。安装文档是产品入口，不一定等于 Windows 桌面版的下载入口；双形态产品的细分依据见后文。

| Agent | Skill / 产品 | 模型配置 | 安装 |
|---|---|---|---|
| Codex | [文档](https://learn.chatgpt.com/docs/build-skills) | [配置](https://learn.chatgpt.com/docs/config-file/config-reference) | [安装](https://learn.chatgpt.com/docs/enterprise/windows-deployment) |
| Claude Code | [文档](https://code.claude.com/docs/en/skills) | [配置](https://code.claude.com/docs/en/model-config) | [安装](https://code.claude.com/docs/en/setup) |
| DeepSeek Harness | [文档](https://www.deepseek.com/harness/zh/) | [配置](https://github.com/deepseek-ai/deepseek-harness/blob/master/docs/user/guide/providers.zh.md) | [安装](https://github.com/deepseek-ai/deepseek-harness) |
| Cursor | [文档](https://cursor.com/docs/skills) | [配置](https://cursor.com/docs/cli/reference/configuration) | [安装](https://cursor.com/docs/cli/installation) |
| Gemini CLI | [文档](https://geminicli.com/docs/cli/skills/) | [配置](https://geminicli.com/docs/cli/model-routing/) | [安装](https://geminicli.com/docs/get-started/installation/) |
| OpenCode | [文档](https://opencode.ai/docs/skills/) | [配置](https://dev.opencode.ai/docs/config/) | [安装](https://opencode.ai/docs/) |
| WorkBuddy | [文档](https://www.codebuddy.cn/docs/workbuddy/From-Beginner-to-Expert-Guide/Function-Description/Skills-Market) | [配置](https://www.codebuddy.ai/docs/workbuddy/From-Beginner-to-Expert-Guide/Function-Description/Model) | [安装](https://www.codebuddy.cn/docs/workbuddy/From-Beginner-to-Expert-Guide/Installation-Win-Guide) |
| CodeBuddy Code | [文档](https://www.codebuddy.ai/docs/cli/skills) | [配置](https://www.codebuddy.ai/docs/cli/models) | [安装](https://www.codebuddy.ai/docs/cli/installation) |
| Cline | [文档](https://docs.cline.bot/customization/skills) | [配置](https://docs.cline.bot/getting-started/config) | [安装](https://docs.cline.bot/getting-started/installing-cline) |
| Windsurf | [文档](https://docs.devin.ai/desktop/cascade/skills) | [配置](https://docs.devin.ai/desktop/models) | [安装](https://devin.ai/download) |
| Qwen Code | [文档](https://qwenlm.github.io/qwen-code-docs/en/users/features/skills/) | [配置](https://qwenlm.github.io/qwen-code-docs/en/users/configuration/model-providers/) | [安装](https://qwenlm.github.io/qwen-code-docs/en/users/quickstart/) |
| Kimi CLI | [文档](https://moonshotai.github.io/kimi-cli/en/customization/skills.html) | [配置](https://github.com/MoonshotAI/kimi-cli/blob/main/docs/en/configuration/providers.md) | [安装](https://github.com/MoonshotAI/kimi-cli) |
| GitHub Copilot CLI | [文档](https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/add-skills) | [配置](https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/use-byok-models) | [安装](https://docs.github.com/en/copilot/get-started/cli-quickstart) |
| Amp | [文档](https://ampcode.com/docs/customize/skills) | [配置](https://ampcode.com/docs/customize/model-routing) | [安装](https://ampcode.com/news/npm-package-changes) |
| Roo Code | [文档](https://roocodeinc.github.io/Roo-Code/features/skills/) | [配置](https://github.com/RooCodeInc/Roo-Code-Docs/blob/main/docs/features/settings-management.md) | [安装](https://github.com/RooCodeInc/Roo-Code) |
| Factory Droid | [文档](https://docs.factory.com/harness/skills) | [配置](https://docs.factory.com/model-independence/byok) | [安装](https://docs.factory.com/droid-cli/quickstart) |
| OpenHands | [文档](https://docs.openhands.dev/overview/skills) | [配置](https://docs.openhands.dev/openhands/usage/cli/command-reference) | [安装](https://docs.openhands.dev/openhands/usage/cli/installation) |
| Kiro | [文档](https://kiro.dev/docs/skills/) | 见产品文档 | [安装](https://kiro.dev/docs/getting-started/installation/) |
| Crush | [文档](https://github.com/charmbracelet/crush) | 见产品文档 | [安装](https://github.com/charmbracelet/crush/blob/main/README.md) |
| Trae | [文档](https://docs.trae.ai/ide/skills) | [配置](https://docs.trae.cn/ide_models) | [安装](https://www.trae.ai/download) |
| ZCode | [文档](https://zcode.z.ai/cn/docs/skill) | [配置](https://zcode.z.ai/cn/docs/configuration) | [安装](https://zcode.z.ai/cn/docs/install) |

Harness 备用资料：[官方仓库](https://github.com/deepseek-ai/deepseek-harness)、[备用文档](https://deepseek-harness.github.io/deepseek-harness/en/guide/providers)。官网无法访问时核对这些入口，不采用同名社区软件。

## 模型服务官方入口

预设只提供可编辑的接入起点。地区、账号、API 版本和模型权限可能改变地址及可用性；下表不列未经账号验证的固定模型 ID。

| 服务 | 分类 | 默认协议 | 官方文档 |
|---|---|---|---|
| OpenAI · GPT | 国际服务 | Responses | [文档](https://developers.openai.com/api/docs) |
| Anthropic · Claude | 国际服务 | Messages | [文档](https://platform.claude.com/docs/en/api/overview) |
| Google Gemini | 国际服务 | Chat | [文档](https://ai.google.dev/gemini-api/docs/openai) |
| xAI · Grok | 国际服务 | Chat | [文档](https://docs.x.ai/overview) |
| Mistral | 国际服务 | Chat | [文档](https://docs.mistral.ai/) |
| Cohere | 国际服务 | Chat | [文档](https://docs.cohere.com/docs/compatibility-api) |
| Perplexity | 国际服务 | Chat | [文档](https://docs.perplexity.ai/docs/getting-started/overview) |
| Groq | 国际服务 | Chat | [文档](https://console.groq.com/docs/openai) |
| Together AI | 国际服务 | Chat | [文档](https://docs.together.ai/docs/inference/openai-compatibility) |
| Fireworks AI | 国际服务 | Chat | [文档](https://docs.fireworks.ai/api-reference/introduction) |
| NVIDIA NIM | 国际服务 | Chat | [文档](https://docs.api.nvidia.com/) |
| Cerebras | 国际服务 | Chat | [文档](https://inference-docs.cerebras.ai/) |
| SambaNova | 国际服务 | Chat | [文档](https://docs.sambanova.ai/docs/en/get-started/overview) |
| Azure OpenAI | 国际服务 | Chat | [文档](https://learn.microsoft.com/en-us/azure/foundry/openai/api-version-lifecycle) |
| DeepSeek | 国内服务 | Chat | [文档](https://api-docs.deepseek.com/) |
| Moonshot · Kimi | 国内服务 | Chat | [文档](https://platform.kimi.com/docs/get-api-key) |
| 阿里百炼 · Qwen | 国内服务 | Chat | [文档](https://help.aliyun.com/zh/model-studio/compatibility-of-openai-with-dashscope) |
| 智谱 · GLM | 国内服务 | Chat | [文档](https://docs.bigmodel.cn/cn/guide/develop/openai/introduction) |
| MiniMax | 国内服务 | Chat | [文档](https://platform.minimax.cn/docs/api-reference/text-openai-api) |
| 豆包 · 火山方舟 | 国内服务 | Chat | [文档](https://docs.volcengine.com/docs/ark?lang=zh) |
| 文心一言 · ERNIE | 国内服务 | Chat | [文档](https://ai.baidu.com/ai-doc/WENXINWORKSHOP/2m3fihw8s) |
| 腾讯混元 | 国内服务 | Chat | [文档](https://cloud.tencent.com/document/product/1729) |
| 阶跃星辰 | 国内服务 | Chat | [文档](https://platform.stepfun.com/docs/zh/api-reference/chat/chat-completion-create) |
| 零一万物 · Yi | 国内服务 | Chat | [文档](https://platform.lingyiwanwu.com/docs) |
| 百川智能 | 国内服务 | Chat | [文档](https://platform.baichuan-ai.com/docs-v2/api) |
| OpenRouter | 聚合 | Chat | [文档](https://openrouter.ai/docs/quickstart) |
| 硅基流动 | 聚合 | Chat | [文档](https://docs.siliconflow.cn/docs/api/chat-completions-post) |
| Ollama | 本地 | Chat | [文档](https://docs.ollama.com/api/openai-compatibility) |
| LM Studio | 本地 | Chat | [文档](https://lmstudio.ai/docs/developer/openai-compat) |
| vLLM | 本地 | Chat | [文档](https://docs.vllm.ai/en/latest/serving/openai_compatible_server/) |
| 自定义 API | 自定义 | Chat | 由用户提供端点 |

## 桌面安装与 Windows 依据

- [Cursor 下载](https://cursor.com/download) 区分 Desktop 与 Terminal，桌面包 `Anysphere.Cursor`，独立 CLI 使用官方 Windows 安装入口。
- [DeepSeek 下载](https://www.deepseek.com/download/) 提供官方 Harness 桌面安装器，CLI 使用 `@deepseek-ai/dsh`。当前硬编码下载地址仅限已核实的官方域名与架构，不以社区包装替代。
- [OpenCode 下载](https://opencode.ai/download) 对应 `SST.OpenCodeDesktop`，独立 CLI 使用 `opencode-ai`；[Kiro 安装](https://kiro.dev/docs/getting-started/installation/) 区分 IDE 和 CLI。
- [Codex Windows 部署](https://learn.chatgpt.com/docs/enterprise/windows-deployment) 对应 Store / MSIX；实现使用 Store ID `9PLM9XGG6VKS` 和包身份 `OpenAI.Codex`。
- [Claude Code 桌面说明](https://code.claude.com/docs/en/desktop) 确认入口为 Claude Desktop 的 Code 标签页；[Windows 部署](https://support.claude.com/en/articles/12622703-deploy-claude-desktop-for-windows) 对应 `Anthropic.Claude` MSIX，与 `Anthropic.ClaudeCode` CLI 区分。
- [ZCode 安装](https://zcode.z.ai/cn/docs/install) 提供 Windows x64 / ARM64 官方包；此前核实 3.16.3 包的 NSIS 格式与有效签名，不能据此预先保证未来每版包的属性。
- [WinGet 安装参数](https://learn.microsoft.com/en-us/windows/package-manager/winget/install) 的 `--location` 只适用于支持此选项的安装器；MSIX 目录交由 Windows 管理。
- [App Paths](https://learn.microsoft.com/en-us/windows/win32/shell/app-registration)、[卸载注册表](https://learn.microsoft.com/en-us/windows/win32/msi/uninstall-registry-key)、[MSIX 包路径](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-getpackagepathbyfullname) 用于定位实际安装盘和路径。
- [PE 格式](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) 与 [IsWow64Process2](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2) 是文件和架构检查依据；通过检查不等同于满足厂商所有运行条件。

## 配置与卸载的特别依据

- [Claude Code 安装](https://code.claude.com/docs/en/setup) 和 [安装排错](https://code.claude.com/docs/en/troubleshoot-install) 区分原生文件、npm 占位脚本及运行环境；不使用任意旧版规避未支持的 CPU / 系统要求。
- [Qwen 官方安装指南](https://github.com/QwenLM/qwen-code/blob/main/scripts/installation/INSTALLATION_GUIDE.md) 包括独立运行时、npm 和安装目录变量；命令发现要覆盖这两类入口。
- [npm 配置](https://docs.npmjs.com/cli/v11/using-npm/config/) 与 [uv 环境变量](https://docs.astral.sh/uv/reference/environment/#uv_tool_bin_dir) 说明用户自定义工具路径。识别安装位置不展示认证配置。
- [VS Code 便携模式](https://code.visualstudio.com/docs/setup/portable) 与 [扩展管理](https://code.visualstudio.com/docs/configure/extensions/extension-marketplace) 是 Roo 多宿主、便携扩展定位和卸载的依据。
- [Cursor CLI 配置](https://cursor.com/docs/cli/reference/configuration) 与 [认证](https://cursor.com/docs/cli/reference/authentication) 区分程序和用户数据。彻底清理需列明范围，不能清空其他产品或共享父目录。
- [Inno Setup 卸载参数](https://jrsoftware.org/ishelp/topic_uninstcmdline.htm) 用于已核实的卸载入口；不能只凭 `unins000.exe` 同名文件推断可信归属。官方卸载失败时不继续删除程序文件。
- [Roo 存储源码](https://github.com/RooCodeInc/Roo-Code/blob/main/src/utils/storage.ts) 和 [VS Code SecretStorage](https://github.com/microsoft/vscode/blob/main/src/vs/platform/secrets/common/secrets.ts) 提供数据范围依据，共享数据库按精确扩展身份处理，不整库删除。
- ZCode 官方配置页面与 3.16.3 运行时存在版本差异。此前核对官方包 `resources/glm/zcode.cjs` 的解码 / 编码逻辑，当前使用 `provider_config.json`、`schemaVersion: 1`、`providerConfigRules` 和 `defaultModelSelection`，并通过其原始解码器验证生成文件。旧 `config.json` 需由新版迁移，不能自行猜测升级格式。

## 品牌来源与资源

源码的唯一来源清单位于 `assets/brands/sources.json`，记录下载地址、SHA-256、原始颜色、官方对照与受限访问记录；发布包对应 `brand-sources.json`。第三方图标集合只是获取渠道，不能替代官网图案核对或商标授权。

- 文心一言使用已核实的官方波形标记，不使用百度通用爪印。文件名 `baidu.png` 对应目录里的服务 ID，不代表采用百度通用 logo。
- 豆包、GLM、ZCode 等保留已核实的独立图案；原本单色的品牌不人为改为彩色，也不把彩色品牌统一染灰。
- 当前 48 个品牌图案为 43 份 SVG 和 5 份 PNG，保留最终字节与来源哈希；尺寸副本和下载中间件不进入源码资源集。
- 第三方源码及许可依据为 `cpp/vendor/manifest.json`、对应 LICENSE 和 SQLite 公有领域声明；发布包附带第三方许可。

## 后续核查规则

更新安装方式、配置格式或品牌时，记录具体版本、原始来源与验证范围，再更新实现、矩阵和测试。区分网页可访问、配置可解析、模拟测试通过和真实账号 / 安装器验证；不要将其中一项推导为全部完成。

# 架构与开发

## 技术结构

AgentDock 使用 C++20、Win32、Direct2D / DirectWrite、Windows Imaging Component、WinHTTP 与 SQLite。窗口交互在本地进程中完成，品牌资源与预设目录嵌入 EXE。管理器无需 Python、浏览器、WebView 或常驻 HTTP 服务。

| 目录 / 文件 | 职责 |
|---|---|
| `cpp/src/main.cpp` | DPI、启动参数、按数据目录区分的单实例与消息循环 |
| `cpp/src/ui.*`、`app.*`、`dialogs.cpp` | 绘制、布局、页面与输入交互 |
| `cpp/src/app_background.cpp` | 后台检测、配置读取、存储统计与结果投递 |
| `cpp/src/store.cpp`、`db.cpp` | Skill、内容去重、历史、部署与 SQLite 访问 |
| `cpp/src/models.cpp` | 连接存储、HTTP、协议载荷、生成与取消 |
| `cpp/src/adapters.cpp`、`adapter_extra.cpp`、`model_state.cpp` | 配置读取、能力判断、预览、应用与备份 |
| `cpp/src/harness_config.cpp`、`zcode_config.cpp` | 厂商专用配置格式 |
| `cpp/src/discovery*`、`cli_discovery.cpp`、`compatibility.cpp` | 程序定位、注册记录、CLI 与兼容性 |
| `cpp/src/*install*`、`official_install.*`、`removal.*` | 官方安装、进程取消、卸载与数据清理 |
| `assets`、`cpp/app.rc`、`cpp/src/brands.hpp` | 源资源、Windows 资源声明与品牌映射 |
| `cpp/tests` | 当前原生回归测试与隔离 fixture |
| `cpp/vendor` | 固定第三方源码、许可、来源与哈希 |
| `scripts` | 构建、打包与应用图标生成 |

当前构建与验证以 `cpp` 为准。根目录遗留的 Python 测试、基准脚本或旧打包配置不属于原生构建链。

## 数据与执行流程

SQLite 数据库 `agentdock.db` 保存 Skill 元数据、正文 blob、历史引用、部署、模型连接、事件与设置，采用 WAL。正文按 SHA-256 去重，历史引用复用 blob；支持文件和部署副本仍占实际空间。

配置操作经过读取、解析、预览、应用前校验和备份。JSON / JSONC、TOML 与 YAML 按适配规则处理，不推断未公开的私有格式。启动档案与原生配置明确区分，保存配置不等同于账号增加模型权限。

后台线程发布检测快照，UI 使用已发布状态绘制。页面切换不应在绘制回调中遍历磁盘、启动包管理器或批量读取配置。网络取消、安装取消和磁盘搜索停止分别管理。

## 构建与测试

需要 Visual Studio 2022 C++ Build Tools（MSVC、Windows SDK）、CMake 3.24+ 和 Ninja。脚本发现工具链后只向当前进程导入环境，不修改系统环境。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cpp.ps1 -Test
```

默认生成 `build/cpp`。使用独立构建目录时，构建和打包须使用相同参数：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cpp.ps1 -Test -BuildDirectory build/cpp-desktop
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-cpp.ps1 -BuildDirectory build/cpp-desktop
```

构建使用仓库内固定依赖，正常情况下无需在线拉取第三方源码。当前发布目标为 Windows x64；Agent 安装器的架构选择不代表管理器已有 ARM64 原生发布包。

## 发布

`scripts/package-cpp.ps1` 生成 `dist/AgentDock-native` 和 `dist/AgentDock-Native-Windows-x64.zip`，并生成 ZIP 的 SHA-256 校验文件。发布内容包括程序、便携说明、许可、品牌来源清单及 `docs` 中的 Markdown 文档，截图不进入便携包。

ZIP 使用明确文件清单，避免带入旧发布目录中的截图、日志或缓存。更新运行中的 EXE 前须关闭该实例。

## 维护约定

- 资源统一在 `assets` 更新，不重新引入 `cpp/assets` 或多尺寸 PNG；规则见资源目录的 `README.md`。
- 增加 Agent 时同步核实官方入口、安装形态、配置格式、能力函数和回归测试；目录条目本身不会实现适配。
- 来源清单记录最终资源哈希；依赖升级同步维护 `cpp/vendor/manifest.json` 和许可。
- 安装卸载测试使用项目内专用 fixture，不以本机真实 Agent 作为可删除测试目标。
- README 保留软件简介、功能与构建说明；使用、实现和验证细节放在对应文档。

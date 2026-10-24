# AgentDock

AgentDock 是 Windows 本地 Skill、Agent 与模型服务管理器，提供 Skill 分类与部署、多 Agent 管理、多模型连接及 LLM Skill 生成功能。

## 启动

双击 `AgentDock.exe`。要求 Windows 10 1703 及以上或 Windows 11，x64，无需额外运行库。各 Agent 的系统要求以其官方说明为准。

默认数据目录为 `%LOCALAPPDATA%\AgentDock`。需要独立数据目录时，可运行：

```powershell
.\AgentDock.exe --data-dir "D:\AgentDockData"
```

## 文档

- [使用指南](../docs/user-guide.md)
- [Agent 适配范围](../docs/adapters.md)
- [常见问题](../docs/troubleshooting.md)
- [完整文档目录](../docs/README.md)

第三方许可见 `THIRD-PARTY-NOTICES.txt`，品牌来源见 `brand-sources.json`。品牌及商标归各自所有者。

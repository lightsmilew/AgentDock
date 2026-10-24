# 性能与验证

## 当前实现

| 目标 | 已采用的方式 | 适用边界 |
|---|---|---|
| 本地搜索 | 3 个及以上 Unicode 字符使用 SQLite FTS5 trigram；较短查询使用转义 LIKE | 按名称、标题、描述匹配，不含向量语义检索；极短查询可能遍历较多行 |
| 搜索交互 | 输入防抖、分页及结果复用 | 不将键入变为模型调用 |
| Skill 扫描 | 修改时间与大小指纹跳过未变正文 | 不是持续文件监控；时间和大小均未变的修改可能需另行确认 |
| 扫描范围 | 明确根目录、深度限制、路径去重和总遍历预算 | Skill 扫描深度至 3 层，最多遍历 100000 个目录项 |
| 存储 | 正文 SHA-256 去重，历史版本复用 blob | 支持文件、部署副本与配置备份仍占实际空间 |
| 页面响应 | 已发布检测快照、条件变量驱动的后台任务，存储统计另行更新 | 耗时受磁盘、安装数量和权限影响 |
| 安装识别 | 注册记录、快捷方式、PATH、包管理器与已知目录优先；完整磁盘搜索按需触发 | 未登记便携程序可手动定位，权限不足的目录无法保证枚举 |
| 绘制和资源 | Direct2D / DirectWrite，SVG 绘制及必要 PNG | 图标按来源保留配色，不统一染灰 |

Skill 扫描与 Agent 安装搜索是独立流程，不能将 Skill 深度限制理解为 Agent 只能安装在固定层级。

## 体积控制

Release 使用静态 CRT、`/O1`、链接时优化、`/OPT:REF` 与 `/OPT:ICF`。依赖编入 EXE，不随包附带编译工具、源码、PDB 或测试文件。

资源只保留使用中的品牌图案，旧界面多尺寸 PNG 移出源码目录。资源已嵌入 EXE，无需在发布包再次复制 `assets`。未参与编译的旧资源清理节省源码空间，不能据此宣称 EXE 按同样字节数缩减。

当前界面截图放在 `docs/images`，不参与程序编译和便携包。

## 验证方法

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cpp.ps1 -Test
Get-Item build/cpp/AgentDock.exe | Select-Object Name,Length
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-cpp.ps1
Get-FileHash dist/AgentDock-Native-Windows-x64.zip -Algorithm SHA256
```

自定义构建目录时同步修改路径。报告体积应区分 EXE、ZIP 和源码资源，注明构建配置，不混用不同阶段的结果。

原生测试覆盖配置合并与默认项、Skill 搜索部署、安装计划与取消、兼容性判断、隔离路径识别、卸载范围、协议请求，以及品牌资源解码和 SVG 颜色渲染。HTTP 测试使用本地模拟服务。

测试通过不能证明所有厂商最新安装器、账号权限及线上 API 均可用。未使用真实账号进行的付费调用、真实桌面安装或本机卸载，不报告为已验证。测试数量与耗时以当次 CTest 输出为准。

启动、切页、内存与大规模 Skill 基准应固定系统、CPU、数据量、冷热缓存及采样方法。目前不承诺未经测量的固定时延或内存上限。

# 资源维护说明

`assets` 是程序资源的唯一源目录。窗口布局、侧栏图标及 Skill 分类图标由程序绘制，不在此生成多份位图。

```text
assets/
  icon.ico             Windows 应用图标
  catalog.json         Agent、模型服务预设及初始 Skill 模板
  brands/
    sources.json       品牌图案、颜色、来源与文件 SHA-256
    *.svg              按原始配色保留的矢量标记
    *.png              官方位图或经核实需保留的标记
```

## 引用关系

- `cpp/app.rc` 将应用图标、目录数据和品牌图标嵌入 EXE，运行时无需外置资源目录。
- `cpp/src/brands.hpp` 定义品牌名称与资源 ID 的对应关系。
- `cpp/CMakeLists.txt` 跟踪资源变化，并向测试提供来源清单路径。
- `scripts/package-cpp.ps1` 将品牌来源清单附入发布包，不复制图标目录。
- 界面截图保存在 `docs/images`，用于文档，不参与程序编译和发布包。

## 更新规范

1. 核对官网图案和颜色，优先保留可被 Direct2D 解析的 SVG；仅有可靠位图时使用 PNG。
2. 一个品牌保留一个实际使用的图案，不生成多尺寸副本，不以第三方同名图标替代未核实的品牌。
3. 在 `brands/sources.json` 记录下载地址、官方依据、原始配色和最终文件的 SHA-256。来源不可访问时保留核查限制。
4. 新品牌同时更新 `app.rc`、`brands.hpp` 和来源清单；PNG 还需检查 `ui.cpp` 与 `brand_tests.cpp` 的解码分支。
5. 运行原生测试，核对 SVG 实际解析、渲染颜色和 PNG 解码，更换格式后移除不再引用的旧文件。

Codex / OpenAI、Harness / DeepSeek 当前各保留同内容的逻辑品牌文件，便于独立维护对应资源 ID。这些是显式别名，不是遗留尺寸副本。

`sha256` 始终对应实际编译的文件。`previous_recorded_sha256` 仅保留清单修正前的记录，不作为当前文件校验值，也不假定其就是原始下载文件的哈希。

第三方许可保存在 `cpp/vendor`；来源清单不等同于商标授权。详见 [来源与核查说明](../docs/source-audit.md)。

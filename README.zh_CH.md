# DucQ 🦆

[English](README.md) | 简体中文

跨平台 [DuckDB](https://duckdb.org) 桌面客户端 —— 纯 C++ / Qt Widgets / DuckDB v1 C API，静态链接，单文件分发。

## 功能

- 打开 / 新建 `.duckdb` 数据库文件，内置内存数据库开箱即用
- Schema 树：按 schema 分组的表 / 视图 / 列，双击生成 `SELECT`
- SQL 编辑器：关键字 / 字符串 / 注释高亮，`Ctrl+Enter` 执行
- 结果表格：虚拟化渲染，默认展示前 100 万行（超过显示"已截断"）
- 查询进度条 + 随时取消（`duckdb_interrupt`）
- 拖入 `csv / tsv / parquet / json` 文件自动建视图并预览
- 结果导出 CSV / Parquet（`COPY` 流式写出，不受展示行数限制）
- 查询历史快速重跑

## 构建

依赖：CMake ≥ 3.24、Ninja、Qt ≥ 6.4（Widgets + Test）、C++20 编译器。DuckDB v2.0.0 静态库由 CMake FetchContent 自动下载（含 SHA256 校验），无需手动安装。

```bash
# macOS（Homebrew Qt）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/DucQ.app

# Linux
sudo apt install qt6-base-dev ninja-build cmake
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/DucQ

# Windows（MSVC + Qt via aqt/install-qt-action）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
.\build\DucQ.exe
```

测试：`ctest --test-dir build --output-on-failure`

三平台 CI：见 [.github/workflows/build.yml](.github/workflows/build.yml)。

## 技术要点

- **线程模型**：每个数据库一个 worker `QThread`；查询在该线程通过 `duckdb_pending_prepared` + `duckdb_pending_execute_task` 驱动，UI 与查询层只经 queued signal/slot 通信；`duckdb_interrupt` 实现跨线程取消。
- **结果访问**：执行完成后获得物化结果（2.0 中 `duckdb_pending_prepared` 默认物化），`duckdb_result` 所有权移交 UI 线程，`duckdb_value_*` 访问器惰性转换单元格——对任意向量布局（字典 / 压缩）安全，避免触碰原始 flat 指针。
- **静态扩展**：DuckDB 2.0 的静态扩展（parquet/json/icu 等）需在 `duckdb_open` 前调用 `duckdb_register_static_extension(<ext>_describe)` 注册，见 `src/core/StaticExtensions.cpp`。
- **大结果集**：物化结果常驻内存，超大数据集请在 SQL 中加 `LIMIT`（导出走 `COPY`，服务端流式，不受影响）。

## Roadmap

- [ ] 大结果集流式展示（服务端分页 / 自动 LIMIT 包装）
- [ ] SQL 自动补全（autocomplete 扩展已静态链接）
- [ ] 内置图表、结果筛选排序
- [ ] 安装包分发（macOS 签名公证 / Windows installer / AppImage）
- [ ] 国际化（当前 UI 为中文）

## 许可

GPL-3.0（Qt 开源版）。DuckDB 为 MIT 许可，静态链接无冲突。

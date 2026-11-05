# DucQ 🦆

English | [简体中文](README.zh_CN.md)

A cross-platform [DuckDB](https://duckdb.org) desktop client — pure C++ / Qt Widgets / DuckDB v1 C API, statically linked for single-binary distribution.

![DucQ main window](docs/screenshots/main-window.png)

*Schema tree, connection selector, SQL editor with highlighting, and a virtualized result table — the query above created the `users` table from a `range()` and reads it back. These screenshots are captured by CI on every push, headlessly on each platform.*

<details>
<summary>Windows and Linux builds (same app, same CI pipeline)</summary>

<p>
  <img src="docs/screenshots/main-window-windows.png" width="45%" alt="DucQ on Windows"> <img src="docs/screenshots/main-window-linux.png" width="45%" alt="DucQ on Linux">
</p>

</details>

![DucQ dark theme](docs/screenshots/main-window-dark.png)

*Dark theme — follows the system appearance by default, switch manually under View → Theme.*

![DucQ result table with NULLs](docs/screenshots/null-values.png)

*NULLs render in gray, numeric columns right-align, and every query opens in its own result tab.*

## Features

- Open / create `.duckdb` database files; an in-memory database works out of the box
- Act as a **client of a remote DuckDB server** (File → Connect to DuckDB Server…): attaches over DuckDB's **Quack** protocol (`ATTACH 'quack:host:9494' AS remote`) — the remote's tables join the schema tree and queries like `FROM remote.main.t` run server-side with filter/projection pushdown; optional token, recent servers remembered (tokens never touch disk); built in by default, `-DDUCQ_QUACK=OFF` compiles it out
- **Share your database over Quack** (File → Serve Database over Quack…): starts a server for the active connection on a chosen port with a generated token — another DucQ (or any DuckDB) on the same machine attaches to it and queries; the dialog binds localhost only, sharing beyond this machine needs the SQL editor (`CALL quack_serve('quack:<host>:<port>', allow_other_hostname => true)` — off-localhost quack serves HTTPS by default)
- Query remote backends over SQL: `ATTACH` for **Postgres, MySQL/MariaDB and SQLite files**, cloud storage & URLs via httpfs (`read_parquet('s3://bucket/data.parquet')`, `read_csv('https://…')`), Excel via `read_xlsx()`, lakehouse formats via **DuckLake** and **Delta Lake** (`ATTACH 'lake.db' AS lake (TYPE DUCKLAKE)` / `read_delta('s3://…')`); attached tables appear in the schema tree
- Full-text search (`PRAGMA create_fts_index` + `match_bm25`) and vector similarity (HNSW indexes) built in
- Multiple simultaneous connections, each with its own worker thread — switch via the selector next to Run or `Ctrl+Tab` (SQL-level `ATTACH` works too)
- Schema tree: tables / views / columns grouped by schema, name filter, double-click to generate a `SELECT`
- SQL editor: syntax highlighting, line numbers, find & replace (`Ctrl+F`), run the selection or the whole buffer with `Ctrl+Enter`
- **Query tabs**: several independent SQL buffers side by side (`Ctrl+T` new, `Ctrl+W` close, double-click to rename) — contents are restored across restarts, and closing a tab with unexecuted edits asks first
- Result table: virtualized rendering (first 1,000,000 rows by default, marked "truncated" beyond), click-to-sort, copy cell/row/selection as TSV
- Light / dark theme, following the system appearance (View → Theme)
- Query progress bar + cancel at any time (`duckdb_interrupt`)
- Drag in `csv / tsv / parquet / json` files to auto-create views and preview them
- Export results to CSV / Parquet (`COPY` streams server-side, unaffected by the display row limit)
- Query history with quick re-run
- UI in 9 languages, switchable at runtime from the menu: English, 简体中文, 日本語, 한국어, Deutsch, Français, Español, Português (Brasil), Русский

## Build

Requirements: CMake ≥ 3.24, Ninja, Qt ≥ 6.4 (Widgets + Test), a C++20 compiler. DuckDB v2.0.0 static libraries are downloaded automatically by CMake FetchContent (SHA256-verified) — nothing to install manually.

Two build modes:

- **Default (static)**: single binary, all bundled extensions statically registered, dynamic extension loading compiled out of the engine.
- **`-DDUCQ_DYNAMIC_EXTENSIONS=ON`**: the engine is built from source with `INSTALL`/`LOAD` enabled. Out-of-tree extensions are placed in an `extensions/` directory next to the executable and auto-loaded at startup, and any DuckDB extension installed into `~/.duckdb/extensions/` (including community builds — `allow_unsigned_extensions` is set) is picked up too. Still a single binary; extension failures at startup are skipped silently.

Quack (the client of DuckDB's client-server protocol) is on by default in both modes; `-DDUCQ_QUACK=OFF` builds a binary without it and without the server-connect UI. To try it end to end, serve from any DuckDB (CLI or DucQ's SQL editor): `CALL quack_serve('quack:localhost');` (token via `token => '…'`, see `CALL quack_serve()`'s output), then connect from File → Connect to DuckDB Server….

```bash
# macOS (Homebrew Qt)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/DucQ.app

# Linux
sudo apt install qt6-base-dev ninja-build cmake
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/DucQ

# Windows (MSVC + Qt via aqt/install-qt-action)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
.\build\DucQ.exe
```

Tests: `ctest --test-dir build --output-on-failure`

Three-platform CI: see [.github/workflows/build.yml](.github/workflows/build.yml).

## Technical notes

- **Threading model**: one worker `QThread` per database; queries are driven there through `duckdb_pending_prepared` + `duckdb_pending_execute_task`, and the UI talks to the query layer only via queued signal/slot. `duckdb_interrupt` provides cross-thread cancellation.
- **Result access**: execution finishes into a materialized result (in 2.0, `duckdb_pending_prepared` materializes by default). The `duckdb_result` is handed to the UI thread, which converts cells lazily with the `duckdb_value_*` accessors — safe for any internal vector layout, without ever touching raw flat pointers.
- **Static extensions**: DuckDB 2.0's statically linked extensions (parquet/json/icu/…) must be registered with `duckdb_register_static_extension(<ext>_describe)` before the first `duckdb_open()`; see `src/core/StaticExtensions.cpp`.
- **Remote databases & extra backends**: the `postgres_scanner`, `mysql_scanner`, `sqlite_scanner`, `httpfs`, `excel`, `fts`, `vss`, `ducklake`, `delta` and `quack` extensions are built from source (the prebuilt static-libs bundle doesn't ship them) and statically registered the same way. Dependency probing keeps builds green everywhere: `postgres_scanner` requires **libpq ≥ 18** (OAuth APIs), `excel` requires **minizip-ng**, `mysql_scanner` requires **libmariadb** (its pinned CMake only builds a loadable, so DucQ packs its objects into a static archive with a hand-written describe entry), and `delta` builds its Rust kernel via cargo (skipped gracefully when rustc is missing or broken). `iceberg` is built but not registered — its static init hangs the process at startup. `spatial` is skipped with GDAL ≥ 3.13 (its pinned code targets the older GDAL VSI API).
- **Large result sets**: materialized results stay in memory; add a `LIMIT` in SQL for very large data (exports go through `COPY`, which streams server-side and is unaffected).

## Roadmap

- [ ] Streaming display for huge result sets (server-side paging / automatic LIMIT wrapping)
- [ ] SQL autocompletion (the autocomplete extension is already statically linked)
- [ ] Built-in charts, result filtering (sorting is done)
- [ ] Packaged distribution (macOS notarization / Windows installer / AppImage)

To add or update a translation: run `lupdate src tests -ts translations/ducq_<lang>.ts`, fill in
`scripts/fill_translations.py` (or edit the `.ts` directly), then `lrelease`. The compiled `.qm`
files are embedded via `src/translations.qrc`.

## License

GPL-3.0 (Qt open-source edition). DuckDB is MIT-licensed; static linking is compatible.

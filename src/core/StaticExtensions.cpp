#include "StaticExtensions.h"

#include "duckdb_static_extension.h"

#include <mutex>

// Statically linked extension archives expose a describe entry point each.
// Referencing these symbols also keeps the extension objects pulled in by the
// linker, so no whole-archive tricks are needed.
extern "C" {
int32_t duckdb_extension_parquet_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_json_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_icu_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_core_functions_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_autocomplete_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_tpch_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_tpcds_describe(duckdb_extension_descriptor *);
// Remote-database scanners are built from source (see CMakeLists.txt). The
// upstream extension config skips postgres on MinGW, so guard the symbol.
int32_t duckdb_extension_sqlite_scanner_describe(duckdb_extension_descriptor *);
#if !defined(__MINGW32__) && !defined(__MINGW64__)
int32_t duckdb_extension_postgres_scanner_describe(duckdb_extension_descriptor *);
#endif
// httpfs (S3/GCS/Azure/HTTP), excel (read_xlsx), full-text and vector search.
int32_t duckdb_extension_httpfs_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_excel_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_fts_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_vss_describe(duckdb_extension_descriptor *);
// Lakehouse: DuckLake catalogs and Delta Lake tables. iceberg is built but
// not registered: its static init hangs the process on startup (see git log).
int32_t duckdb_extension_ducklake_describe(duckdb_extension_descriptor *);
int32_t duckdb_extension_delta_describe(duckdb_extension_descriptor *);
// mysql_scanner ships v1-style CMake (no static archive); CMakeLists packs
// its objects and hand-writes the describe entry.
int32_t duckdb_extension_mysql_scanner_describe(duckdb_extension_descriptor *);
}

namespace ducq {
namespace {

struct ExtensionEntry {
    const char *name;
    int32_t (*describe)(duckdb_extension_descriptor *);
};

const ExtensionEntry kExtensions[] = {
    {"parquet", duckdb_extension_parquet_describe},
    {"json", duckdb_extension_json_describe},
    {"icu", duckdb_extension_icu_describe},
    {"core_functions", duckdb_extension_core_functions_describe},
    {"autocomplete", duckdb_extension_autocomplete_describe},
    {"tpch", duckdb_extension_tpch_describe},
    {"tpcds", duckdb_extension_tpcds_describe},
    {"sqlite_scanner", duckdb_extension_sqlite_scanner_describe},
#if !defined(__MINGW32__) && !defined(__MINGW64__)
    {"postgres_scanner", duckdb_extension_postgres_scanner_describe},
#endif
    {"httpfs", duckdb_extension_httpfs_describe},
    {"excel", duckdb_extension_excel_describe},
    {"fts", duckdb_extension_fts_describe},
    {"vss", duckdb_extension_vss_describe},
    {"ducklake", duckdb_extension_ducklake_describe},
    {"delta", duckdb_extension_delta_describe},
    {"mysql_scanner", duckdb_extension_mysql_scanner_describe},
};

std::once_flag g_registered;

} // namespace

void registerStaticExtensions()
{
    std::call_once(g_registered, [] {
        for (const ExtensionEntry &entry : kExtensions)
            duckdb_register_static_extension(entry.describe);
    });
}

} // namespace ducq

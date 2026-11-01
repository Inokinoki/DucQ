#pragma once

namespace ducq {

// Registers DuckDB's statically linked core extensions (parquet, json, icu,
// core_functions, autocomplete, tpch, tpcds) with the engine. Must run before
// the first duckdb_open(); safe to call repeatedly.
void registerStaticExtensions();

} // namespace ducq

#pragma once

#include <QString>
#include <QStringList>

#include "duckdb.h"

namespace ducq {

// Loads DuckDB extensions that are shipped as dynamic libraries
// (*.duckdb_extension) next to the executable or in DuckDB's standard
// install directory.
//
// Only compiled in for the DUCQ_DYNAMIC_EXTENSIONS build: the prebuilt
// static bundle is built with DUCKDB_STATIC_BUILD, which compiles the
// INSTALL/LOAD machinery out of the engine entirely.
namespace ExtensionLoader {

// Directories that are scanned, in order.
QStringList searchPaths();

// Loads every extension found in searchPaths(). Returns the names that were
// loaded successfully; failures are skipped silently (a mismatched build or
// a broken file must not keep the app from starting).
QStringList loadAll(duckdb_database database);

} // namespace ExtensionLoader
} // namespace ducq

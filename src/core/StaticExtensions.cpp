#include "StaticExtensions.h"

#include "duckdb_static_extension.h"

#include <mutex>

// Statically linked extension archives expose a describe entry point each.
// Referencing these symbols also keeps the extension objects pulled in by the
// linker, so no whole-archive tricks are needed. Registration must happen
// before the first duckdb_open().
//
// duckdb_register_static_extensions() is the batch entry CMakeLists.txt
// generates (stubs/ducq_extension_loader.c, an OBJECT library linked into
// duckdb_bundle in both build modes); referencing it extracts the loader
// object with the describe functions of every extension statically linked
// into the binary. mysql_scanner is NOT in that loader (DucQ packs its
// archive itself) and is registered individually below.
//
// NOT registered here: iceberg — its static init hangs the process on
// startup (bisected; see git log). It is also absent from the engine's
// loader, and building it alone is inert.
extern "C" {
int32_t duckdb_register_static_extensions(void);
int32_t duckdb_extension_mysql_scanner_describe(duckdb_extension_descriptor *);
}

namespace ducq {
namespace {

struct ExtensionEntry {
    const char *name;
    int32_t (*describe)(duckdb_extension_descriptor *);
};

const ExtensionEntry kExtensions[] = {
    {"mysql_scanner", duckdb_extension_mysql_scanner_describe},
};

std::once_flag g_registered;

} // namespace

void registerStaticExtensions()
{
    std::call_once(g_registered, [] {
        // Registers every extension statically linked into the engine.
        duckdb_register_static_extensions();
        for (const ExtensionEntry &entry : kExtensions)
            duckdb_register_static_extension(entry.describe);
    });
}

} // namespace ducq

cmake_minimum_required(VERSION 3.24)

# The prebuilt DuckDB static libraries for Windows are built with the static
# CRT (/MT); match that so LNK2038 doesn't fire. Qt is loaded as DLLs and is
# unaffected.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(DucQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# DuckDB v2.0.0 prebuilt static libraries + headers
#
# Since DuckDB 2.0 the amalgamation no longer ships C++ sources; releases
# provide per-platform static archives plus a headers-only zip. Statically
# linked extensions (parquet/json/icu/...) must be registered explicitly via
# duckdb_register_static_extension() before the first duckdb_open() — see
# src/core/StaticExtensions.cpp.
# ---------------------------------------------------------------------------
set(DUCKDB_VERSION "v2.0.0")
set(DUCKDB_URL_BASE "https://github.com/duckdb/duckdb/releases/download/${DUCKDB_VERSION}")

if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
        set(DUCKDB_PLATFORM_ASSET "duckdb-static-libs-osx-arm64.tar.gz")
        set(DUCKDB_PLATFORM_HASH "c3a571e6d4a0da97534c631df463ab13c07f8cf78049625f5df936b7beaab9ce")
    else()
        set(DUCKDB_PLATFORM_ASSET "duckdb-static-libs-osx-amd64.tar.gz")
        set(DUCKDB_PLATFORM_HASH "7e6101ddc8de73e8dcc66f790e658e07891165d026992b40ceba20e50b01f8ec")
    endif()
elseif(WIN32 AND MINGW)
    set(DUCKDB_PLATFORM_ASSET "duckdb-static-libs-windows-mingw.tar.gz")
    set(DUCKDB_PLATFORM_HASH "b900706bcd5501b94c90b86a23b941a18fa1a73094d414f5ca7e7637cfd9ceea")
elseif(WIN32)
    set(DUCKDB_PLATFORM_ASSET "duckdb-static-libs-windows-amd64.tar.gz")
    set(DUCKDB_PLATFORM_HASH "9a7e2c1200209ea70cdf4535aa3fdf581c3a141137569462ed7b555cde49b112")
elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
    set(DUCKDB_PLATFORM_ASSET "duckdb-static-libs-linux-arm64.tar.gz")
    set(DUCKDB_PLATFORM_HASH "8171b4842e68a85b2e82b342d3bf7b542a88fd6666e0c18438d7b459012494ee")
elseif(UNIX)
    set(DUCKDB_PLATFORM_ASSET "duckdb-static-libs-linux-amd64.tar.gz")
    set(DUCKDB_PLATFORM_HASH "112d62c980f5447cd303a70b3dacbc09acd41ccb4ef6d154e8629f543a0f8568")
else()
    message(FATAL_ERROR "Unsupported platform for prebuilt DuckDB static libraries: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# Extension loading model
#
# OFF: engine + in-tree extensions come from the prebuilt static-libs bundle
#      (built with DUCKDB_STATIC_BUILD by DuckDB's CI); dynamic extension
#      loading is compiled out. Single binary, fully offline.
# ON:  engine is built from source WITHOUT DUCKDB_STATIC_BUILD and with all
#      DuckDB extensions statically linked into it — the same layout as the
#      official duckdb CLI. INSTALL/LOAD of dynamically loaded extensions
#      (e.g. MotherDuck) work, and DucQ auto-LOADs every *.duckdb_extension
#      found next to the executable. Still a single binary.
# ---------------------------------------------------------------------------
option(DUCQ_DYNAMIC_EXTENSIONS
       "Build the DuckDB engine from source with dynamic extension loading enabled"
       OFF)
# Quack turns DuckDB into a client-server database: ATTACH 'quack:host' +
# CONNECT routes a session to a remote duckdb process. Purely additive (its
# HTTP/TLS transport is httpfs, already linked), so it is on by default;
# -DDUCQ_QUACK=OFF builds a binary with no quack code and no server-connect UI.
option(DUCQ_QUACK
       "Include the Quack client-server extension (remote DuckDB server connections)"
       ON)

add_library(duckdb_bundle INTERFACE)
if(DUCQ_QUACK)
    target_compile_definitions(duckdb_bundle INTERFACE DUCQ_QUACK=1)
endif()

if(NOT DUCQ_DYNAMIC_EXTENSIONS)
FetchContent_Declare(duckdb_headers
    URL "${DUCKDB_URL_BASE}/libduckdb-src.zip"
    URL_HASH SHA256=d8fd3301aa01c2425475b153daf57d4a88d361eb09756575551a7a274da9cc16)
FetchContent_Declare(duckdb_libs
    URL "${DUCKDB_URL_BASE}/${DUCKDB_PLATFORM_ASSET}"
    URL_HASH SHA256=${DUCKDB_PLATFORM_HASH})
FetchContent_MakeAvailable(duckdb_headers duckdb_libs)

# Collect the archives we link: core + core extensions + httplib.
# (shell/loadable_extensions are for the CLI, not for embedding.)
file(GLOB DUCKDB_ARCHIVE_FILES LIST_DIRECTORIES false
    "${duckdb_libs_SOURCE_DIR}/*.a"
    "${duckdb_libs_SOURCE_DIR}/*.lib")
set(DUCKDB_LIB_NAMES
    duckdb_static
    parquet_extension
    json_extension
    icu_extension
    core_functions_extension
    autocomplete_extension
    tpcds_extension
    tpch_extension
    duckdb_httplib)
set(DUCKDB_LINK_LIBS "")
foreach(_name IN LISTS DUCKDB_LIB_NAMES)
    set(_found "")
    foreach(_file IN LISTS DUCKDB_ARCHIVE_FILES)
        get_filename_component(_fname "${_file}" NAME)
        if(_fname MATCHES "^(lib)?${_name}\\.(a|lib)$")
            set(_found "${_file}")
            break()
        endif()
    endforeach()
    if(NOT _found)
        message(FATAL_ERROR "DuckDB static library not found in ${duckdb_libs_SOURCE_DIR}: ${_name}")
    endif()
    list(APPEND DUCKDB_LINK_LIBS "${_found}")
endforeach()

target_include_directories(duckdb_bundle SYSTEM INTERFACE
    "${duckdb_headers_SOURCE_DIR}"
    "${duckdb_libs_SOURCE_DIR}")
# Link the static archives, not the (nonexistent) DLL import stubs.
target_compile_definitions(duckdb_bundle INTERFACE DUCKDB_STATIC_BUILD)
if(UNIX AND NOT APPLE)
    # GNU ld is single-pass: core <-> extension symbol cycles need a group.
    list(INSERT DUCKDB_LINK_LIBS 0 "-Wl,--start-group")
    list(APPEND DUCKDB_LINK_LIBS "-Wl,--end-group")
elseif(MSVC)
    # link.exe is single-pass too: repeat the list to resolve both directions.
    set(_duckdb_libs_copy "${DUCKDB_LINK_LIBS}")
    list(APPEND DUCKDB_LINK_LIBS "${_duckdb_libs_copy}")
endif()
target_link_libraries(duckdb_bundle INTERFACE ${DUCKDB_LINK_LIBS} Threads::Threads)
if(NOT WIN32)
    target_link_libraries(duckdb_bundle INTERFACE ${CMAKE_DL_LIBS} m)
else()
    # Restart Manager API used by DuckDB's file-lock diagnostics.
    target_link_libraries(duckdb_bundle INTERFACE Rstrtmgr.lib)
endif()
else() # DUCQ_DYNAMIC_EXTENSIONS
# Engine comes from the source build below; headers live in the source tree.
target_include_directories(duckdb_bundle SYSTEM INTERFACE
    "${CMAKE_BINARY_DIR}/_deps/duckdb_ext-src"
    "${CMAKE_BINARY_DIR}/_deps/duckdb_ext-src/src/include")
# No DUCKDB_STATIC_BUILD: that define is what compiles the INSTALL/LOAD
# machinery out. Extensions loaded at runtime resolve engine symbols from
# this executable, so it must export them.
target_compile_definitions(duckdb_bundle INTERFACE DUCQ_DYNAMIC_EXTENSIONS=1)
if(UNIX AND NOT APPLE)
    target_link_options(duckdb_bundle INTERFACE -Wl,--export-dynamic)
endif()
target_link_libraries(duckdb_bundle INTERFACE Threads::Threads)
if(NOT WIN32)
    target_link_libraries(duckdb_bundle INTERFACE ${CMAKE_DL_LIBS} m)
endif()
endif()

# ---------------------------------------------------------------------------
# Out-of-tree extensions built from source (the prebuilt static-libs bundle
# does not include them):
#
#   postgres_scanner — ATTACH Postgres (needs libpq >= 18, see probe below)
#   sqlite_scanner   — ATTACH SQLite files
#   httpfs           — read parquet/csv/json over HTTP(S), S3, R2, GCS, Azure
#   excel            — read_xlsx() for Excel files
#   fts / vss        — full-text search / vector similarity search
#   quack            — DuckDB's client-server protocol (only with DUCQ_QUACK)
#
# DuckDB's own root CMake fetches each repo at the pinned, v2.0.0-compatible
# commit via BUILD_EXTENSIONS; BUILD_EXTENSIONS_ONLY skips rebuilding the
# core (we link the prebuilt duckdb_static above, same tag => same ABI).
#
# mysql_scanner additionally needs an external libmariadb (vcpkg), so it is
# not built here. On MinGW the upstream config skips postgres, so it is
# optional there.
# ---------------------------------------------------------------------------
set(DUCKDB_OOTE_SOURCES
    postgres_scanner
    sqlite_scanner
    httpfs
    excel
    fts
    vss
    iceberg
    ducklake
    delta
    mysql_scanner
    spatial)
# Quack is fetched from the repo pinned in DuckDB's own
# .github/config/extensions/quack.cmake, exactly like httpfs/excel above.
if(DUCQ_QUACK)
    list(APPEND DUCKDB_OOTE_SOURCES quack)
endif()
# Dynamic mode also builds the three in-tree extensions that the prebuilt
# bundle ships but a source build must compile itself.
set(DUCKDB_EXT_SOURCES ${DUCKDB_OOTE_SOURCES})
list(APPEND DUCKDB_EXT_SOURCES autocomplete tpch tpcds)
# Dependency probes below remove skipped extensions from BOTH lists (static
# mode builds from DUCKDB_OOTE_SOURCES, dynamic from DUCKDB_EXT_SOURCES).
macro(ducq_skip_extension _name)
    list(REMOVE_ITEM DUCKDB_EXT_SOURCES "${_name}")
    list(REMOVE_ITEM DUCKDB_OOTE_SOURCES "${_name}")
endmacro()

# postgres_scanner needs libpq >= 18 (OAuth APIs, e.g. PGoauthBearerRequest).
# Detect the header and drop postgres gracefully when only an older libpq
# exists — sqlite_scanner has no external dependencies and always builds.
find_path(DUCQ_PQ_INCLUDE_DIR libpq-fe.h
    HINTS ${PostgreSQL_ROOT}/include /opt/homebrew/opt/libpq/include
    PATH_SUFFIXES postgresql)
if(DUCQ_PQ_INCLUDE_DIR)
    file(READ "${DUCQ_PQ_INCLUDE_DIR}/libpq-fe.h" _pq_header)
    if(NOT _pq_header MATCHES "PGoauthBearerRequest")
        message(STATUS "DucQ: libpq without OAuth APIs found — building without postgres_scanner (libpq >= 18 required)")
        ducq_skip_extension(postgres_scanner)
    else()
        # Seed the cache variables the postgres extension's own
        # find_package(PostgreSQL) consumes: a bare search can otherwise
        # resolve an older linked libpq without the OAuth APIs the probe just
        # verified, failing the compile in fresh build trees.
        find_library(DUCQ_PQ_LIBRARY NAMES pq HINTS "${DUCQ_PQ_INCLUDE_DIR}/../lib")
        if(DUCQ_PQ_LIBRARY)
            set(PostgreSQL_INCLUDE_DIR "${DUCQ_PQ_INCLUDE_DIR}" CACHE PATH
                "libpq >= 18 headers for postgres_scanner" FORCE)
            set(PostgreSQL_LIBRARY "${DUCQ_PQ_LIBRARY}" CACHE FILEPATH
                "libpq >= 18 import for postgres_scanner" FORCE)
        endif()
    endif()
    unset(_pq_header)
else()
    message(STATUS "DucQ: libpq not found — building without postgres_scanner")
    ducq_skip_extension(postgres_scanner)
endif()

# excel needs minizip-ng; drop it gracefully when unavailable.
find_package(minizip-ng QUIET CONFIG)
if(NOT minizip-ng_FOUND)
    message(STATUS "DucQ: minizip-ng not found — building without excel (brew install minizip-ng / vcpkg minizip-ng)")
    ducq_skip_extension(excel)
endif()

# spatial needs GDAL+PROJ plus the vcpkg-only unofficial-sqlite3 package;
# synthesize the latter from the system sqlite3 when available.
find_package(GDAL QUIET CONFIG)
find_package(PROJ QUIET CONFIG)
find_path(SQLITE3_INCLUDE_DIR sqlite3.h
    HINTS /opt/homebrew/opt/sqlite/include /usr/include /usr/local/include)
find_library(SQLITE3_LIBRARY sqlite3
    HINTS /opt/homebrew/opt/sqlite/lib /usr/lib /usr/local/lib)
if(GDAL_FOUND AND PROJ_FOUND AND SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
    # The pinned spatial commit targets the vcpkg GDAL 3.8/3.9 VSIVirtualHandle
    # API; GDAL >= 3.13 changed those signatures (Read/Write overrides no
    # longer match), and patching them is out of scope. Keep the probe so a
    # compatible GDAL builds spatial automatically; otherwise skip it.
    set(SPATIAL_GDAL_COMPATIBLE OFF)
    if(GDAL_VERSION AND GDAL_VERSION VERSION_LESS "3.13")
        set(SPATIAL_GDAL_COMPATIBLE ON)
    endif()
    if(SPATIAL_GDAL_COMPATIBLE)
        file(WRITE "${CMAKE_BINARY_DIR}/stubs/unofficial-sqlite3/unofficial-sqlite3Config.cmake" "
if(NOT TARGET unofficial::sqlite3::sqlite3)
    add_library(unofficial::sqlite3::sqlite3 UNKNOWN IMPORTED)
    set_target_properties(unofficial::sqlite3::sqlite3 PROPERTIES
        IMPORTED_LOCATION \"${SQLITE3_LIBRARY}\"
        INTERFACE_INCLUDE_DIRECTORIES \"${SQLITE3_INCLUDE_DIR}\")
endif()
if(NOT TARGET unofficial::sqlite3)
    add_library(unofficial::sqlite3 INTERFACE IMPORTED)
    target_link_libraries(unofficial::sqlite3 INTERFACE unofficial::sqlite3::sqlite3)
endif()
")
        list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/stubs/unofficial-sqlite3")
        message(STATUS "DucQ: GDAL ${GDAL_VERSION} compatible — enabling spatial")
    else()
        message(STATUS "DucQ: GDAL ${GDAL_VERSION} incompatible with pinned spatial (needs < 3.13) — skipping spatial")
        ducq_skip_extension(spatial)
    endif()
else()
    message(STATUS "DucQ: GDAL/PROJ/sqlite3 incomplete — building without spatial (brew install gdal)")
    ducq_skip_extension(spatial)
endif()

# mysql_scanner discovers libmariadb through the vcpkg-only
# unofficial-libmariadb package. Probe a system libmariadb (the extension
# includes plain "mysql.h": Homebrew keeps it in include/mariadb/) and
# synthesize that config so the upstream find_package chain resolves; skip
# mysql when absent.
find_path(MARIADB_INCLUDE_DIR mysql.h
    HINTS /opt/homebrew/opt/mariadb-connector-c/include/mariadb
    PATHS /usr/include/mariadb /usr/local/include/mariadb
          /usr/include/mysql /usr/local/include/mysql /opt/homebrew/include/mysql)
find_library(MARIADB_LIBRARY mariadb
    HINTS /opt/homebrew/opt/mariadb-connector-c/lib /usr/lib /usr/local/lib)
if(MARIADB_INCLUDE_DIR AND MARIADB_LIBRARY)
    # mysql_catalog.cpp / mysql_schema_entry.cpp define un-namespaced free
    # helpers (GetSecret, CatalogTypeIsSupported) that postgres' copies of the
    # same code also define — fine for separate loadables, a collision when
    # statically co-linked. Rename them; definitions and calls are file-local.
    foreach(_srcfile mysql_catalog.cpp mysql_schema_entry.cpp)
        set(_patch_target "${CMAKE_BINARY_DIR}/_deps/mysql_scanner_extension_fc-src/src/storage/${_srcfile}")
        if(EXISTS "${_patch_target}")
            file(READ "${_patch_target}" _patch_content)
            string(REPLACE "GetSecret(" "MySQLGetSecret(" _patch_content "${_patch_content}")
            string(REPLACE "CatalogTypeIsSupported(" "MySQLCatalogTypeIsSupported("
                           _patch_content "${_patch_content}")
            file(WRITE "${_patch_target}" "${_patch_content}")
        endif()
    endforeach()
    # mysql_scanner hardcodes include_directories(<build>/vcpkg_installed/<triplet>/include/mysql);
    # outside vcpkg the triplet is empty, so materialize the headers there.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/vcpkg_installed/include/mysql")
    file(COPY ${MARIADB_INCLUDE_DIR}/ DESTINATION "${CMAKE_BINARY_DIR}/vcpkg_installed/include/mysql")
    file(WRITE "${CMAKE_BINARY_DIR}/stubs/unofficial-libmariadb/unofficial-libmariadbConfig.cmake" "
if(NOT TARGET unofficial::libmariadb)
    add_library(unofficial::libmariadb UNKNOWN IMPORTED)
    set_target_properties(unofficial::libmariadb PROPERTIES
        IMPORTED_LOCATION \"${MARIADB_LIBRARY}\"
        INTERFACE_INCLUDE_DIRECTORIES \"${MARIADB_INCLUDE_DIR}\")
endif()
")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/stubs/unofficial-libmariadb")
    message(STATUS "DucQ: libmariadb found — enabling mysql_scanner (${MARIADB_LIBRARY})")
else()
    message(STATUS "DucQ: libmariadb not found — building without mysql_scanner")
    ducq_skip_extension(mysql_scanner)
endif()

# delta builds its Rust kernel via cargo; probe rustc like the other external
# dependencies above. An absent or broken toolchain (e.g. a package-manager
# rust/llvm ABI mismatch after an upgrade, which aborts every rustc run)
# drops delta gracefully instead of failing the whole build.
execute_process(COMMAND rustc --version RESULT_VARIABLE DUCQ_RUSTC_RESULT
                OUTPUT_VARIABLE DUCQ_RUSTC_VERSION OUTPUT_QUIET ERROR_QUIET)
if(NOT DUCQ_RUSTC_RESULT STREQUAL "0")
    message(STATUS "DucQ: rustc not usable — building without delta (cargo required)")
    ducq_skip_extension(delta)
endif()
unset(DUCQ_RUSTC_VERSION)
FetchContent_Declare(duckdb_ext
    GIT_REPOSITORY "https://github.com/duckdb/duckdb.git"
    GIT_TAG "v2.0.0"
    GIT_SHALLOW TRUE)
# In dynamic mode everything but mysql_scanner is statically linked into the
# engine by DuckDB's own build; static mode builds extensions only and links
# the prebuilt engine archive.
# Only the in-tree extensions produce static archives in this mode; every
# out-of-tree extension exists as a loadable and is discovered + LOADed at
# runtime by core/ExtensionLoader.
set(DUCQ_ENGINE_STATIC_LINK "core_functions parquet json icu autocomplete tpch tpcds")
if(DUCQ_DYNAMIC_EXTENSIONS)
    set(BUILD_EXTENSIONS_ONLY OFF CACHE BOOL "" FORCE)
    set(BUILD_MAIN_DUCKDB_LIBRARY ON CACHE BOOL "" FORCE)
    # delta's Rust kernel build is flaky outside its primed build directory;
    # the dynamic build skips it (the static build ships it).
    set(DYN_EXTS "${DUCKDB_EXT_SOURCES}")
    list(REMOVE_ITEM DYN_EXTS delta)
    set(BUILD_EXTENSIONS "${DYN_EXTS}" CACHE STRING "" FORCE)
    set(STATICALLY_LINK_EXTENSIONS "${DUCQ_ENGINE_STATIC_LINK}" CACHE STRING "" FORCE)
    # The engine's own static-extension loader is added to its SHARED target
    # only; link our own generated equivalent into the executable, so the
    # statically linked extensions get registered before duckdb_open().
    # loadable_extensions is DuckDB's pseudo-extension carrying the INSTALL/
    # LOAD machinery; registering it wires the dynamic provider per database.
    set(DUCQ_LOADER_EXTS loadable_extensions core_functions parquet json icu autocomplete tpch tpcds)
    if(NOT MINGW)
        list(INSERT DUCQ_LOADER_EXTS 4 postgres_scanner)
    endif()
    set(DUCQ_LOADER_INCLUDE_DIRS
        "${CMAKE_BINARY_DIR}/_deps/duckdb_ext-src"
        "${CMAKE_BINARY_DIR}/_deps/duckdb_ext-src/src/include")
    # The describe symbols live in the extensions' own static archives.
    # Out-of-tree extensions only exist as loadables in this mode; they are
    # discovered and LOADed at runtime by core/ExtensionLoader from the
    # "extensions" directory next to the executable. Install them there.
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif()
else()
    set(BUILD_EXTENSIONS_ONLY ON CACHE BOOL "" FORCE)
    set(BUILD_EXTENSIONS "${DUCKDB_OOTE_SOURCES}" CACHE STRING "" FORCE)
    set(BUILD_MAIN_DUCKDB_LIBRARY OFF CACHE BOOL "" FORCE)
    # The prebuilt engine archive provides only the singular
    # duckdb_register_static_extension, so DucQ generates the batch loader
    # itself (the same shape DuckDB's source build produces for the engine).
    # Register the bundle's core extensions plus every out-of-tree extension
    # actually built. iceberg stays unregistered — its static init hangs the
    # process on startup (bisected; see git log) — and mysql_scanner is
    # registered from core/StaticExtensions.cpp (DucQ packs its archive
    # itself; see the mysql block below).
    set(DUCQ_LOADER_EXTS parquet json icu core_functions autocomplete tpch tpcds)
    foreach(_ext IN LISTS DUCKDB_OOTE_SOURCES)
        if(_ext STREQUAL "iceberg" OR _ext STREQUAL "mysql_scanner")
            continue()
        endif()
        list(APPEND DUCQ_LOADER_EXTS "${_ext}")
    endforeach()
    set(DUCQ_LOADER_INCLUDE_DIRS
        "${duckdb_headers_SOURCE_DIR}"
        "${duckdb_libs_SOURCE_DIR}")
endif()
set(BUILD_SHELL OFF CACHE BOOL "" FORCE)
set(BUILD_UNITTESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SANITIZER OFF CACHE BOOL "" FORCE)
set(ENABLE_UBSAN OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
# Whether the packed mysql archive excludes its vendored database-connector
# code: in static mode postgres' archive (linked alongside) already carries
# it; in dynamic mode the engine does NOT contain postgres' connector, so
# mysql needs its own.
if(DUCQ_DYNAMIC_EXTENSIONS)
    set(DUCQ_PACK_EXCLUDE_DBCONNECTOR "OFF")
else()
    set(DUCQ_PACK_EXCLUDE_DBCONNECTOR "ON")
endif()

if(NOT DUCQ_DYNAMIC_EXTENSIONS)
# The extension build links its loadable artifacts against a duckdb_static
# target, which BUILD_EXTENSIONS_ONLY does not create. Alias it to the
# prebuilt archive (same v2.0.0 tag, so the symbols all resolve).
if(NOT TARGET duckdb_static)
    add_library(duckdb_static STATIC IMPORTED GLOBAL)
    set_target_properties(duckdb_static PROPERTIES
        IMPORTED_LOCATION "${duckdb_libs_SOURCE_DIR}/libduckdb_static.a")
endif()
endif()
FetchContent_MakeAvailable(duckdb_ext)

# The pinned quack commit predates OpenSSL 4's constification of
# X509_get_subject_name (returns const X509_NAME* from 4.0 on, X509_NAME*
# before); a const_cast keeps both toolchains building. Replace is idempotent,
# so re-configure runs stay clean. (Same pattern as the mysql source patch
# below.) The extension source exists only once DuckDB's own configure fetched
# it, hence the placement after FetchContent_MakeAvailable.
set(_quack_keygen "${CMAKE_BINARY_DIR}/_deps/quack_extension_fc-src/src/quack_ssl_key_generator.cpp")
if(DUCQ_QUACK AND EXISTS "${_quack_keygen}")
    file(READ "${_quack_keygen}" _quack_content)
    string(REPLACE "X509_name_st *name = X509_get_subject_name(cert.get());"
                   "X509_NAME *name = const_cast<X509_NAME *>(X509_get_subject_name(cert.get()));"
                   _quack_content "${_quack_content}")
    file(WRITE "${_quack_keygen}" "${_quack_content}")
    unset(_quack_content)
endif()
unset(_quack_keygen)

# ---------------------------------------------------------------------------
# Generated static-extension loader (both build modes).
#
# Defines duckdb_register_static_extensions(), which core/StaticExtensions.cpp
# calls before the first duckdb_open(): static mode has no such batch entry
# (the prebuilt bundle ships only the singular register function), and dynamic
# mode's own loader is wired into DuckDB's shared target only. Referencing the
# per-extension describe symbols also extracts their archive members —
# unreferenced archive members are never pulled in by the linker. Entries whose
# upstream build was skipped for this platform (e.g. postgres on MinGW) are
# dropped so no describe symbol is referenced that doesn't exist. Compiled as
# an OBJECT library so every toolchain (MSVC included) can build it.
# ---------------------------------------------------------------------------
set(_decls "")
set(_regs "")
foreach(_ext IN LISTS DUCQ_LOADER_EXTS)
    if(_ext IN_LIST DUCKDB_OOTE_SOURCES AND NOT TARGET ${_ext}_extension)
        message(STATUS "DucQ: extension ${_ext} not built — left out of the static loader")
        continue()
    endif()
    string(APPEND _decls "int32_t duckdb_extension_${_ext}_describe(duckdb_extension_descriptor *descriptor);\n")
    string(APPEND _regs "\tif (duckdb_register_static_extension(duckdb_extension_${_ext}_describe) != 0) {\n\t\tresult = 1;\n\t}\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/stubs/ducq_extension_loader.c" "
// Generated by DucQ's CMakeLists.txt: registers the extensions statically
// linked into the binary. Call duckdb_register_static_extensions() before
// the first duckdb_open().
#include \"duckdb_static_extension.h\"
#ifdef __cplusplus
extern \"C\" {
#endif
${_decls}
int32_t duckdb_register_static_extensions(void) {
	int32_t result = 0;
${_regs}
	return result;
}
#ifdef __cplusplus
}
#endif
")
add_library(ducq_extension_loader OBJECT EXCLUDE_FROM_ALL
    "${CMAKE_BINARY_DIR}/stubs/ducq_extension_loader.c")
target_include_directories(ducq_extension_loader PRIVATE ${DUCQ_LOADER_INCLUDE_DIRS})
# $<TARGET_OBJECTS> (not the target name): objects only propagate one link
# hop, and duckdb_bundle is an INTERFACE library the executables link through.
target_link_libraries(duckdb_bundle INTERFACE "$<TARGET_OBJECTS:ducq_extension_loader>")

if(DUCQ_DYNAMIC_EXTENSIONS)
    # The engine built above (with extension loading enabled) is linked
    # statically, so the app stays a single binary.
    target_link_libraries(duckdb_bundle INTERFACE duckdb_static)
    # The INSTALL/LOAD machinery is its own archive, linked only into the
    # engine's shared target by DuckDB's build; the executable needs it too.
    if(TARGET duckdb_loadable_extensions)
        target_link_libraries(duckdb_bundle INTERFACE duckdb_loadable_extensions)
    endif()
    # In-tree extensions have static archives in this mode; their describe
    # symbols are needed by our generated loader.
    foreach(_ext IN LISTS DUCQ_LOADER_EXTS)
        if(TARGET ${_ext}_extension)
            get_target_property(_atype ${_ext}_extension TYPE)
            if(_atype STREQUAL "STATIC_LIBRARY")
                target_link_libraries(duckdb_bundle INTERFACE ${_ext}_extension)
            endif()
        endif()
    endforeach()
endif()

# Link the extension targets directly — output paths and build order come
# from CMake. MinGW's upstream config skips postgres, so check per target.
foreach(_name IN LISTS DUCKDB_EXT_SOURCES)
    if(DUCQ_DYNAMIC_EXTENSIONS)
        # Every extension except mysql is statically linked into the engine by
        # DuckDB's build; only their PRIVATE external deps and the mysql pack
        # need handling here.
        if(_name STREQUAL "mysql_scanner")
            if(NOT TARGET mysql_scanner_loadable_extension)
                message(STATUS "DucQ: remote-db extension ${_name} not built (skipped)")
                continue()
            endif()
            file(GLOB_RECURSE MYSQL_OBJS_ALL
                "${duckdb_ext_BINARY_DIR}/extension/mysql_scanner/*.o")
            set(MYSQL_OBJS "")
            foreach(_o IN LISTS MYSQL_OBJS_ALL)
                if(NOT _o MATCHES "/dbconnector/")
                    list(APPEND MYSQL_OBJS "${_o}")
                endif()
            endforeach()
            set(MYSQL_PACK "${CMAKE_BINARY_DIR}/stubs/libmysql_scanner_extension.a")
            file(WRITE "${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.c" "
// Hand-written for the mysql_scanner static pack (see CMakeLists.txt).
#include \"duckdb_static_extension.h\"

void mysql_scanner_duckdb_cpp_init(void *loader);

#ifdef __cplusplus
extern \"C\" {
#endif

int32_t duckdb_extension_mysql_scanner_describe(duckdb_extension_descriptor *descriptor) {
	if (descriptor->version < 1) {
		descriptor->set_error(descriptor, \"extension mysql_scanner needs descriptor layout 1\");
		return 1;
	}
	descriptor->version = 1;
	descriptor->name = \"mysql_scanner\";
	descriptor->extension_version = \"f7fc0d4271\";
	descriptor->api_version = \"d54cbbe3df\";
	descriptor->entry_cpp = (void (*)(void))mysql_scanner_duckdb_cpp_init;
	return 0;
}

#ifdef __cplusplus
}
#endif
")
            add_custom_command(OUTPUT "${MYSQL_PACK}"
                COMMAND /usr/bin/cc -c "${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.c"
                        -o "${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.o"
                        -I"${duckdb_ext_SOURCE_DIR}" -I"${duckdb_ext_SOURCE_DIR}/src/include"
                COMMAND ${CMAKE_COMMAND}
                        -DMYSQL_SRC_DIR="${duckdb_ext_BINARY_DIR}/extension/mysql_scanner"
                        -DDESCRIBE_OBJ="${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.o"
                        -DMYSQL_PACK="${MYSQL_PACK}"
                        -DEXCLUDE_DBCONNECTOR="${DUCQ_PACK_EXCLUDE_DBCONNECTOR}"
                        -P "${CMAKE_CURRENT_LIST_DIR}/scripts/pack_mysql.cmake"
                DEPENDS mysql_scanner_loadable_extension)
            add_custom_target(mysql_scanner_pack DEPENDS "${MYSQL_PACK}")
            target_link_libraries(duckdb_bundle INTERFACE "${MYSQL_PACK}" "${MARIADB_LIBRARY}")
        elseif(_name STREQUAL "postgres_scanner")
            find_package(OpenSSL QUIET)
            if(OpenSSL_FOUND)
                target_link_libraries(duckdb_bundle INTERFACE ${PostgreSQL_LIBRARY}
                    OpenSSL::SSL OpenSSL::Crypto)
            else()
                target_link_libraries(duckdb_bundle INTERFACE ${PostgreSQL_LIBRARY} ssl crypto)
            endif()
        elseif(_name STREQUAL "excel")
            find_package(EXPAT QUIET)
            find_package(ZLIB QUIET)
            target_link_libraries(duckdb_bundle INTERFACE MINIZIP::minizip-ng)
            if(TARGET EXPAT::EXPAT)
                target_link_libraries(duckdb_bundle INTERFACE EXPAT::EXPAT)
            endif()
            if(TARGET ZLIB::ZLIB)
                target_link_libraries(duckdb_bundle INTERFACE ZLIB::ZLIB)
            endif()
            get_target_property(_mz_inc MINIZIP::minizip-ng INTERFACE_INCLUDE_DIRECTORIES)
            if(_mz_inc)
                list(GET _mz_inc 0 _mz_inc0)
                get_filename_component(_mz_parent "${_mz_inc0}/.." ABSOLUTE)
                foreach(_t excel_extension excel_loadable_extension)
                    if(TARGET ${_t})
                        target_include_directories(${_t} SYSTEM PUBLIC "${_mz_parent}")
                    endif()
                endforeach()
            endif()
        elseif(_name STREQUAL "httpfs")
            find_package(OpenSSL QUIET)
            find_package(CURL QUIET)
            if(OpenSSL_FOUND)
                target_link_libraries(duckdb_bundle INTERFACE OpenSSL::SSL OpenSSL::Crypto)
            else()
                target_link_libraries(duckdb_bundle INTERFACE ssl crypto)
            endif()
            if(TARGET CURL::libcurl)
                target_link_libraries(duckdb_bundle INTERFACE CURL::libcurl)
            endif()
        elseif(_name STREQUAL "iceberg" OR _name STREQUAL "ducklake")
            find_package(roaring QUIET CONFIG)
            if(roaring_FOUND)
                target_link_libraries(duckdb_bundle INTERFACE roaring::roaring)
            endif()
        elseif(_name STREQUAL "delta")
            file(GLOB DELTA_RUST
                "${CMAKE_BINARY_DIR}/rust/src/delta_kernel/target/*/release/libdelta_kernel_ffi.a")
            if(DELTA_RUST)
                target_link_libraries(duckdb_bundle INTERFACE ${DELTA_RUST})
            endif()
        endif()
        continue()
    endif()
    # mysql_scanner never produces a <name>_extension target (v1-style CMake
    # only builds the loadable); its packed archive is handled below via
    # mysql_scanner_pack.
    if(_name STREQUAL "mysql_scanner" AND NOT TARGET mysql_scanner_loadable_extension)
        message(STATUS "DucQ: remote-db extension ${_name} not built (skipped)")
        continue()
    endif()
    if(TARGET ${_name}_extension OR _name STREQUAL "mysql_scanner")
        message(STATUS "DucQ: linking remote-db extension ${_name}")
        # mysql gets a bespoke packed archive below; everything else links as
        # built (the mysql source rename removes the only static-collision).
        if(NOT _name STREQUAL "mysql_scanner")
            target_link_libraries(duckdb_bundle INTERFACE ${_name}_extension)
        endif()
        if(_name STREQUAL "mysql_scanner")
            # This pinned mysql_scanner has v1-style CMake: it only builds the
            # loadable .duckdb_extension, never a static archive, so static
            # registration is impossible out of the box. Pack its objects into
            # an archive ourselves and hand-write the describe entry (the same
            # shape duckdb's own codegen produces for the other extensions).
            # The dbconnector objects are excluded: postgres vendors the same
            # database-connector code, already inside the engine.
            set(MYSQL_PACK "${CMAKE_BINARY_DIR}/stubs/libmysql_scanner_extension.a")
            file(WRITE "${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.c" "
// Hand-written for the mysql_scanner static pack (see CMakeLists.txt).
#include \"duckdb_static_extension.h\"

void mysql_scanner_duckdb_cpp_init(void *loader);

#ifdef __cplusplus
extern \"C\" {
#endif

int32_t duckdb_extension_mysql_scanner_describe(duckdb_extension_descriptor *descriptor) {
	if (descriptor->version < 1) {
		descriptor->set_error(descriptor, \"extension mysql_scanner needs descriptor layout 1\");
		return 1;
	}
	descriptor->version = 1;
	descriptor->name = \"mysql_scanner\";
	descriptor->extension_version = \"f7fc0d4271\";
	descriptor->api_version = \"d54cbbe3df\";
	descriptor->entry_cpp = (void (*)(void))mysql_scanner_duckdb_cpp_init;
	return 0;
}

#ifdef __cplusplus
}
#endif
")
            add_custom_command(OUTPUT "${MYSQL_PACK}"
                COMMAND /usr/bin/cc -c "${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.c"
                        -o "${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.o"
                        -I"${duckdb_headers_SOURCE_DIR}" -I"${duckdb_libs_SOURCE_DIR}"
                COMMAND ${CMAKE_COMMAND}
                        -DMYSQL_SRC_DIR="${duckdb_ext_BINARY_DIR}/extension/mysql_scanner"
                        -DDESCRIBE_OBJ="${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.o"
                        -DMYSQL_PACK="${MYSQL_PACK}"
                        -DEXCLUDE_DBCONNECTOR="${DUCQ_PACK_EXCLUDE_DBCONNECTOR}"
                        -P "${CMAKE_CURRENT_LIST_DIR}/scripts/pack_mysql.cmake"
                DEPENDS mysql_scanner_loadable_extension)
            add_custom_target(mysql_scanner_pack DEPENDS "${MYSQL_PACK}")
            target_link_libraries(duckdb_bundle INTERFACE "${MYSQL_PACK}" "${MARIADB_LIBRARY}")
        elseif(_name STREQUAL "postgres_scanner")
            # The scanner links libpq/OpenSSL PRIVATEly, so the final binary
            # must pick them up itself.
            find_package(OpenSSL QUIET)
            if(OpenSSL_FOUND)
                target_link_libraries(duckdb_bundle INTERFACE ${PostgreSQL_LIBRARY}
                    OpenSSL::SSL OpenSSL::Crypto)
            else()
                target_link_libraries(duckdb_bundle INTERFACE ${PostgreSQL_LIBRARY} ssl crypto)
            endif()
        elseif(_name STREQUAL "excel")
            # excel's code includes "minizip-ng/mz.h", expecting the vcpkg
            # layout where the package exposes <prefix>/include. Homebrew's
            # config exposes <prefix>/include/minizip-ng itself, so add the
            # parent directory to make the prefixed includes resolve — for
            # both the static archive and the loadable targets.
            get_target_property(_mz_inc MINIZIP::minizip-ng INTERFACE_INCLUDE_DIRECTORIES)
            if(_mz_inc)
                list(GET _mz_inc 0 _mz_inc0)
                get_filename_component(_mz_parent "${_mz_inc0}/.." ABSOLUTE)
                foreach(_t excel_extension excel_loadable_extension)
                    if(TARGET ${_t})
                        target_include_directories(${_t} SYSTEM PUBLIC "${_mz_parent}")
                    endif()
                endforeach()
            endif()
        elseif(_name STREQUAL "httpfs")
            # Same PRIVATE-linking situation for its TLS backend.
            find_package(OpenSSL QUIET)
            if(OpenSSL_FOUND)
                target_link_libraries(duckdb_bundle INTERFACE OpenSSL::SSL OpenSSL::Crypto)
            else()
                target_link_libraries(duckdb_bundle INTERFACE ssl crypto)
            endif()
        elseif(_name STREQUAL "quack")
            # Quack rides on httpfs' HTTP/TLS stack; same PRIVATE-linking
            # situation for its TLS backend.
            find_package(OpenSSL QUIET)
            if(OpenSSL_FOUND)
                target_link_libraries(duckdb_bundle INTERFACE OpenSSL::SSL OpenSSL::Crypto)
            else()
                target_link_libraries(duckdb_bundle INTERFACE ssl crypto)
            endif()
        endif()
    else()
        message(STATUS "DucQ: remote-db extension ${_name} not built (skipped)")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# DucQ
# ---------------------------------------------------------------------------
set(DUCQ_CORE_SOURCES
    src/core/StaticExtensions.cpp
    src/core/AppSettings.cpp
    src/core/Database.cpp
    src/core/DbSession.cpp
    src/core/ExtensionLoader.cpp
    src/core/HistoryStore.cpp
    src/core/QuackTargets.cpp
    src/core/ResultCache.cpp
    src/core/QueryRunner.cpp
    src/core/Catalog.cpp)

add_executable(DucQ WIN32 MACOSX_BUNDLE
    ${DUCQ_CORE_SOURCES}
    src/main.cpp
    src/translations.qrc
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/SchemaTree.cpp src/ui/SchemaTree.h
    src/ui/SqlEditor.cpp src/ui/SqlEditor.h
    src/ui/Theme.cpp src/ui/Theme.h
    src/ui/ResultModel.cpp src/ui/ResultModel.h
    src/ui/ResultTabs.cpp src/ui/ResultTabs.h
    src/ui/DropImport.cpp src/ui/DropImport.h
    src/export/Exporter.cpp src/export/Exporter.h)
target_include_directories(DucQ PRIVATE src)
target_link_libraries(DucQ PRIVATE Qt6::Widgets duckdb_bundle)
if(DUCQ_QUACK)
    # The server-connect dialog only exists in quack builds; every reference
    # to it in MainWindow.cpp is behind the same ifdef.
    target_sources(DucQ PRIVATE
        src/ui/QuackConnectDialog.cpp src/ui/QuackConnectDialog.h)
endif()
if(MSVC)
    target_compile_options(DucQ PRIVATE /W4 /bigobj /permissive- /utf-8)
else()
    target_compile_options(DucQ PRIVATE -Wall -Wextra)
endif()
set_target_properties(DucQ PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "cc.inoki.DucQ")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(test_core
    ${DUCQ_CORE_SOURCES}
    tests/test_core.cpp)
target_include_directories(test_core PRIVATE src)
target_link_libraries(test_core PRIVATE Qt6::Test duckdb_bundle)
if(MSVC)
    target_compile_options(test_core PRIVATE /W4 /bigobj /permissive- /utf-8)
else()
    target_compile_options(test_core PRIVATE -Wall -Wextra)
endif()
add_test(NAME test_core COMMAND test_core)

if(DUCQ_DYNAMIC_EXTENSIONS)
    # Install the out-of-tree loadables next to the executable, where
    # core/ExtensionLoader finds and LOADs them at startup.
    add_custom_target(ducq_collect_loadables ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:DucQ>/extensions"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${duckdb_ext_BINARY_DIR}/extension"
                "$<TARGET_FILE_DIR:DucQ>/extensions"
        COMMENT "Collecting *.duckdb_extension loadables"
        DEPENDS DucQ)
    # Same for the test binary, so extension-dependent tests (the quack
    # loopback) exercise the real load path there instead of skipping.
    add_custom_target(ducq_collect_loadables_test ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:test_core>/extensions"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${duckdb_ext_BINARY_DIR}/extension"
                "$<TARGET_FILE_DIR:test_core>/extensions"
        COMMENT "Collecting *.duckdb_extension loadables for tests"
        DEPENDS test_core)
endif()

if(TARGET mysql_scanner_pack)
    # The packed mysql archive is produced by a custom command; make the
    # final binaries wait for it.
    add_dependencies(DucQ mysql_scanner_pack)
    add_dependencies(test_core mysql_scanner_pack)
endif()
if(TARGET ducq_extension_loader)
    # The generated extension loader object (dynamic mode) must exist before
    # the binaries link.
    add_dependencies(DucQ ducq_extension_loader)
    add_dependencies(test_core ducq_extension_loader)
endif()

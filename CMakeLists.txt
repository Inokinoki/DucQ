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

add_library(duckdb_bundle INTERFACE)
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

# ---------------------------------------------------------------------------
# Out-of-tree extensions built from source (the prebuilt static-libs bundle
# does not include them):
#
#   postgres_scanner — ATTACH Postgres (needs libpq >= 18, see probe below)
#   sqlite_scanner   — ATTACH SQLite files
#   httpfs           — read parquet/csv/json over HTTP(S), S3, R2, GCS, Azure
#   excel            — read_xlsx() for Excel files
#   fts / vss        — full-text search / vector similarity search
#
# DuckDB's own root CMake fetches each repo at the pinned, v2.0.0-compatible
# commit via BUILD_EXTENSIONS; BUILD_EXTENSIONS_ONLY skips rebuilding the
# core (we link the prebuilt duckdb_static above, same tag => same ABI).
#
# mysql_scanner additionally needs an external libmariadb (vcpkg), so it is
# not built here. On MinGW the upstream config skips postgres, so it is
# optional there.
# ---------------------------------------------------------------------------
set(DUCKDB_EXT_SOURCES
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
        list(REMOVE_ITEM DUCKDB_EXT_SOURCES postgres_scanner)
    endif()
    unset(_pq_header)
else()
    message(STATUS "DucQ: libpq not found — building without postgres_scanner")
    list(REMOVE_ITEM DUCKDB_EXT_SOURCES postgres_scanner)
endif()

# excel needs minizip-ng; drop it gracefully when unavailable.
find_package(minizip-ng QUIET CONFIG)
if(NOT minizip-ng_FOUND)
    message(STATUS "DucQ: minizip-ng not found — building without excel (brew install minizip-ng / vcpkg minizip-ng)")
    list(REMOVE_ITEM DUCKDB_EXT_SOURCES excel)
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
        list(REMOVE_ITEM DUCKDB_EXT_SOURCES spatial)
    endif()
else()
    message(STATUS "DucQ: GDAL/PROJ/sqlite3 incomplete — building without spatial (brew install gdal)")
    list(REMOVE_ITEM DUCKDB_EXT_SOURCES spatial)
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
    list(REMOVE_ITEM DUCKDB_EXT_SOURCES mysql_scanner)
endif()
FetchContent_Declare(duckdb_ext
    GIT_REPOSITORY "https://github.com/duckdb/duckdb.git"
    GIT_TAG "v2.0.0"
    GIT_SHALLOW TRUE)
set(BUILD_EXTENSIONS_ONLY ON CACHE BOOL "" FORCE)
set(BUILD_EXTENSIONS "${DUCKDB_EXT_SOURCES}" CACHE STRING "" FORCE)
set(BUILD_MAIN_DUCKDB_LIBRARY OFF CACHE BOOL "" FORCE)
set(BUILD_SHELL OFF CACHE BOOL "" FORCE)
set(BUILD_UNITTESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SANITIZER OFF CACHE BOOL "" FORCE)
set(ENABLE_UBSAN OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

# The extension build links its loadable artifacts against a duckdb_static
# target, which BUILD_EXTENSIONS_ONLY does not create. Alias it to the
# prebuilt archive (same v2.0.0 tag, so the symbols all resolve).
if(NOT TARGET duckdb_static)
    add_library(duckdb_static STATIC IMPORTED GLOBAL)
    set_target_properties(duckdb_static PROPERTIES
        IMPORTED_LOCATION "${duckdb_libs_SOURCE_DIR}/libduckdb_static.a")
endif()
FetchContent_MakeAvailable(duckdb_ext)

# Link the extension targets directly — output paths and build order come
# from CMake. MinGW's upstream config skips postgres, so check per target.
foreach(_name IN LISTS DUCKDB_EXT_SOURCES)
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
            file(GLOB_RECURSE MYSQL_OBJS_ALL
                "${duckdb_ext_BINARY_DIR}/extension/mysql_scanner/*.o")
            # The shared database-connector archive (from postgres) covers
            # these; keeping mysql's copies would collide with it.
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
                        -I"${duckdb_headers_SOURCE_DIR}" -I"${duckdb_libs_SOURCE_DIR}"
                COMMAND ar rcs "${MYSQL_PACK}"
                        "${CMAKE_BINARY_DIR}/stubs/mysql_scanner_describe.o" ${MYSQL_OBJS}
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
    src/core/HistoryStore.cpp
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

if(TARGET mysql_scanner_pack)
    # The packed mysql archive is produced by a custom command; make the
    # final binaries wait for it.
    add_dependencies(DucQ mysql_scanner_pack)
    add_dependencies(test_core mysql_scanner_pack)
endif()

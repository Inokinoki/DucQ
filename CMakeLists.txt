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
# Remote-database extensions (postgres / sqlite scanners)
#
# v2.0.0 ships these as out-of-tree extensions: the prebuilt static-libs
# bundle does not include them, so we build them from source. DuckDB's own
# root CMake fetches each extension repo at the pinned, v2.0.0-compatible
# commit via BUILD_EXTENSIONS; BUILD_EXTENSIONS_ONLY skips rebuilding the
# core (we link the prebuilt duckdb_static above, same tag => same ABI).
#
# mysql_scanner additionally needs an external libmariadb (vcpkg), so it is
# not built here. On MinGW the upstream config skips postgres, so it is
# optional there.
# ---------------------------------------------------------------------------
set(DUCKDB_EXT_SOURCES postgres_scanner sqlite_scanner)

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
    if(TARGET ${_name}_extension)
        message(STATUS "DucQ: linking remote-db extension ${_name}")
        target_link_libraries(duckdb_bundle INTERFACE ${_name}_extension)
        if(_name STREQUAL "postgres_scanner")
            # The scanner links libpq/OpenSSL PRIVATEly, so the final binary
            # must pick them up itself.
            find_package(OpenSSL QUIET)
            if(OpenSSL_FOUND)
                target_link_libraries(duckdb_bundle INTERFACE ${PostgreSQL_LIBRARY}
                    OpenSSL::SSL OpenSSL::Crypto)
            else()
                target_link_libraries(duckdb_bundle INTERFACE ${PostgreSQL_LIBRARY} ssl crypto)
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

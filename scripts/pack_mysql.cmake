# Packs the mysql_scanner extension's object files into a static archive for
# static registration. Script mode (cmake -P): runs at build time, so the
# objects already exist.
#
#   MYSQL_SRC_DIR    extension source/build dir scanned recursively for *.o
#   DESCRIBE_OBJ     compiled duckdb_extension_mysql_scanner_describe object
#   DUCKDB_INCLUDES  DuckDB headers (semicolon list)
#   MYSQL_PACK       output archive path
file(GLOB_RECURSE MYSQL_OBJS "${MYSQL_SRC_DIR}/*.o")
# In static mode postgres' archive (linked alongside) carries the vendored
# database-connector code, so mysql's copies would collide; in dynamic mode
# the engine does NOT contain postgres' connector, so mysql needs its own.
if(EXCLUDE_DBCONNECTOR)
    list(FILTER MYSQL_OBJS EXCLUDE REGEX "/dbconnector/")
endif()
if(NOT MYSQL_OBJS)
    message(FATAL_ERROR "pack_mysql: no object files found under ${MYSQL_SRC_DIR}")
endif()
file(REMOVE "${MYSQL_PACK}")
execute_process(COMMAND ar rcs "${MYSQL_PACK}" "${DESCRIBE_OBJ}" ${MYSQL_OBJS}
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "pack_mysql: ar failed")
endif()

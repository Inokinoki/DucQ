# Copies every built *.duckdb_extension into the target directory, flat.
# Invoked -P style from ducq_collect_loadables(-test) at build time; a flat
# sweep (not copy_directory of the whole extension tree) because the tree
# also holds intermediates that are still being written while we run — a
# whole-tree copy raced the last links and failed with ENOENT. core/
# ExtensionLoader scans recursively, so flattening changes nothing for it.
if(NOT DEFINED COLLECT_SRC OR NOT DEFINED COLLECT_DST)
    message(FATAL_ERROR "collect_loadables.cmake needs COLLECT_SRC and COLLECT_DST")
endif()
file(MAKE_DIRECTORY "${COLLECT_DST}")
file(GLOB_RECURSE DUCQ_LOADABLES "${COLLECT_SRC}/*.duckdb_extension")
foreach(_loadable IN LISTS DUCQ_LOADABLES)
    file(COPY "${_loadable}" DESTINATION "${COLLECT_DST}")
endforeach()
list(LENGTH DUCQ_LOADABLES DUCQ_LOADABLE_COUNT)
message(STATUS "Collected ${DUCQ_LOADABLE_COUNT} loadable(s) into ${COLLECT_DST}")

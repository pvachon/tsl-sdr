add_library(pager STATIC
    bch_code.c
    pager.c
    pager_flex.c
    pager_pocsag.c)

target_include_directories(pager PUBLIC
    "${TSL_SDR_BASE_DIR}"
    "${TSL_INCLUDE_DIRS}")

add_subdirectory(test)


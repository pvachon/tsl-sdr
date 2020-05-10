add_library(filter STATIC
    direct_fir.c
    polyphase_fir.c
    sample_buf.c
    utils.c)

target_include_directories(filter PUBLIC
    "${TSL_SDR_BASE_DIR}"
    "${TSL_INCLUDE_DIRS}")

add_subdirectory(test)


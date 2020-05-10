add_library(ais
    ais_decode.c
    ais_demod.c)

target_include_directories(ais PUBLIC
    "${TSL_SDR_BASE_DIR}"
    "${TSL_INCLUDE_DIRS}")

add_subdirectory(test)


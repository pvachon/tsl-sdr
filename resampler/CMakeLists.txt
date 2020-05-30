add_executable(resampler
    resampler.c)

target_include_directories(resampler PUBLIC
    "${TSL_SDR_BASE_DIR}"
    "${TSL_INCLUDE_DIRS}")

install(TARGETS resampler
    DESTINATION ${INSTALL_BIN_DIR})

target_link_libraries(resampler
    filter
    tsltestframework
    tslconfig
    tslapp
    tsl
    pthread
    m
    jansson)


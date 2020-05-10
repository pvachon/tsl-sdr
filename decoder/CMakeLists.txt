add_executable(decoder
    decoder.c)

target_include_directories(decoder PUBLIC
    "${TSL_SDR_BASE_DIR}"
    "${TSL_INCLUDE_DIRS}")

install(TARGETS decoder
    DESTINATION ${INSTALL_BIN_DIR})

target_link_libraries(decoder
    pager
    ais
    filter
    tsltestframework
    jansson
    tslconfig
    tslapp
    tsl
    pthread
    m)



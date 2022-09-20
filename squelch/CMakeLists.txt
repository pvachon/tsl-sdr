add_executable(squelch
    squelch.c
    )

target_include_directories(squelch PUBLIC
    "${TSL_SDR_BASE_DIR}"
    "${TSL_INCLUDE_DIRS}")

install(TARGETS squelch
    DESTINATION ${INSTALL_BIN_DIR})

target_link_libraries(squelch
    filter
    tsltestframework
    tslapp
    tslconfig
    tsl
    pthread
    m
    jansson)
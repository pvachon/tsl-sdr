add_executable(test_ais
    test_ais_demod.c)

target_link_libraries(test_ais
    ais
    tsltestframework
    tslconfig
    tslapp
    tsl
    jansson)

target_include_directories(test_ais PRIVATE "${TSL_SDR_BASE_DIR}")


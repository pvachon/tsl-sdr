add_executable(test_filter
    test_direct_fir.c
    test_polyphase_fir.c)

target_link_libraries(test_filter
    filter
    tsltestframework
    tslconfig
    tslapp
    tsl
    jansson)
target_include_directories(test_filter PRIVATE "${TSL_SDR_BASE_DIR}")


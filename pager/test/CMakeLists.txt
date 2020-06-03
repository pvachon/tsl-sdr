add_executable(test_pager
    test_pager_flex.c
    test_pager_pocsag.c)

target_link_libraries(test_pager
    pager
    tsltestframework
    tslconfig
    tslapp
    tsl
    jansson)

target_include_directories(test_pager PRIVATE "${TSL_SDR_BASE_DIR}")


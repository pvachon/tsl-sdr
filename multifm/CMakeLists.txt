set(RF_INTERFACE_SOURCES )
set(RF_INTERFACE_DIRS )
set(RF_INTERFACE_LIBS )

if(RTLSDR_FOUND)
	list(APPEND RF_INTERFACE_SOURCES rtl_sdr_if.c)
    list(APPEND RF_INTERFACE_DIRS ${RTLSDR_INCLUDE_DIRS})
    list(APPEND RF_INTERFACE_LIBS ${RTLSDR_LIBRARIES})
endif()

if(UHD_FOUND)
	list(APPEND RF_INTERFACE_SOURCES uhd_if.c)
    list(APPEND RF_INTERFACE_DIRS ${UHD_INCLUDE_DIRS})
    list(APPEND RF_INTERFACE_LIBS ${UHD_LIBRARIES})
endif()

if (DESPAIRSPY_FOUND)
	list(APPEND RF_INTERFACE_SOURCES airspy_if.c)
    list(APPEND RF_INTERFACE_DIRS ${DESPAIRSPY_INCLUDE_DIRS})
    list(APPEND RF_INTERFACE_LIBS ${DESPAIRSPY_LIBRARIES})
endif()

add_executable(multifm
	costas_demod.c
	demod.c
	fast_atan2f.c
	file_if.c
	fm_demod.c
	multifm.c
	receiver.c
	${RF_INTERFACE_SOURCES})

# Cumbersome, but add a DEFINE for the libraries found to ONLY the build command
# line for multifm.
if(RTLSDR_FOUND)
    target_compile_definitions(multifm PRIVATE -DHAVE_RTLSDR)
endif()

if(UHD_FOUND)
    target_compile_definitions(multifm PRIVATE -DHAVE_UHD)
endif()

if (DESPAIRSPY_FOUND)
    target_compile_definitions(multifm PRIVATE -DHAVE_DESPAIRSPY)
endif()

target_include_directories(multifm PUBLIC
    "${TSL_SDR_BASE_DIR}"
    "${TSL_INCLUDE_DIRS}"
	"${RF_INTERFACE_DIRS}")

install(TARGETS multifm
    DESTINATION ${INSTALL_BIN_DIR})

target_link_libraries(multifm
    filter
    tsltestframework
    tslconfig
    tslapp
    tsl
    pthread
    m
    ${RF_INTERFACE_LIBS}
    jansson)


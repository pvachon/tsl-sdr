cmake_minimum_required(VERSION 3.7)

include(CMakeDependentOption)

project(TSL-SDR VERSION 1.0.0)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g3 -gdwarf-4 -rdynamic")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# Installation path information
set(INSTALL_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE PATH "Installation directory for executables")

# Add our default set of gcc warnings
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wno-trigraphs")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format-security")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-delete-null-pointer-checks -Wuninitialized -Wmissing-include-dirs -Wshadow")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wframe-larger-than=2047")

set(TSL_SDR_BASE_DIR "${PROJECT_SOURCE_DIR}")

add_definitions(-D_ATS_IN_TREE -D_GNU_SOURCE)
add_definitions(-DSYS_CACHE_LINE_LENGTH=64)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

# Enable POCSAG debugging
option(DEBUG_POCSAG "Enable POCSAG State Machine Debugging" OFF)

# Enable DIAG statements
cmake_dependent_option(DEBUG_TSL "Enable verbose TSL debugging" ON
                       "DEBUG_POCSAG" OFF)

# Grab the revision from git
execute_process(COMMAND git describe --abbrev=16 --dirty --always --tags
    OUTPUT_VARIABLE GIT_REV
    ERROR_QUIET)

if ("${GIT_REV}" STREQUAL "")
    set(GIT_REV "NotInGit")
else()
    string(STRIP "${GIT_REV}" GIT_REV)
endif()

add_definitions(-D_VC_VERSION="${GIT_REV}")

# Enable POCSAG debugging if requested
if (DEBUG_POCSAG)
    add_definitions(-D_PAGER_POCSAG_DEBUG)

endif (DEBUG_POCSAG)

if (DEBUG_TSL)
    add_definitions(-D_TSL_DEBUG)
endif (DEBUG_TSL)

# Grab the CPU model we're building on (no cross-compiling support)
execute_process(COMMAND uname -m
    OUTPUT_VARIABLE CPU_ARCH
    ERROR_QUIET)
string(STRIP "${CPU_ARCH}" CPU_ARCH)

message(STATUS "CPU Archiecture is ${CPU_ARCH}")

if("${CPU_ARCH}" STREQUAL "armv7l")
    message(STATUS "Enabling NEON, setting CPU Architecture to armv7-a")
    add_definitions(-D_USE_ARM_NEON)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=crypto-neon-fp-armv8 -mfloat-abi=hard")
elseif("${CPU_ARCH}" STREQUAL "aarch64")
    message(STATUS "Enabling NEON, setting CPU architecture to armv8-a")
    add_definitions(-D_USE_ARM_NEON)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a -mtune=native")
else()
    message(STATUS "Using conservative defaults for CPU architecture")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -mtune=native")
endif()

# We use pkg-config to find our required libraries
find_package(PkgConfig REQUIRED)

# Find the TSL
pkg_check_modules(TSL REQUIRED tsl)

# Find RTL-SDR
pkg_check_modules(RTLSDR librtlsdr)

# Find DespAirspy
pkg_check_modules(DESPAIRSPY despairspy)

# Find UHD
pkg_check_modules(UHD uhd)

# Find ConcurrencyKit
pkg_check_modules(CK REQUIRED ck)

# Find Jansson
pkg_check_modules(JANSSON REQUIRED jansson)

# Build the Project
add_subdirectory(filter)
add_subdirectory(multifm)
add_subdirectory(pager)
add_subdirectory(ais)
add_subdirectory(resampler)
add_subdirectory(decoder)

# Only include RF interface modules we built as dpkg deps
set(RF_INTERFACE_DEPS )
if(RTLSDR_FOUND)
    set(RF_INTERFACE_DEPS "${RF_INTERFACE_DEPS}, librtlsdr0")
endif()

if(UHD_FOUND)
    set(RF_INTERFACE_DEPS "${RF_INTERFACE_DEPS}, libuhd003")
endif()

# Generate dpkg automatically
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_PACKAGE_NAME "tsl-sdr")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Phil Vachon <phil@security-embedded.com>")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "The Standard Library of SDR Functions")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libjansson4, libck0${RF_INTERFACE_DEPS}")
include(CPack)


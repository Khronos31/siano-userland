# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(l3_siano_prototype LANGUAGES C CXX)
endif()

include(CTest)
find_package(Threads REQUIRED)

set(SIANO_L3_CORE_INCLUDE_DIR "" CACHE PATH
    "Directory containing dtv/l3_owner.h from the Core L3 candidate")
set(SIANO_L3_CORE_SOURCE_DIR "" CACHE PATH
    "Optional Core prototype/l3_owner directory for owner-integrated replay tests")

option(SIANO_L3_ENABLE_ASAN "Build the L3 Siano targets with AddressSanitizer" OFF)
option(SIANO_L3_ENABLE_TSAN "Build the L3 Siano targets with ThreadSanitizer" OFF)
option(SIANO_L3_ENABLE_LIBUSB "Build the real libusb host and probe" ON)
option(SIANO_L3_SPLIT_API "Compile the split usb_provider/product_adapter bridge" OFF)

if(NOT SIANO_L3_CORE_INCLUDE_DIR)
    message(FATAL_ERROR "Set SIANO_L3_CORE_INCLUDE_DIR to the Core L3 include directory")
endif()
if(NOT EXISTS "${SIANO_L3_CORE_INCLUDE_DIR}/dtv/l3_owner.h")
    message(FATAL_ERROR "SIANO_L3_CORE_INCLUDE_DIR does not contain dtv/l3_owner.h")
endif()

set(SIANO_L3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SIANO_L3_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

function(siano_l3_warnings target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(siano_l3 STATIC
    siano-l3.cpp
    ${SIANO_L3_REPO_ROOT}/protocol.c)
target_include_directories(siano_l3 PUBLIC
    ${SIANO_L3_ROOT}
    ${SIANO_L3_REPO_ROOT}
    ${SIANO_L3_CORE_INCLUDE_DIR})
target_compile_features(siano_l3 PUBLIC cxx_std_17 c_std_11)
target_link_libraries(siano_l3 PUBLIC Threads::Threads)
set_target_properties(siano_l3 PROPERTIES CXX_EXTENSIONS OFF C_EXTENSIONS OFF)
siano_l3_warnings(siano_l3)
if(SIANO_L3_SPLIT_API)
    target_compile_definitions(siano_l3 PUBLIC SIANO_L3_SPLIT_API)
endif()

add_library(siano_l3_test_host STATIC tests/siano-l3-test-host.cpp)
target_include_directories(siano_l3_test_host PRIVATE
    ${SIANO_L3_ROOT}
    ${SIANO_L3_ROOT}/tests
    ${SIANO_L3_CORE_INCLUDE_DIR})
target_compile_features(siano_l3_test_host PRIVATE cxx_std_17)
target_link_libraries(siano_l3_test_host PUBLIC siano_l3 Threads::Threads)
set_target_properties(siano_l3_test_host PROPERTIES CXX_EXTENSIONS OFF)
siano_l3_warnings(siano_l3_test_host)
if(SIANO_L3_SPLIT_API)
    target_compile_definitions(siano_l3_test_host PUBLIC SIANO_L3_SPLIT_API)
endif()

set(SIANO_L3_TEST_TARGETS siano_l3 siano_l3_test_host)

if(SIANO_L3_ENABLE_LIBUSB)
    add_library(siano_l3_libusb STATIC siano-l3-libusb.cpp)
    target_include_directories(siano_l3_libusb PRIVATE
        ${SIANO_L3_ROOT}
        ${SIANO_L3_CORE_INCLUDE_DIR})
    target_compile_features(siano_l3_libusb PRIVATE cxx_std_17)
    if(WIN32)
        if(NOT LIBUSB_INCLUDE_DIR)
            find_path(LIBUSB_INCLUDE_DIR libusb.h REQUIRED)
        endif()
        if(NOT LIBUSB_LIBRARY)
            find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 REQUIRED)
        endif()
        target_include_directories(siano_l3_libusb SYSTEM PRIVATE
            ${LIBUSB_INCLUDE_DIR})
        target_link_libraries(siano_l3_libusb PUBLIC
            siano_l3 ${LIBUSB_LIBRARY} Threads::Threads)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SIANO_L3_LIBUSB REQUIRED libusb-1.0)
        target_include_directories(siano_l3_libusb SYSTEM PRIVATE
            ${SIANO_L3_LIBUSB_INCLUDE_DIRS})
        target_compile_options(siano_l3_libusb PRIVATE ${SIANO_L3_LIBUSB_CFLAGS_OTHER})
        target_link_libraries(siano_l3_libusb PUBLIC
            siano_l3 ${SIANO_L3_LIBUSB_LIBRARIES} Threads::Threads)
    endif()
    set_target_properties(siano_l3_libusb PROPERTIES CXX_EXTENSIONS OFF)
    siano_l3_warnings(siano_l3_libusb)
    list(APPEND SIANO_L3_TEST_TARGETS siano_l3_libusb)
endif()

if(SIANO_L3_CORE_SOURCE_DIR)
    if(NOT EXISTS "${SIANO_L3_CORE_SOURCE_DIR}/src/owner_engine.cpp")
        message(FATAL_ERROR "SIANO_L3_CORE_SOURCE_DIR does not contain src/owner_engine.cpp")
    endif()
    add_library(siano_l3_core_owner STATIC
        ${SIANO_L3_CORE_SOURCE_DIR}/src/owner_engine.cpp
        ${SIANO_L3_CORE_SOURCE_DIR}/src/c_api.cpp)
    target_include_directories(siano_l3_core_owner PUBLIC
        ${SIANO_L3_CORE_SOURCE_DIR}/include
        ${SIANO_L3_CORE_SOURCE_DIR}/src)
    target_compile_features(siano_l3_core_owner PUBLIC cxx_std_17)
    target_link_libraries(siano_l3_core_owner PUBLIC Threads::Threads)
    set_target_properties(siano_l3_core_owner PROPERTIES CXX_EXTENSIONS OFF)

    if(SIANO_L3_SPLIT_API)
        add_executable(siano_l3_split tests/test_siano_l3_split.c)
        target_compile_features(siano_l3_split PRIVATE c_std_11)
        target_compile_options(siano_l3_split PRIVATE -Wall -Wextra -Werror -pedantic)
        target_include_directories(siano_l3_split PRIVATE
            ${SIANO_L3_ROOT}
            ${SIANO_L3_ROOT}/tests
            ${SIANO_L3_CORE_SOURCE_DIR}/include)
        target_compile_definitions(siano_l3_split PRIVATE
            SIANO_L3_FIXTURE_DIR="${SIANO_L3_ROOT}/tests/fixtures")
        target_link_libraries(siano_l3_split PRIVATE
            siano_l3 siano_l3_test_host siano_l3_core_owner Threads::Threads)
        list(APPEND SIANO_L3_TEST_TARGETS siano_l3_split)
    endif()

    add_executable(siano_l3_replay tests/test_siano_l3_replay.c)
    target_compile_features(siano_l3_replay PRIVATE c_std_11)
    target_compile_options(siano_l3_replay PRIVATE -Wall -Wextra -Werror -pedantic)
    target_include_directories(siano_l3_replay PRIVATE
        ${SIANO_L3_ROOT}
        ${SIANO_L3_ROOT}/tests
        ${SIANO_L3_CORE_SOURCE_DIR}/include)
    target_compile_definitions(siano_l3_replay PRIVATE
        SIANO_L3_FIXTURE_DIR="${SIANO_L3_ROOT}/tests/fixtures")
    target_link_libraries(siano_l3_replay PRIVATE
        siano_l3 siano_l3_test_host siano_l3_core_owner Threads::Threads)
    list(APPEND SIANO_L3_TEST_TARGETS siano_l3_core_owner siano_l3_replay)

    if(TARGET siano_l3_libusb)
        add_executable(siano_l3_probe tools/siano-l3-probe.cpp)
        target_compile_features(siano_l3_probe PRIVATE cxx_std_17)
        target_include_directories(siano_l3_probe PRIVATE
            ${SIANO_L3_ROOT}
            ${SIANO_L3_CORE_SOURCE_DIR}/include)
        target_link_libraries(siano_l3_probe PRIVATE
            siano_l3_libusb siano_l3_core_owner Threads::Threads)
        siano_l3_warnings(siano_l3_probe)
        list(APPEND SIANO_L3_TEST_TARGETS siano_l3_probe)
    endif()
endif()

if(SIANO_L3_ENABLE_TSAN AND SIANO_L3_ENABLE_ASAN)
    message(FATAL_ERROR "Choose ASan or TSan, not both")
endif()
if(SIANO_L3_ENABLE_TSAN)
    foreach(target IN LISTS SIANO_L3_TEST_TARGETS)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endforeach()
elseif(SIANO_L3_ENABLE_ASAN)
    foreach(target IN LISTS SIANO_L3_TEST_TARGETS)
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=address)
    endforeach()
endif()

if(BUILD_TESTING)
    add_test(NAME siano_l3_source_spdx
        COMMAND bash ${SIANO_L3_ROOT}/tests/check_siano_l3_sources.sh ${SIANO_L3_REPO_ROOT})
    if(SIANO_L3_CORE_SOURCE_DIR)
        foreach(scenario IN ITEMS cold warm tune pid close quality)
            add_test(NAME siano_l3_replay_${scenario}
                COMMAND siano_l3_replay ${scenario})
        endforeach()
        if(TARGET siano_l3_probe)
            add_test(NAME siano_l3_probe_list COMMAND siano_l3_probe --list)
        endif()
        if(TARGET siano_l3_split)
            add_test(NAME siano_l3_split_ex COMMAND siano_l3_split)
        endif()
    endif()
endif()

cmake_minimum_required(VERSION 3.16)
project(siano_public_runtime LANGUAGES C CXX)

include(FetchContent)
option(BUILD_TESTING "Build the existing Siano proofs" ON)
set(SIANO_LINK_MAP_DIR "" CACHE PATH "Optional per-program link map output directory")

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SIANO_PRODUCT_VERSION_LINES)
list(LENGTH SIANO_PRODUCT_VERSION_LINES SIANO_PRODUCT_VERSION_LINE_COUNT)
if(NOT SIANO_PRODUCT_VERSION_LINE_COUNT EQUAL 1 OR
        NOT SIANO_PRODUCT_VERSION_LINES MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain one N.N.N line")
endif()
list(GET SIANO_PRODUCT_VERSION_LINES 0 SIANO_PRODUCT_VERSION)

set(DTV_USERLAND_CORE_SOURCE_DIR "" CACHE PATH
    "Explicit local dtv-userland-core source override")
set(SIANO_PX4_USERLAND_SOURCE_DIR "" CACHE PATH
    "Explicit local PX4 userland source override")
set(SIANO_RUNTIME_VERSION "${SIANO_PRODUCT_VERSION}" CACHE STRING
    "Siano runtime version (defaults to VERSION)")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dtv-userland-core.revision" DTV_CORE_REVISION)
string(STRIP "${DTV_CORE_REVISION}" DTV_CORE_REVISION)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/px4-userland.revision" PX4_USERLAND_REVISION)
string(STRIP "${PX4_USERLAND_REVISION}" PX4_USERLAND_REVISION)
foreach(_revision IN ITEMS DTV_CORE_REVISION PX4_USERLAND_REVISION)
    string(LENGTH "${${_revision}}" _revision_length)
    if(NOT _revision_length EQUAL 40 OR NOT "${${_revision}}" MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "${_revision} must contain a full 40-hex commit")
    endif()
endforeach()

if(DTV_USERLAND_CORE_SOURCE_DIR)
    set(DTV_CORE_SOURCE "${DTV_USERLAND_CORE_SOURCE_DIR}")
    message(WARNING "dtv-userland-core: using explicit local source ${DTV_CORE_SOURCE}; build is not pinned")
else()
    FetchContent_Declare(dtv_userland_core
        GIT_REPOSITORY "https://github.com/Khronos31/dtv-userland-core.git"
        GIT_TAG "${DTV_CORE_REVISION}")
    FetchContent_GetProperties(dtv_userland_core)
    if(NOT dtv_userland_core_POPULATED)
        FetchContent_Populate(dtv_userland_core)
    endif()
    set(DTV_CORE_SOURCE "${dtv_userland_core_SOURCE_DIR}")
endif()

if(SIANO_PX4_USERLAND_SOURCE_DIR)
    set(SIANO_PX4_SOURCE "${SIANO_PX4_USERLAND_SOURCE_DIR}")
    message(WARNING "PX4 userland: using explicit local source ${SIANO_PX4_SOURCE}; build is not pinned")
else()
    FetchContent_Declare(px4_userland
        GIT_REPOSITORY "https://github.com/Khronos31/px4-userland.git"
        GIT_TAG "${PX4_USERLAND_REVISION}")
    FetchContent_GetProperties(px4_userland)
    if(NOT px4_userland_POPULATED)
        FetchContent_Populate(px4_userland)
    endif()
    set(SIANO_PX4_SOURCE "${px4_userland_SOURCE_DIR}")
endif()
set(SIANO_PX4_ROOT "${SIANO_PX4_SOURCE}/userland")
file(WRITE "${CMAKE_BINARY_DIR}/dependency-sources.txt"
    "dtv-userland-core=${DTV_CORE_SOURCE}\nexpected=${DTV_CORE_REVISION}\nPX4 userland=${SIANO_PX4_SOURCE}\nexpected=${PX4_USERLAND_REVISION}\n")

# Distribution scripts pass the existing VERSION explicitly; direct developer
# builds retain the development default and must not fake a v1 release.
if(NOT SIANO_RUNTIME_VERSION)
    message(FATAL_ERROR "Pass SIANO_RUNTIME_VERSION (development value; do not claim v1)")
endif()

if(NOT EXISTS "${DTV_CORE_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "dtv-userland-core source has no CMakeLists.txt: ${DTV_CORE_SOURCE}")
endif()
file(STRINGS "${DTV_CORE_SOURCE}/CMakeLists.txt" DTV_CORE_PROJECT_LINE
    REGEX "^project\\(dtv_userland_core VERSION ")
if(NOT DTV_CORE_PROJECT_LINE MATCHES
        "^project\\(dtv_userland_core VERSION ([0-9]+\\.[0-9]+\\.[0-9]+) LANGUAGES CXX\\)$")
    message(FATAL_ERROR "Could not read dtv-userland-core project version")
endif()
set(DTV_USERLAND_CORE_VERSION "${CMAKE_MATCH_1}")
set(DTV_USERLAND_BUILD_CAPTURE ON CACHE BOOL "Build packet capture storage" FORCE)
set(DTV_USERLAND_BUILD_RECEIVER ON CACHE BOOL "Build receiver engine" FORCE)
set(DTV_USERLAND_BUILD_CARD ON CACHE BOOL "Build card ownership engine" FORCE)
set(DTV_USERLAND_BUILD_STATUS_PROJECTION ON CACHE BOOL
    "Build the optional status projection" FORCE)
set(DTV_USERLAND_BUILD_AUTHORITY ON CACHE BOOL
    "Build the optional control authority" FORCE)
set(DTV_USERLAND_BUILD_CONTROL ON CACHE BOOL
    "Build the optional control protocol" FORCE)
set(DTV_USERLAND_BUILD_HOST_CTL ON CACHE BOOL
    "Build the optional GPL host control frontend" FORCE)
set(DTV_USERLAND_BUILD_HOST_RUNTIME ON CACHE BOOL
    "Build the optional GPL host runtime adapter" FORCE)
set(DTV_USERLAND_BUILD_HOST_TUNER ON CACHE BOOL
    "Build the optional GPL host tuner adapter" FORCE)
set(DTV_USERLAND_BUILD_HOST_WORKERS ON CACHE BOOL
    "Build the optional GPL host control workers" FORCE)
set(DTV_USERLAND_BUILD_HOST_AUTHORITY_OWNER ON CACHE BOOL
    "Build the optional GPL host authority owner" FORCE)
set(DTV_USERLAND_BUILD_HOST_AUTHORITY_BRIDGE ON CACHE BOOL
    "Build the optional GPL host authority bridge" FORCE)
set(DTV_USERLAND_BUILD_HOST_LISTENER_CONTEXT ON CACHE BOOL
    "Build the optional GPL host listener Context" FORCE)
set(DTV_USERLAND_BUILD_HOST_TS ON CACHE BOOL
    "Build the optional GPL host TS consumer" FORCE)
set(DTV_USERLAND_BUILD_HOST_FACADE ON CACHE BOOL
    "Build the optional GPL host facade" FORCE)
set(DTV_USERLAND_BUILD_HOST_DAEMON ON CACHE BOOL
    "Build the optional GPL host daemon services" FORCE)
set(DTV_USERLAND_HOST_RUNTIME_PRODUCT "siano-userland" CACHE STRING
    "Explicit Siano runtime namespace" FORCE)
set(DTV_USERLAND_HOST_RUNTIME_INSTANCE_POLICY "lowercase" CACHE STRING
    "Siano runtime instance normalization" FORCE)
set(_SIANO_HOST_FACADE_INCLUDE_DIRS
    "${DTV_CORE_SOURCE}/adapters/host_daemon/include;${DTV_CORE_SOURCE}/adapters/host_facade/include;${DTV_CORE_SOURCE}/adapters/host_runtime/include;${DTV_CORE_SOURCE}/adapters/host_listener_context/include;${DTV_CORE_SOURCE}/adapters/host_ts/include;${SIANO_PX4_ROOT}/include")
set(DTV_USERLAND_HOST_RUNTIME_FACADE_INCLUDE_DIRS
    "${_SIANO_HOST_FACADE_INCLUDE_DIRS}" CACHE STRING
    "Private host facade include directories for the GPL host runtime" FORCE)
set(DTV_USERLAND_HOST_TS_FACADE_INCLUDE_DIRS
    "${_SIANO_HOST_FACADE_INCLUDE_DIRS};${SIANO_PX4_ROOT}/tools" CACHE STRING
    "Private host facade include directories for the GPL host TS consumer" FORCE)
set(DTV_USERLAND_HOST_CTL_FACADE_INCLUDE_DIRS "${SIANO_PX4_ROOT}/include"
    CACHE STRING "Private host facade include directories for the GPL host control frontend" FORCE)
set(DTV_USERLAND_BUILD_TESTING OFF CACHE BOOL "Build core tests" FORCE)
add_subdirectory(${DTV_CORE_SOURCE} core)

find_package(Threads REQUIRED)
add_library(siano_libusb INTERFACE)
if(WIN32)
    find_path(LIBUSB_INCLUDE_DIR libusb.h REQUIRED)
    find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 REQUIRED)
    target_include_directories(siano_libusb INTERFACE "${LIBUSB_INCLUDE_DIR}")
    target_link_libraries(siano_libusb INTERFACE "${LIBUSB_LIBRARY}")
else()
    find_package(PkgConfig REQUIRED)
    # The existing isolated distribution wrappers implement complete --cflags
    # and --libs queries, including the exact static archive, not split queries.
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    target_compile_options(siano_libusb INTERFACE ${LIBUSB_CFLAGS})
    set(_SIANO_LIBUSB_LINK_FLAGS ${LIBUSB_LDFLAGS})
    if(APPLE)
        # Keep each framework/name pair together; a bare name becomes -lNAME.
        string(REGEX REPLACE "-framework;([^;]+)" "-framework \\1"
            _SIANO_LIBUSB_LINK_FLAGS "${_SIANO_LIBUSB_LINK_FLAGS}")
    endif()
    target_link_libraries(siano_libusb INTERFACE ${_SIANO_LIBUSB_LINK_FLAGS})
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(siano_enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /EHs-c- /GR-)
    else()
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
    endif()
endfunction()

# --- Core host runtime facade ---
# S keeps the product-facing include namespace; all shared implementation is
# owned by Core and its existing MIT engines.
add_library(siano_px4_runtime INTERFACE)
target_include_directories(siano_px4_runtime INTERFACE
    ${_SIANO_HOST_FACADE_INCLUDE_DIRS}
    ${SIANO_PX4_ROOT}/include ${SIANO_PX4_ROOT}/src ${SIANO_PX4_ROOT}/tools)
target_link_libraries(siano_px4_runtime INTERFACE
    dtv::core dtv::wire dtv::control dtv::identity dtv::tuning
    dtv::receiver dtv::capture dtv::card dtv::host_facade dtv::host_runtime
    dtv::host_tuner dtv::host_workers dtv::host_authority_owner
    dtv::host_authority_bridge dtv::host_listener_context dtv::host_ts)
target_compile_features(siano_px4_runtime INTERFACE cxx_std_17)

# --- S C backend + composition (GPL-2.0-or-later) ---
function(siano_include_project_root target)
    if(APPLE)
        # Case-insensitive macOS volumes otherwise resolve libc++ <version>
        # to the project's VERSION text file. Project headers use quotes.
        target_compile_options(${target} PUBLIC "-iquote${CMAKE_CURRENT_SOURCE_DIR}")
    else()
        target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endfunction()

add_library(siano_c_backend STATIC
    siano-device.c siano-capture-host.c protocol.c stream-state.c control-parse.c
    control-input.c device-selector.c exit-codes.c queue-policy.c output-writer.c)
target_compile_features(siano_c_backend PUBLIC c_std_11)
target_compile_definitions(siano_c_backend PRIVATE
    _POSIX_C_SOURCE=200809L _FILE_OFFSET_BITS=64)
target_link_libraries(siano_c_backend PUBLIC siano_libusb Threads::Threads)
siano_include_project_root(siano_c_backend)

add_library(siano_capture_composition STATIC
    siano-capture-consumer.cpp siano-receiver-adapter.cpp)
target_compile_features(siano_capture_composition PUBLIC cxx_std_17)
siano_include_project_root(siano_capture_composition)
target_link_libraries(siano_capture_composition PUBLIC
    dtv::receiver dtv::capture siano_c_backend)

if(BUILD_TESTING AND UNIX)
add_library(siano_capture_composition_proof STATIC siano-capture-consumer.cpp)
target_compile_features(siano_capture_composition_proof PUBLIC cxx_std_17)
target_compile_definitions(siano_capture_composition_proof PRIVATE SIANO_CAPTURE_TEST_SEAMS)
siano_include_project_root(siano_capture_composition_proof)
target_link_libraries(siano_capture_composition_proof PUBLIC
    dtv::receiver dtv::capture siano_c_backend)

add_executable(siano_capture_downstream_link tests/downstream_link.cpp)
target_compile_features(siano_capture_downstream_link PRIVATE cxx_std_17)
target_link_libraries(siano_capture_downstream_link PRIVATE siano_capture_composition)

add_executable(siano_capture_consumer_proof tests/consumer_proof.cpp tests/libusb-loopback.c)
target_compile_features(siano_capture_consumer_proof PRIVATE cxx_std_17)
target_compile_definitions(siano_capture_consumer_proof PRIVATE SIANO_CAPTURE_TEST_SEAMS)
target_link_libraries(siano_capture_consumer_proof PRIVATE siano_capture_composition_proof)
target_link_options(siano_capture_consumer_proof PRIVATE
    -Wl,--wrap=pthread_join -Wl,--wrap=pthread_create
    -Wl,--wrap=pthread_cond_timedwait -Wl,--wrap=pthread_cond_clockwait
    -Wl,--wrap=calloc -Wl,--wrap=ts_pop_capture)

add_executable(siano_receiver_adapter_proof
    tests/receiver_adapter_proof.cpp tests/libusb-loopback.c)
target_compile_features(siano_receiver_adapter_proof PRIVATE cxx_std_17)
target_link_libraries(siano_receiver_adapter_proof PRIVATE siano_capture_composition)
endif()

# S product glue: the P-facing family/shim and the LIST/assemble builder.
add_library(siano_runtime STATIC
    siano-runtime-backend.cpp
    siano-enclosure.cpp)
target_compile_features(siano_runtime PUBLIC cxx_std_17)
target_compile_definitions(siano_runtime PRIVATE
    DTV_USERLAND_CORE_VERSION="${DTV_USERLAND_CORE_VERSION}")
siano_include_project_root(siano_runtime)
target_link_libraries(siano_runtime PUBLIC
    siano_px4_runtime siano_capture_composition
    dtv::control dtv::identity dtv::tuning dtv::host_tuner
    dtv::host_workers dtv::host_authority_owner dtv::host_authority_bridge
    dtv::host_listener_context)

# --- public artifacts ---
add_executable(sianod sianod.cpp)
target_compile_features(sianod PRIVATE cxx_std_17)
target_compile_definitions(sianod PRIVATE SIANO_RUNTIME_VERSION="${SIANO_RUNTIME_VERSION}")
target_link_libraries(sianod PRIVATE siano_runtime dtv::json siano_libusb)

if(WIN32 AND BUILD_TESTING)
    add_executable(sianod_windows_eof_fixture tests/windows_sianod_eof_fixture.cpp)
    target_compile_features(sianod_windows_eof_fixture PRIVATE cxx_std_17)
    target_include_directories(sianod_windows_eof_fixture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(sianod_windows_eof_fixture PRIVATE dtv::host_facade)
endif()

add_executable(sianoctl sianoctl.cpp)
target_compile_features(sianoctl PRIVATE cxx_std_17)
target_compile_definitions(sianoctl PRIVATE
    SIANO_RUNTIME_VERSION="${SIANO_RUNTIME_VERSION}")
target_link_libraries(sianoctl PRIVATE dtv::host_ctl siano_px4_runtime)
siano_enable_project_warnings(sianoctl)

add_executable(siano_wire_ts siano-wire-ts.cpp)
set_target_properties(siano_wire_ts PROPERTIES OUTPUT_NAME siano-ts)
target_compile_features(siano_wire_ts PRIVATE cxx_std_17)
target_compile_definitions(siano_wire_ts PRIVATE
    SIANO_RUNTIME_VERSION="${SIANO_RUNTIME_VERSION}")
target_include_directories(siano_wire_ts PRIVATE ${SIANO_PX4_ROOT}/tools)
target_link_libraries(siano_wire_ts PRIVATE siano_px4_runtime siano_c_backend dtv::host_ts)
if(MSVC)
    # Shared wire status snapshots and bounded control buffers exceed the
    # default 1 MiB main-thread stack during the TS client's status handshake.
    target_link_options(siano_wire_ts PRIVATE /STACK:2097152)
endif()

if(WIN32)
    foreach(_program IN ITEMS sianod sianoctl siano_wire_ts)
        target_compile_definitions(${_program} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
        target_link_libraries(${_program} PRIVATE shell32)
    endforeach()
endif()
if(SIANO_LINK_MAP_DIR AND NOT MSVC)
    file(MAKE_DIRECTORY "${SIANO_LINK_MAP_DIR}")
    foreach(_program IN ITEMS sianod sianoctl siano_wire_ts)
        target_link_options(${_program} PRIVATE
            "-Wl,-Map,${SIANO_LINK_MAP_DIR}/$<TARGET_FILE_BASE_NAME:${_program}>.map")
    endforeach()
endif()

# Link-closure probe: references the P runtime symbols the S product needs so a
# missing subset symbol fails the link here rather than at product link time.
if(BUILD_TESTING AND UNIX)
add_executable(siano_runtime_composition_proof tests/runtime_composition_proof.cpp)
target_compile_features(siano_runtime_composition_proof PRIVATE cxx_std_17)
target_link_libraries(siano_runtime_composition_proof PRIVATE
    siano_px4_runtime siano_capture_composition dtv::identity dtv::tuning)

# Limited acceptance proof: real assembly + real AF_UNIX server + separate
# siano-ts process; only the bottom USB layer is the loopback fixture.
add_executable(siano_runtime_v1_proof
    tests/runtime_v1_proof.cpp
    tests/libusb-loopback.c
    tests/libusb-loopback-topology.c)
target_compile_features(siano_runtime_v1_proof PRIVATE cxx_std_17)
target_compile_definitions(siano_runtime_v1_proof PRIVATE
    SIANO_TS_PATH="$<TARGET_FILE:siano_wire_ts>"
    SIANOCTL_PATH="$<TARGET_FILE:sianoctl>"
    SIANO_RUNTIME_VERSION="${SIANO_RUNTIME_VERSION}")
target_link_libraries(siano_runtime_v1_proof PRIVATE
    siano_runtime siano_libusb Threads::Threads)
add_dependencies(siano_runtime_v1_proof sianoctl siano_wire_ts)

add_executable(siano_control_output_finish_proof
    tests/control_output_finish_proof.cpp)
target_compile_features(siano_control_output_finish_proof PRIVATE cxx_std_17)
target_include_directories(siano_control_output_finish_proof PRIVATE ${SIANO_PX4_ROOT}/tools)
target_link_libraries(siano_control_output_finish_proof PRIVATE
    siano_px4_runtime siano_c_backend dtv::host_ts)

# One focused fixture for the newly connected PID list and wrapped-FD facts.
add_executable(siano_runtime_features_proof
    tests/runtime_features_proof.cpp
    tests/libusb-loopback.c tests/libusb-loopback-topology.c)
target_compile_features(siano_runtime_features_proof PRIVATE cxx_std_17)
target_compile_definitions(siano_runtime_features_proof PRIVATE
    DTV_USERLAND_CORE_VERSION="${DTV_USERLAND_CORE_VERSION}")
target_link_libraries(siano_runtime_features_proof PRIVATE
    siano_runtime siano_libusb Threads::Threads)

# Held-callback close proof.  It compiles the accepted S composition with the
# existing test seam so one packet sink can be held while the real context
# close runs.
add_executable(siano_held_close_proof
    tests/held_close_proof.cpp
    tests/libusb-loopback.c
    tests/libusb-loopback-topology.c
    siano-capture-consumer.cpp
    siano-receiver-adapter.cpp
    siano-runtime-backend.cpp
    siano-enclosure.cpp)
target_compile_features(siano_held_close_proof PRIVATE cxx_std_17)
target_compile_definitions(siano_held_close_proof PRIVATE
    SIANO_CAPTURE_TEST_SEAMS SIANO_RUNTIME_PROOF_HOOKS)
target_include_directories(siano_held_close_proof PRIVATE ${SIANO_PX4_ROOT}/tools)
target_link_libraries(siano_held_close_proof PRIVATE
    siano_px4_runtime siano_c_backend dtv::receiver dtv::capture
    siano_libusb Threads::Threads)
endif()

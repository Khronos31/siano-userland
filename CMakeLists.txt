cmake_minimum_required(VERSION 3.16)
project(siano_public_runtime LANGUAGES C CXX)

include(FetchContent)

set(DTV_USERLAND_CORE_SOURCE_DIR "" CACHE PATH
    "Explicit local dtv-userland-core source override")
set(SIANO_PX4_USERLAND_SOURCE_DIR "" CACHE PATH
    "Explicit local PX4 userland source override")
set(SIANO_RUNTIME_VERSION "0.0.0-dev" CACHE STRING
    "Siano runtime version (default is an unreleased development build)")

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
        GIT_REPOSITORY "git@github.com:Khronos31/dtv-userland-core.git"
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
        GIT_REPOSITORY "git@github.com:Khronos31/px4-userland.git"
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

# The version is a required cache value: this tree carries no VERSION file and
# the new entry must not fake a v1 release.
if(NOT SIANO_RUNTIME_VERSION)
    message(FATAL_ERROR "Pass SIANO_RUNTIME_VERSION (development value; do not claim v1)")
endif()

if(NOT EXISTS "${DTV_CORE_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "dtv-userland-core source has no CMakeLists.txt: ${DTV_CORE_SOURCE}")
endif()
set(DTV_USERLAND_BUILD_CAPTURE ON CACHE BOOL "Build packet capture storage" FORCE)
set(DTV_USERLAND_BUILD_RECEIVER ON CACHE BOOL "Build receiver engine" FORCE)
set(DTV_USERLAND_BUILD_CARD ON CACHE BOOL "Build card ownership engine" FORCE)
set(DTV_USERLAND_BUILD_TESTING OFF CACHE BOOL "Build core tests" FORCE)
add_subdirectory(${DTV_CORE_SOURCE} core)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(siano_enable_project_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
endfunction()

# --- P runtime source subset (GPL-2.0-only, own static target) ---
# The set matches the accepted design report (S/siano-public-runtime-minimal-binding
# section 1.3).  It deliberately excludes P's card.cpp, identity.cpp,
# control_context.cpp and every native USB/frontend backend so the S product
# does not link the PX hardware stack.
set(SIANO_PX4_RUNTIME_SOURCES
    ${SIANO_PX4_ROOT}/src/ipc.cpp
    ${SIANO_PX4_ROOT}/src/posix_ipc.cpp
    ${SIANO_PX4_ROOT}/src/posix_tuner_nonce.cpp
    ${SIANO_PX4_ROOT}/src/control_server.cpp
    ${SIANO_PX4_ROOT}/src/control_workers.cpp
    ${SIANO_PX4_ROOT}/src/tuner_service.cpp
    ${SIANO_PX4_ROOT}/src/card_service.cpp
    ${SIANO_PX4_ROOT}/src/enclosure_context.cpp
    ${SIANO_PX4_ROOT}/src/control_client.cpp
    ${SIANO_PX4_ROOT}/tools/px4_ts_core.cpp
    ${SIANO_PX4_ROOT}/tools/px4_ts_posix.cpp
    ${SIANO_PX4_ROOT}/tools/px4d_signals.cpp
)

add_library(siano_px4_runtime STATIC ${SIANO_PX4_RUNTIME_SOURCES})
target_include_directories(siano_px4_runtime PUBLIC
    ${SIANO_PX4_ROOT}/include ${SIANO_PX4_ROOT}/src ${SIANO_PX4_ROOT}/tools)
target_compile_definitions(siano_px4_runtime PRIVATE
    PX4_USERLAND_RUNTIME_PRODUCT="siano-userland")
target_link_libraries(siano_px4_runtime PUBLIC
    Threads::Threads dtv::core dtv::wire dtv::control dtv::identity dtv::tuning
    dtv::receiver dtv::capture dtv::card)
target_compile_features(siano_px4_runtime PUBLIC cxx_std_17)
siano_enable_project_warnings(siano_px4_runtime)
# Shared core's additional CARD opcode kinds are rejected by the feature gate
# in this legacy non-CARD runtime. Keep all other -Werror checks enabled.
target_compile_options(siano_px4_runtime PRIVATE -Wno-error=switch)

# --- S C backend + composition (GPL-2.0-or-later) ---
add_library(siano_c_backend STATIC
    siano-device.c siano-capture-host.c protocol.c stream-state.c control-parse.c
    control-input.c device-selector.c exit-codes.c queue-policy.c output-writer.c)
target_compile_features(siano_c_backend PUBLIC c_std_11)
target_compile_definitions(siano_c_backend PRIVATE
    _POSIX_C_SOURCE=200809L _FILE_OFFSET_BITS=64)
target_link_libraries(siano_c_backend PUBLIC PkgConfig::LIBUSB Threads::Threads)
target_include_directories(siano_c_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(siano_capture_composition STATIC
    siano-capture-consumer.cpp siano-receiver-adapter.cpp)
target_compile_features(siano_capture_composition PUBLIC cxx_std_17)
target_include_directories(siano_capture_composition PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(siano_capture_composition PUBLIC
    dtv::receiver dtv::capture siano_c_backend)

add_library(siano_capture_composition_proof STATIC siano-capture-consumer.cpp)
target_compile_features(siano_capture_composition_proof PUBLIC cxx_std_17)
target_compile_definitions(siano_capture_composition_proof PRIVATE SIANO_CAPTURE_TEST_SEAMS)
target_include_directories(siano_capture_composition_proof PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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

# S product glue: the P-facing family/shim and the LIST/assemble builder.
add_library(siano_runtime STATIC
    siano-runtime-backend.cpp
    siano-enclosure.cpp)
target_compile_features(siano_runtime PUBLIC cxx_std_17)
target_include_directories(siano_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(siano_runtime PUBLIC
    siano_px4_runtime siano_capture_composition
    dtv::control dtv::identity dtv::tuning)

# --- public artifacts ---
add_executable(sianod sianod.cpp)
target_compile_features(sianod PRIVATE cxx_std_17)
target_compile_definitions(sianod PRIVATE SIANO_RUNTIME_VERSION="${SIANO_RUNTIME_VERSION}")
target_link_libraries(sianod PRIVATE siano_runtime dtv::json)

add_executable(sianoctl ${SIANO_PX4_ROOT}/tools/px4ctl.cpp)
target_compile_features(sianoctl PRIVATE cxx_std_17)
target_compile_definitions(sianoctl PRIVATE
    PX4CTL_TOOL_NAME="sianoctl" PX4CTL_PRODUCT_SLUG="siano-userland"
    PX4CTL_PRODUCT_ENUM=siano PX4CTL_PRODUCT_VERSION="${SIANO_RUNTIME_VERSION}")
target_link_libraries(sianoctl PRIVATE siano_px4_runtime dtv::json dtv::identity)
siano_enable_project_warnings(sianoctl)

add_executable(siano_wire_ts siano-wire-ts.cpp)
set_target_properties(siano_wire_ts PROPERTIES OUTPUT_NAME siano-ts)
target_compile_features(siano_wire_ts PRIVATE cxx_std_17)
target_compile_definitions(siano_wire_ts PRIVATE
    SIANO_RUNTIME_VERSION="${SIANO_RUNTIME_VERSION}")
target_include_directories(siano_wire_ts PRIVATE ${SIANO_PX4_ROOT}/tools)
target_link_libraries(siano_wire_ts PRIVATE siano_px4_runtime)

# Link-closure probe: references the P runtime symbols the S product needs so a
# missing subset symbol fails the link here rather than at product link time.
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
    siano_runtime PkgConfig::LIBUSB Threads::Threads)
add_dependencies(siano_runtime_v1_proof sianoctl siano_wire_ts)

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
    PkgConfig::LIBUSB Threads::Threads)

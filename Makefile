# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
PKG_CONFIG ?= pkg-config
# CMake 4.4 resolves a relative PKG_CONFIG_EXECUTABLE below the source root.
# Keep an explicit override, but pass the resolved absolute path.
PKG_CONFIG := $(shell command -v '$(PKG_CONFIG)' 2>/dev/null || printf '%s' '$(PKG_CONFIG)')
CMAKE ?= cmake
# GNU make predefines CXX=g++; honor c++ unless the caller overrides it.
ifeq ($(origin CXX), default)
CXX := c++
endif
RUNTIME_BUILD_DIR ?= build/runtime
CMAKE_ARGS ?=
ifeq ($(strip $(DTV_USERLAND_CORE_SOURCE_DIR)),)
DTV_USERLAND_CORE_SOURCE_DIR := $(if $(wildcard third_party/dtv-userland-core/CMakeLists.txt),$(CURDIR)/third_party/dtv-userland-core)
endif
ifeq ($(strip $(SIANO_PX4_USERLAND_SOURCE_DIR)),)
SIANO_PX4_USERLAND_SOURCE_DIR := $(if $(wildcard third_party/px4-userland/userland),$(CURDIR)/third_party/px4-userland)
endif
ifneq ($(DTV_USERLAND_CORE_SOURCE_DIR),)
override CMAKE_ARGS += -DDTV_USERLAND_CORE_SOURCE_DIR="$(DTV_USERLAND_CORE_SOURCE_DIR)"
endif
ifneq ($(SIANO_PX4_USERLAND_SOURCE_DIR),)
override CMAKE_ARGS += -DSIANO_PX4_USERLAND_SOURCE_DIR="$(SIANO_PX4_USERLAND_SOURCE_DIR)"
endif
CFLAGS ?= -O2
# _FILE_OFFSET_BITS=64: 32-bit glibc open()+write() past 2GiB (EFBIG) without
# O_LARGEFILE. No-op on LP64, musl, and Bionic (which already sets O_LARGEFILE).
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags libusb-1.0)
LDLIBS += $(shell $(PKG_CONFIG) --libs libusb-1.0) -pthread

.PHONY: all runtime clean test packaging-test linux-static siano-ts sianod sianoctl
.DEFAULT_GOAL := all

# Linux CLI tests use a preload mock for dynamically linked libusb. On Darwin,
# test_cli.sh runs without interposition; its --list check performs read-only
# USB enumeration. No static libusb interposition is claimed.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LIBUSB_MOCK :=
LIBUSB_MOCK_LOAD :=
DEVICE_LIFECYCLE_TARGET :=
DEVICE_LIFECYCLE_RUN :=
DEVICE_LIFECYCLE_LINK :=
CAPTURE_GATE_TARGET :=
CAPTURE_GATE_RUN :=
CAPTURE_GATE_LINK :=
else
LIBUSB_MOCK := tests/libusb-mock.so
LIBUSB_MOCK_LOAD := LD_PRELOAD=$(CURDIR)/$(LIBUSB_MOCK)
DEVICE_LIFECYCLE_TARGET := test-device-lifecycle
DEVICE_LIFECYCLE_RUN := ./test-device-lifecycle
DEVICE_LIFECYCLE_LINK := -Wl,--wrap=pthread_join -Wl,--wrap=free -Wl,--wrap=malloc -Wl,--wrap=mlockall
CAPTURE_GATE_TARGET := test-capture-gate
CAPTURE_GATE_RUN := ./test-capture-gate
CAPTURE_GATE_LINK := -Wl,--wrap=pthread_cond_timedwait
$(LIBUSB_MOCK): tests/libusb-mock.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -shared -o $@ tests/libusb-mock.c
endif

all: runtime

runtime:
	$(CMAKE) -S . -B $(RUNTIME_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_CXX_COMPILER="$(CXX)" -DCMAKE_C_FLAGS="$(CFLAGS)" -DCMAKE_CXX_FLAGS="$(CXXFLAGS)" -DCMAKE_EXE_LINKER_FLAGS="$(LDFLAGS)" -DPKG_CONFIG_EXECUTABLE="$(PKG_CONFIG)" -DSIANO_RUNTIME_VERSION="$$(tr -d '\n' < VERSION)" -DBUILD_TESTING=OFF $(CMAKE_ARGS)
	$(CMAKE) --build $(RUNTIME_BUILD_DIR) --target sianod sianoctl siano_wire_ts --parallel
	cp $(RUNTIME_BUILD_DIR)/sianod $(RUNTIME_BUILD_DIR)/sianoctl $(RUNTIME_BUILD_DIR)/siano-ts .

siano-ts sianod sianoctl: runtime

# Retain historical standalone CLI tests without overwriting a public binary.
build/legacy/siano-ts: siano-ts.o siano-device.o protocol.o stream-state.o control-parse.o control-input.o device-selector.o exit-codes.o queue-policy.o output-writer.o
	mkdir -p build/legacy
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	ln -sfn ../../tests build/legacy/tests

siano-ts.o: siano-ts.c siano-device.h protocol.h stream-state.h control-parse.h control-input.h device-selector.h detach-decision.h exit-codes.h usb-location.h queue-policy.h output-writer.h write-policy.h
siano-device.o: siano-device.c siano-device.h protocol.h stream-state.h device-selector.h detach-decision.h exit-codes.h usb-location.h queue-policy.h output-writer.h
protocol.o: protocol.c protocol.h
stream-state.o: stream-state.c stream-state.h
control-parse.o: control-parse.c control-parse.h
control-input.o: control-input.c control-input.h
device-selector.o: device-selector.c device-selector.h
exit-codes.o: exit-codes.c exit-codes.h
queue-policy.o: queue-policy.c queue-policy.h
output-writer.o: output-writer.c output-writer.h write-policy.h

test-protocol: tests/test_protocol.o protocol.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

tests/test_protocol.o: tests/test_protocol.c protocol.h

test-clock: tests/test_clock.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

tests/test_clock.o: tests/test_clock.c siano-clock.h

test-stream-state: tests/test_stream_state.o stream-state.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/test_stream_state.o: tests/test_stream_state.c stream-state.h

test-control-parse: tests/test_control_parse.o control-parse.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

tests/test_control_parse.o: tests/test_control_parse.c control-parse.h

test-control-input: tests/test_control_input.o control-input.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

tests/test_control_input.o: tests/test_control_input.c control-input.h

test-usb-location: tests/test_usb_location.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

tests/test_usb_location.o: tests/test_usb_location.c usb-location.h

test-detach-decision: tests/test_detach_decision.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/test_detach_decision.o: tests/test_detach_decision.c detach-decision.h

test-device-selector: tests/test_device_selector.o device-selector.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

tests/test_device_selector.o: tests/test_device_selector.c device-selector.h usb-location.h

test-exit-codes: tests/test_exit_codes.o exit-codes.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/test_exit_codes.o: tests/test_exit_codes.c exit-codes.h

test-queue-policy: tests/test_queue_policy.o queue-policy.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/test_queue_policy.o: tests/test_queue_policy.c queue-policy.h siano-os.h

test-write-policy: tests/test_write_policy.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test-capture-sink: tests/test_capture_sink.o output-writer.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/test_capture_sink.o: tests/test_capture_sink.c output-writer.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -c -o $@ $<

tests/test_capture_gate.o: tests/test_capture_gate.c siano-device.h protocol.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -c -o $@ $<

tests/test_write_policy.o: tests/test_write_policy.c write-policy.h

test-output-integration: tests/test_output_integration.o siano-device.o protocol.o stream-state.o control-parse.o control-input.o device-selector.o exit-codes.o queue-policy.o output-writer.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-device-lifecycle: tests/test_device_lifecycle.o siano-device.o protocol.o stream-state.o control-parse.o control-input.o device-selector.o exit-codes.o queue-policy.o output-writer.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(DEVICE_LIFECYCLE_LINK) -o $@ $^ $(LDLIBS)

test-capture-gate: tests/test_capture_gate.o protocol.o stream-state.o control-parse.o control-input.o device-selector.o exit-codes.o queue-policy.o output-writer.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(CAPTURE_GATE_LINK) -o $@ $^ $(LDLIBS)

tests/test_output_integration.o: tests/test_output_integration.c siano-ts.c siano-device.h output-writer.h protocol.h stream-state.h control-parse.h control-input.h device-selector.h detach-decision.h exit-codes.h usb-location.h queue-policy.h write-policy.h

tests/test_device_lifecycle.o: tests/test_device_lifecycle.c siano-ts.c siano-device.h output-writer.h protocol.h stream-state.h control-parse.h control-input.h device-selector.h detach-decision.h exit-codes.h usb-location.h queue-policy.h write-policy.h

tests/test_capture_gate.o: tests/test_capture_gate.c siano-device.c siano-device.h output-writer.h protocol.h stream-state.h control-parse.h control-input.h device-selector.h detach-decision.h exit-codes.h usb-location.h queue-policy.h write-policy.h

test: build/legacy/siano-ts test-protocol test-clock test-stream-state test-control-parse test-control-input test-usb-location test-detach-decision test-device-selector test-exit-codes test-queue-policy test-write-policy test-capture-sink $(CAPTURE_GATE_TARGET) test-output-integration $(DEVICE_LIFECYCLE_TARGET) $(LIBUSB_MOCK)
	./test-protocol
	./test-clock
	./test-stream-state
	./test-control-parse
	./test-control-input
	./test-usb-location
	./test-detach-decision
	./test-device-selector
	./test-exit-codes
	./test-queue-policy
	./test-write-policy
	./test-capture-sink
	$(CAPTURE_GATE_RUN)
	./test-output-integration
	$(DEVICE_LIFECYCLE_RUN)
	cd build/legacy && ./tests/test_channel.sh
	cd build/legacy && $(LIBUSB_MOCK_LOAD) ./tests/test_cli.sh
	./tests/test-mdev.sh

packaging-test:
	python3 scripts/check-release-version.py --self-test
	python3 scripts/audit-artifact.py --self-test
	python3 scripts/package-source.py --self-test
	python3 -m unittest discover -s tests -p 'test_release_verify.py'
	python3 -m unittest discover -s tests -p 'test_release_runs.py'
	python3 scripts/check-workflow-invariants.py

linux-static:
	scripts/build-linux-static.sh

clean:
	rm -f sianod sianoctl
	rm -f siano-ts test-protocol test-clock test-stream-state test-control-parse test-control-input test-usb-location test-detach-decision test-device-selector test-exit-codes test-queue-policy test-write-policy test-capture-sink test-capture-gate test-output-integration test-device-lifecycle tests/libusb-mock.so *.o tests/*.o tests/.cli-error tests/.list-err tests/.channel-err

# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
# _FILE_OFFSET_BITS=64: 32-bit glibc open()+write() past 2GiB (EFBIG) without
# O_LARGEFILE. No-op on LP64, musl, and Bionic (which already sets O_LARGEFILE).
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags libusb-1.0)
LDLIBS += $(shell $(PKG_CONFIG) --libs libusb-1.0) -pthread

.PHONY: all clean test packaging-test linux-static

# Test-only libusb interposition for dynamically linked libusb; static libusb
# cannot be replaced this way. macOS uses interpose entries in an injected dylib.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LIBUSB_MOCK := tests/libusb-mock.dylib
LIBUSB_MOCK_LDFLAGS := -dynamiclib
LIBUSB_MOCK_LINK_LIBS := $(LDLIBS)
LIBUSB_MOCK_LOAD := DYLD_FORCE_FLAT_NAMESPACE=1 DYLD_INSERT_LIBRARIES=$(CURDIR)/$(LIBUSB_MOCK)
else
LIBUSB_MOCK := tests/libusb-mock.so
LIBUSB_MOCK_LDFLAGS := -shared
LIBUSB_MOCK_LOAD := LD_PRELOAD=$(CURDIR)/$(LIBUSB_MOCK)
endif

all: siano-ts

siano-ts: siano-ts.o protocol.o stream-state.o control-parse.o control-input.o device-selector.o exit-codes.o queue-policy.o output-writer.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

siano-ts.o: siano-ts.c protocol.h stream-state.h control-parse.h control-input.h device-selector.h detach-decision.h exit-codes.h usb-location.h queue-policy.h output-writer.h write-policy.h
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

tests/test_write_policy.o: tests/test_write_policy.c write-policy.h

test-output-integration: tests/test_output_integration.o protocol.o stream-state.o control-parse.o control-input.o device-selector.o exit-codes.o queue-policy.o output-writer.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/test_output_integration.o: tests/test_output_integration.c siano-ts.c output-writer.h protocol.h stream-state.h control-parse.h control-input.h device-selector.h detach-decision.h exit-codes.h usb-location.h queue-policy.h write-policy.h

$(LIBUSB_MOCK): tests/libusb-mock.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC $(LIBUSB_MOCK_LDFLAGS) -o $@ tests/libusb-mock.c $(LIBUSB_MOCK_LINK_LIBS)

test: siano-ts test-protocol test-clock test-stream-state test-control-parse test-control-input test-usb-location test-detach-decision test-device-selector test-exit-codes test-queue-policy test-write-policy test-output-integration $(LIBUSB_MOCK)
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
	./test-output-integration
	./tests/test_channel.sh
	$(LIBUSB_MOCK_LOAD) ./tests/test_cli.sh
	./tests/test-mdev.sh

packaging-test:
	python3 scripts/check-release-version.py --self-test
	python3 scripts/audit-artifact.py --self-test
	python3 scripts/package-source.py --self-test
	python3 scripts/check-workflow-invariants.py

linux-static:
	scripts/build-linux-static.sh

clean:
	rm -f siano-ts test-protocol test-clock test-stream-state test-control-parse test-control-input test-usb-location test-detach-decision test-device-selector test-exit-codes test-queue-policy test-write-policy test-output-integration tests/libusb-mock.so tests/libusb-mock.dylib *.o tests/*.o tests/.cli-error tests/.list-err tests/.channel-err

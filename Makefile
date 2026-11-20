# SPDX-License-Identifier: MIT
CC ?= cc
CFLAGS ?= -O2
CPPFLAGS += -Isrc
WARNINGS = -std=c11 -Wall -Wextra -Werror -pedantic
PREFIX ?= /usr/local
DESTDIR ?=
MACOSX_DEPLOYMENT_TARGET ?= 15.0
export MACOSX_DEPLOYMENT_TARGET

# Only the transport and platform shim vary by OS. Everything above them,
# including IDm validation, card polling and argument parsing, is shared.
ifeq ($(OS),Windows_NT)
PLATFORM = windows
PLATFORM_SRC = src/platform/windows/platform.c
USB_BACKEND = src/platform/windows/usb_winusb.c
PCSC_LIBS = -lwinscard -lwinusb -lsetupapi -lole32
EXE = nfcidm.exe
else
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
PLATFORM = macos
PLATFORM_SRC = src/platform/posix/platform.c
USB_BACKEND = src/platform/macos/usb.c
PCSC_LIBS = -framework IOKit -framework CoreFoundation -framework PCSC
EXE = nfcidm
else ifeq ($(UNAME_S),Linux)
PLATFORM = linux
PLATFORM_SRC = src/platform/posix/platform.c
USB_BACKEND = src/platform/usb_unavailable.c
PCSC_CFLAGS = $(shell pkg-config --cflags libpcsclite 2>/dev/null)
PCSC_LIBS = $(shell pkg-config --libs libpcsclite 2>/dev/null)
EXE = nfcidm
else
$(error Unsupported OS: $(UNAME_S))
endif
endif

COMMON = src/common.c src/usb.c src/frame.c
SONY = src/sony_legacy.c src/sony_rcs380.c
SOURCES = src/main.c $(COMMON) $(SONY) src/pcsc_card.c src/pcsc.c $(PLATFORM_SRC) $(USB_BACKEND)
# Keep the project header named nfc_usb.h: a local usb.h would shadow MinGW <usb.h>.
HEADERS = $(wildcard src/*.h)
TEST_COMMON = $(COMMON) src/platform/posix/platform.c
TESTFLAGS = $(CFLAGS) $(WARNINGS)

ifeq ($(SANITIZE),1)
TESTFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
endif

.PHONY: all check-platform test install uninstall clean dist formula
all: $(EXE)

check-platform:
ifeq ($(PLATFORM),macos)
	@test "$$(uname -m)" = arm64 || { echo 'nfcidm requires Apple Silicon on macOS' >&2; exit 1; }
else ifeq ($(PLATFORM),linux)
	@pkg-config --exists libpcsclite || { echo 'Install pkg-config and libpcsclite-dev (plus pcscd at runtime)' >&2; exit 1; }
else
	@:
endif

$(EXE): $(SOURCES) $(HEADERS) | check-platform
	$(CC) $(CPPFLAGS) $(PCSC_CFLAGS) $(CFLAGS) $(WARNINGS) $(LDFLAGS) $(SOURCES) $(PCSC_LIBS) -o $@

build:
	mkdir -p build

build/test_pcsc_card: tests/test_pcsc_card.c src/pcsc_card.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(TESTFLAGS) tests/test_pcsc_card.c src/pcsc_card.c -o $@

build/test_pcsc_backend: tests/test_pcsc_backend.c src/pcsc.c src/pcsc_card.c tests/pcsc_mock/PCSC/winscard.h $(HEADERS) | build
	# Simulate Apple PC/SC (int32_t/uint32_t, no LONG/DWORD typedefs).
	$(CC) $(CPPFLAGS) -Itests/pcsc_mock -D__APPLE__ $(TESTFLAGS) tests/test_pcsc_backend.c src/pcsc.c src/pcsc_card.c -o $@

build/test_sony_legacy: tests/test_sony_legacy.c src/sony_legacy.c $(TEST_COMMON) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(TESTFLAGS) tests/test_sony_legacy.c src/sony_legacy.c $(TEST_COMMON) -o $@

build/test_sony_rcs380: tests/test_sony_rcs380.c src/sony_rcs380.c $(TEST_COMMON) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(TESTFLAGS) tests/test_sony_rcs380.c src/sony_rcs380.c $(TEST_COMMON) -o $@

build/test_winusb_backend: tests/test_winusb_backend.c src/platform/windows/usb_winusb.c src/common.c src/usb.c $(wildcard tests/winusb_mock/*.h) $(HEADERS) | build
	$(CC) $(CPPFLAGS) -Itests/winusb_mock $(TESTFLAGS) tests/test_winusb_backend.c src/platform/windows/usb_winusb.c src/common.c src/usb.c -o $@

build/test_frame: tests/test_frame.c $(TEST_COMMON) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(TESTFLAGS) tests/test_frame.c $(TEST_COMMON) -o $@

build/test_cli: src/main.c tests/fake_platform.c $(SONY) $(TEST_COMMON) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(TESTFLAGS) src/main.c tests/fake_platform.c $(SONY) $(TEST_COMMON) -o $@

ifeq ($(PLATFORM),windows)
test:
	@echo 'Windows native unit tests are not configured yet; run make test on macOS/Linux for WinUSB mocks.' >&2
else
test: build/test_pcsc_card build/test_pcsc_backend build/test_sony_legacy build/test_sony_rcs380 build/test_frame build/test_winusb_backend build/test_cli
	./build/test_pcsc_card
	./build/test_pcsc_backend
	./build/test_sony_legacy
	./build/test_sony_rcs380
	./build/test_frame
	./build/test_winusb_backend
	sh tests/test_cli.sh ./build/test_cli
endif

install: $(EXE)
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 $(EXE) "$(DESTDIR)$(PREFIX)/bin/$(EXE)"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(EXE)"

dist:
	sh scripts/release.sh

formula:
	@test -n "$(ARCHIVE)" -a -n "$(URL)" || { echo 'make formula ARCHIVE=dist/nfcidm-0.5.3.tar.gz URL=https://...' >&2; exit 1; }
	@sh scripts/formula.sh "$(ARCHIVE)" "$(URL)"

clean:
	rm -rf build nfcidm nfcidm.exe

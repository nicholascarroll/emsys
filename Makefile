PROGNAME=emsys
PREFIX=/usr/local
VERSION?=$(shell git rev-parse --short HEAD 2>/dev/null | sed 's/^/git-/' || echo "")
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/man/man1
OBJECTS=main.o wcwidth.o unicode.o row.o region.o undo.o transform.o bound.o command.o find.o pipe.o tab.o register.o keybindings.o compat.o terminal.o display.o
CFLAGS+=-std=c99 -D_POSIX_C_SOURCE=200112L -Wall -Wno-pointer-sign -fstack-protector-strong -D_FORTIFY_SOURCE=2 -DEMSYS_BUILD_DATE=\"`date '+%Y-%m-%dT%H:%M:%S%z'`\" $(if $(VERSION),-DEMSYS_VERSION=\"$(VERSION)\")

# Platform Detection: 3-Platform Strategy
# 
# emsys supports exactly 3 platforms:
# 1. POSIX   - Default for all Unix systems (Linux, *BSD, macOS, Solaris, AIX, HP-UX, etc.)
# 2. Android - Android/Termux with optimizations  
# 3. MSYS2   - Windows MSYS2 environment
#
# If no platform argument specified or platform is not android/msys2, assume POSIX.

UNAME_S = $(shell uname -s)

# Default: assume POSIX-compliant system
DETECTED_PLATFORM = posix

# Exception 1: Android/Termux detection (multiple methods for reliability)
ifdef ANDROID_ROOT
    DETECTED_PLATFORM = android
endif
ifneq (,$(TERMUX))
    DETECTED_PLATFORM = android
endif
ifeq ($(shell test -d /data/data/com.termux && echo termux),termux)
    DETECTED_PLATFORM = android
endif

# Exception 2: MSYS2 detection  
ifneq (,$(findstring MSYS_NT,$(UNAME_S)))
    DETECTED_PLATFORM = msys2
endif
ifneq (,$(findstring MINGW,$(UNAME_S)))
    DETECTED_PLATFORM = msys2
endif
ifneq (,$(findstring CYGWIN,$(UNAME_S)))
    DETECTED_PLATFORM = msys2
endif

# Allow override via command line: make PLATFORM=android
PLATFORM ?= $(DETECTED_PLATFORM)

# Apply platform-specific settings
ifeq ($(PLATFORM),android)
    CC = clang
    CFLAGS += -O2 -fPIC -fPIE -DNDEBUG -DEMSYS_DISABLE_PIPE
    CRT_DIR = /data/data/com.termux/files/usr/lib
    LINK_CMD = ld -o $(PROGNAME) $(CRT_DIR)/crtbegin_dynamic.o $(OBJECTS) -lc --dynamic-linker=/system/bin/linker64 -L$(CRT_DIR) -pie --gc-sections $(CRT_DIR)/crtend_android.o
endif
ifeq ($(PLATFORM),msys2)
    CFLAGS += -D_GNU_SOURCE
endif
# All other platforms (posix, linux, freebsd, darwin, solaris, aix, etc.) 
# use the default POSIX C99 flags and should work without modification.

all: $(PROGNAME)

debug: CFLAGS+=-g -O0
debug: $(PROGNAME)

$(PROGNAME): config.h $(OBJECTS)
ifeq ($(PLATFORM),android)
	$(LINK_CMD)
else
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)
endif

debug-unicodetest: CFLAGS+=-g -O0
debug-unicodetest: unicodetest

unicodetest: unicode.o unicodetest.o wcwidth.o
	$(CC) -o $@ $^ $(LDFLAGS)

install: $(PROGNAME) $(PROGNAME).1
	mkdir -pv $(BINDIR)
	mkdir -pv $(MANDIR)
	mkdir -pv $(PREFIX)/share/doc/
	install -m 0755 $(PROGNAME) $(BINDIR)
	install -m 0644 README.md $(PREFIX)/share/doc/$(PROGNAME).md
	install -m 0644 $(PROGNAME).1 $(MANDIR)/$(PROGNAME).1

config.h:
	cp config.def.h $@

format:
	clang-format -i *.c *.h

# Specialized build targets
optimized: CFLAGS+=-O3 -march=native -flto
optimized: LDFLAGS+=-flto
optimized: $(PROGNAME)
	@echo "Optimized build complete: $(PROGNAME)"

# Cross-platform testing target
test-platforms:
	@echo "Detected Platform: $(DETECTED_PLATFORM)"
	@echo "Using Platform: $(PLATFORM)"
	@echo "System: $(UNAME_S)"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"

TEST_INC = -Itests -I.

# Test targets
.PHONY: test clean-tests

test: test-unicode test-search-replace test-row test-file-io
	@echo "Running all tests..."
	@failed=0; \
	echo "=== Running test_unicode ==="; \
	if ./test_unicode; then echo "✓ test_unicode: PASSED"; else echo "✗ test_unicode: FAILED"; failed=1; fi; \
	echo "=== Running test_search_replace ==="; \
	if ./test_search_replace; then echo "✓ test_search_replace: PASSED"; else echo "✗ test_search_replace: FAILED"; failed=1; fi; \
	echo "=== Running test_row ==="; \
	if ./test_row; then echo "✓ test_row: PASSED"; else echo "✗ test_row: FAILED"; failed=1; fi; \
	echo "=== Running test_file_io ==="; \
	if ./test_file_io; then echo "✓ test_file_io: PASSED"; else echo "✗ test_file_io: FAILED"; failed=1; fi; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "🎉 All tests PASSED"; \
	else \
		echo "❌ Some tests FAILED"; \
		exit 1; \
	fi


test-unicode: tests/test_unicode.c  unicode.o wcwidth.o
	$(CC) $(CFLAGS) $(TEST_INC) -o test_unicode $^

test-search-replace: tests/test_search_replace.c  command.o row.o unicode.o wcwidth.o transform.o bound.o
	$(CC) $(CFLAGS) $(TEST_INC) -o test_search_replace $^

# row tests - all row-related functionality (management + operations)
test-row: tests/test_row.c tests/test_stubs.c  row.o unicode.o wcwidth.o
	$(CC) $(CFLAGS) $(TEST_INC) -o test_row $^

# file I/O tests - needs file operation functions from main.c
# Compile test_stubs.c with TESTING_FILE_IO to exclude conflicting definitions
test_stubs_io.o: tests/test_stubs.c
	$(CC) $(CFLAGS) -DTESTING_FILE_IO -c -o $@ $<

test-file-io: tests/test_file_io.c test_stubs_io.o main.o row.o unicode.o wcwidth.o undo.o region.o terminal.o command.o transform.o bound.o find.o pipe.o tab.o register.o keybindings.o compat.o display.o
	$(CC) $(CFLAGS) $(TEST_INC) -o test_file_io $^ -Wl,--allow-multiple-definition

clean-tests:
	rm -rf test_unicode test_search_replace test_row test_file_io
	rm -rf test_files
	rm -rf test_stubs_io.o

clean:
	rm -rf *.o
	rm -rf *.exe
	rm -rf $(PROGNAME)
	rm -rf unicodetest

.PHONY: all debug optimized test-platforms clean install format

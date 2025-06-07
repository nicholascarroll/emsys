PROGNAME=emsys
PREFIX=/usr/local
VERSION?=git-`git rev-parse --short HEAD`
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/man/man1
OBJECTS=main.o wcwidth.o unicode.o row.o region.o undo.o transform.o bound.o command.o find.o pipe.o tab.o register.o keybindings.o compat.o terminal.o display.o
CFLAGS+=-std=c99 -D_POSIX_C_SOURCE=200112L -Wall -pedantic -Wno-pointer-sign -fstack-protector-strong -D_FORTIFY_SOURCE=2 -DEMSYS_BUILD_DATE=\"`date '+%Y-%m-%dT%H:%M:%S%z'`\" -DEMSYS_VERSION=\"$(VERSION)\"

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

# Simplified test objects
TEST_OBJS = tests/test_main.o tests/test_helpers.o tests/test_stubs.o
TEST_BIN = run_tests
# Objects without main.o for test builds
TEST_LINK_OBJS = row.o unicode.o wcwidth.o transform.o bound.o

test: $(TEST_LINK_OBJS) $(TEST_OBJS)
	$(CC) -g -O0 -o $(TEST_BIN) $^ $(LDFLAGS)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_LINK_OBJS) $(TEST_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

tests/%.o: tests/%.c
	$(CC) -c -o $@ $< $(CFLAGS) -I.

# Coverage analysis
coverage: clean
	$(CC) $(CFLAGS) --coverage -c $(TEST_LINK_OBJS:.o=.c)
	$(CC) $(CFLAGS) --coverage -I. -c -o tests/test_helpers.o tests/test_helpers.c
	$(CC) $(CFLAGS) --coverage -I. -c -o tests/test_main.o tests/test_main.c
	$(CC) $(CFLAGS) --coverage -I. -c -o tests/test_stubs.o tests/test_stubs.c
	$(CC) --coverage -o $(TEST_BIN) $(TEST_LINK_OBJS) tests/test_helpers.o tests/test_main.o tests/test_stubs.o
	./$(TEST_BIN)
	gcov *.c > coverage_report.txt

clean-tests:
	rm -f $(TEST_BIN) tests/*.o tests/generated/*.o
	rm -f *.gcno *.gcda *.gcov

clean:
	rm -rf *.o
	rm -rf *.exe
	rm -rf $(PROGNAME)
	rm -rf unicodetest
	$(MAKE) clean-tests

.PHONY: all debug optimized test-platforms clean clean-tests test coverage install format

PROGNAME = emsys
PREFIX = /usr/local
SHELL = /bin/sh

# Version handling - fallback for when git is not available
VERSION = 1.0.0

# Standard C99 compiler settings
CC = cc

# Enable BSD and POSIX features portably
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wno-pointer-sign -D_DEFAULT_SOURCE -D_BSD_SOURCE -O2

# Installation directories
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/man/man1
DOCDIR = $(PREFIX)/share/doc/emsys

# Source files
OBJECTS = main.o wcwidth.o unicode.o buffer.o region.o undo.o transform.o \
          find.o pipe.o register.o fileio.o terminal.o display.o \
          keymap.o edit.o prompt.o util.o completion.o history.o

# Default target with git version detection
all:
	@VERSION="`git describe --tags --always --dirty 2>/dev/null || echo $(VERSION)`"; \
	$(MAKE) VERSION="$$VERSION" $(PROGNAME)

# Link the executable
$(PROGNAME): $(OBJECTS)
	$(CC) -o $(PROGNAME) $(OBJECTS) $(LDFLAGS)

# POSIX suffix rule for .c to .o
.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -DEMSYS_VERSION=\"$(VERSION)\" -c $<

# Simple header dependency
$(OBJECTS): config.h

# Copy default config if it doesn't exist
config.h:
	cp config.def.h config.h


# Installation
install: $(PROGNAME)
	mkdir -p $(BINDIR) $(MANDIR) $(DOCDIR)
	cp $(PROGNAME) $(BINDIR)/
	-cp $(PROGNAME).1 $(MANDIR)/ 2>/dev/null
	-cp README.md $(DOCDIR)/ 2>/dev/null
	chmod 755 $(BINDIR)/$(PROGNAME)

# Removal
uninstall:
	rm -f $(BINDIR)/$(PROGNAME)
	rm -f $(MANDIR)/$(PROGNAME).1
	rm -f $(DOCDIR)/README.md
	-rmdir $(DOCDIR) 2>/dev/null

# Cleanup
clean:
	rm -f $(OBJECTS) $(PROGNAME)

distclean: clean
	rm -f config.h

# Testing
test: $(PROGNAME)
	./tests/run_tests.sh

check: test

# Sorry Dave
hal:
	$(MAKE) format
	$(MAKE) clean
	for f in *.c; do clang-tidy $$f -- -I. ; done
	$(MAKE) CFLAGS="$(CFLAGS) -D_POSIX_C_SOURCE=200112L -Werror" $(PROGNAME)
	$(MAKE) test

# Development targets
debug:
	$(MAKE) CFLAGS="$(CFLAGS) -g -O0" $(PROGNAME)


format:
	clang-format -i *.c *.h

# Platform-specific variants
android:
	$(MAKE) CC=clang CFLAGS="$(CFLAGS) -fPIC -fPIE -DEMSYS_DISABLE_PIPE" LDFLAGS="-pie" $(PROGNAME)


msys2:
	$(MAKE) CFLAGS="$(CFLAGS) -D_GNU_SOURCE" $(PROGNAME)

minimal:
	$(MAKE) CFLAGS="$(CFLAGS) -DEMSYS_DISABLE_PIPE -Os" $(PROGNAME)

solaris:
	VERSION="$(VERSION)" $(MAKE) CC=cc CFLAGS="-xc99 -D__EXTENSIONS__ -O2 -errtags=yes -erroff=E_ARG_INCOMPATIBLE_WITH_ARG_L" $(PROGNAME)


darwin:
	$(MAKE) CC=clang CFLAGS="$(CFLAGS) -D_DARWIN_C_SOURCE" $(PROGNAME)



# Help
help:
	@echo "emsys build targets:"
	@echo "  all       Build emsys (default)"
	@echo "  install   Install to PREFIX ($(PREFIX))"
	@echo "  uninstall Remove installed files"
	@echo "  clean     Remove object files"
	@echo "  test      Run basic test"
	@echo "  debug     Build with debug symbols"
	@echo "  android   Build for Android/Termux"
	@echo "  darwin    Build for macOS/Darwin"
	@echo "  msys2     Build for MSYS2"
	@echo "  minimal   Build minimal version"
	@echo "  solaris   Build for Solaris Developer Studio"
	@echo "  check     Alias for test"
	@echo "  format    Format code with clang-format"
	@echo "  hal       HAL-9000 compliance"

/* Stub only terminal I/O, display rendering and user prompts */

#include "../emsys.h"

// Global E is declared in main.o when linking with test_file_io
#ifndef TESTING_FILE_IO
struct editorConfig E;
#endif

// Stub function needed by row.c - not needed when linking with main.o
#ifndef TESTING_FILE_IO
int nextScreenX(char *chars, int *i, int current_screen_x) {
    return current_screen_x + 1;
}
#endif

#include "../emsys.h"
#include "test_framework.h"
#include <string.h>
#include <stdlib.h>

int test_failures = 0;

struct editorBuffer* create_test_buffer() {
    struct editorBuffer *buf = newBuffer();
    return buf;
}

void destroy_test_buffer(struct editorBuffer *buf) {
    if (buf) {
        for (int i = 0; i < buf->numrows; i++) {
            free(buf->row[i].chars);
        }
        free(buf->row);
        free(buf->filename);
        free(buf);
    }
}
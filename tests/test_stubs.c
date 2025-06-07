#include "../emsys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

struct editorConfig E;

/* Function declarations */
void insertRow(struct editorBuffer *buf, int at, char *s, size_t len);
void rowInsertChar(erow *row, int at, int c);

int minibuffer_height = 1;
int statusbar_height = 1;

void die(const char *s) {
    fprintf(stderr, "%s\r\n", s);
    exit(1);
}

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) die("malloc failed");
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *newptr = realloc(ptr, size);
    if (!newptr) die("realloc failed");
    return newptr;
}

char *stringdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *ptr = malloc(len);
    if (!ptr) die("malloc failed");
    memcpy(ptr, s, len);
    return ptr;
}

char *xstrdup(const char *s) {
    return stringdup(s);
}

void invalidateScreenCache(struct editorBuffer *buf) {
    /* No-op for tests */
}

void setStatusMessage(const char *fmt, ...) {
    /* No-op for tests */
}

int nextScreenX(char *chars, int *i, int current_screen_x) {
    /* Simplified for tests */
    return current_screen_x + 1;
}

int windowFocusedIdx(struct editorConfig *ed) {
    return 0;
}

struct editorBuffer *newBuffer() {
    struct editorBuffer *ret = xmalloc(sizeof(struct editorBuffer));
    ret->indent = 0;
    ret->markx = -1;
    ret->marky = -1;
    ret->cx = 0;
    ret->cy = 0;
    ret->numrows = 0;
    ret->row = NULL;
    ret->filename = NULL;
    ret->query = NULL;
    ret->dirty = 0;
    ret->special_buffer = 0;
    ret->undo = NULL;
    ret->redo = NULL;
    ret->next = NULL;
    ret->uarg = 0;
    ret->uarg_active = 0;
    ret->truncate_lines = 0;
    ret->rectangle_mode = 0;
    ret->read_only = 0;
    ret->screen_line_start = NULL;
    ret->screen_line_cache_size = 0;
    ret->screen_line_cache_valid = 0;
    return ret;
}

void insertChar(int c) {
    fprintf(stderr, "insertChar: called with c=%c\n", c);
    fprintf(stderr, "insertChar: E.buf->cy = %d, E.buf->numrows = %d\n", E.buf->cy, E.buf->numrows);
    
    if (E.buf->cy == E.buf->numrows) {
        fprintf(stderr, "insertChar: need new row\n");
        insertRow(E.buf, E.buf->numrows, "", 0);
    }
    
    fprintf(stderr, "insertChar: calling rowInsertChar\n");
    rowInsertChar(&E.buf->row[E.buf->cy], E.buf->cx, c);
    E.buf->cx++;
    E.buf->dirty++;
    fprintf(stderr, "insertChar: done\n");
}

void insertNewline(int indent) {
    if (E.buf->cx == 0) {
        insertRow(E.buf, E.buf->cy, "", 0);
    } else {
        erow *row = &E.buf->row[E.buf->cy];
        insertRow(E.buf, E.buf->cy + 1, (char *)&row->chars[E.buf->cx], row->size - E.buf->cx);
        row = &E.buf->row[E.buf->cy];
        row->size = E.buf->cx;
        row->chars[row->size] = '\0';
    }
    E.buf->cy++;
    E.buf->cx = 0;
}

void setupCommands(void) {
    /* No-op for tests */
}
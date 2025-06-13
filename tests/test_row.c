#define _GNU_SOURCE  // for strdup
#include "test.h"
#include "test_helpers.h"
#include "../emsys.h"
#include "../row.h"  // For insertRow, delRow, rowInsertChar, etc.
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

// Global E is declared in test_stubs.c
extern struct editorConfig E;

// Stub functions needed for row operations
void invalidateScreenCache(struct editorBuffer *buf) {
    if (buf) buf->screen_line_cache_valid = 0;
}

void setStatusMessage(const char *fmt, ...) {
    // Test stub - do nothing
}

// Buffer management functions
struct editorBuffer *newBuffer(void) {
    return calloc(1, sizeof(struct editorBuffer));
}

void destroyBuffer(struct editorBuffer *buf) {
    if (buf) {
        for (int i = 0; i < buf->numrows; i++) {
            free(buf->row[i].chars);
        }
        free(buf->row);
        free(buf->filename);
        free(buf);
    }
}

void setUp(void) {
    // Initialize global E
    memset(&E, 0, sizeof(E));
    E.buf = newBuffer();
    E.buf->numrows = 0;
    E.buf->row = NULL;
    E.minibuf = calloc(1, sizeof(struct editorBuffer));
    E.nwindows = 1;
    E.windows = malloc(sizeof(struct editorWindow*));
    E.windows[0] = calloc(1, sizeof(struct editorWindow));
    E.windows[0]->buf = E.buf;
    E.windows[0]->focused = 1;
    
    // Set up test files directory
    create_test_dir();
}

void tearDown(void) {
    // Clean up global E
    if (E.buf) {
        for (int i = 0; i < E.buf->numrows; i++) {
            free(E.buf->row[i].chars);
        }
        free(E.buf->row);
        free(E.buf);
    }
    if (E.minibuf) {
        for (int i = 0; i < E.minibuf->numrows; i++) {
            free(E.minibuf->row[i].chars);
        }
        free(E.minibuf->row);
        free(E.minibuf);
    }
    if (E.windows) {
        free(E.windows[0]);
        free(E.windows);
    }
    
    // Clean up test files
    cleanup_test_files();
}

// ========== ROW MANAGEMENT TESTS ==========

// Test insertRow function - core editor functionality
void test_insertRow(void) {
    // Start with empty buffer
    TEST_ASSERT_EQUAL(0, E.buf->numrows);
    TEST_ASSERT_NULL(E.buf->row);
    
    // Insert first row
    insertRow(E.buf, 0, "Hello World", 11);
    TEST_ASSERT_EQUAL(1, E.buf->numrows);
    TEST_ASSERT_NOT_NULL(E.buf->row);
    TEST_ASSERT_EQUAL_STRING("Hello World", (char*)E.buf->row[0].chars);
    TEST_ASSERT_EQUAL(11, E.buf->row[0].size);
    
    // Insert second row at end
    insertRow(E.buf, 1, "Line 2", 6);
    TEST_ASSERT_EQUAL(2, E.buf->numrows);
    TEST_ASSERT_EQUAL_STRING("Hello World", (char*)E.buf->row[0].chars);
    TEST_ASSERT_EQUAL_STRING("Line 2", (char*)E.buf->row[1].chars);
    
    // Insert row in middle
    insertRow(E.buf, 1, "Middle", 6);
    TEST_ASSERT_EQUAL(3, E.buf->numrows);
    TEST_ASSERT_EQUAL_STRING("Hello World", (char*)E.buf->row[0].chars);
    TEST_ASSERT_EQUAL_STRING("Middle", (char*)E.buf->row[1].chars);
    TEST_ASSERT_EQUAL_STRING("Line 2", (char*)E.buf->row[2].chars);
}

// Test delRow function - row deletion
void test_delRow(void) {
    // Set up multiple rows
    insertRow(E.buf, 0, "First", 5);
    insertRow(E.buf, 1, "Second", 6);
    insertRow(E.buf, 2, "Third", 5);
    TEST_ASSERT_EQUAL(3, E.buf->numrows);
    
    // Delete middle row
    delRow(E.buf, 1);
    TEST_ASSERT_EQUAL(2, E.buf->numrows);
    TEST_ASSERT_EQUAL_STRING("First", (char*)E.buf->row[0].chars);
    TEST_ASSERT_EQUAL_STRING("Third", (char*)E.buf->row[1].chars);
    
    // Delete first row
    delRow(E.buf, 0);
    TEST_ASSERT_EQUAL(1, E.buf->numrows);
    TEST_ASSERT_EQUAL_STRING("Third", (char*)E.buf->row[0].chars);
    
    // Delete last row
    delRow(E.buf, 0);
    TEST_ASSERT_EQUAL(0, E.buf->numrows);
}

// Test insertRow with local buffer (no global E)
void test_insertRow_local_buffer(void) {
    struct editorBuffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.markx = -1;
    buf.marky = -1;
    
    insertRow(&buf, 0, "Hello", 5);
    TEST_ASSERT_EQUAL(1, buf.numrows);
    TEST_ASSERT_EQUAL_STRING("Hello", (char*)buf.row[0].chars);
    
    insertRow(&buf, 1, "World", 5);
    TEST_ASSERT_EQUAL(2, buf.numrows);
    
    // Cleanup
    while (buf.numrows > 0) {
        delRow(&buf, 0);
    }
}

// Test delRow with local buffer (no global E)
void test_delRow_local_buffer(void) {
    struct editorBuffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.markx = -1;
    buf.marky = -1;
    
    insertRow(&buf, 0, "First", 5);
    insertRow(&buf, 1, "Second", 6);
    insertRow(&buf, 2, "Third", 5);
    
    delRow(&buf, 1);  // Delete middle
    
    TEST_ASSERT_EQUAL(2, buf.numrows);
    TEST_ASSERT_EQUAL_STRING("First", (char*)buf.row[0].chars);
    TEST_ASSERT_EQUAL_STRING("Third", (char*)buf.row[1].chars);
    
    // Cleanup
    while (buf.numrows > 0) {
        delRow(&buf, 0);
    }
}

// ========== ROW OPERATIONS TESTS ==========

// Test rowInsertChar function - character insertion within rows
void test_rowInsertChar(void) {
    // Start with a row
    insertRow(E.buf, 0, "Hello", 5);
    erow *row = &E.buf->row[0];
    
    // Insert character at end
    rowInsertChar(row, 5, ' ');
    TEST_ASSERT_EQUAL(6, row->size);
    TEST_ASSERT_EQUAL_STRING("Hello ", (char*)row->chars);
    
    // Insert character at beginning
    rowInsertChar(row, 0, '>');
    TEST_ASSERT_EQUAL(7, row->size);
    TEST_ASSERT_EQUAL_STRING(">Hello ", (char*)row->chars);
    
    // Insert character in middle
    rowInsertChar(row, 3, 'X');
    TEST_ASSERT_EQUAL(8, row->size);
    TEST_ASSERT_EQUAL_STRING(">HeXllo ", (char*)row->chars);
}

// Test rowDelChar function - character deletion within rows
void test_rowDelChar(void) {
    // Start with a row
    insertRow(E.buf, 0, "Hello World", 11);
    erow *row = &E.buf->row[0];
    
    // Delete character from end
    rowDelChar(row, 10);
    TEST_ASSERT_EQUAL(10, row->size);
    TEST_ASSERT_EQUAL_STRING("Hello Worl", (char*)row->chars);
    
    // Delete character from beginning
    rowDelChar(row, 0);
    TEST_ASSERT_EQUAL(9, row->size);
    TEST_ASSERT_EQUAL_STRING("ello Worl", (char*)row->chars);
    
    // Delete character from middle
    rowDelChar(row, 4);
    TEST_ASSERT_EQUAL(8, row->size);
    TEST_ASSERT_EQUAL_STRING("elloWorl", (char*)row->chars);
}

// Test rowInsertString function
void test_rowInsertString(void) {
    // Start with a row
    insertRow(E.buf, 0, "Hello", 5);
    erow *row = &E.buf->row[0];
    
    // Insert string at end (append)
    rowInsertString(row, row->size, " World", 6);
    TEST_ASSERT_EQUAL(11, row->size);
    TEST_ASSERT_EQUAL_STRING("Hello World", (char*)row->chars);
    
    // Insert string at beginning
    rowInsertString(row, 0, "Say ", 4);
    TEST_ASSERT_EQUAL(15, row->size);
    TEST_ASSERT_EQUAL_STRING("Say Hello World", (char*)row->chars);
    
    // Insert string in middle
    rowInsertString(row, 4, "a Big ", 6);
    TEST_ASSERT_EQUAL(21, row->size);
    TEST_ASSERT_EQUAL_STRING("Say a Big Hello World", (char*)row->chars);
}

// Test UTF-8 handling in rows
void test_utf8_row_operations(void) {
    // Insert row with UTF-8 content
    char *utf8_text = "Hello 世界";
    insertRow(E.buf, 0, utf8_text, strlen(utf8_text));
    
    TEST_ASSERT_EQUAL(1, E.buf->numrows);
    TEST_ASSERT_EQUAL_STRING(utf8_text, (char*)E.buf->row[0].chars);
    
    // Insert UTF-8 character into existing row
    erow *row = &E.buf->row[0];
    // Insert café at the end: é is 2 bytes in UTF-8
    char cafe[] = {0xC3, 0xA9, 0}; // "é" in UTF-8
    rowInsertChar(row, row->size, cafe[0]);
    rowInsertChar(row, row->size, cafe[1]);
    
    // The string should now contain the UTF-8 character
    TEST_ASSERT_TRUE(row->size >= strlen(utf8_text) + 2);
}

int main(void) {
    TEST_BEGIN();
    
    // Row management tests
    RUN_TEST(test_insertRow);
    RUN_TEST(test_delRow);
    RUN_TEST(test_insertRow_local_buffer);
    RUN_TEST(test_delRow_local_buffer);
    
    // Row operations tests
    RUN_TEST(test_rowInsertChar);
    RUN_TEST(test_rowDelChar);
    RUN_TEST(test_rowInsertString);
    RUN_TEST(test_utf8_row_operations);
    
    return TEST_END();
}
#define _GNU_SOURCE  // for strdup
#include "test.h"
#include "test_helpers.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "../emsys.h"
#include "../row.h"  // for insertRow, delRow


void editorOpenFile(struct editorBuffer *buf, char *filename);
void save(struct editorBuffer *buf);
char *rowsToString(struct editorBuffer *buf, int *buflen);

extern struct editorConfig E;

void setUp(void) {
    memset(&E, 0, sizeof(E));
    E.buf = malloc(sizeof(struct editorBuffer));
    memset(E.buf, 0, sizeof(struct editorBuffer));
    E.buf->markx = -1;
    E.buf->marky = -1;
    create_test_dir();
}

void tearDown(void) {
    if (E.buf) {
        while (E.buf->numrows > 0) {
            delRow(E.buf, 0);
        }
        free(E.buf->filename);
        free(E.buf);
    }
    cleanup_test_files();
}

void test_editorOpenFile_basic(void) {
    create_test_file("test_files/basic.txt", "Line 1\nLine 2\nLine 3");
    
    editorOpenFile(E.buf, "test_files/basic.txt");
    
    TEST_ASSERT_EQUAL(3, E.buf->numrows);
    TEST_ASSERT_EQUAL_STRING("Line 1", (char*)E.buf->row[0].chars);
    TEST_ASSERT_EQUAL_STRING("Line 2", (char*)E.buf->row[1].chars);
    TEST_ASSERT_EQUAL_STRING("Line 3", (char*)E.buf->row[2].chars);
    TEST_ASSERT_EQUAL_STRING("test_files/basic.txt", E.buf->filename);
    TEST_ASSERT_FALSE(E.buf->dirty);
}

void test_save_basic(void) {
    insertRow(E.buf, 0, "Save test", 9);
    E.buf->filename = strdup("test_files/save_test.txt");
    E.buf->dirty = 1;
    
    save(E.buf);
    
    TEST_ASSERT_FALSE(E.buf->dirty);
    
    // Verify file exists
    FILE *fp = fopen("test_files/save_test.txt", "r");
    TEST_ASSERT_NOT_NULL(fp);
    
    char line[100];
    fgets(line, sizeof(line), fp);
    TEST_ASSERT_EQUAL_STRING("Save test\n", line);
    fclose(fp);
}

void test_rowsToString_basic(void) {
    insertRow(E.buf, 0, "First", 5);
    insertRow(E.buf, 1, "Second", 6);
    
    int buflen;
    char *result = rowsToString(E.buf, &buflen);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL(13, buflen);  // "First\nSecond\n"
    TEST_ASSERT_EQUAL_STRING("First\nSecond\n", result);
    
    free(result);
}

int main(void) {
    TEST_BEGIN();
    RUN_TEST(test_editorOpenFile_basic);
    RUN_TEST(test_save_basic);
    RUN_TEST(test_rowsToString_basic);
    return TEST_END();
}

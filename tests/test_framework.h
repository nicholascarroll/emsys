#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

extern int test_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        test_failures++; \
    } \
} while(0)

#define CHECK_STR(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        printf("FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
               __FILE__, __LINE__, (expected), (actual)); \
        test_failures++; \
    } \
} while(0)

#endif

void test_transformerUpcase_mixed(void) {
    uint8_t *input = stringdup("Hello WoRLd123");
    uint8_t *result = transformerUpcase(input);
    ASSERT_STR_EQ("HELLO WORLD123", (char*)result);
    free(input);
    free(result);
}





void test_transformerCapitalCase_basic(void) {
    uint8_t *input = stringdup("hello world test");
    uint8_t *result = transformerCapitalCase(input);
    ASSERT_STR_EQ("Hello World Test", (char*)result);
    free(input);
    free(result);
}

void test_transformerCapitalCase_punctuation(void) {
    uint8_t *input = stringdup("hello, world! test-case");
    uint8_t *result = transformerCapitalCase(input);
    ASSERT_STR_EQ("Hello, World! Test-Case", (char*)result);
    free(input);
    free(result);
}

void test_transformerTransposeChars_basic(void) {
    uint8_t *input = stringdup("hello");
    uint8_t *result = transformerTransposeChars(input);
    ASSERT_STR_EQ("ello", (char*)result);
    free(input);
    free(result);
}


void test_transformerTransposeWords_basic(void) {
    uint8_t *input = stringdup("hello world");
    uint8_t *result = transformerTransposeWords(input);
    ASSERT_STR_EQ("world hello", (char*)result);
    free(input);
    free(result);
}


void test_stringdup_basic(void) {
    char *result = stringdup("hello");
    ASSERT_STR_EQ("hello", result);
    
    char *result2 = stringdup("world");
    ASSERT_STR_EQ("world", result2);
    
    char *empty = stringdup("");
    ASSERT_STR_EQ("", empty);
    
    char *single = stringdup("a");
    ASSERT_STR_EQ("a", single);
    
    free(result);
    free(result2);
    free(empty);
    free(single);
}



void test_isParaBoundary_non_empty_row(void) {
    uint8_t chars[] = "hello";
    erow row = {5, chars, 0, 0};
    ASSERT(!isParaBoundary(&row));
}


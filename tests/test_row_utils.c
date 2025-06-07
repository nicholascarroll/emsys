void test_calculateLineWidth_simple(void) {
    uint8_t chars[] = "hello";
    erow row = {5, chars, -1, 0};  // width_valid = 0, cached_width = -1
    int width = calculateLineWidth(&row);
    ASSERT_EQ(5, width);
    ASSERT_EQ(1, row.width_valid);  // Should be cached now
    ASSERT_EQ(5, row.cached_width);
}

void test_calculateLineWidth_cached(void) {
    uint8_t chars[] = "hello";
    erow row = {5, chars, 10, 1};  // Already cached with width 10
    int width = calculateLineWidth(&row);
    ASSERT_EQ(10, width);  // Should return cached value
}


void test_charsToDisplayColumn_simple(void) {
    uint8_t chars[] = "hello";
    erow row = {5, chars, -1, 0};
    ASSERT_EQ(0, charsToDisplayColumn(&row, 0));
    ASSERT_EQ(1, charsToDisplayColumn(&row, 1));
    ASSERT_EQ(5, charsToDisplayColumn(&row, 5));
}

void test_charsToDisplayColumn_with_tab(void) {
    uint8_t chars[] = "a\tb";
    erow row = {3, chars, -1, 0};
    int col0 = charsToDisplayColumn(&row, 0);  // 'a'
    int col1 = charsToDisplayColumn(&row, 1);  // '\t'
    int col2 = charsToDisplayColumn(&row, 2);  // 'b'
    
    ASSERT_EQ(0, col0);
    ASSERT(col1 > 1);  // Tab should advance beyond position 1
    ASSERT(col2 > col1);  // 'b' should be after tab
}


void test_displayColumnToChars_simple(void) {
    uint8_t chars[] = "hello";
    erow row = {5, chars, -1, 0};
    ASSERT_EQ(0, displayColumnToChars(&row, 0));
    ASSERT_EQ(1, displayColumnToChars(&row, 1));
    ASSERT_EQ(5, displayColumnToChars(&row, 5));
}

void test_displayColumnToChars_beyond_end(void) {
    uint8_t chars[] = "hello";
    erow row = {5, chars, -1, 0};
    ASSERT_EQ(5, displayColumnToChars(&row, 100));  // Should clamp to end
}


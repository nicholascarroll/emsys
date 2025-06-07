



void test_stringWidth_safe_check(void) {
    // Test that we can call stringWidth safely
    uint8_t str[] = "test";
    int width = stringWidth(str);
    ASSERT(width > 0);
}



void test_charInStringWidth_bounds_check(void) {
    uint8_t str[] = "hello";
    // Test within bounds
    ASSERT_EQ(1, charInStringWidth(str, 0));  // 'h' 
    ASSERT_EQ(1, charInStringWidth(str, 4));  // 'o'
    // Note: function doesn't validate bounds, so we test valid indices only
}


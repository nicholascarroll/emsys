void test_insertChar_basic(void) {
    fprintf(stderr, "test_insertChar_basic: starting\n");
    fprintf(stderr, "test_insertChar_basic: E.buf = %p\n", (void*)E.buf);
    
    insertChar('H');
    fprintf(stderr, "test_insertChar_basic: inserted H\n");
    
    insertChar('i');
    fprintf(stderr, "test_insertChar_basic: inserted i\n");
    
    fprintf(stderr, "test_insertChar_basic: numrows = %d\n", E.buf->numrows);
    ASSERT_EQ(1, E.buf->numrows);
    ASSERT_EQ(2, E.buf->cx);
    ASSERT_STR_EQ("Hi", E.buf->row[0].chars);
}
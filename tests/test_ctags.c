/* Copyright (c) 2026 Nicholas Carroll. SPDX-License-Identifier: MIT */
/* test_ctags.c: Tests for ctags tags-file discovery and path
 * resolution.
 */

#include "test.h"
#include "test_harness.h"
#include "ctags.h"
#include "util.h" /* emil_strlcpy prototype — without it the call at
		    * make_project() is an implicit declaration (assumed
		    * int return), which is invalid C99 and trips
		    * -Wsign-compare on the size_t comparison. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

void setUp(void) {
	initTestEditor();
}
void tearDown(void) {
	cleanupTestEditor();
}

/* ---- resolveTagPath: pure string join ---- */

void test_resolve_relative_joins_onto_tagsdir(void) {
	char out[PATH_MAX];
	int rc = resolveTagPath("/proj", "src/foo.c", out, sizeof(out));
	TEST_ASSERT_EQUAL_INT(0, rc);
	TEST_ASSERT_EQUAL_STRING("/proj/src/foo.c", out);
}

void test_resolve_bare_filename(void) {
	char out[PATH_MAX];
	resolveTagPath("/proj", "foo.c", out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("/proj/foo.c", out);
}

void test_resolve_absolute_passes_through(void) {
	/* An absolute tag path must NOT be joined onto tagsdir. */
	char out[PATH_MAX];
	resolveTagPath("/proj", "/usr/include/stdio.h", out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("/usr/include/stdio.h", out);
}

void test_resolve_home_passes_through(void) {
	char out[PATH_MAX];
	resolveTagPath("/proj", "~/foo.c", out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("~/foo.c", out);
}

void test_resolve_root_tagsdir(void) {
	char out[PATH_MAX];
	resolveTagPath("/", "src/foo.c", out, sizeof(out));
	/* Joining onto "/" yields a leading double slash; that's a
	 * benign path form (POSIX treats "//src" as "/src") and is what
	 * the production join produces, so lock it in rather than
	 * special-casing. */
	TEST_ASSERT_EQUAL_STRING("//src/foo.c", out);
}

void test_resolve_truncation_reported(void) {
	char out[16];
	int rc = resolveTagPath("/a/very/long/directory/name",
				"deep/nested/path/file.c", out, sizeof(out));
	TEST_ASSERT_EQUAL_INT(-1, rc);
}

void test_resolve_absolute_truncation_reported(void) {
	char out[8];
	int rc = resolveTagPath("/x", "/usr/include/stdio.h", out,
				sizeof(out));
	TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* ---- findTagsDir: upward search on a real temp tree ---- */

/* Build a temp project:  <root>/tags  and <root>/src/deep/  */
static int make_project(char *root, size_t rootsz) {
	char tmpl[] = "/tmp/emil_ctags_XXXXXX";
	char *made = mkdtemp(tmpl);
	if (!made)
		return -1;
	if (emil_strlcpy(root, made, rootsz) >= rootsz)
		return -1;

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/tags", root);
	FILE *fp = fopen(path, "w");
	if (!fp)
		return -1;
	fputs("helper_fn\tsrc/foo.c\t/^int helper_fn(void)$/;\"\tf\n", fp);
	fclose(fp);

	snprintf(path, sizeof(path), "%s/src", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/src/deep", root);
	mkdir(path, 0755);
	return 0;
}

static void rm_project(const char *root) {
	char cmd[PATH_MAX + 16];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
	int r = system(cmd);
	(void)r;
}

void test_find_tags_from_root(void) {
	char root[PATH_MAX];
	if (make_project(root, sizeof(root)) != 0) {
		TEST_ASSERT(0 && "could not create temp project");
		return;
	}
	char saved[PATH_MAX];
	TEST_ASSERT_NOT_NULL(getcwd(saved, sizeof(saved)));

	TEST_ASSERT_EQUAL_INT(0, chdir(root));
	char found[PATH_MAX];
	int rc = findTagsDir(found, sizeof(found));
	TEST_ASSERT_EQUAL_INT(0, rc);
	/* Resolve both sides through a real path to dodge /tmp ->
	 * /private/tmp style symlinks on some platforms. */
	char rp_found[PATH_MAX], rp_root[PATH_MAX];
	TEST_ASSERT_NOT_NULL(realpath(found, rp_found));
	TEST_ASSERT_NOT_NULL(realpath(root, rp_root));
	TEST_ASSERT_EQUAL_STRING(rp_root, rp_found);

	TEST_ASSERT_EQUAL_INT(0, chdir(saved));
	rm_project(root);
}

void test_find_tags_from_subdir(void) {
	/* The case that used to fail entirely. */
	char root[PATH_MAX];
	if (make_project(root, sizeof(root)) != 0) {
		TEST_ASSERT(0 && "could not create temp project");
		return;
	}
	char saved[PATH_MAX];
	TEST_ASSERT_NOT_NULL(getcwd(saved, sizeof(saved)));

	/* Sized to hold the longest suffix appended below, so the
	 * compiler can see the result always fits. */
	char sub[PATH_MAX + 16];
	snprintf(sub, sizeof(sub), "%s/src/deep", root);
	TEST_ASSERT_EQUAL_INT(0, chdir(sub));

	char found[PATH_MAX];
	int rc = findTagsDir(found, sizeof(found));
	TEST_ASSERT_EQUAL_INT(0, rc);
	char rp_found[PATH_MAX], rp_root[PATH_MAX];
	TEST_ASSERT_NOT_NULL(realpath(found, rp_found));
	TEST_ASSERT_NOT_NULL(realpath(root, rp_root));
	TEST_ASSERT_EQUAL_STRING(rp_root, rp_found);

	/* End-to-end: resolve the tag's stored path against the found
	 * directory and confirm it points at the right absolute file. */
	char resolved[PATH_MAX];
	resolveTagPath(found, "src/foo.c", resolved, sizeof(resolved));
	char expect[PATH_MAX + 16];
	snprintf(expect, sizeof(expect), "%s/src/foo.c", found);
	TEST_ASSERT_EQUAL_STRING(expect, resolved);

	TEST_ASSERT_EQUAL_INT(0, chdir(saved));
	rm_project(root);
}

void test_find_tags_absent(void) {
	/* A directory tree with no tags file anywhere up to root must
	 * report failure, not succeed with garbage. */
	char tmpl[] = "/tmp/emil_notags_XXXXXX";
	char *dir = mkdtemp(tmpl);
	TEST_ASSERT_NOT_NULL(dir);
	char saved[PATH_MAX];
	TEST_ASSERT_NOT_NULL(getcwd(saved, sizeof(saved)));
	TEST_ASSERT_EQUAL_INT(0, chdir(dir));

	/* NOTE: this assumes no "tags" file exists in any ancestor of a
	 * fresh /tmp/xxx directory, which holds on normal systems. */
	char found[PATH_MAX];
	int rc = findTagsDir(found, sizeof(found));
	TEST_ASSERT_EQUAL_INT(-1, rc);

	TEST_ASSERT_EQUAL_INT(0, chdir(saved));
	rm_project(dir);
}

/* ---- ctagsWordAtPoint: ASCII identifiers and Thai words ---- */

/* "ภาษา" ZWSP "ไทย" ZWSP "ง่าย" -- three words, 12 + 3 + 9 + 3 + 12
 * bytes.  Offsets below are byte offsets into this row. */
#define ZW "\xe2\x80\x8b"
static const char *thai_row = "ภาษา" ZW "ไทย" ZW "ง่าย";

static char *word_at(const char *line, int cx) {
	struct buffer *buf = make_test_buffer(line);
	buf->cx = cx;
	return ctagsWordAtPoint();
}

void test_word_ascii_identifier_unchanged(void) {
	char *w = word_at("foo_bar(x)", 2);
	TEST_ASSERT_EQUAL_STRING("foo_bar", w);
	free(w);
}

void test_word_ascii_just_after_identifier(void) {
	char *w = word_at("foo_bar(x)", 7);
	TEST_ASSERT_EQUAL_STRING("foo_bar", w);
	free(w);
}

void test_word_none_on_punctuation(void) {
	struct buffer *buf = make_test_buffer("a + b");
	buf->cx = 2;
	char *w = ctagsWordAtPoint();
	TEST_ASSERT_NULL(w);
}

void test_word_thai_first_word(void) {
	char *w = word_at(thai_row, 3); /* on the second codepoint */
	TEST_ASSERT_EQUAL_STRING("ภาษา", w);
	free(w);
}

void test_word_thai_middle_word_bounded_by_zwsp(void) {
	char *w = word_at(thai_row, 15); /* on ไ */
	TEST_ASSERT_EQUAL_STRING("ไทย", w);
	free(w);
}

void test_word_thai_on_zwsp_takes_word_before(void) {
	char *w = word_at(thai_row, 12); /* on the first ZWSP */
	TEST_ASSERT_EQUAL_STRING("ภาษา", w);
	free(w);
}

void test_word_thai_with_tone_mark_at_end_of_row(void) {
	int len = (int)strlen(thai_row);
	char *w = word_at(thai_row, len); /* end of row, after ย */
	TEST_ASSERT_EQUAL_STRING("ง่าย", w);
	free(w);
}

void test_word_thai_mai_yamok_is_a_boundary(void) {
	char *w = word_at("ต่างๆ", 0);
	TEST_ASSERT_EQUAL_STRING("ต่าง", w);
	free(w);
}

void test_word_thai_paiyannoi_is_part_of_word(void) {
	char *w = word_at("กรุงเทพฯ", 0);
	TEST_ASSERT_EQUAL_STRING("กรุงเทพฯ", w);
	free(w);
}

void test_word_thai_digits_are_a_boundary(void) {
	char *w = word_at("ปี๒๕๖๙", 0);
	TEST_ASSERT_EQUAL_STRING("ปี", w);
	free(w);
}

void test_word_thai_next_to_ascii(void) {
	char *w = word_at("(ภาษา)", 1);
	TEST_ASSERT_EQUAL_STRING("ภาษา", w);
	free(w);
}

int main(void) {
	TEST_BEGIN();

	RUN_TEST(test_resolve_relative_joins_onto_tagsdir);
	RUN_TEST(test_resolve_bare_filename);
	RUN_TEST(test_resolve_absolute_passes_through);
	RUN_TEST(test_resolve_home_passes_through);
	RUN_TEST(test_resolve_root_tagsdir);
	RUN_TEST(test_resolve_truncation_reported);
	RUN_TEST(test_resolve_absolute_truncation_reported);
	RUN_TEST(test_find_tags_from_root);
	RUN_TEST(test_find_tags_from_subdir);
	RUN_TEST(test_find_tags_absent);
	RUN_TEST(test_word_ascii_identifier_unchanged);
	RUN_TEST(test_word_ascii_just_after_identifier);
	RUN_TEST(test_word_none_on_punctuation);
	RUN_TEST(test_word_thai_first_word);
	RUN_TEST(test_word_thai_middle_word_bounded_by_zwsp);
	RUN_TEST(test_word_thai_on_zwsp_takes_word_before);
	RUN_TEST(test_word_thai_with_tone_mark_at_end_of_row);
	RUN_TEST(test_word_thai_mai_yamok_is_a_boundary);
	RUN_TEST(test_word_thai_paiyannoi_is_part_of_word);
	RUN_TEST(test_word_thai_digits_are_a_boundary);
	RUN_TEST(test_word_thai_next_to_ascii);

	return TEST_END();
}

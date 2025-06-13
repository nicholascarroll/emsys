#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include "keybindings.h"
#include "emsys.h"
#include "region.h"
#include "find.h"
#include "command.h"
#include "pipe.h"
#include "register.h"
#include "tab.h"
#include "undo.h"
#include "transform.h"

extern const int page_overlap;
extern void moveCursor(struct editorBuffer *buf, int key);
extern void insertChar(int c);
extern void insertNewline(int times);
extern void backSpace(int times);
extern void delChar(int times);
extern void save(struct editorBuffer *buf);
extern void killLine(int times);
extern void openLine(int times);
extern void recenter(struct editorWindow *win);
extern void forwardWord(int times);
extern void backWord(int times);
extern void forwardPara(int times);
extern void backPara(int times);
extern void deleteWord(int times);
extern void backspaceWord(int times);
extern void upcaseWord(int times);
extern void downcaseWord(int times);
extern void capitalCaseWord(int times);
extern void transposeChars(void);
extern void transposeWords(void);
extern void gotoLine(void);
extern void killLineBackwards(int times);
extern void toggleTruncateLines(void);
extern void editorToggleReadOnly(void);
extern void pipeCmd(void);
extern void completeWord(void);
extern void undoAppendChar(uint8_t c);
extern void undoAppendUnicode(void);
extern void insertUnicode(void);
extern void execMacro(struct editorMacro *macro);
extern int windowFocusedIdx(struct editorConfig *ed);
extern void setStatusMessage(const char *fmt, ...);
extern void refreshScreen();
extern int readKey(void);
extern void recordKey(int c);
extern void editorOpenFile(struct editorBuffer *buf, char *filename);
extern struct editorBuffer *newBuffer();
extern void stringRectangle(void);
extern int calculateRowsToScroll(struct editorBuffer *buf,
				 struct editorWindow *win, int direction);
extern void copyRectangle(void);
extern void killRectangle(void);
extern void yankRectangle(void);
extern void switchToNamedBuffer(struct editorConfig *ed,
				struct editorBuffer *buf);
extern void switchWindow(void);
extern uint8_t *
promptUser(struct editorBuffer *buf, uint8_t *prompt, enum promptType t,
	   void (*callback)(struct editorBuffer *, uint8_t *, int));
extern void destroyBuffer(struct editorBuffer *buf);
extern void whatCursor(void);
extern void describeKey(void);
extern void viewManPage(void);
extern void helpForHelp(void);
extern void jumpToRegister(void);
extern void macroToRegister(void);
extern void pointToRegister(void);
extern void numberToRegister(int rept);
extern void regionToRegister(void);
extern void incrementRegister(void);
extern void insertRegister(void);
extern void rectRegister(void);

extern struct editorConfig E;

// Helper function to get and consume universal argument repeat count
static int getUArgRepeat(void) {
	int rept = 1;
	if (E.uarg_active) {
		E.uarg_active = 0;
		rept = E.uarg;
		E.uarg = 0;
	}
	return rept;
}

static void forward_char(void) {
    int rept = getUArgRepeat();
    for (int i = 0; i < rept; i++)
        moveCursor(E.buf, ARROW_RIGHT);
}

static void backward_char(void) {
    int rept = getUArgRepeat();
    for (int i = 0; i < rept; i++)
        moveCursor(E.buf, ARROW_LEFT);
}

static void next_line(void) {
    int rept = getUArgRepeat();
    for (int i = 0; i < rept; i++)
        moveCursor(E.buf, ARROW_DOWN);
}

static void previous_line(void) {
    int rept = getUArgRepeat();
    for (int i = 0; i < rept; i++)
        moveCursor(E.buf, ARROW_UP);
}

static void set_mark_wrapper(void) {
    setMark();
}

static void find_wrapper(void) {
    find(E.buf);
}

static void regex_find_wrapper(void) {
    regexFind(E.buf);
}

static void save_wrapper(void) {
    save(E.buf);
}

static void beginning_of_line(void) {
    E.buf->cx = 0;
}

static void end_of_line(void) {
    struct editorBuffer *buf = E.buf;
    if (buf->cy < buf->numrows)
        buf->cx = buf->row[buf->cy].size;
}

static enum {
	PREFIX_NONE,
	PREFIX_CTRL_X,
	PREFIX_CTRL_X_R,
	PREFIX_META
} prefix_state = PREFIX_NONE;

static int is_recording = 0;

static void handle_newline(void) {
	int rept = getUArgRepeat();
	for (int i = 0; i < rept; i++) {
		undoAppendChar('\n');
		insertNewline(1);
	}
}

static void handle_kill_region(void) {
	killRegion();
	clearMark();
}

static void handle_mark_rectangle(void) {
	markRectangle();
}

static void handle_keyboard_quit(void) {
	clearMark();
	setStatusMessage("Quit");
}

static void handle_recenter(void) {
	int winIdx = windowFocusedIdx(&E);
	recenter(E.windows[winIdx]);
}

static void handle_quoted_insert(void) {
	int rept = getUArgRepeat();
	int c;
	while (read(STDIN_FILENO, &c, 1) != 1) {
		if (errno != EAGAIN)
			die("read");
	}
	for (int i = 0; i < rept; i++) {
		undoAppendChar(c);
		insertChar(c);
	}
}

static void handle_tab(void) {
	int rept = getUArgRepeat();
	struct editorBuffer *buf = E.buf;
	for (int i = 0; i < rept; i++) {
		if (buf->indent > 0) {
			int origx = buf->cx;
			for (int j = 0; j < buf->indent; j++) {
				undoAppendChar(' ');
				insertChar(' ');
			}
			buf->cx = origx + buf->indent;
		} else {
			undoAppendChar('\t');
			insertChar('\t');
		}
	}
}

static void handle_suspend(void) {
	raise(SIGTSTP);
}

static void handle_page_up(void) {
	struct editorBuffer *buf = E.buf;
	int winIdx = windowFocusedIdx(&E);
	struct editorWindow *win = E.windows[winIdx];

	int page_size = win->height - page_overlap;
	if (page_size < 1)
		page_size = 1;

	if (win->rowoff == 0) {
		buf->cy = 0;
		buf->cx = 0;
		win->rowoff = 0;
		win->coloff = 0;
		return;
	}

	int cursor_in_window = buf->cy - win->rowoff;

	win->rowoff -= page_size;
	if (win->rowoff < 0)
		win->rowoff = 0;

	if (cursor_in_window >= win->height - page_overlap) {
		buf->cy = win->rowoff + win->height - 1;
	} else {
		buf->cy = win->rowoff + win->height - 1;
	}

	if (buf->cy >= buf->numrows) {
		buf->cy = buf->numrows > 0 ? buf->numrows - 1 : 0;
	}
	if (buf->cy < buf->numrows && buf->cx > buf->row[buf->cy].size) {
		buf->cx = buf->row[buf->cy].size;
	}
}

static void handle_page_down(void) {
	struct editorBuffer *buf = E.buf;
	int winIdx = windowFocusedIdx(&E);
	struct editorWindow *win = E.windows[winIdx];

	int page_size = win->height - page_overlap;
	if (page_size < 1)
		page_size = 1;

	int max_rowoff = buf->numrows - win->height;
	if (max_rowoff < 0)
		max_rowoff = 0;

	if (win->rowoff >= max_rowoff) {
		if (buf->numrows > 0) {
			buf->cy = buf->numrows - 1;
			buf->cx = buf->row[buf->cy].size;

			win->rowoff = buf->numrows - win->height;
			if (win->rowoff < 0) {
				win->rowoff = 0;
			}
			win->coloff = 0;
		} else {
			buf->cy = 0;
			buf->cx = 0;
		}
		return;
	}

	int cursor_in_window = buf->cy - win->rowoff;

	win->rowoff += page_size;

	if (win->rowoff > max_rowoff) {
		win->rowoff = max_rowoff;
	}

	if (cursor_in_window < page_overlap) {
		buf->cy = win->rowoff;
	} else {
		buf->cy = win->rowoff;
	}

	if (buf->cy >= buf->numrows) {
		buf->cy = buf->numrows > 0 ? buf->numrows - 1 : 0;
		buf->cx = 0;
	} else {
		if (buf->cy < buf->numrows &&
		    buf->cx > buf->row[buf->cy].size) {
			buf->cx = buf->row[buf->cy].size;
		}
	}
}

static void handle_quit(void) {
	if (E.buf == E.minibuf) {
		setStatusMessage("Cannot do this from minibuffer");
		return;
	}
	struct editorBuffer *b = E.headbuf;
	while (b) {
		if (b->dirty && b->filename && !b->special_buffer) {
			setStatusMessage(
				"There are unsaved changes. Really quit? (y or n)");
			refreshScreen();
			if (readKey() == 'y')
				exit(0);
			setStatusMessage("");
			return;
		}
		b = b->next;
	}
	exit(0);
}

static void handle_unicode(void) {
	int rept = getUArgRepeat();
	for (int i = 0; i < rept; i++) {
		undoAppendUnicode();
		insertUnicode();
	}
}

static void handle_unicode_error(void) {
	setStatusMessage("Bad UTF-8 sequence");
}

static void handle_beginning_of_buffer(void) {
	struct editorBuffer *buf = E.buf;
	buf->cy = 0;
	buf->cx = 0;

	int winIdx = windowFocusedIdx(&E);
	E.windows[winIdx]->rowoff = 0;
	E.windows[winIdx]->coloff = 0;
}

static void handle_end_of_buffer(void) {
	struct editorBuffer *buf = E.buf;
	if (buf->numrows > 0) {
		buf->cy = buf->numrows - 1;
		buf->cx = buf->row[buf->cy].size;

		int winIdx = windowFocusedIdx(&E);
		struct editorWindow *win = E.windows[winIdx];
		win->rowoff = buf->numrows - win->height;
		if (win->rowoff < 0) {
			win->rowoff = 0;
		}
		win->coloff = 0;
	} else {
		buf->cy = 0;
		buf->cx = 0;
	}
}

static void handle_goto_line(void) {
	gotoLine();
}

static void handle_backtab(void) {
	completeWord();
}

static void handle_what_cursor(void) {
	setStatusMessage("What cursor position not implemented");
}

static void handle_expand(void) {
	completeWord();
}

static void handle_redo(void) {
	E.micro = REDO;
}

static void handle_copy_region(void) {
	copyRegion();
	clearMark();
}

static void handle_yank(void) {
	int rept = getUArgRepeat();
	yank(rept);
}

// Wrapper functions for main.c functions that still take parameters
static void handle_openLine(void) {
	int rept = getUArgRepeat();
	openLine(rept);
}

static void handle_killLine(void) {
	int rept = getUArgRepeat();
	killLine(rept);
}

static void handle_doUndo(void) {
	int rept = getUArgRepeat();
	doUndo(rept);
}

static void handle_delChar(void) {
	int rept = getUArgRepeat();
	delChar(rept);
}

static void handle_killLineBackwards(void) {
	int rept = getUArgRepeat();
	killLineBackwards(rept);
}

static void handle_backSpace(void) {
	int rept = getUArgRepeat();
	backSpace(rept);
}

// Word movement wrappers
static void handle_forwardWord(void) {
	int rept = getUArgRepeat();
	forwardWord(rept);
}

static void handle_backWord(void) {
	int rept = getUArgRepeat();
	backWord(rept);
}

static void handle_deleteWord(void) {
	int rept = getUArgRepeat();
	deleteWord(rept);
}

static void handle_backspaceWord(void) {
	int rept = getUArgRepeat();
	backspaceWord(rept);
}

static void handle_backPara(void) {
	int rept = getUArgRepeat();
	backPara(rept);
}

static void handle_forwardPara(void) {
	int rept = getUArgRepeat();
	forwardPara(rept);
}

static void handle_upcaseWord(void) {
	int rept = getUArgRepeat();
	upcaseWord(rept);
}

static void handle_downcaseWord(void) {
	int rept = getUArgRepeat();
	downcaseWord(rept);
}

static void handle_capitalCaseWord(void) {
	int rept = getUArgRepeat();
	capitalCaseWord(rept);
}

static void handle_swap_mark(void) {
	struct editorBuffer *buf = E.buf;
	if (buf->markx >= 0 && buf->marky >= 0) {
		int tx = buf->cx, ty = buf->cy;
		buf->cx = buf->markx;
		buf->cy = buf->marky;
		buf->markx = tx;
		buf->marky = ty;
	}
}

static void handle_find_file_impl(int read_only) {
	if (E.buf == E.minibuf) {
		setStatusMessage("Cannot do this from minibuffer");
		return;
	}
	uint8_t *prompt =
		promptUser(E.buf, "Find File: %s", PROMPT_FILES, NULL);
	if (!prompt)
		return;

	char expanded[512];
	char *filename = (char *)prompt;
	if (prompt[0] == '~' && prompt[1] == '/') {
		char *home = getenv("HOME");
		if (home) {
			int ret = snprintf(expanded, sizeof(expanded), "%s/%s",
					   home, prompt + 2);
			if (ret >= (int)sizeof(expanded)) {
				setStatusMessage("Path too long");
				free(prompt);
				return;
			}
			filename = expanded;
		}
	}

	struct editorBuffer *b = E.headbuf;
	while (b) {
		if (b->filename && strcmp(b->filename, filename) == 0) {
			E.buf = b;
			E.windows[windowFocusedIdx(&E)]->buf = b;
			free(prompt);
			return;
		}
		b = b->next;
	}

	struct editorBuffer *newBuf = newBuffer();
	editorOpenFile(newBuf, filename);
	if (read_only) {
		newBuf->read_only = 1;
	}
	newBuf->next = E.buf->next;
	E.buf->next = newBuf;
	E.buf = newBuf;
	E.windows[windowFocusedIdx(&E)]->buf = newBuf;
	free(prompt);
}

static void handle_find_file(void) {
	handle_find_file_impl(0);
}

static void handle_find_file_read_only(void) {
	handle_find_file_impl(1);
}

static void handle_other_window(void) {
	switchWindow();
}

static void handle_macro_start(void) {
	if (E.recording) {
		setStatusMessage("Already recording.");
	} else {
		E.macro.nkeys = 0;
		E.recording = 1;
		setStatusMessage("Recording...");
	}
}

static void handle_macro_end(void) {
	if (E.recording) {
		E.recording = 0;
		setStatusMessage("Recorded %d keys.", E.macro.nkeys);
	} else {
		setStatusMessage("Not recording.");
	}
}

static void handle_macro_exec(void) {
	int rept = getUArgRepeat();
	for (int i = 0; i < rept; i++)
		execMacro(&E.macro);
}

static void handle_toggle_read_only(void) {
	editorToggleReadOnly();
}

static void handle_query_replace(void) {
	editorQueryReplace();
}

static void handle_pipe_cmd(void) {
	pipeCmd();
}

static void handle_switch_buffer(void) {
	struct editorBuffer *buf = E.buf;
	if (E.buf == E.minibuf) {
		setStatusMessage("Cannot do this from minibuffer");
		return;
	}
	switchToNamedBuffer(&E, buf);
}

static void handle_string_rectangle(void) {
	stringRectangle();
}

static void handle_copy_rectangle(void) {
	copyRectangle();
}

static void handle_kill_rectangle(void) {
	killRectangle();
}

static void handle_yank_rectangle(void) {
	yankRectangle();
}

static void handle_jump_register(void) {
	jumpToRegister();
}

static void handle_macro_register(void) {
	macroToRegister();
}

static void handle_point_register(void) {
	pointToRegister();
}

static void handle_number_register(void) {
	int rept = getUArgRepeat();
	numberToRegister(rept);
}

static void handle_region_register(void) {
	regionToRegister();
}

static void handle_inc_register(void) {
	incrementRegister();
}

static void handle_insert_register(void) {
	insertRegister();
}

static void handle_view_register(void) {
	viewRegister();
}

static void handle_rect_register(void) {
	rectRegister();
}

static void handle_mark_whole_buffer(void) {
	markWholeBuffer();
}

static void handle_custom_info(void) {
	struct editorBuffer *buf = E.buf;
	struct editorWindow *win = E.windows[windowFocusedIdx(&E)];
	char msg[256];
	snprintf(
		msg, sizeof(msg),
		"Win:%d/%d Buf:%s cy:%d cx:%d rowoff:%d coloff:%d height:%d trunc:%d",
		windowFocusedIdx(&E) + 1, E.nwindows,
		buf->filename ? buf->filename : "*scratch*", buf->cy, buf->cx,
		win->rowoff, win->coloff, win->height, buf->truncate_lines);
	setStatusMessage("%s", msg);
}

static void showCommandsOnEmpty(struct editorBuffer *buf, uint8_t *input,
				int key) {
	// Only show commands on TAB with empty input
	if (key == CTRL('i') && strlen((char *)input) == 0) {
		// Build list of all commands
		char msg[512];
		strcpy(msg, "Commands: ");
		int first = 1;

		for (int i = 0; i < E.cmd_count; i++) {
			// Stop if we're running out of space
			if (strlen(msg) + strlen(E.cmd[i].key) + 3 >
			    sizeof(msg) - 20) {
				strcat(msg, "...");
				break;
			}

			if (!first)
				strcat(msg, ", ");
			strcat(msg, E.cmd[i].key);
			first = 0;
		}

		setStatusMessage("%s", msg);
	}
}

static void handle_execute_extended_command(void) {
	if (E.buf == E.minibuf) {
		setStatusMessage("Cannot do this from minibuffer");
		return;
	}
	uint8_t *cmd = promptUser(E.buf, "M-x %s", PROMPT_COMMANDS,
				  showCommandsOnEmpty);
	if (cmd == NULL)
		return;
	runCommand((char *)cmd);
	free(cmd);
}

static void handle_create_window(void) {
	E.windows = xrealloc(E.windows,
			     sizeof(struct editorWindow *) * (++E.nwindows));
	E.windows[E.nwindows - 1] = xmalloc(sizeof(struct editorWindow));
	E.windows[E.nwindows - 1]->focused = 0;
	E.windows[E.nwindows - 1]->buf = E.buf;
	E.windows[E.nwindows - 1]->rowoff = 0;
	E.windows[E.nwindows - 1]->coloff = 0;
	E.windows[E.nwindows - 1]->cx = E.buf->cx;
	E.windows[E.nwindows - 1]->cy = E.buf->cy;
	E.windows[E.nwindows - 1]->scx = 0;
	E.windows[E.nwindows - 1]->scy = 0;
}

static void handle_destroy_window(void) {
	if (E.nwindows == 1) {
		setStatusMessage("Can't delete the only window");
		return;
	}

	int winIdx = windowFocusedIdx(&E);
	free(E.windows[winIdx]);

	for (int i = winIdx; i < E.nwindows - 1; i++) {
		E.windows[i] = E.windows[i + 1];
	}

	E.nwindows--;
	E.windows =
		xrealloc(E.windows, sizeof(struct editorWindow *) * E.nwindows);

	if (winIdx > 0) {
		E.windows[winIdx - 1]->focused = 1;
	} else {
		E.windows[0]->focused = 1;
	}
}

static void handle_destroy_other_windows(void) {
	int winIdx = windowFocusedIdx(&E);
	struct editorWindow *focusedWin = E.windows[winIdx];

	for (int i = 0; i < E.nwindows; i++) {
		if (i != winIdx) {
			free(E.windows[i]);
		}
	}

	E.windows[0] = focusedWin;
	E.nwindows = 1;
	E.windows = xrealloc(E.windows, sizeof(struct editorWindow *));
}

static void handle_kill_buffer(void) {
	struct editorBuffer *buf = E.buf;
	struct editorBuffer *target = buf;

	if (target->dirty && target->filename && !target->special_buffer) {
		setStatusMessage(
			"Buffer has unsaved changes. Kill anyway? (y or n)");
		refreshScreen();
		int c = readKey();
		if (c != 'y' && c != 'Y') {
			setStatusMessage("");
			return;
		}
		setStatusMessage("");
	}

	int bufferCount = 0;
	struct editorBuffer *b = E.headbuf;
	while (b != NULL) {
		bufferCount++;
		b = b->next;
	}

	struct editorBuffer *prev = NULL;
	b = E.headbuf;
	while (b != NULL && b != target) {
		prev = b;
		b = b->next;
	}

	if (prev == NULL) {
		E.headbuf = target->next;
	} else {
		prev->next = target->next;
	}

	struct editorBuffer *replacement = NULL;
	if (bufferCount == 1) {
		replacement = newBuffer();
		replacement->special_buffer = 1;
		replacement->filename = stringdup("*scratch*");
		E.headbuf = replacement;
	} else {
		if (target->next) {
			replacement = target->next;
		} else if (prev) {
			replacement = prev;
		} else {
			replacement = E.headbuf;
		}
	}

	for (int i = 0; i < E.nwindows; i++) {
		if (E.windows[i]->buf == target) {
			E.windows[i]->buf = replacement;
		}
	}

	if (E.buf == target) {
		E.buf = replacement;
		E.edbuf = replacement;
	}

	if (E.backbuf == target) {
		E.backbuf = NULL;
	}

	destroyBuffer(target);
}

static void handle_previous_buffer(void) {
	struct editorBuffer *buf = E.buf;
	struct editorBuffer *b = E.headbuf;
	struct editorBuffer *prev = NULL;

	while (b != NULL && b != buf) {
		prev = b;
		b = b->next;
	}

	if (prev != NULL) {
		E.buf = prev;
		E.edbuf = prev;
		E.windows[windowFocusedIdx(&E)]->buf = prev;
	}
}

static void handle_next_buffer(void) {
	struct editorBuffer *buf = E.buf;
	if (buf->next != NULL) {
		E.buf = buf->next;
		E.edbuf = buf->next;
		E.windows[windowFocusedIdx(&E)]->buf = buf->next;
	}
}


static KeyBinding basic_bindings[] = {
	{ '\r', handle_newline, "newline" },
	{ CTRL('o'), handle_openLine, "open-line" },
	{ CTRL('a'), beginning_of_line, "beginning-of-line" },
	{ CTRL('e'), end_of_line, "end-of-line" },
	{ CTRL('f'), forward_char, "forward-char" },
	{ CTRL('b'), backward_char, "backward-char" },
	{ CTRL('n'), next_line, "next-line" },
	{ CTRL('p'), previous_line, "previous-line" },
	{ CTRL('k'), handle_killLine, "kill-line" },
	{ CTRL('w'), handle_kill_region, "kill-region" },
	{ CTRL('y'), handle_yank, "yank" },
	{ CTRL('_'), handle_doUndo, "undo" },
	{ CTRL('@'), set_mark_wrapper, "set-mark" },
	{ CTRL('s'), find_wrapper, "isearch-forward" },
	{ ISEARCH_FORWARD_REGEXP, regex_find_wrapper, "isearch-forward-regexp" },
	{ CTRL('g'), handle_keyboard_quit, "keyboard-quit" },
	{ CTRL('l'), handle_recenter, "recenter" },
	{ CTRL('t'), transposeChars, "transpose-chars" },
	{ CTRL('h'), NULL, "help-prefix" },
	{ CTRL('d'), handle_delChar, "delete-char" },
	{ CTRL('q'), handle_quoted_insert, "quoted-insert" },
	{ CTRL('i'), handle_tab, "tab" },
	{ CTRL('u'), handle_killLineBackwards, "kill-line-backwards" },
#ifdef EMSYS_CUA
	{ CTRL('z'), handle_doUndo, "undo" },
#else
	{ CTRL('z'), handle_suspend, "suspend-emacs" },
#endif
	{ CTRL('x'), NULL, "ctrl-x-prefix" },
	{ 033, NULL, "meta-prefix" },
	{ BACKSPACE, handle_backSpace, "backward-delete-char" },
	{ DEL_KEY, handle_delChar, "delete-char" },
	{ HOME_KEY, beginning_of_line, "beginning-of-line" },
	{ END_KEY, end_of_line, "end-of-line" },
	{ PAGE_UP, handle_page_up, "scroll-down" },
	{ PAGE_DOWN, handle_page_down, "scroll-up" },
	{ ARROW_LEFT, backward_char, "backward-char" },
	{ ARROW_RIGHT, forward_char, "forward-char" },
	{ ARROW_UP, previous_line, "previous-line" },
	{ ARROW_DOWN, next_line, "next-line" },
	{ QUIT, handle_quit, "quit" },
	{ UNICODE, handle_unicode, "unicode" },
	{ UNICODE_ERROR, handle_unicode_error, "unicode-error" },
	{ BEG_OF_FILE, handle_beginning_of_buffer, "beginning-of-buffer" },
	{ END_OF_FILE, handle_end_of_buffer, "end-of-buffer" },
	{ BACKTAB, handle_backtab, "complete-word" },
	{ WHAT_CURSOR, handle_what_cursor, "what-cursor" },
	{ PIPE_CMD, handle_pipe_cmd, "pipe-command" },
	{ EXPAND, handle_expand, "expand" },
	{ SUSPEND, handle_suspend, "suspend" },
	{ REDO, handle_redo, "redo" },
	{ SAVE, save_wrapper, "save-buffer" },
	{ COPY, handle_copy_region, "copy-region" },
	{ CUT, handle_kill_region, "kill-region" },
	{ SWAP_MARK, handle_swap_mark, "swap-mark" },
	{ EXEC_CMD, handle_execute_extended_command,
	  "execute-extended-command" },
};

static KeyBinding ctrl_x_bindings[] = {
	{ CTRL('c'), handle_quit, "save-buffers-kill-emacs" },
	{ CTRL('s'), save_wrapper, "save-buffer" },
	{ CTRL('f'), handle_find_file, "find-file" },
	{ CTRL('r'), handle_find_file_read_only, "find-file-read-only" },
	{ CTRL('q'), handle_toggle_read_only, "toggle-read-only" },
	{ CTRL('x'), handle_swap_mark, "exchange-point-and-mark" },
	{ 'b', handle_switch_buffer, "switch-to-buffer" },
	{ 'k', handle_kill_buffer, "kill-buffer" },
	{ CTRL('b'), handle_switch_buffer, "list-buffers" },
	{ 'o', handle_other_window, "other-window" },
	{ '2', handle_create_window, "split-window-vertically" },
	{ '0', handle_destroy_window, "delete-window" },
	{ '1', handle_destroy_other_windows, "delete-other-windows" },
	{ ARROW_LEFT, handle_previous_buffer, "previous-buffer" },
	{ ARROW_RIGHT, handle_next_buffer, "next-buffer" },
	{ 'r', NULL, "ctrl-x-r-prefix" },
	{ '(', handle_macro_start, "start-kbd-macro" },
	{ ')', handle_macro_end, "end-kbd-macro" },
	{ 'e', handle_macro_exec, "call-last-kbd-macro" },
	{ ' ', handle_mark_rectangle, "rectangle-mark-mode" },
	{ STRING_RECT, handle_string_rectangle, "string-rectangle" },
	{ COPY_RECT, handle_copy_rectangle, "copy-rectangle-to-register" },
	{ KILL_RECT, handle_kill_rectangle, "kill-rectangle" },
	{ YANK_RECT, handle_yank_rectangle, "yank-rectangle" },
	{ 'h', handle_mark_whole_buffer, "mark-whole-buffer" },
	{ '?', handle_custom_info, "custom-info" },
	{ CTRL('_'), handle_redo, "redo" },
};

static KeyBinding ctrl_x_r_bindings[] = {
	{ 'j', handle_jump_register, "jump-to-register" },
	{ 'J', handle_jump_register, "jump-to-register" },
	{ 'a', handle_macro_register, "append-to-register" },
	{ 'A', handle_macro_register, "append-to-register" },
	{ 'm', handle_macro_register, "append-to-register" },
	{ 'M', handle_macro_register, "append-to-register" },
	{ CTRL('@'), handle_point_register, "point-to-register" },
	{ ' ', handle_point_register, "point-to-register" },
	{ 'n', handle_number_register, "number-to-register" },
	{ 'N', handle_number_register, "number-to-register" },
	{ 'r', handle_rect_register, "copy-rectangle-to-register" },
	{ 'R', handle_rect_register, "copy-rectangle-to-register" },
	{ 's', handle_region_register, "copy-to-register" },
	{ 'S', handle_region_register, "copy-to-register" },
	{ 't', handle_string_rectangle, "string-rectangle" },
	{ 'T', handle_string_rectangle, "string-rectangle" },
	{ '+', handle_inc_register, "increment-register" },
	{ 'i', handle_insert_register, "insert-register" },
	{ 'I', handle_insert_register, "insert-register" },
	{ 'k', handle_kill_rectangle, "kill-rectangle" },
	{ 'K', handle_kill_rectangle, "kill-rectangle" },
	{ CTRL('W'), handle_kill_rectangle, "kill-rectangle" },
	{ 'v', handle_view_register, "view-register" },
	{ 'V', handle_view_register, "view-register" },
	{ 'y', handle_yank_rectangle, "yank-rectangle" },
	{ 'Y', handle_yank_rectangle, "yank-rectangle" },
};

static KeyBinding meta_bindings[] = {
	{ 'f', handle_forwardWord, "forward-word" },
	{ 'b', handle_backWord, "backward-word" },
	{ 'd', handle_deleteWord, "kill-word" },
	{ BACKSPACE, handle_backspaceWord, "backward-kill-word" },
	{ '<', handle_beginning_of_buffer, "beginning-of-buffer" },
	{ '>', handle_end_of_buffer, "end-of-buffer" },
	{ 'w', handle_copy_region, "copy" },
	{ CTRL('x'), handle_swap_mark, "exchange-point-and-mark" },
	{ '{', handle_backPara, "backward-para" },
	{ '}', handle_forwardPara, "forward-para" },
	{ 'u', handle_upcaseWord, "upcase-word" },
	{ 'l', handle_downcaseWord, "downcase-word" },
	{ 'c', handle_capitalCaseWord, "capitalize-word" },
	{ '%', handle_query_replace, "query-replace" },
	{ 'g', handle_goto_line, "goto-line" },
	{ 't', transposeWords, "transpose-words" },
	{ 'x', handle_execute_extended_command, "execute-extended-command" },
	{ CTRL('_'), handle_redo, "redo" },
};

KeyTable global_keys = { "global", basic_bindings,
			 sizeof(basic_bindings) / sizeof(basic_bindings[0]) };

KeyTable ctrl_x_keys = { "ctrl-x", ctrl_x_bindings,
			 sizeof(ctrl_x_bindings) / sizeof(ctrl_x_bindings[0]) };

KeyTable meta_keys = { "meta", meta_bindings,
		       sizeof(meta_bindings) / sizeof(meta_bindings[0]) };

KeyTable ctrl_x_r_keys = { "ctrl-x-r", ctrl_x_r_bindings,
			   sizeof(ctrl_x_r_bindings) /
				   sizeof(ctrl_x_r_bindings[0]) };

void initKeyBindings(void) {
	prefix_state = PREFIX_NONE;
	is_recording = 0;
}

int getRepeatCount(struct editorBuffer *buf) {
	int rept = 1;
	if (E.uarg_active) {
		E.uarg_active = 0;
		rept = E.uarg;
		E.uarg = 0;
	}
	return rept;
}

int handleUniversalArgument(int key, struct editorBuffer *buf) {
	if (ALT_0 <= key && key <= ALT_9) {
		if (!E.uarg_active) {
			E.uarg_active = 1;
			E.uarg = 0;
		}
		E.uarg = E.uarg * 10 + (key - ALT_0);
		setStatusMessage("uarg: %i", E.uarg);
		return 1;
	}
#ifdef EMSYS_CU_UARG
	if (key == UNIVERSAL_ARGUMENT) {
		E.uarg_active = 1;
		E.uarg = 4;
		setStatusMessage("C-u");
		return 1;
	}
	if (E.uarg_active && key >= '0' && key <= '9') {
		E.uarg = (E.uarg == 4) ? key - '0' :
					       E.uarg * 10 + (key - '0');
		setStatusMessage("C-u %d", E.uarg);
		return 1;
	}
#endif
	return 0;
}

static void describeKeyInternal(int key) {
	// Build key description
	char keydesc[64];
	if (ISCTRL(key) && key < 32) {
		snprintf(keydesc, sizeof(keydesc), "C-%c", key + '@');
	} else if (key == 127) {
		strcpy(keydesc, "DEL");
	} else if (key < 256) {
		snprintf(keydesc, sizeof(keydesc), "%c", key);
	} else {
		// Special keys
		switch (key) {
		case ARROW_UP:
			strcpy(keydesc, "<up>");
			break;
		case ARROW_DOWN:
			strcpy(keydesc, "<down>");
			break;
		case ARROW_LEFT:
			strcpy(keydesc, "<left>");
			break;
		case ARROW_RIGHT:
			strcpy(keydesc, "<right>");
			break;
		default:
			snprintf(keydesc, sizeof(keydesc), "<%d>", key);
			break;
		}
	}

	// Find binding
	const char *command = "is undefined";

	// Check global keys
	for (int i = 0; i < global_keys.count; i++) {
		if (global_keys.bindings[i].key == key) {
			command = global_keys.bindings[i].name;
			break;
		}
	}

	setStatusMessage("%s %s", keydesc, command);
}

void processKeySequence(int key) {
	struct editorBuffer *buf = E.buf;

	// Handle describe-key mode
	if (E.describe_key_mode) {
		E.describe_key_mode = 0;
		describeKeyInternal(key);
		E.uarg_active = 0;
		E.uarg = 0;
		return;
	}

	if (E.micro == REDO) {
		if (key == CTRL('_')) {
			doRedo(1);
			E.uarg_active = 0;
			E.uarg = 0;
			return;
		}
		E.micro = 0;
	}

	if (handleUniversalArgument(key, buf)) {
		return;
	}

	if (key == PIPE_CMD) {
		pipeCmd();
		E.uarg_active = 0;
		return;
	}

	if (E.recording && key != CTRL('x')) {
		recordKey(key);
	}

#ifdef EMSYS_CUA
	// CUA mode key bindings
	if (key == CTRL('c') && prefix_state == PREFIX_NONE) {
		if (buf->markx >= 0 && buf->marky >= 0) {
			handle_copy_region();
		} else {
			setStatusMessage("No region selected");
		}
		E.uarg_active = 0;
		E.uarg = 0;
		return;
	}
	if (key == CTRL('v') && prefix_state == PREFIX_NONE) {
		handle_yank();
		E.uarg_active = 0;
		E.uarg = 0;
		return;
	}
	if (key == CTRL('x') && prefix_state == PREFIX_NONE &&
	    buf->markx >= 0 && buf->marky >= 0) {
		handle_kill_region();
		E.uarg_active = 0;
		E.uarg = 0;
		return;
	}
#endif

	if (key == CTRL('x') && prefix_state == PREFIX_NONE) {
		if (E.buf == E.minibuf) {
			setStatusMessage("C-x not available in minibuffer");
			return;
		}
		prefix_state = PREFIX_CTRL_X;
		strcpy(E.prefix_display, "C-x ");
		return;
	}
	if (key == 033 && prefix_state == PREFIX_NONE) {
		prefix_state = PREFIX_META;
		strcpy(E.prefix_display, "M-");
		return;
	}
	if (key == 'r' && prefix_state == PREFIX_CTRL_X) {
		prefix_state = PREFIX_CTRL_X_R;
		strcpy(E.prefix_display, "C-x r ");
		return;
	}

	// Handle C-h prefix commands
	if (key == CTRL('h') && prefix_state == PREFIX_NONE) {
		int help_key = readKey();
		switch (help_key) {
		case 'k':
			describeKey();
			break;
		case 'b':
		case 'i':
		case 'm':
			viewManPage();
			break;
		case '?':
		default:
			helpForHelp();
			break;
		}
		return;
	}

	KeyTable *table = prefix_state == PREFIX_CTRL_X	  ? &ctrl_x_keys :
			  prefix_state == PREFIX_META	  ? &meta_keys :
			  prefix_state == PREFIX_CTRL_X_R ? &ctrl_x_r_keys :
							    &global_keys;

	KeyBinding *binding = NULL;
	for (int i = 0; i < table->count; i++) {
		if (table->bindings[i].key == key) {
			binding = &table->bindings[i];
			break;
		}
	}

	if (!binding) {
		if (!ISCTRL(key) && key < 256 && prefix_state == PREFIX_NONE) {
			int rept = getUArgRepeat();
			for (int i = 0; i < rept; i++) {
				undoAppendChar(key);
				insertChar(key);
			}
		} else {
			setStatusMessage("Unknown key sequence");
		}
		prefix_state = PREFIX_NONE;
		E.prefix_display[0] = '\0';
		return;
	}

	if (!binding->handler) {
		prefix_state = PREFIX_NONE;
		E.prefix_display[0] = '\0';
		return;
	}

	if (prefix_state != PREFIX_NONE) {
		prefix_state = PREFIX_NONE;
		E.prefix_display[0] = '\0';
	}

	binding->handler();
	
	// Always clear universal argument after executing any command
	E.uarg_active = 0;
	E.uarg = 0;
}
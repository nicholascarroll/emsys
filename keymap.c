#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "emsys.h"
#include "fileio.h"
#include "find.h"
#include "pipe.h"
#include "region.h"
#include "register.h"
#include "buffer.h"
#include "tab.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "unused.h"
#include "terminal.h"
#include "display.h"
#include "keymap.h"
#include "edit.h"
#include "region.h"


extern struct editorConfig E;
void editorMoveCursor(struct editorBuffer *bufr, int key);

// Forward declarations for command functions
void editorVersion(struct editorConfig *ed, struct editorBuffer *buf);

// Comparison function for qsort and bsearch
static int compare_commands(const void *a, const void *b) {
	return strcmp(((struct editorCommand *)a)->key,
		      ((struct editorCommand *)b)->key);
}

void setupCommands(struct editorConfig *ed) {
	static struct editorCommand commands[] = {
		{ "capitalize-region", editorCapitalizeRegion },
		{ "indent-spaces", editorIndentSpaces },
		{ "indent-tabs", editorIndentTabs },
		{ "kanaya", editorCapitalizeRegion },
		{ "query-replace", editorQueryReplace },
		{ "replace-regexp", editorReplaceRegex },
		{ "replace-string", editorReplaceString },
		{ "revert", editorRevert },
		{ "toggle-truncate-lines", editorToggleTruncateLines },
		{ "version", editorVersion },
		{ "view-register", editorViewRegister },
		{ "whitespace-cleanup", editorWhitespaceCleanup },
#ifdef EMSYS_DEBUG_UNDO
		{ "debug-unpair", debugUnpair },
#endif
	};

	ed->cmd = commands;
	ed->cmd_count = sizeof(commands) / sizeof(commands[0]);

	// Sort the commands array
	qsort(ed->cmd, ed->cmd_count, sizeof(struct editorCommand),
	      compare_commands);
}

void runCommand(char *cmd, struct editorConfig *ed, struct editorBuffer *buf) {
	for (int i = 0; cmd[i]; i++) {
		uint8_t c = cmd[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		} else if (c == ' ') {
			c = '-';
		}
		cmd[i] = c;
	}

	struct editorCommand key = { cmd, NULL };
	struct editorCommand *found = bsearch(&key, ed->cmd, ed->cmd_count,
					      sizeof(struct editorCommand),
					      compare_commands);

	if (found) {
		found->cmd(ed, buf);
	} else {
		editorSetStatusMessage("No command found");
	}
}

/*** terminal ***/

void editorDeserializeUnicode() {
	E.unicode[0] = E.macro.keys[E.playback++];
	E.nunicode = utf8_nBytes(E.unicode[0]);
	for (int i = 1; i < E.nunicode; i++) {
		E.unicode[i] = E.macro.keys[E.playback++];
	}
}

/* Raw reading a keypress */
int editorReadKey() {
	if (E.playback) {
		int ret = E.macro.keys[E.playback++];
		if (ret == UNICODE) {
			editorDeserializeUnicode();
		}
		return ret;
	}
	int nread;
	uint8_t c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}
#ifdef EMSYS_CU_UARG
	if (c == CTRL('u')) {
		return UNIVERSAL_ARGUMENT;
	}
#endif //EMSYS_CU_UARG
	if (c == 033) {
		char seq[5] = { 0, 0, 0, 0, 0 };
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			goto ESC_UNKNOWN;

		if (seq[0] == '[') {
			if (read(STDIN_FILENO, &seq[1], 1) != 1)
				goto ESC_UNKNOWN;
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					goto ESC_UNKNOWN;
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '1':
						return HOME_KEY;
					case '3':
						return DEL_KEY;
					case '4':
						return END_KEY;
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					case '7':
						return HOME_KEY;
					case '8':
						return END_KEY;
					}
				} else if (seq[2] == '4') {
					if (read(STDIN_FILENO, &seq[3], 1) != 1)
						goto ESC_UNKNOWN;
					if (seq[3] == '~') {
						errno = EINTR;
						die("Panic key");
					}
				}
			} else {
				switch (seq[1]) {
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;
				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				case 'F':
					return END_KEY;
				case 'H':
					return HOME_KEY;
				case 'Z':
					return BACKTAB;
				}
			}
		} else if ('0' <= seq[0] && seq[0] <= '9') {
			return ALT_0 + (seq[0] - '0');
		} else if (seq[0] == '<') {
			return BEG_OF_FILE;
		} else if (seq[0] == '>') {
			return END_OF_FILE;
		} else if (seq[0] == '|') {
			return PIPE_CMD;
		} else if (seq[0] == '%') {
			return QUERY_REPLACE;
		} else if (seq[0] == '?') {
			return CUSTOM_INFO_MESSAGE;
		} else if (seq[0] == '/') {
			return EXPAND;
		} else if (seq[0] == 127) {
			return BACKSPACE_WORD;
		} else {
			switch ((seq[0] & 0x1f) | 0x40) {
			case 'B':
				return BACKWARD_WORD;
			case 'C':
				return CAPCASE_WORD;
			case 'D':
				return DELETE_WORD;
			case 'F':
				return FORWARD_WORD;
			case 'G':
				return GOTO_LINE;
			case 'H':
				return BACKSPACE_WORD;
			case 'L':
				return DOWNCASE_WORD;
			case 'N':
				return FORWARD_PARA;
			case 'P':
				return BACKWARD_PARA;
			case 'T':
				return TRANSPOSE_WORDS;
			case 'U':
				return UPCASE_WORD;
			case 'V':
				return PAGE_UP;
			case 'W':
				return COPY;
			case 'X':
				return EXEC_CMD;
			}
		}

ESC_UNKNOWN:;
		char seqR[32];
		seqR[0] = 0;
		char buf[8];
		for (int i = 0; seq[i]; i++) {
			if (seq[i] < ' ') {
				sprintf(buf, "C-%c ", seq[i] + '`');
			} else {
				sprintf(buf, "%c ", seq[i]);
			}
			strcat(seqR, buf);
		}
		editorSetStatusMessage("Unknown command M-%s", seqR);
		return 033;
	} else if (c == CTRL('x')) {
		/* Welcome to Emacs! */
#ifdef EMSYS_CUA
		// CUA mode: if the region is marked, C-x means 'cut' region.
		// Otherwise, proceed.
		if (E.focusBuf->markx != -1 && E.focusBuf->marky != -1) {
			return CUT;
		}
#endif //EMSYS_CUA
		char seq[5] = { 0, 0, 0, 0, 0 };
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			goto CX_UNKNOWN;
		if (seq[0] == CTRL('c')) {
			return QUIT;
		} else if (seq[0] == CTRL('s')) {
			return SAVE;
		} else if (seq[0] == CTRL('f')) {
			return FIND_FILE;
		} else if (seq[0] == CTRL('_')) {
			return REDO;
		} else if (seq[0] == CTRL('x')) {
			return SWAP_MARK;
		} else if (seq[0] == 'b' || seq[0] == 'B' ||
			   seq[0] == CTRL('b')) {
			return SWITCH_BUFFER;
		} else if (seq[0] == '\x1b') {
			// C-x left and C-x right
			if (read(STDIN_FILENO, &seq[1], 1) != 1)
				goto CX_UNKNOWN;
			if (read(STDIN_FILENO, &seq[2], 1) != 1)
				goto CX_UNKNOWN;
			if (seq[1] == '[') {
				switch (seq[2]) {
				case 'C':
					return NEXT_BUFFER;
				case 'D':
					return PREVIOUS_BUFFER;
				}
			} else if (seq[1] ==
				   'O') { // Check for C-x C-right/left
				switch (seq[2]) {
				case 'C':
					return NEXT_BUFFER; // C-x C-right
				case 'D':
					return PREVIOUS_BUFFER; // C-x C-left
				}
			}
		} else if (seq[0] == 'h') {
			return MARK_BUFFER;
		} else if (seq[0] == 'o' || seq[0] == 'O') {
			return OTHER_WINDOW;
		} else if (seq[0] == '2') {
			return CREATE_WINDOW;
		} else if (seq[0] == '0') {
			return DESTROY_WINDOW;
		} else if (seq[0] == '1') {
			return DESTROY_OTHER_WINDOWS;
		} else if (seq[0] == 'k') {
			return KILL_BUFFER;
		} else if (seq[0] == '(') {
			return MACRO_RECORD;
		} else if (seq[0] == 'e' || seq[0] == 'E') {
			return MACRO_EXEC;
		} else if (seq[0] == ')') {
			return MACRO_END;
		} else if (seq[0] == 'z' || seq[0] == 'Z' ||
			   seq[0] == CTRL('z')) {
			return SUSPEND;
		} else if (seq[0] == 'u' || seq[0] == 'U' ||
			   seq[0] == CTRL('u')) {
			return UPCASE_REGION;
		} else if (seq[0] == 'l' || seq[0] == 'L' ||
			   seq[0] == CTRL('l')) {
			return DOWNCASE_REGION;
		} else if (seq[0] == 'x') {
			if (read(STDIN_FILENO, &seq[1], 1) != 1)
				goto CX_UNKNOWN;
			if (seq[1] == 't')
				return TOGGLE_TRUNCATE_LINES;
		} else if (seq[0] == 'r' || seq[0] == 'R') {
			if (read(STDIN_FILENO, &seq[1], 1) != 1)
				goto CX_UNKNOWN;
			switch (seq[1]) {
			case 033:
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					goto CX_UNKNOWN;
				if (seq[2] == 'W' || seq[2] == 'w') {
					return COPY_RECT;
				}
				goto CX_UNKNOWN;
			case 'j':
			case 'J':
				return JUMP_REGISTER;
			case 'a':
			case 'A':
			case 'm':
			case 'M':
				return MACRO_REGISTER;
			case CTRL('@'):
			case ' ':
				return POINT_REGISTER;
			case 'n':
			case 'N':
				return NUMBER_REGISTER;
			case 'r':
			case 'R':
				return RECT_REGISTER;
			case 's':
			case 'S':
				return REGION_REGISTER;
			case 't':
			case 'T':
				return STRING_RECT;
			case '+':
				return INC_REGISTER;
			case 'i':
			case 'I':
				return INSERT_REGISTER;
			case 'k':
			case 'K':
			case CTRL('W'):
				return KILL_RECT;
			case 'v':
			case 'V':
				return VIEW_REGISTER;
			case 'y':
			case 'Y':
				return YANK_RECT;
			}
		} else if (seq[0] == '=') {
			return WHAT_CURSOR;
		}

CX_UNKNOWN:;
		char seqR[32];
		seqR[0] = 0;
		char buf[8];
		for (int i = 0; seq[i]; i++) {
			if (seq[i] < ' ') {
				sprintf(buf, "C-%c ", seq[i] + '`');
			} else {
				sprintf(buf, "%c ", seq[i]);
			}
			strcat(seqR, buf);
		}
		editorSetStatusMessage("Unknown command C-x %s", seqR);
		return CTRL('x');
	} else if (c == CTRL('p')) {
		return ARROW_UP;
	} else if (c == CTRL('n')) {
		return ARROW_DOWN;
	} else if (c == CTRL('b')) {
		return ARROW_LEFT;
	} else if (c == CTRL('f')) {
		return ARROW_RIGHT;
	} else if (utf8_is2Char(c)) {
		/* 2-byte UTF-8 sequence */
		E.nunicode = 2;

		E.unicode[0] = c;
		if (read(STDIN_FILENO, &E.unicode[1], 1) != 1)
			return UNICODE_ERROR;
		return UNICODE;
	} else if (utf8_is3Char(c)) {
		/* 3-byte UTF-8 sequence */
		E.nunicode = 3;

		E.unicode[0] = c;
		if (read(STDIN_FILENO, &E.unicode[1], 1) != 1)
			return UNICODE_ERROR;
		if (read(STDIN_FILENO, &E.unicode[2], 1) != 1)
			return UNICODE_ERROR;
		return UNICODE;
	} else if (utf8_is4Char(c)) {
		/* 4-byte UTF-8 sequence */
		E.nunicode = 4;

		E.unicode[0] = c;
		if (read(STDIN_FILENO, &E.unicode[1], 1) != 1)
			return UNICODE_ERROR;
		if (read(STDIN_FILENO, &E.unicode[2], 1) != 1)
			return UNICODE_ERROR;
		if (read(STDIN_FILENO, &E.unicode[3], 1) != 1)
			return UNICODE_ERROR;
		return UNICODE;
	}
	return c;
}

/*** editor operations ***/



void editorKillLine(struct editorBuffer *buf) {
	if (buf->numrows <= 0) {
		return;
	}

	erow *row = &buf->row[buf->cy];

	if (buf->cx == row->size) {
		editorDelChar(buf);
	} else {
		// Copy to kill ring
		int kill_len = row->size - buf->cx;
		free(E.kill);
		E.kill = malloc(kill_len + 1);
		memcpy(E.kill, &row->chars[buf->cx], kill_len);
		E.kill[kill_len] = '\0';

		clearRedos(buf);
		struct editorUndo *new = newUndo();
		new->starty = buf->cy;
		new->endy = buf->cy;
		new->startx = buf->cx;
		new->endx = row->size;
		new->delete = 1;
		new->prev = buf->undo;
		buf->undo = new;

		new->datalen = kill_len;
		if (new->datasize < new->datalen + 1) {
			new->datasize = new->datalen + 1;
			new->data = realloc(new->data, new->datasize);
		}
		for (int i = 0; i < kill_len; i++) {
			new->data[i] = E.kill[kill_len - i - 1];
		}
		new->data[kill_len] = '\0';

		row->size = buf->cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
		buf->dirty = 1;
		editorClearMark(buf);
	}
}

void editorKillLineBackwards(struct editorBuffer *buf) {
	if (buf->cx == 0) {
		return;
	}

	erow *row = &buf->row[buf->cy];

	// Copy to kill ring
	free(E.kill);
	E.kill = malloc(buf->cx + 1);
	memcpy(E.kill, row->chars, buf->cx);
	E.kill[buf->cx] = '\0';

	clearRedos(buf);
	struct editorUndo *new = newUndo();
	new->starty = buf->cy;
	new->endy = buf->cy;
	new->startx = 0;
	new->endx = buf->cx;
	new->delete = 1;
	new->prev = buf->undo;
	buf->undo = new;

	new->datalen = buf->cx;
	if (new->datasize < new->datalen + 1) {
		new->datasize = new->datalen + 1;
		new->data = realloc(new->data, new->datasize);
	}
	for (int i = 0; i < buf->cx; i++) {
		new->data[i] = E.kill[buf->cx - i - 1];
	}
	new->data[buf->cx] = '\0';

	row->size -= buf->cx;
	memmove(row->chars, &row->chars[buf->cx], row->size);
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	buf->cx = 0;
	buf->dirty = 1;
}

void editorRecordKey(int c) {
	if (E.recording) {
		E.macro.keys[E.macro.nkeys++] = c;
		if (E.macro.nkeys >= E.macro.skeys) {
			E.macro.skeys *= 2;
			E.macro.keys = realloc(E.macro.keys,
					       E.macro.skeys * sizeof(int));
		}
		if (c == UNICODE) {
			for (int i = 0; i < E.nunicode; i++) {
				E.macro.keys[E.macro.nkeys++] = E.unicode[i];
				if (E.macro.nkeys >= E.macro.skeys) {
					E.macro.skeys *= 2;
					E.macro.keys = realloc(
						E.macro.keys,
						E.macro.skeys * sizeof(int));
				}
			}
		}
	}
}


/*** append buffer ***/

/*** output ***/


/*** input ***/

uint8_t *editorPrompt(struct editorBuffer *bufr, uint8_t *prompt,
		      enum promptType t,
		      void (*callback)(struct editorBuffer *, uint8_t *, int)) {
	size_t bufsize = 128;
	uint8_t *buf = malloc(bufsize);

	int promptlen = stringWidth(prompt) - 2;

	size_t buflen = 0;
	size_t bufwidth = 0;
	size_t curs = 0;
	size_t cursScr = 0;
	buf[0] = 0;

	for (;;) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();
#ifdef EMSYS_DEBUG_PROMPT
		char dbg[32];
		snprintf(dbg, sizeof(dbg), CSI "%d;%dHc: %ld cs: %ld", 0, 0,
			 curs, cursScr);
		write(STDOUT_FILENO, dbg, strlen(dbg));
#endif
		editorCursorBottomLineLong(promptlen + cursScr + 1);

		int c = editorReadKey();
		editorRecordKey(c);
		switch (c) {
		case '\r':
			editorSetStatusMessage("");
			if (callback)
				callback(bufr, buf, c);
			return buf; // Return the buffer even if it's empty
		case CTRL('g'):
		case CTRL('c'):
			editorSetStatusMessage("");
			if (callback)
				callback(bufr, buf, c);
			free(buf);
			return NULL;
			break;
		case CTRL('h'):
		case BACKSPACE:
PROMPT_BACKSPACE:
			if (curs <= 0)
				break;
			if (buflen == 0) {
				break;
			}
			int w = 1;
			curs--;
			while (utf8_isCont(buf[curs])) {
				curs--;
				w++;
			}
			cursScr -= charInStringWidth(buf, curs);
			memmove(&(buf[curs]), &(buf[curs + w]),
				bufsize - (curs + w));
			buflen -= w;
			bufwidth = stringWidth(buf);
			break;
		case CTRL('i'):
			if (t == PROMPT_FILES) {
				uint8_t *tc = tabCompleteFiles(buf);
				if (tc != buf) {
					free(buf);
					buf = tc;
					buflen = strlen((char *)buf);
					bufsize = buflen + 1;
					bufwidth = stringWidth(buf);
					curs = buflen;
					cursScr = bufwidth;
				}
			} else if (t == PROMPT_BASIC) { // For buffer switching
				uint8_t *tc =
					tabCompleteBufferNames(&E, buf, bufr);
				if (tc && tc != buf) {
					free(buf);
					buf = tc;
					buflen = strlen((char *)buf);
					bufsize = buflen + 1;
					bufwidth = stringWidth(buf);
					curs = buflen;
					cursScr = bufwidth;
				}
			}
			break;
		case CTRL('a'):
		case HOME_KEY:
			curs = 0;
			cursScr = 0;
			break;
		case CTRL('e'):
		case END_KEY:
			curs = buflen;
			cursScr = bufwidth;
			break;
		case CTRL('k'):
			buf[curs] = 0;
			buflen = curs;
			bufwidth = stringWidth(buf);
			break;
		case CTRL('u'):
			if (curs == buflen) {
				buflen = 0;
				bufwidth = 0;
				buf[0] = 0;
			} else {
				memmove(buf, &(buf[curs]), bufsize - curs);
				buflen = strlen(buf);
				bufwidth = stringWidth(buf);
			}
			cursScr = 0;
			curs = 0;
			break;
		case ARROW_LEFT:
			if (curs <= 0)
				break;
			curs--;
			while (utf8_isCont(buf[curs]))
				curs--;
			cursScr -= charInStringWidth(buf, curs);
			break;
		case DEL_KEY:
		case CTRL('d'):
		case ARROW_RIGHT:
			if (curs >= buflen)
				break;
			cursScr += charInStringWidth(buf, curs);
			curs++;
			while (utf8_isCont(buf[curs]))
				curs++;
			if (c == CTRL('d') || c == DEL_KEY) {
				goto PROMPT_BACKSPACE;
			}
			break;
		case UNICODE:;
			buflen += E.nunicode;
			if (buflen >= (bufsize - 5)) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			if (curs == buflen) {
				for (int i = 0; i < E.nunicode; i++) {
					buf[(buflen - E.nunicode) + i] =
						E.unicode[i];
				}
				buf[buflen] = 0;
			} else {
				memmove(&(buf[curs + E.nunicode]), &(buf[curs]),
					bufsize - (curs + E.nunicode));
				for (int i = 0; i < E.nunicode; i++) {
					buf[curs + i] = E.unicode[i];
				}
			}
			cursScr += charInStringWidth(buf, curs);
			curs += E.nunicode;
			bufwidth = stringWidth(buf);
			break;
		default:
			if (!ISCTRL(c) && c < 256) {
				if (buflen >= bufsize - 5) {
					bufsize *= 2;
					buf = realloc(buf, bufsize);
				}
				if (curs == buflen) {
					buf[buflen++] = c;
					buf[buflen] = 0;
				} else {
					memmove(&(buf[curs + 1]), &(buf[curs]),
						bufsize - 1);
					buf[curs] = c;
					buflen++;
				}
				bufwidth++;
				curs++;
				cursScr++;
			}
		}

		if (callback)
			callback(bufr, buf, c);
	}
}

void editorMoveCursor(struct editorBuffer *bufr, int key) {
	erow *row = (bufr->cy >= bufr->numrows) ? NULL : &bufr->row[bufr->cy];

	switch (key) {
	case ARROW_LEFT:
		if (bufr->cx != 0) {
			do
				bufr->cx--;
			while (bufr->cx != 0 &&
			       utf8_isCont(row->chars[bufr->cx]));
		} else if (bufr->cy > 0) {
			bufr->cy--;
			bufr->cx = bufr->row[bufr->cy].size;
		}
		break;

	case ARROW_RIGHT:
		if (row && bufr->cx < row->size) {
			bufr->cx += utf8_nBytes(row->chars[bufr->cx]);
		} else if (row && bufr->cx == row->size) {
			bufr->cy++;
			bufr->cx = 0;
		}
		break;
	case ARROW_UP:
		if (bufr->cy > 0) {
			bufr->cy--;
			if (bufr->row[bufr->cy].chars == NULL)
				break;
			while (utf8_isCont(bufr->row[bufr->cy].chars[bufr->cx]))
				bufr->cx++;
		}
		break;
	case ARROW_DOWN:
		if (bufr->cy < bufr->numrows) {
			bufr->cy++;
			if (bufr->cy < bufr->numrows) {
				if (bufr->row[bufr->cy].chars == NULL)
					break;
				while (bufr->cx < bufr->row[bufr->cy].size &&
				       utf8_isCont(bufr->row[bufr->cy]
							   .chars[bufr->cx]))
					bufr->cx++;
			} else {
				bufr->cx = 0;
			}
		}
		break;
	}
	row = (bufr->cy >= bufr->numrows) ? NULL : &bufr->row[bufr->cy];
	int rowlen = row ? row->size : 0;
	if (bufr->cx > rowlen) {
		bufr->cx = rowlen;
	}
}

void bufferEndOfForwardWord(struct editorBuffer *buf, int *dx, int *dy) {
	int cx = buf->cx;
	int icy = buf->cy;
	if (icy >= buf->numrows) {
		*dx = cx;
		*dy = icy;
		return;
	}
	int pre = 1;
	for (int cy = icy; cy < buf->numrows; cy++) {
		int l = buf->row[cy].size;
		while (cx < l) {
			uint8_t c = buf->row[cy].chars[cx];
			if (isWordBoundary(c) && !pre) {
				*dx = cx;
				*dy = cy;
				return;
			} else if (!isWordBoundary(c)) {
				pre = 0;
			}
			cx++;
		}
		if (!pre) {
			*dx = cx;
			*dy = cy;
			return;
		}
		cx = 0;
	}
	*dx = cx;
	*dy = icy;
}

void bufferEndOfBackwardWord(struct editorBuffer *buf, int *dx, int *dy) {
	int cx = buf->cx;
	int icy = buf->cy;

	if (icy >= buf->numrows) {
		return;
	}

	int pre = 1;

	for (int cy = icy; cy >= 0; cy--) {
		if (cy != icy) {
			cx = buf->row[cy].size;
		}
		while (cx > 0) {
			uint8_t c = buf->row[cy].chars[cx - 1];
			if (isWordBoundary(c) && !pre) {
				*dx = cx;
				*dy = cy;
				return;
			} else if (!isWordBoundary(c)) {
				pre = 0;
			}
			cx--;
		}
		if (!pre) {
			*dx = cx;
			*dy = cy;
			return;
		}
	}

	*dx = cx;
	*dy = 0;
}

void editorForwardWord(struct editorBuffer *bufr) {
	bufferEndOfForwardWord(bufr, &bufr->cx, &bufr->cy);
}

void editorBackWord(struct editorBuffer *bufr) {
	bufferEndOfBackwardWord(bufr, &bufr->cx, &bufr->cy);
}

void wordTransform(struct editorConfig *ed, struct editorBuffer *bufr,
		   int times, uint8_t *(*transformer)(uint8_t *)) {
	int icx = bufr->cx;
	int icy = bufr->cy;
	for (int i = 0; i < times; i++) {
		bufferEndOfForwardWord(bufr, &bufr->cx, &bufr->cy);
	}
	bufr->markx = icx;
	bufr->marky = icy;
	editorTransformRegion(ed, bufr, transformer);
}

void editorUpcaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
		      int times) {
	wordTransform(ed, bufr, times, transformerUpcase);
}

void editorDowncaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
			int times) {
	wordTransform(ed, bufr, times, transformerDowncase);
}

void editorCapitalCaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
			   int times) {
	wordTransform(ed, bufr, times, transformerCapitalCase);
}

void editorDeleteWord(struct editorBuffer *bufr) {
	int origMarkx = bufr->markx;
	int origMarky = bufr->marky;
	bufferEndOfForwardWord(bufr, &bufr->markx, &bufr->marky);
	editorKillRegion(&E, bufr);
	bufr->markx = origMarkx;
	bufr->marky = origMarky;
}

void editorBackspaceWord(struct editorBuffer *bufr) {
	int origMarkx = bufr->markx;
	int origMarky = bufr->marky;
	bufferEndOfBackwardWord(bufr, &bufr->markx, &bufr->marky);
	editorKillRegion(&E, bufr);
	bufr->markx = origMarkx;
	bufr->marky = origMarky;
}

void editorBackPara(struct editorBuffer *bufr) {
	bufr->cx = 0;
	int icy = bufr->cy;

	if (icy >= bufr->numrows) {
		icy--;
	}

	if (bufr->numrows == 0) {
		return;
	}

	int pre = 1;

	for (int cy = icy; cy >= 0; cy--) {
		erow *row = &bufr->row[cy];
		if (isParaBoundary(row) && !pre) {
			bufr->cy = cy;
			return;
		} else if (!isParaBoundary(row)) {
			pre = 0;
		}
	}

	bufr->cy = 0;
}

void editorForwardPara(struct editorBuffer *bufr) {
	bufr->cx = 0;
	int icy = bufr->cy;

	if (icy >= bufr->numrows) {
		return;
	}

	if (bufr->numrows == 0) {
		return;
	}

	int pre = 1;

	for (int cy = icy; cy < bufr->numrows; cy++) {
		erow *row = &bufr->row[cy];
		if (isParaBoundary(row) && !pre) {
			bufr->cy = cy;
			return;
		} else if (!isParaBoundary(row)) {
			pre = 0;
		}
	}

	bufr->cy = bufr->numrows;
}

void editorPipeCmd(struct editorConfig *ed, struct editorBuffer *bufr) {
	uint8_t *pipeOutput = editorPipe(ed, bufr);
	if (pipeOutput != NULL) {
		size_t outputLen = strlen((char *)pipeOutput);
		if (outputLen < sizeof(E.minibuffer) - 1) {
			editorSetStatusMessage("%s", pipeOutput);
		} else {
			struct editorBuffer *newBuf = newBuffer();
			newBuf->filename = stringdup("*Shell Output*");
			newBuf->special_buffer = 1;

			// Use a temporary buffer to build each row
			size_t rowStart = 0;
			size_t rowLen = 0;
			for (size_t i = 0; i < outputLen; i++) {
				if (pipeOutput[i] == '\n' ||
				    i == outputLen - 1) {
					// Found a newline or end of output, insert the row
					editorInsertRow(
						newBuf, newBuf->numrows,
						(char *)&pipeOutput[rowStart],
						rowLen);
					rowStart =
						i + 1; // Start of the next row
					rowLen = 0;    // Reset row length
				} else {
					rowLen++;
				}
			}

			// Link the new buffer and update focus
			if (E.firstBuf == NULL) {
				E.firstBuf = newBuf;
			} else {
				struct editorBuffer *temp = E.firstBuf;
				while (temp->next != NULL) {
					temp = temp->next;
				}
				temp->next = newBuf;
			}
			E.focusBuf = newBuf;

			// Update the focused window
			int idx = windowFocusedIdx(&E);
			E.windows[idx]->buf = E.focusBuf;
			editorRefreshScreen();
		}
		free(pipeOutput);
	}
}

void editorGotoLine(struct editorBuffer *bufr) {
	uint8_t *nls;
	int nl;

	for (;;) {
		nls = editorPrompt(bufr, "Goto line: %s", PROMPT_BASIC, NULL);
		if (!nls) {
			return;
		}

		nl = atoi((char *)nls);
		free(nls);

		if (nl) {
			bufr->cx = 0;
			if (nl < 0) {
				bufr->cy = 0;
			} else if (nl > bufr->numrows) {
				bufr->cy = bufr->numrows;
			} else {
				bufr->cy = nl;
			}
			return;
		}
	}
}

void editorTransposeWords(struct editorConfig *ed, struct editorBuffer *bufr) {
	if (bufr->numrows == 0) {
		editorSetStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		editorSetStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)) {
		editorSetStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy, endcx, endcy;
	bufferEndOfBackwardWord(bufr, &startcx, &startcy);
	bufferEndOfForwardWord(bufr, &endcx, &endcy);
	if ((startcx == bufr->cx && bufr->cy == startcy) ||
	    (endcx == bufr->cx && bufr->cy == endcy)) {
		editorSetStatusMessage("Cannot transpose here");
		return;
	}
	bufr->cx = startcx;
	bufr->cy = startcy;
	bufr->markx = endcx;
	bufr->marky = endcy;

	editorTransformRegion(ed, bufr, transformerTransposeWords);
}

void editorTransposeChars(struct editorConfig *ed, struct editorBuffer *bufr) {
	if (bufr->numrows == 0) {
		editorSetStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		editorSetStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)) {
		editorSetStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy;
	editorMoveCursor(bufr, ARROW_LEFT);
	startcx = bufr->cx;
	startcy = bufr->cy;
	editorMoveCursor(bufr, ARROW_RIGHT);
	editorMoveCursor(bufr, ARROW_RIGHT);
	bufr->markx = startcx;
	bufr->marky = startcy;
	editorTransformRegion(ed, bufr, transformerTransposeChars);
}

void editorSwitchToNamedBuffer(struct editorConfig *ed,
			       struct editorBuffer *current) {
	char prompt[512];
	const char *defaultBufferName = NULL;

	if (ed->lastVisitedBuffer && ed->lastVisitedBuffer != current) {
		defaultBufferName = ed->lastVisitedBuffer->filename ?
					    ed->lastVisitedBuffer->filename :
					    "*scratch*";
	} else {
		// Find the first buffer that isn't the current one
		struct editorBuffer *defaultBuffer = ed->firstBuf;
		while (defaultBuffer == current && defaultBuffer->next) {
			defaultBuffer = defaultBuffer->next;
		}
		if (defaultBuffer != current) {
			defaultBufferName = defaultBuffer->filename ?
						    defaultBuffer->filename :
						    "*scratch*";
		}
	}

	if (defaultBufferName) {
		snprintf(prompt, sizeof(prompt),
			 "Switch to buffer (default %s): %%s",
			 defaultBufferName);
	} else {
		snprintf(prompt, sizeof(prompt), "Switch to buffer: %%s");
	}

	uint8_t *buffer_name =
		editorPrompt(current, (uint8_t *)prompt, PROMPT_BASIC, NULL);

	if (buffer_name == NULL) {
		// User canceled the prompt
		editorSetStatusMessage("Buffer switch canceled");
		return;
	}

	struct editorBuffer *targetBuffer = NULL;

	if (buffer_name[0] == '\0') {
		// User pressed Enter without typing anything
		if (defaultBufferName) {
			// Find the default buffer
			for (struct editorBuffer *buf = ed->firstBuf;
			     buf != NULL; buf = buf->next) {
				if (buf == current)
					continue;
				if ((buf->filename &&
				     strcmp(buf->filename, defaultBufferName) ==
					     0) ||
				    (!buf->filename &&
				     strcmp("*scratch*", defaultBufferName) ==
					     0)) {
					targetBuffer = buf;
					break;
				}
			}
		}
		if (!targetBuffer) {
			editorSetStatusMessage("No buffer to switch to");
			free(buffer_name);
			return;
		}
	} else {
		for (struct editorBuffer *buf = ed->firstBuf; buf != NULL;
		     buf = buf->next) {
			if (buf == current)
				continue;

			const char *bufName = buf->filename ? buf->filename :
							      "*scratch*";
			if (strcmp((char *)buffer_name, bufName) == 0) {
				targetBuffer = buf;
				break;
			}
		}

		if (!targetBuffer) {
			editorSetStatusMessage("No buffer named '%s'",
					       buffer_name);
			free(buffer_name);
			return;
		}
	}

	if (targetBuffer) {
		ed->lastVisitedBuffer =
			current; // Update the last visited buffer
		ed->focusBuf = targetBuffer;

		const char *switchedBufferName =
			ed->focusBuf->filename ? ed->focusBuf->filename :
						 "*scratch*";
		editorSetStatusMessage("Switched to buffer %s",
				       switchedBufferName);

		for (int i = 0; i < ed->nwindows; i++) {
			if (ed->windows[i]->focused) {
				ed->windows[i]->buf = ed->focusBuf;
			}
		}
	}

	free(buffer_name);
}

/* Where the magic happens */
void editorProcessKeypress(int c) {
	struct editorBuffer *bufr = E.focusBuf;
	int idx;
	struct editorWindow **windows;
	uint8_t *prompt;
	int windowIdx = windowFocusedIdx(&E);
	struct editorWindow *win = E.windows[windowIdx];

	if (E.micro) {
#ifdef EMSYS_CUA
		if (E.micro == REDO && (c == CTRL('_') || c == CTRL('z'))) {
#else
		if (E.micro == REDO && c == CTRL('_')) {
#endif //EMSYS_CUA
			editorDoRedo(bufr);
			return;
		} else {
			E.micro = 0;
		}
	} else {
		E.micro = 0;
	}

	if (ALT_0 <= c && c <= ALT_9) {
		if (!bufr->uarg_active) {
			bufr->uarg_active = 1;
			bufr->uarg = 0;
		}
		bufr->uarg *= 10;
		bufr->uarg += c - ALT_0;
		editorSetStatusMessage("uarg: %i", bufr->uarg);
		return;
	}

#ifdef EMSYS_CU_UARG
	// Handle C-u (Universal Argument)
	if (c == UNIVERSAL_ARGUMENT) {
		bufr->uarg_active = 1;
		bufr->uarg = 4; // Default value for C-u is 4
		editorSetStatusMessage("C-u");
		return;
	}

	// Handle numeric input after C-u
	if (bufr->uarg_active && c >= '0' && c <= '9') {
		if (bufr->uarg == 4) { // If it's the first digit after C-u
			bufr->uarg = c - '0';
		} else {
			bufr->uarg = bufr->uarg * 10 + (c - '0');
		}
		editorSetStatusMessage("C-u %d", bufr->uarg);
		return;
	}
#endif //EMSYS_CU_UARG

	// Handle PIPE_CMD before resetting uarg_active
	if (c == PIPE_CMD) {
		editorPipeCmd(&E, bufr);
		bufr->uarg_active = 0;
		bufr->uarg = 0;
		return;
	}

	int rept = 1;
	if (bufr->uarg_active) {
		bufr->uarg_active = 0;
		rept = bufr->uarg;
	}

	switch (c) {
	case '\r':
		for (int i = 0; i < rept; i++) {
			editorUndoAppendChar(bufr, '\n');
			editorInsertNewline(bufr);
		}
		break;
	case BACKSPACE:
	case CTRL('h'):
		for (int i = 0; i < rept; i++) {
			editorBackSpace(bufr);
		}
		break;
	case DEL_KEY:
	case CTRL('d'):
		for (int i = 0; i < rept; i++) {
			editorDelChar(bufr);
		}
		break;
	case CTRL('l'):
		editorRecenter(win);
		break;
	case QUIT:
		if (E.recording) {
			E.recording = 0;
		}
		// Check all buffers for unsaved changes, except the special buffers
		struct editorBuffer *current = E.firstBuf;
		int hasUnsavedChanges = 0;
		while (current != NULL) {
			if (current->dirty && current->filename != NULL &&
			    !current->special_buffer) {
				hasUnsavedChanges = 1;
				break;
			}
			current = current->next;
		}

		if (hasUnsavedChanges) {
			editorSetStatusMessage(
				"There are unsaved changes. Really quit? (y or n)");
			editorRefreshScreen();
			int c = editorReadKey();
			if (c == 'y' || c == 'Y') {
				exit(0);
			}
			editorSetStatusMessage("");
		} else {
			exit(0);
		}
		break;
	case ARROW_LEFT:
	case ARROW_RIGHT:
	case ARROW_UP:
	case ARROW_DOWN:
		for (int i = 0; i < rept; i++) {
			editorMoveCursor(bufr, c);
		}
		break;
	case PAGE_UP:
#ifndef EMSYS_CUA
	case CTRL('z'):
#endif //EMSYS_CUA
		if (bufr->truncate_lines) {
			bufr->cy = win->rowoff;
			int times = win->height;
			while (times--)
				editorMoveCursor(bufr, ARROW_UP);
		} else {
			int current_screen_line = 0;
			for (int i = 0; i < bufr->cy; i++) {
				current_screen_line +=
					(bufr->row[i].renderwidth /
					 E.screencols) +
					1;
			}
			int target_screen_line =
				current_screen_line - win->height;
			if (target_screen_line < 0)
				target_screen_line = 0;
			while (current_screen_line > target_screen_line) {
				bufr->cy--;
				current_screen_line -=
					(bufr->row[bufr->cy].renderwidth /
					 E.screencols) +
					1;
			}
		}
		break;

	case PAGE_DOWN:
#ifndef EMSYS_CUA
	case CTRL('v'):
#endif //EMSYS_CUA
		if (bufr->truncate_lines) {
			bufr->cy = win->rowoff + win->height - 1;
			if (bufr->cy > bufr->numrows)
				bufr->cy = bufr->numrows;
			int times = win->height;
			while (times--)
				editorMoveCursor(bufr, ARROW_DOWN);
		} else {
			int current_screen_line = 0;
			for (int i = 0; i < bufr->cy; i++) {
				current_screen_line +=
					(bufr->row[i].renderwidth /
					 E.screencols) +
					1;
			}
			int target_screen_line =
				current_screen_line + win->height;
			while (current_screen_line < target_screen_line &&
			       bufr->cy < bufr->numrows) {
				current_screen_line +=
					(bufr->row[bufr->cy].renderwidth /
					 E.screencols) +
					1;
				bufr->cy++;
			}
			if (bufr->cy > bufr->numrows)
				bufr->cy = bufr->numrows;
		}
		break;
	case BEG_OF_FILE:
		bufr->cy = 0;
		bufr->cx = 0;
		break;
	case CUSTOM_INFO_MESSAGE: {
		int winIdx = windowFocusedIdx(&E);
		struct editorWindow *win = E.windows[winIdx];
		struct editorBuffer *buf = win->buf;

		editorSetStatusMessage(
			"(buf->cx%d,cy%d) (win->scx%d,scy%d) win->height=%d screenrows=%d, rowoff=%d",
			buf->cx, buf->cy, win->scx, win->scy, win->height,
			E.screenrows, win->rowoff);
	} break;
	case END_OF_FILE:
		bufr->cy = bufr->numrows;
		bufr->cx = 0;
		break;
	case HOME_KEY:
	case CTRL('a'):
		bufr->cx = 0;
		break;
	case END_KEY:
	case CTRL('e'):
		if (bufr->row != NULL && bufr->cy < bufr->numrows) {
			bufr->cx = bufr->row[bufr->cy].size;
		}
		break;
	case CTRL('s'):
		editorFind(bufr);
		break;
	case UNICODE_ERROR:
		editorSetStatusMessage("Bad UTF-8 sequence");
		break;
	case UNICODE:
		for (int i = 0; i < rept; i++) {
			editorUndoAppendUnicode(&E, bufr);
			editorInsertUnicode(bufr);
		}
		break;
#ifdef EMSYS_CUA
	case CUT:
		editorKillRegion(&E, bufr);
		editorClearMark(bufr);
		break;
#endif //EMSYS_CUA
	case SAVE:
		editorSave(bufr);
		break;
	case COPY:
		editorCopyRegion(&E, bufr);
		editorClearMark(bufr);
		break;
#ifdef EMSYS_CUA
	case CTRL('C'):
		editorCopyRegion(&E, bufr);
		editorClearMark(bufr);
		break;
#endif //EMSYS_CUA
	case CTRL('@'):
		editorSetMark(bufr);
		break;
	case CTRL('y'):
#ifdef EMSYS_CUA
	case CTRL('v'):
#endif //EMSYS_CUA
		for (int i = 0; i < rept; i++) {
			editorYank(&E, bufr);
		}
		break;
	case CTRL('w'):
		for (int i = 0; i < rept; i++) {
			editorKillRegion(&E, bufr);
		}
		editorClearMark(bufr);
		break;
	case CTRL('i'):
		editorIndent(bufr, rept);
		break;
	case CTRL('_'):
#ifdef EMSYS_CUA
	case CTRL('z'):
#endif //EMSYS_CUA
		for (int i = 0; i < rept; i++) {
			editorDoUndo(bufr);
		}
		break;
	case CTRL('k'):
		for (int i = 0; i < rept; i++) {
			editorKillLine(bufr);
		}
		break;
	case CTRL('u'):
		editorKillLineBackwards(bufr);
		break;
	case CTRL('j'):
		for (int i = 0; i < rept; i++) {
			editorInsertNewlineAndIndent(bufr);
		}
		break;
	case CTRL('o'):
		for (int i = 0; i < rept; i++) {
			editorOpenLine(bufr);
		}
		break;
	case CTRL('q'):;
		int nread;
		while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
			if (nread == -1 && errno != EAGAIN)
				die("read");
		}
		for (int i = 0; i < rept; i++) {
			editorUndoAppendChar(bufr, c);
			editorInsertChar(bufr, c);
		}
		break;
	case FORWARD_WORD:
		for (int i = 0; i < rept; i++) {
			editorForwardWord(bufr);
		}
		break;
	case BACKWARD_WORD:
		for (int i = 0; i < rept; i++) {
			editorBackWord(bufr);
		}
		break;
	case FORWARD_PARA:
		for (int i = 0; i < rept; i++) {
			editorForwardPara(bufr);
		}
		break;
	case BACKWARD_PARA:
		for (int i = 0; i < rept; i++) {
			editorBackPara(bufr);
		}
		break;
	case REDO:
		for (int i = 0; i < rept; i++) {
			editorDoRedo(bufr);
			if (bufr->redo != NULL) {
				editorSetStatusMessage(
					"Press C-_ or C-/ to redo again");
				E.micro = REDO;
			}
		}
		break;
	case SWITCH_BUFFER:
		editorSwitchToNamedBuffer(&E, E.focusBuf);
		break;
	case NEXT_BUFFER:
		E.focusBuf = E.focusBuf->next;
		if (E.focusBuf == NULL) {
			E.focusBuf = E.firstBuf;
		}
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i]->focused) {
				E.windows[i]->buf = E.focusBuf;
			}
		}
		break;
	case PREVIOUS_BUFFER:
		if (E.focusBuf == E.firstBuf) {
			// If we're at the first buffer, go to the last buffer
			E.focusBuf = E.firstBuf;
			while (E.focusBuf->next != NULL) {
				E.focusBuf = E.focusBuf->next;
			}
		} else {
			// Otherwise, go to the previous buffer
			struct editorBuffer *temp = E.firstBuf;
			while (temp->next != E.focusBuf) {
				temp = temp->next;
			}
			E.focusBuf = temp;
		}
		// Update the focused buffer in all windows
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i]->focused) {
				E.windows[i]->buf = E.focusBuf;
			}
		}
		break;
	case MARK_BUFFER:
		if (bufr->numrows > 0) {
			bufr->cy = bufr->numrows;
			bufr->cx = bufr->row[--bufr->cy].size;
			editorSetMark(bufr);
			bufr->cy = 0;
			bufr->cx = 0;
		}
		break;

	case FIND_FILE:
		prompt = editorPrompt(E.focusBuf, "Find File: %s", PROMPT_FILES,
				      NULL);
		if (prompt == NULL) {
			editorSetStatusMessage("Canceled.");
			break;
		}
		if (prompt[strlen(prompt) - 1] == '/') {
			editorSetStatusMessage(
				"Directory editing not supported.");
			break;
		}

		// Check if a buffer with the same filename already exists
		struct editorBuffer *buf = E.firstBuf;
		while (buf != NULL) {
			if (buf->filename != NULL &&
			    strcmp(buf->filename, (char *)prompt) == 0) {
				editorSetStatusMessage(
					"File '%s' already open in a buffer.",
					prompt);
				free(prompt);
				E.focusBuf =
					buf; // Switch to the existing buffer

				// Update the focused window to display the found buffer
				idx = windowFocusedIdx(&E);
				E.windows[idx]->buf = E.focusBuf;

				editorRefreshScreen(); // Refresh to reflect the change
				break; // Exit the loop and the case
			}
			buf = buf->next;
		}

		// If a buffer with the same filename was found, don't create a new one
		if (buf != NULL) {
			break;
		}

		E.firstBuf = newBuffer();
		editorOpen(E.firstBuf, prompt);
		free(prompt);
		E.firstBuf->next = E.focusBuf;
		E.focusBuf = E.firstBuf;
		idx = windowFocusedIdx(&E);
		E.windows[idx]->buf = E.focusBuf;
		break;

	case OTHER_WINDOW:
		editorSwitchWindow(&E);
		break;

	case CREATE_WINDOW:
		E.windows = realloc(E.windows, sizeof(struct editorWindow *) *
						       (++E.nwindows));
		E.windows[E.nwindows - 1] = malloc(sizeof(struct editorWindow));
		E.windows[E.nwindows - 1]->focused = 0;
		E.windows[E.nwindows - 1]->buf = E.focusBuf;
		E.windows[E.nwindows - 1]->cx = E.focusBuf->cx;
		E.windows[E.nwindows - 1]->cy = E.focusBuf->cy;
		E.windows[E.nwindows - 1]->rowoff = 0;
		E.windows[E.nwindows - 1]->coloff = 0;
		E.windows[E.nwindows - 1]->height =
			(E.screenrows - minibuffer_height) / E.nwindows -
			statusbar_height;
		break;

	case DESTROY_WINDOW:
		if (E.nwindows == 1) {
			editorSetStatusMessage("Can't kill last window");
			break;
		}
		idx = windowFocusedIdx(&E);
		editorSwitchWindow(&E);
		free(E.windows[idx]);
		windows =
			malloc(sizeof(struct editorWindow *) * (--E.nwindows));
		int j = 0;
		for (int i = 0; i <= E.nwindows; i++) {
			if (i != idx) {
				windows[j] = E.windows[i];
				if (windows[j]->focused) {
					E.focusBuf = windows[j]->buf;
				}
				j++;
			}
		}
		free(E.windows);
		E.windows = windows;
		break;

	case DESTROY_OTHER_WINDOWS:
		if (E.nwindows == 1) {
			editorSetStatusMessage("No other windows to delete");
			break;
		}
		idx = windowFocusedIdx(&E);
		windows = malloc(sizeof(struct editorWindow *));
		for (int i = 0; i < E.nwindows; i++) {
			if (i != idx) {
				free(E.windows[i]);
			}
		}
		windows[0] = E.windows[idx];
		windows[0]->focused = 1;
		E.focusBuf = windows[0]->buf;
		E.nwindows = 1;
		free(E.windows);
		E.windows = windows;
		editorRefreshScreen();
		break;
	case KILL_BUFFER:
		// Bypass confirmation for special buffers
		if (bufr->dirty && bufr->filename != NULL &&
		    !bufr->special_buffer) {
			editorSetStatusMessage(
				"Buffer %.20s modified; kill anyway? (y or n)",
				bufr->filename);
			editorRefreshScreen();
			int c = editorReadKey();
			if (c != 'y' && c != 'Y') {
				editorSetStatusMessage("");
				break;
			}
		}

		// Find the previous buffer (if any)
		struct editorBuffer *prevBuf = NULL;
		if (E.focusBuf != E.firstBuf) {
			prevBuf = E.firstBuf;
			while (prevBuf->next != E.focusBuf) {
				prevBuf = prevBuf->next;
			}
		}

		// Update window focus
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i]->buf == bufr) {
				// If it's the last buffer, create a new scratch buffer
				if (bufr->next == NULL && prevBuf == NULL) {
					E.windows[i]->buf = newBuffer();
					E.windows[i]->buf->filename =
						stringdup("*scratch*");
					E.windows[i]->buf->special_buffer = 1;
					E.firstBuf = E.windows[i]->buf;
					E.focusBuf =
						E.firstBuf; // Ensure E.focusBuf is updated
				} else if (bufr->next == NULL) {
					E.windows[i]->buf = E.firstBuf;
					E.focusBuf =
						E.firstBuf; // Ensure E.focusBuf is updated
				} else {
					E.windows[i]->buf = bufr->next;
					E.focusBuf =
						bufr->next; // Ensure E.focusBuf is updated
				}
			}
		}

		// Update the main buffer list
		if (E.firstBuf == bufr) {
			E.firstBuf = bufr->next;
		} else if (prevBuf != NULL) {
			prevBuf->next = bufr->next;
		}

		// Update the focused buffer
		if (E.focusBuf == bufr) {
			E.focusBuf = (bufr->next != NULL) ? bufr->next :
							    prevBuf;
		}

		destroyBuffer(bufr);
		break;

	case SUSPEND:
		raise(SIGTSTP);
		break;

	case DELETE_WORD:
		for (int i = 0; i < rept; i++) {
			editorDeleteWord(bufr);
		}
		break;
	case BACKSPACE_WORD:
		for (int i = 0; i < rept; i++) {
			editorBackspaceWord(bufr);
		}
		break;

	case UPCASE_WORD:
		editorUpcaseWord(&E, bufr, rept);
		break;

	case DOWNCASE_WORD:
		editorDowncaseWord(&E, bufr, rept);
		break;

	case CAPCASE_WORD:
		editorCapitalCaseWord(&E, bufr, rept);
		break;

	case UPCASE_REGION:
		editorTransformRegion(&E, bufr, transformerUpcase);
		break;

	case DOWNCASE_REGION:
		editorTransformRegion(&E, bufr, transformerDowncase);
		break;
	case TOGGLE_TRUNCATE_LINES:
		editorToggleTruncateLines(&E, bufr);
		break;

	case WHAT_CURSOR:
		c = 0;
		if (bufr->cy >= bufr->numrows) {
			editorSetStatusMessage("End of buffer");
			break;
		} else if (bufr->row[bufr->cy].size <= bufr->cx) {
			c = (uint8_t)'\n';
		} else {
			c = (uint8_t)bufr->row[bufr->cy].chars[bufr->cx];
		}

		int npoint = 0, point;
		for (int y = 0; y < bufr->numrows; y++) {
			for (int x = 0; x <= bufr->row[y].size; x++) {
				npoint++;
				if (x == bufr->cx && y == bufr->cy) {
					point = npoint;
				}
			}
		}
		int perc = ((point - 1) * 100) / npoint;

		if (c == 127) {
			editorSetStatusMessage("char: ^? (%d #o%03o #x%02X)"
					       " point=%d of %d (%d%%)",
					       c, c, c, point, npoint, perc);
		} else if (c < ' ') {
			editorSetStatusMessage("char: ^%c (%d #o%03o #x%02X)"
					       " point=%d of %d (%d%%)",
					       c + 0x40, c, c, c, point, npoint,
					       perc);
		} else {
			editorSetStatusMessage("char: %c (%d #o%03o #x%02X)"
					       " point=%d of %d (%d%%)",
					       c, c, c, c, point, npoint, perc);
		}
		break;

	case TRANSPOSE_WORDS:
		editorTransposeWords(&E, bufr);
		break;

	case CTRL('t'):
		editorTransposeChars(&E, bufr);
		break;

	case EXEC_CMD:;
		uint8_t *cmd =
			editorPrompt(bufr, "cmd: %s", PROMPT_BASIC, NULL);
		if (cmd != NULL) {
			runCommand(cmd, &E, bufr);
			free(cmd);
		}
		break;
	case QUERY_REPLACE:
		editorQueryReplace(&E, bufr);
		break;

	case GOTO_LINE:
		editorGotoLine(bufr);
		break;

	case CTRL('x'):
	case 033:
		/* These take care of their own error messages */
		break;

	case CTRL('g'):
		editorClearMark(bufr);
		editorSetStatusMessage("Quit");
		break;

	case BACKTAB:
		editorUnindent(bufr, rept);
		break;

	case SWAP_MARK:
		if (0 <= bufr->markx &&
		    (0 <= bufr->marky && bufr->marky < bufr->numrows)) {
			int swapx = bufr->cx;
			int swapy = bufr->cy;
			bufr->cx = bufr->markx;
			bufr->cy = bufr->marky;
			bufr->markx = swapx;
			bufr->marky = swapy;
		}
		break;

	case JUMP_REGISTER:
		editorJumpToRegister(&E);
		break;
	case MACRO_REGISTER:
		editorMacroToRegister(&E);
		break;
	case POINT_REGISTER:
		editorPointToRegister(&E);
		break;
	case NUMBER_REGISTER:
		editorNumberToRegister(&E, rept);
		break;
	case REGION_REGISTER:
		editorRegionToRegister(&E, bufr);
		break;
	case INC_REGISTER:
		editorIncrementRegister(&E, bufr);
		break;
	case INSERT_REGISTER:
		editorInsertRegister(&E, bufr);
		break;
	case VIEW_REGISTER:
		editorViewRegister(&E, bufr);
		break;

	case STRING_RECT:
		editorStringRectangle(&E, bufr);
		break;

	case COPY_RECT:
		editorCopyRectangle(&E, bufr);
		editorClearMark(bufr);
		break;

	case KILL_RECT:
		editorKillRectangle(&E, bufr);
		editorClearMark(bufr);
		break;

	case YANK_RECT:
		editorYankRectangle(&E, bufr);
		break;

	case RECT_REGISTER:
		editorRectRegister(&E, bufr);
		break;

	case EXPAND:
		editorCompleteWord(&E, bufr);
		break;

	default:
		if (ISCTRL(c)) {
			editorSetStatusMessage("Unknown command C-%c",
					       c | 0x60);
		} else {
			for (int i = 0; i < rept; i++) {
				editorUndoAppendChar(bufr, c);
				editorInsertChar(bufr, c);
			}
		}
		break;
	}
}

/*** init ***/


void editorExecMacro(struct editorMacro *macro) {
	struct editorMacro tmp;
	tmp.keys = NULL;
	if (macro != &E.macro) {
		/* HACK: Annoyance here with readkey needs us to futz
		 * around with E.macro */
		memcpy(&tmp, &E.macro, sizeof(struct editorMacro));
		memcpy(&E.macro, macro, sizeof(struct editorMacro));
	}
	E.playback = 0;
	while (E.playback < E.macro.nkeys) {
		/* HACK: increment here, so that
		 * readkey sees playback != 0 */
		int key = E.macro.keys[E.playback++];
		if (key == UNICODE) {
			editorDeserializeUnicode();
		}
		editorProcessKeypress(key);
	}
	E.playback = 0;
	if (tmp.keys != NULL) {
		memcpy(&E.macro, &tmp, sizeof(struct editorMacro));
	}
}


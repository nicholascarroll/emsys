#include "platform.h"
#include "compat.h"
#include "terminal.h"
#include "display.h"

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
#include "bound.h"
#include "command.h"
#include "emsys.h"
#include "find.h"
#include "pipe.h"
#include "region.h"
#include "register.h"
#include "row.h"
#include "tab.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "unused.h"
#include "keybindings.h"
#include "editor.h"
#include "fileio.h"

const int minibuffer_height = 1;
const int statusbar_height = 1;
const int page_overlap = 2;

struct editorConfig E;
void setupHandlers();
void processKeypress(int c);

int windowFocusedIdx(struct editorConfig *ed) {
	for (int i = 0; i < ed->nwindows; i++) {
		if (ed->windows[i]->focused) {
			return i;
		}
	}
	/* You're in trouble m80 */
	return 0;
}

erow *safeGetRow(struct editorBuffer *buf, int row_index) {
	return (row_index >= buf->numrows) ? NULL : &buf->row[row_index];
}

int nextScreenX(char *chars, int *i, int current_screen_x) {
	if (chars[*i] == '\t') {
		current_screen_x = (current_screen_x + EMSYS_TAB_STOP) /
				   EMSYS_TAB_STOP * EMSYS_TAB_STOP;
	} else if (ISCTRL(chars[*i])) {
		current_screen_x += 2;
	} else {
		current_screen_x += charInStringWidth(chars, *i);
	}
	*i += utf8_nBytes(chars[*i]) - 1;
	return current_screen_x;
}

void synchronizeBufferCursor(struct editorBuffer *buf,
			     struct editorWindow *win) {
	if (win->cy >= buf->numrows) {
		win->cy = buf->numrows > 0 ? buf->numrows - 1 : 0;
	}
	if (win->cy < buf->numrows && win->cx > buf->row[win->cy].size) {
		win->cx = buf->row[win->cy].size;
	}

	buf->cx = win->cx;
	buf->cy = win->cy;
}

void switchWindow(void) {
	if (E.nwindows == 1) {
		setStatusMessage("No other windows to select");
		return;
	}

	int currentIdx = windowFocusedIdx(&E);
	struct editorWindow *currentWindow = E.windows[currentIdx];
	struct editorBuffer *currentBuffer = currentWindow->buf;

	currentWindow->cx = currentBuffer->cx;
	currentWindow->cy = currentBuffer->cy;

	currentWindow->focused = 0;
	int nextIdx = (currentIdx + 1) % E.nwindows;
	struct editorWindow *nextWindow = E.windows[nextIdx];
	nextWindow->focused = 1;

	E.buf = nextWindow->buf;

	E.buf->cx = nextWindow->cx;
	E.buf->cy = nextWindow->cy;

	synchronizeBufferCursor(E.buf, nextWindow);
}

/*** editor operations ***/

void recordKey(int c) {
	if (E.recording) {
		E.macro.keys[E.macro.nkeys++] = c;
		if (E.macro.nkeys >= E.macro.skeys) {
			E.macro.skeys *= 2;
			E.macro.keys = xrealloc(E.macro.keys,
						E.macro.skeys * sizeof(int));
		}
		if (c == UNICODE) {
			for (int i = 0; i < E.nunicode; i++) {
				E.macro.keys[E.macro.nkeys++] = E.unicode[i];
				if (E.macro.nkeys >= E.macro.skeys) {
					E.macro.skeys *= 2;
					E.macro.keys = xrealloc(
						E.macro.keys,
						E.macro.skeys * sizeof(int));
				}
			}
		}
	}
}

/*** file i/o ***/

void setStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int ret = vsnprintf(E.minibuffer, sizeof(E.minibuffer), fmt, ap);
	va_end(ap);
	if (ret >= (int)sizeof(E.minibuffer)) {
		strcpy(E.minibuffer + sizeof(E.minibuffer) - 4, "...");
	}
	E.statusmsg_time = time(NULL);
}

void recenterCommand(void) {
	int winIdx = windowFocusedIdx(&E);
	recenter(E.windows[winIdx]);
}

void suspend(int UNUSED(sig)) {
	signal(SIGTSTP, SIG_DFL);
	disableRawMode();
	raise(SIGTSTP);
}

void resume(int sig) {
	setupHandlers();
	enableRawMode();
	editorResizeScreen(sig);
}

/*** input ***/

uint8_t *promptUser(struct editorBuffer *bufr, uint8_t *prompt,
		    enum promptType t,
		    void (*callback)(struct editorBuffer *, uint8_t *, int)) {
	uint8_t *result = NULL;
	struct editorBuffer *saved_focus = E.buf;
	struct editorBuffer *saved_edit = E.edbuf;

	/* Clear minibuffer */
	while (E.minibuf->numrows > 0) {
		delRow(E.minibuf, 0);
	}
	insertRow(E.minibuf, 0, "", 0);
	E.minibuf->cx = 0;
	E.minibuf->cy = 0;

	/* Setup state */
	E.edbuf = E.buf;
	E.buf = E.minibuf;

	while (1) {
		/* Display */
		char *content = E.minibuf->numrows > 0 ?
					(char *)E.minibuf->row[0].chars :
					"";
		setStatusMessage((char *)prompt, content);
		refreshScreen();

		/* Position cursor on bottom line */
		int prompt_width = stringWidth((uint8_t *)prompt) - 2;
		cursorBottomLine(prompt_width + E.minibuf->cx + 1);

		/* Read key */
		int c = readKey();
		recordKey(c);

		/* Handle special minibuffer keys */
		switch (c) {
		case '\r':
			if (E.minibuf->numrows > 0 &&
			    E.minibuf->row[0].size > 0) {
				result = (uint8_t *)stringdup(
					(char *)E.minibuf->row[0].chars);
			}
			goto done;

		case CTRL('g'):
			result = NULL;
			goto done;

		case CTRL('j'):
		case CTRL('o'):
			continue;

		case CTRL('i'):
			if (t == PROMPT_FILES) {
				uint8_t *old_text =
					E.minibuf->numrows > 0 ?
						(uint8_t *)stringdup(
							(char *)E.minibuf
								->row[0]
								.chars) :
						(uint8_t *)stringdup("");
				uint8_t *tc = tabCompleteFiles(old_text);
				if (tc && tc != old_text) {
					delRow(E.buf, 0);
					insertRow(E.buf, 0, (char *)tc,
						  strlen((char *)tc));
					E.minibuf->cx = strlen((char *)tc);
					E.minibuf->cy = 0;
					free(tc);
				}
				free(old_text);
			} else if (t == PROMPT_COMMANDS) {
				uint8_t *old_text =
					E.minibuf->numrows > 0 ?
						(uint8_t *)stringdup(
							(char *)E.minibuf
								->row[0]
								.chars) :
						(uint8_t *)stringdup("");
				uint8_t *tc = tabCompleteCommands(&E, old_text);
				if (tc && tc != old_text) {
					delRow(E.buf, 0);
					insertRow(E.buf, 0, (char *)tc,
						  strlen((char *)tc));
					E.minibuf->cx = strlen((char *)tc);
					E.minibuf->cy = 0;
					free(tc);
				}
				free(old_text);
			}
			break;

		default:
			processKeypress(c);

			/* Ensure single line */
			if (E.minibuf->numrows > 1) {
				/* Join all rows into first row */
				int total_len = 0;
				for (int i = 0; i < E.minibuf->numrows; i++) {
					total_len += E.minibuf->row[i].size;
				}

				char *joined = xmalloc(total_len + 1);
				joined[0] = 0;
				for (int i = 0; i < E.minibuf->numrows; i++) {
					if (E.minibuf->row[i].chars) {
						strncat(joined,
							(char *)E.minibuf
								->row[i]
								.chars,
							E.minibuf->row[i].size);
					}
				}

				while (E.minibuf->numrows > 0) {
					delRow(E.buf, 0);
				}
				insertRow(E.buf, 0, joined, strlen(joined));
				E.minibuf->cx = strlen(joined);
				E.minibuf->cy = 0;
				free(joined);
			}
		}

		/* Callback if provided */
		if (callback) {
			char *text = E.minibuf->numrows > 0 ?
					     (char *)E.minibuf->row[0].chars :
					     "";
			callback(bufr, (uint8_t *)text, c);
		}
	}

done:
	/* Restore state */
	E.buf = saved_focus;
	E.edbuf = saved_edit;
	return result;
}

void moveCursor(struct editorBuffer *bufr, int key) {
	erow *row = safeGetRow(bufr, bufr->cy);

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
			if (bufr->cy == bufr->numrows) {
				bufr->cx = 0;
				break;
			}
			if (bufr->row[bufr->cy].chars == NULL)
				break;
			while (bufr->cx < bufr->row[bufr->cy].size &&
			       utf8_isCont(bufr->row[bufr->cy].chars[bufr->cx]))
				bufr->cx++;
		}
		break;
	}
	row = safeGetRow(bufr, bufr->cy);
	int rowlen = row ? row->size : 0;
	if (bufr->cx > rowlen) {
		bufr->cx = rowlen;
	}
}













void pipeCmd(void) {
	uint8_t *pipeOutput = editorPipe();
	if (pipeOutput != NULL) {
		size_t outputLen = strlen((char *)pipeOutput);
		if (outputLen < sizeof(E.minibuffer) - 1) {
			setStatusMessage("%s", pipeOutput);
		} else {
			struct editorBuffer *newBuf = newBuffer();
			newBuf->filename = stringdup("*Shell Output*");
			newBuf->special_buffer = 1;

			size_t rowStart = 0;
			size_t rowLen = 0;
			for (size_t i = 0; i < outputLen; i++) {
				if (pipeOutput[i] == '\n' ||
				    i == outputLen - 1) {
					insertRow(newBuf, newBuf->numrows,
						  (char *)&pipeOutput[rowStart],
						  rowLen);
					rowStart = i + 1;
					rowLen = 0;
				} else {
					rowLen++;
				}
			}

			if (E.headbuf == NULL) {
				E.headbuf = newBuf;
			} else {
				struct editorBuffer *temp = E.headbuf;
				while (temp->next != NULL) {
					temp = temp->next;
				}
				temp->next = newBuf;
			}
			E.buf = newBuf;

			int idx = windowFocusedIdx(&E);
			E.windows[idx]->buf = E.buf;
			refreshScreen();
		}
		free(pipeOutput);
	}
}

void gotoLine(void) {
	struct editorBuffer *buf = E.buf;
	uint8_t *nls;
	int nl;

	for (;;) {
		nls = promptUser(buf, (uint8_t *)"Goto line: %s", PROMPT_BASIC,
				 NULL);
		if (!nls) {
			return;
		}

		nl = atoi((char *)nls);
		free(nls);

		if (nl) {
			buf->cx = 0;
			if (nl < 0) {
				buf->cy = 0;
			} else if (nl > buf->numrows) {
				buf->cy = buf->numrows;
			} else {
				buf->cy = nl;
			}
			return;
		}
	}
}



void switchToNamedBuffer(struct editorConfig *ed,
			 struct editorBuffer *current) {
	char promptMsg[512];
	const char *defaultBufferName = NULL;

	if (ed->backbuf && ed->backbuf != current) {
		defaultBufferName = ed->backbuf->filename ?
					    ed->backbuf->filename :
					    "*scratch*";
	} else {
		struct editorBuffer *defaultBuffer = ed->headbuf;
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
		snprintf(promptMsg, sizeof(promptMsg),
			 "Switch to buffer (default %s): %%s",
			 defaultBufferName);
	} else {
		snprintf(promptMsg, sizeof(promptMsg), "Switch to buffer: %%s");
	}

	uint8_t *buffer_name =
		promptUser(current, (uint8_t *)promptMsg, PROMPT_BASIC, NULL);

	if (buffer_name == NULL) {
		setStatusMessage("Buffer switch canceled");
		return;
	}

	struct editorBuffer *targetBuffer = NULL;

	if (buffer_name[0] == '\0') {
		if (defaultBufferName) {
			for (struct editorBuffer *buf = ed->headbuf;
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
			setStatusMessage("No buffer to switch to");
			free(buffer_name);
			return;
		}
	} else {
		for (struct editorBuffer *buf = ed->headbuf; buf != NULL;
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
			setStatusMessage("No buffer named '%s'", buffer_name);
			free(buffer_name);
			return;
		}
	}

	if (targetBuffer) {
		ed->backbuf = current;
		ed->buf = targetBuffer;
		ed->edbuf = targetBuffer;

		const char *switchedBufferName =
			ed->buf->filename ? ed->buf->filename : "*scratch*";
		setStatusMessage("Switched to buffer %s", switchedBufferName);

		for (int i = 0; i < ed->nwindows; i++) {
			if (ed->windows[i]->focused) {
				ed->windows[i]->buf = ed->buf;
			}
		}
	}

	free(buffer_name);
}

/* Where the magic happens */
void processKeypress(int c) {
	/* Initialize keybindings on first use */
	static int initialized = 0;
	if (!initialized) {
		initKeyBindings();
		initialized = 1;
	}

	/* Process the key using the new system */
	processKeySequence(c);
}
/*** init ***/

void crashHandler(int sig) {
	/* Use only async-signal-safe functions */
	write(STDOUT_FILENO, CSI "?1049l", 8);
	write(STDERR_FILENO, "\nProgram terminated by signal\n", 29);
	_exit(sig);
}

void editorResume(int sig) {
	/* Simple resume handler - just refresh the screen */
	refreshScreen();
}

void editorSuspend(int sig) {
	/* Restore terminal and suspend */
	disableRawMode();
	signal(SIGTSTP, SIG_DFL);
	raise(SIGTSTP);
}

void setupHandlers() {
	signal(SIGWINCH, editorResizeScreen);
	signal(SIGCONT, editorResume);
	signal(SIGTSTP, editorSuspend);

	/* Graceful crash handling */
	signal(SIGINT, crashHandler);
	signal(SIGTERM, crashHandler);
	signal(SIGSEGV, crashHandler);
	signal(SIGABRT, crashHandler);
	signal(SIGQUIT, crashHandler);
}

struct editorBuffer *newBuffer() {
	struct editorBuffer *ret = xmalloc(sizeof(struct editorBuffer));
	ret->indent = 0;
	ret->markx = -1;
	ret->marky = -1;
	ret->cx = 0;
	ret->cy = 0;
	ret->numrows = 0;
	ret->row = NULL;
	ret->filename = NULL;
	ret->query = NULL;
	ret->dirty = 0;
	ret->special_buffer = 0;
	ret->undo = NULL;
	ret->redo = NULL;
	ret->next = NULL;
	ret->truncate_lines = 0;
	ret->rectangle_mode = 0;
	ret->read_only = 0;
	ret->screen_line_start = NULL;
	ret->screen_line_cache_size = 0;
	ret->screen_line_cache_valid = 0;
	return ret;
}

void invalidateScreenCache(struct editorBuffer *buf) {
	buf->screen_line_cache_valid = 0;
}

void buildScreenCache(struct editorBuffer *buf) {
	if (buf->screen_line_cache_valid)
		return;

	if (buf->screen_line_cache_size < buf->numrows) {
		buf->screen_line_cache_size = buf->numrows + 100;
		int *new_ptr =
			realloc(buf->screen_line_start,
				buf->screen_line_cache_size * sizeof(int));
		if (!new_ptr)
			return;
		buf->screen_line_start = new_ptr;
	}

	if (!buf->screen_line_start)
		return;

	int screen_line = 0;
	for (int i = 0; i < buf->numrows; i++) {
		buf->screen_line_start[i] = screen_line;
		if (buf->truncate_lines) {
			screen_line += 1;
		} else {
			int width = calculateLineWidth(&buf->row[i]);
			int lines_used = (width / E.screencols) + 1;
			screen_line += lines_used;
		}
	}

	buf->screen_line_cache_valid = 1;
}

int getScreenLineForRow(struct editorBuffer *buf, int row) {
	if (!buf->screen_line_cache_valid) {
		buildScreenCache(buf);
	}
	if (row >= buf->numrows || row < 0)
		return 0;
	return buf->screen_line_start[row];
}

void destroyBuffer(struct editorBuffer *buf) {
	clearUndosAndRedos();
	free(buf->filename);
	free(buf->screen_line_start);
	free(buf);
}

void initEditor() {
	E.minibuffer[0] = 0;
	E.prefix_display[0] = '\0';
	E.describe_key_mode = 0;
	E.kill = NULL;
	E.rectKill = NULL;
	E.windows = xmalloc(sizeof(struct editorWindow *) * 1);
	E.windows[0] = xmalloc(sizeof(struct editorWindow));
	E.windows[0]->focused = 1;
	E.windows[0]->cx = 0;
	E.windows[0]->cy = 0;
	E.windows[0]->scx = 0;
	E.windows[0]->scy = 0;
	E.windows[0]->rowoff = 0;
	E.windows[0]->coloff = 0;
	E.nwindows = 1;
	E.recording = 0;
	E.macro.nkeys = 0;
	E.macro.keys = NULL;
	E.uarg = 0;
	E.uarg_active = 0;
	E.micro = 0;
	E.playback = 0;
	E.headbuf = NULL;
	memset(E.registers, 0, sizeof(E.registers));
	setupCommands();
	E.backbuf = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}

void execMacro(struct editorMacro *macro) {
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
			insertUnicode();
		}
		processKeypress(key);
	}
	E.playback = 0;
	if (tmp.keys != NULL) {
		memcpy(&E.macro, &tmp, sizeof(struct editorMacro));
	}
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	E.headbuf = newBuffer();
	E.buf = E.headbuf;
	if (argc >= 2) {
		int i = 1;
		int linum = -1;
		if (argv[1][0] == '+' && argc > 2) {
			linum = atoi(argv[1] + 1);
			i++;
		}
		for (; i < argc; i++) {
			E.headbuf = newBuffer();
			editorOpenFile(E.headbuf, argv[i]);
			E.headbuf->next = E.buf;
			if (linum > 0) {
				E.headbuf->cy = linum - 1;
				linum = -1;
				if (E.headbuf->cy > E.headbuf->numrows) {
					E.headbuf->cy = E.headbuf->numrows;
				}
			}
			E.buf = E.headbuf;
		}
	}
	E.windows[0]->buf = E.buf;

	E.minibuf = newBuffer();
	E.minibuf->is_minibuffer = 1;
	E.minibuf->single_line = 1;
	E.minibuf->truncate_lines = 1;
	E.minibuf->filename = stringdup("*minibuffer*");
	E.edbuf = E.buf;

	setStatusMessage("emsys " EMSYS_VERSION " - C-x C-c to quit");
	setupHandlers();
	refreshScreen();

	for (;;) {
		int c = readKey();
		if (c == MACRO_RECORD) {
			if (E.recording) {
				setStatusMessage(
					"Already defining keyboard macro");
			} else {
				setStatusMessage("Defining keyboard macro...");
				E.recording = 1;
				E.macro.nkeys = 0;
				E.macro.skeys = 0x10;
				free(E.macro.keys);
				E.macro.keys =
					xmalloc(E.macro.skeys * sizeof(int));
			}
		} else if (c == MACRO_END) {
			if (E.recording) {
				setStatusMessage("Keyboard macro defined");
				E.recording = 0;
			} else {
				setStatusMessage("Not defining keyboard macro");
			}
		} else if (c == MACRO_EXEC ||
			   (E.micro == MACRO_EXEC && (c == 'e' || c == 'E'))) {
			if (E.recording) {
				setStatusMessage("Keyboard macro defined");
				E.recording = 0;
			}
			execMacro(&E.macro);
			E.micro = MACRO_EXEC;
		} else {
			recordKey(c);
			processKeypress(c);
		}
		refreshScreen();
	}
	return 0;
#ifndef TESTING
}
#endif

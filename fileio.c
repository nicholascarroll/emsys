#include "platform.h"
#include "compat.h"

#include "emsys.h"
#include "fileio.h"
#include "buffer.h"
#include "tab.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/select.h>
#include "display.h"
#include "prompt.h"
#include "util.h"
#include "undo.h"
#include "keymap.h"
#include "unused.h"

/* Access global editor state */
extern struct editorConfig E;

/* External functions we need */
extern void die(const char *s);

/* Check if C-g has been pressed (non-blocking) */
static int check_for_interrupt(void) {
	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
		uint8_t c;
		if (read(STDIN_FILENO, &c, 1) == 1 && c == CTRL('g')) {
			return 1;
		}
	}
	return 0;
}

/*** file i/o ***/

char *editorRowsToString(struct editorBuffer *bufr, int *buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < bufr->numrows; j++) {
		totlen += bufr->row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (j = 0; j < bufr->numrows; j++) {
		memcpy(p, bufr->row[j].chars, bufr->row[j].size);
		p += bufr->row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpen(struct editorBuffer *bufr, char *filename) {
	free(bufr->filename);
	bufr->filename = xstrdup(filename);

	/* Get file size for progress calculation */
	struct stat file_stat;
	long file_size = 0;
	if (stat(filename, &file_stat) == 0) {
		file_size = file_stat.st_size;
	}

	FILE *fp = fopen(filename, "r");
	if (!fp) {
		if (errno == ENOENT) {
			editorSetStatusMessage("(New file)", bufr->filename);
			return;
		}
		die("fopen");
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;

	clock_t start_time = clock();
	clock_t last_update = clock();

	/* Doesn't handle null bytes */
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 &&
		       (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(bufr, bufr->numrows, line, linelen);

		/* Check for progress update every 1000 lines */
		if (bufr->numrows % 1000 == 0) {
			clock_t now = clock();
			double elapsed =
				(double)(now - start_time) / CLOCKS_PER_SEC;

			/* Show progress after 2 seconds, update every 2 seconds */
			if (elapsed > 2.0 &&
			    (double)(now - last_update) / CLOCKS_PER_SEC >=
				    2.0) {
				int percent = 0;
				if (file_size > 0) {
					long current_pos = ftell(fp);
					percent =
						(current_pos * 100) / file_size;
				}
				double rate = bufr->numrows / elapsed;

				editorSetStatusMessage(
					"Loading... %3d%% (%6.0f lines/sec, C-g to cancel)",
					percent, rate);

				if (E.minibuf != NULL) {
					refreshScreen();
				} else {
					fprintf(stderr,
						"\rLoading... %3d%% (%6.0f lines/sec, C-g to cancel)",
						percent, rate);
					fflush(stderr);
				}

				if (check_for_interrupt()) {
					free(line);
					fclose(fp);
					/* Mark buffer as cancelled by clearing filename */
					free(bufr->filename);
					bufr->filename = NULL;
					editorSetStatusMessage(
						"Load cancelled at %d%%",
						percent);
					return;
				}

				last_update = now;
			}
		}
	}
	free(line);
	fclose(fp);
	bufr->dirty = 0;

	/* Clear the progress line if we were showing console output */
	if (E.minibuf == NULL && bufr->numrows > 1000) {
		fprintf(stderr, "\r\033[K"); /* Clear line */
		fflush(stderr);
	}
}

void editorRevert(struct editorConfig *ed, struct editorBuffer *buf) {
	struct editorBuffer *new = newBuffer();
	editorOpen(new, buf->filename);
	new->next = buf->next;
	ed->buf = new;
	if (ed->headbuf == buf) {
		ed->headbuf = new;
	}
	struct editorBuffer *cur = ed->headbuf;
	while (cur != NULL) {
		if (cur->next == buf) {
			cur->next = new;
			break;
		}
		cur = cur->next;
	}
	for (int i = 0; i < ed->nwindows; i++) {
		if (ed->windows[i]->buf == buf) {
			ed->windows[i]->buf = new;
		}
	}
	new->indent = buf->indent;
	new->cx = buf->cx;
	new->cy = buf->cy;
	if (new->numrows == 0) {
		new->cy = 0;
		new->cx = 0;
	} else if (new->cy >= new->numrows) {
		new->cy = new->numrows - 1;
		new->cx = 0;
	} else if (new->cx > new->row[new->cy].size) {
		new->cx = new->row[new->cy].size;
	}
	destroyBuffer(buf);
}

void editorSave(struct editorBuffer *bufr) {
	if (bufr->filename == NULL) {
		bufr->filename = (char *)editorPrompt(
			bufr, (uint8_t *)"Save as: %s", PROMPT_FILES, NULL);
		if (bufr->filename == NULL) {
			editorSetStatusMessage("Save aborted.");
			return;
		}
	}

	int len;
	char *buf = editorRowsToString(bufr, &len);

	int fd = open(bufr->filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len)) {
				close(fd);
				free(buf);
				bufr->dirty = 0;

				// Clear undo/redo on successful save
				clearUndosAndRedos(bufr);

				editorSetStatusMessage(
					"Wrote %d bytes to %s (undo history cleared)",
					len, bufr->filename);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Save failed: %s", strerror(errno));
}

void findFile(void) {
	struct editorConfig *E_ptr = &E;
	uint8_t *prompt =
		editorPrompt(E_ptr->buf, "Find File: %s", PROMPT_FILES, NULL);

	if (prompt == NULL) {
		editorSetStatusMessage("Canceled.");
		return;
	}

	if (prompt[strlen(prompt) - 1] == '/') {
		editorSetStatusMessage("Directory editing not supported.");
		free(prompt);
		return;
	}

	// Check if a buffer with the same filename already exists
	struct editorBuffer *buf = E_ptr->headbuf;
	while (buf != NULL) {
		if (buf->filename != NULL &&
		    strcmp(buf->filename, (char *)prompt) == 0) {
			editorSetStatusMessage(
				"File '%s' already open in a buffer.", prompt);
			free(prompt);
			E_ptr->buf = buf; // Switch to the existing buffer

			// Update the focused window to display the found buffer
			int idx = windowFocusedIdx();
			E_ptr->windows[idx]->buf = E_ptr->buf;

			refreshScreen(); // Refresh to reflect the change
			return;
		}
		buf = buf->next;
	}

	// Create new buffer for the file
	struct editorBuffer *newBuf = newBuffer();
	editorOpen(newBuf, (char *)prompt);
	free(prompt);

	/* Check if load was cancelled */
	if (newBuf->filename == NULL) {
		/* Load was cancelled, destroy the buffer */
		destroyBuffer(newBuf);
		return;
	}

	newBuf->next = E_ptr->headbuf;
	E_ptr->headbuf = newBuf;
	E_ptr->buf = newBuf;
	int idx = windowFocusedIdx();
	E_ptr->windows[idx]->buf = E_ptr->buf;
}

void editorInsertFile(struct editorConfig *UNUSED(ed),
		      struct editorBuffer *buf) {
	uint8_t *filename =
		editorPrompt(buf, "Insert file: %s", PROMPT_FILES, NULL);
	if (filename == NULL) {
		return; /* User cancelled */
	}

	FILE *fp = fopen((char *)filename, "r");
	if (!fp) {
		if (errno == ENOENT) {
			editorSetStatusMessage("File not found: %s", filename);
		} else {
			editorSetStatusMessage("Error opening file: %s",
					       strerror(errno));
		}
		free(filename);
		return;
	}

	/* Save current position for undo */
	int saved_cy = buf->cy;

	/* Create undo entry for the entire operation */
	newUndo(buf);

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int lines_inserted = 0;

	/* Read and insert lines at current position */
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		/* Remove newline/carriage return */
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
				       line[linelen - 1] == '\r')) {
			linelen--;
		}

		/* Insert the line */
		editorInsertRow(buf, saved_cy + lines_inserted, line, linelen);
		lines_inserted++;
	}

	free(line);
	fclose(fp);

	/* Move cursor to end of inserted text */
	if (lines_inserted > 0) {
		buf->cy = saved_cy + lines_inserted - 1;
		buf->cx = buf->row[buf->cy].size;
	}

	editorSetStatusMessage("Inserted %d lines from %s", lines_inserted,
			       filename);
	free(filename);

	/* Mark buffer as dirty */
	buf->dirty++;
}

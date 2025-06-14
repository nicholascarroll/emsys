#include "editor.h"
#include "emsys.h"
#include "row.h"
#include "undo.h"
#include "transform.h"
#include "bound.h"
#include "region.h"
#include "unicode.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

extern struct editorConfig E;

void insertChar(int c) {
	CHECK_READ_ONLY(E.buf);
	if (E.buf->cy == E.buf->numrows) {
		insertRow(E.buf, E.buf->numrows, "", 0);
	}
	rowInsertChar(&E.buf->row[E.buf->cy], E.buf->cx, c);
	E.buf->cx++;
}

void insertUnicode(void) {
	CHECK_READ_ONLY(E.buf);
	if (E.buf->cy == E.buf->numrows) {
		insertRow(E.buf, E.buf->numrows, "", 0);
	}
	rowInsertUnicode(&E.buf->row[E.buf->cy], E.buf->cx);
	E.buf->cx += E.nunicode;
}

void insertNewline(int times) {
	struct editorBuffer *buf = E.buf;
	CHECK_READ_ONLY(buf);
	for (int i = 0; i < times; i++) {
		if (buf->cx == 0) {
			insertRow(buf, buf->cy, "", 0);
		} else {
			erow *row = &buf->row[buf->cy];
			insertRow(buf, buf->cy + 1, &row->chars[buf->cx],
				  row->size - buf->cx);
			row = &buf->row[buf->cy];
			rowDeleteRange(row, buf->cx, row->size);
		}
		buf->cy++;
		buf->cx = 0;
	}
}

void openLine(int times) {
	struct editorBuffer *buf = E.buf;
	CHECK_READ_ONLY(buf);
	for (int i = 0; i < times; i++) {
		int ccx = buf->cx;
		int ccy = buf->cy;
		insertNewline(1);
		buf->cx = ccx;
		buf->cy = ccy;
	}
}

void insertNewlineAndIndent(void) {
	struct editorBuffer *buf = E.buf;
	CHECK_READ_ONLY(buf);
	undoAppendChar('\n');
	insertNewline(1);
	int i = 0;
	uint8_t c = buf->row[buf->cy - 1].chars[i];
	while (c == ' ' || c == CTRL('i')) {
		undoAppendChar(c);
		insertChar(c);
		c = buf->row[buf->cy - 1].chars[++i];
	}
}

void indent(int rept) {
	struct editorBuffer *buf = E.buf;
	int ocx = buf->cx;
	int indWidth = 1;
	if (buf->indent) {
		indWidth = buf->indent;
	}
	buf->cx = 0;
	for (int i = 0; i < rept; i++) {
		if (buf->indent) {
			for (int i = 0; i < buf->indent; i++) {
				undoAppendChar(' ');
				insertChar(' ');
			}
		} else {
			undoAppendChar('\t');
			insertChar('\t');
		}
	}
	buf->cx = ocx + indWidth * rept;
}

void unindent(int rept) {
	struct editorBuffer *buf = E.buf;
	if (buf->cy >= buf->numrows) {
		setStatusMessage("End of buffer.");
		return;
	}

	/* Setup for indent mode */
	int indWidth = 1;
	char indCh = '\t';
	struct erow *row = &buf->row[buf->cy];
	if (buf->indent) {
		indWidth = buf->indent;
		indCh = ' ';
	}

	/* Calculate size of unindent */
	int trunc = 0;
	for (int i = 0; i < rept; i++) {
		for (int j = 0; j < indWidth; j++) {
			if (row->chars[trunc] != indCh)
				goto UNINDENT_PERFORM;
			trunc++;
		}
	}

UNINDENT_PERFORM:
	if (trunc == 0)
		return;

	/* Create undo */
	struct editorUndo *new = newUndo();
	new->prev = buf->undo;
	new->startx = 0;
	new->starty = buf->cy;
	new->endx = trunc;
	new->endy = buf->cy;
	new->delete = 1;
	new->append = 0;
	buf->undo = new;
	if (new->datasize < trunc - 1) {
		new->datasize = trunc + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	memset(new->data, indCh, trunc);
	new->data[trunc] = 0;
	new->datalen = trunc;

	/* Perform row operation & dirty buffer */
	rowDeleteRange(row, 0, trunc);
	buf->cx -= trunc;
	buf->dirty = 1;
}

void delChar(int times) {
	struct editorBuffer *buf = E.buf;
	CHECK_READ_ONLY(buf);

	for (int i = 0; i < times; i++) {
		if (buf->cy == buf->numrows)
			return;
		if (buf->cy == buf->numrows - 1 &&
		    buf->cx == buf->row[buf->cy].size)
			return;

		erow *row = &buf->row[buf->cy];
		undoDelChar(row);
		if (buf->cx == row->size) {
			row = &buf->row[buf->cy + 1];
			rowInsertString(&buf->row[buf->cy],
					buf->row[buf->cy].size, row->chars,
					row->size);
			delRow(buf, buf->cy + 1);
		} else {
			rowDelChar(row, buf->cx);
		}
	}
}

void backSpace(int times) {
	struct editorBuffer *buf = E.buf;
	CHECK_READ_ONLY(buf);

	for (int i = 0; i < times; i++) {
		if (!buf->numrows)
			return;
		if (buf->cy == buf->numrows) {
			buf->cx = buf->row[--buf->cy].size;
			return;
		}
		if (buf->cy == 0 && buf->cx == 0)
			return;

		erow *row = &buf->row[buf->cy];
		if (buf->cx > 0) {
			do {
				buf->cx--;
				undoBackSpace(row->chars[buf->cx]);
			} while (utf8_isCont(row->chars[buf->cx]));
			rowDelChar(row, buf->cx);
		} else {
			undoBackSpace('\n');
			buf->cx = buf->row[buf->cy - 1].size;
			rowInsertString(&buf->row[buf->cy - 1],
					buf->row[buf->cy - 1].size, row->chars,
					row->size);
			delRow(buf, buf->cy);
			buf->cy--;
		}
	}
}

void killLine(int times) {
	struct editorBuffer *buf = E.buf;
	CHECK_READ_ONLY(buf);

	for (int t = 0; t < times; t++) {
		if (buf->numrows <= 0) {
			return;
		}

		erow *row = &buf->row[buf->cy];

		if (buf->cx == row->size) {
			delChar(1);
		} else {
			int kill_len = row->size - buf->cx;
			free(E.kill);
			E.kill = xmalloc(kill_len + 1);
			memcpy(E.kill, &row->chars[buf->cx], kill_len);
			E.kill[kill_len] = '\0';

			clearRedos();
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
				new->data = xrealloc(new->data, new->datasize);
			}
			for (int i = 0; i < kill_len; i++) {
				new->data[i] = E.kill[kill_len - i - 1];
			}
			new->data[kill_len] = '\0';

			rowDeleteRange(row, buf->cx, row->size);
			buf->dirty = 1;
			clearMark();
		}
	}
}

void killLineBackwards(int times) {
	struct editorBuffer *buf = E.buf;
	CHECK_READ_ONLY(buf);

	for (int t = 0; t < times; t++) {
		if (buf->cx == 0) {
			return;
		}

		erow *row = &buf->row[buf->cy];

		free(E.kill);
		E.kill = xmalloc(buf->cx + 1);
		memcpy(E.kill, row->chars, buf->cx);
		E.kill[buf->cx] = '\0';

		clearRedos();
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
			new->data = xrealloc(new->data, new->datasize);
		}
		for (int i = 0; i < buf->cx; i++) {
			new->data[i] = E.kill[buf->cx - i - 1];
		}
		new->data[buf->cx] = '\0';

		rowDeleteRange(row, 0, buf->cx);
		buf->cx = 0;
		buf->dirty = 1;
	}
}

void endOfForwardWord(struct editorBuffer *buf, int *dx, int *dy) {
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

void endOfBackwardWord(struct editorBuffer *buf, int *dx, int *dy) {
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

void forwardWord(int times) {
	struct editorBuffer *buf = E.buf;
	for (int i = 0; i < times; i++) {
		endOfForwardWord(buf, &buf->cx, &buf->cy);
	}
}

void backWord(int times) {
	struct editorBuffer *buf = E.buf;
	for (int i = 0; i < times; i++) {
		endOfBackwardWord(buf, &buf->cx, &buf->cy);
	}
}

void wordTransform(int times, uint8_t *(*transformer)(uint8_t *)) {
	struct editorBuffer *buf = E.buf;
	int icx = buf->cx;
	int icy = buf->cy;
	for (int i = 0; i < times; i++) {
		endOfForwardWord(buf, &buf->cx, &buf->cy);
	}
	buf->markx = icx;
	buf->marky = icy;
	transformRegion(transformer);
}

void upcaseWord(int times) {
	wordTransform(times, transformerUpcase);
}

void downcaseWord(int times) {
	wordTransform(times, transformerDowncase);
}

void capitalCaseWord(int times) {
	wordTransform(times, transformerCapitalCase);
}

void deleteWord(int times) {
	struct editorBuffer *buf = E.buf;
	for (int i = 0; i < times; i++) {
		int origMarkx = buf->markx;
		int origMarky = buf->marky;
		endOfForwardWord(buf, &buf->markx, &buf->marky);
		killRegion();
		buf->markx = origMarkx;
		buf->marky = origMarky;
	}
}

void backspaceWord(int times) {
	struct editorBuffer *buf = E.buf;
	for (int i = 0; i < times; i++) {
		int origMarkx = buf->markx;
		int origMarky = buf->marky;
		endOfBackwardWord(buf, &buf->markx, &buf->marky);
		killRegion();
		buf->markx = origMarkx;
		buf->marky = origMarky;
	}
}

void backPara(int times) {
	struct editorBuffer *buf = E.buf;

	for (int t = 0; t < times; t++) {
		buf->cx = 0;
		int icy = buf->cy;

		if (icy >= buf->numrows) {
			icy--;
		}

		if (buf->numrows == 0) {
			return;
		}

		int pre = 1;

		for (int cy = icy; cy >= 0; cy--) {
			erow *row = &buf->row[cy];
			if (isParaBoundary(row) && !pre) {
				buf->cy = cy;
				break;
			} else if (!isParaBoundary(row)) {
				pre = 0;
			}
		}

		if (buf->cy == icy) {
			buf->cy = 0;
		}
	}
}

void forwardPara(int times) {
	struct editorBuffer *buf = E.buf;

	for (int t = 0; t < times; t++) {
		buf->cx = 0;
		int icy = buf->cy;

		if (icy >= buf->numrows) {
			return;
		}

		if (buf->numrows == 0) {
			return;
		}

		int pre = 1;

		for (int cy = icy; cy < buf->numrows; cy++) {
			erow *row = &buf->row[cy];
			if (isParaBoundary(row) && !pre) {
				buf->cy = cy;
				break;
			} else if (!isParaBoundary(row)) {
				pre = 0;
			}
		}

		if (buf->cy == icy) {
			buf->cy = buf->numrows;
		}
	}
}

void transposeWords(void) {
	struct editorBuffer *buf = E.buf;
	if (buf->numrows == 0) {
		setStatusMessage("Buffer is empty");
		return;
	}

	if (buf->cx == 0 && buf->cy == 0) {
		setStatusMessage("Beginning of buffer");
		return;
	} else if (buf->cy >= buf->numrows ||
		   (buf->cy == buf->numrows - 1 &&
		    buf->cx == buf->row[buf->cy].size)) {
		setStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy, endcx, endcy;

	startcx = buf->cx;
	startcy = buf->cy;
	endcx = buf->cx;
	endcy = buf->cy;

	endOfBackwardWord(buf, &startcx, &startcy);
	endOfForwardWord(buf, &endcx, &endcy);

	if (startcy < 0 || startcy >= buf->numrows || endcy < 0 ||
	    endcy >= buf->numrows) {
		setStatusMessage("Invalid buffer position");
		return;
	}

	if ((startcx == buf->cx && buf->cy == startcy) ||
	    (endcx == buf->cx && buf->cy == endcy)) {
		setStatusMessage("Cannot transpose here");
		return;
	}

	if (startcy == endcy && startcx >= endcx) {
		setStatusMessage("No words to transpose");
		return;
	}

	if (startcy == endcy) {
		struct erow *row = &buf->row[startcy];
		if (startcx < 0 || startcx > row->size || endcx < 0 ||
		    endcx > row->size) {
			setStatusMessage("Invalid word boundaries");
			return;
		}
	}

	buf->cx = startcx;
	buf->cy = startcy;
	buf->markx = endcx;
	buf->marky = endcy;

	uint8_t *regionText = NULL;

	if (startcy == endcy) {
		struct erow *row = &buf->row[startcy];
		int len = endcx - startcx;
		regionText = xmalloc(len + 1);
		if (regionText) {
			memcpy(regionText, &row->chars[startcx], len);
			regionText[len] = '\0';
		}
	} else {
		setStatusMessage("Multi-line word transpose not supported");
		return;
	}

	if (!regionText) {
		setStatusMessage("Failed to extract words");
		return;
	}

	uint8_t *result = transformerTransposeWords(regionText);
	if (!result) {
		free(regionText);
		setStatusMessage("Transpose failed");
		return;
	}

	buf->cx = startcx;
	buf->cy = startcy;
	buf->markx = endcx;
	buf->marky = endcy;

	killRegion();

	for (int i = 0; result[i] != '\0'; i++) {
		insertChar(result[i]);
	}

	free(regionText);
	free(result);
}

void transposeChars(void) {
	struct editorBuffer *buf = E.buf;
	if (buf->numrows == 0) {
		setStatusMessage("Buffer is empty");
		return;
	}

	if (buf->cx == 0 && buf->cy == 0) {
		setStatusMessage("Beginning of buffer");
		return;
	} else if (buf->cy >= buf->numrows ||
		   (buf->cy == buf->numrows - 1 &&
		    buf->cx == buf->row[buf->cy].size)) {
		setStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy;
	moveCursor(buf, ARROW_LEFT);
	startcx = buf->cx;
	startcy = buf->cy;
	moveCursor(buf, ARROW_RIGHT);
	moveCursor(buf, ARROW_RIGHT);
	buf->markx = startcx;
	buf->marky = startcy;
	transformRegion(transformerTransposeChars);
}
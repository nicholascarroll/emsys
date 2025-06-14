#define _GNU_SOURCE
#include "fileio.h"
#include "emsys.h"
#include "row.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

extern struct editorConfig E;

char *rowsToString(struct editorBuffer *bufr, int *buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < bufr->numrows; j++) {
		totlen += bufr->row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = xmalloc(totlen);
	char *p = buf;
	for (j = 0; j < bufr->numrows; j++) {
		memcpy(p, bufr->row[j].chars, bufr->row[j].size);
		p += bufr->row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpenFile(struct editorBuffer *bufr, char *filename) {
	free(bufr->filename);
	bufr->filename = stringdup(filename);
	FILE *fp = fopen(filename, "rb");
	if (!fp) {
		if (errno == ENOENT) {
			setStatusMessage("(New file)", bufr->filename);
			return;
		}
		die("fopen");
	}

	/* Check for binary file by looking for null bytes in first 8KB */
	char check_buf[8192];
	size_t bytes_read = fread(check_buf, 1, sizeof(check_buf), fp);
	for (size_t i = 0; i < bytes_read; i++) {
		if (check_buf[i] == '\0') {
			fclose(fp);
			setStatusMessage("Cannot open binary file: %s",
					 filename);
			free(bufr->filename);
			bufr->filename = NULL;
			return;
		}
	}

	/* Reset file position and reopen in text mode */
	fclose(fp);
	fp = fopen(filename, "r");
	if (!fp) {
		die("fopen");
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 &&
		       (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		insertRow(bufr, bufr->numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	bufr->dirty = 0;
}

void save(struct editorBuffer *buf) {
	if (buf->filename == NULL) {
		buf->filename = (char *)promptUser(
			buf, (uint8_t *)"Save as: %s", PROMPT_FILES, NULL);
		if (buf->filename == NULL) {
			setStatusMessage("Save aborted.");
			return;
		}
	}

	int len;
	char *data = rowsToString(buf, &len);

	int fd = open(buf->filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, data, len) == len) {
				close(fd);
				free(data);
				buf->dirty = 0;
				setStatusMessage("Wrote %d bytes to %s", len,
						 buf->filename);
				return;
			}
		}
		close(fd);
	}

	free(data);
	setStatusMessage("Save failed: %s", strerror(errno));
}
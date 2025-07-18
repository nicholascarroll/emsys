#include "util.h"

#ifndef EMSYS_DISABLE_PIPE

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Define feature test macros for subprocess.h to get fdopen, fileno, kill */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "emsys.h"
#include "region.h"
#include "pipe.h"
#include "subprocess.h"
#include "display.h"
#include "prompt.h"
#include "buffer.h"
#include "unicode.h"
#include "util.h"

extern struct editorConfig E;

static uint8_t *cmd;
static char *buf;

static uint8_t *transformerPipeCmd(uint8_t *input) {
	int bsiz = BUFSIZ + 1;
	/* Using sh -c lets us use pipes and stuff and takes care of quoting. */
	const char *command_line[4] = { "/bin/sh", "-c", (char *)cmd, NULL };
	struct subprocess_s subprocess;
	int result = subprocess_create(command_line,
				       subprocess_option_inherit_environment,
				       &subprocess);
	if (result) {
		editorSetStatusMessage(
			"Shell command failed: unable to create subprocess");
		return NULL;
	}
	FILE *p_stdin = subprocess_stdin(&subprocess);
	FILE *p_stdout = subprocess_stdout(&subprocess);

	/* Write region to subprocess */
	for (int i = 0; input[i]; i++) {
		fputc(input[i], p_stdin);
	}

	/* Join process */
	int sub_ret;
	if (subprocess_join(&subprocess, &sub_ret) != 0) {
		editorSetStatusMessage(
			"Shell command failed: error waiting for subprocess");
		return NULL;
	}

	/* Check if subprocess exited with error */
	if (sub_ret != 0) {
		editorSetStatusMessage("Shell command exited with status %d",
				       sub_ret);
		/* Continue anyway to show any output/errors */
	}

	/* Read stdout of process into buffer */
	int c = fgetc(p_stdout);
	int i = 0;
	while (c != EOF) {
		buf[i++] = c;
		buf[i] = 0;
		if (i >= bsiz - 10) {
			bsiz <<= 1;
			char *newbuf = xrealloc(buf, bsiz);
			buf = newbuf;
		}
		c = fgetc(p_stdout);
	}

	/* Only show byte count if subprocess succeeded */
	if (sub_ret == 0) {
		editorSetStatusMessage("Read %d bytes", i);
	}

	/* Cleanup & return */
	subprocess_destroy(&subprocess);
	return (uint8_t *)buf;
}

uint8_t *editorPipe(struct editorConfig *ed, struct editorBuffer *bf) {
	buf = xcalloc(1, BUFSIZ + 1);
	cmd = NULL;
	cmd = editorPrompt(bf, (uint8_t *)"Shell command on region: %s",
			   PROMPT_BASIC, NULL);

	if (cmd == NULL) {
		editorSetStatusMessage("Canceled shell command.");
	} else {
		if (E.uarg) {
			E.uarg = 0;
			editorTransformRegion(ed, bf, transformerPipeCmd);
			// unmark region
			bf->markx = -1;
			bf->marky = -1;
			free(cmd);
			return NULL;
		} else {
			// 1. Extract the selected region
			if (markInvalid()) {
				editorSetStatusMessage("Mark invalid.");
				free(cmd);
				free(buf);
				return NULL;
			}

			editorCopyRegion(
				ed, bf); // ed->kill now holds the selected text

			// 2. Pass the extracted text to transformerPipeCmd
			uint8_t *result = transformerPipeCmd(ed->kill);

			free(cmd);
			return result;
		}
	}

	free(cmd);
	free(buf);
	return NULL;
}

void editorPipeCmd(struct editorConfig *ed, struct editorBuffer *bufr) {
	uint8_t *pipeOutput = editorPipe(ed, bufr);
	if (pipeOutput != NULL) {
		size_t outputLen = strlen((char *)pipeOutput);
		if (outputLen < sizeof(ed->statusmsg) - 1) {
			editorSetStatusMessage("%s", pipeOutput);
		} else {
			struct editorBuffer *newBuf = newBuffer();
			newBuf->filename = xstrdup("*Shell Output*");
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
			if (ed->headbuf == NULL) {
				ed->headbuf = newBuf;
			} else {
				struct editorBuffer *temp = ed->headbuf;
				while (temp->next != NULL) {
					temp = temp->next;
				}
				temp->next = newBuf;
			}
			ed->buf = newBuf;

			// Update the focused window
			int idx = windowFocusedIdx();
			ed->windows[idx]->buf = ed->buf;
			refreshScreen();
		}
		free(pipeOutput);
	}
}

#else /* EMSYS_DISABLE_PIPE */

#include "emsys.h"
#include "display.h"

void editorPipeCmd(struct editorConfig *ed, struct editorBuffer *bufr) {
	(void)ed;   /* unused parameter */
	(void)bufr; /* unused parameter */
	editorSetStatusMessage("Pipe command not available on this platform");
}

#endif /* EMSYS_DISABLE_PIPE */

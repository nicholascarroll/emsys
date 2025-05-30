#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "emsys.h"
#include "command.h"
#include "find.h"
#include "region.h"
#include "register.h"
#include "row.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "unused.h"

static void editorRegexFindCommand(struct editorConfig *UNUSED(ed),
				   struct editorBuffer *buf) {
	editorRegexFind(buf);
}

// https://stackoverflow.com/a/779960
// You must free the result if result is non-NULL.
char *str_replace(char *orig, char *rep, char *with) {
	char *result;  // the return string
	char *ins;     // the next insert point
	char *tmp;     // varies
	int len_rep;   // length of rep (the string to remove)
	int len_with;  // length of with (the string to replace rep with)
	int len_front; // distance between rep and end of last rep
	int count;     // number of replacements

	// sanity checks and initialization
	if (!orig || !rep)
		return NULL;
	len_rep = strlen(rep);
	if (len_rep == 0)
		return NULL; // empty rep causes infinite loop during count
	if (!with)
		with = "";
	len_with = strlen(with);

	// count the number of replacements needed
	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count) {
		ins = tmp + len_rep;
	}

	tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

	if (!result)
		return NULL;

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}

void editorVersion(struct editorConfig *UNUSED(ed),
		   struct editorBuffer *UNUSED(buf)) {
	editorSetStatusMessage("emsys version " EMSYS_VERSION
			       ", built " EMSYS_BUILD_DATE);
}

void editorIndentTabs(struct editorConfig *UNUSED(ed),
		      struct editorBuffer *buf) {
	buf->indent = 0;
	editorSetStatusMessage("Indentation set to tabs");
}

void editorIndentSpaces(struct editorConfig *UNUSED(ed),
			struct editorBuffer *buf) {
	uint8_t *indentS =
		editorPrompt(buf, "Set indentation to: %s", PROMPT_BASIC, NULL);
	if (indentS == NULL) {
		goto cancel;
	}
	int indent = atoi((char *)indentS);
	free(indentS);
	if (indent <= 0) {
cancel:
		editorSetStatusMessage("Canceled.");
		return;
	}
	buf->indent = indent;
	editorSetStatusMessage("Indentation set to %i spaces", indent);
}

void editorRevert(struct editorConfig *ed, struct editorBuffer *buf) {
	struct editorBuffer *new = newBuffer();
	editorOpen(new, buf->filename);
	new->next = buf->next;
	ed->focusBuf = new;
	if (ed->firstBuf == buf) {
		ed->firstBuf = new;
	}
	struct editorBuffer *cur = ed->firstBuf;
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
	if (new->cy > new->numrows) {
		new->cy = new->numrows;
		new->cx = 0;
	} else if (new->cx > new->row[new->cy].size) {
		new->cx = new->row[new->cy].size;
	}
	destroyBuffer(buf);
}

uint8_t *orig;
uint8_t *repl;

uint8_t *transformerReplaceString(uint8_t *input) {
	return str_replace(input, orig, repl);
}

void editorReplaceString(struct editorConfig *ed, struct editorBuffer *buf) {
	orig = NULL;
	repl = NULL;
	orig = editorPrompt(buf, "Replace: %s", PROMPT_BASIC, NULL);
	if (orig == NULL) {
		editorSetStatusMessage("Canceled replace-string.");
		return;
	}

	size_t prompt_size = strlen(orig) + 20;
	uint8_t *prompt = xmalloc(prompt_size);
	snprintf(prompt, prompt_size, "Replace %s with: %%s", orig);
	repl = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
	free(prompt);
	if (repl == NULL) {
		free(orig);
		editorSetStatusMessage("Canceled replace-string.");
		return;
	}

	editorTransformRegion(ed, buf, transformerReplaceString);

	free(orig);
	free(repl);
}

static int nextOccur(struct editorBuffer *buf, uint8_t *needle, int ocheck) {
	int ox = buf->cx;
	int oy = buf->cy;
	if (!ocheck) {
		ox = -69;
	}
	while (buf->cy < buf->numrows) {
		erow *row = &buf->row[buf->cy];
		uint8_t *match = strstr(&(row->chars[buf->cx]), needle);
		if (match) {
			if (!(buf->cx == ox && buf->cy == oy)) {
				buf->cx = match - row->chars;
				buf->marky = buf->cy;
				buf->markx = buf->cx + strlen(needle);
				/* buf->rowoff = buf->numrows; */
				return 1;
			}
			buf->cx++;
		}
		buf->cx = 0;
		buf->cy++;
	}
	return 0;
}

void editorQueryReplace(struct editorConfig *ed, struct editorBuffer *buf) {
	orig = NULL;
	repl = NULL;
	orig = editorPrompt(buf, "Query replace: %s", PROMPT_BASIC, NULL);
	if (orig == NULL) {
		editorSetStatusMessage("Canceled query-replace.");
		return;
	}

	size_t prompt_size = strlen(orig) + 25;
	uint8_t *prompt = xmalloc(prompt_size);
	snprintf(prompt, prompt_size, "Query replace %s with: %%s", orig);
	repl = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
	free(prompt);
	if (repl == NULL) {
		free(orig);
		editorSetStatusMessage("Canceled query-replace.");
		return;
	}

	prompt_size = strlen(orig) + strlen(repl) + 32;
	prompt = xmalloc(prompt_size);
	snprintf(prompt, prompt_size, "Query replacing %s with %s:", orig,
		 repl);
	int bufwidth = stringWidth(prompt);
	int savedMx = buf->markx;
	int savedMy = buf->marky;
	struct editorUndo *first = buf->undo;
	uint8_t *newStr = NULL;
	buf->query = orig;
	int currentIdx = windowFocusedIdx(ed);
	struct editorWindow *currentWindow = ed->windows[currentIdx];

#define NEXT_OCCUR(ocheck)                 \
	if (!nextOccur(buf, orig, ocheck)) \
	goto QR_CLEANUP

	NEXT_OCCUR(false);

	for (;;) {
		editorSetStatusMessage(prompt);
		editorRefreshScreen();
		editorCursorBottomLine(bufwidth + 2);

		int c = editorReadKey();
		editorRecordKey(c);
		switch (c) {
		case ' ':
		case 'y':
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			NEXT_OCCUR(true);
			break;
		case CTRL('h'):
		case BACKSPACE:
		case DEL_KEY:
		case 'n':
			buf->cx++;
			NEXT_OCCUR(true);
			break;
		case '\r':
		case 'q':
		case 'N':
		case CTRL('g'):
			goto QR_CLEANUP;
			break;
		case '.':
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			goto QR_CLEANUP;
			break;
		case '!':
		case 'Y':
			buf->marky = buf->numrows - 1;
			buf->markx = buf->row[buf->marky].size;
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			goto QR_CLEANUP;
			break;
		case 'u':
			editorDoUndo(buf);
			buf->markx = buf->cx;
			buf->marky = buf->cy;
			buf->cx -= strlen(orig);
			break;
		case 'U':
			while (buf->undo != first)
				editorDoUndo(buf);
			buf->markx = buf->cx;
			buf->marky = buf->cy;
			buf->cx -= strlen(orig);
			break;
		case CTRL('r'):
			prompt_size = strlen(orig) + 25;
			prompt = xmalloc(prompt_size);
			snprintf(prompt, prompt_size,
				 "Replace this %s with: %%s", orig);
			newStr = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
			free(prompt);
			if (newStr == NULL) {
				goto RESET_PROMPT;
			}
			uint8_t *tmp = repl;
			repl = newStr;
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			free(newStr);
			repl = tmp;
			NEXT_OCCUR(true);
			goto RESET_PROMPT;
			break;
		case 'e':
		case 'E':
			prompt_size = strlen(orig) + 25;
			prompt = xmalloc(prompt_size);
			snprintf(prompt, prompt_size,
				 "Query replace %s with: %%s", orig);
			newStr = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
			free(prompt);
			if (newStr == NULL) {
				goto RESET_PROMPT;
			}
			free(repl);
			repl = newStr;
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			NEXT_OCCUR(true);
RESET_PROMPT:
			prompt_size = strlen(orig) + strlen(repl) + 32;
			prompt = xmalloc(prompt_size);
			snprintf(prompt, prompt_size,
				 "Query replacing %s with %s:", orig, repl);
			bufwidth = stringWidth(prompt);
			break;
		case CTRL('l'):
			editorRecenter(currentWindow);
			break;
		}
	}

QR_CLEANUP:
	editorSetStatusMessage("");
	buf->query = NULL;
	buf->markx = savedMx;
	buf->marky = savedMy;
	free(orig);
	free(repl);
	free(prompt);
}

void editorCapitalizeRegion(struct editorConfig *ed, struct editorBuffer *buf) {
	editorTransformRegion(ed, buf, transformerCapitalCase);
}

void editorWhitespaceCleanup(struct editorConfig *UNUSED(ed),
			     struct editorBuffer *buf) {
	unsigned int trailing = 0;
	for (int i = 0; i < buf->numrows; i++) {
		erow *row = &buf->row[i];
		for (int j = row->size - 1; j >= 0; j--) {
			if (row->chars[j] == ' ' || row->chars[j] == '\t') {
				row->size--;
				trailing++;
			} else {
				break;
			}
		}
	}

	if (buf->cx > buf->row[buf->cy].size) {
		buf->cx = buf->row[buf->cy].size;
	}

	if (trailing > 0) {
		clearUndosAndRedos(buf);
		editorSetStatusMessage("%d trailing characters removed",
				       trailing);
	} else {
		editorSetStatusMessage("No change.");
	}
}

void editorToggleTruncateLines(struct editorConfig *UNUSED(ed),
			       struct editorBuffer *buf) {
	buf->truncate_lines = !buf->truncate_lines;
	editorSetStatusMessage(buf->truncate_lines ?
				       "Truncate long lines enabled" :
				       "Truncate long lines disabled");
}

void editorDescribeKey(struct editorConfig *ed,
		       struct editorBuffer *UNUSED(buf)) {
	editorSetStatusMessage("Describe key: ");
	ed->describe_key_mode = 1;
}

void editorViewManPage(struct editorConfig *ed, struct editorBuffer *buf) {
	FILE *fp = popen("man -w emsys 2>/dev/null", "r");
	if (!fp) {
		editorSetStatusMessage("Cannot check for man page");
		return;
	}

	char path[256];
	if (!fgets(path, sizeof(path), fp) || strlen(path) < 2) {
		pclose(fp);
		editorSetStatusMessage("No man page found for emsys");
		return;
	}
	pclose(fp);

	struct editorBuffer *manpage = newBuffer();
	manpage->filename = stringdup("*man emsys*");
	manpage->special_buffer = 1;

	fp = popen("man emsys 2>/dev/null | col -b", "r");
	if (!fp) {
		destroyBuffer(manpage);
		editorSetStatusMessage("Cannot run man command");
		return;
	}

	char line[1024];
	while (fgets(line, sizeof(line), fp)) {
		int len = strlen(line);
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
			len--;
		}
		editorInsertRow(manpage, manpage->numrows, line, len);
	}
	pclose(fp);

	if (manpage->numrows == 0) {
		destroyBuffer(manpage);
		editorSetStatusMessage("Man page is empty");
		return;
	}

	ed->focusBuf = manpage;
	for (int i = 0; i < ed->nwindows; i++) {
		if (ed->windows[i]->focused) {
			ed->windows[i]->buf = manpage;
		}
	}
}

void editorHelpForHelp(struct editorConfig *ed, struct editorBuffer *buf) {
	editorSetStatusMessage(
		"C-h k: describe key, C-h m: view man page, M-x TAB: list commands");
}

#define ADDCMD(name, func)               \
	newCmd = malloc(sizeof *newCmd); \
	newCmdName = name;               \
	newCmd->cmd = func;              \
	HASH_ADD_KEYPTR(hh, ed->cmd, newCmdName, strlen(newCmdName), newCmd)

#ifdef EMSYS_DEBUG_UNDO
void debugUnpair(struct editorConfig *UNUSED(ed), struct editorBuffer *buf) {
	int undos = 0;
	int redos = 0;
	for (struct editorUndo *i = buf->undo; i; i = i->prev) {
		i->paired = 0;
		undos++;
	}
	for (struct editorUndo *i = buf->redo; i; i = i->prev) {
		i->paired = 0;
		redos++;
	}
	editorSetStatusMessage("Unpaired %d undos, %d redos.", undos, redos);
}
#endif

// Comparison function for qsort and bsearch
static int compare_commands(const void *a, const void *b) {
	return strcmp(((struct editorCommand *)a)->key,
		      ((struct editorCommand *)b)->key);
}

void setupCommands(struct editorConfig *ed) {
	static struct editorCommand commands[] = {
		{ "capitalize-region", editorCapitalizeRegion },
		{ "describe-key", editorDescribeKey },
		{ "help", editorViewManPage },
		{ "help-for-help", editorHelpForHelp },
		{ "indent-spaces", editorIndentSpaces },
		{ "indent-tabs", editorIndentTabs },
		{ "isearch-forward-regexp", editorRegexFindCommand },
		{ "kanaya", editorCapitalizeRegion },
		{ "man", editorViewManPage },
		{ "query-replace", editorQueryReplace },
		{ "recenter", editorRecenterCommand },
		{ "replace-regexp", editorReplaceRegex },
		{ "replace-string", editorReplaceString },
		{ "revert", editorRevert },
		{ "toggle-truncate-lines", editorToggleTruncateLines },
		{ "version", editorVersion },
		{ "view-man-page", editorViewManPage },
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

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stddef.h>

/* Forward declarations */
struct editorWindow;
struct editorBuffer;
struct editorConfig;

/* Append buffer for efficient screen updates */
struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

/* Display constants */
extern const int minibuffer_height;
extern const int statusbar_height;

/* Append buffer operations */
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

/* Display functions */
void editorRefreshScreen(void);
void editorDrawRows(struct editorWindow *win, struct abuf *ab, int screenrows, int screencols);
void editorDrawStatusBar(struct abuf *ab, struct editorWindow *win);
void editorDrawMinibuffer(struct abuf *ab);
void editorScroll(void);
void editorSetScxScy(struct editorWindow *win);
void editorCursorBottomLine(int curs);
void editorCursorBottomLineLong(long curs);
void editorSetStatusMessage(const char *fmt, ...);
void editorResizeScreen(void);
void editorRecenter(struct editorWindow *win);
void editorToggleTruncateLines(struct editorConfig *ed, struct editorBuffer *buf);
void editorVersion(struct editorConfig *ed, struct editorBuffer *buf);

/* Window management functions */
int windowFocusedIdx(struct editorConfig *ed);
void editorSwitchWindow(struct editorConfig *ed);
void synchronizeBufferCursor(struct editorBuffer *buf, struct editorWindow *win);

#endif /* DISPLAY_H */
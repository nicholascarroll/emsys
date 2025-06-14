#ifndef EMSYS_H
#define EMSYS_H 1

#include <stdint.h>
#include <termios.h>
#include <time.h>
#include "config.h"
#include "keymap.h"

/*** util ***/

#define EMSYS_TAB_STOP 8

#ifndef EMSYS_VERSION
#define EMSYS_VERSION "unknown"
#endif

#ifndef EMSYS_BUILD_DATE
#define EMSYS_BUILD_DATE "unknown"
#endif

#define ESC "\033"
#define CSI ESC "["
#define CRLF "\r\n"
#define ISCTRL(c) ((0 < c && c < 0x20) || c == 0x7f)

enum promptType {
	PROMPT_BASIC,
	PROMPT_FILES,
};
/*** data ***/

typedef struct erow {
	int size;
	int rsize;
	int renderwidth;
	uint8_t *chars;
	uint8_t *render;
} erow;

struct editorUndo {
	struct editorUndo *prev;
	int startx;
	int starty;
	int endx;
	int endy;
	int append;
	int datalen;
	int datasize;
	int delete;
	int paired;
	uint8_t *data;
};

struct editorBuffer {
	int indent;
	int cx, cy;
	int markx, marky;
	int numrows;
	int end;
	int dirty;
	int uarg;
	int uarg_active;
	int special_buffer;
	int truncate_lines; // 0 for wrapped, 1 for unwrapped
	int word_wrap;
	erow *row;
	char *filename;
	uint8_t *query;
	uint8_t match;
	struct editorUndo *undo;
	struct editorUndo *redo;
	struct editorBuffer *next;
};

struct editorWindow {
	int focused;
	struct editorBuffer *buf;
	int scx, scy;
	int cx, cy; // Buffer cx,cy  (only updated when switching windows)
	int rowoff;
	int coloff;
	int height;
};

struct editorMacro {
	int *keys;
	int nkeys;
	int skeys;
};

struct editorConfig;

struct editorCommand {
	const char *key;
	void (*cmd)(struct editorConfig *, struct editorBuffer *);
};

enum registerType {
	REGISTER_NULL,
	REGISTER_REGION,
	REGISTER_NUMBER,
	REGISTER_POINT,
	REGISTER_MACRO,
	REGISTER_RECTANGLE,
};

struct editorPoint {
	int cx;
	int cy;
	struct editorBuffer *buf;
};

struct editorRectangle {
	int rx;
	int ry;
	uint8_t *rect;
};

union registerData {
	uint8_t *region;
	int64_t number;
	struct editorMacro *macro;
	struct editorPoint *point;
	struct editorRectangle *rect;
};

struct editorRegister {
	enum registerType rtype;
	union registerData rdata;
};

struct editorConfig {
	uint8_t *kill;
	uint8_t *rectKill;
	int rx;
	int ry;
	int screenrows;
	int screencols;
	uint8_t unicode[4];
	int nunicode;
	char minibuffer[80];
	time_t statusmsg_time;
	struct termios orig_termios;
	struct editorBuffer *firstBuf;
	struct editorBuffer *focusBuf;
	int nwindows;
	struct editorWindow **windows;
	int recording;
	struct editorMacro macro;
	int playback;
	int micro;
	struct editorCommand *cmd;
	int cmd_count;
	struct editorRegister registers[127];
	struct editorBuffer *lastVisitedBuffer;
};

/*** prototypes ***/

uint8_t *editorPrompt(struct editorBuffer *bufr, uint8_t *prompt,
		      enum promptType t,
		      void (*callback)(struct editorBuffer *, uint8_t *, int));
void editorUpdateBuffer(struct editorBuffer *buf);
void editorInsertNewline(struct editorBuffer *bufr);
void editorInsertChar(struct editorBuffer *bufr, int c);
void editorOpen(struct editorBuffer *bufr, char *filename);
void die(const char *s);
struct editorBuffer *newBuffer();
void destroyBuffer(struct editorBuffer *);
int editorReadKey();
void editorRecordKey(int c);
void editorExecMacro(struct editorMacro *macro);
char *stringdup(const char *s);

#endif

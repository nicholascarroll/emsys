#ifndef EMSYS_REGION_H
#define EMSYS_REGION_H 1

#include <stdint.h>
#include "emsys.h"

int markInvalid(struct editorBuffer *buf);

int markInvalidSilent(struct editorBuffer *buf);

void editorSetMark(struct editorBuffer *buf);

void editorClearMark(struct editorBuffer *buf);

void editorMarkRectangle(struct editorBuffer *buf);

void editorKillRegion(struct editorConfig *ed, struct editorBuffer *buf);

void editorCopyRegion(struct editorConfig *ed, struct editorBuffer *buf);

void editorYank(struct editorConfig *ed, struct editorBuffer *buf);

void editorTransformRegion(struct editorConfig *ed, struct editorBuffer *buf,
			   uint8_t *(*transformer)(uint8_t *));

void editorReplaceRegex(struct editorConfig *ed, struct editorBuffer *buf);

void editorStringRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorCopyRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorKillRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorYankRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorMarkWholeBuffer(struct editorBuffer *buf);

#endif

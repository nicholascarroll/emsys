#ifndef EDITOR_H
#define EDITOR_H

#include "emsys.h"

/* Movement commands */
void forwardWord(int times);
void backWord(int times);
void forwardPara(int times);
void backPara(int times);

/* Basic editing */
void insertChar(int c);
void insertUnicode(void);
void insertNewline(int times);
void insertNewlineAndIndent(void);
void delChar(int times);
void backSpace(int times);
void killLine(int times);
void killLineBackwards(int times);
void openLine(int times);

/* Word operations */
void deleteWord(int times);
void backspaceWord(int times);
void upcaseWord(int times);
void downcaseWord(int times);
void capitalCaseWord(int times);
void transposeChars(void);
void transposeWords(void);

/* Tab operations */
void indent(int rept);
void unindent(int rept);

/* Helper functions (internal use) */
void endOfForwardWord(struct editorBuffer *buf, int *dx, int *dy);
void endOfBackwardWord(struct editorBuffer *buf, int *dx, int *dy);
void wordTransform(int times, uint8_t *(*transformer)(uint8_t *));

#endif /* EDITOR_H */
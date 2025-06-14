#ifndef FILEIO_H
#define FILEIO_H

#include "emsys.h"

/* File operations */
void editorOpenFile(struct editorBuffer *bufr, char *filename);
void save(struct editorBuffer *buf);
char *rowsToString(struct editorBuffer *buf, int *buflen);

#endif /* FILEIO_H */
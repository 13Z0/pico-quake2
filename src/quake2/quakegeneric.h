#ifndef QUAKEGENERIC_H
#define QUAKEGENERIC_H


int QG_Milliseconds(void);
void QG_GetMouseDiff(int* dx, int* dy);
void QG_CaptureMouse(void);
void QG_ReleaseMouse(void);

void QG_Mkdir(const char* path);

// pico-quake2: directory listing for Sys_FindFirst/Sys_FindNext. pattern is a path
// whose last component may contain * and ? ("save/current/*.sav"). Each call returns
// the full path of the next match ("save/current/base1.sav") and sets *isdir, or NULL
// when there are no more. Needed for savegames and FS_ListFiles.
const char *QG_FindFirst(const char *pattern, int *isdir);
const char *QG_FindNext(int *isdir);
void QG_FindClose(void);

// pico-quake2: a complete line of console input (without the newline), or NULL.
// Polled every frame; the returned buffer is used before the next call.
const char *QG_ConsoleInput(void);

// pico-quake2: console commands run after default.cfg and before config.cfg
// (platform key bindings and cvar defaults), "" for none.
const char *QG_PlatformConfig(void);

#endif
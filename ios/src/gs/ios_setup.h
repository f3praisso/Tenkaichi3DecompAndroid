/* ios_setup.c (the disc to gamedata) and ios_setup_ui.c (its screen) */
#ifndef IOS_SETUP_H
#define IOS_SETUP_H
#include <stddef.h>
#include <stdint.h>

/* what the screen reads while the work goes on (written by the worker thread, read by the main thread) */
typedef struct {
    volatile uint64_t done, total;
    volatile int finished, ok;
    char what[96];  /* the file being written */
    char fail[512]; /* why it did not work (valid when finished && !ok) */
} IosProgress;

int IosData_Ready(const char *gamedata);
int IosData_FindIso(const char *dir, char *out, size_t cap);
int IosData_Extract(const char *isoPath, const char *gamedata, IosProgress *p);
void IosData_Translations(const char *gamedata);
#endif

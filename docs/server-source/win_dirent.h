/* win_dirent.h — minimal dirent for Windows (FindFirstFile) — Copyright (c) 2026 ETC */
#ifndef WIN_DIRENT_H
#define WIN_DIRENT_H
#ifdef _WIN32
#include <windows.h>
#include <string.h>

struct dirent {
    char d_name[1024];
};

typedef struct DIR {
    HANDLE h;
    struct dirent ent;
    int first;
    char pattern[MAX_PATH];
} DIR;

static inline DIR *opendir(const char *path) {
    DIR *d = (DIR *)calloc(1, sizeof(DIR));
    if (!d) return NULL;
    _snprintf(d->pattern, MAX_PATH, "%s\\*", path);
    d->h = INVALID_HANDLE_VALUE;
    d->first = 1;
    return d;
}
static inline struct dirent *readdir(DIR *d) {
    if (!d) return NULL;
    WIN32_FIND_DATAW fd;
    if (d->first) {
        d->first = 0;
        wchar_t wpat[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, d->pattern, -1, wpat, MAX_PATH);
        d->h = FindFirstFileW(wpat, &fd);
        if (d->h == INVALID_HANDLE_VALUE) return NULL;
    } else {
        if (!FindNextFileW(d->h, &fd)) return NULL;
    }
    WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, d->ent.d_name, sizeof d->ent.d_name, NULL, NULL);
    return &d->ent;
}
static inline int closedir(DIR *d) {
    if (!d) return 0;
    if (d->h != INVALID_HANDLE_VALUE) FindClose(d->h);
    free(d);
    return 0;
}
#endif
#endif

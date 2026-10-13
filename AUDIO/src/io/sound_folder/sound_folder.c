#include "io/sound_folder/sound_folder.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 256

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

static int compare_nocase(const char* a, const char* b) {
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

static int compare_names(const void* a, const void* b) {
    return compare_nocase((const char*)a, (const char*)b);
}

// extension NULL = a sound file (.wav / .mp3)
static bool wanted(const char* name, const char* extension) {
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    if (extension) return compare_nocase(dot, extension) == 0;
    return compare_nocase(dot, ".wav") == 0 || compare_nocase(dot, ".mp3") == 0;
}

bool sound_folder_exists(const char* folder) {
    DWORD attributes = GetFileAttributesA(folder);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

uint32_t sound_folder_list(const char* folder, char (*paths)[AT_SOUND_FOLDER_PATH], uint32_t max) {
    return sound_folder_list_ext(folder, NULL, paths, max);
}

uint32_t sound_folder_list_ext(const char* folder, const char* extension, char (*paths)[AT_SOUND_FOLDER_PATH], uint32_t max) {
    static char names[MAX_FILES][AT_SOUND_FOLDER_PATH];       // control thread only
    uint32_t    count = 0;

    char pattern[AT_SOUND_FOLDER_PATH];
    snprintf(pattern, sizeof pattern, "%s\\*", folder);

    WIN32_FIND_DATAA found;
    HANDLE           search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return 0;

    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!wanted(found.cFileName, extension)) continue;
        snprintf(names[count++], AT_SOUND_FOLDER_PATH, "%s", found.cFileName);
    } while (count < MAX_FILES && FindNextFileA(search, &found));
    FindClose(search);

    qsort(names, count, sizeof names[0], compare_names);

    uint32_t n = 0;
    for (uint32_t i = 0; i < count && n < max; i++) {
        int length = snprintf(paths[n], AT_SOUND_FOLDER_PATH, "%s\\%s", folder, names[i]);
        if (length > 0 && length < AT_SOUND_FOLDER_PATH) n++;     // a cut-off path would not open: skip it
    }
    return n;
}

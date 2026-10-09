/* Loading one of Windows' own libraries at run time, for the functions that
 * old systems lack and the program therefore cannot simply link against.
 *
 * The library is asked for by its full path in the system directory. Asked
 * for by name alone, a library the system does not have (combase.dll before
 * Windows 8, say) is searched for further, in the end in the current
 * directory, which for a player started by opening a file is the folder
 * that file is in: a library of that name planted beside a song would be
 * loaded and run. */
#ifndef WIN32_LIBRARY_H
#define WIN32_LIBRARY_H

#include <windows.h>

static inline HMODULE win32_system_library(const char *name)
{
    char path[MAX_PATH + 32];
    UINT length = GetSystemDirectoryA(path, MAX_PATH);

    if (!length || length >= MAX_PATH || lstrlenA(name) > 30)
        return NULL;
    lstrcatA(path, "\\");
    lstrcatA(path, name);
    return LoadLibraryA(path);
}

#endif

/*
 * utils.h
 *
 *  Created on: Jun 30, 2026
 *      Author: brettsodie
 */


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sdcard.h"
#include "ff.h"

static int asciiToLower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }

    return c;
}

static int hasMp3Extension(const char *name) {
    const char *ext = strrchr(name, '.');

    if (ext == NULL) {
        return 0;
    }

    return asciiToLower(ext[1]) == 'm' &&

           asciiToLower(ext[2]) == 'p' &&

           ext[3] == '3' &&
           ext[4] == '\0';
}

static int isAppleDoubleFile(const char *name) {
    return name[0] == '.' && name[1] == '_';
}

int shouldSkipAudioFile(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return 1;
    }

    if (isAppleDoubleFile(name)) {
        return 1;
    }

    if (!hasMp3Extension(name)) {
        return 1;
    }

    return 0;
}

void listMp3Files(char filenames[][64], uint32_t *count) {
    *count = 0;

    DIR dir;
    FILINFO fno;

    FRESULT res = f_opendir(&dir, "/");
    if (res != FR_OK) {
        printf("Failed to open directory: %d\r\n", res);
        return;
    }

    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_DIR) continue;

        if (!shouldSkipAudioFile(&fno.fname[0])) {
            if (*count >= 99) break;
            strncpy(filenames[*count], fno.fname, 63);
            filenames[*count][63] = '\0';
            (*count)++;
        }
    }

    f_closedir(&dir);
}

/*
 * audio.c
 *
 *  Created on: Jul 1, 2026
 *      Author: brettsodie
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "audio.h"
#include "ff.h"
#include "sdcard.h"

typedef struct {
    FIL file;
    bool isOpen;
    bool isPlaying;
    char currentFilename[256];
} AudioStream;

static AudioStream audioStream = {0};

static volatile bool songChangeRequested = false;
static char pendingFilename[256];

static uint32_t pendingDuration = 0;
static volatile bool durationReady = false;

uint32_t audioGetPendingDuration(void) {
    durationReady = false;
    return pendingDuration;
}

bool audioIsDurationReady(void) {
    return durationReady;
}

void audioRequestPlayFile(const char *filename) {
    strncpy(pendingFilename, filename, sizeof(pendingFilename) - 1);
    pendingFilename[sizeof(pendingFilename) - 1] = '\0';
    songChangeRequested = true;
}

void audioPlayFile(const char *filename) {
	audioStream.isPlaying = false;

    if (audioStream.isOpen) {
        f_close(&audioStream.file);
        audioStream.isOpen = false;
    }

    vs1053WriteRegister(0x04, 0x0000);

    if (f_open(&audioStream.file, filename, FA_READ) != FR_OK) {
        printf("Failed to open file: %s\r\n", filename);
        audioStream.isPlaying = false;
        return;
    }

    audioStream.isOpen = true;
    audioStream.isPlaying = true;
}

static void vs1053CancelDecode(void) {
    uint16_t mode = vs1053ReadRegister(0x00);
    vs1053WriteRegister(0x00, mode | 0x0008);  // SM_CANCEL bit

    uint32_t timeout = 10000;
    while ((vs1053ReadRegister(0x00) & 0x0008) && --timeout);
}

void audioSetPlaying(bool playing) {
    if (!audioStream.isOpen) return;
    audioStream.isPlaying = playing;
}

void audioStop(void) {
    if (audioStream.isOpen) {
        f_close(&audioStream.file);
        audioStream.isOpen = false;
    }
    audioStream.isPlaying = false;
}

bool audioIsPlaying(void) {
    return audioStream.isPlaying;
}

void audioProcess(void) {
	if (songChangeRequested) {
	    songChangeRequested = false;

	    bool isDifferentFile = (strcmp(audioStream.currentFilename, pendingFilename) != 0);

	    audioStream.isPlaying = false;

	    if (isDifferentFile) {
	        vs1053CancelDecode();
	    }

	    if (audioStream.isOpen) {
	        f_close(&audioStream.file);
	        audioStream.isOpen = false;
	    }

	    vs1053WriteRegister(0x04, 0x0000);

	    pendingDuration = getMp3Duration(pendingFilename);

	    if (f_open(&audioStream.file, pendingFilename, FA_READ) == FR_OK) {
	        strncpy(audioStream.currentFilename, pendingFilename, sizeof(audioStream.currentFilename) - 1);
	        audioStream.isOpen = true;
	        audioStream.isPlaying = true;
	        durationReady = true;
	    } else {
	        printf("Failed to open file: %s\r\n", pendingFilename);
	    }
	    return;
	}

    if (!audioStream.isPlaying || !audioStream.isOpen) return;

    uint8_t buffer[32];
    UINT bytesRead;

    f_read(&audioStream.file, buffer, sizeof(buffer), &bytesRead);

    if (bytesRead == 0) {
        f_close(&audioStream.file);
        audioStream.isOpen = false;
        audioStream.isPlaying = false;
        return;
    }

    vs1053SendData(buffer, bytesRead);
}

uint32_t audioGetPosition(void) {
    return vs1053ReadRegister(0x04);  // SCI_DECODE_TIME, returns seconds
}

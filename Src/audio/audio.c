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

typedef struct {
    FIL file;
    bool isOpen;
    bool isPlaying;
} AudioStream;

static AudioStream audioStream = {0};

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

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

static uint8_t vs1053ReadEndFillByte(void) {
    vs1053WriteRegister(0x07, 0x1E06);            // SCI_WRAMADDR -> endFillByte param
    return (uint8_t)(vs1053ReadRegister(0x06) & 0xFF);  // SCI_WRAM low byte
}

static void vs1053CancelDecode(void) {
    uint8_t efb = vs1053ReadEndFillByte();
    uint8_t fill[32];
    memset(fill, efb, sizeof(fill));

    uint16_t mode = vs1053ReadRegister(0x00);
    vs1053WriteRegister(0x00, mode | 0x0008);     // SM_CANCEL

    // Feed endFillByte and poll, up to 2048 bytes
    bool cancelled = false;
    for (int i = 0; i < 64; i++) {                // 64 * 32 = 2048
        vs1053SendData(fill, sizeof(fill));
        if ((vs1053ReadRegister(0x00) & 0x0008) == 0) { cancelled = true; break; }
    }

    if (!cancelled) {
        // Decoder wedged — nuclear option, soft reset
        vs1053WriteRegister(0x00, vs1053ReadRegister(0x00) | 0x0004);  // SM_RESET
        // NOTE: after reset you must re-apply SCI_CLOCKF, SCI_VOL, etc.
    }

    // Drain the last partial frame
    for (int i = 0; i < 65; i++) vs1053SendData(fill, sizeof(fill));  // ~2080 >= 2052
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

        uint16_t savedVol = vs1053ReadRegister(0x0B);      // SCI_VOL
        vs1053WriteRegister(0x0B, 0xFEFE);                 // mute across the transition

        if (isDifferentFile) {
            vs1053CancelDecode();                          // feeds endFillByte + flushes >=2052
        }

        if (audioStream.isOpen) {
            f_close(&audioStream.file);
            audioStream.isOpen = false;
        }

        vs1053WriteRegister(0x04, 0x0000);                 // clear SCI_DECODE_TIME
        vs1053WriteRegister(0x04, 0x0000);                 // write twice to win the once/sec update race

        pendingDuration = getMp3Duration(pendingFilename);

        if (f_open(&audioStream.file, pendingFilename, FA_READ) == FR_OK) {
            strncpy(audioStream.currentFilename, pendingFilename, sizeof(audioStream.currentFilename) - 1);
            audioStream.currentFilename[sizeof(audioStream.currentFilename) - 1] = '\0';
            audioStream.isOpen = true;
            audioStream.isPlaying = true;
            durationReady = true;

            // Prime the FIFO before unmuting so the decoder doesn't start on an empty buffer
            uint8_t primeBuf[32];
            UINT primeRead;
            for (int i = 0; i < 64; i++) {                 // ~2KB, fills the input FIFO
                if (f_read(&audioStream.file, primeBuf, sizeof(primeBuf), &primeRead) != FR_OK || primeRead == 0) {
                    break;
                }
                vs1053SendData(primeBuf, primeRead);
            }
        } else {
            printf("Failed to open file: %s\r\n", pendingFilename);
        }

        vs1053WriteRegister(0x0B, savedVol);               // restore volume
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

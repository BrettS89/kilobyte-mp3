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
#include "stm32f4xx.h"
#include "audio.h"
#include "ff.h"
#include "sdcard.h"

#define STAGE_SIZE 512

static uint8_t stageBuf[STAGE_SIZE];
static UINT stageLen = 0;   // bytes currently in stageBuf
static UINT stagePos = 0;   // next byte to send

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

// DREQ with a timeout so a wedged chip can't freeze the whole device.
// Returns true if DREQ went high, false on timeout.
static bool vs1053WaitForDREQTimeout(void) {
    for (volatile uint32_t i = 0; i < 2000000; i++) {   // ~100ms-ish, tune later
        if (GPIOA->IDR & (1U << 11)) return true;
    }
    printf("VS1053 DREQ timeout!\r\n");
    return false;
}

// Send up to 32 bytes with NO DREQ polling inside.
// Contract: caller has already verified DREQ is high, which per the
// data sheet guarantees the chip can accept at least 32 bytes.
static void vs1053SendChunk32(const uint8_t *data, uint16_t length) {
    GPIOB->BSRR = (1U << (2 + 16));   // XDCS low
    for (uint16_t i = 0; i < length; i++) {
        spi2Transfer(data[i]);
    }
    GPIOB->BSRR = (1U << 2);          // XDCS high
}

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
    vs1053WriteRegister(0x00, mode | 0x0008);         // SM_CANCEL

    bool cancelled = false;
    for (int i = 0; i < 64; i++) {                    // up to 2048 bytes
        if (!vs1053WaitForDREQTimeout()) break;
        vs1053SendChunk32(fill, sizeof(fill));
        if ((vs1053ReadRegister(0x00) & 0x0008) == 0) { cancelled = true; break; }
    }

    if (!cancelled) {
        // Nuclear option: soft reset — then RESTORE the chip config,
        // otherwise CLOCKF reverts and the chip can't decode in real
        // time (and SCI comms may go out of spec).
        vs1053WriteRegister(0x00, 0x0804);            // SM_SDINEW | SM_RESET
        for (volatile int i = 0; i < 500000; i++);
        vs1053WaitForDREQTimeout();
        vs1053WriteRegister(0x03, 0x9800);            // re-apply SCI_CLOCKF
        for (volatile int i = 0; i < 100000; i++);
        vs1053WriteRegister(0x0B, 0x1414);            // re-apply volume
        return;                                        // reset already cleared everything
    }

    // Drain the last partial frame
    for (int i = 0; i < 65; i++) {                    // ~2080 >= 2052
        if (!vs1053WaitForDREQTimeout()) break;
        vs1053SendChunk32(fill, sizeof(fill));
    }
}

// Called when a song ends naturally. Per datasheet: send 2052 bytes of
// endFillByte so the decoder can finish its final partial frame, then
// cancel to leave the chip in a clean state for the next song.
static void vs1053FinishSong(void) {
    uint8_t efb = vs1053ReadEndFillByte();
    uint8_t fill[32];
    memset(fill, efb, sizeof(fill));

    for (int i = 0; i < 65; i++) {                    // 65*32 = 2080 >= 2052
        if (!vs1053WaitForDREQTimeout()) return;
        vs1053SendChunk32(fill, sizeof(fill));
    }
    vs1053CancelDecode();
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
    audioStream.currentFilename[0] = '\0';
    stageLen = stagePos = 0;
}

bool audioIsPlaying(void) {
    return audioStream.isPlaying;
}

void audioProcess(void) {
    // ---------- deferred song change ----------
    if (songChangeRequested) {
        songChangeRequested = false;

        audioStream.isPlaying = false;
        stageLen = stagePos = 0;                       // discard staged bytes of old song

        uint16_t savedVol = vs1053ReadRegister(0x0B);  // SCI_VOL
        vs1053WriteRegister(0x0B, 0xFEFE);             // mute across the transition

        // Cancel unconditionally. The old same-file shortcut saved little
        // and could append a new stream onto stale mid-frame decoder state.
        vs1053CancelDecode();

        if (audioStream.isOpen) {
            f_close(&audioStream.file);
            audioStream.isOpen = false;
        }

        vs1053WriteRegister(0x04, 0x0000);             // clear SCI_DECODE_TIME
        vs1053WriteRegister(0x04, 0x0000);             // twice: wins the once/sec update race

        pendingDuration = getMp3Duration(pendingFilename);

        if (f_open(&audioStream.file, pendingFilename, FA_READ) == FR_OK) {
            strncpy(audioStream.currentFilename, pendingFilename,
                    sizeof(audioStream.currentFilename) - 1);
            audioStream.currentFilename[sizeof(audioStream.currentFilename) - 1] = '\0';
            audioStream.isOpen = true;
            audioStream.isPlaying = true;
            durationReady = true;

            // Prime: fill the decoder's input FIFO before unmuting so it
            // doesn't start on an empty buffer. 512B reads -> multi-sector
            // CMD18 path, 32B chunks out, gated on DREQ.
            for (int chunk = 0; chunk < 64; chunk++) {         // up to ~2KB
                if (!(GPIOA->IDR & (1U << 11))) break;         // FIFO full already
                UINT br = 0;
                uint8_t primeBuf[32];
                if (f_read(&audioStream.file, primeBuf, sizeof(primeBuf), &br) != FR_OK || br == 0)
                    break;
                vs1053SendChunk32(primeBuf, br);
            }
        } else {
            printf("Failed to open file: %s\r\n", pendingFilename);
            audioStream.currentFilename[0] = '\0';
        }

        vs1053WriteRegister(0x0B, savedVol);           // restore volume
        return;
    }

    // ---------- steady-state streaming ----------
    if (!audioStream.isPlaying || !audioStream.isOpen) return;

    // Top-up loop: feed while the codec will take data, capped at 16
    // chunks (512B) per pass so we never hog the main loop. If DREQ is
    // low we return immediately — zero blocking, unlike the old version
    // which spun waiting for the codec.
    for (int chunk = 0; chunk < 16; chunk++) {
        if (!(GPIOA->IDR & (1U << 11))) return;        // DREQ low: FIFO full

        // Refill staging buffer from SD when drained (one 512B f_read —
        // sector-sized, so FatFs passes it through efficiently instead
        // of 16 tiny buffered reads per sector)
        if (stagePos >= stageLen) {
            FRESULT res = f_read(&audioStream.file, stageBuf, STAGE_SIZE, &stageLen);
            stagePos = 0;

            if (res != FR_OK) {
                printf("audio f_read error: %d\r\n", res);
                audioStop();                            // real error: don't flush garbage
                return;
            }
            if (stageLen == 0) {
                // Natural end of song: flush the decoder's final frame
                f_close(&audioStream.file);
                audioStream.isOpen = false;
                audioStream.isPlaying = false;
                audioStream.currentFilename[0] = '\0';
                vs1053FinishSong();
                return;
            }
        }

        UINT n = stageLen - stagePos;
        if (n > 32) n = 32;
        vs1053SendChunk32(&stageBuf[stagePos], n);
        stagePos += n;
    }
}

uint32_t audioGetPosition(void) {
    return vs1053ReadRegister(0x04);  // SCI_DECODE_TIME, returns seconds
}

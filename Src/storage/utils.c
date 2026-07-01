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

uint32_t getMp3Duration(const char *filename) {
    FIL file;
    UINT bytesRead;
    uint8_t buf[4];

    if (f_open(&file, filename, FA_READ) != FR_OK) {
        printf("f_open failed\r\n");
        return 0;
    }

    // skip ID3v2 tag if present
    uint8_t id3Header[10];
    f_read(&file, id3Header, 10, &bytesRead);

    uint32_t audioStart = 0;

    if (id3Header[0] == 'I' && id3Header[1] == 'D' && id3Header[2] == '3') {
        uint32_t id3Size = ((id3Header[6] & 0x7F) << 21) |
                           ((id3Header[7] & 0x7F) << 14) |
                           ((id3Header[8] & 0x7F) << 7)  |
                            (id3Header[9] & 0x7F);
        audioStart = 10 + id3Size;
        printf("ID3 tag found, size: %lu, audioStart: %lu\r\n", id3Size, audioStart);
    } else {
        printf("No ID3 tag found\r\n");
    }

    f_lseek(&file, audioStart);

    uint8_t b1, b2;
    uint32_t syncPos = audioStart;

    while (1) {
        f_read(&file, &b1, 1, &bytesRead);
        if (bytesRead == 0) {
            printf("EOF before valid sync found\r\n");
            f_close(&file);
            return 0;
        }

        if (b1 == 0xFF) {
            f_read(&file, &b2, 1, &bytesRead);
            if ((b2 & 0xE0) == 0xE0) {
                buf[0] = b1;
                buf[1] = b2;
                f_read(&file, &buf[2], 2, &bytesRead);

                uint8_t bi = (buf[2] >> 4) & 0x0F;
                uint8_t si = (buf[2] >> 2) & 0x03;

                if (bi == 0 || bi == 15 || si == 3) {
                    f_lseek(&file, f_tell(&file) - 3);
                    continue;
                }

                syncPos = f_tell(&file) - 4;
                printf("Valid sync found at: %lu\r\n", syncPos);
                break;
            }
        }
    }

    uint8_t bitrateIndex = (buf[2] >> 4) & 0x0F;
    uint8_t sampleIndex  = (buf[2] >> 2) & 0x03;
    uint8_t channelMode  = (buf[3] >> 6) & 0x03;

    static const uint16_t bitrateTable[] = {
        0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
    };
    static const uint32_t sampleRateTable[] = {
        44100, 48000, 32000, 0
    };

    uint16_t bitrate    = bitrateTable[bitrateIndex];
    uint32_t sampleRate = sampleRateTable[sampleIndex];

    printf("bitrate: %d sampleRate: %lu channelMode: %d\r\n", bitrate, sampleRate, channelMode);

    if (bitrate == 0 || sampleRate == 0) {
        printf("Invalid bitrate or sampleRate\r\n");
        f_close(&file);
        return 0;
    }

    // check for Xing/Info VBR header
    uint32_t xingOffset = (channelMode == 3) ? 17 : 32;

    printf("seeking to Xing at: %lu\r\n", syncPos + 4 + xingOffset);

    f_lseek(&file, syncPos + 4 + xingOffset);

    uint8_t xingId[4];
    f_read(&file, xingId, 4, &bytesRead);
    printf("Xing id: %c%c%c%c\r\n", xingId[0], xingId[1], xingId[2], xingId[3]);

    if ((xingId[0] == 'X' && xingId[1] == 'i' && xingId[2] == 'n' && xingId[3] == 'g') ||
        (xingId[0] == 'I' && xingId[1] == 'n' && xingId[2] == 'f' && xingId[3] == 'o')) {

        uint8_t flags[4];
        f_read(&file, flags, 4, &bytesRead);
        uint32_t flagWord = (flags[0] << 24) | (flags[1] << 16) | (flags[2] << 8) | flags[3];
        printf("Xing found, flagWord: 0x%08lX\r\n", flagWord);

        if (flagWord & 0x01) {
            uint8_t frameCountBytes[4];
            f_read(&file, frameCountBytes, 4, &bytesRead);
            uint32_t frameCount = ((uint32_t)frameCountBytes[0] << 24) |
                                  ((uint32_t)frameCountBytes[1] << 16) |
                                  ((uint32_t)frameCountBytes[2] << 8)  |
                                   (uint32_t)frameCountBytes[3];
            printf("frameCount: %lu\r\n", frameCount);
            f_close(&file);
            return (frameCount * 1152) / sampleRate;
        } else {
            printf("Xing found but no frame count flag\r\n");
        }
    } else {
        printf("No Xing header — sampling frames for VBR estimate\r\n");
    }

    // sample 10 frames to estimate average bitrate
    uint32_t totalBitrate = 0;
    uint32_t framesSampled = 0;
    uint32_t scanPos = syncPos;

    for (int sample = 0; sample < 10; sample++) {
        f_lseek(&file, scanPos);
        uint8_t frameBuf[4];
        f_read(&file, frameBuf, 4, &bytesRead);
        if (bytesRead < 4) break;

        uint8_t bi = (frameBuf[2] >> 4) & 0x0F;
        uint8_t si = (frameBuf[2] >> 2) & 0x03;

        if (bi == 0 || bi == 15 || si == 3) break;

        uint16_t frameBitrate   = bitrateTable[bi];
        uint32_t frameSampleRate = sampleRateTable[si];

        if (frameBitrate == 0 || frameSampleRate == 0) break;

        totalBitrate += frameBitrate;
        framesSampled++;

        // advance to next frame
        uint32_t frameSize = 144 * frameBitrate * 1000 / frameSampleRate;
        scanPos += frameSize;
    }

    FILINFO fno;
    f_stat(filename, &fno);

    if (framesSampled > 0) {
        uint32_t avgBitrate = totalBitrate / framesSampled;
        printf("avgBitrate from %lu frames: %lu\r\n", framesSampled, avgBitrate);
        uint32_t audioSize = fno.fsize - syncPos;
        uint32_t duration = (uint32_t)((uint64_t)audioSize * 8 / ((uint32_t)avgBitrate * 1000));
        printf("VBR estimated duration: %lu\r\n", duration);
        f_close(&file);
        return duration;
    }

    // final fallback — single frame CBR estimate
    uint32_t audioSize = fno.fsize - syncPos;
    uint32_t duration = (uint32_t)((uint64_t)audioSize * 8 / ((uint32_t)bitrate * 1000));
    printf("CBR fallback duration: %lu\r\n", duration);

    f_close(&file);
    return duration;
}

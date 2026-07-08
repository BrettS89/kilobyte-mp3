/*
 * track-list.c
 *
 *  Created on: Jul 5, 2026
 *      Author: brettsodie
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "state.h"
#include "sdcard.h"
#include "ff.h"
#include "index-tracks.h"

#define FILE_CHUNK_SIZE 100U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t trackCount;
} __attribute__((packed)) IndexHeader;

bool loadTotalTrackCount(uint32_t *totalTracks) {
    FIL file;

    if (f_open(&file, "/system/kilobyte.idx", FA_READ) != FR_OK) {
        return false;
    }

    FSIZE_t fileSize = f_size(&file);

    f_close(&file);

    *totalTracks = fileSize / sizeof(TrackRecord);

    return true;
}

void sortTracks(TrackRecord tracks[], uint32_t count) {
    for (uint32_t i = 1; i < count; i++) {
        TrackRecord temp = tracks[i];

        int j = i - 1;
        while (j >= 0 && strcmp(tracks[j].filename, temp.filename) > 0) {
            tracks[j + 1] = tracks[j];
            j--;
        }

        tracks[j + 1] = temp;
    }
}

bool writeFile(TrackRecord tracks[], uint32_t count, uint32_t fileNum) {
    FIL file;
    UINT written;

    char filename[32];
    snprintf(filename, sizeof(filename), "/system/%03d-kilobyte.idx.tmp", (int)fileNum);

    FRESULT res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);

    if (res != FR_OK) {
        return false;
    }

    res = f_write(&file, tracks, count * sizeof(TrackRecord), &written);

    if (res != FR_OK || written != count * sizeof(TrackRecord)) {
        f_close(&file);
        printf("file was not written\r\n");
        return false;
    }

    f_close(&file);

    return true;
}

bool readID3Tags(const char *filename, char *title, char *artist, char *album) {
    // NOTE: title, artist, album must each be at least 64 bytes
    FIL file;
    UINT bytesRead;

    title[0]  = '\0';
    artist[0] = '\0';
    album[0]  = '\0';

    if (f_open(&file, filename, FA_READ) != FR_OK) {
        return false;
    }

    uint8_t header[10];
    f_read(&file, header, 10, &bytesRead);

    if (bytesRead != 10 || header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        f_close(&file);
        return false;  // no ID3v2 tag present at all
    }

    uint8_t majorVersion = header[3];       // 3 = ID3v2.3, 4 = ID3v2.4
    uint8_t tagFlags     = header[5];

    // Tag size is always synchsafe (7 bits per byte)
    uint32_t tagSize = ((uint32_t)(header[6] & 0x7F) << 21) |
                       ((uint32_t)(header[7] & 0x7F) << 14) |
                       ((uint32_t)(header[8] & 0x7F) << 7)  |
                        (uint32_t)(header[9] & 0x7F);

    uint32_t bytesConsumed = 0;

    // Skip extended header if present
    if (tagFlags & 0x40) {
        uint8_t extHeader[4];
        f_read(&file, extHeader, 4, &bytesRead);
        if (bytesRead != 4) {
            f_close(&file);
            return false;
        }

        uint32_t extSize;
        if (majorVersion == 4) {
            // v2.4: synchsafe, size includes these 4 bytes
            extSize = ((uint32_t)(extHeader[0] & 0x7F) << 21) |
                      ((uint32_t)(extHeader[1] & 0x7F) << 14) |
                      ((uint32_t)(extHeader[2] & 0x7F) << 7)  |
                       (uint32_t)(extHeader[3] & 0x7F);
            f_lseek(&file, f_tell(&file) + (extSize - 4));
            bytesConsumed += extSize;
        } else {
            // v2.3: plain int, size EXCLUDES these 4 bytes
            extSize = ((uint32_t)extHeader[0] << 24) |
                      ((uint32_t)extHeader[1] << 16) |
                      ((uint32_t)extHeader[2] << 8)  |
                       (uint32_t)extHeader[3];
            f_lseek(&file, f_tell(&file) + extSize);
            bytesConsumed += 4 + extSize;
        }
    }

    while (bytesConsumed + 10 <= tagSize) {
        uint8_t frameHeader[10];
        f_read(&file, frameHeader, 10, &bytesRead);

        if (bytesRead != 10) break;

        // Frame ID of 0x00 means we've hit the padding region — done
        if (frameHeader[0] == 0) break;

        char frameId[5] = {
            frameHeader[0], frameHeader[1], frameHeader[2], frameHeader[3], '\0'
        };

        // v2.3 frame size: plain big-endian. v2.4: synchsafe.
        uint32_t frameSize;
        if (majorVersion == 4) {
            frameSize = ((uint32_t)(frameHeader[4] & 0x7F) << 21) |
                        ((uint32_t)(frameHeader[5] & 0x7F) << 14) |
                        ((uint32_t)(frameHeader[6] & 0x7F) << 7)  |
                         (uint32_t)(frameHeader[7] & 0x7F);
        } else {
            frameSize = ((uint32_t)frameHeader[4] << 24) |
                        ((uint32_t)frameHeader[5] << 16) |
                        ((uint32_t)frameHeader[6] << 8)  |
                         (uint32_t)frameHeader[7];
        }

        bytesConsumed += 10;

        if (frameSize == 0) break;                        // malformed frame — bail
        if (bytesConsumed + frameSize > tagSize) break;   // frame overruns tag — corrupt

        char *target = NULL;
        if      (strcmp(frameId, "TIT2") == 0) target = title;
        else if (strcmp(frameId, "TPE1") == 0) target = artist;
        else if (strcmp(frameId, "TALB") == 0) target = album;

        if (target != NULL && frameSize > 1) {
            uint8_t encoding;
            f_read(&file, &encoding, 1, &bytesRead);

            uint32_t textLen = frameSize - 1;
            uint8_t raw[128];
            uint32_t toRead = (textLen < sizeof(raw)) ? textLen : sizeof(raw);

            f_read(&file, raw, toRead, &bytesRead);

            if (encoding == 0x01 || encoding == 0x02) {
                // UTF-16: skip BOM if present, extract ASCII-range chars
                uint32_t i = 0;
                uint32_t charOffset = 0;   // which byte of each pair holds the char

                if (bytesRead >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
                    i = 2;                 // LE BOM: char is first byte of pair
                    charOffset = 0;
                } else if (bytesRead >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
                    i = 2;                 // BE BOM: char is second byte of pair
                    charOffset = 1;
                } else if (encoding == 0x02) {
                    charOffset = 1;        // UTF-16BE without BOM
                }

                uint32_t out = 0;
                for (; i + 1 < bytesRead && out < 63; i += 2) {
                    char c = (char)raw[i + charOffset];
                    if (c == '\0') break;
                    target[out++] = c;
                }
                target[out] = '\0';
            } else {
                // ISO-8859-1 (0x00) or UTF-8 (0x03): copy through
                uint32_t out = (bytesRead < 63) ? bytesRead : 63;
                memcpy(target, raw, out);
                target[out] = '\0';

                // Trim trailing nulls some taggers include in the frame
                while (out > 0 && target[out - 1] == '\0') out--;
            }

            // Skip whatever portion of the frame we didn't read
            if (textLen > bytesRead) {
                f_lseek(&file, f_tell(&file) + (textLen - bytesRead));
            }
        } else {
            // Not a frame we want (APIC, COMM, etc.) — skip it, however large
            f_lseek(&file, f_tell(&file) + frameSize);
        }

        bytesConsumed += frameSize;
    }

    f_close(&file);
    return true;
}

void createTempFiles(void) {
    uint32_t fileNum = 1;
    uint32_t count = 0;
    TrackRecord tracks[FILE_CHUNK_SIZE] = {0};

    DIR dir;
    FILINFO fno;

    FRESULT res = f_opendir(&dir, "/");

    if (res != FR_OK) {
        printf("Failed to open directory: %d\r\n", res);
        return;
    }

    while (1) {
        res = f_readdir(&dir, &fno);

        if (res != FR_OK) {
            break;
        }

        if (fno.fname[0] == 0) {
            if (count > 0) {
                sortTracks(tracks, count);
                writeFile(tracks, count, fileNum);
            }
            break;
        }

        if (fno.fattrib & AM_DIR) continue;

        if (!shouldSkipAudioFile(&fno.fname[0])) {
            strncpy(tracks[count].filename, fno.fname, 127);
            tracks[count].filename[127] = '\0';

            if (!readID3Tags(fno.fname, tracks[count].title, tracks[count].artist, tracks[count].album)) {
                strncpy(tracks[count].title, fno.fname, 63);
                tracks[count].title[63] = '\0';
            }

            count++;
        }

        if (count % FILE_CHUNK_SIZE == 0) {
            sortTracks(tracks, FILE_CHUNK_SIZE);
            writeFile(tracks, FILE_CHUNK_SIZE, fileNum);

            fileNum++;
            count = 0;

            memset(tracks, 0, sizeof(tracks));
        }
    }

    f_closedir(&dir);
}

typedef struct {
    FIL file;
    TrackRecord current;
    bool exhausted;
} MergeSource;

bool mergeFiles(void) {
    char inputPaths[10][64];
    uint32_t inputCount = 0;

    DIR dir;
    FILINFO fno;

    if (f_opendir(&dir, "/system") != FR_OK) {
        return false;
    }

    while (inputCount < 10) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_DIR) continue;

        snprintf(inputPaths[inputCount], 64, "/system/%s", fno.fname);
        inputCount++;
    }

    f_closedir(&dir);

    if (inputCount <= 1) return false;

    static uint32_t mergeCounter = 0;
    char outputPath[64];
    snprintf(outputPath, sizeof(outputPath), "/system/merged-%03d.tmp", (int)mergeCounter++);

    MergeSource sources[10] = {0};
    UINT bytesRead;

    for (uint32_t i = 0; i < inputCount; i++) {
        if (f_open(&sources[i].file, inputPaths[i], FA_READ) != FR_OK) {
            printf("failed to open %s\r\n", inputPaths[i]);
            return false;
        }

        f_read(&sources[i].file, &sources[i].current, sizeof(TrackRecord), &bytesRead);
        sources[i].exhausted = (bytesRead != sizeof(TrackRecord));
    }

    FIL outFile;
    if (f_open(&outFile, outputPath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        printf("failed to open output file\r\n");
        for (uint32_t i = 0; i < inputCount; i++) {
            f_close(&sources[i].file);
        }
        return false;
    }

    while (1) {
        int smallestIndex = -1;

        for (uint32_t i = 0; i < inputCount; i++) {
            if (sources[i].exhausted) continue;

            if (smallestIndex == -1 ||
                strcmp(sources[i].current.filename, sources[smallestIndex].current.filename) < 0) {
                smallestIndex = i;
            }
        }

        if (smallestIndex == -1) break;

        UINT written;
        f_write(&outFile, &sources[smallestIndex].current, sizeof(TrackRecord), &written);

        f_read(&sources[smallestIndex].file, &sources[smallestIndex].current, sizeof(TrackRecord), &bytesRead);
        sources[smallestIndex].exhausted = (bytesRead != sizeof(TrackRecord));
    }

    for (uint32_t i = 0; i < inputCount; i++) {
        f_close(&sources[i].file);
    }
    f_close(&outFile);

    for (uint32_t i = 0; i < inputCount; i++) {
        f_unlink(inputPaths[i]);
    }

    return true;
}

void assignIndices(const char *path) {
    FIL file;
    if (f_open(&file, path, FA_READ | FA_WRITE) != FR_OK) {
        return;
    }

    TrackRecord record;
    UINT bytesRead, bytesWritten;
    uint32_t index = 0;

    while (1) {
        FSIZE_t pos = (FSIZE_t)index * sizeof(TrackRecord);
        f_lseek(&file, pos);

        if (f_read(&file, &record, sizeof(TrackRecord), &bytesRead) != FR_OK || bytesRead != sizeof(TrackRecord)) {
            break;
        }

        record.index = index;

        f_lseek(&file, pos);
        f_write(&file, &record, sizeof(TrackRecord), &bytesWritten);

        index++;
    }

    f_close(&file);
}

void indexSongList() {
    f_unlink("/system/kilobyte.idx");  // TEMPORARY - testing only, remove before shipping

    createTempFiles();

    while (mergeFiles()) {
        // keep merging until only one file remains
    }

    DIR dir;
    FILINFO fno;

    if (f_opendir(&dir, "/system") == FR_OK) {
        if (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            char finalPath[64];
            snprintf(finalPath, sizeof(finalPath), "/system/%s", fno.fname);

            f_unlink("/system/kilobyte.idx");
            f_rename(finalPath, "/system/kilobyte.idx");
        }
        f_closedir(&dir);
    }

    assignIndices("/system/kilobyte.idx");
}


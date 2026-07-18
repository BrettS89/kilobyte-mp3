/*
 * fast-tag-read.c
 *
 *  Created on: Jul 17, 2026
 *      Author: brettsodie
 */


/*
 * fast-tag-read.c — read ID3 tags without f_open's directory search.
 *
 * f_open(name) linearly searches the directory for the entry we're
 * ALREADY POINTING AT during the f_readdir scan — O(N) per open, O(N²)
 * per scan in flat directories. This module skips the search: it pulls
 * the file's start cluster straight from the 32-byte FAT directory
 * entry (a layout fixed by the FAT on-disk specification, not by
 * FatFs internals), computes the data sector arithmetically, and reads
 * the file's first cluster with disk_read.
 *
 * FAT directory entry layout (FAT spec, 32 bytes):
 *   [20..21] start cluster, high 16 bits (FAT32)
 *   [26..27] start cluster, low 16 bits
 *   [28..31] file size (little-endian u32)
 *
 * Scope limits, by design:
 *   - Reads ONLY the first cluster. If the ID3 tag region extends past
 *     it (rare: giant album art + tags after it), we return NEED_FALLBACK
 *     and the caller uses the normal f_open path for that file.
 *   - Requires FF_FS_EXFAT == 0 (exFAT stores clusters differently).
 *   - Assumes a mounted single volume (fs from the global FATFS).
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "ff.h"
#include "diskio.h"
#include "index-tracks.h"

// Result codes: distinguish "tags read", "use f_open instead", "no tag"


// Sector staging buffer. One sector at a time keeps RAM cost at 512B;
// we early-exit as soon as all three tags are found, so most files
// cost 1-4 sector reads total.
static uint8_t secBuf[512];

// ---------------------------------------------------------------------------
// Pull start cluster + size from the raw directory entry the DIR is
// currently pointing at. Must be called immediately after f_readdir
// returned this entry, before the DIR advances.
// ---------------------------------------------------------------------------
static bool entryLocation(DIR *dir, uint32_t *startCluster, uint32_t *fileSize) {
    const uint8_t *e = dir->dir;   // FatFs: pointer to the 32-byte entry
    if (e == NULL) return false;

    uint32_t clLo = (uint32_t)e[26] | ((uint32_t)e[27] << 8);
    uint32_t clHi = (uint32_t)e[20] | ((uint32_t)e[21] << 8);
    *startCluster = (clHi << 16) | clLo;

    *fileSize = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
                ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);

    // cluster 0 = empty file or corrupt entry — nothing to read
    return (*startCluster >= 2);
}

// ---------------------------------------------------------------------------
// Streaming byte reader over the first cluster: gives the tag parser
// a "read next N bytes" interface backed by sector-at-a-time disk_read.
// ---------------------------------------------------------------------------
typedef struct {
    FATFS   *fs;
    uint32_t firstSector;    // of the start cluster
    uint32_t clusterBytes;   // csize * 512
    uint32_t fileSize;
    uint32_t pos;            // absolute position within the file
    uint32_t bufSector;      // which sector index (within cluster) is staged
    bool     bufValid;
} ClusterReader;

static bool crInit(ClusterReader *cr, FATFS *fs, uint32_t startCluster,
                   uint32_t fileSize) {
    cr->fs           = fs;
    cr->firstSector  = fs->database + (startCluster - 2) * fs->csize;
    cr->clusterBytes = (uint32_t)fs->csize * 512;
    cr->fileSize     = fileSize;
    cr->pos          = 0;
    cr->bufValid     = false;
    return true;
}

// Read n bytes at the current position. Returns bytes actually read;
// stops at first-cluster boundary or EOF. Caller checks for shortfall.
static uint32_t crRead(ClusterReader *cr, uint8_t *dst, uint32_t n) {
    uint32_t done = 0;

    while (done < n) {
        if (cr->pos >= cr->fileSize) break;          // EOF
        if (cr->pos >= cr->clusterBytes) break;      // first-cluster limit

        uint32_t secIdx  = cr->pos / 512;
        uint32_t secOff  = cr->pos % 512;

        if (!cr->bufValid || cr->bufSector != secIdx) {
            if (disk_read(cr->fs->pdrv, secBuf, cr->firstSector + secIdx, 1) != RES_OK) {
                break;
            }
            cr->bufSector = secIdx;
            cr->bufValid  = true;
        }

        uint32_t avail = 512 - secOff;
        uint32_t limit = cr->clusterBytes - cr->pos;
        uint32_t fileLeft = cr->fileSize - cr->pos;
        uint32_t take = n - done;
        if (take > avail)    take = avail;
        if (take > limit)    take = limit;
        if (take > fileLeft) take = fileLeft;

        memcpy(dst + done, &secBuf[secOff], take);
        done    += take;
        cr->pos += take;
    }

    return done;
}

static void crSkip(ClusterReader *cr, uint32_t n) {
    cr->pos += n;    // sector staging handles the jump on next read
}

// ---------------------------------------------------------------------------
// The tag parser, ported onto the ClusterReader. Same ID3 logic as
// readID3Tags — v2.3/v2.4 sizes, extended header, UTF-16, early exit —
// with f_read/f_lseek swapped for crRead/crSkip and one new rule:
// any attempt to touch data past the first cluster = NEED_FALLBACK.
// ---------------------------------------------------------------------------
FastTagResult fastReadID3Tags(DIR *dir, char *title, char *artist, char *album) {
    title[0] = artist[0] = album[0] = '\0';

    FATFS *fs = dir->obj.fs;
    if (fs == NULL) return FASTTAG_NEED_FALLBACK;

    uint32_t startCluster, fileSize;
    if (!entryLocation(dir, &startCluster, &fileSize)) {
        return FASTTAG_NEED_FALLBACK;
    }

    ClusterReader cr;
    crInit(&cr, fs, startCluster, fileSize);

    uint8_t header[10];
    if (crRead(&cr, header, 10) != 10) return FASTTAG_NEED_FALLBACK;

    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        return FASTTAG_NO_TAG;
    }

    uint8_t majorVersion = header[3];
    uint8_t tagFlags     = header[5];

    uint32_t tagSize = ((uint32_t)(header[6] & 0x7F) << 21) |
                       ((uint32_t)(header[7] & 0x7F) << 14) |
                       ((uint32_t)(header[8] & 0x7F) << 7)  |
                        (uint32_t)(header[9] & 0x7F);

    uint32_t bytesConsumed = 0;

    if (tagFlags & 0x40) {
        uint8_t extHeader[4];
        if (crRead(&cr, extHeader, 4) != 4) return FASTTAG_NEED_FALLBACK;

        uint32_t extSize;
        if (majorVersion == 4) {
            extSize = ((uint32_t)(extHeader[0] & 0x7F) << 21) |
                      ((uint32_t)(extHeader[1] & 0x7F) << 14) |
                      ((uint32_t)(extHeader[2] & 0x7F) << 7)  |
                       (uint32_t)(extHeader[3] & 0x7F);
            crSkip(&cr, extSize - 4);
            bytesConsumed += extSize;
        } else {
            extSize = ((uint32_t)extHeader[0] << 24) |
                      ((uint32_t)extHeader[1] << 16) |
                      ((uint32_t)extHeader[2] << 8)  |
                       (uint32_t)extHeader[3];
            crSkip(&cr, extSize);
            bytesConsumed += 4 + extSize;
        }
    }

    while (bytesConsumed + 10 <= tagSize) {
        // Anything still unparsed past the first cluster: punt to f_open
        if (cr.pos + 10 > cr.clusterBytes) return FASTTAG_NEED_FALLBACK;

        uint8_t frameHeader[10];
        if (crRead(&cr, frameHeader, 10) != 10) break;

        if (frameHeader[0] == 0) break;              // padding region

        char frameId[5] = {
            frameHeader[0], frameHeader[1], frameHeader[2], frameHeader[3], '\0'
        };

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

        if (frameSize == 0) break;
        if (bytesConsumed + frameSize > tagSize) break;

        char *target = NULL;
        if      (strcmp(frameId, "TIT2") == 0) target = title;
        else if (strcmp(frameId, "TPE1") == 0) target = artist;
        else if (strcmp(frameId, "TALB") == 0) target = album;

        if (target != NULL && frameSize > 1) {
            uint8_t encoding;
            if (crRead(&cr, &encoding, 1) != 1) break;

            uint32_t textLen = frameSize - 1;
            uint8_t raw[128];
            uint32_t toRead = (textLen < sizeof(raw)) ? textLen : sizeof(raw);

            uint32_t got = crRead(&cr, raw, toRead);

            if (encoding == 0x01 || encoding == 0x02) {
                uint32_t i = 0;
                uint32_t charOffset = 0;

                if (got >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
                    i = 2; charOffset = 0;
                } else if (got >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
                    i = 2; charOffset = 1;
                } else if (encoding == 0x02) {
                    charOffset = 1;
                }

                uint32_t out = 0;
                for (; i + 1 < got && out < 63; i += 2) {
                    char c = (char)raw[i + charOffset];
                    if (c == '\0') break;
                    target[out++] = c;
                }
                target[out] = '\0';
            } else {
                uint32_t out = (got < 63) ? got : 63;
                memcpy(target, raw, out);
                target[out] = '\0';
                while (out > 0 && target[out - 1] == '\0') out--;
            }

            if (textLen > got) crSkip(&cr, textLen - got);
        } else {
            crSkip(&cr, frameSize);
        }

        if (title[0] && artist[0] && album[0]) break;   // early exit

        bytesConsumed += frameSize;
    }

    return FASTTAG_OK;
}

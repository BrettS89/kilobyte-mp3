/*
 * track-list.c
 *
 *  Created on: Jul 6, 2026
 *      Author: brettsodie
 *
 * Builds /system/kilobyte.idx: every audio file on the card, one
 * TrackRecord each, sorted by filename, with each record's .index
 * field stamped to its own record position.
 *
 * Pipeline:
 *   1. Create /system if needed.
 *   2. Delete stale temporary files and the previous kilobyte.idx.
 *   3. Scan the root directory with f_readdir_ref().
 *   4. Open each file directly from its captured directory entry with
 *      f_open_ref_read(), avoiding FatFs pathname lookup per file.
 *   5. Read bounded ID3v2.3/v2.4 title, artist, and album tags.
 *   6. Write RAM-sorted temporary chunks.
 *   7. K-way merge chunks until one temporary file remains.
 *   8. Rename the final temporary file to /system/kilobyte.idx.
 *
 * There is deliberately no post-pass to assign indices. Chunk-local
 * indices are stamped by writeFile(), and final global indices are
 * stamped while mergeFiles() writes its output.
 *
 * RAM strategy:
 *   The scan buffer and merge record buffers are never needed at the
 *   same time, so they share one IndexWorkspace union.
 *
 *   Scan phase:
 *       48 TrackRecords
 *
 *   Merge phase:
 *       10 sources x 2 input records = 20 TrackRecords
 *       16 output records            = 16 TrackRecords
 *                                      ----------------
 *                                      36 TrackRecords
 *
 *   Because this is a union, the workspace occupies 48 TrackRecords,
 *   not 48 + 36 TrackRecords.
 *
 * Merge optimizations:
 *   - Inputs are opened by captured directory reference.
 *   - Each source reads two TrackRecords at a time.
 *   - Output is written 16 TrackRecords at a time.
 *   - Output space is preallocated with f_expand() when FF_USE_EXPAND
 *     is enabled and contiguous allocation is available.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "ff.h"
#include "state.h"
#include "systick.h"
#include "sdcard.h"
#include "index-tracks.h"


// Records per RAM-sorted chunk.
// 48 x approximately 330 bytes = approximately 16 KB.
#define FILE_CHUNK_SIZE 48U

// Maximum number of files merged during one merge pass.
#define MAX_MERGE_SOURCES 10U

// Records buffered from each merge input source.
#define MERGE_IN_BUF_RECORDS 2U

// Records staged per merge output write.
// 16 x approximately 330 bytes = approximately 5.3 KB.
#define MERGE_OUT_BUF 16U

/*
 * Verify that the merge buffers fit inside the same TrackRecord
 * workspace used by the scan phase.
 */
#if ((MAX_MERGE_SOURCES * MERGE_IN_BUF_RECORDS) + MERGE_OUT_BUF) > FILE_CHUNK_SIZE
#error Merge buffers exceed the shared TrackRecord workspace
#endif

/*
 * Maximum amount of ID3 tag data traversed per file.
 *
 * This prevents a malformed tag or a very large APIC frame located
 * before the wanted text frames from consuming excessive indexing time.
 *
 * The limit does not include the initial 10-byte ID3 header.
 */
#define MAX_ID3_SCAN_BYTES (256UL * 1024UL)


// Boot-validity sidecar header. Wire-up is still pending.
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t trackCount;
} __attribute__((packed)) IndexHeader;


/*
 * Shared TrackRecord workspace.
 *
 * createTempFiles() uses scan[].
 * mergeFiles() uses merge.input[][] and merge.output[].
 */
typedef union {
    TrackRecord scan[FILE_CHUNK_SIZE];

    struct {
        TrackRecord input[MAX_MERGE_SOURCES][MERGE_IN_BUF_RECORDS];
        TrackRecord output[MERGE_OUT_BUF];
    } merge;
} IndexWorkspace;


/*
 * Information captured while enumerating a merge input.
 *
 * path is still needed for:
 *   - Error messages.
 *   - f_unlink(), which remains pathname-based.
 *
 * ref is used to open the file without a pathname directory search.
 */
typedef struct {
    char path[64];
    FF_DIRENT_REF ref;
} MergeInput;


/*
 * State for one buffered merge source.
 *
 * The record buffer itself lives in indexWorkspace.merge.input.
 */
typedef struct {
    FIL file;
    TrackRecord *records;
    uint32_t position;
    uint32_t count;
    bool exhausted;
} MergeSource;


// Shared record workspace for scan and merge phases.
static IndexWorkspace indexWorkspace;

TrackRecord *getIndexWorkspace(void) {
    return indexWorkspace.scan;
}

// Kept static to avoid a large merge-time stack allocation.
static MergeInput mergeInputs[MAX_MERGE_SOURCES];
static MergeSource mergeSources[MAX_MERGE_SOURCES];

// Final merge output path, read by indexSongList after the merge loop.
static char lastMergeOutput[64];


// ---------------------------------------------------------------------------
// General helpers
// ---------------------------------------------------------------------------

static bool hasSuffix(const char *text, const char *suffix) {
    if (text == NULL || suffix == NULL) {
        return false;
    }

    size_t textLength = strlen(text);
    size_t suffixLength = strlen(suffix);

    if (suffixLength > textLength) {
        return false;
    }

    return strcmp(
        text + textLength - suffixLength,
        suffix
    ) == 0;
}


// ---------------------------------------------------------------------------
// Force-reindex / debug tool: delete every file inside /system.
//
// Call only with all index handles closed, or the unlinks may fail with
// FR_LOCKED. Subdirectories are skipped.
// ---------------------------------------------------------------------------

int wipeSystemDir(void) {
    DIR dir;
    FILINFO fno;
    FRESULT res;
    int deleted = 0;
    char path[80];

    res = f_opendir(&dir, "/system");

    if (res != FR_OK) {
        printf(
            "wipe: opendir /system failed (%d)\r\n",
            res
        );

        return -1;
    }

    for (;;) {
        res = f_readdir(&dir, &fno);

        if (res != FR_OK) {
            printf(
                "wipe: readdir failed (%d)\r\n",
                res
            );

            break;
        }

        if (fno.fname[0] == '\0') {
            break;
        }

        if ((fno.fattrib & AM_DIR) != 0U) {
            continue;
        }

        snprintf(
            path,
            sizeof(path),
            "/system/%s",
            fno.fname
        );

        res = f_unlink(path);

        if (res == FR_OK) {
            deleted++;
        } else {
            printf(
                "wipe: failed to delete %s (%d)\r\n",
                fno.fname,
                res
            );
        }
    }

    f_closedir(&dir);

    printf(
        "wipe: %d files deleted\r\n",
        deleted
    );

    return deleted;
}


// ---------------------------------------------------------------------------
// Chunk sort and write
// ---------------------------------------------------------------------------

void sortTracks(
    TrackRecord tracks[],
    uint32_t count
) {
    for (uint32_t i = 1U; i < count; i++) {
        TrackRecord temp = tracks[i];
        int32_t j = (int32_t)i - 1;

        while (
            j >= 0 &&
            strcmp(
                tracks[j].filename,
                temp.filename
            ) > 0
        ) {
            tracks[j + 1] = tracks[j];
            j--;
        }

        tracks[j + 1] = temp;
    }
}


bool writeFile(
    TrackRecord tracks[],
    uint32_t count,
    uint32_t fileNum
) {
    FIL file;
    UINT written = 0U;

    /*
     * Stamp chunk-local indices.
     *
     * If this is the only chunk, they are already globally correct.
     * Otherwise, the final merge pass stamps them again.
     */
    for (uint32_t i = 0U; i < count; i++) {
        tracks[i].index = i;
    }

    char filename[32];

    snprintf(
        filename,
        sizeof(filename),
        "/system/%03lu-kilobyte.idx.tmp",
        (unsigned long)fileNum
    );

    FRESULT res = f_open(
        &file,
        filename,
        FA_WRITE | FA_CREATE_ALWAYS
    );

    if (res != FR_OK) {
        printf(
            "index: failed to open %s for writing (%d)\r\n",
            filename,
            res
        );

        return false;
    }

    UINT expected =
        (UINT)(count * sizeof(TrackRecord));

    res = f_write(
        &file,
        tracks,
        expected,
        &written
    );

    if (
        res != FR_OK ||
        written != expected
    ) {
        printf(
            "index: write failed for %s, result=%d, "
            "written=%u, expected=%u\r\n",
            filename,
            res,
            written,
            expected
        );

        f_close(&file);
        return false;
    }

    res = f_close(&file);

    if (res != FR_OK) {
        printf(
            "index: close failed for %s (%d)\r\n",
            filename,
            res
        );

        return false;
    }

    return true;
}


// ---------------------------------------------------------------------------
// ID3 tag-reading helpers
// ---------------------------------------------------------------------------

static bool readExact(
    FIL *file,
    void *buffer,
    UINT size
) {
    UINT bytesRead = 0U;

    FRESULT res = f_read(
        file,
        buffer,
        size,
        &bytesRead
    );

    return (
        res == FR_OK &&
        bytesRead == size
    );
}


static bool skipBytesWithin(
    FIL *file,
    FSIZE_t count,
    FSIZE_t boundary
) {
    FSIZE_t position = f_tell(file);

    if (position > boundary) {
        return false;
    }

    /*
     * Use subtraction instead of position + count > boundary so a
     * malformed count cannot overflow the addition.
     */
    if (count > boundary - position) {
        return false;
    }

    return f_lseek(
        file,
        position + count
    ) == FR_OK;
}


static uint32_t readSynchsafe32(
    const uint8_t bytes[4]
) {
    return (
        ((uint32_t)(bytes[0] & 0x7FU) << 21) |
        ((uint32_t)(bytes[1] & 0x7FU) << 14) |
        ((uint32_t)(bytes[2] & 0x7FU) << 7)  |
         (uint32_t)(bytes[3] & 0x7FU)
    );
}


static uint32_t readBigEndian32(
    const uint8_t bytes[4]
) {
    return (
        ((uint32_t)bytes[0] << 24) |
        ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8)  |
         (uint32_t)bytes[3]
    );
}


static bool isValidFrameIdByte(uint8_t value) {
    return (
        (
            value >= (uint8_t)'A' &&
            value <= (uint8_t)'Z'
        ) ||
        (
            value >= (uint8_t)'0' &&
            value <= (uint8_t)'9'
        )
    );
}


static bool isValidFrameId(
    const uint8_t frameHeader[10]
) {
    return (
        isValidFrameIdByte(frameHeader[0]) &&
        isValidFrameIdByte(frameHeader[1]) &&
        isValidFrameIdByte(frameHeader[2]) &&
        isValidFrameIdByte(frameHeader[3])
    );
}


/*
 * Decode an ID3 text-frame payload into a 64-byte output field.
 *
 * frameData includes the one-byte ID3 encoding marker at index zero.
 *
 * Supported:
 *   0x00: ISO-8859-1, copied byte-for-byte.
 *   0x01: UTF-16 with BOM; defaults to little-endian if BOM is absent.
 *   0x02: UTF-16BE without BOM.
 *   0x03: UTF-8, copied byte-for-byte.
 *
 * UTF-16 code points outside the 7-bit ASCII range are represented as
 * '?'. Full Unicode conversion can be added later if the UI supports it.
 */
static void decodeID3TextFrame(
    const uint8_t *frameData,
    uint32_t frameDataSize,
    char *target
) {
    target[0] = '\0';

    if (
        frameData == NULL ||
        frameDataSize <= 1U
    ) {
        return;
    }

    uint8_t encoding = frameData[0];

    if (
        encoding == 0x00U ||
        encoding == 0x03U
    ) {
        uint32_t out = 0U;

        for (
            uint32_t i = 1U;
            i < frameDataSize &&
            out < 63U &&
            frameData[i] != 0U;
            i++
        ) {
            target[out++] =
                (char)frameData[i];
        }

        target[out] = '\0';
        return;
    }

    if (
        encoding != 0x01U &&
        encoding != 0x02U
    ) {
        return;
    }

    bool bigEndian =
        encoding == 0x02U;

    uint32_t position = 1U;

    if (frameDataSize >= 3U) {
        if (
            frameData[1] == 0xFFU &&
            frameData[2] == 0xFEU
        ) {
            bigEndian = false;
            position = 3U;
        } else if (
            frameData[1] == 0xFEU &&
            frameData[2] == 0xFFU
        ) {
            bigEndian = true;
            position = 3U;
        }
    }

    uint32_t out = 0U;

    while (
        position + 1U < frameDataSize &&
        out < 63U
    ) {
        uint16_t codeUnit;

        if (bigEndian) {
            codeUnit =
                ((uint16_t)frameData[position] << 8) |
                 (uint16_t)frameData[position + 1U];
        } else {
            codeUnit =
                ((uint16_t)frameData[position + 1U] << 8) |
                 (uint16_t)frameData[position];
        }

        position += 2U;

        if (codeUnit == 0U) {
            break;
        }

        if (codeUnit <= 0x7FU) {
            target[out++] = (char)codeUnit;
        } else {
            target[out++] = '?';
        }
    }

    target[out] = '\0';
}


/*
 * Return true when the frame has compression, encryption, grouping,
 * per-frame unsynchronization, or another format-specific prefix this
 * parser does not currently decode.
 */
static bool hasUnsupportedFrameFormat(
    uint8_t majorVersion,
    const uint8_t frameHeader[10]
) {
    uint8_t formatFlags =
        frameHeader[9];

    if (majorVersion == 3U) {
        /*
         * ID3v2.3:
         *   0x80 compression
         *   0x40 encryption
         *   0x20 grouping identity
         */
        return (
            formatFlags & 0xE0U
        ) != 0U;
    }

    /*
     * ID3v2.4:
     *   0x40 grouping identity
     *   0x08 compression
     *   0x04 encryption
     *   0x02 frame unsynchronization
     *   0x01 data-length indicator
     */
    return (
        formatFlags & 0x4FU
    ) != 0U;
}


// ---------------------------------------------------------------------------
// ID3 tag reading
//
// Operates on an already-open read-only FIL. The caller owns open/close.
//
// Reads ID3v2.3 and ID3v2.4 TIT2, TPE1, and TALB frames.
// Track-number parsing can later be added by handling the TRCK frame.
// ---------------------------------------------------------------------------

bool readID3Tags(
    FIL *file,
    char *title,
    char *artist,
    char *album
) {
    /*
     * title, artist, and album must each point to at least 64 bytes.
     */
    title[0] = '\0';
    artist[0] = '\0';
    album[0] = '\0';

    uint8_t header[10];

    if (!readExact(
            file,
            header,
            sizeof(header)
        )) {
        return false;
    }

    if (
        header[0] != (uint8_t)'I' ||
        header[1] != (uint8_t)'D' ||
        header[2] != (uint8_t)'3'
    ) {
        return false;
    }

    uint8_t majorVersion =
        header[3];

    uint8_t tagFlags =
        header[5];

    /*
     * ID3v2.2 uses a different frame-header format and is not currently
     * supported by this parser.
     */
    if (
        majorVersion != 3U &&
        majorVersion != 4U
    ) {
        return false;
    }

    /*
     * Tag-level unsynchronization changes the byte stream by inserting
     * bytes this parser does not currently remove.
     */
    if ((tagFlags & 0x80U) != 0U) {
        return false;
    }

    uint32_t tagSize =
        readSynchsafe32(&header[6]);

    FSIZE_t fileSize =
        f_size(file);

    if (fileSize < 10U) {
        return false;
    }

    /*
     * Ensure the ID3 tag declared by the file fits inside the actual
     * file before calculating tagEnd.
     */
    if (
        (FSIZE_t)tagSize >
        fileSize - 10U
    ) {
        return false;
    }

    FSIZE_t tagEnd =
        10U + (FSIZE_t)tagSize;

    uint32_t bytesConsumed = 0U;

    // Skip the optional extended header.
    if ((tagFlags & 0x40U) != 0U) {
        uint8_t extHeader[4];

        if (!readExact(
                file,
                extHeader,
                sizeof(extHeader)
            )) {
            return false;
        }

        uint32_t extSize;

        if (majorVersion == 4U) {
            /*
             * ID3v2.4 extended-header size is synchsafe and includes
             * these four size bytes.
             */
            extSize =
                readSynchsafe32(extHeader);

            if (
                extSize < 4U ||
                extSize > tagSize
            ) {
                return false;
            }

            if (
                extSize >
                MAX_ID3_SCAN_BYTES
            ) {
                return false;
            }

            if (!skipBytesWithin(
                    file,
                    (FSIZE_t)(extSize - 4U),
                    tagEnd
                )) {
                return false;
            }

            bytesConsumed = extSize;
        } else {
            /*
             * ID3v2.3 extended-header size is a normal big-endian
             * integer and excludes these four size bytes.
             */
            extSize =
                readBigEndian32(extHeader);

            if (tagSize < 4U) {
                return false;
            }

            if (
                extSize >
                tagSize - 4U
            ) {
                return false;
            }

            if (
                extSize >
                MAX_ID3_SCAN_BYTES - 4U
            ) {
                return false;
            }

            if (!skipBytesWithin(
                    file,
                    (FSIZE_t)extSize,
                    tagEnd
                )) {
                return false;
            }

            bytesConsumed =
                4U + extSize;
        }
    }

    /*
     * The first byte is the text encoding marker, leaving up to
     * 128 bytes of text payload.
     */
    uint8_t frameData[129];

    while (
        bytesConsumed <= tagSize &&
        tagSize - bytesConsumed >= 10U
    ) {
        if (
            bytesConsumed >
            MAX_ID3_SCAN_BYTES
        ) {
            break;
        }

        if (
            10U >
            (uint32_t)(
                MAX_ID3_SCAN_BYTES -
                bytesConsumed
            )
        ) {
            break;
        }

        uint8_t frameHeader[10];

        if (!readExact(
                file,
                frameHeader,
                sizeof(frameHeader)
            )) {
            return false;
        }

        // Zero marks the beginning of the ID3 padding region.
        if (frameHeader[0] == 0U) {
            break;
        }

        if (!isValidFrameId(frameHeader)) {
            break;
        }

        char frameId[5] = {
            (char)frameHeader[0],
            (char)frameHeader[1],
            (char)frameHeader[2],
            (char)frameHeader[3],
            '\0'
        };

        uint32_t frameSize;

        if (majorVersion == 4U) {
            frameSize =
                readSynchsafe32(
                    &frameHeader[4]
                );
        } else {
            frameSize =
                readBigEndian32(
                    &frameHeader[4]
                );
        }

        bytesConsumed += 10U;

        if (frameSize == 0U) {
            break;
        }

        /*
         * Ensure the frame fits in the declared tag without using an
         * overflow-prone bytesConsumed + frameSize expression.
         */
        if (
            bytesConsumed > tagSize ||
            frameSize >
                tagSize - bytesConsumed
        ) {
            break;
        }

        /*
         * Do not traverse beyond the configured scan limit.
         */
        if (
            bytesConsumed >
                MAX_ID3_SCAN_BYTES ||
            frameSize >
                MAX_ID3_SCAN_BYTES -
                bytesConsumed
        ) {
            break;
        }

        char *target = NULL;

        /*
         * Do not replace a value already found if duplicate text frames
         * appear later in the tag.
         */
        if (
            title[0] == '\0' &&
            strcmp(frameId, "TIT2") == 0
        ) {
            target = title;
        } else if (
            artist[0] == '\0' &&
            strcmp(frameId, "TPE1") == 0
        ) {
            target = artist;
        } else if (
            album[0] == '\0' &&
            strcmp(frameId, "TALB") == 0
        ) {
            target = album;
        }

        if (
            target != NULL &&
            hasUnsupportedFrameFormat(
                majorVersion,
                frameHeader
            )
        ) {
            target = NULL;
        }

        if (target != NULL) {
            uint32_t toRead =
                frameSize;

            if (
                toRead >
                sizeof(frameData)
            ) {
                toRead =
                    sizeof(frameData);
            }

            if (!readExact(
                    file,
                    frameData,
                    (UINT)toRead
                )) {
                return false;
            }

            decodeID3TextFrame(
                frameData,
                toRead,
                target
            );

            if (frameSize > toRead) {
                if (!skipBytesWithin(
                        file,
                        (FSIZE_t)(
                            frameSize -
                            toRead
                        ),
                        tagEnd
                    )) {
                    return false;
                }
            }
        } else {
            /*
             * Unwanted frame such as APIC or COMM, an already populated
             * duplicate frame, or a frame with unsupported format flags.
             */
            if (!skipBytesWithin(
                    file,
                    (FSIZE_t)frameSize,
                    tagEnd
                )) {
                return false;
            }
        }

        bytesConsumed += frameSize;

        if (
            title[0] != '\0' &&
            artist[0] != '\0' &&
            album[0] != '\0'
        ) {
            break;
        }
    }

    /*
     * A valid supported ID3 header was parsed. Individual fields may
     * remain empty and will be handled by the indexing fallback logic.
     */
    return true;
}


// ---------------------------------------------------------------------------
// Card scan to sorted chunk temporary files
// ---------------------------------------------------------------------------

void createTempFiles(void) {
    uint32_t fileNum = 1U;
    uint32_t count = 0U;
    uint32_t totalMatched = 0U;

    TrackRecord *tracks =
        indexWorkspace.scan;

    memset(
        indexWorkspace.scan,
        0,
        sizeof(indexWorkspace.scan)
    );

    DIR dir;
    FILINFO fno;

    FRESULT res =
        f_opendir(&dir, "/");

    if (res != FR_OK) {
        printf(
            "SCAN: failed to open root directory (%d)\r\n",
            res
        );

        return;
    }

    uint32_t scanStart =
        millis();

    for (;;) {
        FF_DIRENT_REF ref;

        res = f_readdir_ref(
            &dir,
            &fno,
            &ref
        );

        if (res != FR_OK) {
            printf(
                "SCAN: readdir error %d\r\n",
                res
            );

            break;
        }

        if (fno.fname[0] == '\0') {
            if (count > 0U) {
                sortTracks(
                    tracks,
                    count
                );

                if (!writeFile(
                        tracks,
                        count,
                        fileNum
                    )) {
                    printf(
                        "SCAN: failed to write final chunk %lu\r\n",
                        (unsigned long)fileNum
                    );
                }
            }

            printf(
                "SCAN: done, %lu tracks indexed in %lums\r\n",
                (unsigned long)totalMatched,
                (unsigned long)(
                    millis() - scanStart
                )
            );

            break;
        }

        if (
            (fno.fattrib & AM_DIR) != 0U
        ) {
            continue;
        }

        if (
            shouldSkipAudioFile(
                fno.fname
            )
        ) {
            continue;
        }

        strncpy(
            tracks[count].filename,
            fno.fname,
            sizeof(
                tracks[count].filename
            ) - 1U
        );

        tracks[count].filename[
            sizeof(
                tracks[count].filename
            ) - 1U
        ] = '\0';

        FIL mp3;
        bool tagsRead = false;

        FRESULT openResult =
            f_open_ref_read(
                &mp3,
                &ref
            );

        if (openResult == FR_OK) {
            tagsRead = readID3Tags(
                &mp3,
                tracks[count].title,
                tracks[count].artist,
                tracks[count].album
            );

            FRESULT closeResult =
                f_close(&mp3);

            if (closeResult != FR_OK) {
                printf(
                    "SCAN: close error %d for %s\r\n",
                    closeResult,
                    fno.fname
                );
            }
        } else {
            printf(
                "SCAN: reference open error %d for %s "
                "cluster=%lu size=%lu\r\n",
                openResult,
                fno.fname,
                (unsigned long)
                    ref.start_cluster,
                (unsigned long)
                    ref.file_size
            );
        }

        if (
            !tagsRead ||
            tracks[count].title[0] == '\0'
        ) {
            strncpy(
                tracks[count].title,
                fno.fname,
                sizeof(
                    tracks[count].title
                ) - 1U
            );

            tracks[count].title[
                sizeof(
                    tracks[count].title
                ) - 1U
            ] = '\0';
        }

        count++;
        totalMatched++;

        if (count == FILE_CHUNK_SIZE) {
            sortTracks(
                tracks,
                FILE_CHUNK_SIZE
            );

            if (!writeFile(
                    tracks,
                    FILE_CHUNK_SIZE,
                    fileNum
                )) {
                printf(
                    "SCAN: failed to write chunk %lu\r\n",
                    (unsigned long)fileNum
                );

                break;
            }

            fileNum++;
            count = 0U;

            memset(
                indexWorkspace.scan,
                0,
                sizeof(indexWorkspace.scan)
            );
        }
    }

    FRESULT closeResult =
        f_closedir(&dir);

    if (closeResult != FR_OK) {
        printf(
            "SCAN: closedir error %d\r\n",
            closeResult
        );
    }
}


// ---------------------------------------------------------------------------
// Buffered k-way merge helpers
// ---------------------------------------------------------------------------

/*
 * Refill one source's two-record buffer.
 *
 * A successful zero-byte read marks the source exhausted.
 * A partial TrackRecord indicates a corrupt temporary index file.
 */
static bool refillMergeSource(
    MergeSource *source
) {
    UINT bytesRead = 0U;

    FRESULT res = f_read(
        &source->file,
        source->records,
        (UINT)(
            MERGE_IN_BUF_RECORDS *
            sizeof(TrackRecord)
        ),
        &bytesRead
    );

    if (res != FR_OK) {
        return false;
    }

    if (
        bytesRead %
        sizeof(TrackRecord) != 0U
    ) {
        return false;
    }

    source->position = 0U;

    source->count =
        (uint32_t)(
            bytesRead /
            sizeof(TrackRecord)
        );

    source->exhausted =
        source->count == 0U;

    return true;
}


static TrackRecord *currentMergeRecord(
    MergeSource *source
) {
    if (
        source->exhausted ||
        source->position >= source->count
    ) {
        return NULL;
    }

    return &source->records[
        source->position
    ];
}


/*
 * Consume the current record. Refill the source when its local buffer
 * has been exhausted.
 */
static bool advanceMergeSource(
    MergeSource *source
) {
    if (source->exhausted) {
        return true;
    }

    source->position++;

    if (
        source->position <
        source->count
    ) {
        return true;
    }

    return refillMergeSource(source);
}


static void closeMergeSources(
    uint32_t count
) {
    for (
        uint32_t i = 0U;
        i < count;
        i++
    ) {
        FRESULT res =
            f_close(
                &mergeSources[i].file
            );

        if (res != FR_OK) {
            printf(
                "merge: input close failed for %s (%d)\r\n",
                mergeInputs[i].path,
                res
            );
        }
    }
}


/*
 * Try to preallocate a contiguous output region.
 *
 * FR_DENIED normally means a sufficiently large contiguous free region
 * was not available. In that case, the merge falls back to normal file
 * growth through f_write().
 *
 * Disk, object, and internal errors are treated as fatal.
 */
static bool preallocateMergeOutput(
    FIL *file,
    FSIZE_t outputSize,
    const char *outputPath
) {
#if defined(FF_USE_EXPAND) && FF_USE_EXPAND

    if (outputSize == 0U) {
        return true;
    }

    FRESULT res = f_expand(
        file,
        outputSize,
        1
    );

    if (res == FR_OK) {
        return true;
    }

    if (res == FR_DENIED) {
        printf(
            "merge: contiguous preallocation unavailable "
            "for %s; using normal allocation\r\n",
            outputPath
        );

        return true;
    }

    printf(
        "merge: output preallocation failed for %s (%d)\r\n",
        outputPath,
        res
    );

    return false;

#else

    /*
     * Prevent unused-parameter warnings when f_expand is disabled.
     */
    (void)file;
    (void)outputSize;
    (void)outputPath;

    return true;

#endif
}


// ---------------------------------------------------------------------------
// K-way merge with buffered input and output
// ---------------------------------------------------------------------------

bool mergeFiles(void) {
    uint32_t inputCount = 0U;
    FSIZE_t outputSize = 0U;

    /*
     * The scan phase is complete before mergeFiles() is called, so the
     * shared TrackRecord workspace can now be reused for merge buffers.
     */
    memset(
        &indexWorkspace.merge,
        0,
        sizeof(indexWorkspace.merge)
    );

    memset(
        mergeInputs,
        0,
        sizeof(mergeInputs)
    );

    memset(
        mergeSources,
        0,
        sizeof(mergeSources)
    );

    DIR dir;
    FILINFO fno;

    FRESULT res =
        f_opendir(
            &dir,
            "/system"
        );

    if (res != FR_OK) {
        printf(
            "merge: failed to open /system (%d)\r\n",
            res
        );

        return false;
    }

    while (
        inputCount <
        MAX_MERGE_SOURCES
    ) {
        FF_DIRENT_REF ref;

        res = f_readdir_ref(
            &dir,
            &fno,
            &ref
        );

        if (res != FR_OK) {
            printf(
                "merge: readdir failed (%d)\r\n",
                res
            );

            break;
        }

        if (fno.fname[0] == '\0') {
            break;
        }

        if (
            (fno.fattrib & AM_DIR) != 0U
        ) {
            continue;
        }

        /*
         * Only temporary index files participate in merging.
         *
         * This prevents grouped.idx, artists.idx, albums.idx,
         * sidecars, or other /system files from being interpreted as
         * TrackRecord streams.
         */
        if (!hasSuffix(
                fno.fname,
                ".tmp"
            )) {
            continue;
        }

        /*
         * Every temporary input should contain only complete
         * TrackRecords.
         */
        if (
            ref.file_size %
            sizeof(TrackRecord) != 0U
        ) {
            printf(
                "merge: invalid temp-file size for %s: %lu\r\n",
                fno.fname,
                (unsigned long)
                    ref.file_size
            );

            res = FR_INT_ERR;
            break;
        }

        snprintf(
            mergeInputs[inputCount].path,
            sizeof(
                mergeInputs[inputCount].path
            ),
            "/system/%s",
            fno.fname
        );

        /*
         * The reference contains stable copied metadata rather than a
         * pointer into FatFs's directory-sector window.
         */
        mergeInputs[inputCount].ref =
            ref;

        /*
         * The merged output size is exactly the sum of the input sizes.
         */
        outputSize += ref.file_size;

        inputCount++;
    }

    FRESULT closeDirResult =
        f_closedir(&dir);

    if (closeDirResult != FR_OK) {
        printf(
            "merge: closedir failed (%d)\r\n",
            closeDirResult
        );

        return false;
    }

    if (res != FR_OK) {
        return false;
    }

    if (inputCount == 1U) {
        /*
         * A single temporary file is already the final output.
         */
        strncpy(
            lastMergeOutput,
            mergeInputs[0].path,
            sizeof(lastMergeOutput) - 1U
        );

        lastMergeOutput[
            sizeof(lastMergeOutput) - 1U
        ] = '\0';

        return false;
    }

    if (inputCount == 0U) {
        return false;
    }

    static uint32_t mergeCounter = 0U;

    char outputPath[64];

    snprintf(
        outputPath,
        sizeof(outputPath),
        "/system/merged-%03lu.tmp",
        (unsigned long)mergeCounter++
    );

    /*
     * Open every input directly from the directory reference captured
     * during the /system enumeration above.
     */
    for (
        uint32_t i = 0U;
        i < inputCount;
        i++
    ) {
        mergeSources[i].records =
            indexWorkspace.merge.input[i];

        mergeSources[i].position = 0U;
        mergeSources[i].count = 0U;
        mergeSources[i].exhausted = false;

        res = f_open_ref_read(
            &mergeSources[i].file,
            &mergeInputs[i].ref
        );

        if (res != FR_OK) {
            printf(
                "merge: reference open failed for %s, "
                "result=%d cluster=%lu size=%lu\r\n",
                mergeInputs[i].path,
                res,
                (unsigned long)
                    mergeInputs[i]
                        .ref.start_cluster,
                (unsigned long)
                    mergeInputs[i]
                        .ref.file_size
            );

            for (
                uint32_t j = 0U;
                j < i;
                j++
            ) {
                f_close(
                    &mergeSources[j].file
                );
            }

            return false;
        }

        if (!refillMergeSource(
                &mergeSources[i]
            )) {
            printf(
                "merge: initial buffered read failed for %s\r\n",
                mergeInputs[i].path
            );

            for (
                uint32_t j = 0U;
                j <= i;
                j++
            ) {
                f_close(
                    &mergeSources[j].file
                );
            }

            return false;
        }
    }

    FIL outFile;

    res = f_open(
        &outFile,
        outputPath,
        FA_WRITE | FA_CREATE_ALWAYS
    );

    if (res != FR_OK) {
        printf(
            "merge: failed to open output %s (%d)\r\n",
            outputPath,
            res
        );

        closeMergeSources(inputCount);
        return false;
    }

    /*
     * Try to allocate the final output extent before writing.
     *
     * When contiguous preallocation is unavailable, ordinary f_write()
     * allocation is used instead.
     */
    if (!preallocateMergeOutput(
            &outFile,
            outputSize,
            outputPath
        )) {
        closeMergeSources(inputCount);
        f_close(&outFile);
        f_unlink(outputPath);

        return false;
    }

    TrackRecord *outBuf =
        indexWorkspace.merge.output;

    uint32_t buffered = 0U;
    uint32_t outIndex = 0U;
    bool mergeSucceeded = true;

    for (;;) {
        int32_t smallestIndex = -1;

        for (
            uint32_t i = 0U;
            i < inputCount;
            i++
        ) {
            TrackRecord *candidate =
                currentMergeRecord(
                    &mergeSources[i]
                );

            if (candidate == NULL) {
                continue;
            }

            if (smallestIndex < 0) {
                smallestIndex =
                    (int32_t)i;

                continue;
            }

            TrackRecord *smallest =
                currentMergeRecord(
                    &mergeSources[
                        (uint32_t)
                            smallestIndex
                    ]
                );

            if (
                smallest == NULL ||
                strcmp(
                    candidate->filename,
                    smallest->filename
                ) < 0
            ) {
                smallestIndex =
                    (int32_t)i;
            }
        }

        if (smallestIndex < 0) {
            break;
        }

        MergeSource *selectedSource =
            &mergeSources[
                (uint32_t)smallestIndex
            ];

        TrackRecord *selectedRecord =
            currentMergeRecord(
                selectedSource
            );

        if (selectedRecord == NULL) {
            printf(
                "merge: selected source has no current record\r\n"
            );

            mergeSucceeded = false;
            break;
        }

        /*
         * Copy the record into the output buffer, then stamp the copy.
         * The source buffer remains unchanged.
         */
        outBuf[buffered] =
            *selectedRecord;

        outBuf[buffered].index =
            outIndex;

        buffered++;
        outIndex++;

        if (buffered == MERGE_OUT_BUF) {
            UINT bytesToWrite =
                (UINT)(
                    buffered *
                    sizeof(TrackRecord)
                );

            UINT written = 0U;

            res = f_write(
                &outFile,
                outBuf,
                bytesToWrite,
                &written
            );

            if (
                res != FR_OK ||
                written != bytesToWrite
            ) {
                printf(
                    "merge: output write failed, result=%d, "
                    "written=%u, expected=%u\r\n",
                    res,
                    written,
                    bytesToWrite
                );

                mergeSucceeded = false;
                break;
            }

            buffered = 0U;
        }

        if (!advanceMergeSource(
                selectedSource
            )) {
            printf(
                "merge: buffered input read failed for %s\r\n",
                mergeInputs[
                    (uint32_t)smallestIndex
                ].path
            );

            mergeSucceeded = false;
            break;
        }
    }

    if (
        mergeSucceeded &&
        buffered > 0U
    ) {
        UINT bytesToWrite =
            (UINT)(
                buffered *
                sizeof(TrackRecord)
            );

        UINT written = 0U;

        res = f_write(
            &outFile,
            outBuf,
            bytesToWrite,
            &written
        );

        if (
            res != FR_OK ||
            written != bytesToWrite
        ) {
            printf(
                "merge: final output write failed, result=%d, "
                "written=%u, expected=%u\r\n",
                res,
                written,
                bytesToWrite
            );

            mergeSucceeded = false;
        }
    }

    /*
     * Verify the number of bytes logically emitted matches the expected
     * sum of the input files.
     */
    if (
        mergeSucceeded &&
        (FSIZE_t)outIndex *
            sizeof(TrackRecord) !=
            outputSize
    ) {
        printf(
            "merge: output size mismatch, "
            "records=%lu expectedBytes=%lu\r\n",
            (unsigned long)outIndex,
            (unsigned long)outputSize
        );

        mergeSucceeded = false;
    }

    closeMergeSources(inputCount);

    res = f_close(&outFile);

    if (res != FR_OK) {
        printf(
            "merge: output close failed for %s (%d)\r\n",
            outputPath,
            res
        );

        mergeSucceeded = false;
    }

    if (!mergeSucceeded) {
        /*
         * Preserve the input files for diagnosis or retry, and remove
         * the incomplete merge output.
         */
        FRESULT unlinkResult =
            f_unlink(outputPath);

        if (
            unlinkResult != FR_OK &&
            unlinkResult != FR_NO_FILE
        ) {
            printf(
                "merge: failed to remove incomplete output %s (%d)\r\n",
                outputPath,
                unlinkResult
            );
        }

        return false;
    }

    /*
     * f_unlink() is still pathname-based because stock FatFs does not
     * expose unlink-by-directory-entry.
     */
    for (
        uint32_t i = 0U;
        i < inputCount;
        i++
    ) {
        res = f_unlink(
            mergeInputs[i].path
        );

        if (res != FR_OK) {
            printf(
                "merge: failed to delete input %s (%d)\r\n",
                mergeInputs[i].path,
                res
            );
        }
    }

    strncpy(
        lastMergeOutput,
        outputPath,
        sizeof(lastMergeOutput) - 1U
    );

    lastMergeOutput[
        sizeof(lastMergeOutput) - 1U
    ] = '\0';

    return true;
}


// ---------------------------------------------------------------------------
// Cleanup and debug utilities
// ---------------------------------------------------------------------------

/*
 * Delete stale merge and chunk temporary files so a crashed prior run
 * cannot contaminate this one.
 */
static void deleteTempFiles(void) {
    DIR dir;
    FILINFO fno;
    char path[80];

    FRESULT res =
        f_opendir(
            &dir,
            "/system"
        );

    if (res != FR_OK) {
        return;
    }

    for (;;) {
        res = f_readdir(
            &dir,
            &fno
        );

        if (res != FR_OK) {
            printf(
                "cleanup: readdir failed (%d)\r\n",
                res
            );

            break;
        }

        if (fno.fname[0] == '\0') {
            break;
        }

        if (
            (fno.fattrib & AM_DIR) != 0U
        ) {
            continue;
        }

        if (!hasSuffix(
                fno.fname,
                ".tmp"
            )) {
            continue;
        }

        snprintf(
            path,
            sizeof(path),
            "/system/%s",
            fno.fname
        );

        res = f_unlink(path);

        if (res != FR_OK) {
            printf(
                "cleanup: failed to delete %s (%d)\r\n",
                path,
                res
            );
        }
    }

    f_closedir(&dir);
}


// Debug only. Not called in the indexing hot path.
void printSystemDir(void) {
    DIR dir;
    FILINFO fno;
    FRESULT res;
    uint32_t count = 0U;
    uint64_t totalBytes = 0U;

    res = f_opendir(
        &dir,
        "/system"
    );

    if (res != FR_OK) {
        printf(
            "/system: opendir failed (%d)\r\n",
            res
        );

        return;
    }

    printf(
        "---- /system ----\r\n"
    );

    for (;;) {
        res = f_readdir(
            &dir,
            &fno
        );

        if (res != FR_OK) {
            printf(
                "readdir error (%d)\r\n",
                res
            );

            break;
        }

        if (fno.fname[0] == '\0') {
            break;
        }

        if (
            (fno.fattrib & AM_DIR) != 0U
        ) {
            printf(
                "  [DIR]  %s\r\n",
                fno.fname
            );
        } else {
            printf(
                "  %8lu  %s\r\n",
                (unsigned long)
                    fno.fsize,
                fno.fname
            );

            totalBytes +=
                fno.fsize;
        }

        count++;
    }

    printf(
        "---- %lu entries, %llu bytes in files ----\r\n",
        (unsigned long)count,
        (unsigned long long)totalBytes
    );

    f_closedir(&dir);
}


// ---------------------------------------------------------------------------
// Indexing pipeline
// ---------------------------------------------------------------------------

void indexSongList(void) {
    FRESULT res =
        f_mkdir("/system");

    if (
        res != FR_OK &&
        res != FR_EXIST
    ) {
        printf(
            "index: cannot create /system (%d)\r\n",
            res
        );

        return;
    }

    /*
     * Clear the previous filename index and all stale temporary files.
     * Other finished index files are left untouched until their own
     * rebuild stages replace them.
     */
    res = f_unlink(
        "/system/kilobyte.idx"
    );

    if (
        res != FR_OK &&
        res != FR_NO_FILE
    ) {
        printf(
            "index: cannot delete previous kilobyte.idx (%d)\r\n",
            res
        );

        return;
    }

    deleteTempFiles();

    lastMergeOutput[0] = '\0';

    createTempFiles();

    while (mergeFiles()) {
        // Continue until only one temporary file remains.
    }

    if (lastMergeOutput[0] == '\0') {
        printf(
            "index: no output produced "
            "(empty card or indexing error)\r\n"
        );

        return;
    }

    res = f_rename(
        lastMergeOutput,
        "/system/kilobyte.idx"
    );

    if (res != FR_OK) {
        printf(
            "index: rename failed (%d)\r\n",
            res
        );
    }
}

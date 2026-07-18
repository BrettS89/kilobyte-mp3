/*
 * grouped-list.c
 *
 *  Created on: Jul 16, 2026
 *      Author: brettsodie
 *
 * Builds grouped.idx from the all-songs index.
 *
 * grouped.idx contains the same TrackRecords as kilobyte.idx, sorted by:
 *
 *   1. artist, case-insensitive
 *   2. album, case-insensitive
 *   3. trackNumber, when that field is added
 *   4. title, case-insensitive
 *
 * Empty artist and album strings sort before non-empty strings. This
 * creates the Unknown Artist and Unknown Album runs at the beginning of
 * their respective groups.
 *
 * External-sort design:
 *
 *   Phase 1:
 *     Read 48 TrackRecords at a time, sort them in RAM, and write the
 *     sorted runs sequentially to /system/GRA.TMP.
 *
 *   Phase 2:
 *     Perform iterative 10-way merge passes, ping-ponging between:
 *
 *       /system/GRA.TMP
 *       /system/GRB.TMP
 *
 *     Each input source buffers four TrackRecords. Output is buffered
 *     eight TrackRecords at a time.
 *
 * RAM strategy:
 *
 *   This file reuses the same 48-record workspace allocated by
 *   track-list.c through getIndexWorkspace().
 *
 *   Initial-run phase:
 *       48 records used as the qsort buffer.
 *
 *   Merge phase:
 *       10 sources x 4 records = 40 records
 *       8 output records       =  8 records
 *                                ----------
 *                                48 records
 *
 *   No additional TrackRecord array is permanently allocated here.
 *
 * Fast-seek strategy:
 *
 *   Each of the ten source FIL objects receives a small FatFs
 *   cluster-link map table. This avoids repeatedly traversing the FAT
 *   chain when each source handle seeks to a different sorted run.
 *
 *   If a temporary file is too fragmented for the small table, that
 *   source falls back to normal FatFs seeking.
 */

#include "ff.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "index-tracks.h"
#include "state.h"


// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Number of records sorted in RAM per initial run.
#define GROUP_RUN_RECORDS 48U

// Number of sorted runs merged simultaneously.
#define GROUP_MERGE_SOURCES 10U

// Records buffered from each merge source.
#define GROUP_IN_BUF_RECORDS 4U

// Records staged per destination write.
#define GROUP_OUT_BUF_RECORDS 8U

// Size of the shared workspace exported by track-list.c.
#define GROUP_WORKSPACE_RECORDS 48U

/*
 * DWORD entries reserved for each source file's FatFs cluster-link map.
 *
 * A contiguous file needs only a small table. Eight entries provide
 * some room for limited fragmentation while consuming:
 *
 *     10 sources x 8 DWORDs x 4 bytes = 320 bytes
 */
#define GROUP_CLMT_ITEMS 8U

// Short 8.3 names avoid unnecessary LFN directory entries.
#define GROUP_TMP_A "/system/GRA.TMP"
#define GROUP_TMP_B "/system/GRB.TMP"


#define GROUP_INPUT_WORKSPACE_RECORDS \
    (GROUP_MERGE_SOURCES * GROUP_IN_BUF_RECORDS)

#define GROUP_MERGE_WORKSPACE_RECORDS \
    (GROUP_INPUT_WORKSPACE_RECORDS + GROUP_OUT_BUF_RECORDS)


#if GROUP_RUN_RECORDS > GROUP_WORKSPACE_RECORDS
#error GROUP_RUN_RECORDS exceeds the shared index workspace
#endif

#if GROUP_MERGE_WORKSPACE_RECORDS > GROUP_WORKSPACE_RECORDS
#error Grouped merge buffers exceed the shared index workspace
#endif

#if defined(FF_FS_LOCK) && \
    FF_FS_LOCK > 0 && \
    FF_FS_LOCK < (GROUP_MERGE_SOURCES + 1U)
#error FF_FS_LOCK is too small for the grouped 10-way merge
#endif


/*
 * Implemented in track-list.c.
 *
 * Index-building stages must run sequentially because they share this
 * TrackRecord workspace.
 */
extern TrackRecord *getIndexWorkspace(void);


// ---------------------------------------------------------------------------
// Merge-source state
// ---------------------------------------------------------------------------

typedef struct {
    FIL file;

#if FF_USE_FASTSEEK
    /*
     * Each independently opened FIL object gets its own cluster-link
     * map and its own fast-seek state.
     */
    DWORD clmt[GROUP_CLMT_ITEMS];
    bool fastSeekEnabled;
#endif

    /*
     * Points into the shared 48-record workspace.
     */
    TrackRecord *records;

    // Current record inside records[].
    uint32_t position;

    // Number of valid records currently buffered.
    uint32_t bufferedCount;

    // Records in this sorted run not yet loaded into the buffer.
    uint32_t remainingToRead;

    // True while this source has a current record.
    bool active;
} GroupRunSource;


/*
 * Ten FIL objects can consume several kilobytes with FF_FS_TINY == 0,
 * so keep these objects out of the stack.
 */
static GroupRunSource groupSources[GROUP_MERGE_SOURCES];


// ---------------------------------------------------------------------------
// General filesystem helpers
// ---------------------------------------------------------------------------

static FRESULT unlinkIfExists(const char *path) {
    FRESULT res = f_unlink(path);

    if (res == FR_NO_FILE) {
        return FR_OK;
    }

    return res;
}


static FRESULT readExact(
    FIL *file,
    void *buffer,
    UINT bytesToRead
) {
    UINT bytesRead = 0U;

    FRESULT res = f_read(
        file,
        buffer,
        bytesToRead,
        &bytesRead
    );

    if (res != FR_OK) {
        return res;
    }

    if (bytesRead != bytesToRead) {
        return FR_INT_ERR;
    }

    return FR_OK;
}


static FRESULT writeExact(
    FIL *file,
    const void *buffer,
    UINT bytesToWrite
) {
    UINT bytesWritten = 0U;

    FRESULT res = f_write(
        file,
        buffer,
        bytesToWrite,
        &bytesWritten
    );

    if (res != FR_OK) {
        return res;
    }

    if (bytesWritten != bytesToWrite) {
        return FR_DISK_ERR;
    }

    return FR_OK;
}


/*
 * Try to allocate a contiguous data area for an output file.
 *
 * FR_DENIED means FatFs could not find a large enough contiguous area.
 * That is not fatal; ordinary f_write() allocation remains usable.
 */
static FRESULT preallocateOutput(
    FIL *file,
    FSIZE_t outputSize
) {
    /*
     * Disabled for grouped-index files.
     *
     * These files are small enough that searching the FAT for a
     * contiguous region may cost more than ordinary incremental
     * allocation.
     */
    (void)file;
    (void)outputSize;

    return FR_OK;
}

// ---------------------------------------------------------------------------
// Comparator
// ---------------------------------------------------------------------------

static uint8_t asciiLower(uint8_t value) {
    if (
        value >= (uint8_t)'A' &&
        value <= (uint8_t)'Z'
    ) {
        return (uint8_t)(
            value +
            ((uint8_t)'a' - (uint8_t)'A')
        );
    }

    return value;
}


static int cmpNoCase(
    const char *leftText,
    const char *rightText
) {
    const uint8_t *left =
        (const uint8_t *)leftText;

    const uint8_t *right =
        (const uint8_t *)rightText;

    while (
        *left != 0U &&
        *right != 0U
    ) {
        uint8_t leftLower =
            asciiLower(*left);

        uint8_t rightLower =
            asciiLower(*right);

        if (leftLower != rightLower) {
            return (
                (int)leftLower -
                (int)rightLower
            );
        }

        left++;
        right++;
    }

    /*
     * The empty or shorter string sorts first. This naturally places
     * untagged artist and album values first.
     */
    return (
        (int)*left -
        (int)*right
    );
}


/*
 * qsort-compatible grouped comparator.
 *
 * Track-number sorting can be enabled later by uncommenting the marked
 * block once TrackRecord contains a populated trackNumber field.
 */
static int groupedCompare(
    const void *leftPointer,
    const void *rightPointer
) {
    const TrackRecord *left =
        (const TrackRecord *)leftPointer;

    const TrackRecord *right =
        (const TrackRecord *)rightPointer;

    int comparison = cmpNoCase(
        left->artist,
        right->artist
    );

    if (comparison != 0) {
        return comparison;
    }

    comparison = cmpNoCase(
        left->album,
        right->album
    );

    if (comparison != 0) {
        return comparison;
    }

    /*
     * Enable this after trackNumber is added and populated:
     *
     * if (left->trackNumber != right->trackNumber) {
     *     return left->trackNumber < right->trackNumber ? -1 : 1;
     * }
     */

    comparison = cmpNoCase(
        left->title,
        right->title
    );

    if (comparison != 0) {
        return comparison;
    }

    /*
     * Deterministic final tie-breaker. This does not change grouping,
     * but prevents otherwise equal records from changing order between
     * qsort and merge operations.
     */
    return cmpNoCase(
        left->filename,
        right->filename
    );
}


// ---------------------------------------------------------------------------
// Phase 1: create sorted initial runs
// ---------------------------------------------------------------------------

static FRESULT makeInitialRuns(
    const char *sourcePath,
    const char *destinationPath,
    uint32_t *outTotalRecords
) {
    if (
        sourcePath == NULL ||
        destinationPath == NULL ||
        outTotalRecords == NULL
    ) {
        return FR_INVALID_PARAMETER;
    }

    *outTotalRecords = 0U;

    TrackRecord *workspace =
        getIndexWorkspace();

    if (workspace == NULL) {
        return FR_NOT_ENOUGH_CORE;
    }

    FIL source;
    FIL destination;

    bool sourceOpen = false;
    bool destinationOpen = false;

    FRESULT result = f_open(
        &source,
        sourcePath,
        FA_READ
    );

    if (result != FR_OK) {
        return result;
    }

    sourceOpen = true;

    FSIZE_t sourceSize =
        f_size(&source);

    if (
        sourceSize %
        sizeof(TrackRecord) != 0U
    ) {
        result = FR_INT_ERR;
        goto cleanup;
    }

    uint32_t totalRecords =
        (uint32_t)(
            sourceSize /
            sizeof(TrackRecord)
        );

    result = f_open(
        &destination,
        destinationPath,
        FA_WRITE | FA_CREATE_ALWAYS
    );

    if (result != FR_OK) {
        goto cleanup;
    }

    destinationOpen = true;

    result = preallocateOutput(
        &destination,
        sourceSize
    );

    if (result != FR_OK) {
        goto cleanup;
    }

    uint32_t recordsRemaining =
        totalRecords;

    while (recordsRemaining > 0U) {
        uint32_t recordsThisRun =
            GROUP_RUN_RECORDS;

        if (
            recordsThisRun >
            recordsRemaining
        ) {
            recordsThisRun =
                recordsRemaining;
        }

        UINT bytesThisRun =
            (UINT)(
                recordsThisRun *
                sizeof(TrackRecord)
            );

        result = readExact(
            &source,
            workspace,
            bytesThisRun
        );

        if (result != FR_OK) {
            goto cleanup;
        }

        qsort(
            workspace,
            recordsThisRun,
            sizeof(TrackRecord),
            groupedCompare
        );

        result = writeExact(
            &destination,
            workspace,
            bytesThisRun
        );

        if (result != FR_OK) {
            goto cleanup;
        }

        recordsRemaining -=
            recordsThisRun;
    }

    if (
        f_tell(&destination) !=
        sourceSize
    ) {
        result = FR_INT_ERR;
        goto cleanup;
    }

    *outTotalRecords =
        totalRecords;

cleanup:
    if (sourceOpen) {
        FRESULT closeResult =
            f_close(&source);

        if (
            result == FR_OK &&
            closeResult != FR_OK
        ) {
            result = closeResult;
        }
    }

    if (destinationOpen) {
        FRESULT closeResult =
            f_close(&destination);

        if (
            result == FR_OK &&
            closeResult != FR_OK
        ) {
            result = closeResult;
        }
    }

    if (result != FR_OK) {
        f_unlink(destinationPath);
        *outTotalRecords = 0U;
    }

    return result;
}


// ---------------------------------------------------------------------------
// Buffered run-source helpers
// ---------------------------------------------------------------------------

static void disableGroupSource(
    GroupRunSource *source
) {
    source->position = 0U;
    source->bufferedCount = 0U;
    source->remainingToRead = 0U;
    source->active = false;
}


/*
 * Load up to GROUP_IN_BUF_RECORDS from the source's current sorted run.
 */
static FRESULT refillGroupSource(
    GroupRunSource *source
) {
    if (source->remainingToRead == 0U) {
        disableGroupSource(source);
        return FR_OK;
    }

    uint32_t recordsToRead =
        GROUP_IN_BUF_RECORDS;

    if (
        recordsToRead >
        source->remainingToRead
    ) {
        recordsToRead =
            source->remainingToRead;
    }

    UINT bytesToRead =
        (UINT)(
            recordsToRead *
            sizeof(TrackRecord)
        );

    FRESULT res = readExact(
        &source->file,
        source->records,
        bytesToRead
    );

    if (res != FR_OK) {
        disableGroupSource(source);
        return res;
    }

    source->position = 0U;
    source->bufferedCount =
        recordsToRead;

    source->remainingToRead -=
        recordsToRead;

    source->active = true;

    return FR_OK;
}


/*
 * Position one source handle at one sorted run.
 */
static FRESULT initializeGroupSource(
    GroupRunSource *source,
    uint32_t startRecord,
    uint32_t runLength,
    uint32_t totalRecords
) {
    disableGroupSource(source);

    if (startRecord >= totalRecords) {
        return FR_OK;
    }

    uint32_t availableRecords =
        totalRecords - startRecord;

    uint32_t recordsInRun =
        runLength;

    if (recordsInRun > availableRecords) {
        recordsInRun =
            availableRecords;
    }

    FSIZE_t byteOffset =
        (FSIZE_t)startRecord *
        sizeof(TrackRecord);

    FRESULT res = f_lseek(
        &source->file,
        byteOffset
    );

    if (res != FR_OK) {
        return res;
    }

    /*
     * In read mode, FatFs can clip a seek at EOF. Verify that the
     * requested position was actually reached.
     */
    if (
        f_tell(&source->file) !=
        byteOffset
    ) {
        return FR_INT_ERR;
    }

    source->remainingToRead =
        recordsInRun;

    return refillGroupSource(source);
}


static TrackRecord *currentGroupRecord(
    GroupRunSource *source
) {
    if (
        !source->active ||
        source->position >=
            source->bufferedCount
    ) {
        return NULL;
    }

    return &source->records[
        source->position
    ];
}


/*
 * Consume the current record and refill when the local buffer empties.
 */
static FRESULT advanceGroupSource(
    GroupRunSource *source
) {
    if (!source->active) {
        return FR_OK;
    }

    source->position++;

    if (
        source->position <
        source->bufferedCount
    ) {
        return FR_OK;
    }

    return refillGroupSource(source);
}


// ---------------------------------------------------------------------------
// Merge-output helpers
// ---------------------------------------------------------------------------

static FRESULT flushGroupedOutput(
    FIL *destination,
    TrackRecord *outputBuffer,
    uint32_t *bufferedCount
) {
    if (*bufferedCount == 0U) {
        return FR_OK;
    }

    UINT bytesToWrite =
        (UINT)(
            *bufferedCount *
            sizeof(TrackRecord)
        );

    FRESULT res = writeExact(
        destination,
        outputBuffer,
        bytesToWrite
    );

    if (res == FR_OK) {
        *bufferedCount = 0U;
    }

    return res;
}


static FRESULT closeGroupSourceFiles(
    uint32_t openedCount
) {
    FRESULT firstError =
        FR_OK;

    for (
        uint32_t i = 0U;
        i < openedCount;
        i++
    ) {
        FRESULT closeResult =
            f_close(
                &groupSources[i].file
            );

        if (
            firstError == FR_OK &&
            closeResult != FR_OK
        ) {
            firstError =
                closeResult;
        }
    }

    return firstError;
}


// ---------------------------------------------------------------------------
// Fast-seek helpers
// ---------------------------------------------------------------------------

/*
 * Build a FatFs cluster-link map for one already-open source handle.
 *
 * Fast seek prevents repeated FAT-chain traversal when the merge moves
 * this handle to the beginning of a different sorted run.
 *
 * If GROUP_CLMT_ITEMS is too small for the file's fragmentation, the
 * handle falls back to normal FatFs seeking.
 */
static FRESULT enableGroupFastSeek(
    GroupRunSource *source,
    const char *sourcePath,
    uint32_t sourceIndex
) {
#if FF_USE_FASTSEEK

    source->fastSeekEnabled =
        false;

    source->clmt[0] =
        GROUP_CLMT_ITEMS;

    source->file.cltbl =
        source->clmt;

    FRESULT res = f_lseek(
        &source->file,
        CREATE_LINKMAP
    );

    if (res == FR_OK) {
        source->fastSeekEnabled =
            true;

        return FR_OK;
    }

    if (res == FR_NOT_ENOUGH_CORE) {
        /*
         * FatFs places the required number of DWORD entries in the
         * first CLMT element when the supplied table is too small.
         */
        DWORD requiredItems =
            source->clmt[0];

        source->file.cltbl =
            NULL;

        /*
         * All ten handles point to the same file and therefore normally
         * need the same table size. Only print the first fallback.
         */
        if (sourceIndex == 0U) {
            printf(
                "group: fast seek disabled for %s; "
                "CLMT needs %lu items, has %lu\r\n",
                sourcePath,
                (unsigned long)requiredItems,
                (unsigned long)GROUP_CLMT_ITEMS
            );
        }

        return FR_OK;
    }

    source->file.cltbl =
        NULL;

    return res;

#else

    (void)source;
    (void)sourcePath;
    (void)sourceIndex;

    return FR_OK;

#endif
}


// ---------------------------------------------------------------------------
// Phase 2: one 10-way merge pass
// ---------------------------------------------------------------------------

static FRESULT mergePass(
    const char *sourcePath,
    const char *destinationPath,
    uint32_t totalRecords,
    uint32_t runLength
) {
    if (
        sourcePath == NULL ||
        destinationPath == NULL ||
        runLength == 0U
    ) {
        return FR_INVALID_PARAMETER;
    }

    TrackRecord *workspace =
        getIndexWorkspace();

    if (workspace == NULL) {
        return FR_NOT_ENOUGH_CORE;
    }

    memset(
        groupSources,
        0,
        sizeof(groupSources)
    );

    /*
     * Shared workspace layout:
     *
     *   Records 0 through 39:
     *       ten four-record source buffers
     *
     *   Records 40 through 47:
     *       one eight-record output buffer
     */
    for (
        uint32_t i = 0U;
        i < GROUP_MERGE_SOURCES;
        i++
    ) {
        groupSources[i].records =
            &workspace[
                i *
                GROUP_IN_BUF_RECORDS
            ];
    }

    TrackRecord *outputBuffer =
        &workspace[
            GROUP_INPUT_WORKSPACE_RECORDS
        ];

    uint32_t openedSources = 0U;
    bool destinationOpen = false;

    FRESULT result = FR_OK;

    /*
     * Open ten independent read handles. Each handle needs its own file
     * pointer, sector buffer, and fast-seek link map.
     */
    for (
        uint32_t i = 0U;
        i < GROUP_MERGE_SOURCES;
        i++
    ) {
        result = f_open(
            &groupSources[i].file,
            sourcePath,
            FA_READ
        );

        if (result != FR_OK) {
            goto cleanup;
        }

        openedSources++;

        /*
         * Build the cluster-link map immediately after opening the
         * handle, before normal seeks or reads.
         */
        result = enableGroupFastSeek(
            &groupSources[i],
            sourcePath,
            i
        );

        if (result != FR_OK) {
            goto cleanup;
        }
    }

    FIL destination;

    result = f_open(
        &destination,
        destinationPath,
        FA_WRITE | FA_CREATE_ALWAYS
    );

    if (result != FR_OK) {
        goto cleanup;
    }

    destinationOpen = true;

    FSIZE_t expectedOutputSize =
        (FSIZE_t)totalRecords *
        sizeof(TrackRecord);

    result = preallocateOutput(
        &destination,
        expectedOutputSize
    );

    if (result != FR_OK) {
        goto cleanup;
    }

    uint32_t outputCount = 0U;
    uint32_t emittedRecords = 0U;

    uint64_t groupSpan =
        (uint64_t)runLength *
        GROUP_MERGE_SOURCES;

    /*
     * Each outer iteration merges up to ten adjacent sorted runs.
     */
    for (
        uint64_t groupStart = 0U;
        groupStart < totalRecords;
        groupStart += groupSpan
    ) {
        /*
         * Position each source handle at one run in this group.
         */
        for (
            uint32_t sourceIndex = 0U;
            sourceIndex <
                GROUP_MERGE_SOURCES;
            sourceIndex++
        ) {
            uint64_t runStart =
                groupStart +
                (
                    (uint64_t)sourceIndex *
                    runLength
                );

            uint32_t startRecord;

            if (runStart >= totalRecords) {
                startRecord =
                    totalRecords;
            } else {
                startRecord =
                    (uint32_t)runStart;
            }

            result = initializeGroupSource(
                &groupSources[sourceIndex],
                startRecord,
                runLength,
                totalRecords
            );

            if (result != FR_OK) {
                goto cleanup;
            }
        }

        /*
         * Merge this group of up to ten active sorted runs.
         */
        for (;;) {
            int32_t smallestSource =
                -1;

            for (
                uint32_t sourceIndex = 0U;
                sourceIndex <
                    GROUP_MERGE_SOURCES;
                sourceIndex++
            ) {
                TrackRecord *candidate =
                    currentGroupRecord(
                        &groupSources[
                            sourceIndex
                        ]
                    );

                if (candidate == NULL) {
                    continue;
                }

                if (smallestSource < 0) {
                    smallestSource =
                        (int32_t)sourceIndex;

                    continue;
                }

                TrackRecord *smallest =
                    currentGroupRecord(
                        &groupSources[
                            (uint32_t)
                                smallestSource
                        ]
                    );

                /*
                 * Strictly less-than means the earlier input run wins
                 * ties, preserving stable merge order.
                 */
                if (
                    smallest == NULL ||
                    groupedCompare(
                        candidate,
                        smallest
                    ) < 0
                ) {
                    smallestSource =
                        (int32_t)sourceIndex;
                }
            }

            if (smallestSource < 0) {
                break;
            }

            GroupRunSource *selectedSource =
                &groupSources[
                    (uint32_t)smallestSource
                ];

            TrackRecord *selectedRecord =
                currentGroupRecord(
                    selectedSource
                );

            if (selectedRecord == NULL) {
                result = FR_INT_ERR;
                goto cleanup;
            }

            outputBuffer[outputCount++] =
                *selectedRecord;

            emittedRecords++;

            if (
                outputCount ==
                GROUP_OUT_BUF_RECORDS
            ) {
                result = flushGroupedOutput(
                    &destination,
                    outputBuffer,
                    &outputCount
                );

                if (result != FR_OK) {
                    goto cleanup;
                }
            }

            result = advanceGroupSource(
                selectedSource
            );

            if (result != FR_OK) {
                goto cleanup;
            }
        }
    }

    result = flushGroupedOutput(
        &destination,
        outputBuffer,
        &outputCount
    );

    if (result != FR_OK) {
        goto cleanup;
    }

    if (emittedRecords != totalRecords) {
        result = FR_INT_ERR;
        goto cleanup;
    }

    if (
        f_tell(&destination) !=
        expectedOutputSize
    ) {
        result = FR_INT_ERR;
        goto cleanup;
    }

cleanup:
    {
        FRESULT closeResult =
            closeGroupSourceFiles(
                openedSources
            );

        if (
            result == FR_OK &&
            closeResult != FR_OK
        ) {
            result =
                closeResult;
        }
    }

    if (destinationOpen) {
        FRESULT closeResult =
            f_close(&destination);

        if (
            result == FR_OK &&
            closeResult != FR_OK
        ) {
            result =
                closeResult;
        }
    }

    if (result != FR_OK) {
        f_unlink(destinationPath);
    }

    return result;
}


// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

FRESULT buildGroupedIndex(
    const char *songIndexPath,
    const char *groupedPath
) {
    if (
        songIndexPath == NULL ||
        groupedPath == NULL
    ) {
        return FR_INVALID_PARAMETER;
    }

    /*
     * Remove stale files from an interrupted previous grouped build.
     */
    FRESULT res =
        unlinkIfExists(GROUP_TMP_A);

    if (res != FR_OK) {
        return res;
    }

    res = unlinkIfExists(
        GROUP_TMP_B
    );

    if (res != FR_OK) {
        return res;
    }

    uint32_t totalRecords =
        0U;

    res = makeInitialRuns(
        songIndexPath,
        GROUP_TMP_A,
        &totalRecords
    );

    if (res != FR_OK) {
        return res;
    }

    /*
     * An empty source still produces a valid empty grouped index.
     */
    if (totalRecords == 0U) {
        res = unlinkIfExists(
            groupedPath
        );

        if (res != FR_OK) {
            return res;
        }

        return f_rename(
            GROUP_TMP_A,
            groupedPath
        );
    }

    const char *sourcePath =
        GROUP_TMP_A;

    const char *destinationPath =
        GROUP_TMP_B;

    uint32_t runLength =
        GROUP_RUN_RECORDS;

    while (runLength < totalRecords) {
        res = mergePass(
            sourcePath,
            destinationPath,
            totalRecords,
            runLength
        );

        if (res != FR_OK) {
            return res;
        }

        const char *swap =
            sourcePath;

        sourcePath =
            destinationPath;

        destinationPath =
            swap;

        /*
         * Each ten-way merge pass increases the sorted-run length by a
         * factor of ten.
         *
         * The division check prevents integer overflow.
         */
        if (
            runLength >
            totalRecords /
                GROUP_MERGE_SOURCES
        ) {
            runLength =
                totalRecords;
        } else {
            runLength *=
                GROUP_MERGE_SOURCES;
        }
    }

    /*
     * sourcePath now contains one fully sorted run.
     */
    res = unlinkIfExists(
        groupedPath
    );

    if (res != FR_OK) {
        return res;
    }

    res = f_rename(
        sourcePath,
        groupedPath
    );

    if (res != FR_OK) {
        return res;
    }

    /*
     * Remove whichever ping-pong temporary file was not renamed.
     */
    const char *unusedTemp =
        strcmp(
            sourcePath,
            GROUP_TMP_A
        ) == 0
            ? GROUP_TMP_B
            : GROUP_TMP_A;

    res = unlinkIfExists(
        unusedTemp
    );

    if (res != FR_OK) {
        return res;
    }

    return FR_OK;
}

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
#include "state.h"
#include "audio.h"
#include "ff.h"
#include "sdcard.h"
#include "systick.h"


#define STAGE_SIZE 512U
#define AUDIO_CHUNK_SIZE 32U
#define AUDIO_MAX_CHUNKS_PER_PASS 64U
#define AUDIO_PRIME_MAX_BYTES 2048U

#define MP3_FRAME_SEARCH_BYTES 4096U


static uint8_t stageBuf[STAGE_SIZE];

static UINT stageLen = 0U;
static UINT stagePos = 0U;


typedef struct {
    FIL file;
    bool isOpen;
    bool isPlaying;
    char currentFilename[256];
} AudioStream;


typedef struct {
    uint32_t bitrateKbps;
    uint32_t sampleRate;
    uint32_t samplesPerFrame;
    uint32_t frameSize;

    bool isMpeg1;
    bool isMono;
} Mp3FrameInfo;


static AudioStream audioStream = {0};


static volatile bool songChangeRequested = false;

static char pendingFilename[256];

static DWORD pendingStartCluster = 0U;
static FSIZE_t pendingFileSize = 0U;


static uint32_t pendingDuration = 0U;
static volatile bool durationReady = false;


// ---------------------------------------------------------------------------
// VS1053 helpers
// ---------------------------------------------------------------------------

static bool vs1053WaitForDREQTimeout(void) {
    for (
        volatile uint32_t i = 0U;
        i < 2000000U;
        i++
    ) {
        if (
            (
                GPIOA->IDR &
                (1U << 11)
            ) != 0U
        ) {
            return true;
        }
    }

    printf(
        "VS1053 DREQ timeout!\r\n"
    );

    return false;
}


static void vs1053SendChunk32(
    const uint8_t *data,
    uint16_t length
) {
    /*
     * XDCS low.
     */
    GPIOB->BSRR =
        (1U << (2U + 16U));

    for (
        uint16_t i = 0U;
        i < length;
        i++
    ) {
        spi2Transfer(data[i]);
    }

    /*
     * XDCS high.
     */
    GPIOB->BSRR =
        (1U << 2U);
}


static uint8_t vs1053ReadEndFillByte(void) {
    vs1053WriteRegister(
        0x07,
        0x1E06
    );

    return (uint8_t)(
        vs1053ReadRegister(0x06) &
        0x00FFU
    );
}


static void vs1053CancelDecode(void) {
    uint8_t endFillByte =
        vs1053ReadEndFillByte();

    uint8_t fill[32];

    memset(
        fill,
        endFillByte,
        sizeof(fill)
    );

    uint16_t mode =
        vs1053ReadRegister(0x00);

    /*
     * Set SM_CANCEL.
     */
    vs1053WriteRegister(
        0x00,
        mode | 0x0008U
    );

    bool cancelled = false;

    for (
        uint32_t i = 0U;
        i < 64U;
        i++
    ) {
        if (!vs1053WaitForDREQTimeout()) {
            break;
        }

        vs1053SendChunk32(
            fill,
            sizeof(fill)
        );

        if (
            (
                vs1053ReadRegister(0x00) &
                0x0008U
            ) == 0U
        ) {
            cancelled = true;
            break;
        }
    }

    if (!cancelled) {
        /*
         * Cancel failed, so reset the decoder.
         */
        vs1053WriteRegister(
            0x00,
            0x0804
        );

        for (
            volatile uint32_t i = 0U;
            i < 500000U;
            i++
        ) {
        }

        vs1053WaitForDREQTimeout();

        /*
         * Restore the decoder clock.
         */
        vs1053WriteRegister(
            0x03,
            0x9800
        );

        for (
            volatile uint32_t i = 0U;
            i < 100000U;
            i++
        ) {
        }

        /*
         * Restore the default volume.
         */
        vs1053WriteRegister(
            0x0B,
            0x1414
        );

        return;
    }

    /*
     * Drain the final partial frame.
     */
    for (
        uint32_t i = 0U;
        i < 65U;
        i++
    ) {
        if (!vs1053WaitForDREQTimeout()) {
            break;
        }

        vs1053SendChunk32(
            fill,
            sizeof(fill)
        );
    }
}


static void vs1053FinishSong(void) {
    uint8_t endFillByte =
        vs1053ReadEndFillByte();

    uint8_t fill[32];

    memset(
        fill,
        endFillByte,
        sizeof(fill)
    );

    /*
     * 65 * 32 = 2080 bytes.
     *
     * The VS1053 requires at least 2052 end-fill bytes.
     */
    for (
        uint32_t i = 0U;
        i < 65U;
        i++
    ) {
        if (!vs1053WaitForDREQTimeout()) {
            return;
        }

        vs1053SendChunk32(
            fill,
            sizeof(fill)
        );
    }

    vs1053CancelDecode();
}


// ---------------------------------------------------------------------------
// Basic file helpers
// ---------------------------------------------------------------------------

static bool readExactAt(
    FIL *file,
    FSIZE_t position,
    void *buffer,
    UINT size
) {
    if (
        file == NULL ||
        buffer == NULL
    ) {
        return false;
    }

    FRESULT result =
        f_lseek(
            file,
            position
        );

    if (result != FR_OK) {
        return false;
    }

    UINT bytesRead = 0U;

    result = f_read(
        file,
        buffer,
        size,
        &bytesRead
    );

    return (
        result == FR_OK &&
        bytesRead == size
    );
}


static uint32_t readBigEndian32(
    const uint8_t bytes[4]
) {
    return (
        ((uint32_t)bytes[0] << 24) |
        ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8) |
        (uint32_t)bytes[3]
    );
}


static uint32_t readSynchsafe32(
    const uint8_t bytes[4]
) {
    return (
        ((uint32_t)(bytes[0] & 0x7FU) << 21) |
        ((uint32_t)(bytes[1] & 0x7FU) << 14) |
        ((uint32_t)(bytes[2] & 0x7FU) << 7) |
        (uint32_t)(bytes[3] & 0x7FU)
    );
}


// ---------------------------------------------------------------------------
// MP3 header parsing
// ---------------------------------------------------------------------------

static bool parseMp3FrameHeader(
    const uint8_t headerBytes[4],
    Mp3FrameInfo *info
) {
    if (
        headerBytes == NULL ||
        info == NULL
    ) {
        return false;
    }

    uint32_t header =
        ((uint32_t)headerBytes[0] << 24) |
        ((uint32_t)headerBytes[1] << 16) |
        ((uint32_t)headerBytes[2] << 8) |
        (uint32_t)headerBytes[3];

    /*
     * First 11 bits must be set for an MPEG audio frame sync.
     */
    if (
        (header & 0xFFE00000UL) !=
        0xFFE00000UL
    ) {
        return false;
    }

    uint32_t versionId =
        (header >> 19) & 0x03U;

    uint32_t layerId =
        (header >> 17) & 0x03U;

    uint32_t bitrateIndex =
        (header >> 12) & 0x0FU;

    uint32_t sampleRateIndex =
        (header >> 10) & 0x03U;

    uint32_t padding =
        (header >> 9) & 0x01U;

    uint32_t channelMode =
        (header >> 6) & 0x03U;

    /*
     * Version 1 is reserved.
     */
    if (versionId == 1U) {
        return false;
    }

    /*
     * Layer bits 01 mean Layer III.
     */
    if (layerId != 1U) {
        return false;
    }

    /*
     * Bitrate indexes zero and fifteen are invalid here.
     */
    if (
        bitrateIndex == 0U ||
        bitrateIndex == 15U
    ) {
        return false;
    }

    /*
     * Sample-rate index three is reserved.
     */
    if (sampleRateIndex == 3U) {
        return false;
    }

    static const uint16_t mpeg1Layer3Bitrates[16] = {
        0U,
        32U,
        40U,
        48U,
        56U,
        64U,
        80U,
        96U,
        112U,
        128U,
        160U,
        192U,
        224U,
        256U,
        320U,
        0U
    };

    static const uint16_t mpeg2Layer3Bitrates[16] = {
        0U,
        8U,
        16U,
        24U,
        32U,
        40U,
        48U,
        56U,
        64U,
        80U,
        96U,
        112U,
        128U,
        144U,
        160U,
        0U
    };

    static const uint32_t baseSampleRates[3] = {
        44100U,
        48000U,
        32000U
    };

    bool isMpeg1 =
        versionId == 3U;

    uint32_t bitrateKbps;

    if (isMpeg1) {
        bitrateKbps =
            mpeg1Layer3Bitrates[
                bitrateIndex
            ];
    } else {
        bitrateKbps =
            mpeg2Layer3Bitrates[
                bitrateIndex
            ];
    }

    uint32_t sampleRate =
        baseSampleRates[
            sampleRateIndex
        ];

    if (versionId == 2U) {
        /*
         * MPEG 2.
         */
        sampleRate /= 2U;
    } else if (versionId == 0U) {
        /*
         * MPEG 2.5.
         */
        sampleRate /= 4U;
    }

    if (
        bitrateKbps == 0U ||
        sampleRate == 0U
    ) {
        return false;
    }

    uint32_t samplesPerFrame =
        isMpeg1
            ? 1152U
            : 576U;

    uint32_t frameSize;

    if (isMpeg1) {
        frameSize =
            (
                144000U *
                bitrateKbps
            ) /
            sampleRate;

        frameSize += padding;
    } else {
        frameSize =
            (
                72000U *
                bitrateKbps
            ) /
            sampleRate;

        frameSize += padding;
    }

    if (frameSize < 4U) {
        return false;
    }

    info->bitrateKbps =
        bitrateKbps;

    info->sampleRate =
        sampleRate;

    info->samplesPerFrame =
        samplesPerFrame;

    info->frameSize =
        frameSize;

    info->isMpeg1 =
        isMpeg1;

    /*
     * Channel mode three means mono.
     */
    info->isMono =
        channelMode == 3U;

    return true;
}


/*
 * Search forward from startPosition for a valid MP3 frame header.
 */
static bool findFirstMp3Frame(
    FIL *file,
    FSIZE_t startPosition,
    FSIZE_t *framePosition,
    Mp3FrameInfo *frameInfo
) {
    if (
        file == NULL ||
        framePosition == NULL ||
        frameInfo == NULL
    ) {
        return false;
    }

    FSIZE_t fileSize =
        f_size(file);

    if (
        startPosition >= fileSize ||
        fileSize - startPosition < 4U
    ) {
        return false;
    }

    uint8_t header[4];

    FSIZE_t maximumPosition =
        startPosition +
        MP3_FRAME_SEARCH_BYTES;

    if (
        maximumPosition >
        fileSize - 4U
    ) {
        maximumPosition =
            fileSize - 4U;
    }

    for (
        FSIZE_t position = startPosition;
        position <= maximumPosition;
        position++
    ) {
        if (!readExactAt(
                file,
                position,
                header,
                sizeof(header)
            )) {
            return false;
        }

        Mp3FrameInfo candidate;

        if (parseMp3FrameHeader(
                header,
                &candidate
            )) {
            *framePosition =
                position;

            *frameInfo =
                candidate;

            return true;
        }
    }

    return false;
}


// ---------------------------------------------------------------------------
// ID3 handling
// ---------------------------------------------------------------------------

/*
 * Find the end of the ID3v2 tag at the beginning of the MP3.
 *
 * This prevents album artwork and metadata from being sent to the
 * VS1053 before the actual MP3 audio.
 */
static bool findAudioStart(
    FIL *file,
    FSIZE_t *audioStart
) {
    if (
        file == NULL ||
        audioStart == NULL
    ) {
        return false;
    }

    *audioStart = 0U;

    uint8_t header[10];

    if (!readExactAt(
            file,
            0U,
            header,
            sizeof(header)
        )) {
        /*
         * A very short file cannot have a complete ID3 header.
         */
        return f_lseek(
            file,
            0U
        ) == FR_OK;
    }

    if (
        header[0] != (uint8_t)'I' ||
        header[1] != (uint8_t)'D' ||
        header[2] != (uint8_t)'3'
    ) {
        return f_lseek(
            file,
            0U
        ) == FR_OK;
    }

    uint32_t tagSize =
        readSynchsafe32(
            &header[6]
        );

    FSIZE_t position =
        10U + (FSIZE_t)tagSize;

    /*
     * ID3v2.4 may have a 10-byte footer.
     */
    if (
        header[3] == 4U &&
        (header[5] & 0x10U) != 0U
    ) {
        position += 10U;
    }

    if (position > f_size(file)) {
        printf(
            "AUDIO: invalid ID3 tag size\r\n"
        );

        return false;
    }

    *audioStart =
        position;

    return true;
}


// ---------------------------------------------------------------------------
// Duration calculation
// ---------------------------------------------------------------------------

/*
 * Try to read a Xing or Info header.
 *
 * These headers provide the total number of MP3 frames and therefore
 * allow an accurate duration calculation for VBR and CBR files.
 */
static bool readXingDuration(
    FIL *file,
    FSIZE_t framePosition,
    const Mp3FrameInfo *frameInfo,
    uint32_t *durationSeconds
) {
    uint32_t sideInformationOffset;

    if (frameInfo->isMpeg1) {
        sideInformationOffset =
            frameInfo->isMono
                ? 21U
                : 36U;
    } else {
        sideInformationOffset =
            frameInfo->isMono
                ? 13U
                : 21U;
    }

    uint8_t xingHeader[12];

    if (!readExactAt(
            file,
            framePosition +
                sideInformationOffset,
            xingHeader,
            sizeof(xingHeader)
        )) {
        return false;
    }

    bool isXing =
        memcmp(
            xingHeader,
            "Xing",
            4U
        ) == 0;

    bool isInfo =
        memcmp(
            xingHeader,
            "Info",
            4U
        ) == 0;

    if (
        !isXing &&
        !isInfo
    ) {
        return false;
    }

    uint32_t flags =
        readBigEndian32(
            &xingHeader[4]
        );

    /*
     * Bit zero means a total-frame count follows.
     */
    if ((flags & 0x00000001UL) == 0U) {
        return false;
    }

    uint32_t totalFrames =
        readBigEndian32(
            &xingHeader[8]
        );

    if (totalFrames == 0U) {
        return false;
    }

    uint64_t totalSamples =
        (uint64_t)totalFrames *
        frameInfo->samplesPerFrame;

    *durationSeconds =
        (uint32_t)(
            (
                totalSamples +
                frameInfo->sampleRate / 2U
            ) /
            frameInfo->sampleRate
        );

    return true;
}


/*
 * Try to read a VBRI header.
 */
static bool readVbriDuration(
    FIL *file,
    FSIZE_t framePosition,
    const Mp3FrameInfo *frameInfo,
    uint32_t *durationSeconds
) {
    /*
     * VBRI normally starts 36 bytes after the MPEG frame begins.
     */
    uint8_t vbriHeader[18];

    if (!readExactAt(
            file,
            framePosition + 36U,
            vbriHeader,
            sizeof(vbriHeader)
        )) {
        return false;
    }

    if (
        memcmp(
            vbriHeader,
            "VBRI",
            4U
        ) != 0
    ) {
        return false;
    }

    /*
     * The total frame count is bytes 14 through 17 of the VBRI header.
     */
    uint32_t totalFrames =
        readBigEndian32(
            &vbriHeader[14]
        );

    if (totalFrames == 0U) {
        return false;
    }

    uint64_t totalSamples =
        (uint64_t)totalFrames *
        frameInfo->samplesPerFrame;

    *durationSeconds =
        (uint32_t)(
            (
                totalSamples +
                frameInfo->sampleRate / 2U
            ) /
            frameInfo->sampleRate
        );

    return true;
}


/*
 * Check for an ID3v1 tag at the end of the file.
 *
 * ID3v1 occupies the final 128 bytes and should not count as audio data.
 */
static bool hasId3v1Tag(
    FIL *file
) {
    FSIZE_t fileSize =
        f_size(file);

    if (fileSize < 128U) {
        return false;
    }

    uint8_t marker[3];

    if (!readExactAt(
            file,
            fileSize - 128U,
            marker,
            sizeof(marker)
        )) {
        return false;
    }

    return (
        marker[0] == (uint8_t)'T' &&
        marker[1] == (uint8_t)'A' &&
        marker[2] == (uint8_t)'G'
    );
}


/*
 * Calculate the duration using the already-open MP3 file.
 *
 * Order:
 *
 *   1. Find the first MP3 frame.
 *   2. Use Xing/Info frame count when available.
 *   3. Use VBRI frame count when available.
 *   4. Otherwise calculate using file size and the frame bitrate.
 *
 * The file-size calculation is accurate for normal constant-bitrate
 * MP3 files. Xing and VBRI provide accurate duration for most VBR files.
 */
static uint32_t calculateMp3Duration(
    FIL *file,
    FSIZE_t audioStart,
    FSIZE_t *firstFramePosition
) {
    if (
        file == NULL ||
        firstFramePosition == NULL
    ) {
        return 0U;
    }

    Mp3FrameInfo frameInfo;

    FSIZE_t framePosition =
        audioStart;

    if (!findFirstMp3Frame(
            file,
            audioStart,
            &framePosition,
            &frameInfo
        )) {
        printf(
            "AUDIO: no valid MP3 frame found\r\n"
        );

        return 0U;
    }

    *firstFramePosition =
        framePosition;

    printf(
        "AUDIO: first frame=%lu bitrate=%lukbps "
        "sampleRate=%lu\r\n",
        (unsigned long)framePosition,
        (unsigned long)frameInfo.bitrateKbps,
        (unsigned long)frameInfo.sampleRate
    );

    uint32_t duration = 0U;

    if (readXingDuration(
            file,
            framePosition,
            &frameInfo,
            &duration
        )) {
        printf(
            "AUDIO: duration source=Xing/Info\r\n"
        );

        return duration;
    }

    if (readVbriDuration(
            file,
            framePosition,
            &frameInfo,
            &duration
        )) {
        printf(
            "AUDIO: duration source=VBRI\r\n"
        );

        return duration;
    }

    FSIZE_t fileSize =
        f_size(file);

    if (fileSize <= framePosition) {
        return 0U;
    }

    uint64_t audioBytes =
        (uint64_t)(
            fileSize -
            framePosition
        );

    if (
        hasId3v1Tag(file) &&
        audioBytes >= 128U
    ) {
        audioBytes -= 128U;
    }

    uint64_t bits =
        audioBytes * 8ULL;

    uint64_t bitsPerSecond =
        (uint64_t)
            frameInfo.bitrateKbps *
        1000ULL;

    if (bitsPerSecond == 0U) {
        return 0U;
    }

    duration =
        (uint32_t)(
            (
                bits +
                bitsPerSecond / 2ULL
            ) /
            bitsPerSecond
        );

    printf(
        "AUDIO: duration source=file-size/bitrate\r\n"
    );

    return duration;
}


// ---------------------------------------------------------------------------
// Direct file opening
// ---------------------------------------------------------------------------

/*
 * Open the MP3 directly using the cluster and file size stored inside
 * TrackRecord.
 *
 * This avoids the slow filename search through the root directory.
 */
static FRESULT openPendingAudioFile(
    State *state,
    bool *usedDirectOpen
) {
    if (usedDirectOpen != NULL) {
        *usedDirectOpen = false;
    }

    if (
        state != NULL &&
        pendingStartCluster != 0U &&
        pendingFileSize != 0U &&
        state->indexFiles.allTracksFile.obj.fs != NULL
    ) {
        FATFS *fs =
            state->indexFiles.allTracksFile.obj.fs;

        FF_DIRENT_REF fileRef;

        memset(
            &fileRef,
            0,
            sizeof(fileRef)
        );

        fileRef.fs =
            fs;

        fileRef.mount_id =
            fs->id;

        fileRef.attr =
            AM_ARC;

        fileRef.start_cluster =
            pendingStartCluster;

        fileRef.file_size =
            pendingFileSize;

        FRESULT result =
            f_open_ref_read(
                &audioStream.file,
                &fileRef
            );

        if (result == FR_OK) {
            if (usedDirectOpen != NULL) {
                *usedDirectOpen = true;
            }

            return FR_OK;
        }

        printf(
            "AUDIO: direct open failed=%d "
            "cluster=%lu size=%lu\r\n",
            result,
            (unsigned long)pendingStartCluster,
            (unsigned long)pendingFileSize
        );
    }

    /*
     * Slow fallback.
     */
    return f_open(
        &audioStream.file,
        pendingFilename,
        FA_READ
    );
}


// ---------------------------------------------------------------------------
// Public audio functions
// ---------------------------------------------------------------------------

uint32_t audioGetPendingDuration(void) {
    durationReady = false;

    return pendingDuration;
}


bool audioIsDurationReady(void) {
    return durationReady;
}


void audioRequestPlayFile(
    const TrackRecord *track
) {
    if (track == NULL) {
        return;
    }

    strncpy(
        pendingFilename,
        track->filename,
        sizeof(pendingFilename) - 1U
    );

    pendingFilename[
        sizeof(pendingFilename) - 1U
    ] = '\0';

    pendingStartCluster =
        track->startCluster;

    pendingFileSize =
        track->fileSize;

    /*
     * Do not mark the old song's duration as ready.
     */
    durationReady = false;

    songChangeRequested = true;
}


void audioSetPlaying(bool playing) {
    if (!audioStream.isOpen) {
        return;
    }

    audioStream.isPlaying =
        playing;
}


void audioStop(void) {
    if (audioStream.isOpen) {
        FRESULT closeResult =
            f_close(
                &audioStream.file
            );

        if (closeResult != FR_OK) {
            printf(
                "AUDIO: close error=%d\r\n",
                closeResult
            );
        }

        audioStream.isOpen = false;
    }

    audioStream.isPlaying = false;

    audioStream.currentFilename[0] =
        '\0';

    stageLen = 0U;
    stagePos = 0U;
}


bool audioIsPlaying(void) {
    return audioStream.isPlaying;
}


// ---------------------------------------------------------------------------
// Next-track prefetch
// ---------------------------------------------------------------------------

void playbackPrefetchNext(State *state) {
    if (
        !state->player.isPlaying ||
        !state->playbackContext.nextTrackIsValid ||
        state->playbackContext.nextTrackIsLoaded
    ) {
        return;
    }

    printf(
        "prefetching next running logic\r\n"
    );

    PlaybackContext *context =
        &state->playbackContext;

    TrackRecord next;
    UINT bytesRead = 0U;

    FRESULT result =
        f_lseek(
            &state->indexFiles.allTracksFile,
            (FSIZE_t)(
                state->player.track.index + 1U
            ) *
            sizeof(TrackRecord)
        );

    if (result != FR_OK) {
        printf(
            "prefetching seek error\r\n"
        );

        return;
    }

    result = f_read(
        &state->indexFiles.allTracksFile,
        &next,
        sizeof(TrackRecord),
        &bytesRead
    );

    if (result != FR_OK) {
        printf(
            "prefetching read error\r\n"
        );

        return;
    }

    if (bytesRead < sizeof(TrackRecord)) {
        context->nextTrackIsValid = false;
        context->nextTrackIsLoaded = false;

        memset(
            &context->nextTrack,
            0,
            sizeof(TrackRecord)
        );

        printf(
            "prefetching no bytes read error\r\n"
        );

        return;
    }

    context->nextTrack =
        next;

    context->nextTrackIsLoaded =
        true;

    context->nextTrackIsValid =
        true;

    printf(
        "song: %s\r\n",
        context->nextTrack.filename
    );
}


void onTrackEnd(State *state) {
    PlaybackContext *context =
        &state->playbackContext;

    printf(
        "In onTrackEnd\r\n"
    );

    if (context->nextTrackIsLoaded) {
        printf(
            "nextTrackIsLoaded\r\n"
        );

        playAudioFile(
            state,
            &context->nextTrack
        );

        return;
    }

    if (context->nextTrackIsValid) {
        printf(
            "onTrackEndPrefetchNext\r\n"
        );

        playbackPrefetchNext(state);

        if (context->nextTrackIsLoaded) {
            playAudioFile(
                state,
                &context->nextTrack
            );
        } else {
            context->nextTrackIsLoaded =
                false;

            context->nextTrackIsValid =
                false;
        }
    }
}


// ---------------------------------------------------------------------------
// Main audio processing
// ---------------------------------------------------------------------------

void audioProcess(State *state) {
    // ---------------------------------------------------------------------
    // Start a newly requested song.
    // ---------------------------------------------------------------------

    if (songChangeRequested) {
        uint32_t totalStart =
            millis();

        uint32_t phaseStart;

        songChangeRequested = false;

        audioStream.isPlaying = false;

        stageLen = 0U;
        stagePos = 0U;

        uint16_t savedVolume =
            vs1053ReadRegister(0x0B);

        /*
         * Mute during the song transition.
         */
        vs1053WriteRegister(
            0x0B,
            0xFEFE
        );

        // -----------------------------------------------------------------
        // Cancel the old decoder stream.
        // -----------------------------------------------------------------

        phaseStart =
            millis();

        vs1053CancelDecode();

        printf(
            "AUDIO: cancel=%lums\r\n",
            (unsigned long)(
                millis() - phaseStart
            )
        );

        // -----------------------------------------------------------------
        // Close the old MP3 file.
        // -----------------------------------------------------------------

        phaseStart =
            millis();

        if (audioStream.isOpen) {
            FRESULT closeResult =
                f_close(
                    &audioStream.file
                );

            if (closeResult != FR_OK) {
                printf(
                    "AUDIO: old file close error=%d\r\n",
                    closeResult
                );
            }

            audioStream.isOpen = false;
        }

        printf(
            "AUDIO: close=%lums\r\n",
            (unsigned long)(
                millis() - phaseStart
            )
        );

        /*
         * Reset the VS1053 playback-position timer.
         */
        vs1053WriteRegister(
            0x04,
            0x0000
        );

        vs1053WriteRegister(
            0x04,
            0x0000
        );

        // -----------------------------------------------------------------
        // Open the new MP3 directly.
        // -----------------------------------------------------------------

        phaseStart =
            millis();

        bool usedDirectOpen =
            false;

        FRESULT openResult =
            openPendingAudioFile(
                state,
                &usedDirectOpen
            );

        printf(
            "AUDIO: open=%lums result=%d mode=%s\r\n",
            (unsigned long)(
                millis() - phaseStart
            ),
            openResult,
            usedDirectOpen
                ? "direct"
                : "filename"
        );

        if (openResult != FR_OK) {
            printf(
                "Failed to open file: %s\r\n",
                pendingFilename
            );

            audioStream.currentFilename[0] =
                '\0';

            audioStream.isOpen =
                false;

            audioStream.isPlaying =
                false;

            durationReady =
                false;

            vs1053WriteRegister(
                0x0B,
                savedVolume
            );

            return;
        }

        strncpy(
            audioStream.currentFilename,
            pendingFilename,
            sizeof(
                audioStream.currentFilename
            ) - 1U
        );

        audioStream.currentFilename[
            sizeof(
                audioStream.currentFilename
            ) - 1U
        ] = '\0';

        audioStream.isOpen =
            true;

        audioStream.isPlaying =
            true;

        // -----------------------------------------------------------------
        // Find the ID3 tag end.
        // -----------------------------------------------------------------

        phaseStart =
            millis();

        FSIZE_t audioStart =
            0U;

        if (!findAudioStart(
                &audioStream.file,
                &audioStart
            )) {
            printf(
                "AUDIO: failed to find audio start\r\n"
            );

            audioStop();

            vs1053WriteRegister(
                0x0B,
                savedVolume
            );

            return;
        }

        // -----------------------------------------------------------------
        // Calculate the full song duration.
        // -----------------------------------------------------------------

        FSIZE_t firstFramePosition =
            audioStart;

        pendingDuration =
            calculateMp3Duration(
                &audioStream.file,
                audioStart,
                &firstFramePosition
            );

        /*
         * These are the same values your original state-update system
         * reads through audioIsDurationReady() and
         * audioGetPendingDuration().
         */
        durationReady =
            true;

        printf(
            "AUDIO: duration=%lu seconds calculation=%lums\r\n",
            (unsigned long)pendingDuration,
            (unsigned long)(
                millis() - phaseStart
            )
        );

        /*
         * Move back to the beginning of real MP3 audio before priming.
         */
        FRESULT seekResult =
            f_lseek(
                &audioStream.file,
                firstFramePosition
            );

        if (seekResult != FR_OK) {
            printf(
                "AUDIO: failed to seek to first frame=%d\r\n",
                seekResult
            );

            audioStop();

            vs1053WriteRegister(
                0x0B,
                savedVolume
            );

            return;
        }

        // -----------------------------------------------------------------
        // Prime the decoder with actual MP3 data.
        // -----------------------------------------------------------------

        phaseStart =
            millis();

        uint32_t primeBytesSent =
            0U;

        uint32_t primeReadCalls =
            0U;

        stageLen = 0U;
        stagePos = 0U;

        while (
            primeBytesSent <
                AUDIO_PRIME_MAX_BYTES &&
            (
                GPIOA->IDR &
                (1U << 11)
            ) != 0U
        ) {
            if (stagePos >= stageLen) {
                stagePos = 0U;
                stageLen = 0U;

                FRESULT readResult =
                    f_read(
                        &audioStream.file,
                        stageBuf,
                        STAGE_SIZE,
                        &stageLen
                    );

                primeReadCalls++;

                if (readResult != FR_OK) {
                    printf(
                        "AUDIO: prime read error=%d\r\n",
                        readResult
                    );

                    audioStop();

                    break;
                }

                if (stageLen == 0U) {
                    break;
                }
            }

            UINT bytesToSend =
                stageLen - stagePos;

            if (
                bytesToSend >
                AUDIO_CHUNK_SIZE
            ) {
                bytesToSend =
                    AUDIO_CHUNK_SIZE;
            }

            vs1053SendChunk32(
                &stageBuf[stagePos],
                (uint16_t)bytesToSend
            );

            stagePos +=
                bytesToSend;

            primeBytesSent +=
                bytesToSend;
        }

        printf(
            "AUDIO: prime=%lums bytes=%lu reads=%lu "
            "filePosition=%lu dreq=%u\r\n",
            (unsigned long)(
                millis() - phaseStart
            ),
            (unsigned long)
                primeBytesSent,
            (unsigned long)
                primeReadCalls,
            (unsigned long)
                f_tell(
                    &audioStream.file
                ),
            (
                GPIOA->IDR &
                (1U << 11)
            ) != 0U
                ? 1U
                : 0U
        );

        /*
         * Restore the normal volume.
         */
        vs1053WriteRegister(
            0x0B,
            savedVolume
        );

        printf(
            "AUDIO: complete startup path=%lums\r\n",
            (unsigned long)(
                millis() - totalStart
            )
        );

        return;
    }

    // ---------------------------------------------------------------------
    // Normal audio streaming.
    // ---------------------------------------------------------------------

    if (
        !audioStream.isPlaying ||
        !audioStream.isOpen
    ) {
        return;
    }

    for (
        uint32_t chunk = 0U;
        chunk <
            AUDIO_MAX_CHUNKS_PER_PASS;
        chunk++
    ) {
        /*
         * The VS1053 input buffer is full.
         */
        if (
            (
                GPIOA->IDR &
                (1U << 11)
            ) == 0U
        ) {
            return;
        }

        /*
         * Refill the staging buffer when all staged bytes have been sent.
         */
        if (stagePos >= stageLen) {
            stageLen = 0U;
            stagePos = 0U;

            FRESULT readResult =
                f_read(
                    &audioStream.file,
                    stageBuf,
                    STAGE_SIZE,
                    &stageLen
                );

            if (readResult != FR_OK) {
                printf(
                    "audio f_read error: %d\r\n",
                    readResult
                );

                audioStop();

                return;
            }

            /*
             * Zero bytes means the MP3 file has ended.
             */
            if (stageLen == 0U) {
                FRESULT closeResult =
                    f_close(
                        &audioStream.file
                    );

                if (closeResult != FR_OK) {
                    printf(
                        "AUDIO: end close error=%d\r\n",
                        closeResult
                    );
                }

                audioStream.isOpen =
                    false;

                audioStream.isPlaying =
                    false;

                audioStream.currentFilename[0] =
                    '\0';

                vs1053FinishSong();

                onTrackEnd(state);

                return;
            }
        }

        UINT bytesToSend =
            stageLen - stagePos;

        if (
            bytesToSend >
            AUDIO_CHUNK_SIZE
        ) {
            bytesToSend =
                AUDIO_CHUNK_SIZE;
        }

        vs1053SendChunk32(
            &stageBuf[stagePos],
            (uint16_t)bytesToSend
        );

        stagePos +=
            bytesToSend;
    }
}


uint32_t audioGetPosition(void) {
    return vs1053ReadRegister(
        0x04
    );
}

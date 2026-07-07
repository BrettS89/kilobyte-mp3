/*
 * utils.c
 *
 *  Created on: Jul 6, 2026
 *      Author: brettsodie
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "ff.h"
#include "controls.h"
#include "screens.h"

void initialLoadTrackWindow(State *state);
//void maybeRefillTrackWindow(State *state);
void maybeRefillTrackWindowOnScrollDown(State *state);
void maybeRefillTrackWindowOnScrollUp(State *state);

static FIL idxFile;
static bool idxOpen = false;

static bool initialTrackLoadRequested = false;
static bool scrollDownRefillRequested = false;
static bool scrollUpRefillRequested = false;
//static bool refillTrackWindowRequested = false;

void requestInitialTrackLoad() {
	initialTrackLoadRequested = true;
}

void requestScrollDownRefillTrackWindow() {
	scrollDownRefillRequested = true;
}

void requestScrollUpRefillTrackWindow() {
	scrollUpRefillRequested = true;
}

void runInputRequests(State *state) {
	if (initialTrackLoadRequested) {
		initialLoadTrackWindow(state);
		initialTrackLoadRequested = false;
		navigate(state, SONGS);
	} else if (scrollDownRefillRequested) {
		maybeRefillTrackWindowOnScrollDown(state);
		scrollDownRefillRequested = false;
	} else if (scrollUpRefillRequested) {
		maybeRefillTrackWindowOnScrollUp(state);
		scrollUpRefillRequested = false;
	}
}

bool trackIndexInit(void) {
    if (f_open(&idxFile, "/system/kilobyte.idx", FA_READ) != FR_OK) {
        printf("failed to open kilobyte.idx\r\n");
        return false;
    }
    idxOpen = true;
    return true;
}


bool loadTrackWindow(TrackRecord tracks[], uint32_t startIndex, uint32_t count, uint32_t *trackCount) {
    if (!idxOpen) return false;

    FSIZE_t offset = (FSIZE_t)startIndex * sizeof(TrackRecord);

    if (f_lseek(&idxFile, offset) != FR_OK) {
        printf("failed to seek to index %lu\r\n", startIndex);
        return false;
    }

    UINT bytesRead;
    if (f_read(&idxFile, tracks, count * sizeof(TrackRecord), &bytesRead) != FR_OK) {
        printf("failed to read tracks\r\n");
        return false;
    }

    *trackCount = bytesRead / sizeof(TrackRecord);
    return true;
}

void initialLoadTrackWindow(State *state) {
	uint32_t trackCount;
	loadTrackWindow(state->trackList.tracks, 0, 16, &trackCount);
}

//void maybeRefillTrackWindow(State *state) {
//    uint32_t globalCursor = state->trackList.cursorIndex;
//    uint32_t windowStart = state->trackList.tracks[0].index;
//    uint32_t totalTracks = state->trackList.totalTracksInSystem;
//
//    uint32_t positionInWindow = globalCursor - windowStart;
//
//    printf("cursor=%lu windowStart=%lu positionInWindow=%lu total=%lu\r\n",
//           globalCursor, windowStart, positionInWindow, totalTracks);
//
//    uint32_t newStart = windowStart;
//
//    if (positionInWindow >= 8 && windowStart + 16 < totalTracks) {
//        printf("  -> branch: scroll down shift\r\n");
//        newStart = globalCursor - 4;
//    } else if (positionInWindow <= 3 && windowStart > 0) {
//        printf("  -> branch: scroll up shift\r\n");
//        newStart = (globalCursor < 12) ? 0 : globalCursor - 12;
//    } else {
//        printf("  -> branch: no refill\r\n");
//        return;
//    }
//
//    loadTrackWindow(state->trackList.tracks, newStart, 16, &state->trackList.totalCount);
//
//    printf("  after load: windowStart now=%lu\r\n", state->trackList.tracks[0].index);
//}

void maybeRefillTrackWindowOnScrollDown(State *state) {
    uint32_t globalCursor = state->trackList.cursorIndex;
    uint32_t windowStart = state->trackList.tracks[0].index;
    uint32_t totalTracks = state->trackList.totalTracksInSystem;

    uint32_t positionInWindow = globalCursor - windowStart;

    if (positionInWindow >= 8 && windowStart + 16 < totalTracks) {
        uint32_t newStart = globalCursor - 4;
        loadTrackWindow(state->trackList.tracks, newStart, 16, &state->trackList.totalCount);
    }
}

void maybeRefillTrackWindowOnScrollUp(State *state) {
    uint32_t globalCursor = state->trackList.cursorIndex;
    uint32_t windowStart = state->trackList.tracks[0].index;

    if (windowStart == 0) return; // already at the true start, nothing to fetch

    uint32_t positionInWindow = globalCursor - windowStart;

    if (positionInWindow < 4) {
        uint32_t newStart = (globalCursor < 8) ? 0 : globalCursor - 8;
        loadTrackWindow(state->trackList.tracks, newStart, 16, &state->trackList.totalCount);
    }
}

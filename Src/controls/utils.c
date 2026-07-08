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
#include "state.h"

void initialLoadTrackWindow(State *state);
//void maybeRefillTrackWindow(State *state);
void maybeRefillTrackWindowOnScrollDown(State *state);
void maybeRefillTrackWindowOnScrollUp(State *state);

static bool initialTrackLoadRequested = false;
static bool scrollDownRefillRequested = false;
static bool scrollUpRefillRequested = false;

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

bool trackIndexInit(State *state) {
    if (f_open(&state->indexFiles.allTracksFile, "/system/kilobyte.idx", FA_READ) != FR_OK) {
        printf("failed to open kilobyte.idx\r\n");
        return false;
    }
    state->indexFiles.allTracksFileOpen = true;
    return true;
}

bool loadTrackWindow(FIL *file, TrackRecord tracks[], uint32_t startIndex, uint32_t count, uint32_t *trackCount) {
    FSIZE_t offset = (FSIZE_t)startIndex * sizeof(TrackRecord);

    if (f_lseek(file, offset) != FR_OK) {
        printf("failed to seek to index %lu\r\n", startIndex);
        return false;
    }

    UINT bytesRead;
    if (f_read(file, tracks, count * sizeof(TrackRecord), &bytesRead) != FR_OK) {
        printf("failed to read tracks\r\n");
        return false;
    }

    *trackCount = bytesRead / sizeof(TrackRecord);
    return true;
}

void initialLoadTrackWindow(State *state) {
	uint32_t trackCount;
	loadTrackWindow(&state->indexFiles.allTracksFile, state->trackList.tracks, 0, 16, &trackCount);
}

void maybeRefillTrackWindowOnScrollDown(State *state) {
    uint32_t globalCursor = state->trackList.cursorIndex;
    uint32_t windowStart = state->trackList.tracks[0].index;
    uint32_t totalTracks = state->trackList.totalTracksInSystem;

    uint32_t positionInWindow = globalCursor - windowStart;

    if (positionInWindow >= 8 && windowStart + 16 < totalTracks) {
        uint32_t newStart = globalCursor - 4;
        loadTrackWindow(&state->indexFiles.allTracksFile, state->trackList.tracks, newStart, 16, &state->trackList.totalCount);
    }
}

void maybeRefillTrackWindowOnScrollUp(State *state) {
    uint32_t globalCursor = state->trackList.cursorIndex;
    uint32_t windowStart = state->trackList.tracks[0].index;

    if (windowStart == 0) return; // already at the true start, nothing to fetch

    uint32_t positionInWindow = globalCursor - windowStart;

    if (positionInWindow < 4) {
        uint32_t newStart = (globalCursor < 8) ? 0 : globalCursor - 8;
        loadTrackWindow(&state->indexFiles.allTracksFile, state->trackList.tracks, newStart, 16, &state->trackList.totalCount);
    }
}

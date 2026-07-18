/*
 * tracks.c
 *
 *  Created on: Jul 16, 2026
 *      Author: brettsodie
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "ff.h"
#include "screens.h"
#include "state.h"

void initialLoadArtistWindow(State *state);
//void maybeRefillTrackWindow(State *state);
static void maybeRefillArtistWindowOnScrollDown(State *state);
static void maybeRefillArtistWindowOnScrollUp(State *state);

static bool initialArtistLoadRequested = false;
static bool scrollDownRefillRequested = false;
static bool scrollUpRefillRequested = false;

void requestInitialArtistLoad() {
	initialArtistLoadRequested = true;
}

void requestScrollDownRefillArtistWindow() {
	scrollDownRefillRequested = true;
}

void requestScrollUpRefillArtistWindow() {
	scrollUpRefillRequested = true;
}

void runArtistInputRequests(State *state) {
	if (initialArtistLoadRequested) {
		initialLoadArtistWindow(state);
		initialArtistLoadRequested = false;
		navigate(state, ARTISTS);
	} else if (scrollDownRefillRequested) {
		maybeRefillArtistWindowOnScrollDown(state);
		scrollDownRefillRequested = false;
	} else if (scrollUpRefillRequested) {
		maybeRefillArtistWindowOnScrollUp(state);
		scrollUpRefillRequested = false;
	}
}

bool artistIndexInit(State *state) {
    if (f_open(&state->indexFiles.allArtistsFile, "/system/artists.idx", FA_READ) != FR_OK) {
        printf("failed to open artists.idx\r\n");
        return false;
    }
    state->indexFiles.allArtistsFileOpen = true;

	FSIZE_t fileSize = f_size(&state->indexFiles.allArtistsFile);

	state->artistList.totalArtists = fileSize / sizeof(ArtistRecord);

    return true;
}

bool loadArtistWindow(FIL *file, ArtistRecord artists[], uint32_t startIndex, uint32_t count, uint32_t *trackCount) {
    FSIZE_t offset = (FSIZE_t)startIndex * sizeof(ArtistRecord);

    if (f_lseek(file, offset) != FR_OK) {
        printf("failed to seek to index %lu\r\n", startIndex);
        return false;
    }

    UINT bytesRead;
    if (f_read(file, artists, count * sizeof(ArtistRecord), &bytesRead) != FR_OK) {
        printf("failed to read tracks\r\n");
        return false;
    }

    *trackCount = bytesRead / sizeof(TrackRecord);
    return true;
}

void initialLoadArtistWindow(State *state) {
	uint32_t artistCount;
	loadArtistWindow(&state->indexFiles.allArtistsFile, state->artistList.artists, 0, 16, &artistCount);
}

static void maybeRefillArtistWindowOnScrollDown(State *state) {
    uint32_t globalCursor = state->artistList.cursorIndex;
    uint32_t windowStart = state->artistList.artists[0].index;
    uint32_t totalTracks = state->artistList.totalArtists;

    uint32_t positionInWindow = globalCursor - windowStart;

    if (positionInWindow >= 8 && windowStart + 16 < totalTracks) {
        uint32_t newStart = globalCursor - 4;
        loadArtistWindow(&state->indexFiles.allArtistsFile, state->artistList.artists, newStart, 16, &state->artistList.inWindowCount);
    }
}

static void maybeRefillArtistWindowOnScrollUp(State *state) {
    uint32_t globalCursor = state->artistList.cursorIndex;
    uint32_t windowStart = state->artistList.artists[0].index;

    if (windowStart == 0) return; // already at the true start, nothing to fetch

    uint32_t positionInWindow = globalCursor - windowStart;

    if (positionInWindow < 4) {
        uint32_t newStart = (globalCursor < 8) ? 0 : globalCursor - 8;
        loadArtistWindow(&state->indexFiles.allArtistsFile, state->artistList.artists, newStart, 16, &state->artistList.inWindowCount);
    }
}

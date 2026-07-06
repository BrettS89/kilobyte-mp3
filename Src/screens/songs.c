/*
 * songs.c
 *
 *  Created on: Jun 29, 2026
 *      Author: brettsodie
 */

#include <stdio.h>
#include <string.h>
#include "state.h"
#include "screens.h"
#include "display.h"
#include "components.h"
#include "sdcard.h"
#include "index-tracks.h"

#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))

void drawSongsScreen(State *state) {
    clearFrameBuffer();

    uint32_t globalCursor = state->trackList.cursorIndex;
    uint32_t totalTracks = state->trackList.totalTracksInSystem;
    uint32_t windowStart = state->trackList.tracks[0].index;

    uint32_t positionInWindow = globalCursor - windowStart;

    int topIndex;

    if (globalCursor <= 3) {
        topIndex = 0;
    } else if (globalCursor >= totalTracks - 4) {
        topIndex = (int)totalTracks - (int)windowStart - 7;
    } else {
        topIndex = (int)positionInWindow - 3;
    }

    if (topIndex < 0) topIndex = 0;
    if (topIndex > 9) topIndex = 9;

    int visibleCursorRow = (int)positionInWindow - topIndex;

    for (int i = 0; i < 7; i++) {
        if (i == visibleCursorRow) {
            uint8_t row = i * 8 + 8;
            drawPixel(row + 3, 0);
            drawPixel(row + 4, 0);
            drawPixel(row + 3, 1);
            drawPixel(row + 4, 1);
            drawPixel(row + 5, 0);
            drawPixel(row + 5, 1);
        }

        uint8_t strRow = i + 1;
        drawString(strRow, 4, state->trackList.tracks[i + topIndex].filename);
    }

    renderHeaderInverse("Songs", state);

    setFrameBufferUpdated();
}

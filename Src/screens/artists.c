/*
 * artists.c
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

void drawArtistsScreen(State *state) {
    clearFrameBuffer();

    uint32_t globalCursor = state->artistList.cursorIndex;
    uint32_t totalTracks = state->artistList.totalArtists;
    uint32_t windowStart = state->artistList.artists[0].index;

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
        drawString(strRow, 4, state->artistList.artists[i + topIndex].name);
    }

    renderHeaderInverse("Artists", state);

    setFrameBufferUpdated();
}

/*
 * songs.c
 *
 *  Created on: Jun 29, 2026
 *      Author: brettsodie
 */


#include <string.h>
#include "state.h"
#include "screens.h"
#include "display.h"
#include "components.h"
#include "sdcard.h"

#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))

void drawSongsScreen(State *state) {
    static char fileNames[100][64];
    static uint32_t fileCount = 0;

    if (fileCount == 0) {
        listMp3Files(fileNames, &fileCount);
    }

    state->navigationHistory[state->historyIndex].scrollableItemCount = fileCount;

    for (int i = 0; i < fileCount && i < 100; i++) {
        strncpy(state->navigationHistory[state->historyIndex].items[i].name,
                fileNames[i], 63);
    }

    clearFrameBuffer();

    for (int i = 0; i < 7; i++) {
        if (i == state->navigationHistory[state->historyIndex].cursorIndex) {
            uint8_t row = i * 8 + 8;
            drawPixel(row + 3, 0);
            drawPixel(row + 4, 0);
            drawPixel(row + 3, 1);
            drawPixel(row + 4, 1);
            drawPixel(row + 5, 0);
            drawPixel(row + 5, 1);
        }

        uint8_t strRow = i + 1;
        drawString(strRow, 4, fileNames[i]);
    }

    renderHeaderInverse("Songs");
    drawFrame();
}

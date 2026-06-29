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

#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))

void drawSongsScreen(State *state) {
	ScrollableItem items[] = {
			{
				.name = "Guns N Roses - Don't Cry",
				.screenTrigger = ARTISTS,
			},
			{	.name = "ACDC - Back in Black",
				.screenTrigger = SONGS,
			},
	};

    memcpy(state->navigationHistory[state->historyIndex].items, items, sizeof(items));

    size_t len = ARRAY_LENGTH(items);

	state->navigationHistory[state->historyIndex].scrollableItemCount = len;

	clearFrameBuffer();

	for (int i = 0; i < len; i++) {
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

		drawString(strRow, 4, items[i].name);
//		drawString(strRow, 118, ">");
	}

	renderHeaderInverse("Songs");

	drawFrame();
}

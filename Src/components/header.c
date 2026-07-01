/*
 * header.c
 *
 *  Created on: Jun 29, 2026
 *      Author: brettsodie
 */

#include <stdint.h>
#include "display.h"
#include "components.h"
#include "state.h"

void renderHeader(char *str) {
	drawStringShifted(0, 0, str, 1);

	for(int i = 0; i < 128; i++) {
		drawPixel(7, i);
	}
}

void renderHeaderInverse(char *str, State *state) {
    // fill entire header row white
    for (int i = 0; i < 128; i++) {
        frameBuffer[0][i] = 0xFF;
    }

    // draw text in black on top of white background
    drawStringShiftedInverse(0, 2, str, 0);

    uint8_t iconX = 119;

    if (!state->player.isPlaying) {
        // pause icon — two vertical bars
        for (int row = 1; row <= 6; row++) {
            frameBuffer[0][iconX]     &= ~(1U << row);
            frameBuffer[0][iconX + 1] &= ~(1U << row);
        }
        for (int row = 1; row <= 6; row++) {
            frameBuffer[0][iconX + 4] &= ~(1U << row);
            frameBuffer[0][iconX + 5] &= ~(1U << row);
        }
    } else {
        // play icon — right pointing triangle, 6px tall, 5px wide
    	// play icon — right pointing triangle, 6px tall, 7px wide
		for (int row = 1; row <= 6; row++) {
			int halfHeight = 3;
			int dist = (row <= halfHeight) ? (row - 1) * 2 : (6 - row) * 2;
			for (int col = 0; col <= dist; col++) {
				frameBuffer[0][iconX + col] &= ~(1U << row);
			}
		}
    }
}

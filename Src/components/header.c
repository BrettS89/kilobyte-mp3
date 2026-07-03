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
#include "string.h"

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

    // center the text
    int textLen = strlen(str);
    int textWidth = textLen * 6;  // 6px per character (5px char + 1px spacing)
    int startCol = (128 - textWidth) / 2;
    if (startCol < 0) startCol = 0;

    drawStringShiftedInverse(0, startCol, str, 0);

    // play/pause icon on the LEFT side
    uint8_t iconX = 4;

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
        // play icon — right pointing triangle
        for (int row = 1; row <= 6; row++) {
            int halfHeight = 3;
            int dist = (row <= halfHeight) ? (row - 1) * 2 : (6 - row) * 2;
            for (int col = 0; col <= dist; col++) {
                frameBuffer[0][iconX + col] &= ~(1U << row);
            }
        }
    }

    // battery icon on the RIGHT side (full battery)
    // battery icon on the RIGHT side (full battery)
    // battery icon on the RIGHT side (full battery)
    // battery icon on the RIGHT side (partially drained)
	uint8_t battX = 114;

	// battery body outline (10px wide, 6px tall)
	for (int col = 0; col <= 9; col++) {
		frameBuffer[0][battX + col] &= ~(1U << 1);  // top border
		frameBuffer[0][battX + col] &= ~(1U << 6);  // bottom border
	}
	for (int row = 1; row <= 6; row++) {
		frameBuffer[0][battX]     &= ~(1U << row);  // left border
		frameBuffer[0][battX + 9] &= ~(1U << row);  // right border
	}

	// battery terminal nub (small bump on the right)
	frameBuffer[0][battX + 10] &= ~(1U << 3);
	frameBuffer[0][battX + 10] &= ~(1U << 4);

	// fill only part of the battery (partially drained - removed 2 bars from the right)
	for (int col = 1; col <= 6; col++) {
		for (int row = 2; row <= 5; row++) {
			frameBuffer[0][battX + col] &= ~(1U << row);
		}
	}
}

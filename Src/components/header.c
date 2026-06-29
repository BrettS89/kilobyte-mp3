/*
 * header.c
 *
 *  Created on: Jun 29, 2026
 *      Author: brettsodie
 */

#include <stdint.h>
#include "display.h"
#include "components.h"

void renderHeader(char *str) {
	drawStringShifted(0, 0, str, 1);

	for(int i = 0; i < 128; i++) {
		drawPixel(7, i);
	}
}

void renderHeaderInverse(char *str) {
    // fill entire header row white
    for (int i = 0; i < 128; i++) {
        frameBuffer[0][i] = 0xFF;  // all 8 pixels on = white column
    }

    // draw text in black on top of white background
    drawStringShiftedInverse(0, 2, str, 0);
}

/*
 * radio.h
 *
 *  Created on: Jun 29, 2026
 *      Author: brettsodie
 */
#include <stdint.h>
#include "state.h"
#include "display.h"
#include "components.h"

void drawRadioScreen(State *state) {
	clearFrameBuffer();

	renderHeaderInverse("Radio", state);

	drawString(1, 0, "Coming soon");

	drawFrame();
}

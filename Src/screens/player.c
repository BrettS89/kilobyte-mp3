/*
 * player.c
 *
 *  Created on: Jul 1, 2026
 *      Author: brettsodie
 */

#include <string.h>
#include <stdio.h>
#include "state.h"
#include "screens.h"
#include "display.h"
#include "components.h"
#include "sdcard.h"

void drawPlayerScreen(State *state) {
    clearFrameBuffer();

    renderHeaderInverse("Now Playing", state);

    if (state->player.artist && state->player.album && state->player.title) {
    	drawString(2, 2, state->player.artist);
    	drawString(3, 2, state->player.album);
    	drawString(4, 2, state->player.title);
    }	else {
    	drawString(3, 2, state->player.filename);
    }

    renderPlayerProgressBar(state, 6);

    setFrameBufferUpdated();
}

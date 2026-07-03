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
    printf("position: %d\r\n", (int)state->player.position);

    clearFrameBuffer();

    renderHeaderInverse("Now Playing", state);

    drawString(3, 2, state->player.filename);

    renderPlayerProgressBar(state, 6);

    setFrameBufferUpdated();
}

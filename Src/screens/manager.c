/*
 * manager.c
 *
 *  Created on: Jun 25, 2026
 *      Author: brettsodie
 */

#include <stdio.h>
#include "screens.h"
#include "state.h"

void drawScreen(State *state) {
	switch(state->navigationHistory[state->historyIndex].name) {
		case HOME:
			drawHomeScreen(state);
			break;

		case MUSIC:
			drawMusicScreen(state);
			break;

		case SONGS:
			drawSongsScreen(state);
			break;

		case RADIO:
			drawRadioScreen(state);
			break;

		default:
			break;
	}
}

void navigate(State *state, ScreenName screen) {
	if (state->historyIndex == 4) {
		return;
	}

	state->historyIndex++;

	state->navigationHistory[state->historyIndex].name = screen;
	state->navigationHistory[state->historyIndex].cursorIndex = 0;
	state->navigationHistory[state->historyIndex].scrollIndex = 0;

	drawScreen(state);
}

void onBack(State *state) {
	if (state->historyIndex == 0) {
		return;
	}

	state->historyIndex--;

	drawScreen(state);
}

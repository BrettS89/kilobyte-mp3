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

		case PLAYER:
			drawPlayerScreen(state);
			break;

		default:
			break;
	}
}

const char* screenNameToString(ScreenName name) {
    switch (name) {
        case HOME:   return "HOME";
        case MUSIC:  return "MUSIC";
        case SONGS:  return "SONGS";
        case RADIO:  return "RADIO";
        case PLAYER: return "PLAYER";
        default:     return "UNKNOWN";
    }
}

void navigate(State *state, ScreenName screen) {
	if (state->historyIndex == 4) {
		return;
	}

	printf("%s screen:\r\n", screenNameToString(screen));

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

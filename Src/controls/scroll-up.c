/*
 * scroll-up.c
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 */


#include <stdio.h>
#include "state.h"
#include "controls.h"
#include "screens.h"

static void homeScreenScrollUpInputHandler(State *state);
static void musicScreenScrollUpInputHandler(State *state);
static void songsScreenScrollUpInputHandler(State *state);
static void playerScreenScrollUpInputHandler(State *state);

void scrollUpInputHandler(State *state) {
	switch(state->navigationHistory[state->historyIndex].name) {
		case HOME:
			homeScreenScrollUpInputHandler(state);
			break;
		case MUSIC:
			musicScreenScrollUpInputHandler(state);
			break;
		case SONGS:
			songsScreenScrollUpInputHandler(state);
			break;
		case PLAYER:
			playerScreenScrollUpInputHandler(state);
			break;
		default:
			break;
	}
}

static void homeScreenScrollUpInputHandler(State *state) {
	if (state->navigationHistory[state->historyIndex].cursorIndex == 0) {
		return;
	}

	state->navigationHistory[state->historyIndex].cursorIndex--;

	drawScreen(state);
}

static void musicScreenScrollUpInputHandler(State *state) {
	if (state->navigationHistory[state->historyIndex].cursorIndex == 0) {
			return;
	}

	state->navigationHistory[state->historyIndex].cursorIndex--;

	drawScreen(state);
}

static void songsScreenScrollUpInputHandler(State *state) {
	printf("scroll up button pressed in songs screen\r\n");
}

static void playerScreenScrollUpInputHandler(State *state) {
	printf("scroll up button pressed in player screen\r\n");
}

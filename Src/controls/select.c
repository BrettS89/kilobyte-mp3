/*
 * select.c
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "state.h"
#include "controls.h"
#include "screens.h"
#include "sdcard.h"
#include "audio.h"

static void homeScreenSelectInputHandler(State *state);
static void musicScreenSelectInputHandler(State *state);
static void songsScreenSelectInputHandler(State *state);
static void playerScreenSelectInputHandler(State *state);

void selectInputHandler(State *state) {
	switch(state->navigationHistory[state->historyIndex].name) {
		case HOME:
			homeScreenSelectInputHandler(state);
			break;
		case MUSIC:
			musicScreenSelectInputHandler(state);
			break;
		case SONGS:
			songsScreenSelectInputHandler(state);
			break;
		case PLAYER:
			playerScreenSelectInputHandler(state);
			break;
		default:
			break;
	}
}

static void homeScreenSelectInputHandler(State *state) {
	uint32_t cursorIndex = state->navigationHistory[state->historyIndex].cursorIndex;

	ScrollableItem item = state->navigationHistory[state->historyIndex].items[cursorIndex];

	navigate(state, item.screenTrigger);
}

static void musicScreenSelectInputHandler(State *state) {
	uint32_t cursorIndex = state->navigationHistory[state->historyIndex].cursorIndex;

	ScrollableItem item = state->navigationHistory[state->historyIndex].items[cursorIndex];

	navigate(state, item.screenTrigger);
}

static void songsScreenSelectInputHandler(State *state) {
    uint32_t cursorIndex = state->navigationHistory[state->historyIndex].cursorIndex;
    ScrollableItem item = state->navigationHistory[state->historyIndex].items[cursorIndex];

    strncpy(state->player.filename, item.name, sizeof(state->player.filename) - 1);
    state->player.filename[sizeof(state->player.filename) - 1] = '\0';

    state->player.isPlaying = true;
    state->player.position = 0;
    state->player.duration = 0;

    navigate(state, PLAYER);

    audioRequestPlayFile(state->player.filename);
}

static void playerScreenSelectInputHandler(State *state) {
	printf("select button pressed in player screen \r\n");
}

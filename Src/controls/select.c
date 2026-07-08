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
#include "index-tracks.h"

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

	if (item.screenTrigger == SONGS) {
		requestInitialTrackLoad();
		state->trackList.cursorIndex = 0;
	}
}

static void songsScreenSelectInputHandler(State *state) {
    uint32_t windowStart = state->trackList.tracks[0].index;
    uint32_t positionInWindow = state->trackList.cursorIndex - windowStart;
    TrackRecord track = state->trackList.tracks[positionInWindow];

	playAudioFile(state, &track);

    navigate(state, PLAYER);
}

static void playerScreenSelectInputHandler(State *state) {
	printf("select button pressed in player screen \r\n");
}

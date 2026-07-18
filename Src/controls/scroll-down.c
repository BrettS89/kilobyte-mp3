/*
 * scroll-down.c
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 */


#include <stdio.h>
#include "state.h"
#include "controls.h"
#include "screens.h"
#include "window.h"

static void homeScreenScrollDownInputHandler(State *state);
static void musicScreenScrollDownInputHandler(State *state);
static void songsScreenScrollDownInputHandler(State *state);
static void playerScreenScrollDownInputHandler(State *state);
static void artistsScreenScrollDownInputHandler(State *state);

void scrollDownInputHandler(State *state) {
	switch(state->navigationHistory[state->historyIndex].name) {
		case HOME:
			homeScreenScrollDownInputHandler(state);
			break;
		case MUSIC:
			musicScreenScrollDownInputHandler(state);
			break;
		case SONGS:
			songsScreenScrollDownInputHandler(state);
			break;
		case PLAYER:
			playerScreenScrollDownInputHandler(state);
			break;
		case ARTISTS:
			artistsScreenScrollDownInputHandler(state);
		default:
			break;
	}
}

static void homeScreenScrollDownInputHandler(State *state) {
	printf("Scroll down pressed on home screen\r\n");
	uint32_t scrollableItemCount = state->navigationHistory[state->historyIndex].scrollableItemCount;

	if (state->navigationHistory[state->historyIndex].cursorIndex + 1 == scrollableItemCount) {
		return;
	}

	state->navigationHistory[state->historyIndex].cursorIndex++;

	drawScreen(state);
}

static void musicScreenScrollDownInputHandler(State *state) {
	printf("Scroll down pressed on music screen\r\n");
	uint32_t scrollableItemCount = state->navigationHistory[state->historyIndex].scrollableItemCount;

	if (state->navigationHistory[state->historyIndex].cursorIndex + 1 == scrollableItemCount) {
		return;
	}

	state->navigationHistory[state->historyIndex].cursorIndex++;

	drawScreen(state);
}

static void songsScreenScrollDownInputHandler(State *state) {
    if (state->trackList.cursorIndex + 1 == state->trackList.totalTracksInSystem) {
        return;
    }

    state->trackList.cursorIndex++;

    requestScrollDownRefillTrackWindow();

    drawScreen(state);
}

static void artistsScreenScrollDownInputHandler(State *state) {
	printf("total artists: %d\r\n", (int)state->artistList.totalArtists);

    if (state->artistList.cursorIndex + 1 == state->artistList.totalArtists) {
        return;
    }

    state->artistList.cursorIndex++;

    requestScrollDownRefillArtistWindow();

    drawScreen(state);
}

static void playerScreenScrollDownInputHandler(State *state) {
	printf("scroll down button pressed in player screen\r\n");
}

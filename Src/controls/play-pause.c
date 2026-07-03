/*
 * play-pause.c
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 */


#include <stdio.h>
#include <stdbool.h>
#include "state.h"
#include "controls.h"
#include "screens.h"
#include "audio.h"

static void homeScreenPlayPauseInputHandler(State *state);
static void musicScreenPlayPauseInputHandler(State *state);
static void songsScreenPlayPauseInputHandler(State *state);
static void playerScreenPlayPauseInputHandler(State *state);

void playPauseInputHandler(State *state) {
	switch(state->navigationHistory[state->historyIndex].name) {
		case HOME:
			homeScreenPlayPauseInputHandler(state);
			break;
		case MUSIC:
			musicScreenPlayPauseInputHandler(state);
			break;
		case SONGS:
			songsScreenPlayPauseInputHandler(state);
			break;
		case PLAYER:
			playerScreenPlayPauseInputHandler(state);
			break;
		default:
			break;
	}
}

static void homeScreenPlayPauseInputHandler(State *state) {
	if (state->player.filename[0]) {
		if (state->player.isPlaying) {
			state->player.isPlaying = false;
		}	else {
			state->player.isPlaying = true;
		}

		audioSetPlaying(state->player.isPlaying);

		drawScreen(state);
	}
}

static void musicScreenPlayPauseInputHandler(State *state) {
	if (state->player.filename[0]) {
		if (state->player.isPlaying) {
			state->player.isPlaying = false;
		}	else {
			state->player.isPlaying = true;
		}

		audioSetPlaying(state->player.isPlaying);

		drawScreen(state);
	}
}

static void songsScreenPlayPauseInputHandler(State *state) {
	if (state->player.filename[0]) {
		if (state->player.isPlaying) {
			state->player.isPlaying = false;
		}	else {
			state->player.isPlaying = true;
		}

		audioSetPlaying(state->player.isPlaying);

		drawScreen(state);
	}
}

static void playerScreenPlayPauseInputHandler(State *state) {
	if (state->player.filename[0]) {
		if (state->player.isPlaying) {
			state->player.isPlaying = false;
		}	else {
			state->player.isPlaying = true;
		}

		audioSetPlaying(state->player.isPlaying);

		drawScreen(state);
	}
}

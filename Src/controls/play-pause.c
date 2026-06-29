/*
 * play-pause.c
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 */


#include <stdio.h>
#include "state.h"
#include "controls.h"

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
	printf("play/pause button pressed in home screen\r\n");
}

static void musicScreenPlayPauseInputHandler(State *state) {
	printf("play/pause button pressed in music screen\r\n");
}

static void songsScreenPlayPauseInputHandler(State *state) {
	printf("play/pause button pressed in songs screen\r\n");
}

static void playerScreenPlayPauseInputHandler(State *state) {
	printf("play/pause button pressed in player screen\r\n");
}

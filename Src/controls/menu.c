/*
 * menu.c
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 */


#include <stdio.h>
#include "state.h"
#include "controls.h"
#include "screens.h"

static void homeScreenMenuInputHandler(State *state);
static void musicScreenMenuInputHandler(State *state);
static void songsScreenMenuInputHandler(State *state);
static void playerScreenMenuInputHandler(State *state);
static void radioScreenMenuInputHandler(State *state);
static void artistScreenMenuInputHandler(State *state);

void menuInputHandler(State *state) {
	switch(state->navigationHistory[state->historyIndex].name) {
		case HOME:
			homeScreenMenuInputHandler(state);
			break;
		case RADIO:
			radioScreenMenuInputHandler(state);
			break;
		case MUSIC:
			musicScreenMenuInputHandler(state);
			break;
		case SONGS:
			songsScreenMenuInputHandler(state);
			break;
		case PLAYER:
			playerScreenMenuInputHandler(state);
			break;
		case ARTISTS:
			artistScreenMenuInputHandler(state);
		default:
			break;
	}
}

static void homeScreenMenuInputHandler(State *state) {
	onBack(state);
}

static void radioScreenMenuInputHandler(State *state) {
	onBack(state);
}

static void musicScreenMenuInputHandler(State *state) {
	onBack(state);
}

static void songsScreenMenuInputHandler(State *state) {
	onBack(state);
}

static void playerScreenMenuInputHandler(State *state) {
	onBack(state);
}

static void artistScreenMenuInputHandler(State *state) {
	onBack(state);
}

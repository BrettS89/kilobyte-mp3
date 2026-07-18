/*
 * screens.h
 *
 *  Created on: Jun 25, 2026
 *      Author: brettsodie
 */

#ifndef SCREENS_H_
#define SCREENS_H_

#include "state.h"

void drawScreen(State *state);
void navigate(State *state, ScreenName screen);
void onBack(State *state);

void drawHomeScreen(State *state);
void drawMusicScreen(State *state);
void drawRadioScreen(State *state);
void drawSongsScreen(State *state);
void drawPlayerScreen(State *state);
void drawArtistsScreen(State *state);

#endif /* SCREENS_H_ */

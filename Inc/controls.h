/*
 * controls.h
 *
 *  Created on: Jun 25, 2026
 *      Author: brettsodie
 */

#ifndef CONTROLS_H_
#define CONTROLS_H_

#include "ff.h"
#include "state.h"

void controlsInit();
bool trackIndexInit(State *state);

// Input handlers
void controlsService(State *state);
void menuInputHandler(State *state);
void scrollDownInputHandler(State *state);
void scrollUpInputHandler(State *state);
void selectInputHandler(State *state);
void playPauseInputHandler(State *state);
void maybeRefillTrackWindow(State *state);
bool loadTrackWindow(FIL *file, TrackRecord tracks[], uint32_t startIndex, uint32_t count, uint32_t *trackCount);
void requestInitialTrackLoad();
void requestRefillTrackWindow();
void runInputRequests(State *state);
void requestScrollDownRefillTrackWindow();
void requestScrollUpRefillTrackWindow();

#endif /* CONTROLS_H_ */

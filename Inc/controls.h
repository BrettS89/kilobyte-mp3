/*
 * controls.h
 *
 *  Created on: Jun 25, 2026
 *      Author: brettsodie
 */

#ifndef CONTROLS_H_
#define CONTROLS_H_

#include "state.h"

void controlsInit();

// Input handlers
void menuInputHandler(State *state);
void scrollDownInputHandler(State *state);
void scrollUpInputHandler(State *state);
void selectInputHandler(State *state);
void playPauseInputHandler(State *state);

#endif /* CONTROLS_H_ */

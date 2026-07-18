/*
 * window.h
 *
 *  Created on: Jul 16, 2026
 *      Author: brettsodie
 */

#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdbool.h>
#include "ff.h"
#include "state.h"

bool artistIndexInit(State *state);
void runArtistInputRequests(State *state);
void requestInitialArtistLoad();
void requestScrollDownRefillArtistWindow();
void requestScrollUpRefillArtistWindow();

#endif

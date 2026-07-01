/*
 * components.h
 *
 *  Created on: Jun 29, 2026
 *      Author: brettsodie
 */

#ifndef COMPONENTS_H_
#define COMPONENTS_H_

#include "state.h"

void renderHeader(char *str);
void renderHeaderInverse(char *str, State *state);
void renderPlayerProgressBar(State *state, uint8_t row);

#endif /* COMPONENTS_H_ */

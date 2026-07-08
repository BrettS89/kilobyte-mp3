/*
 * systick.h
 *
 *  Created on: Jun 23, 2026
 *      Author: brettsodie
 */

#ifndef SYSTICK_H_
#define SYSTICK_H_

#include <stdint.h>

extern volatile uint32_t msTicks;

void systickDelayMs(int);
void systickInit(void);
uint32_t millis(void);

#endif /* SYSTICK_H_ */

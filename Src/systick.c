/*
 * systick.c
 *
 *  Created on: Jul 2, 2026
 *      Author: brettsodie
 */

#include "stm32f4xx.h"
#include <stdint.h>

#define SYSTICK_LOAD_VAL 16000
#define CTRL_ENABLE (1U << 0)
#define CTRL_CLKSRC (1U << 2)
#define CTRL_COUNTFLAG (1U << 16)

void systickDelayMs(int delay) {
	// configure systick

	// reload with n umber of clocks per millisecond
	SysTick->LOAD = SYSTICK_LOAD_VAL;

	// clear s ystick current value register
	SysTick->VAL = 0;

	// enable systick and select internal clk source
	SysTick->CTRL = CTRL_ENABLE | CTRL_CLKSRC;

	for (int i = 0; i < delay; i++) {
		// wait for count flag is set
		while((SysTick->CTRL & CTRL_COUNTFLAG) == 0) {}
	}

	SysTick->CTRL = 0;
}

volatile uint32_t msTicks = 0;

void systickInit(void) {
	SysTick->LOAD = SYSTICK_LOAD_VAL - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = CTRL_ENABLE | CTRL_CLKSRC | (1U << 1);  // enable, clk source, enable interrupt
}

void SysTick_Handler(void) {
	msTicks++;
}



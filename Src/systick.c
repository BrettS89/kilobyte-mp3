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

volatile uint32_t msTicks = 0;

void systickInit(void) {
    SysTick->LOAD = SYSTICK_LOAD_VAL - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = CTRL_ENABLE | CTRL_CLKSRC | (1U << 1);
}

void SysTick_Handler(void) {
    msTicks++;
}

uint32_t millis(void) {
    return msTicks;
}

void systickDelayMs(uint32_t delay) {
    uint32_t start = millis();
    while (millis() - start < delay) {}   // wrap-safe unsigned subtraction
}


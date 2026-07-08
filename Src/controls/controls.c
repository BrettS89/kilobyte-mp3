/*
 * controls.c
 *
 *  Created on: Jun 25, 2026
 *      Author: brettsodie
 *
 *  PIN OUT
 *
 *  SCROLL UP: PA6
 *  SCROLL DOWN: PA7
 */

#include <stdio.h>
#include <stdint.h>
#include "stm32f4xx.h"
#include "systick.h"
#include "controls.h"
#include "state.h"

#define HOLD_INITIAL_DELAY_MS  200
#define HOLD_REPEAT_MS         60     // ~14 steps/sec

volatile uint8_t  buttonEvent   = 0;       // 0 = none, else pin number
volatile uint8_t  heldPin       = 0;       // pin currently held, 0 = none
volatile uint32_t heldSinceMs   = 0;

void controlsInit() {
    // enable clocks
    RCC->AHB1ENR |= (1U << 0);       // GPIOA
    RCC->APB2ENR |= (1U << 14);      // SYSCFG
    RCC->APB1ENR |= (1U << 0);       // TIM2

    // set pins 5-9 to input mode
    GPIOA->MODER &= ~(0x3FFU << 10);

    // set pull-up for pins 5-9
    GPIOA->PUPDR &= ~(0x3FFU << 10);
    GPIOA->PUPDR |=  (0x155U << 10);

    // connect pins 5-9 to EXTI lines via SYSCFG
    // all port A so write 0000 for each
    SYSCFG->EXTICR[1] &= ~(0xFFFFU);  // EXTI5, EXTI6, EXTI7
    SYSCFG->EXTICR[2] &= ~(0x00FFU);  // EXTI8, EXTI9

    // unmask EXTI lines 5-9
    EXTI->IMR |= (0x1FU << 5);

    // falling edge trigger for pins 5-9
    EXTI->FTSR |= (0x1FU << 5);

    // enable EXTI interrupts in NVIC
    NVIC_EnableIRQ(EXTI9_5_IRQn);

    // configure TIM2 for 20ms debounce
    TIM2->PSC = 16 - 1;
    TIM2->ARR = 20000 - 1;
    TIM2->DIER |= (1U << 0);
    NVIC_EnableIRQ(TIM2_IRQn);
}

static uint32_t pendingPin = 0;

void dispatchButton(State *state, uint8_t pin) {
	switch (pendingPin) {
		case 5:   // menu/back
			menuInputHandler(state);
			break;
		case 6:   // scroll up
			scrollUpInputHandler(state);
			break;
		case 7:   // scroll down
			scrollDownInputHandler(state);
			break;
		case 8:   // select
			selectInputHandler(state);
			break;
		case 9:   // play/pause
			playPauseInputHandler(state);
			break;
	}
}

void EXTI9_5_IRQHandler(void) {
    // if timer already running ignore this press
    if (TIM2->CR1 & (1U << 0)) {
        EXTI->PR |= (0x1FU << 5);
        return;
    }

    if (EXTI->PR & (1U << 5)) { pendingPin = 5;  EXTI->PR |= (1U << 5);  }
    if (EXTI->PR & (1U << 6)) { pendingPin = 6;  EXTI->PR |= (1U << 6);  }
    if (EXTI->PR & (1U << 7)) { pendingPin = 7;  EXTI->PR |= (1U << 7);  }
    if (EXTI->PR & (1U << 8)) { pendingPin = 8;  EXTI->PR |= (1U << 8);  }
    if (EXTI->PR & (1U << 9)) { pendingPin = 9;  EXTI->PR |= (1U << 9);  }

    // start debounce timer
    TIM2->CNT = 0;
    TIM2->CR1 |= (1U << 0);
}

void controlsService(State *state) {
    // 1. Dispatch any confirmed press from the ISR
    if (buttonEvent) {
        uint8_t pin = buttonEvent;
        buttonEvent = 0;
        dispatchButton(state, pin);          // the switch from your TIM2 ISR, moved here
    }

    // 2. Hold tracking
    if (heldPin == 0) return;

    if (GPIOA->IDR & (1U << heldPin)) {      // pin high = released
        heldPin = 0;
        return;
    }

    // Only the scroll buttons repeat
    if (heldPin != 6 && heldPin != 7) return;

    uint32_t now  = millis();
    uint32_t held = now - heldSinceMs;

    if (held < HOLD_INITIAL_DELAY_MS) return;

    static uint32_t lastRepeatMs = 0;
    if (now - lastRepeatMs >= HOLD_REPEAT_MS) {
        lastRepeatMs = now;
        dispatchButton(state, heldPin);      // same handler, re-fired
    }
}

void TIM2_IRQHandler(void) {
    // clear flag and stop timer
    TIM2->SR  &= ~(1U << 0);
    TIM2->CR1 &= ~(1U << 0);

    if (!(GPIOA->IDR & (1U << pendingPin))) {
		buttonEvent = pendingPin;          // main loop dispatches this
		heldPin     = pendingPin;          // arm hold tracking
		heldSinceMs = millis();
	}
}

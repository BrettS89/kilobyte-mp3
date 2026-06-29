/*
 * controls.c
 *
 *  Created on: Jun 25, 2026
 *      Author: brettsodie
 */
#include <stdio.h>
#include "stm32f4xx.h"
#include "controls.h"
#include "state.h"

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

void TIM2_IRQHandler(void) {
    // clear flag and stop timer
    TIM2->SR  &= ~(1U << 0);
    TIM2->CR1 &= ~(1U << 0);

    // check if button still pressed after 20ms
    if (!(GPIOA->IDR & (1U << pendingPin))) {
        switch (pendingPin) {
            case 5:   // menu/back
                menuInputHandler(&state);
                break;
            case 6:   // scroll up
                scrollUpInputHandler(&state);
                break;
            case 7:   // scroll down
                scrollDownInputHandler(&state);
                break;
            case 8:   // select
                selectInputHandler(&state);
                break;
            case 9:   // play/pause
                playPauseInputHandler(&state);
                break;
        }
    }
}

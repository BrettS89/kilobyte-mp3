/*
 * sd.c
 *
 *  Created on: Jun 30, 2026
 *      Author: brettsodie
 *
 *  Pinout
 *
 *  PB3: SCK
 *  PB4: MISO
 *  PB5: MOSI
 *  PA3: CS
 */

#include <stdint.h>
#include <stdio.h>
#include "stm32f4xx.h"
#include "spi1.h"

void spi1Init(void) {
    // enable clocks
    RCC->AHB1ENR |= (1U << 0) | (1U << 1);  // GPIOA, GPIOB
    RCC->APB2ENR |= (1U << 12);              // SPI1

    // PB3, PB4, PB5 to alternate function mode
    GPIOB->MODER &= ~((3U << 6) | (3U << 8) | (3U << 10));
    GPIOB->MODER |=  ((2U << 6) | (2U << 8) | (2U << 10));

    // set AF5 for PB3, PB4, PB5
    GPIOB->AFR[0] &= ~((0xFU << 12) | (0xFU << 16) | (0xFU << 20));
    GPIOB->AFR[0] |=  ((5U << 12) | (5U << 16) | (5U << 20));

    GPIOB->PUPDR &= ~(3U << 8);   // clear PB4 pull bits
    GPIOB->PUPDR |=  (1U << 8);   // pull-up on PB4 (MISO)

    // PA4 as plain GPIO output for CS
	GPIOA->MODER &= ~(3U << 8);
	GPIOA->MODER |=  (1U << 8);

    // CS idle high (not selected)
	GPIOA->BSRR = (1U << 4);

    // configure SPI1
    SPI1->CR1 = 0;  // reset
    SPI1->CR1 |= (1U << 2);   // master mode
    SPI1->CR1 |= (7U << 3);   // baud rate prescaler /256 (slow for init)
    SPI1->CR1 &= ~(1U << 1);  // CPOL = 0
    SPI1->CR1 &= ~(1U << 0);  // CPHA = 0
    SPI1->CR1 |= (1U << 9);   // SSM software slave management
    SPI1->CR1 |= (1U << 8);   // SSI internal slave select
    SPI1->CR1 |= (1U << 6);   // SPE - enable SPI1
}

uint8_t spi1Transfer(uint8_t data) {
    while (!(SPI1->SR & (1U << 1)));  // wait for TXE
    *(volatile uint8_t*)&SPI1->DR = data;
    while (!(SPI1->SR & (1U << 0)));  // wait for RXNE
    return *(volatile uint8_t*)&SPI1->DR;
}

void spi1Select(void) {
    GPIOA->BSRR = (1U << (4 + 16));  // CS low
}

void spi1Deselect(void) {
    GPIOA->BSRR = (1U << 4);  // CS high
    spi1Transfer(0xFF);       // extra clock to release card
}

void spi1SetSpeedFast(void) {
    /*
     * Wait until the current SPI transaction is completely finished
     * before changing the baud-rate divider.
     */
    while ((SPI1->SR & SPI_SR_TXE) == 0U) {
    }

    while ((SPI1->SR & SPI_SR_BSY) != 0U) {
    }

    SPI1->CR1 &= ~SPI_CR1_SPE;

    /*
     * BR = 000 means divide the APB2 peripheral clock by 2.
     *
     * If PCLK2 is 16 MHz:
     *
     *     SPI1 clock = 16 MHz / 2 = 8 MHz
     */
    SPI1->CR1 &= ~SPI_CR1_BR;

    SPI1->CR1 |= SPI_CR1_SPE;
}

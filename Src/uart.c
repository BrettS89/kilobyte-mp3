/*
 * uart.c
 *
 *  Created on: Jun 21, 2026
 *      Author: brettsodie
 */
#include "uart.h"

#include <stdint.h>
#include <stdio.h>
#include "stm32f4xx.h"

#define GIPOAEN (1U<<0)

void uart2Write(int ch) {
    /* Wait until transmit data register is empty */
    while(!(USART2->SR & (1U << 7)));

    /* Write character to data register */
    USART2->DR = (ch & 0xFF);
}

char usart2Read() {
	while(!(USART2->SR & (1U << 5)));

	char c = USART2->DR;

	return c;
}

int __io_putchar(int ch) {
	uart2Write(ch);
	return ch;
}

uint16_t computeBrrValue(uint32_t periphClk, uint32_t baudRate) {
	return ((periphClk + (baudRate / 2U)) / baudRate);
}


void uartSetBaudRate(USART_TypeDef *USARTx, uint32_t periphClock, uint32_t baudRate) {
	USARTx->BRR = computeBrrValue(periphClock, baudRate);
}

void uartTxinit() {
	// enable the ports
	RCC->AHB1ENR |= (1U<<0);
	RCC->APB1ENR |= (1U<<17);

	// set the mode to be alternate function
	GPIOA->MODER |= (1U<<5);
	GPIOA->MODER &=~ (1U<<4);


	GPIOA->AFR[0] |= (1U<<8);
	GPIOA->AFR[0] |= (1U<<9);
	GPIOA->AFR[0] |= (1U<<10);
	GPIOA->AFR[0] &=~ (1U<<11);


	uartSetBaudRate(USART2, 16000000, 115200);

	// set to transmission mode in control register 1
	USART2->CR1 |= (1U<<3);
	USART2->CR1 |= (1U<<13);
}

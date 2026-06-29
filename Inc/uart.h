/*
 * uart.h
 *
 *  Created on: Jun 21, 2026
 *      Author: brettsodie
 */

#include <stdint.h>

#ifndef UART_H_
#define UART_H_

void uart2Write(int);
void uartTxinit();
char usart2Read();

#endif /* UART_H_ */

/*
 * sd.h
 *
 *  Created on: Jun 30, 2026
 *      Author: brettsodie
 */

#ifndef SPI1_H_
#define SPI1_H_

#include <stdint.h>

void spi1Init(void);
uint8_t spi1Transfer(uint8_t data);
void spi1Select(void);
void spi1Deselect(void);
void spi1SetSpeedFast(void);

#endif /* SPI1_H_ */

/*
 * display.h
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 */

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdint.h>
#include <stdbool.h>

extern uint8_t frameBuffer[8][128];

void oledInit(void);
void oledSendData(uint8_t data);
void oledClear(void);
void drawStringShifted(uint8_t row, uint8_t col, const char *str, uint8_t shiftUp);
void drawStringShiftedInverse(uint8_t row, uint8_t col, const char *str, uint8_t shiftUp);
void drawString(uint8_t page, uint8_t col, const char *str);
void fontInit(void);
void setFrameBufferUpdated();
void drawFrame();
void clearFrameBuffer(void);
void drawPixel(uint8_t y, uint8_t x);
void drawRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
void fillRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
void dma1Stream6Init(void);

#endif /* DISPLAY_H_ */

/*
 * display.c
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 *
 *  Pinout
 *  PB8 SCL
 *  PB9 SDA
 */


#include "stm32f4xx.h"
#include <string.h>
#include "font.h"

void i2cSendData(uint8_t data);
void oledSetCursor(uint8_t page, uint8_t col);

uint8_t frameBuffer[8][128];

void i2cInit() {
	// enable clock access for gpiob
	RCC->AHB1ENR |= (1U << 1);

	// enable clock access for i2c1
	RCC->APB1ENR |= (1U << 21);

	// set pb8 and pb9 to alternate function mode
	GPIOB->MODER |= (1U << 17);
	GPIOB->MODER &= ~(1U << 16);

	GPIOB->MODER |= (1U << 19);
	GPIOB->MODER &= ~(1U << 18);

	// set pull up for pb8 and pb9
	GPIOB->PUPDR &= ~(0xFU << 16);  // clear bits 19:16
	GPIOB->PUPDR |=  (0x5U << 16);  // pull-up for both PB8 and PB9

	// set alternate function for pb8
	GPIOB->AFR[1] &= ~(1U << 0);
	GPIOB->AFR[1] &= ~(1U << 1);
	GPIOB->AFR[1] |= (1U << 2);
	GPIOB->AFR[1] &= ~(1U << 3);

	// set alternate function for pb9
	GPIOB->AFR[1] &= ~(1U << 4);
	GPIOB->AFR[1] &= ~(1U << 5);
	GPIOB->AFR[1] |= (1U << 6);
	GPIOB->AFR[1] &= ~(1U << 7);

	// set open drain
	GPIOB->OTYPER |= (1U << 8);  // PB8 open drain
	GPIOB->OTYPER |= (1U << 9);  // PB9 open drain

	// reset i2c1
	I2C1->CR1 |= (1U << 15);
	I2C1->CR1 &= ~(1U << 15);  // clear reset — you're missing this

	// set clock speed
	I2C1->CR2 &= ~(1U << 0);
	I2C1->CR2 &= ~(1U << 1);
	I2C1->CR2 &= ~(1U << 2);
	I2C1->CR2 &= ~(1U << 3);
	I2C1->CR2 |= (1U << 4);
	I2C1->CR2 &= ~(1U << 5);

	I2C1->CCR = 80;  // standard mode 100kHz, F/S=0, DUTY=0

	I2C1->TRISE  = 17;          // max rise time

	// enable I2C1
	I2C1->CR1 |= (1U << 0);
}

// generate start condition
void i2cStart(void) {
    I2C1->CR1 |= (1U << 8);  // START bit
    while (!(I2C1->SR1 & (1U << 0)));  // wait for SB flag
}

// send address
void i2cSendAddress(uint8_t address) {
    I2C1->DR = address << 1;  // shift left, bit 0 = 0 for write

    while (!(I2C1->SR1 & (1U << 1)));  // wait for ADDR flag
    // must read SR1 then SR2 to clear ADDR flag
    (void)I2C1->SR1;
    (void)I2C1->SR2;
}

// send one byte of data
void i2cSendData(uint8_t data) {
    while (!(I2C1->SR1 & (1U << 7)));  // wait for TXE
    I2C1->DR = data;
}

// generate stop condition
void i2cStop(void) {
    while (!(I2C1->SR1 & (1U << 2)));  // wait for BTF
    I2C1->CR1 |= (1U << 9);  // STOP bit
}

void oledSendCommand(uint8_t command) {
    i2cStart();
    i2cSendAddress(0x3C);  // OLED address
    i2cSendData(0x00);     // control byte - command
    i2cSendData(command);  // the actual command
    i2cStop();
}

void oledSendData(uint8_t data) {
    i2cStart();
    i2cSendAddress(0x3C);  // OLED address
    i2cSendData(0x40);     // control byte - data
    i2cSendData(data);     // the actual data
    i2cStop();
}

void oledSetCursor(uint8_t page, uint8_t col) {
    oledSendCommand(0xB0 + page);              // set page address
    oledSendCommand(0x00 + (col & 0x0F));      // set low column
    oledSendCommand(0x10 + (col >> 4));        // set high column
}

void drawPixel(uint8_t y, uint8_t x) {
    if (x >= 128 || y >= 64) return;
    uint8_t page = y / 8;
    uint8_t bit  = y % 8;

    frameBuffer[page][x] |=  (1U << bit);
}

void drawChar(uint8_t row, uint8_t col, char c) {
    if (col + 8 >= 128) return;  // was checking col + 8 >= 127, off by one

    for (int i = 0; i < 8; i++) {
        uint8_t characterByte = font[(uint8_t)c][i];
        frameBuffer[row][col + i] = characterByte;
    }
}

void drawCharShifted(uint8_t row, uint8_t col, char c, uint8_t shiftUp) {
    if (col + 8 >= 128) return;

    for (int i = 0; i < 8; i++) {
        uint8_t byte = font[(uint8_t)c][i];
        byte = byte >> shiftUp;  // shift bits toward top
        frameBuffer[row][col + i] = byte;
    }
}

void drawStringShifted(uint8_t row, uint8_t col, const char *str, uint8_t shiftUp) {
	while(*str) {
		drawCharShifted(row, col, *str, shiftUp);
		col += 6;
		str++;
	}
}

void drawCharShiftedInverse(uint8_t row, uint8_t col, char c, uint8_t shiftUp) {
    if (col + 8 >= 128) return;

    for (int i = 0; i < 8; i++) {
        uint8_t byte = font[(uint8_t)c][i];
        byte = byte >> shiftUp;
        byte = ~byte;  // invert — white pixels become black, black become white
        frameBuffer[row][col + i] = byte;
    }
}

void drawStringShiftedInverse(uint8_t row, uint8_t col, const char *str, uint8_t shiftUp) {
    while (*str) {
        drawCharShiftedInverse(row, col, *str, shiftUp);
        col += 6;
        str++;
    }
}

void drawString(uint8_t row, uint8_t col, const char *str) {
    while (*str) {
        if (col >= 128) return;  // stop if we've run off screen

        if (*str == ' ') {
            col += 3;
        } else {
            if (col + 8 >= 128) return;  // not enough room for next char
            drawChar(row, col, *str);
            col += 6;
        }

        str++;
    }
}

void drawHLine(uint8_t x, uint8_t y, uint8_t width) {
    for (uint8_t i = 0; i < width; i++) {
        drawPixel(y, x + i);
    }
}

void drawVLine(uint8_t x, uint8_t y, uint8_t height) {
    for (uint8_t i = 0; i < height; i++) {
        drawPixel(y + i, x);
    }
}

void drawRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
    drawHLine(x, y, width);                  // top
    drawHLine(x, y + height - 1, width);     // bottom
    drawVLine(x, y, height);                 // left
    drawVLine(x + width - 1, y, height);     // right
}

void fillRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
    for (uint8_t row = 0; row < height; row++) {
        drawHLine(x, y + row, width);
    }
}

void oledClear(void) {
    for (uint8_t page = 0; page < 8; page++) {
        oledSetCursor(page, 0);
        i2cStart();
        i2cSendAddress(0x3C);
        i2cSendData(0x40);  // data mode
        for (uint8_t col = 0; col < 128; col++) {
            i2cSendData(0x00);  // send zeros to turn off all pixels
        }
        i2cStop();
    }
}

void oledInit(void) {
	i2cInit();
    oledSendCommand(0xAE);  // display off
    oledSendCommand(0x20);  // memory addressing mode
    oledSendCommand(0x00);  // horizontal addressing
    oledSendCommand(0xA1);  // segment remap
    oledSendCommand(0xC8);  // COM scan direction
    oledSendCommand(0x81);  // contrast
    oledSendCommand(0x7F);  // 50% instead of 0xFF
    oledSendCommand(0x8D);  // charge pump
    oledSendCommand(0x14);  // enable charge pump
    oledSendCommand(0xAF);  // display on
}

void drawFrame() {
	oledSetCursor(0, 0);

    i2cStart();
    i2cSendAddress(0x3C);  // OLED address
    i2cSendData(0x40);     // control byte - data

	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 128; j++) {
		    i2cSendData(frameBuffer[i][j]);     // the actual data
		}
	}

    i2cStop();
}

void clearFrameBuffer(void) {
    memset(frameBuffer, 0, sizeof(frameBuffer));
}


/*
 * display.c
 *
 *  Created on: Jun 26, 2026
 *      Author: brettsodie
 *
 *  Pinout
 *  PB8 SCL
 *  PB9 SDA
 *
 *  I2C1 @ 400kHz fast mode (PCLK1 = 16MHz assumed — if the clock tree
 *  ever changes, CCR and TRISE below must be recomputed).
 *
 *  Display updates are DMA-driven: drawFrame() snapshots frameBuffer
 *  into dmaSendBuffer and starts a 1024-byte DMA stream. The DMA ISR
 *  issues STOP and clears the busy flag. drawFrame() is a no-op while
 *  a transfer is in flight or if nothing changed.
 */

#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "font.h"

// Bounded wait for I2C status flags. A hung bus (flaky wire, confused
// slave) otherwise freezes the whole player inside a while loop.
// ~few thousand iterations is far longer than any legitimate wait at
// 400kHz, so a timeout here always means something is actually wrong.
#define I2C_FLAG_TIMEOUT  5000U

void i2cSendData(uint8_t data);
void i2cStop(void);

uint8_t frameBuffer[8][128];
uint8_t dmaSendBuffer[8][128];

bool frameBufferUpdated = false;

static volatile bool dmaTransferInProgress = false;

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------

void dma1Stream6Init(void) {
    RCC->AHB1ENR |= (1U << 21);              // DMA1 clock

    DMA1_Stream6->CR &= ~(1U << 0);          // disable stream
    while (DMA1_Stream6->CR & (1U << 0)) {}

    DMA1_Stream6->PAR = (uint32_t)&I2C1->DR;

    DMA1_Stream6->CR &= ~(1U << 27);         // channel select bits: channel 1
    DMA1_Stream6->CR &= ~(1U << 26);
    DMA1_Stream6->CR |= (1U << 25);

    DMA1_Stream6->CR &= ~(1U << 7);          // peripheral addr fixed
    DMA1_Stream6->CR |= (1U << 6);           // memory-to-peripheral

    DMA1_Stream6->CR |= (1U << 10);          // memory increment
    DMA1_Stream6->CR |= (1U << 4);           // transfer-complete interrupt

    DMA1_Stream6->FCR = 0;                   // direct mode

    I2C1->CR2 |= (1U << 11);                 // I2C DMA requests enable

    NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

void dma1Stream6Start(uint32_t source, uint32_t len) {
    // clear all stream 6 event flags
    DMA1->HIFCR |= (1U << 16) | (1U << 18) | (1U << 19) | (1U << 20) | (1U << 21);

    DMA1_Stream6->CR &= ~(1U << 0);
    while (DMA1_Stream6->CR & (1U << 0)) {}

    DMA1_Stream6->M0AR = source;
    DMA1_Stream6->NDTR = len;

    DMA1_Stream6->CR |= (1U << 0);           // go
}

void DMA1_Stream6_IRQHandler(void) {
    if (DMA1->HISR & (1U << 21)) {           // transfer complete
        DMA1->HIFCR |= (1U << 21);
        i2cStop();                           // bounded wait inside — see i2cStop
        dmaTransferInProgress = false;
    }
}

// ---------------------------------------------------------------------------
// I2C low level
// ---------------------------------------------------------------------------

void i2cInit(void) {
    // enable clock access for gpiob
    RCC->AHB1ENR |= (1U << 1);

    // enable clock access for i2c1
    RCC->APB1ENR |= (1U << 21);

    // set pb8 and pb9 to alternate function mode
    GPIOB->MODER |= (1U << 17);
    GPIOB->MODER &= ~(1U << 16);

    GPIOB->MODER |= (1U << 19);
    GPIOB->MODER &= ~(1U << 18);

    // Internal pull-ups (~40k). Too weak to matter at 400kHz — the
    // module's onboard pull-ups do the real work. Harmless to leave on.
    GPIOB->PUPDR &= ~(0xFU << 16);
    GPIOB->PUPDR |=  (0x5U << 16);

    // alternate function AF4 (I2C1) for pb8
    GPIOB->AFR[1] &= ~(1U << 0);
    GPIOB->AFR[1] &= ~(1U << 1);
    GPIOB->AFR[1] |= (1U << 2);
    GPIOB->AFR[1] &= ~(1U << 3);

    // alternate function AF4 (I2C1) for pb9
    GPIOB->AFR[1] &= ~(1U << 4);
    GPIOB->AFR[1] &= ~(1U << 5);
    GPIOB->AFR[1] |= (1U << 6);
    GPIOB->AFR[1] &= ~(1U << 7);

    // open drain — I2C requires it
    GPIOB->OTYPER |= (1U << 8);
    GPIOB->OTYPER |= (1U << 9);

    // software reset pulse: set then CLEAR, or the peripheral stays
    // held in reset and nothing after this works
    I2C1->CR1 |= (1U << 15);
    I2C1->CR1 &= ~(1U << 15);

    // peripheral clock frequency = 16 (MHz), CR2 FREQ[5:0]
    I2C1->CR2 &= ~(1U << 0);
    I2C1->CR2 &= ~(1U << 1);
    I2C1->CR2 &= ~(1U << 2);
    I2C1->CR2 &= ~(1U << 3);
    I2C1->CR2 |= (1U << 4);
    I2C1->CR2 &= ~(1U << 5);

    // Fast mode 400kHz, DUTY=0 (Tlow:Thigh = 2:1)
    //   SCL = PCLK1 / (3 * CCR) = 16MHz / (3 * 14) = ~381kHz
    // NOTE: DUTY (bit 14) must stay 0 — with DUTY=1 the divider becomes
    // 25*CCR and the same CCR value yields ~46kHz, slower than standard
    // mode. Bit 15 is F/S mode select.
    I2C1->CCR = (1U << 15) | 14;

    // Fast mode max rise time 300ns: (16 * 300 / 1000) + 1 = 5.8 -> 5
    I2C1->TRISE = 5;

    // enable I2C1 (CCR/TRISE writable only while PE=0, so this comes last)
    I2C1->CR1 |= (1U << 0);
}

// generate start condition
void i2cStart(void) {
    uint32_t timeout = I2C_FLAG_TIMEOUT;
    I2C1->CR1 |= (1U << 8);                          // START
    while (!(I2C1->SR1 & (1U << 0)) && --timeout) {} // wait SB
}

// send address (write direction)
void i2cSendAddress(uint8_t address) {
    uint32_t timeout = I2C_FLAG_TIMEOUT;
    I2C1->DR = address << 1;                         // bit 0 = 0 -> write

    while (!(I2C1->SR1 & (1U << 1)) && --timeout) {} // wait ADDR
    // read SR1 then SR2 to clear ADDR
    (void)I2C1->SR1;
    (void)I2C1->SR2;
}

// send one byte of data
void i2cSendData(uint8_t data) {
    uint32_t timeout = I2C_FLAG_TIMEOUT;
    while (!(I2C1->SR1 & (1U << 7)) && --timeout) {} // wait TXE
    I2C1->DR = data;
}

// generate stop condition
void i2cStop(void) {
    uint32_t timeout = I2C_FLAG_TIMEOUT;
    while (!(I2C1->SR1 & (1U << 2)) && --timeout) {} // wait BTF
    I2C1->CR1 |= (1U << 9);                          // STOP regardless —
                                                     // recovers the bus even
                                                     // if BTF never came
}

// ---------------------------------------------------------------------------
// OLED commands
// ---------------------------------------------------------------------------

void oledSendCommand(uint8_t command) {
    i2cStart();
    i2cSendAddress(0x3C);
    i2cSendData(0x00);     // control byte: command
    i2cSendData(command);
    i2cStop();
}

// Set the horizontal-addressing window and reset the RAM pointer to its
// start. This is the correct pointer reset for horizontal mode (0x20/0x00,
// set in oledInit); the page-mode commands (0xB0+page) that used to live
// here only nominally apply to page addressing mode — most panels honored
// them anyway, but this is the by-the-datasheet version.
void oledResetWindow(void) {
    oledSendCommand(0x21);  // column address
    oledSendCommand(0);     //   start 0
    oledSendCommand(127);   //   end 127
    oledSendCommand(0x22);  // page address
    oledSendCommand(0);     //   start 0
    oledSendCommand(7);     //   end 7
}

// ---------------------------------------------------------------------------
// Frame buffer drawing (no bus traffic — pure memory writes)
// ---------------------------------------------------------------------------

void drawPixel(uint8_t y, uint8_t x) {
    if (x >= 128 || y >= 64) return;
    uint8_t page = y / 8;
    uint8_t bit  = y % 8;

    frameBuffer[page][x] |= (1U << bit);
}

void drawChar(uint8_t row, uint8_t col, char c) {
    if (col + 8 >= 128) return;

    for (int i = 0; i < 8; i++) {
        frameBuffer[row][col + i] = font[(uint8_t)c][i];
    }
}

void drawCharShifted(uint8_t row, uint8_t col, char c, uint8_t shiftUp) {
    if (col + 8 >= 128) return;

    for (int i = 0; i < 8; i++) {
        uint8_t byte = font[(uint8_t)c][i];
        byte = byte >> shiftUp;
        frameBuffer[row][col + i] = byte;
    }
}

void drawStringShifted(uint8_t row, uint8_t col, const char *str, uint8_t shiftUp) {
    while (*str) {
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
        byte = ~byte;
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
        if (col >= 128) return;

        if (*str == ' ') {
            col += 3;
        } else {
            if (col + 8 >= 128) return;
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
    drawHLine(x, y, width);
    drawHLine(x, y + height - 1, width);
    drawVLine(x, y, height);
    drawVLine(x + width - 1, y, height);
}

void fillRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
    for (uint8_t row = 0; row < height; row++) {
        drawHLine(x, y + row, width);
    }
}

// ---------------------------------------------------------------------------
// Display update
// ---------------------------------------------------------------------------

// Blank the panel directly over the bus. Init-time only — during normal
// operation, clear frameBuffer and let drawFrame() do the work.
void oledClear(void) {
    oledResetWindow();
    i2cStart();
    i2cSendAddress(0x3C);
    i2cSendData(0x40);                  // control byte: data
    for (uint16_t i = 0; i < 1024; i++) {
        i2cSendData(0x00);
    }
    i2cStop();
}

void oledInit(void) {
    i2cInit();
    oledSendCommand(0xAE);  // display off
    oledSendCommand(0x20);  // memory addressing mode...
    oledSendCommand(0x00);  //   ...horizontal
    oledSendCommand(0xA1);  // segment remap
    oledSendCommand(0xC8);  // COM scan direction
    oledSendCommand(0x81);  // contrast...
    oledSendCommand(0x7F);  //   ...50%
    oledSendCommand(0x8D);  // charge pump...
    oledSendCommand(0x14);  //   ...enable
    oledSendCommand(0xAF);  // display on
}

void setFrameBufferUpdated(void) {
    frameBufferUpdated = true;
}

void drawFrame(void) {
    if (!frameBufferUpdated) return;
    if (dmaTransferInProgress) return;

    frameBufferUpdated = false;

    // Snapshot so the UI can keep drawing into frameBuffer while DMA
    // drains the previous frame — this is the tear prevention.
    memcpy(dmaSendBuffer, frameBuffer, sizeof(dmaSendBuffer));

    dmaTransferInProgress = true;

    oledResetWindow();

    i2cStart();
    i2cSendAddress(0x3C);
    i2cSendData(0x40);      // control byte: data — DMA supplies the rest

    dma1Stream6Start((uint32_t)dmaSendBuffer, 1024);
}

void clearFrameBuffer(void) {
    memset(frameBuffer, 0, sizeof(frameBuffer));
}

/*
 * spi2.c
 *
 *  Created on: Jul 1, 2026
 *      Author: brettsodie
 *
 *      PINOUT:
 *      	PB13 CLK
 *      	PB14 MISO
 *      	PB15 MOSI
 *      	PB1  CS
 *      	PB2	 XDCS
 *      	PB11 DREQ
 *      	PB12 RST
 */


#include "audio.h"

void spi2Init(void) {
    // enable clocks
    RCC->AHB1ENR |= (1U << 1);   // GPIOB
    RCC->APB1ENR |= (1U << 14);  // SPI2

    // PB13, PB14, PB15 to alternate function mode
    GPIOB->MODER &= ~((3U << 26) | (3U << 28) | (3U << 30));
    GPIOB->MODER |=  ((2U << 26) | (2U << 28) | (2U << 30));

    // set AF5 for PB13, PB14, PB15
    GPIOB->AFR[1] &= ~((0xFU << 20) | (0xFU << 24) | (0xFU << 28));
    GPIOB->AFR[1] |=  ((5U << 20) | (5U << 24) | (5U << 28));

    GPIOB->PUPDR &= ~(3U << 28);   // clear PB14 pull bits
    GPIOB->PUPDR |=  (1U << 28);   // pull-up on PB14 (MISO)

    // configure SPI2
    SPI2->CR1 = 0;  // reset
    SPI2->CR1 |= (1U << 2);   // master mode
    SPI2->CR1 |= (7U << 3);   // baud rate prescaler /256 (slow for init)
    SPI2->CR1 &= ~(1U << 1);  // CPOL = 0
    SPI2->CR1 &= ~(1U << 0);  // CPHA = 0
    SPI2->CR1 |= (1U << 9);   // SSM software slave management
    SPI2->CR1 |= (1U << 8);   // SSI internal slave select
    SPI2->CR1 |= (1U << 6);   // SPE - enable SPI2
}

void vs1053CsInit(void) {
    RCC->AHB1ENR |= (1U << 1);

    // PB1 as output (CS)
    GPIOB->MODER &= ~(3U << 2);
    GPIOB->MODER |=  (1U << 2);

    // PB2 as output (XDCS)
    GPIOB->MODER &= ~(3U << 4);
    GPIOB->MODER |=  (1U << 4);

    // PB11 as input (DREQ)
    GPIOB->MODER &= ~(3U << 22);

    // PB12 as output (RST)
    GPIOB->MODER &= ~(3U << 24);
    GPIOB->MODER |=  (1U << 24);

    // idle states — set once
    GPIOB->BSRR = (1U << 1);   // CS high
    GPIOB->BSRR = (1U << 2);   // XDCS high
    GPIOB->BSRR = (1U << 12);  // RST high (not in reset)
}

void vs1053Reset(void) {
    GPIOB->BSRR = (1U << (12 + 16));  // RST low
    for (volatile int i = 0; i < 100000; i++);
    GPIOB->BSRR = (1U << 12);          // RST high
    for (volatile int i = 0; i < 100000; i++);
}

void vs1053WaitForDREQ(void) {
    while (!(GPIOB->IDR & (1U << 11)));  // wait until DREQ goes high
}

void vs1053WriteRegister(uint8_t address, uint16_t data) {
    vs1053WaitForDREQ();

    GPIOB->BSRR = (1U << (1 + 16));  // CS low

    spi2Transfer(0x02);              // write instruction
    spi2Transfer(address);
    spi2Transfer((data >> 8) & 0xFF);
    spi2Transfer(data & 0xFF);

    GPIOB->BSRR = (1U << 1);         // CS high
}

uint16_t vs1053ReadRegister(uint8_t address) {
    vs1053WaitForDREQ();

    GPIOB->BSRR = (1U << (1 + 16));  // CS low

    spi2Transfer(0x03);              // read instruction
    spi2Transfer(address);
    uint8_t high = spi2Transfer(0xFF);
    uint8_t low  = spi2Transfer(0xFF);

    GPIOB->BSRR = (1U << 1);         // CS high

    return (high << 8) | low;
}

void vs1053SendData(uint8_t *data, uint16_t length) {
    GPIOB->BSRR = (1U << (2 + 16));  // XDCS low

    for (uint16_t i = 0; i < length; i++) {
        vs1053WaitForDREQ();
        spi2Transfer(data[i]);
    }

    GPIOB->BSRR = (1U << 2);         // XDCS high
}

void vs1053InitChip(void) {
    vs1053Reset();

    // set MP3 mode explicitly (fixes the floating pin issue)
    vs1053WriteRegister(0x00, 0x0800 | 0x0004);  // SCI_MODE: SM_SDINEW | SM_RESET

    for (volatile int i = 0; i < 100000; i++);

    // set initial volume (0x00 = loudest, 0xFE = quietest per channel)
    vs1053WriteRegister(0x0B, 0x2020);  // SCI_VOL, moderate volume both channels

    // increase SPI2 speed now that init is done
    SPI2->CR1 &= ~(1U << 6);
    SPI2->CR1 &= ~(7U << 3);
    SPI2->CR1 |= (2U << 3);  // faster prescaler for streaming
    SPI2->CR1 |= (1U << 6);
}

void audioInit() {
	spi2Init();
	vs1053CsInit();
}

void vs1053PlayFile(const char *filename) {
    FIL file;
    uint8_t buffer[32];
    UINT bytesRead;

    if (f_open(&file, filename, FA_READ) != FR_OK) {
        printf("Failed to open file\r\n");
        return;
    }

    while (1) {
        f_read(&file, buffer, 32, &bytesRead);
        if (bytesRead == 0) break;

        vs1053SendData(buffer, bytesRead);
    }

    f_close(&file);
}


/*
 * sdcard.c
 *
 *  Created on: Jun 30, 2026
 *      Author: brettsodie
 */

#include <stdint.h>
#include <stdio.h>
#include "spi1.h"
#include "sdcard.h"

#define CMD0    0
#define CMD8    8
#define CMD16   16
#define CMD17   17
#define CMD24   24
#define CMD55   55
#define CMD58   58
#define ACMD41  41

static uint8_t sdSendCmd(uint8_t cmd, uint32_t arg) {
    uint8_t crc = 0x01;
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;

    spi1Transfer(0x40 | cmd);
    spi1Transfer((arg >> 24) & 0xFF);
    spi1Transfer((arg >> 16) & 0xFF);
    spi1Transfer((arg >> 8) & 0xFF);
    spi1Transfer(arg & 0xFF);
    spi1Transfer(crc);

    uint8_t response;
    for (int i = 0; i < 10; i++) {
        response = spi1Transfer(0xFF);
        if (!(response & 0x80)) break;
    }
    return response;
}

uint8_t sdInit(void) {
	for (volatile int i = 0; i < 500000; i++);

    spi1Deselect();
    for (int i = 0; i < 10; i++) spi1Transfer(0xFF);

    spi1Select();

    uint8_t r = sdSendCmd(CMD0, 0);
    printf("CMD0 response: 0x%02X\r\n", r);
    if (r != 0x01) { spi1Deselect(); return 0; }

    sdSendCmd(CMD8, 0x1AA);
    uint8_t b1 = spi1Transfer(0xFF);
    uint8_t b2 = spi1Transfer(0xFF);
    uint8_t b3 = spi1Transfer(0xFF);
    uint8_t b4 = spi1Transfer(0xFF);
    printf("CMD8 response bytes: %02X %02X %02X %02X\r\n", b1, b2, b3, b4);

    uint32_t timeout = 100000;
    do {
        sdSendCmd(CMD55, 0);
        r = sdSendCmd(ACMD41, 0x40000000);
    } while (r != 0x00 && --timeout);

    printf("ACMD41 final response: 0x%02X, timeout remaining: %lu\r\n", r, timeout);

    if (timeout == 0) { spi1Deselect(); return 0; }

    sdSendCmd(CMD16, 512);

    spi1Deselect();
    spi1SetSpeedFast();

    return 1;
}

uint8_t sdReadBlock(uint32_t blockAddr, uint8_t *buffer) {
    spi1Select();

    if (sdSendCmd(CMD17, blockAddr) != 0x00) {
        spi1Deselect();
        return 0;
    }

    // wait for data start token (0xFE)
    uint32_t timeout = 100000;
    uint8_t token;
    do {
        token = spi1Transfer(0xFF);
    } while (token != 0xFE && --timeout);

    if (timeout == 0) { spi1Deselect(); return 0; }

    // read 512 bytes of data
    for (int i = 0; i < 512; i++) {
        buffer[i] = spi1Transfer(0xFF);
    }

    // discard 2 CRC bytes
    spi1Transfer(0xFF);
    spi1Transfer(0xFF);

    spi1Deselect();
    return 1;
}

uint8_t sdWriteBlock(uint32_t blockAddr, const uint8_t *buffer) {
    spi1Select();

    if (sdSendCmd(CMD24, blockAddr) != 0x00) {
        spi1Deselect();
        return 0;
    }

    spi1Transfer(0xFE);  // start token

    for (int i = 0; i < 512; i++) {
        spi1Transfer(buffer[i]);
    }

    spi1Transfer(0xFF);  // dummy CRC
    spi1Transfer(0xFF);

    uint8_t response = spi1Transfer(0xFF);
    if ((response & 0x1F) != 0x05) {
        spi1Deselect();
        return 0;
    }

    // wait while card is busy writing
    while (spi1Transfer(0xFF) == 0x00);

    spi1Deselect();
    return 1;
}

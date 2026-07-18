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
#include "systick.h"

#define CMD0    0
#define CMD8    8
#define CMD16   16
#define CMD17   17
#define CMD24   24
#define CMD55   55
#define CMD58   58
#define ACMD41  41

#define CMD12   12
#define CMD18   18

#define CMD24 24U
#define CMD25 25U

#define SD_TOKEN_SINGLE_WRITE 0xFEU
#define SD_TOKEN_MULTI_WRITE  0xFCU
#define SD_TOKEN_STOP_WRITE   0xFDU

#define SD_DATA_ACCEPTED      0x05U

/*
 * These are polling limits rather than exact timeouts. Adjust upward
 * if a slow card legitimately reaches them.
 */
#define SD_BUSY_TIMEOUT_POLLS     1000000UL
#define SD_RESPONSE_TIMEOUT_POLLS 64U

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

    // Power-up: >=74 clocks with CS deselected. 16 bytes for margin —
    // some cards need more wake-up than the minimum 10.
    spi1Deselect();
    for (int i = 0; i < 16; i++) spi1Transfer(0xFF);

    spi1Select();

    // CMD0 with a short retry: covers cards that need a second nudge
    // into idle (also likely cures the old card's failed-first-init)
    uint8_t r = 0xFF;
    for (int attempt = 0; attempt < 5; attempt++) {
        r = sdSendCmd(CMD0, 0);
        if (r == 0x01) break;
        spi1Transfer(0xFF);              // breathing room between tries
    }
    printf("CMD0 response: 0x%02X\r\n", r);
    if (r != 0x01) { spi1Deselect(); return 0; }

    // CMD8: R7 response = R1 status byte + 4 payload bytes.
    // THE FIX: capture and check the R1 before reading payload.
    r = sdSendCmd(CMD8, 0x1AA);
    printf("CMD8 R1: 0x%02X\r\n", r);

    uint8_t sdv2 = 0;                    // does the card speak SD v2?

    if (r == 0x01) {
        // v2 card: read the 4-byte payload and verify echo
        uint8_t b1 = spi1Transfer(0xFF);
        uint8_t b2 = spi1Transfer(0xFF);
        uint8_t b3 = spi1Transfer(0xFF);
        uint8_t b4 = spi1Transfer(0xFF);
        printf("CMD8 payload: %02X %02X %02X %02X\r\n", b1, b2, b3, b4);

        if (b3 == 0x01 && b4 == 0xAA) {
            sdv2 = 1;                    // voltage accepted, pattern echoed
        } else {
            printf("CMD8 echo mismatch\r\n");
            spi1Deselect();
            return 0;
        }
    } else if (r == 0x05) {
        // illegal command: SD v1 / MMC — no payload follows
        printf("CMD8 rejected: v1 card\r\n");
        sdv2 = 0;
    } else {
        printf("CMD8: no valid response\r\n");
        spi1Deselect();
        return 0;
    }

    // ACMD41: HCS bit only for v2 cards. Bounded by TIME, not raw
    // iteration count — spec says init completes within 1 second.
    uint32_t acmdArg = sdv2 ? 0x40000000 : 0;
    uint32_t start = millis();

    do {
        sdSendCmd(CMD55, 0);
        r = sdSendCmd(ACMD41, acmdArg);
        if (r == 0x00) break;
    } while (millis() - start < 1000);

    printf("ACMD41 final response: 0x%02X after %lums\r\n",
           r, (unsigned long)(millis() - start));

    if (r != 0x00) { spi1Deselect(); return 0; }

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

uint8_t sdReadBlocks(uint32_t blockAddr, uint8_t *buffer, uint32_t count) {
    if (count == 1) return sdReadBlock(blockAddr, buffer);

    spi1Select();
    if (sdSendCmd(CMD18, blockAddr) != 0x00) {
        spi1Deselect();
        return 0;
    }

    for (uint32_t b = 0; b < count; b++) {
        uint32_t timeout = 100000;
        uint8_t token;
        do {
            token = spi1Transfer(0xFF);
        } while (token != 0xFE && --timeout);
        if (timeout == 0) { spi1Deselect(); return 0; }

        for (int i = 0; i < 512; i++) {
            buffer[b * 512 + i] = spi1Transfer(0xFF);
        }
        spi1Transfer(0xFF);  // CRC
        spi1Transfer(0xFF);
    }

    // CMD12: stop transmission. Skip the stuff byte the card
    // emits right after the command before its R1b response.
    sdSendCmd(CMD12, 0);
    while (spi1Transfer(0xFF) == 0x00);  // wait out busy

    spi1Deselect();
    return 1;
}

/*
 * Wait until the card releases MISO and returns 0xFF.
 *
 * SD cards hold MISO low while internally programming written data.
 * The polling limit prevents a bad card or communication failure from
 * locking the firmware forever.
 */
static uint8_t sdWaitUntilReady(uint32_t maximumPolls) {
    while (maximumPolls > 0U) {
        if (spi1Transfer(0xFFU) == 0xFFU) {
            return 1U;
        }

        maximumPolls--;
    }

    return 0U;
}


/*
 * Read the data-response token returned after a write block.
 *
 * Ignore leading 0xFF bytes in case the card does not present the token
 * on the first byte immediately following the CRC.
 */
static uint8_t sdReadWriteResponse(uint8_t *response) {
    if (response == NULL) {
        return 0U;
    }

    for (
        uint32_t i = 0U;
        i < SD_RESPONSE_TIMEOUT_POLLS;
        i++
    ) {
        uint8_t value =
            spi1Transfer(0xFFU);

        if (value != 0xFFU) {
            *response = value;
            return 1U;
        }
    }

    return 0U;
}


/*
 * Send one 512-byte data block after CMD24 or CMD25.
 *
 * token must be:
 *
 *     0xFE for CMD24 single-block writing
 *     0xFC for CMD25 multi-block writing
 */
static uint8_t sdSendWriteDataBlock(
    const uint8_t *buffer,
    uint8_t token
) {
    if (buffer == NULL) {
        return 0U;
    }

    spi1Transfer(token);

    for (uint32_t i = 0U; i < 512U; i++) {
        spi1Transfer(buffer[i]);
    }

    /*
     * Dummy CRC is accepted when SPI CRC checking has not been enabled.
     */
    spi1Transfer(0xFFU);
    spi1Transfer(0xFFU);

    uint8_t response = 0xFFU;

    if (!sdReadWriteResponse(&response)) {
        return 0U;
    }

    /*
     * The low five bits equal 0x05 when the card accepted the data.
     */
    if ((response & 0x1FU) != SD_DATA_ACCEPTED) {
        return 0U;
    }

    /*
     * Wait for the card to finish internally programming this block.
     */
    if (!sdWaitUntilReady(SD_BUSY_TIMEOUT_POLLS)) {
        return 0U;
    }

    return 1U;
}

uint8_t sdWriteBlock(
    uint32_t blockAddr,
    const uint8_t *buffer
) {
    if (buffer == NULL) {
        return 0U;
    }

    spi1Select();

    /*
     * Make sure the card is not still busy from a previous operation.
     */
    if (!sdWaitUntilReady(SD_BUSY_TIMEOUT_POLLS)) {
        spi1Deselect();
        return 0U;
    }

    if (sdSendCmd(CMD24, blockAddr) != 0x00U) {
        spi1Deselect();
        return 0U;
    }

    uint8_t succeeded =
        sdSendWriteDataBlock(
            buffer,
            SD_TOKEN_SINGLE_WRITE
        );

    spi1Deselect();

    return succeeded;
}

/*
 * Write multiple consecutive 512-byte sectors with one CMD25
 * transaction.
 *
 * CMD25 transaction:
 *
 *   1. Select card.
 *   2. Send CMD25 once.
 *   3. Send each block with token 0xFC.
 *   4. Send stop-transmission token 0xFD.
 *   5. Wait for the card to finish.
 *   6. Deselect card.
 */
uint8_t sdWriteBlocks(
    uint32_t blockAddr,
    const uint8_t *buffer,
    uint32_t blockCount
) {
    if (
        buffer == NULL ||
        blockCount == 0U
    ) {
        return 0U;
    }

    if (blockCount == 1U) {
        return sdWriteBlock(
            blockAddr,
            buffer
        );
    }

    uint8_t commandAccepted = 0U;
    uint8_t succeeded = 1U;

    spi1Select();

    /*
     * Ensure the card has completed any previous operation.
     */
    if (!sdWaitUntilReady(SD_BUSY_TIMEOUT_POLLS)) {
        succeeded = 0U;
        goto cleanup;
    }

    if (sdSendCmd(CMD25, blockAddr) != 0x00U) {
        succeeded = 0U;
        goto cleanup;
    }

    commandAccepted = 1U;

    for (
        uint32_t blockIndex = 0U;
        blockIndex < blockCount;
        blockIndex++
    ) {
        const uint8_t *blockBuffer =
            buffer +
            (blockIndex * 512U);

        if (!sdSendWriteDataBlock(
                blockBuffer,
                SD_TOKEN_MULTI_WRITE
            )) {
            succeeded = 0U;
            break;
        }
    }

    /*
     * CMD25 is terminated by the special 0xFD data token, not CMD12.
     * Send it even when one of the blocks failed, provided CMD25 itself
     * was accepted.
     */
    if (commandAccepted) {
        spi1Transfer(
            SD_TOKEN_STOP_WRITE
        );

        if (!sdWaitUntilReady(
                SD_BUSY_TIMEOUT_POLLS
            )) {
            succeeded = 0U;
        }
    }

cleanup:
    spi1Deselect();

    return succeeded;
}

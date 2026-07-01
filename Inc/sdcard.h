/*
 * sdcard.h
 *
 *  Created on: Jun 30, 2026
 *      Author: brettsodie
 */

#ifndef SDCARD_H_
#define SDCARD_H_

#include <stdint.h>

uint8_t sdInit(void);
uint8_t sdReadBlock(uint32_t blockAddr, uint8_t *buffer);
uint8_t sdWriteBlock(uint32_t blockAddr, const uint8_t *buffer);
int shouldSkipAudioFile(const char *name);
void listMp3Files(char filenames[][64], uint32_t *count);
uint32_t getMp3Duration(const char *filename);

#endif /* SDCARD_H_ */

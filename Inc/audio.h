/*
 * audio.h
 *
 *  Created on: Jul 1, 2026
 *      Author: brettsodie
 */

#ifndef AUDIO_H_
#define AUDIO_H_

#include <stdbool.h>
#include <stdint.h>
#include "state.h"

void audioInit();
uint8_t spi2Transfer(uint8_t data);
void vs1053PlayFile(const char *filename);
void vs1053SendData(uint8_t *data, uint16_t length);
uint16_t vs1053ReadRegister(uint8_t address);
void vs1053WriteRegister(uint8_t address, uint16_t data);
void audioPlayFile(const char *filename);
void audioSetPlaying(bool playing);
bool audioIsPlaying(void);
void audioProcess(State *state);
void audioStop(void);
uint32_t audioGetPosition(void);
uint32_t audioGetPendingDuration(void);
bool audioIsDurationReady(void);
void audioRequestPlayFile(const TrackRecord *track);
void playbackPrefetchNext(State *state);

// AUDIO CONTROL FUNCTIONS
void playAudioFile(State *state, TrackRecord *trackPtr);

#endif /* AUDIO_H_ */

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "uart.h"
#include "controls.h"
#include "state.h"
#include "screens.h"
#include "display.h"
#include "font.h"
#include "spi1.h"
#include "ff.h"
#include "audio.h"
#include "systick.h"
#include "index-tracks.h"
#include "window.h"

int main() {
	uartTxinit();
	spi1Init();
	systickInit();

	FATFS fs;
	FRESULT res = f_mount(&fs, "", 1);

	if (res == FR_OK) {
		printf("SD card mounted successfully!\r\n");
	} else {
		printf("Mount failed: %d\r\n", res);
	}

//	wipeSystemDir();
//
//	printf("Beginning track index\r\n");
//
//	indexSongList();
//
//	printf("Track index built\r\n");
//
//	uint32_t t0 = millis();
//	buildGroupedIndex("/system/kilobyte.idx", "/system/grouped.idx");
//	printf("Grouped index build took %lums\r\n", (unsigned long)(millis() - t0));
//
//	printf("Grouped index built\r\n");
//
//	buildHeaderIndexes("/system/grouped.idx",
//	                   "/system/artists.idx", "/system/albums.idx");

	printf("Artist and album indexes built\r\n");

	trackIndexInit(&state);
	artistIndexInit(&state);

	printf("innn\r\n");
	printf("total tracks %d\r\n", (int)state.trackList.totalTracksInSystem);

	fontInit();
    oledInit();
	dma1Stream6Init();
    oledClear();

    printf("initializing audio\r\n");

    audioInit();

    printf("audio initialized\r\n");

    __disable_irq();
    controlsInit();
    __enable_irq();

    navigate(&state, HOME);

    static uint32_t lastPositionUpdate = 0;

    while(1) {
    	audioProcess(&state);
    	controlsService(&state);
    	runInputRequests(&state);
    	runArtistInputRequests(&state);
    	playbackPrefetchNext(&state);

    	if (msTicks - lastPositionUpdate >= 100) {
    	    lastPositionUpdate = msTicks;

    	    if (audioIsDurationReady()) {
    	        state.player.duration = audioGetPendingDuration();
    	        drawScreen(&state);
    	    }

    	    if (state.player.isPlaying) {
    	        uint32_t position = audioGetPosition();

    	        if (position != state.player.position) {
    	            state.player.position = position;

    	            if (state.navigationHistory[state.historyIndex].name == PLAYER) {
    	            	drawScreen(&state);
    	            }
    	        }
    	    }


    	}

    	drawFrame();
    }



    return 0;
}

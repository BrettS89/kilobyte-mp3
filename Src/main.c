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


int main() {
	uartTxinit();
	spi1Init();
	systickInit();

	FATFS fs;
	FRESULT res = f_mount(&fs, "", 1);

	fontInit();
    oledInit();
    oledClear();

    printf("initializing audio\r\n");

    audioInit();

    printf("audio initialized\r\n");

    __disable_irq();
    controlsInit();
    __enable_irq();

    navigate(&state, HOME);

    if (res == FR_OK) {
		printf("SD card mounted successfully!\r\n");
	} else {
		printf("Mount failed: %d\r\n", res);
	}

    static uint32_t lastPositionUpdate = 0;

    while(1) {
    	audioProcess();

    	if (msTicks - lastPositionUpdate >= 50) {
			lastPositionUpdate = msTicks;

			if (state.player.isPlaying) {
				uint32_t position = audioGetPosition();

				printf("position: %d\r\n", (int)position);

				if (position != state.player.position) {
					state.player.position = position;
					drawScreen(&state);
				}
			}

			drawFrame();
		}
    }

    return 0;
}

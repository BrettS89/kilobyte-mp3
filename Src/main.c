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


int main() {
	uartTxinit();

	spi1Init();

	FATFS fs;
	FRESULT res = f_mount(&fs, "", 1);

	fontInit();

    oledInit();

    oledClear();

    __disable_irq();
    controlsInit();
    __enable_irq();


    navigate(&state, HOME);

    if (res == FR_OK) {
		printf("SD card mounted successfully!\r\n");
	} else {
		printf("Mount failed: %d\r\n", res);
	}

    while(1) {
    }

    return 0;
}

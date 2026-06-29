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

int main() {
	uartTxinit();
	fontInit();
    oledInit();
    oledClear();

    __disable_irq();
    controlsInit();
    __enable_irq();

//    oledDrawString(0, 0, "We in brah");
//    drawFrame();

//    oledDrawString(0, 0, "Hello World!");
//    oledDrawString(1, 0, "Foo bar");
//    oledDrawString(2, 0, "Bar baz");

    navigate(&state, HOME);

    while(1) {}

    return 0;
}

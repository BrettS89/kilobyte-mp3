/*
 * player-progress.c
 *
 *  Created on: Jul 1, 2026
 *      Author: brettsodie
 */


#include <string.h>
#include <stdio.h>
#include "components.h"
#include "display.h"
#include "state.h"

void renderPlayerProgressBar(State *state, uint8_t row) {
    // bar dimensions
    uint8_t barWidth = 110;
    uint8_t barHeight = 6;
    uint8_t barX = (128 - barWidth) / 2;
    uint8_t barY = row * 8;

    // draw outline rectangle
    drawRect(barX, barY, barWidth, barHeight);

    // calculate fill width based on position/duration
    if (state->player.duration > 0) {
        uint32_t fillWidth = (state->player.position * (barWidth - 2)) / state->player.duration;
        if (fillWidth > barWidth - 2) fillWidth = barWidth - 2;
        if (fillWidth > 0) {
            fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2);
        }
    }

    // format time strings
    char posStr[10];
    char durStr[10];

    printf("duration: %d\r\n", (int)state->player.duration);

    uint32_t posMins = (state->player.position / 60) % 100;  // cap at 99
    uint32_t posSecs = state->player.position % 60;
    uint32_t durMins = (state->player.duration / 60) % 100;  // cap at 99
    uint32_t durSecs = state->player.duration % 60;

    // m:ss for single digit minutes, mm:ss for double
    if (posMins >= 10) {
        snprintf(posStr, sizeof(posStr), "%lu:%02lu", posMins, posSecs);
    } else {
        snprintf(posStr, sizeof(posStr), "%lu:%02lu", posMins, posSecs);
    }

    if (durMins >= 10) {
        snprintf(durStr, sizeof(durStr), "%lu:%02lu", durMins, durSecs);
    } else {
        snprintf(durStr, sizeof(durStr), "%lu:%02lu", durMins, durSecs);
    }

    // draw position on left below bar
    uint8_t timeRow = row + 1;
    drawString(timeRow, barX, posStr);

    // calculate duration string width to right align it
    uint8_t durStrLen = strlen(durStr);
    uint8_t durStrWidth = durStrLen * 6;
    uint8_t durX = barX + barWidth - durStrWidth + 2;

    drawString(timeRow, durX, durStr);
}

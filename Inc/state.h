/*
 * state.h
 *
 *  Created on: Jun 25, 2026
 *      Author: brettsodie
 */

#ifndef STATE_H_
#define STATE_H_

#include <stdint.h>
#include <stdbool.h>

#define SCREEN_NUM 5
#define SCREEN_NAME_MAX_LEN 10
#define SCROLLABLE_ITEMS_MAX_LEN 10

typedef struct {
	uint32_t index;
    char filename[128];
    char title[64];
    char artist[64];
    char album[64];
    uint32_t duration;
} __attribute__((packed)) TrackRecord;

typedef struct {
	TrackRecord tracks[16];
	uint32_t cursorIndex;
	uint32_t totalCount;
	uint32_t totalTracksInSystem;
} TrackList;

typedef enum {
    HOME,
	MUSIC,
	RADIO,
    SONGS,
	PLAYER,
	ARTISTS,
} ScreenName;

typedef struct {
	char name[256];
	ScreenName screenTrigger;
} ScrollableItem;

typedef struct {
	ScreenName name;
	uint32_t cursorIndex;
	uint32_t scrollIndex;
	ScrollableItem items[SCROLLABLE_ITEMS_MAX_LEN];
	uint32_t scrollableItemCount;
} Screen;

typedef struct {
	char artist[64];
	char album[64];
	char title[64];
	char filename[256];
	uint32_t duration;
	uint32_t position;
	bool isPlaying;
} Player;

typedef struct {
	Screen navigationHistory[5];
	uint32_t historyIndex;
	Player player;
	TrackList trackList;
} State;

extern State state;

#endif /* STATE_H_ */

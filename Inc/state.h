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
#include "ff.h"

#define SCREEN_NUM 5
#define SCREEN_NAME_MAX_LEN 10
#define SCROLLABLE_ITEMS_MAX_LEN 10

typedef struct {
	FIL allTracksFile;
	FIL allArtistsFile;
	bool allTracksFileOpen;
	bool allArtistsFileOpen;
} IndexFiles;

typedef struct {
	uint32_t index;
    char filename[128];
    char title[64];
    char artist[64];
    char album[64];
    uint32_t duration;
} __attribute__((packed)) TrackRecord;

typedef enum {
    SOURCE_ALL_TRACKS,
    SOURCE_ARTIST,
    SOURCE_ALBUM,
    SOURCE_PLAYLIST
} PlaybackSourceType;

typedef struct {
    PlaybackSourceType sourceType;
    uint16_t sourceId;        // which playlist/album/artist; unused for ALL
    uint16_t trackCount;      // total tracks in the source, captured at play start
    TrackRecord currentTrack;    // whatever you need to identify/open the file
    TrackRecord nextTrack;       // prefetched, see below
    bool nextTrackIsLoaded;
    bool nextTrackIsValid;
} PlaybackContext;

typedef struct {
	TrackRecord tracks[16];
	uint32_t cursorIndex;
	uint32_t totalCount;
	uint32_t totalTracksInSystem;
} TrackList;

typedef struct {
	uint32_t index;
	char     name[48];       // artist name, NUL-terminated, cmpNoCase order
	uint32_t firstTrack;     // artist's full run in grouped.idx
	uint16_t trackCount;     //   (kept so artist-level play isn't a format change)
	uint16_t firstAlbum;     // artist's album run in albums.idx
	uint16_t albumCount;     //   Albums screen window = [firstAlbum, +albumCount)
	uint16_t _reserved;      // pad to 4-byte multiple; zero-fill; future use
} ArtistRecord;           // 60 bytes

typedef struct {
	ArtistRecord artists[16];
	uint32_t cursorIndex;
	uint32_t inWindowCount;
	uint32_t totalArtists;
} ArtistList;

typedef struct {
	uint32_t index;
	char     name[48];
	uint32_t firstTrack;
	uint16_t trackCount;
	uint16_t year;
} AlbumRecord;

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
	TrackRecord track;
	uint32_t duration;
	uint32_t position;
	bool isPlaying;
} Player;

typedef struct {
	Screen navigationHistory[5];
	uint32_t historyIndex;
	Player player;
	TrackList trackList;
	ArtistList artistList;
	PlaybackContext playbackContext;
	IndexFiles indexFiles;
} State;

extern State state;

#endif /* STATE_H_ */

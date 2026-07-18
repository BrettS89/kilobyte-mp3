/*
 * index-tracks.h
 *
 *  Created on: Jul 6, 2026
 *      Author: brettsodie
 */

#ifndef INDEX_TRACKS_H_
#define INDEX_TRACKS_H_

#include <stdbool.h>
#include <stdint.h>
#include "ff.h"
#include "state.h"

typedef enum {
    FASTTAG_OK,             // tags parsed from first cluster
    FASTTAG_NO_TAG,         // file has no ID3v2 tag
    FASTTAG_NEED_FALLBACK   // couldn't complete — caller should f_open
} FastTagResult;

// ---- pipeline stage 1: scan MP3s, build the all-songs index ----
void indexSongList(void);

// ---- pipeline stage 2: re-sort into (artist, album, track#, title) ----
FRESULT buildGroupedIndex(const char *songIndexPath, const char *groupedPath);

// ---- pipeline stage 3: one linear scan emits both header files ----
FRESULT buildHeaderIndexes(const char *groupedPath,
                           const char *artistsPath,
                           const char *albumsPath);

int wipeSystemDir(void);

FastTagResult fastReadID3Tags(DIR *dir, char *title, char *artist, char *album);

TrackRecord *getIndexWorkspace(void);

// Canonical index ordering — the sort that writes grouped.idx and the
// scan that reads its run boundaries must both use this comparator.

//bool loadTotalTrackCount(uint32_t *totalTracks);

#endif /* INDEX_TRACKS_H_ */

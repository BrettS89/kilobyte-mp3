/*
 * artist-album-index.c
 *
 *  Created on: Jul 16, 2026
 *      Author: brettsodie
 *
 * Builds artists.idx and albums.idx from grouped.idx.
 *
 * grouped.idx is TrackRecords sorted by (artist, album, trackNumber, title),
 * so every artist is one contiguous run and every album is one contiguous
 * sub-run. This is a single linear pass that emits a header record each
 * time a run closes.
 *
 * artists.idx: ArtistRecords, in artist order (inherited from the scan).
 *              Each record's .index field = its own record position.
 * albums.idx:  AlbumRecords, in (artist, album) order — each artist's
 *              albums are contiguous, pointed at by firstAlbum/albumCount.
 *              Each record's .index field = its own record position.
 *
 * An empty album tag sorted to the front of its artist's run; it emits
 * here as an "Unknown Album" record like any other album.
 *
 * NOTE: ArtistRecord/AlbumRecord layouts are index FILE FORMATS. The
 * .index fields changed the struct sizes — any index files built by
 * older firmware are invalid. Force a re-index after flashing this.
 *
 * RAM: two pending header records + one TrackRecord + FILs. O(1) always.
 */

#include "ff.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "state.h"          // TrackRecord, ArtistRecord, AlbumRecord
#include "index-tracks.h"

#define UNKNOWN_ALBUM_NAME  "Unknown Album"

// TODO: dedupe with the grouped-sort comparator when buildGroupedIndex
// returns to the build — shared declaration already lives in
// index-tracks.h; both static copies should die then.
static int cmpNoCase(const char *a, const char *b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : (unsigned char)*a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : (unsigned char)*b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// ---------------------------------------------------------------------------

static void beginAlbum(AlbumRecord *alb, const TrackRecord *t,
                       uint32_t albumIdx, uint32_t trackIdx) {
    memset(alb, 0, sizeof(*alb));

    alb->index = albumIdx;

    if (t->album[0] == '\0') {
        strncpy(alb->name, UNKNOWN_ALBUM_NAME, sizeof(alb->name) - 1);
    } else {
        strncpy(alb->name, t->album, sizeof(alb->name) - 1);
    }
    alb->firstTrack = trackIdx;
    alb->trackCount = 0;
    // alb->year: populate here if/when TrackRecord carries a year field
}

static void beginArtist(ArtistRecord *art, const TrackRecord *t,
                        uint32_t artistIdx, uint32_t trackIdx, uint16_t albumIdx) {
    memset(art, 0, sizeof(*art));

    art->index = artistIdx;

    strncpy(art->name, t->artist, sizeof(art->name) - 1);
    art->firstTrack = trackIdx;
    art->trackCount = 0;
    art->firstAlbum = albumIdx;
    art->albumCount = 0;
}

FRESULT buildHeaderIndexes(const char *groupedPath,
                           const char *artistsPath,
                           const char *albumsPath) {
    FIL src, artFile, albFile;
    FRESULT res;
    UINT br, bw;

    TrackRecord  track;
    ArtistRecord curArtist;
    AlbumRecord  curAlbum;

    uint32_t trackIdx       = 0;     // index of the record we're reading
    uint16_t albumsEmitted  = 0;     // albums written so far = next album's index
    uint32_t artistsEmitted = 0;     // artists written so far = next artist's index
    bool     haveRun        = false; // false until the first record is seen

    res = f_open(&src, groupedPath, FA_READ);
    if (res != FR_OK) return res;

    res = f_open(&artFile, artistsPath, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) { f_close(&src); return res; }

    res = f_open(&albFile, albumsPath, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) { f_close(&src); f_close(&artFile); return res; }

    for (;;) {
        res = f_read(&src, &track, sizeof(TrackRecord), &br);
        if (res != FR_OK) goto fail;
        if (br < sizeof(TrackRecord)) break;         // EOF (or torn tail record)

        if (!haveRun) {
            // very first track: open both runs (indexes 0 and 0)
            beginAlbum(&curAlbum, &track, albumsEmitted, trackIdx);
            beginArtist(&curArtist, &track, artistsEmitted, trackIdx, albumsEmitted);
            haveRun = true;
        } else {
            bool artistChanged = (cmpNoCase(track.artist, curArtist.name) != 0);

            // Album boundary check must compare against the RAW tag value:
            // curAlbum.name holds "Unknown Album" for the empty-tag run, so
            // compare emptiness class first, then names.
            bool curIsUnknown = (strcmp(curAlbum.name, UNKNOWN_ALBUM_NAME) == 0);
            bool newIsEmpty   = (track.album[0] == '\0');
            bool albumChanged = artistChanged ||
                                (curIsUnknown != newIsEmpty) ||
                                (!newIsEmpty && cmpNoCase(track.album, curAlbum.name) != 0);

            if (albumChanged) {
                // flush album FIRST — it belongs to the (possibly closing) artist
                res = f_write(&albFile, &curAlbum, sizeof(AlbumRecord), &bw);
                if (res != FR_OK || bw != sizeof(AlbumRecord)) goto fail;
                albumsEmitted++;
                curArtist.albumCount++;

                if (artistChanged) {
                    res = f_write(&artFile, &curArtist, sizeof(ArtistRecord), &bw);
                    if (res != FR_OK || bw != sizeof(ArtistRecord)) goto fail;
                    artistsEmitted++;
                    beginArtist(&curArtist, &track, artistsEmitted, trackIdx, albumsEmitted);
                }

                beginAlbum(&curAlbum, &track, albumsEmitted, trackIdx);
            }
        }

        curAlbum.trackCount++;
        curArtist.trackCount++;
        trackIdx++;
    }

    // Final flush — the loop's boundary logic never fires for the last
    // runs. Without this, the alphabetically-last artist and album
    // silently vanish from the device.
    if (haveRun) {
        res = f_write(&albFile, &curAlbum, sizeof(AlbumRecord), &bw);
        if (res != FR_OK || bw != sizeof(AlbumRecord)) goto fail;
        curArtist.albumCount++;

        res = f_write(&artFile, &curArtist, sizeof(ArtistRecord), &bw);
        if (res != FR_OK || bw != sizeof(ArtistRecord)) goto fail;
    }

    f_close(&src);
    res = f_close(&artFile);                 // closes flush writes — check both
    FRESULT res2 = f_close(&albFile);
    if (res != FR_OK) return res;
    return res2;

fail:
    f_close(&src);
    f_close(&artFile);
    f_close(&albFile);
    return (res == FR_OK) ? FR_DISK_ERR : res;
}

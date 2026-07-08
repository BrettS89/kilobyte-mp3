/*
 * controls.c
 *
 *  Created on: Jul 8, 2026
 *      Author: brettsodie
 */


#include <string.h>
#include "audio.h"
#include "state.h"

void playAudioFile(State *state, TrackRecord *trackPtr) {
	TrackRecord track = *trackPtr;

	strncpy(state->player.filename, track.filename, sizeof(state->player.filename) - 1);
	strncpy(state->player.album, track.album, sizeof(state->player.album) - 1);
	strncpy(state->player.artist, track.artist, sizeof(state->player.artist) - 1);
	strncpy(state->player.title, track.title, sizeof(state->player.title) - 1);

	state->player.isPlaying = true;
	state->player.position = 0;
	state->player.duration = 0;
	state->player.track = *trackPtr;

	state->playbackContext.currentTrack = track;
	state->playbackContext.sourceType = SOURCE_ALL_TRACKS;

	memset(&state->playbackContext.nextTrack, 0, sizeof(TrackRecord));
	state->playbackContext.nextTrackIsLoaded = false;
	state->playbackContext.nextTrackIsValid = true;

	audioRequestPlayFile(state->player.filename);
}


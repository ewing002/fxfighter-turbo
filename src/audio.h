/*
 * FX Fighter Turbo - open reimplementation
 * Audio: CD music (MP3 replacement) + sound effects (.asf samples, .fxl banks), mixed with SDL audio.
 */
#ifndef FX_AUDIO_H
#define FX_AUDIO_H
#include "assets.h"

/* opens the audio device; music_dirs: candidate directories (with trailing separator)
 * that may contain trackNN.ogg or trackNN.mp3 (optional) */
int  audio_init(const Pac *pac, const char *const *music_dirs, int ndirs);
void audio_shutdown(void);

void music_play(int track);        /* CD track number (2..14); no-op if already playing */
void music_stop(void);
void music_toggle(void);
void music_volume(float delta);
int  music_available(void);
int  music_playing(void);          /* a track is open and playing (Ogg Vorbis or MP3) */
void music_set_volume(float v);    /* 0..1 */
float music_get_volume(void);
void sfx_set_volume(float v);
float sfx_get_volume(void);

/* sound banks (FUN_0043509b): 0 = generic/announcer, 1 = player 1 fighter, 2 = player 2 fighter,
 * 3 = arena (b%d.fxl). Loads fight/sound/<name>.fxl and its samples. */
int  sfx_bank_load(int bank, const char *name);
/* play entry `index` (1-based) of a bank (FUN_00435eb4) */
void sfx_play(int bank, int index);
/* an FMV sound track: interleaved stereo 16-bit; the buffer must stay valid until stopped */
void audio_stream_play(const int16_t *stereo, size_t frames, int rate);
void audio_stream_stop(void);
void sfx_toggle(void);
int  sfx_enabled(void);

/* decode an .asf sample into malloc'd 16-bit mono PCM; returns sample count */
int  asf_decode(const uint8_t *d, size_t n, int16_t **pcm, int *rate);
#endif

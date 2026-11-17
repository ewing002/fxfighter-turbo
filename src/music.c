/*
 * FX Fighter Turbo - open reimplementation
 * CD music replacement: plays music/trackNN.mp3 (ripped from the game CD) with SDL audio.
 * FIGHT.EXE plays CD track (arena + 2) during a fight (FUN_0043b077 -> MCI_PLAY),
 * track 2 in the credits and track 14 in attract mode.
 */
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#include "music.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_AudioDeviceID g_dev;
static drmp3 g_mp3;
static int   g_open, g_track = -1, g_enabled = 1;
static float g_volume = 0.7f;
static char  g_dir[1024];

static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    Sint16 *out = (Sint16 *)stream;
    int frames = len / 4;
    memset(stream, 0, (size_t)len);
    if (!g_open || !g_enabled) return;
    int done = 0;
    while (done < frames) {
        drmp3_uint64 got = drmp3_read_pcm_frames_s16(&g_mp3, (drmp3_uint64)(frames - done), out + done * 2);
        if (got == 0) {                       /* loop the track like the CD player did (auto repeat) */
            if (!drmp3_seek_to_pcm_frame(&g_mp3, 0)) break;
            got = drmp3_read_pcm_frames_s16(&g_mp3, (drmp3_uint64)(frames - done), out + done * 2);
            if (got == 0) break;
        }
        done += (int)got;
    }
    int vol = (int)(g_volume * 256.0f);
    for (int i = 0; i < frames * 2; i++) out[i] = (Sint16)((out[i] * vol) >> 8);
}

static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

int music_init(const char *const *dirs, int ndirs)
{
    g_dir[0] = 0;
    for (int i = 0; i < ndirs; i++) {
        if (!dirs[i]) continue;
        char p[1100];
        snprintf(p, sizeof p, "%strack03.mp3", dirs[i]);
        if (file_exists(p)) { snprintf(g_dir, sizeof g_dir, "%s", dirs[i]); break; }
    }
    if (!g_dir[0]) { printf("music: no music/trackNN.mp3 found (optional)\n"); return 0; }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { printf("music: %s\n", SDL_GetError()); return 0; }
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048;
    want.callback = audio_cb;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) { printf("music: %s\n", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(g_dev, 0);
    printf("music: using %s\n", g_dir);
    return 1;
}

void music_play(int track)
{
    if (!g_dev || track == g_track) return;
    char p[1100];
    snprintf(p, sizeof p, "%strack%02d.mp3", g_dir, track);
    SDL_LockAudioDevice(g_dev);
    if (g_open) { drmp3_uninit(&g_mp3); g_open = 0; }
    g_track = track;
    if (drmp3_init_file(&g_mp3, p, NULL)) {
        if (g_mp3.sampleRate != 44100 || g_mp3.channels != 2) { drmp3_uninit(&g_mp3); }   /* CD rips are 44.1k stereo */
        else g_open = 1;
    }
    SDL_UnlockAudioDevice(g_dev);
}

void music_stop(void)
{
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    if (g_open) { drmp3_uninit(&g_mp3); g_open = 0; }
    g_track = -1;
    SDL_UnlockAudioDevice(g_dev);
}

void music_toggle(void) { g_enabled = !g_enabled; }
int  music_enabled(void) { return g_enabled; }
void music_volume(float delta)
{
    g_volume += delta;
    if (g_volume < 0) g_volume = 0;
    if (g_volume > 1) g_volume = 1;
}
int  music_available(void) { return g_dev != 0; }

void music_shutdown(void)
{
    if (!g_dev) return;
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    if (g_open) { drmp3_uninit(&g_mp3); g_open = 0; }
}

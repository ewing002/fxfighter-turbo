/*
 * FX Fighter Turbo - open reimplementation
 * Audio mixer: music (music/trackNN.mp3, ripped from the CD) + sound effects.
 *
 * FIGHT.EXE plays CD track (arena + 2) during a fight (FUN_0043b077 -> MCI_PLAY),
 * track 2 in the credits and track 14 in attract mode.
 * Sound effects are 4-bit DPCM .asf files (decoder FUN_0044f7b1), listed in .fxl banks.
 */
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"            /* Ogg Vorbis music (public domain), tried before MP3 */
#include "audio.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  .asf decoder (FUN_0044f7b1, routines 0x44f7e6 / 0x44fb1e)          */
/*  header 44 bytes: 'ASF\0' .. u32 nblocks @0x18, u32 block samples   */
/*  @0x1c (32), u16 rate @0x24, u32 flags @0x28 (bit0 16-bit,          */
/*  bit1 stereo). Block: control byte (hi nibble shift, bit2 2nd order */
/*  predictor) + block/2 bytes of signed 4-bit deltas, high nibble 1st. */
/* ------------------------------------------------------------------ */
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int asf_decode(const uint8_t *d, size_t n, int16_t **pcm, int *rate)
{
    *pcm = NULL; *rate = 22050;
    if (n < 0x2c || memcmp(d, "ASF", 3)) return 0;
    uint32_t nb = rd32(d + 0x18), bs = rd32(d + 0x1c);
    if (bs == 0 || bs > 256 || nb > 1000000) return 0;
    *rate = d[0x24] | (d[0x25] << 8);
    if (*rate < 4000) *rate = 22050;
    int16_t *out = malloc(sizeof(int16_t) * ((size_t)nb * bs + 2));
    if (!out) return 0;
    int32_t prev = 0, prev2 = 0;
    size_t p = 0x2c, o = 0;
    for (uint32_t b = 0; b < nb && p < n; b++) {
        uint8_t hdr = d[p++];
        int cl = (hdr >> 4) + 2, pred2 = hdr & 4;
        for (uint32_t k = 0; k < bs / 2; k++) {
            uint8_t byte = p + k < n ? d[p + k] : 0;
            for (int h = 0; h < 2; h++) {
                int32_t nib = (int8_t)(h == 0 ? (byte & 0xf0) : ((byte & 0x0f) << 4));   /* nibble * 16 */
                uint32_t s;
                if (pred2) {
                    uint32_t ebx = (uint32_t)prev * 128u;
                    uint32_t eax = (uint32_t)(-prev2) * 64u;
                    uint32_t edx = (uint32_t)nib << cl;
                    uint64_t t = (uint64_t)eax + edx;
                    uint32_t carry = (uint32_t)(t >> 32);
                    s = (uint32_t)t + ebx + carry;
                } else {
                    s = (uint32_t)prev * 64u + ((uint32_t)nib << cl);
                }
                int32_t v = (int32_t)s >> 6;
                int16_t w = (int16_t)(uint16_t)v;            /* stored as a 16-bit word, no clamping */
                out[o++] = w;
                prev2 = prev; prev = w;
            }
        }
        p += bs / 2;
    }
    *pcm = out;
    return (int)o;
}

/* ------------------------------------------------------------------ */
/*  Sample cache and banks                                             */
/* ------------------------------------------------------------------ */
typedef struct { char name[16]; int16_t *pcm; int n, rate; } Sample;
#define MAX_SAMPLES 400
#define MAX_BANK    64
static Sample g_samples[MAX_SAMPLES];
static int    g_nsamples;
static int    g_bank[4][MAX_BANK];      /* sample index, -1 = none; entry 0 = bank index 1 */
static int    g_bank_n[4];
static uint8_t g_bank_speech[4][MAX_BANK];  /* '*' / '#' entries: speech (see sfx_play) */
static const Pac *g_pac;

static int sample_get(const char *name)
{
    for (int i = 0; i < g_nsamples; i++) if (!strcmp(g_samples[i].name, name)) return i;
    if (g_nsamples >= MAX_SAMPLES || !g_pac) return -1;
    char path[64];
    snprintf(path, sizeof path, "fight/sound/%s.asf", name);
    size_t sz;
    const uint8_t *d = pac_get(g_pac, path, &sz);
    if (!d) return -1;
    Sample *s = &g_samples[g_nsamples];
    snprintf(s->name, sizeof s->name, "%s", name);
    s->n = asf_decode(d, sz, &s->pcm, &s->rate);
    if (s->n <= 0) return -1;
    return g_nsamples++;
}

int sfx_bank_load(int bank, const char *name)
{
    if (bank < 0 || bank > 3 || !g_pac) return 0;
    for (int i = 0; i < MAX_BANK; i++) { g_bank[bank][i] = -1; g_bank_speech[bank][i] = 0; }
    g_bank_n[bank] = 0;
    char path[64], lname[32];
    size_t k = 0;
    for (; name[k] && k < sizeof lname - 1; k++) lname[k] = (char)tolower((unsigned char)name[k]);
    lname[k] = 0;
    snprintf(path, sizeof path, "fight/sound/%s.fxl", lname);
    size_t sz;
    const uint8_t *d = pac_get(g_pac, path, &sz);
    if (!d) return 0;
    char *txt = malloc(sz + 1);
    memcpy(txt, d, sz); txt[sz] = 0;
    int n = 0;
    for (char *line = strtok(txt, "\r\n\x1a"); line && n < MAX_BANK; line = strtok(NULL, "\r\n\x1a")) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line) continue;
        if (!strcmp(line, "end")) break;
        if (strchr(line, ';')) continue;                  /* comment */
        /* playback modes (FUN_0043509b, entry +0x10): none 0, '*' 1 and '#' 3 = speech, streamed on one
         * channel and queued; '+' 2 = short sound through a ring of small buffers (plays like 0 here) */
        if (*line == '*' || *line == '#') g_bank_speech[bank][n] = 1;
        while (*line == '*' || *line == '+' || *line == '#') line++;
        for (char *c = line; *c; c++) *c = (char)tolower((unsigned char)*c);
        for (size_t e = strlen(line); e > 0 && (line[e - 1] == ' ' || line[e - 1] == '\t'); e--) line[e - 1] = 0;   /* names are space padded */
        g_bank[bank][n++] = strcmp(line, "null") ? sample_get(line) : -1;
    }
    free(txt);
    g_bank_n[bank] = n;
    return n;
}

/* ------------------------------------------------------------------ */
/*  Mixer                                                              */
/* ------------------------------------------------------------------ */
static unsigned g_age;
#define VOICES 24
#define SPEECH VOICES                    /* the last voice: the speech channel */
#define SPEECH_Q 10
typedef struct { const Sample *s; uint64_t pos; uint32_t step; int vol; unsigned age; } Voice;   /* pos: 48.16, samples can be > 65536 frames */
static Voice g_voice[VOICES + 1];
static int   g_speech_q[SPEECH_Q], g_speech_head, g_speech_n;   /* FUN_0041142a: 10 entry ring */

static void voice_start(Voice *vc, const Sample *s)
{
    vc->s = s;
    vc->pos = 0;
    vc->step = (uint32_t)(((uint64_t)s->rate << 16) / 44100);
    vc->age = ++g_age;
}
static SDL_AudioDeviceID g_dev;
static drmp3 g_mp3;
static stb_vorbis *g_ogg;                     /* the open Ogg track, or NULL when the track is an MP3 */
static int   g_mopen, g_track = -1, g_music_on = 1, g_sfx_on = 1;
static float g_mvol = 0.6f, g_svol = 0.9f;
static char  g_dir[1024];
static int32_t g_mix[8192];
/* FMV sound track: interleaved stereo, 48.16 position (video.c) */
static const int16_t *g_vs; static size_t g_vs_n; static uint64_t g_vs_pos; static uint32_t g_vs_step;

static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    Sint16 *out = (Sint16 *)stream;
    int frames = len / 4;
    if (frames > 4096) frames = 4096;
    memset(g_mix, 0, sizeof(int32_t) * (size_t)frames * 2);
    /* music */
    if (g_mopen && g_music_on) {
        int done = 0;
        while (done < frames) {
            drmp3_uint64 got;
            if (g_ogg) got = (drmp3_uint64)stb_vorbis_get_samples_short_interleaved(g_ogg, 2, out + done * 2, (frames - done) * 2);
            else got = drmp3_read_pcm_frames_s16(&g_mp3, (drmp3_uint64)(frames - done), out + done * 2);
            if (got == 0) {                       /* loop like the CD player's auto repeat */
                if (g_ogg) { if (!stb_vorbis_seek_start(g_ogg)) break; got = (drmp3_uint64)stb_vorbis_get_samples_short_interleaved(g_ogg, 2, out + done * 2, (frames - done) * 2); }
                else {
                    if (!drmp3_seek_to_pcm_frame(&g_mp3, 0)) break;
                    got = drmp3_read_pcm_frames_s16(&g_mp3, (drmp3_uint64)(frames - done), out + done * 2);
                }
                if (got == 0) break;
            }
            done += (int)got;
        }
        int mv = (int)(g_mvol * 256);
        for (int i = 0; i < done * 2; i++) g_mix[i] = (out[i] * mv) >> 8;
    }
    /* sound effects (mono -> both channels, linear resampling) */
    if (g_sfx_on) {
        int sv = (int)(g_svol * 256);
        for (int v = 0; v <= VOICES; v++) {
            Voice *vc = &g_voice[v];
            if (!vc->s) continue;
            const Sample *s = vc->s;
            for (int i = 0; i < frames; i++) {
                uint64_t ip = vc->pos >> 16;
                if (ip + 1 >= (uint64_t)s->n) {
                    vc->s = NULL;
                    if (v == SPEECH && g_speech_n > 0) {          /* next queued line */
                        voice_start(vc, &g_samples[g_speech_q[g_speech_head]]);
                        g_speech_head = (g_speech_head + 1) % SPEECH_Q; g_speech_n--;
                        s = vc->s;
                        continue;
                    }
                    break;
                }
                int32_t a = s->pcm[ip], b = s->pcm[ip + 1], fr = (int32_t)(vc->pos & 0xffff);
                int32_t x = a + (((b - a) * fr) >> 16);
                x = (x * sv) >> 8;
                g_mix[i * 2] += x; g_mix[i * 2 + 1] += x;
                vc->pos += vc->step;
            }
        }
    }
    if (g_vs) {                                    /* FMV sound track (linear resampling) */
        int sv = (int)(g_svol * 256);
        for (int i = 0; i < frames; i++) {
            uint64_t ip = g_vs_pos >> 16;
            if (ip + 1 >= g_vs_n) { g_vs = NULL; break; }
            int32_t fr = (int32_t)(g_vs_pos & 0xffff);
            for (int c = 0; c < 2; c++) {
                int32_t a = g_vs[ip * 2 + c], b = g_vs[ip * 2 + 2 + c];
                g_mix[i * 2 + c] += ((a + (((b - a) * fr) >> 16)) * sv) >> 8;
            }
            g_vs_pos += g_vs_step;
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        int32_t x = g_mix[i];
        out[i] = (Sint16)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
    if (len / 4 > frames) memset(stream + frames * 4, 0, (size_t)(len - frames * 4));
}

void audio_stream_play(const int16_t *stereo, size_t frames, int rate)
{
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    g_vs = stereo && frames > 1 && rate > 0 ? stereo : NULL;
    g_vs_n = frames; g_vs_pos = 0;
    g_vs_step = rate > 0 ? (uint32_t)(((uint64_t)rate << 16) / 44100) : 0;
    SDL_UnlockAudioDevice(g_dev);
}

void audio_stream_stop(void) { audio_stream_play(NULL, 0, 0); }

void sfx_play(int bank, int index)
{
    if (!g_dev || bank < 0 || bank > 3 || index < 1 || index > g_bank_n[bank]) return;
    int si = g_bank[bank][index - 1];
    if (si < 0) return;
    SDL_LockAudioDevice(g_dev);
    if (g_bank_speech[bank][index - 1]) {                 /* speech: one at a time, the rest waits */
        if (!g_voice[SPEECH].s) voice_start(&g_voice[SPEECH], &g_samples[si]);
        else if (g_speech_n < SPEECH_Q) { g_speech_q[(g_speech_head + g_speech_n) % SPEECH_Q] = si; g_speech_n++; }
        SDL_UnlockAudioDevice(g_dev);
        return;
    }
    int best = 0;
    for (int v = 0; v < VOICES; v++) {
        if (!g_voice[v].s) { best = v; break; }
        if (g_voice[v].age < g_voice[best].age) best = v;   /* steal the oldest */
    }
    voice_start(&g_voice[best], &g_samples[si]);
    SDL_UnlockAudioDevice(g_dev);
}

static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

/* the open music file is closed with the audio device locked (or not yet running) */
static void music_close_locked(void)
{
    if (g_ogg) { stb_vorbis_close(g_ogg); g_ogg = NULL; g_mopen = 0; }
    else if (g_mopen) { drmp3_uninit(&g_mp3); g_mopen = 0; }
}

/* file names of a track, in the order they are tried: Ogg Vorbis first, then MP3 */
static const char *const TRACK_FMT[] = { "%strack%02d.ogg", "%strack_%02d.ogg", "%strack%02d.mp3" };

int audio_init(const Pac *pac, const char *const *dirs, int ndirs)
{
    g_pac = pac;
    for (int b = 0; b < 4; b++) { g_bank_n[b] = 0; }
    g_dir[0] = 0;
    for (int i = 0; i < ndirs; i++) {
        if (!dirs[i]) continue;
        char p[1100];
        int found = 0;
        for (size_t f = 0; f < sizeof TRACK_FMT / sizeof TRACK_FMT[0] && !found; f++) {
            snprintf(p, sizeof p, TRACK_FMT[f], dirs[i], 3);
            found = file_exists(p);
        }
        if (found) { snprintf(g_dir, sizeof g_dir, "%s", dirs[i]); break; }
    }
    if (g_dir[0]) printf("music: using %s\n", g_dir);
    else printf("music: no music/trackNN.ogg or trackNN.mp3 found (optional)\n");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { printf("audio: %s\n", SDL_GetError()); return 0; }
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    want.callback = audio_cb;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) { printf("audio: %s\n", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(g_dev, 0);
    return 1;
}

void music_play(int track)
{
    if (!g_dev || !g_dir[0] || track == g_track) return;
    char p[1100];
    SDL_LockAudioDevice(g_dev);
    music_close_locked();
    g_track = track;
    for (size_t f = 0; f < sizeof TRACK_FMT / sizeof TRACK_FMT[0] && !g_mopen; f++) {
        snprintf(p, sizeof p, TRACK_FMT[f], g_dir, track);
        if (!file_exists(p)) continue;
        if (f < 2) {                                      /* Ogg Vorbis: 44.1 kHz, any channel count is mixed to stereo */
            int err = 0;
            stb_vorbis *v = stb_vorbis_open_filename(p, &err, NULL);
            if (!v) { printf("music: cannot read %s (Vorbis error %d)\n", p, err); continue; }
            if (stb_vorbis_get_info(v).sample_rate != 44100) {
                printf("music: %s is not 44.1 kHz, skipped\n", p);
                stb_vorbis_close(v);
                continue;
            }
            g_ogg = v; g_mopen = 1;
        } else if (drmp3_init_file(&g_mp3, p, NULL)) {
            if (g_mp3.sampleRate != 44100 || g_mp3.channels != 2) drmp3_uninit(&g_mp3);   /* CD rips: 44.1k stereo */
            else g_mopen = 1;
        }
    }
    SDL_UnlockAudioDevice(g_dev);
}

void music_stop(void)
{
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    music_close_locked();
    g_track = -1;
    SDL_UnlockAudioDevice(g_dev);
}

void music_toggle(void) { g_music_on = !g_music_on; }
void music_volume(float delta)
{
    g_mvol += delta;
    if (g_mvol < 0) g_mvol = 0;
    if (g_mvol > 1) g_mvol = 1;
}
void music_set_volume(float v) { g_mvol = v < 0 ? 0 : v > 1 ? 1 : v; }
float music_get_volume(void) { return g_mvol; }
void sfx_set_volume(float v) { g_svol = v < 0 ? 0 : v > 1 ? 1 : v; }
float sfx_get_volume(void) { return g_svol; }
int  music_available(void) { return g_dev && g_dir[0]; }
int  music_playing(void) { return g_mopen; }
void sfx_toggle(void) { g_sfx_on = !g_sfx_on; }
int  sfx_enabled(void) { return g_sfx_on; }

void audio_shutdown(void)
{
    if (!g_dev) return;
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    music_close_locked();
    for (int i = 0; i < g_nsamples; i++) free(g_samples[i].pcm);
    g_nsamples = 0;
}

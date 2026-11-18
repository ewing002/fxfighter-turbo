/*
 * FX Fighter Turbo - open reimplementation
 * FMV playback (see video.h and docs/FORMATS.md, ".BRP"). Decoding follows FIGHT.EXE:
 * demuxer 0x419900 / 0x419bae, MAD1 24-bit codec 0x42ba80, stereo DPCM 0x44ef4c.
 */
#include "video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAD 32                      /* guard rows above and below the frame */

struct Video {
    uint8_t *file; size_t size;
    int w, h, vid_idx, aud_idx;
    size_t pos;                     /* next chunk */
    uint32_t *buf, *prev;           /* 0xRRGGBB, (h + 2 PAD) rows; frame starts at PAD rows */
    size_t nbuf;
    int tabA[128], tabB[16];
    uint8_t *rgb;
    int16_t *pcm; size_t pcm_frames; int rate;
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* ---- audio: BASF chunk = .asf file without its first 0x18 bytes; blocks of 34 bytes:
 * left (control + 16 bytes), right (control + 16 bytes); predictor state per channel carries over */
static void audio_decode(Video *v)
{
    size_t total = 0;
    for (size_t p = v->pos; p + 12 <= v->size; ) {
        uint32_t s = rd32(v->file + p), sz = rd32(v->file + p + 8);
        if (s == 0xffffffffu || p + 12 + sz > v->size) break;
        if ((int)s == v->aud_idx && sz >= 0x14) total += (size_t)rd32(v->file + p + 12) * 32;
        p += 12 + sz;
    }
    if (!total) return;
    v->pcm = calloc(total * 2, sizeof(int16_t));
    if (!v->pcm) return;
    int32_t last[2][2] = { { 0, 0 }, { 0, 0 } };         /* prev2, prev1 as stored (int16) */
    size_t o = 0;
    for (size_t p = v->pos; p + 12 <= v->size; ) {
        uint32_t s = rd32(v->file + p), sz = rd32(v->file + p + 8);
        if (s == 0xffffffffu || p + 12 + sz > v->size) break;
        const uint8_t *c = v->file + p + 12;
        p += 12 + sz;
        if ((int)s != v->aud_idx || sz < 0x14) continue;
        uint32_t nb = rd32(c);
        v->rate = rd16(c + 0x0c);
        const uint8_t *q = c + 0x14;
        for (uint32_t b = 0; b < nb && q + 34 <= c + sz && o + 32 <= total; b++) {
            int16_t smp[2][32];
            for (int ch = 0; ch < 2; ch++) {
                int ctrl = *q++, cl = (ctrl >> 4) + 2, pred2 = ctrl & 4;
                int32_t m2 = last[ch][0], m1 = last[ch][1];
                int32_t reg = m1;                         /* 32-bit register, not truncated in a block */
                for (int k = 0; k < 32; k++) {
                    int nib = (k & 1) ? (q[k >> 1] & 0x0f) << 4 : q[k >> 1] & 0xf0;
                    if (nib >= 128) nib -= 256;
                    uint32_t d = (uint32_t)nib << cl, sres;
                    if (pred2) {                          /* 0x44f2d9: reg*128 - prev2*64 + d, with carry */
                        uint32_t ebx = (uint32_t)reg << 7, eax = (uint32_t)(-m2) << 6;
                        uint64_t t = (uint64_t)eax + d;
                        sres = (uint32_t)t + ebx + (uint32_t)(t >> 32);
                    } else sres = ((uint32_t)reg << 6) + d;
                    reg = (int32_t)sres >> 6;
                    int16_t w16 = (int16_t)(uint16_t)reg;
                    m2 = m1; m1 = w16;
                    smp[ch][k] = w16;
                }
                q += 16;
                last[ch][0] = m2; last[ch][1] = m1;
            }
            for (int k = 0; k < 32; k++) { v->pcm[2 * (o + k)] = smp[0][k]; v->pcm[2 * (o + k) + 1] = smp[1][k]; }
            o += 32;
        }
    }
    v->pcm_frames = o;
    if (!v->rate) v->rate = 22050;
}

Video *video_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    Video *v = calloc(1, sizeof *v);
    if (!v || n < 32) { fclose(f); free(v); return NULL; }
    v->file = malloc((size_t)n);
    v->size = (size_t)n;
    if (!v->file || fread(v->file, 1, v->size, f) != v->size || memcmp(v->file, "BRPP", 4)) {
        fclose(f); video_close(v); return NULL;
    }
    fclose(f);
    uint32_t ns = rd32(v->file + 4);
    size_t p = 12;
    v->vid_idx = v->aud_idx = -1;
    for (uint32_t i = 0; i < ns && i < 16 && p + 20 <= v->size; i++) {
        const uint8_t *e = v->file + p;
        uint32_t ex = rd32(e + 16);
        if (!memcmp(e, "BVID", 4) && ex >= 16 && p + 20 + 16 <= v->size) {
            v->vid_idx = (int)rd32(e + 4);
            v->w = (int)rd32(e + 24); v->h = (int)rd32(e + 28);
            if (rd32(e + 32) != 24) v->vid_idx = -1;        /* only the 24-bit MAD1 path ships */
        } else if (!memcmp(e, "BASF", 4)) v->aud_idx = (int)rd32(e + 4);
        p += 20 + ex;
    }
    if (v->vid_idx < 0 || v->w <= 0 || v->h <= 0 || v->w > 1024 || v->h > 1024) { video_close(v); return NULL; }
    v->pos = p;
    v->nbuf = (size_t)v->w * (size_t)(v->h + 2 * PAD);
    v->buf = calloc(v->nbuf, 4); v->prev = calloc(v->nbuf, 4);
    v->rgb = malloc((size_t)v->w * v->h * 3);
    if (!v->buf || !v->prev || !v->rgb) { video_close(v); return NULL; }
    int W = v->w;
    for (int r = 0; r < 8; r++) for (int x = 0; x < 16; x++) v->tabA[r * 16 + x] = (r - 4) * W + (x - 14);    /* 0x42b9d0 */
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) v->tabB[r * 4 + c] = (-5 + 2 * r) * W + (-5 + 2 * c);   /* 0x42ba20 */
    if (v->aud_idx >= 0) audio_decode(v);
    return v;
}

void video_close(Video *v)
{
    if (!v) return;
    free(v->file); free(v->buf); free(v->prev); free(v->rgb); free(v->pcm);
    free(v);
}

int video_width(const Video *v) { return v->w; }
int video_height(const Video *v) { return v->h; }
const uint8_t *video_rgb(const Video *v) { return v->rgb; }
const int16_t *video_audio(const Video *v, size_t *frames, int *rate)
{
    *frames = v->pcm_frames; *rate = v->rate;
    return v->pcm;
}

/* ---- MAD1 (0x42ba80): ops until 0xFF, decoded in place into the persistent frame ---- */
#define NEED(k) do { if (p + (size_t)(k) > n) return 0; } while (0)
#define INB(o) ((o) >= 0 && (size_t)(o) < v->nbuf)

static int mad1(Video *v, const uint8_t *d, size_t n)
{
    int w = v->w, h = v->h, W = w;
    long base = (long)PAD * W;
    uint32_t *buf = v->buf;
    size_t p = 0;
    for (;;) {
        NEED(1);
        int op = d[p++];
        if (op == 0xff) return 1;
        if (op == 8) {                                        /* keyframe: clear, 12x12 flat blocks */
            memset(buf, 0, v->nbuf * 4);
            for (int by = 0; by + 12 <= h; by += 12)
                for (int bx = 0; bx + 12 <= w; bx += 12) {
                    NEED(3);
                    uint32_t col = (uint32_t)d[p] << 16 | (uint32_t)d[p + 1] << 8 | d[p + 2]; p += 3;
                    for (int y = 0; y < 12; y++) for (int x = 0; x < 12; x++) buf[base + (long)(by + y) * W + bx + x] = col;
                }
        } else if (op == 10) {                                /* save the frame as "previous" */
            memcpy(v->prev, buf, v->nbuf * 4);
        } else if (op == 11 || op == 13) {                    /* BxB blocks, 2-bit codes */
            NEED(1);
            int B = d[p++];
            if (B <= 0) return 0;
            const uint32_t *src = op == 11 ? v->prev : buf;
            int nbx = w / B, nby = h / B;
            size_t codes = p;
            NEED(((size_t)nbx * nby + 3) / 4);
            p += ((size_t)nbx * nby + 3) / 4;
            int idx = 0, mvx = 0, mvy = 0;
            for (int by = 0; by + B <= h; by += B)
                for (int bx = 0; bx + B <= w; bx += B) {
                    int c = (d[codes + (idx >> 2)] >> ((idx & 3) * 2)) & 3;
                    idx++;
                    if (c == 0) continue;
                    if (c == 2) {
                        NEED(3);
                        uint32_t col = (uint32_t)d[p] << 16 | (uint32_t)d[p + 1] << 8 | d[p + 2]; p += 3;
                        for (int y = 0; y < B; y++) for (int x = 0; x < B; x++) buf[base + (long)(by + y) * W + bx + x] = col;
                        continue;
                    }
                    if (c == 1) { NEED(2); mvx = d[p] - 0x80; mvy = d[p + 1] - 0x80; p += 2; }
                    else { NEED(1); int t = d[p++]; mvx += (t & 15) - 8; mvy += (t >> 4) - 8; }
                    long so = base + (long)(by + mvy) * W + bx + mvx;
                    for (int y = 0; y < B; y++) {
                        long o = base + (long)(by + y) * W + bx, s = so + (long)y * W;
                        if (!INB(s) || !INB(s + B - 1)) continue;
                        for (int x = 0; x < B; x++) buf[o + x] = src[s + x];   /* forward copy */
                    }
                }
        } else if (op == 12) {                                /* 4x4 blocks, column-major */
            int bw = (w + 3) / 4, bh = (h + 3) / 4;
            size_t bits = p;
            NEED(((size_t)bw * bh + 7) / 8);
            p += ((size_t)bw * bh + 7) / 8;
            int bi = 0, saved = 0;
            for (int cx = 0; cx < bw; cx++)
                for (int cy = 0; cy < bh; cy++) {
                    int set = (d[bits + (bi >> 3)] >> (bi & 7)) & 1;
                    bi++;
                    if (!set) continue;
                    NEED(1);
                    int M = d[p++], nibcnt = 0, nibbyte = 0;
                    for (int r = 0; r < 4; r++) {
                        int mode = (M >> (2 * r)) & 3, P = 0;
                        if (!mode) continue;
                        long o = base + (long)(cy * 4 + r) * W + cx * 4;
                        if (mode & 1) { NEED(1); P = d[p++]; }
                        if (mode == 1) saved = P; else if (mode == 2) P = saved;
                        for (int x = 0; x < 4; x++, o++) {
                            int sc = P & 3; P >>= 2;
                            if (!INB(o)) { if (sc == 3) { if (mode == 3) { if (!(nibcnt++ & 1)) { NEED(1); p++; } } else { NEED(1); if (d[p++] >= 0x80) { NEED(2); p += 2; } } } continue; }
                            if (sc == 1) { if (INB(o - 1)) buf[o] = buf[o - 1]; }
                            else if (sc == 2) { if (INB(o - W)) buf[o] = buf[o - W]; }
                            else if (sc == 3) {
                                if (mode == 3) {
                                    int k;
                                    if (nibcnt & 1) k = nibbyte >> 4;
                                    else { NEED(1); nibbyte = d[p++]; k = nibbyte & 15; }
                                    nibcnt++;
                                    if (INB(o + v->tabB[k])) buf[o] = buf[o + v->tabB[k]];
                                } else {
                                    NEED(1);
                                    int t = d[p++];
                                    if (t < 0x80) { if (INB(o + v->tabA[t])) buf[o] = buf[o + v->tabA[t]]; }
                                    else { NEED(2); buf[o] = (uint32_t)((t & 0x7f) * 2) << 16 | (uint32_t)d[p] << 8 | d[p + 1]; p += 2; }
                                }
                            }
                        }
                    }
                }
        } else if (op == 7) {                                 /* in-frame block moves (memmove) */
            NEED(1);
            int B = d[p++];
            while (B > 0) {
                NEED(2);
                int cnt = d[p] << 8 | d[p + 1]; p += 2;
                for (int i = 0; i < cnt; i++) {
                    NEED(4);
                    int x = d[p] * B, y = d[p + 1] * B, t = (d[p + 2] & 0x3f) << 8 | d[p + 3];
                    p += 4;
                    int sy = (t >> 7) + y - 0x40, sx = (t & 0x7f) + x - 0x40;
                    uint32_t tmp[64 * 64];
                    if (B > 64) return 0;
                    for (int r = 0; r < B; r++) {
                        long s = base + (long)(sy + r) * W + sx;
                        for (int c = 0; c < B; c++) tmp[r * B + c] = INB(s + c) ? buf[s + c] : 0;
                    }
                    for (int r = 0; r < B; r++) {
                        long o = base + (long)(y + r) * W + x;
                        for (int c = 0; c < B; c++) if (INB(o + c)) buf[o + c] = tmp[r * B + c];
                    }
                }
                NEED(1);
                B = d[p++];
            }
        } else return 0;                                      /* "ERROR FRAME" */
    }
}

int video_next(Video *v, uint32_t *ts_ms)
{
    while (v->pos + 12 <= v->size) {
        uint32_t s = rd32(v->file + v->pos), ts = rd32(v->file + v->pos + 4), sz = rd32(v->file + v->pos + 8);
        if (s == 0xffffffffu || v->pos + 12 + sz > v->size) return 0;
        const uint8_t *c = v->file + v->pos + 12;
        v->pos += 12 + sz;
        if ((int)s != v->vid_idx || sz < 4) continue;
        if (!memcmp(c, "PAL8", 4)) continue;                  /* the palette comes from .PAL (unused here) */
        if (memcmp(c, "MAD1", 4)) return 0;
        if (!mad1(v, c + 4, sz - 4)) return 0;
        const uint32_t *fr = v->buf + (size_t)PAD * v->w;
        for (size_t i = 0; i < (size_t)v->w * v->h; i++) {
            v->rgb[3 * i] = (uint8_t)(fr[i] >> 16); v->rgb[3 * i + 1] = (uint8_t)(fr[i] >> 8); v->rgb[3 * i + 2] = (uint8_t)fr[i];
        }
        *ts_ms = ts;
        return 1;
    }
    return 0;
}

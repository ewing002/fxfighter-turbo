/*
 * FX Fighter Turbo - open reimplementation
 * FMV playback: the .BRP files of the CD (fight/video/NAME.brp, see docs/FORMATS.md).
 * Container 'BRPP' with a 'BVID' stream (MAD1 codec, 24-bit path of FIGHT.EXE 0x42ba80) and a
 * 'BASF' stream (stereo 4-bit DPCM, 22050 Hz, decoder 0x44ef4c).
 */
#ifndef FX_VIDEO_H
#define FX_VIDEO_H
#include <stddef.h>
#include <stdint.h>

typedef struct Video Video;

Video *video_open(const char *path);     /* NULL if missing or not a BRP file */
void   video_close(Video *v);
int    video_width(const Video *v);
int    video_height(const Video *v);
/* decodes the next frame; returns 0 at the end. *ts_ms = its time stamp from the start */
int    video_next(Video *v, uint32_t *ts_ms);
const uint8_t *video_rgb(const Video *v); /* width*height*3 bytes, R G B */
/* the whole sound track, decoded at open: interleaved stereo, *frames sample frames */
const int16_t *video_audio(const Video *v, size_t *frames, int *rate);

#endif

/* BRP video check: decodes a .brp, writes the given frames as PPM and the sound track as a WAV.
 * usage: brpcheck file.brp outprefix [frame...]   (prints the frame count, size and audio length) */
#include <stdio.h>
#include <stdlib.h>
#include "../src/video.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: brpcheck file.brp outprefix [frame...]\n"); return 1; }
    Video *v = video_open(argv[1]);
    if (!v) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    int w = video_width(v), h = video_height(v), n = 0;
    uint32_t ts = 0, last = 0;
    while (video_next(v, &ts)) {
        for (int i = 3; i < argc; i++)
            if (atoi(argv[i]) == n) {
                char nm[512]; snprintf(nm, sizeof nm, "%s_%04d.ppm", argv[2], n);
                FILE *f = fopen(nm, "wb");
                if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(video_rgb(v), 1, (size_t)w * h * 3, f); fclose(f); }
            }
        last = ts; n++;
    }
    size_t frames; int rate;
    const int16_t *pcm = video_audio(v, &frames, &rate);
    printf("%dx%d, %d frames, last time stamp %u ms, audio %zu frames at %d Hz (%.2f s)\n",
           w, h, n, last, frames, rate, rate ? (double)frames / rate : 0.0);
    if (pcm && frames) {
        char nm[512]; snprintf(nm, sizeof nm, "%s.wav", argv[2]);
        FILE *f = fopen(nm, "wb");
        if (f) {
            unsigned data = (unsigned)(frames * 4), hdr[11] = { 0x46464952, 36 + data, 0x45564157, 0x20746d66, 16,
                0x00020001, (unsigned)rate, (unsigned)rate * 4, 0x00100004, 0x61746164, data };
            fwrite(hdr, 4, 11, f); fwrite(pcm, 4, frames, f); fclose(f);
        }
    }
    video_close(v);
    return 0;
}

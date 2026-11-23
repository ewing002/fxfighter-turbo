/* Music loading check (needs SDL audio; run with SDL_AUDIODRIVER=dummy for a silent test).
 * usage: audiocheck <FIGHT.PAC> <music folder> [track]
 * Opens the audio device, starts the track (Ogg Vorbis first, then MP3), lets it play for three seconds
 * (the callback decodes and loops it) and prints whether it was playing. Exit code 0 = playing. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "audio.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: audiocheck FIGHT.PAC music_dir/ [track]\n"); return 2; }
    Pac pac;
    if (!pac_open(&pac, argv[1])) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    char dir[1024]; size_t n = strlen(argv[2]);
    snprintf(dir, sizeof dir, "%s%s", argv[2], (n && (argv[2][n - 1] == '/' || argv[2][n - 1] == '\\')) ? "" : "/");
    const char *dirs[1] = { dir };
    if (!audio_init(&pac, dirs, 1)) { fprintf(stderr, "audio_init failed\n"); return 2; }
    int track = argc > 3 ? atoi(argv[3]) : 3;
    music_play(track);
    printf("available=%d playing=%d\n", music_available(), music_playing());
    SDL_Delay(3000);
    int ok = music_playing();
    printf("after 3 s: playing=%d\n", ok);
    audio_shutdown();
    return ok ? 0 : 1;
}

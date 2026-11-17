#ifndef FX_MUSIC_H
#define FX_MUSIC_H
/* dirs: candidate directories (with trailing separator) that may contain trackNN.mp3 */
int  music_init(const char *const *dirs, int ndirs);
void music_play(int track);        /* CD track number (2..14); no-op if already playing */
void music_stop(void);
void music_toggle(void);
int  music_enabled(void);
void music_volume(float delta);
int  music_available(void);
void music_shutdown(void);
#endif

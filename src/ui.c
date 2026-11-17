/*
 * FX Fighter Turbo - open reimplementation
 * 2D sprites (see ui.h).
 */
#include <SDL.h>
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_ITEMS 260
typedef struct { char name[24]; Sprite s; } Item;
static Item   g_items[MAX_ITEMS];
static int    g_nitems;
static Sprite g_back, g_title;
static const char *BOUT_NAMES[] = {
    "fight", "ko", "round", "rndnumbr", "ringout", "timeover", "winner", "youwin", "youlose",
    "suddeath", "draw", "timer", "panel", "pan_cpu", "gameover", "pause",
    "pan_pl1", "pan_pl2", "pan_cr1", "pan_cr2", "pan_cr3", "pan_cr4", "pan_cr5", "pan_cr6",
    "pan_cr7", "pan_cr8", "pan_cr9", "pan_cr10", "pan_cr11", "pan_cr12" };
#define NBOUT (int)(sizeof BOUT_NAMES / sizeof *BOUT_NAMES)
static Sprite g_bout[NBOUT];
static float  g_ox, g_oy, g_sc;

static int pot(int v) { int p = 1; while (p < v) p <<= 1; return p; }

/* indexed image -> texture (padded to power of two, nearest filtering for crisp pixel art).
 * key >= 0: that palette index is transparent. flip: image stored bottom-up. */
static Sprite make_sprite_m(const uint8_t *px, int w, int h, int stride, const Rgb *pal, int key, int flip, int mirror)
{
    Sprite s; memset(&s, 0, sizeof s);
    if (!px || w <= 0 || h <= 0) return s;
    int tw = pot(w), th = pot(h);
    uint8_t *rgba = calloc((size_t)tw * th, 4);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = px + (size_t)(flip ? h - 1 - y : y) * stride;
        for (int x = 0; x < w; x++) {
            uint8_t v = row[mirror ? w - 1 - x : x];
            uint8_t *o = rgba + ((size_t)y * tw + x) * 4;
            o[0] = pal[v].r; o[1] = pal[v].g; o[2] = pal[v].b; o[3] = (key >= 0 && v == key) ? 0 : 255;
        }
    }
    glGenTextures(1, &s.tex);
    glBindTexture(GL_TEXTURE_2D, s.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    s.w = w; s.h = h; s.u1 = (float)w / (float)tw; s.v1 = (float)h / (float)th; s.ok = 1;
    return s;
}

static Sprite make_sprite(const uint8_t *px, int w, int h, int stride, const Rgb *pal, int key, int flip)
{
    return make_sprite_m(px, w, h, stride, pal, key, flip, 0);
}

static int opaque_item(const char *n)
{
    return !strncmp(n, "PLYR", 4) || !strncmp(n, "ARENA", 5) || !strcmp(n, "MAGNON") ||
           !strcmp(n, "EMPTY") || strstr(n, "_T") == n + strlen(n) - 2;
}

static void load_bmp(const Pac *pac, const char *name, Sprite *out)
{
    size_t sz;
    const uint8_t *d = pac_get(pac, name, &sz);
    if (!d || sz < 1078 || d[0] != 'B' || d[1] != 'M') return;
    uint32_t off = d[10] | (d[11] << 8) | (d[12] << 16) | ((uint32_t)d[13] << 24);
    int32_t w = (int32_t)(d[18] | (d[19] << 8) | (d[20] << 16) | ((uint32_t)d[21] << 24));
    int32_t h = (int32_t)(d[22] | (d[23] << 8) | (d[24] << 16) | ((uint32_t)d[25] << 24));
    uint32_t hs = d[14] | (d[15] << 8);
    if (d[28] != 8 || w <= 0 || h == 0) return;
    int bottom_up = h > 0; if (h < 0) h = -h;
    Rgb pal[256];
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = d + 14 + hs + i * 4;
        if (14 + hs + i * 4 + 3 >= off) { pal[i].r = pal[i].g = pal[i].b = 0; continue; }
        pal[i].r = c[2]; pal[i].g = c[1]; pal[i].b = c[0];
    }
    int stride = (w + 3) & ~3;
    if (off + (size_t)stride * h > sz) return;
    *out = make_sprite(d + off, w, h, stride, pal, -1, bottom_up);
}

void ui_init(const Pac *pac)
{
    size_t sz;
    static Pixmap pm[MAX_ITEMS];
    const uint8_t *d = pac_get(pac, "fight/frontend/align.pix", &sz);
    int n = d ? pix_load_all(d, sz, pm, MAX_ITEMS) : 0;
    g_nitems = 0;
    for (int i = 0; i < n; i++) {
        Pixmap *p = &pm[i];
        if (g_nitems < MAX_ITEMS && p->has_pal) {
            Item *it = &g_items[g_nitems++];
            snprintf(it->name, sizeof it->name, "%s", p->name);
            int rot = !strcmp(p->name, "VS");          /* stored rotated by 180 degrees, not just flipped */
            it->s = make_sprite_m(p->pixels, p->width, p->height, p->row_bytes, p->pal,
                                  opaque_item(p->name) ? -1 : p->pixels[0], !rot, rot);
        }
        pix_free(p);
    }
    d = pac_get(pac, "fight/frontend/back1.pix", &sz);
    if (d && pix_load_all(d, sz, pm, 1) == 1) {
        g_back = make_sprite(pm[0].pixels, pm[0].width, pm[0].height, pm[0].row_bytes, pm[0].pal, -1, 0);
        pix_free(&pm[0]);
    }
    load_bmp(pac, "fight/fxscreen.bmp", &g_title);
}

void ui_load_bout(const Pac *pac, const Rgb *pal)
{
    for (int i = 0; i < NBOUT; i++) {
        if (g_bout[i].ok) glDeleteTextures(1, &g_bout[i].tex);
        memset(&g_bout[i], 0, sizeof g_bout[i]);
        char path[64]; size_t sz; Pixmap pm;
        snprintf(path, sizeof path, "fight/boutgfx/%s.pix", BOUT_NAMES[i]);
        const uint8_t *d = pac_get(pac, path, &sz);
        if (d && pix_load_all(d, sz, &pm, 1) == 1) {
            g_bout[i] = make_sprite(pm.pixels, pm.width, pm.height, pm.row_bytes, pal, pm.pixels[0], 0);
            pix_free(&pm);
        }
    }
}

const Sprite *ui_sprite(const char *name)
{
    for (int i = 0; i < g_nitems; i++) if (!strcmp(g_items[i].name, name)) return &g_items[i].s;
    return NULL;
}

const Sprite *ui_bout(const char *name)
{
    for (int i = 0; i < NBOUT; i++) if (!strcmp(BOUT_NAMES[i], name)) return g_bout[i].ok ? &g_bout[i] : NULL;
    return NULL;
}

const Sprite *ui_back(void) { return g_back.ok ? &g_back : NULL; }
const Sprite *ui_title(void) { return g_title.ok ? &g_title : NULL; }

/* ------------------------------------------------------------------ */
void ui_begin(int w, int h)
{
    g_sc = (float)h / 480.0f;
    if (640.0f * g_sc > (float)w) g_sc = (float)w / 640.0f;
    g_ox = ((float)w - 640.0f * g_sc) * 0.5f;
    g_oy = ((float)h - 480.0f * g_sc) * 0.5f;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
    glTranslatef(g_ox, g_oy, 0); glScalef(g_sc, g_sc, 1);
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void ui_end(void)
{
    glDisable(GL_BLEND); glDisable(GL_TEXTURE_2D); glEnable(GL_DEPTH_TEST);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

static void quad(const Sprite *s, float u0, float u1, float x, float y, float w, float h, float a)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glColor4f(1, 1, 1, a);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, 0);     glVertex2f(x, y);
    glTexCoord2f(u1, 0);     glVertex2f(x + w, y);
    glTexCoord2f(u1, s->v1); glVertex2f(x + w, y + h);
    glTexCoord2f(u0, s->v1); glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void ui_draw(const Sprite *s, float x, float y, float sc, float a)
{
    if (s && s->ok) quad(s, 0, s->u1, x, y, (float)s->w * sc, (float)s->h * sc, a);
}

void ui_draw_c(const Sprite *s, float cx, float cy, float sc, float a)
{
    if (s && s->ok) ui_draw(s, cx - (float)s->w * sc * 0.5f, cy - (float)s->h * sc * 0.5f, sc, a);
}

void ui_draw_part(const Sprite *s, int sx0, int sx1, float x, float y, float sc, float a)
{
    if (!s || !s->ok || sx1 <= sx0) return;
    float tw = (float)s->w / s->u1;
    quad(s, (float)sx0 / tw, (float)sx1 / tw, x, y, (float)(sx1 - sx0) * sc, (float)s->h * sc, a);
}

void ui_rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

void ui_frame(float x0, float y0, float x1, float y1, float t, float r, float g, float b, float a)
{
    ui_rect(x0, y0, x1, y0 + t, r, g, b, a);
    ui_rect(x0, y1 - t, x1, y1, r, g, b, a);
    ui_rect(x0, y0, x0 + t, y1, r, g, b, a);
    ui_rect(x1 - t, y0, x1, y1, r, g, b, a);
}

static const Sprite *glyph(char c)
{
    char n[4] = { 0 };
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') { n[0] = c; return ui_sprite(n); }
    if (c >= '0' && c <= '9') { char m[4] = { c, '_', 'T', 0 }; return ui_sprite(m); }
    return NULL;
}

float ui_text_width(const char *s, float sc)
{
    float w = 0;
    for (; *s; s++) { const Sprite *g = glyph(*s); w += (g ? (float)g->w : 10.f) * sc; }
    return w;
}

float ui_text(const char *s, float x, float y, float sc, float a)
{
    for (; *s; s++) {
        const Sprite *g = glyph(*s);
        if (g) { ui_draw(g, x, y, sc, a); x += (float)g->w * sc; }
        else x += 10.f * sc;
    }
    return x;
}

/* glyph columns measured from the sprites (empty columns between digits) */
int ui_timer_digit_x(int d, int *x0, int *x1)
{
    static const int c[11][2] = { {0,23},{26,39},{48,71},{72,95},{96,119},{120,143},{144,166},{168,191},{192,214},{216,238},{240,264} };
    if (d < 0 || d > 10) return 0;
    *x0 = c[d][0]; *x1 = c[d][1]; return 1;
}

int ui_round_digit_x(int d, int *x0, int *x1)
{
    static const int c[10][2] = { {0,26},{30,44},{50,76},{77,103},{104,130},{131,157},{158,184},{185,211},{212,238},{239,265} };
    if (d < 0 || d > 9) return 0;
    *x0 = c[d][0]; *x1 = c[d][1]; return 1;
}

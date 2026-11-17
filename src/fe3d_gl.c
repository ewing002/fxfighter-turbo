/*
 * FX Fighter Turbo - open reimplementation
 * 3D front end renderer (see fe3d_gl.h)
 *
 * The original (FUN_00412a7a / FUN_004135f1) renders into an 8-bit 640x480 screen with the
 * demo.pal palette: back1.pix is copied first, then RIGHT_WORLD and LEFT_WORLD into the two
 * side panels (parallel cameras), then THE_WORLD over the whole screen (perspective camera).
 */
#include "fe3d_gl.h"
#include <SDL_opengl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static GLuint g_tex[FE_MAX_MAPS], g_back;

static GLuint upload(const Pixmap *pm, const Rgb *pal)
{
    int w = pm->width, h = pm->height;
    uint8_t *rgba = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t v = pm->pixels[y * pm->row_bytes + x];
            Rgb c = pal[v];
            uint8_t *o = rgba + ((size_t)y * w + x) * 4;
            o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = 255;
        }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    return t;
}

void fe_gl_init(const FeAssets *fe)
{
    /* every map is drawn with the screen palette, as on the original 8-bit screen */
    for (int i = 0; i < fe->nmaps; i++) g_tex[i] = fe->maps[i].pixels ? upload(&fe->maps[i], fe->pal) : 0;
    g_back = fe->back.pixels ? upload(&fe->back, fe->pal) : 0;
}

void fe_gl_free(void)
{
    for (int i = 0; i < FE_MAX_MAPS; i++) if (g_tex[i]) { glDeleteTextures(1, &g_tex[i]); g_tex[i] = 0; }
    if (g_back) { glDeleteTextures(1, &g_back); g_back = 0; }
}

/* ---------------- matrices (BRender row-vector 4x3) ---------------- */
typedef float M43[4][3];

static void m_ident(M43 m) { memset(m, 0, sizeof(M43)); m[0][0] = m[1][1] = m[2][2] = 1; }

static void m_mul(M43 out, M43 a, M43 b)     /* out = a * b (apply a, then b) */
{
    M43 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
            if (i == 3) r[i][j] += b[3][j];
        }
    memcpy(out, r, sizeof r);
}

static void m_rot(M43 m, int axis, float deg)  /* BrMatrix34RotateX/Y/Z */
{
    float a = deg / 57.29578f, c = cosf(a), s = sinf(a);
    m_ident(m);
    if (axis == 0) { m[1][1] = c; m[1][2] = s; m[2][1] = -s; m[2][2] = c; }
    if (axis == 1) { m[0][0] = c; m[0][2] = -s; m[2][0] = s; m[2][2] = c; }
    if (axis == 2) { m[0][0] = c; m[0][1] = s; m[1][0] = -s; m[1][1] = c; }
}

/* FUN_00414900 + FUN_00414f36: RotY * PostRotX * PostRotZ, translation, PreScale */
static void node_matrix(const FeNode *n, M43 out)
{
    M43 ry, rx, rz, s;
    m_rot(ry, 1, n->rot[1]); m_rot(rx, 0, n->rot[0]); m_rot(rz, 2, n->rot[2]);
    m_mul(out, ry, rx);
    m_mul(out, out, rz);
    out[3][0] = n->pos[0]; out[3][1] = n->pos[1]; out[3][2] = n->pos[2];
    m_ident(s); s[0][0] = n->scale[0]; s[1][1] = n->scale[1]; s[2][2] = n->scale[2];
    m_mul(out, s, out);
}

static void world_matrix(const FeScene *sc, int i, M43 out)
{
    node_matrix(&sc->n[i], out);
    for (int p = sc->n[i].parent, guard = 0; p >= 0 && guard < 32; p = sc->n[p].parent, guard++) {
        M43 pm; node_matrix(&sc->n[p], pm);
        m_mul(out, out, pm);
    }
}

static void gl_load_br(M43 m)
{
    GLfloat g[16] = { m[0][0], m[0][1], m[0][2], 0,  m[1][0], m[1][1], m[1][2], 0,
                      m[2][0], m[2][1], m[2][2], 0,  m[3][0], m[3][1], m[3][2], 1 };
    glMultMatrixf(g);
}

static int find_mat(const FeAssets *fe, const char *name)
{
    for (int i = 0; i < fe->nmats; i++) if (!strcmp(fe->mats[i].name, name)) return i;
    return -1;
}

static void draw_model(const FeAssets *fe, const Model *m)
{
    for (int i = 0; i < m->nfaces; i++) {
        const Face *f = &m->faces[i];
        if (f->v[0] >= m->nverts || f->v[1] >= m->nverts || f->v[2] >= m->nverts) continue;
        int mi = (f->mat && f->mat <= m->nmats) ? find_mat(fe, m->mats[f->mat - 1]) : -1;
        int tex = mi >= 0 && fe->mat_map[mi] >= 0 && m->uvs ? fe->mat_map[mi] : -1;
        if (tex >= 0 && g_tex[tex]) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, g_tex[tex]);
            glColor3f(1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            if (mi >= 0) glColor3ub(fe->mats[mi].r, fe->mats[mi].g, fe->mats[mi].b);
            else glColor3f(0.6f, 0.6f, 0.6f);
        }
        const Vec3 *a = &m->verts[f->v[0]], *b = &m->verts[f->v[1]], *c = &m->verts[f->v[2]];
        float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
        glBegin(GL_TRIANGLES);
        glNormal3f(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx);
        for (int k = 0; k < 3; k++) {
            int vi = f->v[k];
            if (tex >= 0) glTexCoord2f(m->uvs[vi].u, m->uvs[vi].v);
            glVertex3f(m->verts[vi].x, m->verts[vi].y, m->verts[vi].z);
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

static void draw_world(const FeAssets *fe, const FeScene *sc, int world)
{
    /* one directional light per world: RotateY(45) then 45 degrees about Z (FUN_00412a7a) */
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL); glEnable(GL_NORMALIZE);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    GLfloat lp[4] = { 0.5f, 0.5f, 0.7071f, 0 }, amb[4] = { 0.5f, 0.5f, 0.5f, 1 }, dif[4] = { 0.6f, 0.6f, 0.6f, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, lp);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    for (int i = 0; i < sc->count; i++) {
        const FeNode *n = &sc->n[i];
        if (n->world != world || n->hidden) continue;
        int hid = 0;
        for (int p = n->parent, g = 0; p >= 0 && g < 32; p = sc->n[p].parent, g++) if (sc->n[p].hidden) hid = 1;
        const Model *m = fe_type_model(fe, n->type);
        if (hid || !m) continue;
        M43 w; world_matrix(sc, i, w);
        glPushMatrix();
        gl_load_br(w);
        draw_model(fe, m);
        glPopMatrix();
    }
    glDisable(GL_LIGHTING);
}

void fe_gl_render(const FeAssets *fe, const FeScene *sc, int w, int h)
{
    float s = (float)h / 480.f;
    if (640.f * s > (float)w) s = (float)w / 640.f;
    float ox = ((float)w - 640.f * s) * 0.5f, oy = ((float)h - 480.f * s) * 0.5f;
#define VP(x, y, ww, hh) glViewport((GLint)(ox + (x) * s), (GLint)((float)h - oy - ((y) + (hh)) * s), (GLsizei)((ww) * s), (GLsizei)((hh) * s))

    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);

    /* back1.pix over the 640x480 area */
    VP(0, 0, 640, 480);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    if (g_back) {
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g_back); glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(0, 0); glTexCoord2f(1, 0); glVertex2f(1, 0);
        glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(0, 1);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    glEnable(GL_DEPTH_TEST);

    /* side panels: parallel cameras at (+-0.19, 0, 0) looking down -Z, hither 0.2, yon 20;
     * the 0.378 high view fills the 442 pixel panel with square pixels */
    static const float px[2] = { 496, 30 }, cx[2] = { 0.19f, -0.19f };
    for (int k = 0; k < 2; k++) {
        VP(px[k], 17, 114, 442);
        glClear(GL_DEPTH_BUFFER_BIT);
        float hh = 0.378f * 0.5f, hw = hh * 114.f / 442.f;
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-hw, hw, -hh, hh, 0.2f, 20.f);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glTranslatef(-cx[k], 0, 0);
        draw_world(fe, sc, 1 + k);
    }

    /* main view: perspective, 45 degrees, aspect 1.46 (as the original, whatever the screen),
     * hither 0.1, yon 50, camera at (0, 0, 0.2) looking down -Z */
    VP(0, 0, 640, 480);
    glClear(GL_DEPTH_BUFFER_BIT);
    float zn = 0.1f, th = tanf(22.5f / 57.29578f), aspect = 1.46f;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-zn * th * aspect, zn * th * aspect, -zn * th, zn * th, zn, 50.f);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glTranslatef(0, 0, -0.2f);
    draw_world(fe, sc, 0);
    glViewport(0, 0, w, h);
#undef VP
}

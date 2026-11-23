/* FX Fighter Turbo launcher (Windows).
 *
 * A small Win32 program that sits next to fxfighter.exe and edits the game's settings file
 * (%APPDATA%\FXFighter\Turbo\options.cfg) with ordinary controls: resolution, fullscreen, volumes, match rules,
 * key bindings and detail levels. "Play" saves the settings and starts fxfighter.exe with the chosen FIGHT.PAC.
 * It does not read any game data itself. The window opens in the middle of the desktop.
 *
 * options.cfg is a list of key=value lines. The launcher keeps every line it does not know and only changes the
 * values it shows, so it can be used together with the in-game menus and hotkeys.
 * The folder can be redirected with the environment variable FX_PREFDIR (as in the game; used by tests).
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ---------------------------------------------------------------------------------------------- */
/* settings model: a list of key=value lines, as the game writes them                              */
/* ---------------------------------------------------------------------------------------------- */
#define MAXKV 128
static char g_k[MAXKV][48];
static int  g_v[MAXKV];
static int  g_n;

static int kv_find(const char *k) { for (int i = 0; i < g_n; i++) if (!strcmp(g_k[i], k)) return i; return -1; }
static int kv_get(const char *k, int def) { int i = kv_find(k); return i < 0 ? def : g_v[i]; }
static void kv_set(const char *k, int v)
{
    int i = kv_find(k);
    if (i < 0) { if (g_n >= MAXKV) return; i = g_n++; strncpy(g_k[i], k, sizeof g_k[i] - 1); g_k[i][sizeof g_k[i] - 1] = 0; }
    g_v[i] = v;
}

/* defaults = the game's built-in defaults (g_cfg and g_keys in src/main.c) */
static const struct { const char *k; int v; } DEFAULTS[] = {
    { "fullscreen", 0 }, { "vsync", 1 }, { "music", 6 }, { "sfx", 9 }, { "rounds", 3 }, { "round_time", 60 },
    { "ringouts", 1 }, { "rumble", 1 }, { "difficulty", 4 }, { "handicap1", 4 }, { "handicap2", 4 }, { "speed", 1 },
    { "ring_guard", 1 }, { "input_buffer", 4 }, { "quick_continue", 0 }, { "challenger", 1 },
    { "detail1", 1 }, { "detail2", 1 }, { "detail3", 1 }, { "detail4", 1 }, { "detail5", 2 }, { "detail6", 2 }, { "detail7", 1 },
    { "window_w", 1280 }, { "window_h", 960 }, { "fullscreen_w", 0 }, { "fullscreen_h", 0 },
    { "key1", 0x20 }, { "key2", 0x1e }, { "key3", 0x11 }, { "key4", 0x1f }, { "key5", 0x21 }, { "key6", 0x22 },
    { "key7", 0xcb }, { "key8", 0xcd }, { "key9", 0xc8 }, { "key10", 0xd0 }, { "key11", 0x25 }, { "key12", 0x26 },
};
#define NDEF ((int)(sizeof DEFAULTS / sizeof DEFAULTS[0]))

static int dflt(const char *k) { for (int i = 0; i < NDEF; i++) if (!strcmp(DEFAULTS[i].k, k)) return DEFAULTS[i].v; return 0; }
static int cget(const char *k) { return kv_get(k, dflt(k)); }

static wchar_t g_pref_dir[MAX_PATH];     /* ends with '\' */
static wchar_t g_exe_dir[MAX_PATH];      /* folder of the launcher, ends with '\' */
static wchar_t g_pac[MAX_PATH];          /* chosen FIGHT.PAC */

static void pref_init(void)
{
    wchar_t env[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"FX_PREFDIR", env, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        wcscpy(g_pref_dir, env);
    } else {
        wchar_t ap[MAX_PATH] = L"";
        SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ap);
        swprintf(g_pref_dir, MAX_PATH, L"%ls\\FXFighter\\Turbo", ap);
    }
    size_t l = wcslen(g_pref_dir);
    if (l && g_pref_dir[l - 1] != L'\\' && g_pref_dir[l - 1] != L'/') { g_pref_dir[l] = L'\\'; g_pref_dir[l + 1] = 0; }
    SHCreateDirectoryExW(NULL, g_pref_dir, NULL);
}

static void pref_path(wchar_t *out, const wchar_t *name) { swprintf(out, MAX_PATH, L"%ls%ls", g_pref_dir, name); }

static void cfg_load(void)
{
    g_n = 0;
    wchar_t p[MAX_PATH]; pref_path(p, L"options.cfg");
    FILE *f = _wfopen(p, L"r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        kv_set(line, atoi(eq + 1));
    }
    fclose(f);
}

static int cfg_save(void)
{
    wchar_t p[MAX_PATH]; pref_path(p, L"options.cfg");
    FILE *f = _wfopen(p, L"w");
    if (!f) return 0;
    for (int i = 0; i < g_n; i++) fprintf(f, "%s=%d\n", g_k[i], g_v[i]);
    fclose(f);
    return 1;
}

/* launcher's own setting: which FIGHT.PAC to use */
static void pac_load(void)
{
    wchar_t p[MAX_PATH]; pref_path(p, L"launcher.cfg");
    FILE *f = _wfopen(p, L"rb");
    if (f) {
        char b[2 * MAX_PATH]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
        char *e = b + strlen(b); while (e > b && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        if (!strncmp(b, "pac=", 4)) MultiByteToWideChar(CP_UTF8, 0, b + 4, -1, g_pac, MAX_PATH);
    }
    if (g_pac[0] && GetFileAttributesW(g_pac) != INVALID_FILE_ATTRIBUTES) return;
    g_pac[0] = 0;
    static const wchar_t *rel[] = { L"FIGHT.PAC", L"..\\FIGHT.PAC", L"..\\..\\FIGHT.PAC" };   /* as the game searches */
    for (int i = 0; i < 3; i++) {
        wchar_t c[MAX_PATH]; swprintf(c, MAX_PATH, L"%ls%ls", g_exe_dir, rel[i]);
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) { GetFullPathNameW(c, MAX_PATH, g_pac, NULL); return; }
    }
}

static void pac_save(void)
{
    wchar_t p[MAX_PATH]; pref_path(p, L"launcher.cfg");
    FILE *f = _wfopen(p, L"wb");
    if (!f) return;
    char u[2 * MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, g_pac, -1, u, sizeof u, NULL, NULL);
    fprintf(f, "pac=%s\n", u);
    fclose(f);
}

/* ---------------------------------------------------------------------------------------------- */
/* user interface                                                                                   */
/* ---------------------------------------------------------------------------------------------- */
enum {
    ID_BROWSE = 100, ID_PLAY, ID_SAVE, ID_RESET, ID_CLOSE, ID_TAB, ID_PACEDIT, ID_STATUS,
    ID_FULL, ID_VSYNC, ID_WINSIZE, ID_FSRES, ID_MUSIC, ID_SFX, ID_MUSICV, ID_SFXV,
    ID_ROUNDS, ID_ROUNDTIME, ID_DIFF, ID_SPEED, ID_HC1, ID_HC2,
    ID_RINGOUTS, ID_RINGGUARD, ID_CHALLENGER, ID_QUICK, ID_RUMBLE, ID_IBUF,
    ID_KEYRESET, ID_DET_P1TEX, ID_DET_P1SHADE, ID_DET_P2TEX, ID_DET_P2SHADE, ID_DET_FLOORTEX, ID_DET_FLOOR, ID_DET_BG,
    ID_KEY0 = 300,                         /* ID_KEY0 + 0..11: key buttons */
};
#define NPAGES 4
#define MAXCTL 160

static HWND g_wnd, g_tab;
static HFONT g_font, g_bold;
static HWND g_ctl[NPAGES][MAXCTL];
static int  g_nctl[NPAGES];
static int  g_page;
static int  g_capture = -1;                /* key button waiting for a key press */
static int  g_dpi = 96;
static HANDLE g_game_proc;                 /* the started game, while the launcher hands over the foreground */
static DWORD  g_game_pid;
static int    g_game_wait;
static HBRUSH g_bg_brush;                  /* the colour of the tab page, used for the whole window so that labels blend in */
static COLORREF g_bg_color;

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static HWND mk(int page, const wchar_t *cls, const wchar_t *txt, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (page >= 0 && g_nctl[page] < MAXCTL) g_ctl[page][g_nctl[page]++] = c;
    return c;
}
static HWND label(int page, const wchar_t *t, int x, int y, int w) { return mk(page, L"STATIC", t, SS_LEFT, x, y + 3, w, 20, 0); }
static HWND group(int page, const wchar_t *t, int x, int y, int w, int h) { return mk(page, L"BUTTON", t, BS_GROUPBOX, x, y, w, h, 0); }
static HWND check(int page, const wchar_t *t, int x, int y, int w, int id) { return mk(page, L"BUTTON", t, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, w, 22, id); }
static HWND combo(int page, int x, int y, int w, int id) { return mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 300, id); }
static HWND slider(int page, int x, int y, int w, int id, int lo, int hi)
{
    HWND t = mk(page, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, x, y, w, 30, id);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELONG(lo, hi));
    SendMessageW(t, TBM_SETTICFREQ, 1, 0);
    return t;
}

static HWND C(int id) { return GetDlgItem(g_wnd, id); }
static int  cb_add(int id, const wchar_t *t, int data)
{
    HWND c = C(id);
    int i = (int)SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(c, CB_SETITEMDATA, i, data);
    return i;
}
static void cb_select_data(int id, int data)
{
    HWND c = C(id); int n = (int)SendMessageW(c, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) if ((int)SendMessageW(c, CB_GETITEMDATA, i, 0) == data) { SendMessageW(c, CB_SETCURSEL, i, 0); return; }
    SendMessageW(c, CB_SETCURSEL, n ? 0 : -1, 0);
}
static int cb_data(int id, int def)
{
    HWND c = C(id); int i = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
    return i < 0 ? def : (int)SendMessageW(c, CB_GETITEMDATA, i, 0);
}
static void set_check(int id, int v) { SendMessageW(C(id), BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }
static int  get_check(int id) { return SendMessageW(C(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }

static const int WIN_SIZES[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 400 }, { 640, 480 }, { 800, 600 }, { 960, 720 }, { 1024, 768 }, { 1280, 720 }, { 1280, 960 },
    { 1600, 900 }, { 1600, 1200 }, { 1920, 1080 }, { 1920, 1440 }, { 2560, 1440 }, { 3440, 1440 }, { 3840, 2160 } };

static void add_res(int id, int w, int h)
{
    wchar_t t[48]; swprintf(t, 48, L"%d x %d", w, h);
    cb_add(id, t, (w << 16) | h);
}

/* the display modes of the primary monitor, unique width x height, ascending */
static int list_modes(int out[][2], int max)
{
    int n = 0;
    DEVMODEW dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
    for (int i = 0; EnumDisplaySettingsW(NULL, i, &dm) && n < max; i++) {
        if ((int)dm.dmPelsWidth < 320 || (int)dm.dmPelsHeight < 200) continue;
        int dup = 0;
        for (int j = 0; j < n; j++) if (out[j][0] == (int)dm.dmPelsWidth && out[j][1] == (int)dm.dmPelsHeight) dup = 1;
        if (!dup) { out[n][0] = (int)dm.dmPelsWidth; out[n][1] = (int)dm.dmPelsHeight; n++; }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if ((long)out[j][0] * out[j][1] < (long)out[i][0] * out[i][1]) {
                int a = out[i][0], b = out[i][1]; out[i][0] = out[j][0]; out[i][1] = out[j][1]; out[j][0] = a; out[j][1] = b;
            }
    return n;
}

static const wchar_t *KEY_LABEL[6] = { L"Forward", L"Back", L"Jump", L"Duck", L"Punch", L"Kick" };

static int key_value[12];                  /* DirectInput scan codes being edited */

static void key_button_text(int i)
{
    wchar_t name[64] = L"?";
    int di = key_value[i];
    LONG lp = ((di & 0x7f) << 16) | ((di & 0x80) ? (1 << 24) : 0);
    if (!GetKeyNameTextW(lp, name, 64)) swprintf(name, 64, L"scan code %d", di);
    SetWindowTextW(C(ID_KEY0 + i), g_capture == i ? L"Press a key ... (Esc cancels)" : name);
}

static void ui_from_cfg(void)
{
    set_check(ID_FULL, cget("fullscreen")); set_check(ID_VSYNC, cget("vsync"));
    {   /* window size: list entry, or the current custom size as an extra entry */
        int w = cget("window_w"), h = cget("window_h"), have = 0;
        HWND c = C(ID_WINSIZE); int n = (int)SendMessageW(c, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < n; i++) if ((int)SendMessageW(c, CB_GETITEMDATA, i, 0) == ((w << 16) | h)) have = 1;
        if (!have) add_res(ID_WINSIZE, w, h);
        cb_select_data(ID_WINSIZE, (w << 16) | h);
    }
    {
        int w = cget("fullscreen_w"), h = cget("fullscreen_h"), have = 0;
        HWND c = C(ID_FSRES); int n = (int)SendMessageW(c, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < n; i++) if ((int)SendMessageW(c, CB_GETITEMDATA, i, 0) == ((w << 16) | h)) have = 1;
        if (!have) add_res(ID_FSRES, w, h);
        cb_select_data(ID_FSRES, (w << 16) | h);
    }
    SendMessageW(C(ID_MUSIC), TBM_SETPOS, TRUE, cget("music")); SendMessageW(C(ID_SFX), TBM_SETPOS, TRUE, cget("sfx"));
    cb_select_data(ID_ROUNDS, cget("rounds")); cb_select_data(ID_ROUNDTIME, cget("round_time"));
    cb_select_data(ID_DIFF, cget("difficulty")); cb_select_data(ID_SPEED, cget("speed"));
    cb_select_data(ID_HC1, cget("handicap1")); cb_select_data(ID_HC2, cget("handicap2"));
    set_check(ID_RINGOUTS, cget("ringouts")); set_check(ID_RINGGUARD, cget("ring_guard"));
    set_check(ID_CHALLENGER, cget("challenger")); set_check(ID_QUICK, cget("quick_continue")); set_check(ID_RUMBLE, cget("rumble"));
    cb_select_data(ID_IBUF, cget("input_buffer"));
    for (int i = 0; i < 12; i++) { char k[16]; snprintf(k, sizeof k, "key%d", i + 1); key_value[i] = cget(k); key_button_text(i); }
    set_check(ID_DET_P1TEX, cget("detail1")); set_check(ID_DET_P1SHADE, cget("detail2"));
    set_check(ID_DET_P2TEX, cget("detail3")); set_check(ID_DET_P2SHADE, cget("detail4"));
    cb_select_data(ID_DET_FLOORTEX, cget("detail5")); cb_select_data(ID_DET_FLOOR, cget("detail6"));
    set_check(ID_DET_BG, cget("detail7"));
    for (int id = ID_MUSIC; id <= ID_SFX; id++) {
        wchar_t t[16]; swprintf(t, 16, L"%d", (int)SendMessageW(C(id), TBM_GETPOS, 0, 0));
        SetWindowTextW(C(id == ID_MUSIC ? ID_MUSICV : ID_SFXV), t);
    }
}

static void cfg_from_ui(void)
{
    kv_set("fullscreen", get_check(ID_FULL)); kv_set("vsync", get_check(ID_VSYNC));
    { int d = cb_data(ID_WINSIZE, (1280 << 16) | 960); kv_set("window_w", d >> 16); kv_set("window_h", d & 0xffff); }
    { int d = cb_data(ID_FSRES, 0); kv_set("fullscreen_w", d >> 16); kv_set("fullscreen_h", d & 0xffff); }
    kv_set("music", (int)SendMessageW(C(ID_MUSIC), TBM_GETPOS, 0, 0)); kv_set("sfx", (int)SendMessageW(C(ID_SFX), TBM_GETPOS, 0, 0));
    kv_set("rounds", cb_data(ID_ROUNDS, 3)); kv_set("round_time", cb_data(ID_ROUNDTIME, 60));
    kv_set("difficulty", cb_data(ID_DIFF, 4)); kv_set("speed", cb_data(ID_SPEED, 1));
    kv_set("handicap1", cb_data(ID_HC1, 4)); kv_set("handicap2", cb_data(ID_HC2, 4));
    kv_set("ringouts", get_check(ID_RINGOUTS)); kv_set("ring_guard", get_check(ID_RINGGUARD));
    kv_set("challenger", get_check(ID_CHALLENGER)); kv_set("quick_continue", get_check(ID_QUICK)); kv_set("rumble", get_check(ID_RUMBLE));
    kv_set("input_buffer", cb_data(ID_IBUF, 4));
    for (int i = 0; i < 12; i++) { char k[16]; snprintf(k, sizeof k, "key%d", i + 1); kv_set(k, key_value[i]); }
    kv_set("detail1", get_check(ID_DET_P1TEX)); kv_set("detail2", get_check(ID_DET_P1SHADE));
    kv_set("detail3", get_check(ID_DET_P2TEX)); kv_set("detail4", get_check(ID_DET_P2SHADE));
    kv_set("detail5", cb_data(ID_DET_FLOORTEX, 2)); kv_set("detail6", cb_data(ID_DET_FLOOR, 2));
    kv_set("detail7", get_check(ID_DET_BG));
}

static int file_exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

/* a short report on what the game will find next to FIGHT.PAC */
static void update_status(void)
{
    wchar_t t[512];
    if (!g_pac[0]) { SetWindowTextW(C(ID_STATUS), L"FIGHT.PAC not found. Use Browse ... to select it (from your own copy of the original game)."); return; }
    wchar_t dir[MAX_PATH]; wcscpy(dir, g_pac);
    wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0;
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%lsFIGHT.EXE", dir); int exe = file_exists(p);
    int music = 0, video = 0;
    const wchar_t *mdir[] = { L"%lsmusic\\track03.ogg", L"%lsmusic\\track03.mp3", L"%lsport\\music\\track03.ogg", L"%lsport\\music\\track03.mp3",
                              L"%ls..\\music\\track03.ogg", L"%ls..\\music\\track03.mp3" };
    for (int i = 0; i < 6; i++) { swprintf(p, MAX_PATH, mdir[i], dir); if (file_exists(p)) music = 1; }
    for (int i = 0; i < 6; i++) { swprintf(p, MAX_PATH, mdir[i], g_exe_dir); if (file_exists(p)) music = 1; }
    const wchar_t *vdir[] = { L"%lsvideo\\fxintro.brp", L"%lsport\\video\\fxintro.brp", L"%lsFIGHT\\VIDEO\\fxintro.brp", L"%ls..\\video\\fxintro.brp" };
    for (int i = 0; i < 4; i++) { swprintf(p, MAX_PATH, vdir[i], dir); if (file_exists(p)) video = 1; }
    for (int i = 0; i < 4; i++) { swprintf(p, MAX_PATH, vdir[i], g_exe_dir); if (file_exists(p)) video = 1; }
    swprintf(t, 512, L"FIGHT.EXE (needed for the menus): %ls     Music: %ls     Videos: %ls",
             exe ? L"found" : L"MISSING", music ? L"found" : L"not found (optional)", video ? L"found" : L"not found (optional)");
    SetWindowTextW(C(ID_STATUS), t);
}

static void show_page(int pg)
{
    g_page = pg;
    for (int p = 0; p < NPAGES; p++)
        for (int i = 0; i < g_nctl[p]; i++) ShowWindow(g_ctl[p][i], p == pg ? SW_SHOW : SW_HIDE);
}

static void end_capture(void)
{
    int c = g_capture; g_capture = -1;
    if (c >= 0) key_button_text(c);
}

static int do_save(int quiet)
{
    cfg_from_ui();
    pac_save();
    if (!cfg_save()) { MessageBoxW(g_wnd, L"The settings file could not be written.", L"FX Fighter Turbo", MB_ICONERROR); return 0; }
    if (!quiet) SetWindowTextW(C(ID_STATUS), L"Settings saved.");
    return 1;
}

static void do_play(void)
{
    if (!g_pac[0]) { MessageBoxW(g_wnd, L"Select FIGHT.PAC first (Browse ...).", L"FX Fighter Turbo", MB_ICONINFORMATION); return; }
    if (!do_save(1)) return;
    wchar_t exe[MAX_PATH], cmd[2 * MAX_PATH + 8];
    swprintf(exe, MAX_PATH, L"%lsfxfighter.exe", g_exe_dir);
    if (!file_exists(exe)) { MessageBoxW(g_wnd, L"fxfighter.exe was not found next to the launcher.", L"FX Fighter Turbo", MB_ICONERROR); return; }
    swprintf(cmd, 2 * MAX_PATH + 8, L"\"%ls\" \"%ls\"", exe, g_pac);
    STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, g_exe_dir, &si, &pi)) {
        MessageBoxW(g_wnd, L"The game could not be started.", L"FX Fighter Turbo", MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    AllowSetForegroundWindow(pi.dwProcessId);
    /* stay in front until the game's window is there, then hand the foreground over to it and quit
     * (a program that is not in the foreground may not bring another window to the front) */
    g_game_proc = pi.hProcess; g_game_pid = pi.dwProcessId; g_game_wait = 0;
    EnableWindow(C(ID_PLAY), FALSE);
    SetWindowTextW(C(ID_STATUS), L"Starting the game ...");
    SetTimer(g_wnd, 1, 100, NULL);
}

static BOOL CALLBACK find_game_window(HWND h, LPARAM l)
{
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid == g_game_pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { *(HWND *)l = h; return FALSE; }
    return TRUE;
}

static void game_wait_tick(void)
{
    HWND gw = NULL;
    EnumWindows(find_game_window, (LPARAM)&gw);
    int done = WaitForSingleObject(g_game_proc, 0) == WAIT_OBJECT_0;
    if (gw) {
        /* make the hand-over reliable: attach to the game's input queue for the call */
        DWORD me = GetCurrentThreadId(), it = GetWindowThreadProcessId(gw, NULL);
        AttachThreadInput(me, it, TRUE);
        ShowWindow(gw, SW_SHOW);
        SetForegroundWindow(gw);
        BringWindowToTop(gw);
        AttachThreadInput(me, it, FALSE);
    }
    if (gw || done || ++g_game_wait > 300) {         /* found, the game ended, or 30 s passed */
        KillTimer(g_wnd, 1);
        CloseHandle(g_game_proc); g_game_proc = NULL;
        DestroyWindow(g_wnd);
    }
}

static void do_browse(void)
{
    wchar_t f[MAX_PATH] = L"";
    OPENFILENAMEW o; memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o; o.hwndOwner = g_wnd;
    o.lpstrFilter = L"FIGHT.PAC\0FIGHT.PAC;fight.pac\0All files\0*.*\0";
    o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.lpstrTitle = L"Select FIGHT.PAC of the original FX Fighter Turbo";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&o)) { wcscpy(g_pac, f); SetWindowTextW(C(ID_PACEDIT), g_pac); update_status(); }
}

static void do_reset_all(void)
{
    if (MessageBoxW(g_wnd, L"Reset every setting shown here to the game's defaults?\n(Nothing is written until you press Save or Play.)",
                    L"FX Fighter Turbo", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    for (int i = 0; i < NDEF; i++) kv_set(DEFAULTS[i].k, DEFAULTS[i].v);
    ui_from_cfg();
}

static void build_ui(void)
{
    /* header and game folder (always visible) */
    HWND t = mk(-1, L"STATIC", L"FX Fighter Turbo", SS_LEFT, 16, 10, 400, 28, 0);
    SendMessageW(t, WM_SETFONT, (WPARAM)g_bold, TRUE);
    label(-1, L"FIGHT.PAC:", 16, 44, 80);
    HWND e = mk(-1, L"EDIT", g_pac, ES_AUTOHSCROLL | ES_READONLY | WS_BORDER, 100, 44, 430, 24, ID_PACEDIT);
    (void)e;
    mk(-1, L"BUTTON", L"Browse ...", BS_PUSHBUTTON | WS_TABSTOP, 540, 43, 84, 26, ID_BROWSE);
    mk(-1, L"STATIC", L"", SS_LEFT, 16, 74, 610, 20, ID_STATUS);

    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, S(10), S(100), S(620), S(396), g_wnd,
                            (HMENU)(INT_PTR)ID_TAB, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_tab, WM_SETFONT, (WPARAM)g_font, TRUE);
    const wchar_t *tabs[NPAGES] = { L"Display and sound", L"Gameplay", L"Controls", L"Detail" };
    for (int i = 0; i < NPAGES; i++) {
        TCITEMW ti; memset(&ti, 0, sizeof ti); ti.mask = TCIF_TEXT; ti.pszText = (LPWSTR)tabs[i];
        SendMessageW(g_tab, TCM_INSERTITEMW, i, (LPARAM)&ti);
    }
    SetWindowPos(g_tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    /* page 0: display and sound */
    group(0, L"Display", 24, 136, 592, 156);
    check(0, L"Fullscreen (F11 in the game)", 40, 160, 260, ID_FULL);
    check(0, L"Vertical sync", 330, 160, 200, ID_VSYNC);
    label(0, L"Window size:", 40, 194, 150);
    combo(0, 200, 190, 200, ID_WINSIZE);
    label(0, L"Fullscreen resolution:", 40, 228, 150);
    combo(0, 200, 224, 200, ID_FSRES);
    label(0, L"In the game, F9 / Shift+F9 change the size of the current mode.", 40, 260, 540);
    group(0, L"Volume", 24, 304, 592, 118);
    label(0, L"Music:", 40, 330, 150);
    slider(0, 200, 328, 330, ID_MUSIC, 0, 10);
    mk(0, L"STATIC", L"", SS_LEFT, 540, 333, 40, 20, ID_MUSICV);
    label(0, L"Sound effects:", 40, 372, 150);
    slider(0, 200, 370, 330, ID_SFX, 0, 10);
    mk(0, L"STATIC", L"", SS_LEFT, 540, 375, 40, 20, ID_SFXV);

    /* page 1: gameplay */
    group(1, L"Match", 24, 136, 592, 124);
    label(1, L"Rounds per match:", 40, 162, 140);  combo(1, 190, 158, 120, ID_ROUNDS);
    label(1, L"Round time:", 340, 162, 100);       combo(1, 450, 158, 140, ID_ROUNDTIME);
    label(1, L"CPU difficulty:", 40, 196, 140);    combo(1, 190, 192, 120, ID_DIFF);
    label(1, L"Game speed:", 340, 196, 100);       combo(1, 450, 192, 140, ID_SPEED);
    label(1, L"Handicap, player 1:", 40, 230, 140); combo(1, 190, 226, 120, ID_HC1);
    label(1, L"Handicap, player 2:", 340, 230, 110); combo(1, 450, 226, 140, ID_HC2);
    group(1, L"Rules", 24, 270, 592, 152);
    check(1, L"Ring-outs", 40, 294, 260, ID_RINGOUTS);
    check(1, L"Ring guard (own moves cannot leave the ring)", 330, 294, 270, ID_RINGGUARD);
    check(1, L"Challenger can join a 1P ladder", 40, 324, 260, ID_CHALLENGER);
    check(1, L"Quick CONTINUE (instant rematch)", 330, 324, 270, ID_QUICK);
    check(1, L"Controller rumble", 40, 354, 260, ID_RUMBLE);
    label(1, L"Input buffer (ticks):", 330, 358, 140);  combo(1, 480, 354, 110, ID_IBUF);

    /* page 2: controls */
    group(2, L"Player 1", 24, 136, 292, 240);
    group(2, L"Player 2", 324, 136, 292, 240);
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < 6; i++) {
            int x = 36 + p * 300, y = 164 + i * 33;
            label(2, KEY_LABEL[i], x, y, 118);
            mk(2, L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, x + 122, y - 2, 148, 26, ID_KEY0 + p * 6 + i);
        }
    mk(2, L"BUTTON", L"Default keys", BS_PUSHBUTTON | WS_TABSTOP, 24, 388, 130, 26, ID_KEYRESET);
    label(2, L"Click a key button, then press the new key. Forward / Back are screen directions (forward = towards the opponent at the start).", 24, 424, 592);

    /* page 3: detail */
    group(3, L"Fighters", 24, 136, 592, 100);
    check(3, L"Player 1 textures", 40, 162, 260, ID_DET_P1TEX);
    check(3, L"Player 1 smooth (Gouraud) shading", 330, 162, 270, ID_DET_P1SHADE);
    check(3, L"Player 2 textures", 40, 196, 260, ID_DET_P2TEX);
    check(3, L"Player 2 smooth (Gouraud) shading", 330, 196, 270, ID_DET_P2SHADE);
    group(3, L"Arena", 24, 246, 592, 140);
    label(3, L"Floor texture:", 40, 274, 150); combo(3, 200, 270, 200, ID_DET_FLOORTEX);
    label(3, L"Floor detail:", 40, 308, 150);  combo(3, 200, 304, 200, ID_DET_FLOOR);
    label(3, L"Floor detail None = no floor model and no ring-outs, as in the original.", 40, 336, 560);
    check(3, L"Background", 40, 358, 260, ID_DET_BG);

    /* buttons */
    mk(-1, L"BUTTON", L"Reset all", BS_PUSHBUTTON | WS_TABSTOP, 16, 508, 100, 30, ID_RESET);
    mk(-1, L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, 330, 508, 90, 30, ID_SAVE);
    mk(-1, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 430, 508, 90, 30, ID_CLOSE);
    HWND play = mk(-1, L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, 530, 508, 94, 30, ID_PLAY);
    SendMessageW(play, WM_SETFONT, (WPARAM)g_bold, TRUE);
}

static void fill_lists(void)
{
    for (size_t i = 0; i < sizeof WIN_SIZES / sizeof WIN_SIZES[0]; i++) add_res(ID_WINSIZE, WIN_SIZES[i][0], WIN_SIZES[i][1]);
    cb_add(ID_FSRES, L"Desktop resolution (borderless)", 0);
    int modes[96][2], n = list_modes(modes, 96);
    /* low resolutions the display has no mode for: the game draws at that size and scales it up (sharp pixels) */
    static const int LOW[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 400 }, { 640, 480 } };
    for (size_t k = 0; k < sizeof LOW / sizeof LOW[0]; k++) {
        int real = 0;
        for (int i = 0; i < n; i++) if (modes[i][0] == LOW[k][0] && modes[i][1] == LOW[k][1]) real = 1;
        if (!real) { wchar_t t[64]; swprintf(t, 64, L"%d x %d (scaled)", LOW[k][0], LOW[k][1]); cb_add(ID_FSRES, t, (LOW[k][0] << 16) | LOW[k][1]); }
    }
    for (int i = 0; i < n; i++) add_res(ID_FSRES, modes[i][0], modes[i][1]);
    cb_add(ID_ROUNDS, L"1", 1); cb_add(ID_ROUNDS, L"3 (best of 3)", 3); cb_add(ID_ROUNDS, L"5 (best of 5)", 5);
    cb_add(ID_ROUNDTIME, L"30 seconds", 30); cb_add(ID_ROUNDTIME, L"60 seconds", 60); cb_add(ID_ROUNDTIME, L"90 seconds", 90);
    cb_add(ID_ROUNDTIME, L"99 seconds", 99); cb_add(ID_ROUNDTIME, L"Unlimited", -1);
    for (int i = 0; i < 8; i++) { wchar_t t[32]; swprintf(t, 32, i == 4 ? L"%d (default)" : L"%d", i); cb_add(ID_DIFF, t, i); }
    cb_add(ID_SPEED, L"48 Hz (original)", 1); cb_add(ID_SPEED, L"60 Hz", 2); cb_add(ID_SPEED, L"66.7 Hz", 3);
    for (int i = 1; i <= 8; i++) { wchar_t t[32]; swprintf(t, 32, i == 4 ? L"%d (default)" : L"%d", i); cb_add(ID_HC1, t, i); cb_add(ID_HC2, t, i); }
    for (int i = 0; i <= 12; i++) { wchar_t t[32]; swprintf(t, 32, i == 0 ? L"0 (original)" : i == 4 ? L"4 (default)" : L"%d", i); cb_add(ID_IBUF, t, i); }
    cb_add(ID_DET_FLOORTEX, L"Off", 0); cb_add(ID_DET_FLOORTEX, L"Textured", 1); cb_add(ID_DET_FLOORTEX, L"Perspective-correct", 2);
    cb_add(ID_DET_FLOOR, L"None", 0); cb_add(ID_DET_FLOOR, L"Medium", 1); cb_add(ID_DET_FLOOR, L"High", 2);
}

static void center_window(HWND w)
{
    RECT wa, r;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);          /* work area of the primary monitor: the middle of the desktop */
    GetWindowRect(w, &r);
    int x = wa.left + ((wa.right - wa.left) - (r.right - r.left)) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - (r.bottom - r.top)) / 2;
    SetWindowPos(w, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/* take the colour of the tab control's page from the theme (it differs between Windows versions) */
static void pick_bg(void)
{
    g_bg_color = GetSysColor(COLOR_BTNFACE);
    HDC wdc = GetDC(g_wnd), m = CreateCompatibleDC(wdc);
    RECT r; GetClientRect(g_tab, &r);
    HBITMAP b = CreateCompatibleBitmap(wdc, r.right, r.bottom), old = (HBITMAP)SelectObject(m, b);
    FillRect(m, &r, (HBRUSH)(COLOR_BTNFACE + 1));
    SendMessageW(g_tab, WM_PRINTCLIENT, (WPARAM)m, PRF_CLIENT | PRF_ERASEBKGND);
    COLORREF c = GetPixel(m, r.right / 2, r.bottom - S(12));
    if (c != CLR_INVALID) g_bg_color = c;
    SelectObject(m, old); DeleteObject(b); DeleteDC(m); ReleaseDC(g_wnd, wdc);
    g_bg_brush = CreateSolidBrush(g_bg_color);
    SetClassLongPtrW(g_wnd, GCLP_HBRBACKGROUND, (LONG_PTR)g_bg_brush);
}

static void update_volume_labels(void)
{
    for (int id = ID_MUSIC; id <= ID_SFX; id++) {
        wchar_t t[16]; swprintf(t, 16, L"%d", (int)SendMessageW(C(id), TBM_GETPOS, 0, 0));
        HWND v = C(id == ID_MUSIC ? ID_MUSICV : ID_SFXV);
        if (v) SetWindowTextW(v, t);
    }
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_NOTIFY:
        if (((NMHDR *)l)->idFrom == ID_TAB && ((NMHDR *)l)->code == TCN_SELCHANGE) { end_capture(); show_page((int)SendMessageW(g_tab, TCM_GETCURSEL, 0, 0)); }
        return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        if (g_bg_brush) { SetBkColor((HDC)w, g_bg_color); return (LRESULT)g_bg_brush; }
        break;
    case WM_HSCROLL:
        update_volume_labels();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (g_capture >= 0 && !(id >= ID_KEY0 && id < ID_KEY0 + 12)) end_capture();
        if (id >= ID_KEY0 && id < ID_KEY0 + 12 && HIWORD(w) == BN_CLICKED) {
            end_capture();
            g_capture = id - ID_KEY0; key_button_text(g_capture);
            SetFocus(C(id));
        }
        else if (id == ID_BROWSE) do_browse();
        else if (id == ID_PLAY) do_play();
        else if (id == ID_SAVE) do_save(0);
        else if (id == ID_RESET) do_reset_all();
        else if (id == ID_CLOSE) DestroyWindow(h);
        else if (id == ID_KEYRESET) {
            for (int i = 0; i < 12; i++) { char k[16]; snprintf(k, sizeof k, "key%d", i + 1); key_value[i] = dflt(k); key_button_text(i); }
        }
        return 0;
    }
    case WM_TIMER: if (w == 1 && g_game_proc) game_wait_tick(); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void capture_key(LPARAM lp, WPARAM vk)
{
    int scan = (lp >> 16) & 0xff, ext = (lp >> 24) & 1;
    if (vk == VK_ESCAPE) { end_capture(); return; }
    int di = scan | (ext ? 0x80 : 0);
    if (scan == 0 || di > 255) return;
    key_value[g_capture] = di;
    end_capture();
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show)
{
    (void)hp; (void)cmd; (void)show;
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&ic);
    HDC dc = GetDC(NULL); g_dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc);

    GetModuleFileNameW(NULL, g_exe_dir, MAX_PATH);
    wchar_t *sl = wcsrchr(g_exe_dir, L'\\'); if (sl) sl[1] = 0;
    pref_init(); cfg_load(); pac_load();

    NONCLIENTMETRICSW ncm; memset(&ncm, 0, sizeof ncm); ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    ncm.lfMessageFont.lfHeight = -MulDiv(9, g_dpi, 72);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_BOLD; ncm.lfMessageFont.lfHeight = -MulDiv(14, g_dpi, 72);
    g_bold = CreateFontIndirectW(&ncm.lfMessageFont);

    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc; wc.hInstance = hi; wc.lpszClassName = L"FXFighterLauncher";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);

    RECT rc = { 0, 0, S(640), S(552) };
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, st, FALSE);
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"FX Fighter Turbo - Launcher", st, 0, 0, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hi, NULL);
    build_ui();
    pick_bg();
    fill_lists();
    ui_from_cfg();
    update_volume_labels();
    update_status();
    show_page(0);
    center_window(g_wnd);
    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_capture >= 0 && (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)) { capture_key(msg.lParam, msg.wParam); continue; }
        if (g_capture >= 0 && (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP || msg.message == WM_CHAR)) continue;
        if (!IsDialogMessageW(g_wnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}

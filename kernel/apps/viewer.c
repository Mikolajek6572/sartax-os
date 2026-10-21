// Sartax OS - Podglad obrazow
#include "../apps.h"

typedef struct {
    char path[256];
    image_t *img;
    int zoom;            // procent, 0 = dopasuj
    int px, py;          // przesuniecie
    int dragx, dragy;
    bool dragging;
    window_t *win;
} viewer_t;

static void vw_load(viewer_t *v, const char *path) {
    image_free(v->img);
    v->img = NULL;
    v->zoom = 0;
    v->px = v->py = 0;
    if (!path) { v->path[0] = 0; wm_set_title(v->win, "Podglad obrazow"); return; }
    strlcpy(v->path, path, sizeof v->path);
    u8 *d;
    u64 sz;
    if (fs_read_file(path, &d, &sz) == 0) {
        v->img = image_load_any(d, sz);
        kfree(d);
    }
    char t[160];
    snprintf(t, sizeof t, "%s - Podglad obrazow", path_name(path));
    wm_set_title(v->win, t);
}

// nastepny/poprzedni obraz w tym samym folderze
static void vw_step(viewer_t *v, int dir) {
    if (!v->path[0]) return;
    char folder[256];
    path_parent(folder, sizeof folder, v->path);
    fs_entry_t *list;
    int n = fs_list(folder, &list);
    if (n <= 0) return;
    int cur = -1, cnt = 0;
    int idx[256];
    for (int i = 0; i < n && cnt < 256; i++)
        if (list[i].type == FT_FILE && file_icon(list[i].name, FT_FILE) == ICON_IMAGE) {
            if (!strcmp(list[i].name, path_name(v->path))) cur = cnt;
            idx[cnt++] = i;
        }
    if (cnt > 1 && cur >= 0) {
        int nx = (cur + dir + cnt) % cnt;
        char p[256];
        path_join(p, sizeof p, folder, list[idx[nx]].name);
        vw_load(v, p);
    }
    kfree(list);
}

static void vw_frame(window_t *w, surface_t *s) {
    viewer_t *v = w->data;
    int W = ui.cw, H = ui.ch, TB = 46;
    if (ui_key(KEY_RIGHT)) vw_step(v, 1);
    if (ui_key(KEY_LEFT)) vw_step(v, -1);
    gfx_fill(s, 0, 0, W, H, RGB(36, 40, 46));
    gfx_fill(s, 0, 0, W, TB, g_theme.panel);
    gfx_fill(s, 0, TB - 1, W, 1, RGB(222, 228, 234));
    if (ui_button_icon(8, 6, 34, 34, ICON_BACK, NULL, BTN_FLAT)) vw_step(v, -1);
    if (ui_button_icon(44, 6, 34, 34, ICON_FORWARD, NULL, BTN_FLAT)) vw_step(v, 1);
    if (ui_button(90, 6, 40, 34, "-", BTN_FLAT) && v->img) v->zoom = MAX(10, (v->zoom ? v->zoom : 100) - 25);
    if (ui_button(132, 6, 40, 34, "+", BTN_FLAT) && v->img) v->zoom = MIN(800, (v->zoom ? v->zoom : 100) + 25);
    if (ui_button(174, 6, 90, 34, "Dopasuj", BTN_FLAT)) { v->zoom = 0; v->px = v->py = 0; }
    if (ui_button(266, 6, 64, 34, "100%", BTN_FLAT)) { v->zoom = 100; v->px = v->py = 0; }
    if (v->img && ui_button_icon(W - 180, 6, 170, 34, ICON_DESKTOP, "Ustaw jako tapete", BTN_FLAT)) {
        cfg_set("tapeta_plik", v->path);
        cfg_save();
        desktop_reload_wallpaper();
    }
    int vh = H - TB;
    if (!v->img) {
        gfx_blit(s, W / 2 - 24, TB + vh / 2 - 60, icon_get(ICON_VIEWER, 48));
        gfx_text_center(s, 0, TB + vh / 2, W, FONT_UI, v->path[0] ? "Nie mozna odczytac obrazu (obslugiwane: PNG, JPEG, GIF, BMP)"
                                                                 : "Otworz obraz z eksploratora plikow", RGB(220, 226, 232));
        if (!v->path[0] && ui_button_icon(W / 2 - 90, TB + vh / 2 + 30, 180, 36, ICON_PICTURES, "Otworz Obrazy", BTN_PRIMARY))
            explorer_open("/Obrazy");
        return;
    }
    image_t *im = v->img;
    int z = v->zoom;
    if (!z) {
        z = MIN((W - 20) * 100 / im->w, (vh - 20) * 100 / im->h);
        z = MIN(z, 100);
        z = MAX(z, 1);
    }
    if (ui.wheel && ui_hit(0, TB, W, vh)) { v->zoom = CLAMP(z - ui.wheel * 10, 10, 800); z = v->zoom; }
    int dw = im->w * z / 100, dh = im->h * z / 100;
    if (ui.mpressed && ui_hit(0, TB, W, vh)) { v->dragging = true; v->dragx = ui.mx - v->px; v->dragy = ui.my - v->py; }
    if (v->dragging) {
        if (!ui.mdown) v->dragging = false;
        else if (v->zoom) { v->px = ui.mx - v->dragx; v->py = ui.my - v->dragy; }
    }
    int x = (W - dw) / 2 + (v->zoom ? v->px : 0), y = TB + (vh - dh) / 2 + (v->zoom ? v->py : 0);
    int sx0 = s->cx0, sy0 = s->cy0, sx1 = s->cx1, sy1 = s->cy1;
    gfx_set_clip(s, 0, TB, W, vh);
    gfx_shadow(s, x, y, dw, dh, 0, 14, 120);
    gfx_blit_scaled(s, x, y, dw, dh, im);
    char info[64];
    snprintf(info, sizeof info, "%d x %d  |  %d%%", im->w, im->h, z);
    int iw = gfx_text_width(FONT_SMALL, info) + 20;
    gfx_round_rect(s, W - iw - 10, H - 34, iw, 24, 12, RGBA(0, 0, 0, 140));
    gfx_text(s, W - iw, H - 29, FONT_SMALL, info, RGB(240, 240, 240));
    s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
}

static void vw_close(window_t *w) {
    viewer_t *v = w->data;
    image_free(v->img);
    kfree(v);
}

void viewer_open(const char *path) {
    viewer_t *v = kzalloc(sizeof *v);
    window_t *w = wm_create("viewer", "Podglad obrazow", ICON_VIEWER, 820, 600, WF_RESIZABLE, vw_frame, v);
    w->on_close = vw_close;
    v->win = w;
    vw_load(v, path);
}

// Sartax OS - Eksplorator plikow
#include "../apps.h"

char g_clip_paths[16][256];
int g_clip_count;
bool g_clip_cut;

typedef struct {
    char path[256];
    char hist[24][256];
    int nhist, hpos;
    fs_entry_t *ents;
    int n;
    bool *sel;
    int anchor;
    int scroll;
    bool list_view;
    char addr[256];
    bool addr_edit;
    window_t *win;
} explorer_t;

static window_t *explorers[16];

static int ent_cmp(const fs_entry_t *a, const fs_entry_t *b) {
    if (a->type != b->type) return a->type == FT_DIR ? -1 : 1;
    return strcasecmp_pl(a->name, b->name);
}

static void ex_load(explorer_t *e) {
    kfree(e->ents);
    kfree(e->sel);
    e->ents = NULL;
    e->n = fs_list(e->path, &e->ents);
    if (e->n < 0) { e->n = 0; e->ents = kzalloc(sizeof(fs_entry_t)); }
    // sortowanie przez wstawianie
    for (int i = 1; i < e->n; i++) {
        fs_entry_t t = e->ents[i];
        int j = i - 1;
        while (j >= 0 && ent_cmp(&e->ents[j], &t) > 0) { e->ents[j + 1] = e->ents[j]; j--; }
        e->ents[j + 1] = t;
    }
    e->sel = kzalloc(e->n + 1);
    e->anchor = -1;
    strlcpy(e->addr, e->path, sizeof e->addr);
    const char *nm = !strcmp(e->path, "/") ? "Ten komputer" : path_name(e->path);
    char t[128];
    snprintf(t, sizeof t, "%s - Pliki", nm);
    if (e->win) wm_set_title(e->win, t);
}

void explorer_refresh_all(void) {
    for (int i = 0; i < 16; i++)
        if (explorers[i]) {
            explorer_t *e = explorers[i]->data;
            // zachowaj zaznaczenie po nazwie
            char keep[256] = "";
            for (int k = 0; k < e->n; k++)
                if (e->sel[k]) { strlcpy(keep, e->ents[k].name, sizeof keep); break; }
            if (!fs_is_dir(e->path)) strlcpy(e->path, "/", sizeof e->path);
            ex_load(e);
            for (int k = 0; k < e->n; k++)
                if (!strcmp(e->ents[k].name, keep)) e->sel[k] = true;
        }
    desktop_refresh();
    wm_invalidate();
}

static void ex_go(explorer_t *e, const char *path, bool record) {
    if (!fs_is_dir(path)) { dlg_message(e->win, "Pliki", "Nie mozna otworzyc tego folderu.", ICON_WARNING); return; }
    strlcpy(e->path, path, sizeof e->path);
    if (record) {
        if (e->hpos < e->nhist - 1) e->nhist = e->hpos + 1;
        if (e->nhist == 24) { memmove(e->hist[0], e->hist[1], 23 * 256); e->nhist--; }
        strlcpy(e->hist[e->nhist++], path, 256);
        e->hpos = e->nhist - 1;
    }
    e->scroll = 0;
    e->addr_edit = false;
    ex_load(e);
}

static void ex_up(explorer_t *e) {
    if (!strcmp(e->path, "/")) return;
    char p[256];
    path_parent(p, sizeof p, e->path);
    ex_go(e, p, true);
}

static int sel_count(explorer_t *e) {
    int c = 0;
    for (int i = 0; i < e->n; i++) c += e->sel[i];
    return c;
}

static int first_sel(explorer_t *e) {
    for (int i = 0; i < e->n; i++)
        if (e->sel[i]) return i;
    return -1;
}

static void ex_open_item(explorer_t *e, int i) {
    char p[256];
    path_join(p, sizeof p, e->path, e->ents[i].name);
    if (e->ents[i].type == FT_DIR) ex_go(e, p, true);
    else open_path(p);
}

// ---------------- operacje na plikach ----------------
static void unique_name(char *out, size_t n, const char *dir, const char *name) {
    path_join(out, n, dir, name);
    if (!fs_exists(out)) return;
    char base[128], ext[32] = "";
    strlcpy(base, name, sizeof base);
    char *dot = strrchr(base, '.');
    if (dot && dot != base) { strlcpy(ext, dot, sizeof ext); *dot = 0; }
    for (int i = 1; i < 1000; i++) {
        char nm[200];
        if (i == 1) snprintf(nm, sizeof nm, "%s - kopia%s", base, ext);
        else snprintf(nm, sizeof nm, "%s - kopia (%d)%s", base, i, ext);
        path_join(out, n, dir, nm);
        if (!fs_exists(out)) return;
    }
}

void explorer_paste(window_t *owner, const char *dir) {
    if (!g_clip_count) return;
    int err = 0;
    for (int i = 0; i < g_clip_count; i++) {
        const char *src = g_clip_paths[i];
        char parent[256], dst[256];
        path_parent(parent, sizeof parent, src);
        if (g_clip_cut) {
            if (!strcmp(parent, dir)) continue;
            path_join(dst, sizeof dst, dir, path_name(src));
            if (fs_exists(dst)) unique_name(dst, sizeof dst, dir, path_name(src));
            int r = fs_rename(src, dst);
            if (r) err = r;
        } else {
            unique_name(dst, sizeof dst, dir, path_name(src));
            int r = fs_copy(src, dst);
            if (r) err = r;
        }
    }
    if (g_clip_cut) g_clip_count = 0;
    if (err) dlg_message(owner, "Wklejanie", err == FS_EINVAL ? "Nie mozna wkleic folderu do samego siebie." : fs_error(err), ICON_WARNING);
    explorer_refresh_all();
}

typedef struct { char paths[16][256]; int n; window_t *owner; } del_ctx_t;

static void del_cb(void *p, bool yes) {
    del_ctx_t *c = p;
    if (yes) {
        for (int i = 0; i < c->n; i++) {
            int r = fs_delete(c->paths[i]);
            if (r) { dlg_message(c->owner, "Usuwanie", fs_error(r), ICON_WARNING); break; }
        }
        explorer_refresh_all();
    }
    kfree(c);
}

void explorer_delete_paths(window_t *owner, char paths[][256], int n) {
    if (!n) return;
    del_ctx_t *c = kzalloc(sizeof *c);
    c->n = MIN(n, 16);
    c->owner = owner;
    for (int i = 0; i < c->n; i++) strlcpy(c->paths[i], paths[i], 256);
    char msg[320];
    if (n == 1) snprintf(msg, sizeof msg, "Czy na pewno trwale usunac \"%s\"?", path_name(paths[0]));
    else snprintf(msg, sizeof msg, "Czy na pewno trwale usunac %d elementy?", n);
    dlg_confirm(owner, "Usun", msg, "Usun", del_cb, c);
}

static void ex_collect(explorer_t *e, char out[][256], int *n) {
    *n = 0;
    for (int i = 0; i < e->n && *n < 16; i++)
        if (e->sel[i]) path_join(out[(*n)++], 256, e->path, e->ents[i].name);
}

static void ex_copy(explorer_t *e, bool cut) {
    ex_collect(e, g_clip_paths, &g_clip_count);
    g_clip_cut = cut;
}

static void ex_delete(explorer_t *e) {
    char p[16][256];
    int n;
    ex_collect(e, p, &n);
    explorer_delete_paths(e->win, p, n);
}

typedef struct { explorer_t *e; char old[256]; } ren_t;

static bool ex_alive(explorer_t *e) {
    for (int i = 0; i < 16; i++)
        if (explorers[i] && explorers[i]->data == e) return true;
    return false;
}

static void rename_cb(void *p, const char *name) {
    ren_t *r = p;
    if (ex_alive(r->e)) {
        if (!path_valid_name(name)) dlg_message(r->e->win, "Zmien nazwe", fs_error(FS_ENAME), ICON_WARNING);
        else {
            char parent[256], dst[256];
            path_parent(parent, sizeof parent, r->old);
            path_join(dst, sizeof dst, parent, name);
            int rr = fs_rename(r->old, dst);
            if (rr) dlg_message(r->e->win, "Zmien nazwe", fs_error(rr), ICON_WARNING);
            explorer_refresh_all();
        }
    }
    kfree(r);
}

static void ex_rename(explorer_t *e) {
    int i = first_sel(e);
    if (i < 0) return;
    ren_t *r = kzalloc(sizeof *r);
    r->e = e;
    path_join(r->old, sizeof r->old, e->path, e->ents[i].name);
    dlg_input(e->win, "Zmien nazwe", "Nowa nazwa:", e->ents[i].name, rename_cb, r);
}

typedef struct { explorer_t *e; bool file; } new_t;
static void new_cb(void *p, const char *name) {
    new_t *n = p;
    if (ex_alive(n->e)) {
        char dst[256];
        if (!path_valid_name(name)) dlg_message(n->e->win, "Nowy", fs_error(FS_ENAME), ICON_WARNING);
        else {
            path_join(dst, sizeof dst, n->e->path, name);
            int r = n->file ? (fs_exists(dst) ? FS_EEXIST : fs_write_file(dst, "", 0)) : fs_mkdir(dst);
            if (r) dlg_message(n->e->win, "Nowy", fs_error(r), ICON_WARNING);
            explorer_refresh_all();
            for (int i = 0; i < n->e->n; i++) n->e->sel[i] = !strcmp(n->e->ents[i].name, name);
        }
    }
    kfree(n);
}

static void ex_new(explorer_t *e, bool file) {
    new_t *n = kzalloc(sizeof *n);
    n->e = e;
    n->file = file;
    if (file) dlg_input(e->win, "Nowy plik", "Nazwa nowego pliku:", "Nowy dokument.txt", new_cb, n);
    else dlg_input(e->win, "Nowy folder", "Nazwa nowego folderu:", "Nowy folder", new_cb, n);
}

static void ex_props(explorer_t *e) {
    int i = first_sel(e);
    char msg[512];
    if (i < 0) {
        char fr[32], tot[32];
        fmt_size(fr, sizeof fr, fs_free_bytes());
        fmt_size(tot, sizeof tot, fs_total_bytes());
        snprintf(msg, sizeof msg, "Folder: %s\nElementow: %d\n\nDysk SartaxFS\nWolne: %s z %s", e->path, e->n, fr, tot);
    } else {
        fs_entry_t *f = &e->ents[i];
        char sz[32], dt[32];
        fmt_size(sz, sizeof sz, f->size);
        fmt_datetime(dt, sizeof dt, f->mtime);
        snprintf(msg, sizeof msg, "Nazwa: %s\nTyp: %s\nRozmiar: %s\nZmodyfikowano: %s\nLokalizacja: %s", f->name,
                 file_type_name(f->name, f->type), f->type == FT_DIR ? "-" : sz, dt, e->path);
    }
    dlg_message(e->win, "Wlasciwosci", msg, i >= 0 ? file_icon(e->ents[i].name, e->ents[i].type) : ICON_DISK);
}

enum { EM_OPEN = 1, EM_COPY, EM_CUT, EM_PASTE, EM_RENAME, EM_DELETE, EM_PROPS, EM_NEWFOLDER, EM_NEWFILE, EM_REFRESH,
       EM_VIEW, EM_NOTEPAD, EM_WALLPAPER };

static void wallpaper_cb(void *p) {
    char *path = p;
    cfg_set("tapeta_plik", path);
    cfg_save();
    desktop_reload_wallpaper();
    kfree(path);
}

static void ex_menu_cb(void *ctx, int id) {
    explorer_t *e = ctx;
    if (!ex_alive(e)) return;
    int i = first_sel(e);
    switch (id) {
    case EM_OPEN: if (i >= 0) ex_open_item(e, i); break;
    case EM_NOTEPAD:
        if (i >= 0) { char p[256]; path_join(p, sizeof p, e->path, e->ents[i].name); notepad_open(p); }
        break;
    case EM_WALLPAPER:
        if (i >= 0) { char *p = kmalloc(256); path_join(p, 256, e->path, e->ents[i].name); wallpaper_cb(p); }
        break;
    case EM_COPY: ex_copy(e, false); break;
    case EM_CUT: ex_copy(e, true); break;
    case EM_PASTE: explorer_paste(e->win, e->path); break;
    case EM_RENAME: ex_rename(e); break;
    case EM_DELETE: ex_delete(e); break;
    case EM_PROPS: ex_props(e); break;
    case EM_NEWFOLDER: ex_new(e, false); break;
    case EM_NEWFILE: ex_new(e, true); break;
    case EM_REFRESH: ex_load(e); break;
    case EM_VIEW: e->list_view = !e->list_view; break;
    }
}

// ---------------- rysowanie ----------------
#define TB_H 50
#define SIDE_W 176
#define STATUS_H 26
#define GRID_W 100
#define GRID_H 98
#define ROW_H 28

typedef struct { const char *label; const char *path; int icon; } place_t;
static const place_t places[] = {
    { "Ten komputer", "/", ICON_COMPUTER },
    { "Pulpit", "/Pulpit", ICON_DESKTOP },
    { "Dokumenty", "/Dokumenty", ICON_DOCS },
    { "Pobrane", "/Pobrane", ICON_DOWNLOAD },
    { "Obrazy", "/Obrazy", ICON_PICTURES },
    { "Programy", "/Programy", ICON_PROGRAMS },
    { "System", "/System", ICON_SETTINGS },
};

static void item_rect(explorer_t *e, int i, int vw, int *x, int *y, int *w, int *h) {
    if (e->list_view) {
        *x = SIDE_W + 8;
        *y = TB_H + 30 + i * ROW_H - e->scroll;
        *w = vw - 16;
        *h = ROW_H;
    } else {
        int cols = MAX(1, (vw - 16) / GRID_W);
        *x = SIDE_W + 8 + (i % cols) * GRID_W;
        *y = TB_H + 8 + (i / cols) * GRID_H - e->scroll;
        *w = GRID_W - 6;
        *h = GRID_H - 6;
    }
}

static int content_height(explorer_t *e, int vw) {
    if (e->list_view) return 30 + e->n * ROW_H + 8;
    int cols = MAX(1, (vw - 16) / GRID_W);
    return 8 + (e->n + cols - 1) / cols * GRID_H + 8;
}

static void ex_frame(window_t *w, surface_t *s) {
    explorer_t *e = w->data;
    int W = ui.cw, H = ui.ch;
    int vw = W - SIDE_W - 14;              // szerokosc obszaru plikow (bez paska przewijania)
    int vh = H - TB_H - STATUS_H;

    // ---- klawiatura ----
    if (ui.key && !ui.focus) {
        event_t *k = ui.key;
        int fs = first_sel(e);
        if (k->key == KEY_ENTER && fs >= 0) { ui.key = NULL; ex_open_item(e, fs); return; }
        else if (k->key == KEY_BACKSPACE) { ui.key = NULL; ex_up(e); return; }
        else if (k->key == KEY_DEL) { ui.key = NULL; ex_delete(e); }
        else if (k->key == KEY_F2) { ui.key = NULL; ex_rename(e); }
        else if (k->key == KEY_F5) { ui.key = NULL; ex_load(e); }
        else if (ui_ctrl_key('c')) ex_copy(e, false);
        else if (ui_ctrl_key('x')) ex_copy(e, true);
        else if (ui_ctrl_key('v')) explorer_paste(w, e->path);
        else if (ui_ctrl_key('a')) { for (int i = 0; i < e->n; i++) e->sel[i] = true; }
        else if (k->key == KEY_DOWN || k->key == KEY_UP || k->key == KEY_LEFT || k->key == KEY_RIGHT) {
            int cols = e->list_view ? 1 : MAX(1, (vw - 16) / GRID_W);
            int d = k->key == KEY_DOWN ? cols : k->key == KEY_UP ? -cols : k->key == KEY_RIGHT ? 1 : -1;
            int ni = fs < 0 ? 0 : CLAMP(fs + d, 0, e->n - 1);
            if (e->n) { memset(e->sel, 0, e->n); e->sel[ni] = true; }
            ui.key = NULL;
        }
    }

    gfx_fill(s, 0, 0, W, H, RGB(255, 255, 255));
    // ---- pasek narzedzi ----
    gfx_fill(s, 0, 0, W, TB_H, g_theme.panel);
    gfx_fill(s, 0, TB_H - 1, W, 1, RGB(222, 228, 234));
    int bx = 8;
    if (ui_button_icon(bx, 8, 34, 34, ICON_BACK, NULL, BTN_FLAT) && e->hpos > 0) { e->hpos--; ex_go(e, e->hist[e->hpos], false); return; }
    bx += 36;
    if (ui_button_icon(bx, 8, 34, 34, ICON_FORWARD, NULL, BTN_FLAT) && e->hpos < e->nhist - 1) { e->hpos++; ex_go(e, e->hist[e->hpos], false); return; }
    bx += 36;
    if (ui_button_icon(bx, 8, 34, 34, ICON_UP, NULL, BTN_FLAT)) { ex_up(e); return; }
    bx += 40;
    int aw = W - bx - 290;
    if (ui_textbox(bx, 9, aw, e->addr, sizeof e->addr, 0, "Sciezka")) {
        char p[256];
        strlcpy(p, e->addr, sizeof p);
        if (p[0] != '/') { char q[256]; snprintf(q, sizeof q, "/%s", p); strlcpy(p, q, sizeof p); }
        size_t l = strlen(p);
        while (l > 1 && p[l - 1] == '/') p[--l] = 0;
        ui_focus_clear();
        ex_go(e, p, true);
        return;
    }
    bx += aw + 8;
    if (ui_button_icon(bx, 8, 34, 34, ICON_REFRESH, NULL, BTN_FLAT)) ex_load(e);
    bx += 38;
    if (ui_button_icon(bx, 8, 118, 34, ICON_NEWFOLDER, "Nowy folder", BTN_FLAT)) ex_new(e, false);
    bx += 122;
    if (ui_button(bx, 8, 74, 34, e->list_view ? "Ikony" : "Lista", BTN_FLAT)) e->list_view = !e->list_view;

    // ---- panel boczny ----
    gfx_fill(s, 0, TB_H, SIDE_W, H - TB_H, RGB(246, 248, 250));
    gfx_fill(s, SIDE_W - 1, TB_H, 1, H - TB_H, RGB(228, 233, 238));
    gfx_text(s, 16, TB_H + 10, FONT_SMALL, "MIEJSCA", g_theme.text_dim);
    for (int i = 0; i < (int)ARRAY_LEN(places); i++) {
        int y = TB_H + 30 + i * 34;
        bool cur = !strcmp(e->path, places[i].path);
        bool hov = ui_hit(6, y, SIDE_W - 12, 32);
        if (cur) gfx_round_rect(s, 6, y, SIDE_W - 12, 32, 7, g_theme.sel);
        else if (hov) gfx_round_rect(s, 6, y, SIDE_W - 12, 32, 7, g_theme.hover);
        gfx_blit(s, 16, y + 4, icon_get(places[i].icon, 24));
        gfx_text(s, 48, y + 8, FONT_UI, places[i].label, g_theme.text);
        if (ui.mpressed && hov) { ex_go(e, places[i].path, true); return; }
    }
    // pasek zajetosci dysku
    {
        int y = H - STATUS_H - 64;
        gfx_blit(s, 14, y, icon_get(ICON_DISK, 24));
        gfx_text(s, 46, y + 4, FONT_UI, "Dysk Sartax", g_theme.text);
        u64 tot = fs_total_bytes(), fr = fs_free_bytes();
        ui_progress(14, y + 32, SIDE_W - 28, 8, tot ? (int)((tot - fr) * 1000 / tot) : 0);
        char a[32], t[64];
        fmt_size(a, sizeof a, fr);
        snprintf(t, sizeof t, "Wolne: %s", a);
        gfx_text(s, 14, y + 44, FONT_SMALL, t, g_theme.text_dim);
    }

    // ---- lista plikow ----
    int ch = content_height(e, vw);
    if (ui.wheel && ui_hit(SIDE_W, TB_H, W - SIDE_W, vh)) e->scroll += ui.wheel * 3 * ROW_H;
    e->scroll = CLAMP(e->scroll, 0, MAX(0, ch - vh));
    int sx0 = s->cx0, sy0 = s->cy0, sx1 = s->cx1, sy1 = s->cy1;
    gfx_set_clip(s, SIDE_W, TB_H, W - SIDE_W, vh);
    if (e->list_view) {
        int y = TB_H + 4 - e->scroll;
        gfx_text(s, SIDE_W + 46, y + 6, FONT_SMALL, "Nazwa", g_theme.text_dim);
        gfx_text(s, SIDE_W + vw - 330, y + 6, FONT_SMALL, "Data modyfikacji", g_theme.text_dim);
        gfx_text(s, SIDE_W + vw - 190, y + 6, FONT_SMALL, "Typ", g_theme.text_dim);
        gfx_text(s, SIDE_W + vw - 70, y + 6, FONT_SMALL, "Rozmiar", g_theme.text_dim);
        gfx_fill(s, SIDE_W + 8, y + 24, vw - 16, 1, RGB(230, 234, 238));
    }
    int clicked = -2;
    bool rclick = false;
    bool in_area = ui_hit(SIDE_W, TB_H, vw, vh);
    for (int i = 0; i < e->n; i++) {
        int x, y, iw, ih;
        item_rect(e, i, vw, &x, &y, &iw, &ih);
        if (y + ih < TB_H || y > TB_H + vh) {
            if (in_area && (ui.mpressed || ui.rpressed) && ui_hit(x, y, iw, ih)) { clicked = i; rclick = ui.rpressed; }
            continue;
        }
        bool hov = in_area && ui_hit(x, y, iw, ih);
        if ((ui.mpressed || ui.rpressed) && hov) { clicked = i; rclick = ui.rpressed; }
        fs_entry_t *f = &e->ents[i];
        bool cut = false;
        if (g_clip_cut)
            for (int k = 0; k < g_clip_count; k++) {
                char p[256];
                path_join(p, sizeof p, e->path, f->name);
                if (!strcmp(p, g_clip_paths[k])) cut = true;
            }
        if (e->sel[i]) gfx_round_rect(s, x, y, iw, ih, 7, g_theme.sel);
        else if (hov) gfx_round_rect(s, x, y, iw, ih, 7, g_theme.hover);
        int ic = file_icon(f->name, f->type);
        if (e->list_view) {
            gfx_blit_alpha(s, x + 8, y + 2, icon_get(ic, 24), cut ? 110 : 255);
            gfx_text_ellipsis(s, x + 40, y + 6, vw - 380, FONT_UI, f->name, g_theme.text);
            char dt[32], sz[32];
            fmt_datetime(dt, sizeof dt, f->mtime);
            gfx_text(s, SIDE_W + vw - 330, y + 6, FONT_UI, dt, g_theme.text_dim);
            gfx_text(s, SIDE_W + vw - 190, y + 6, FONT_UI, file_type_name(f->name, f->type), g_theme.text_dim);
            if (f->type == FT_FILE) { fmt_size(sz, sizeof sz, f->size); gfx_text(s, SIDE_W + vw - 70, y + 6, FONT_UI, sz, g_theme.text_dim); }
        } else {
            gfx_blit_alpha(s, x + (iw - 48) / 2, y + 6, icon_get(ic, 48), cut ? 110 : 255);
            int sxa = s->cx0, sya = s->cy0, sxb = s->cx1, syb = s->cy1;
            gfx_set_clip(s, x, y, iw, ih);
            if (gfx_text_width(FONT_SMALL, f->name) <= iw - 6)
                gfx_text_center(s, x, y + 60, iw, FONT_SMALL, f->name, g_theme.text);
            else gfx_text_wrap(s, x + 4, y + 58, iw - 8, FONT_SMALL, f->name, g_theme.text, 13);
            s->cx0 = sxa, s->cy0 = sya, s->cx1 = sxb, s->cy1 = syb;
        }
    }
    if (!e->n) gfx_text_center(s, SIDE_W, TB_H + 60, vw, FONT_UI, "Ten folder jest pusty", g_theme.text_dim);
    s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
    ui_scrollbar(W - 14, TB_H + 2, vh - 4, &e->scroll, ch, vh);

    // ---- klikniecia ----
    if (in_area && (ui.mpressed || ui.rpressed)) {
        bool ctrl = g_mouse_buttons && ui.key == NULL && 0;
        (void)ctrl;
        if (clicked >= 0) {
            bool multi = false;
            // Ctrl = dodaj do zaznaczenia (modyfikatory z ostatniego zdarzenia myszy)
            extern u8 g_last_mouse_mods;
            multi = g_last_mouse_mods & MOD_CTRL;
            if (rclick && e->sel[clicked]) {}
            else if (multi) e->sel[clicked] = !e->sel[clicked];
            else { memset(e->sel, 0, e->n); e->sel[clicked] = true; }
            if (ui.dclick && !rclick) { ex_open_item(e, clicked); return; }
        } else {
            memset(e->sel, 0, e->n);
        }
        if (rclick) {
            int sc = sel_count(e);
            if (sc) {
                int fsel = first_sel(e);
                bool img = sc == 1 && file_icon(e->ents[fsel].name, e->ents[fsel].type) == ICON_IMAGE;
                bool file = sc == 1 && e->ents[fsel].type == FT_FILE;
                menu_item_t it[] = {
                    { "Otworz", -1, EM_OPEN, sc != 1 },
                    { "Otworz w Notatniku", ICON_NOTEPAD, EM_NOTEPAD, !file },
                    { "Ustaw jako tapete", ICON_DESKTOP, EM_WALLPAPER, !img },
                    { NULL, -1, 0, false },
                    { "Kopiuj", -1, EM_COPY, false },
                    { "Wytnij", -1, EM_CUT, false },
                    { "Zmien nazwe", -1, EM_RENAME, sc != 1 },
                    { "Usun", ICON_TRASH, EM_DELETE, false },
                    { NULL, -1, 0, false },
                    { "Wlasciwosci", ICON_INFO, EM_PROPS, sc != 1 },
                };
                menu_open(g_mouse_x, g_mouse_y, it, ARRAY_LEN(it), ex_menu_cb, e);
            } else {
                menu_item_t it[] = {
                    { "Nowy folder", ICON_NEWFOLDER, EM_NEWFOLDER, false },
                    { "Nowy plik tekstowy", ICON_NEWFILE, EM_NEWFILE, false },
                    { "Wklej", -1, EM_PASTE, g_clip_count == 0 },
                    { NULL, -1, 0, false },
                    { e->list_view ? "Widok: ikony" : "Widok: lista", -1, EM_VIEW, false },
                    { "Odswiez", ICON_REFRESH, EM_REFRESH, false },
                    { "Wlasciwosci", ICON_INFO, EM_PROPS, false },
                };
                menu_open(g_mouse_x, g_mouse_y, it, ARRAY_LEN(it), ex_menu_cb, e);
            }
        }
    }

    // ---- pasek stanu ----
    gfx_fill(s, SIDE_W, H - STATUS_H, W - SIDE_W, STATUS_H, g_theme.panel);
    gfx_fill(s, SIDE_W, H - STATUS_H, W - SIDE_W, 1, RGB(228, 233, 238));
    char st[128];
    int sc = sel_count(e);
    if (sc == 1) {
        fs_entry_t *f = &e->ents[first_sel(e)];
        char sz[32];
        fmt_size(sz, sizeof sz, f->size);
        snprintf(st, sizeof st, "Elementy: %d   |   Zaznaczono: %s%s%s", e->n, f->name, f->type == FT_FILE ? ", " : "",
                 f->type == FT_FILE ? sz : "");
    } else if (sc > 1) snprintf(st, sizeof st, "Elementy: %d   |   Zaznaczono: %d", e->n, sc);
    else snprintf(st, sizeof st, "Elementy: %d", e->n);
    gfx_text_ellipsis(s, SIDE_W + 12, H - STATUS_H + 6, W - SIDE_W - 24, FONT_SMALL, st, g_theme.text_dim);
}

static void ex_close(window_t *w) {
    explorer_t *e = w->data;
    for (int i = 0; i < 16; i++)
        if (explorers[i] == w) explorers[i] = NULL;
    kfree(e->ents);
    kfree(e->sel);
    kfree(e);
}

void explorer_open(const char *path) {
    int slot = -1;
    for (int i = 0; i < 16; i++)
        if (!explorers[i]) { slot = i; break; }
    if (slot < 0) return;
    explorer_t *e = kzalloc(sizeof *e);
    window_t *w = wm_create("explorer", "Pliki", ICON_COMPUTER, 900, 580, WF_RESIZABLE, ex_frame, e);
    w->min_w = 640;
    w->min_h = 360;
    w->on_close = ex_close;
    e->win = w;
    explorers[slot] = w;
    ex_go(e, path && fs_is_dir(path) ? path : "/", true);
}

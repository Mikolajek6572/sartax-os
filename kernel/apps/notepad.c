// Sartax OS - Notatnik
#include "../apps.h"

typedef struct {
    char path[256];
    char *buf;
    int len, cap;
    int caret;
    int want_col;
    int scroll, hscroll;
    bool modified;
    int *lines;          // poczatki linii
    int nlines;
    bool lines_dirty;
    window_t *win;
    bool close_after_save;
} notepad_t;

#define NP_TB 46
#define NP_STATUS 26
#define NP_PAD 10

static void np_title(notepad_t *n) {
    char t[160];
    snprintf(t, sizeof t, "%s%s - Notatnik", n->modified ? "*" : "", n->path[0] ? path_name(n->path) : "Bez nazwy");
    wm_set_title(n->win, t);
}

static void np_index(notepad_t *n) {
    if (!n->lines_dirty) return;
    int c = 1;
    for (int i = 0; i < n->len; i++) c += n->buf[i] == '\n';
    kfree(n->lines);
    n->lines = kmalloc((c + 1) * sizeof(int));
    n->lines[0] = 0;
    int k = 1;
    for (int i = 0; i < n->len; i++)
        if (n->buf[i] == '\n') n->lines[k++] = i + 1;
    n->nlines = c;
    n->lines_dirty = false;
}

static int line_of(notepad_t *n, int pos) {
    int lo = 0, hi = n->nlines - 1;
    while (lo < hi) {
        int m = (lo + hi + 1) / 2;
        if (n->lines[m] <= pos) lo = m; else hi = m - 1;
    }
    return lo;
}

static int line_end(notepad_t *n, int l) { return l + 1 < n->nlines ? n->lines[l + 1] - 1 : n->len; }

static int col_of(notepad_t *n, int pos) {
    int l = line_of(n, pos), c = 0;
    for (int p = n->lines[l]; p < pos; p = utf8_next(n->buf, p)) c++;
    return c;
}

static int pos_at(notepad_t *n, int l, int col) {
    l = CLAMP(l, 0, n->nlines - 1);
    int p = n->lines[l], e = line_end(n, l);
    while (col > 0 && p < e) { p = utf8_next(n->buf, p); col--; }
    return p;
}

static void np_insert(notepad_t *n, const char *s, int l) {
    if (n->len + l + 1 > n->cap) {
        n->cap = (n->len + l + 1) * 2;
        n->buf = krealloc(n->buf, n->cap);
    }
    memmove(n->buf + n->caret + l, n->buf + n->caret, n->len - n->caret + 1);
    memcpy(n->buf + n->caret, s, l);
    n->len += l;
    n->caret += l;
    n->modified = true;
    n->lines_dirty = true;
}

static void np_erase(notepad_t *n, int from, int to) {
    if (to <= from) return;
    memmove(n->buf + from, n->buf + to, n->len - to + 1);
    n->len -= to - from;
    n->caret = from;
    n->modified = true;
    n->lines_dirty = true;
}

static bool np_save(notepad_t *n);

static void saveas_cb(void *ctx, const char *path) {
    window_t *w = ctx;
    // okno moglo zostac zamkniete
    bool alive = false;
    for (window_t *x = wm_first(); x; x = x->next) if (x == w && !x->closing) alive = true;
    if (!alive) return;
    notepad_t *n = w->data;
    char p[256];
    if (path[0] != '/') snprintf(p, sizeof p, "/Dokumenty/%s", path);
    else strlcpy(p, path, sizeof p);
    if (!path_valid_name(path_name(p))) { dlg_message(w, "Zapisz jako", fs_error(FS_ENAME), ICON_WARNING); return; }
    strlcpy(n->path, p, sizeof n->path);
    if (np_save(n) && n->close_after_save) { n->modified = false; wm_close(w); }
}

static bool np_save(notepad_t *n) {
    if (!n->path[0]) {
        dlg_input(n->win, "Zapisz jako", "Gdzie zapisac plik? (pelna sciezka albo sama nazwa - wtedy do /Dokumenty)",
                  "/Dokumenty/Bez nazwy.txt", saveas_cb, n->win);
        return false;
    }
    int r = fs_write_file(n->path, n->buf, n->len);
    if (r) { dlg_message(n->win, "Zapisz", fs_error(r), ICON_WARNING); return false; }
    n->modified = false;
    np_title(n);
    explorer_refresh_all();
    return true;
}

static void close_cb(void *ctx, bool yes) {
    window_t *w = ctx;
    bool alive = false;
    for (window_t *x = wm_first(); x; x = x->next) if (x == w && !x->closing) alive = true;
    if (!alive) return;
    notepad_t *n = w->data;
    if (yes) {
        n->close_after_save = true;
        if (np_save(n)) wm_close(w);
    } else {
        n->modified = false;
        wm_close(w);
    }
}

static bool np_close_req(window_t *w) {
    notepad_t *n = w->data;
    if (!n->modified) return true;
    dlg_confirm(w, "Notatnik", "Plik ma niezapisane zmiany. Zapisac je przed zamknieciem?", "Zapisz", close_cb, w);
    return false;
}

static void np_frame(window_t *w, surface_t *s) {
    notepad_t *n = w->data;
    np_index(n);
    int W = ui.cw, H = ui.ch;
    int cw = gfx_char_width(FONT_MONO, 'M');
    int lh = gfx_font_height(FONT_MONO) + 3;
    int gutter = 52;
    int tx = gutter + NP_PAD, ty = NP_TB + 6;
    int vh = H - NP_TB - NP_STATUS - 6, vw = W - tx - 16;
    int vis = MAX(1, vh / lh);

    // ---- klawisze ----
    if (ui.key && !ui.focus) {
        event_t *k = ui.key;
        int l = line_of(n, n->caret);
        bool moved = true, vert = false;
        if (ui_ctrl_key('s')) { np_save(n); return; }
        else if (k->key == KEY_CHAR && (!(k->mods & MOD_CTRL) || (k->mods & (MOD_ALT | MOD_ALTGR)))) {
            char enc[4];
            np_insert(n, enc, utf8_encode(k->ch, enc));
        } else if (k->key == KEY_ENTER) np_insert(n, "\n", 1);
        else if (k->key == KEY_TAB) np_insert(n, "    ", 4);
        else if (k->key == KEY_BACKSPACE) { if (n->caret > 0) np_erase(n, utf8_prev(n->buf, n->caret), n->caret); }
        else if (k->key == KEY_DEL) { if (n->caret < n->len) np_erase(n, n->caret, utf8_next(n->buf, n->caret)); }
        else if (k->key == KEY_LEFT) n->caret = utf8_prev(n->buf, n->caret);
        else if (k->key == KEY_RIGHT) n->caret = utf8_next(n->buf, n->caret);
        else if (k->key == KEY_HOME) n->caret = n->lines[l];
        else if (k->key == KEY_END) n->caret = line_end(n, l);
        else if (k->key == KEY_UP) { n->caret = pos_at(n, l - 1, n->want_col); vert = true; }
        else if (k->key == KEY_DOWN) { n->caret = pos_at(n, l + 1, n->want_col); vert = true; }
        else if (k->key == KEY_PGUP) { n->caret = pos_at(n, l - vis, n->want_col); vert = true; }
        else if (k->key == KEY_PGDN) { n->caret = pos_at(n, l + vis, n->want_col); vert = true; }
        else moved = false;
        if (moved) {
            ui.key = NULL;
            np_index(n);
            if (!vert) n->want_col = col_of(n, n->caret);
            int cl = line_of(n, n->caret);
            if (cl < n->scroll) n->scroll = cl;
            if (cl >= n->scroll + vis) n->scroll = cl - vis + 1;
            int cx = col_of(n, n->caret) * cw;
            if (cx < n->hscroll) n->hscroll = MAX(0, cx - 40);
            if (cx > n->hscroll + vw - 20) n->hscroll = cx - vw + 60;
            np_title(n);
        }
    }

    gfx_fill(s, 0, 0, W, H, RGB(255, 255, 255));
    // ---- pasek narzedzi ----
    gfx_fill(s, 0, 0, W, NP_TB, g_theme.panel);
    gfx_fill(s, 0, NP_TB - 1, W, 1, RGB(222, 228, 234));
    if (ui_button_icon(8, 6, 92, 34, ICON_NEWFILE, "Nowy", BTN_FLAT)) { notepad_open(NULL); return; }
    if (ui_button_icon(104, 6, 100, 34, ICON_FOLDER, "Otworz", BTN_FLAT)) { explorer_open("/Dokumenty"); return; }
    if (ui_button(208, 6, 80, 34, "Zapisz", BTN_FLAT)) { np_save(n); return; }
    if (ui_button(292, 6, 110, 34, "Zapisz jako", BTN_FLAT)) {
        dlg_input(w, "Zapisz jako", "Gdzie zapisac plik? (pelna sciezka albo sama nazwa - wtedy do /Dokumenty)",
                  n->path[0] ? n->path : "/Dokumenty/Bez nazwy.txt", saveas_cb, w);
        return;
    }

    // ---- tekst ----
    if (ui.wheel) n->scroll += ui.wheel * 3;
    n->scroll = CLAMP(n->scroll, 0, MAX(0, n->nlines - vis));
    if (ui.mpressed && ui_hit(0, NP_TB, W - 14, vh + 6)) {
        int l = n->scroll + (ui.my - ty) / lh;
        int col = (ui.mx - tx + n->hscroll + cw / 2) / cw;
        n->caret = pos_at(n, CLAMP(l, 0, n->nlines - 1), MAX(col, 0));
        n->want_col = col_of(n, n->caret);
    }
    gfx_fill(s, 0, NP_TB, gutter, vh + 6, RGB(246, 248, 250));
    int cl = line_of(n, n->caret);
    int sx0 = s->cx0, sy0 = s->cy0, sx1 = s->cx1, sy1 = s->cy1;
    gfx_set_clip(s, 0, NP_TB, W, vh + 6);
    for (int i = 0; i < vis + 1 && n->scroll + i < n->nlines; i++) {
        int l = n->scroll + i;
        int y = ty + i * lh;
        char num[12];
        snprintf(num, sizeof num, "%d", l + 1);
        if (l == cl) gfx_fill(s, gutter, y - 1, W - gutter, lh, RGB(246, 250, 255));
        gfx_text(s, gutter - 10 - gfx_text_width(FONT_MONO, num), y, FONT_MONO, num, l == cl ? g_theme.text : RGB(170, 178, 188));
        int a = n->lines[l], b = line_end(n, l);
        int sxa = s->cx0, sxb = s->cx1;
        gfx_set_clip(s, tx - 2, NP_TB, W - tx - 14, vh + 6);
        gfx_text_n(s, tx - n->hscroll, y, FONT_MONO, n->buf + a, b - a, g_theme.text);
        if (l == cl && ui.focused && (ticks_ms() / 530) % 2 == 0)
            gfx_fill(s, tx - n->hscroll + col_of(n, n->caret) * cw, y - 1, 2, lh - 1, g_theme.accent);
        s->cx0 = sxa, s->cx1 = sxb;
    }
    s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
    int sc = n->scroll * lh;
    ui_scrollbar(W - 14, NP_TB + 4, vh - 2, &sc, n->nlines * lh, vh);
    if (ui.active) n->scroll = sc / lh;

    // ---- stan ----
    gfx_fill(s, 0, H - NP_STATUS, W, NP_STATUS, g_theme.panel);
    gfx_fill(s, 0, H - NP_STATUS, W, 1, RGB(228, 233, 238));
    char st[128];
    snprintf(st, sizeof st, "Wiersz %d, kolumna %d   |   %d znakow   |   UTF-8%s", cl + 1, col_of(n, n->caret) + 1, n->len,
             n->modified ? "   |   niezapisane" : "");
    gfx_text(s, 12, H - NP_STATUS + 6, FONT_SMALL, st, g_theme.text_dim);
}

static bool np_tick(window_t *w) {
    // migajaca karetka
    static u64 last;
    u64 t = ticks_ms() / 530;
    if (w == wm_focused() && t != last) { last = t; return true; }
    return false;
}

static void np_close(window_t *w) {
    notepad_t *n = w->data;
    kfree(n->buf);
    kfree(n->lines);
    kfree(n);
}

void notepad_open(const char *path) {
    notepad_t *n = kzalloc(sizeof *n);
    if (path) {
        u8 *d;
        u64 sz;
        int r = fs_read_file(path, &d, &sz);
        if (r) { dlg_message(NULL, "Notatnik", fs_error(r), ICON_WARNING); kfree(n); return; }
        if (sz > 4 * 1024 * 1024) { kfree(d); dlg_message(NULL, "Notatnik", "Plik jest za duzy (max 4 MB).", ICON_WARNING); kfree(n); return; }
        // usun \r
        int o = 0;
        for (u64 i = 0; i < sz; i++)
            if (d[i] != '\r' && d[i] != 0) d[o++] = d[i];
        d[o] = 0;
        n->buf = (char *)d;
        n->len = o;
        n->cap = sz + 1;
        strlcpy(n->path, path, sizeof n->path);
    } else {
        n->cap = 256;
        n->buf = kzalloc(n->cap);
    }
    n->lines_dirty = true;
    window_t *w = wm_create("notepad", "Notatnik", ICON_NOTEPAD, 760, 540, WF_RESIZABLE, np_frame, n);
    w->close_req = np_close_req;
    w->on_close = np_close;
    w->tick = np_tick;
    n->win = w;
    np_title(n);
}

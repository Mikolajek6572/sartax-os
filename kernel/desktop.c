// Sartax OS - pulpit, pasek zadan, menu Start, ekran logowania
#include "apps.h"
#include "net.h"
#include "bearssl.h"

static u32 *wallpaper;
static bool start_open;
static bool locked;
static char lock_pw[64];
static char lock_msg[64];
static u64 last_refresh;

// ---------------- pomocnicze ----------------
void fmt_datetime(char *buf, size_t n, u32 t) {
    datetime_t d;
    int off = cfg_get_int("strefa", 60);
    unix_to_datetime(t + off * 60, &d);
    snprintf(buf, n, "%02d.%02d.%d %02d:%02d", d.day, d.month, d.year, d.hour, d.min);
}

int file_icon(const char *name, int type) {
    if (type == FT_DIR) {
        if (!strcmp(name, "Pulpit")) return ICON_DESKTOP;
        if (!strcmp(name, "Dokumenty")) return ICON_DOCS;
        if (!strcmp(name, "Obrazy")) return ICON_PICTURES;
        if (!strcmp(name, "Programy")) return ICON_PROGRAMS;
        return ICON_FOLDER;
    }
    if (str_ends_with_ci(name, ".txt") || str_ends_with_ci(name, ".cfg") || str_ends_with_ci(name, ".md") ||
        str_ends_with_ci(name, ".log") || str_ends_with_ci(name, ".ini") || str_ends_with_ci(name, ".csv")) return ICON_TEXT;
    if (str_ends_with_ci(name, ".bmp")) return ICON_IMAGE;
    if (str_ends_with_ci(name, ".html") || str_ends_with_ci(name, ".htm")) return ICON_GLOBE;
    if (str_ends_with_ci(name, ".sap")) return ICON_APP;
    if (str_ends_with_ci(name, ".sxk")) return ICON_UPDATE;
    return ICON_FILE;
}

const char *file_type_name(const char *name, int type) {
    if (type == FT_DIR) return "Folder";
    switch (file_icon(name, type)) {
    case ICON_TEXT: return "Dokument tekstowy";
    case ICON_IMAGE: return "Obraz BMP";
    case ICON_APP: return "Program Sartax";
    case ICON_UPDATE: return "Jadro systemu";
    case ICON_GLOBE: return "Strona HTML";
    }
    return "Plik";
}

void open_path(const char *path) {
    fs_stat_t st;
    if (fs_stat(path, &st)) { dlg_message(NULL, "Blad", "Nie znaleziono pliku.", ICON_WARNING); return; }
    if (st.type == FT_DIR) { explorer_open(path); return; }
    switch (file_icon(path_name(path), FT_FILE)) {
    case ICON_IMAGE: viewer_open(path); break;
    case ICON_APP: app_run_sap(path); break;
    case ICON_UPDATE: store_open(1); break;
    case ICON_GLOBE: { char u[300]; snprintf(u, sizeof u, "plik:%s", path); browser_open(u); break; }
    default: notepad_open(path); break;
    }
}

void app_open(const char *app, const char *arg) {
    if (!strcmp(app, "explorer")) explorer_open(arg ? arg : "/");
    else if (!strcmp(app, "notepad")) notepad_open(arg);
    else if (!strcmp(app, "viewer")) viewer_open(arg);
    else if (!strcmp(app, "store")) store_open(0);
    else if (!strcmp(app, "browser")) browser_open(arg);
    else if (!strcmp(app, "updates")) store_open(1);
    else if (!strcmp(app, "settings")) settings_open(arg);
    else if (!strcmp(app, "about")) settings_open("info");
}

// ---------------- tapeta ----------------
void desktop_reload_wallpaper(void) {
    if (!wallpaper) wallpaper = kmalloc((size_t)g_scr_w * g_scr_h * 4);
    const char *file = cfg_get("tapeta_plik", "");
    if (file[0]) {
        u8 *d;
        u64 sz;
        if (fs_read_file(file, &d, &sz) == 0) {
            image_t *img = image_load_bmp(d, sz);
            kfree(d);
            if (img) {
                surface_t s = { wallpaper, g_scr_w, g_scr_h, g_scr_w, 0, 0, 0, 0, g_scr_w, g_scr_h };
                gfx_fill(&s, 0, 0, g_scr_w, g_scr_h, RGB(0, 0, 0));
                // wypelnij ekran z zachowaniem proporcji
                int w = g_scr_w, h = (int)((i64)img->h * g_scr_w / img->w);
                if (h < g_scr_h) { h = g_scr_h; w = (int)((i64)img->w * g_scr_h / img->h); }
                gfx_blit_scaled(&s, (g_scr_w - w) / 2, (g_scr_h - h) / 2, w, h, img);
                image_free(img);
                wm_invalidate();
                return;
            }
        }
    }
    wallpaper_render(wallpaper, g_scr_w, g_scr_h, g_theme.wallpaper);
    wm_invalidate();
}

// ---------------- ikony pulpitu ----------------
typedef struct {
    char label[64];
    int icon;
    char target[256];       // sciezka albo "app:nazwa"
    bool file;
} dicon_t;

static dicon_t dicons[64];
static int ndicons, dsel = -1;
#define DI_W 92
#define DI_H 92

void desktop_refresh(void) {
    int n = 0;
    struct { const char *l; int ic; const char *t; } sys[] = {
        { "Ten komputer", ICON_COMPUTER, "app:explorer" },
        { "Przegladarka", ICON_GLOBE, "app:browser" },
        { "Pobieralnia", ICON_DOWNLOAD, "app:store" },
        { "Ustawienia", ICON_SETTINGS, "app:settings" },
    };
    for (int i = 0; i < (int)ARRAY_LEN(sys); i++) {
        strlcpy(dicons[n].label, sys[i].l, 64);
        dicons[n].icon = sys[i].ic;
        strlcpy(dicons[n].target, sys[i].t, 256);
        dicons[n].file = false;
        n++;
    }
    fs_entry_t *list;
    int c = fs_list("/Pulpit", &list);
    if (c > 0) {
        for (int i = 0; i < c && n < 64; i++) {
            strlcpy(dicons[n].label, list[i].name, 64);
            dicons[n].icon = file_icon(list[i].name, list[i].type);
            path_join(dicons[n].target, 256, "/Pulpit", list[i].name);
            dicons[n].file = true;
            n++;
        }
        kfree(list);
    }
    if (dsel >= n) dsel = -1;
    ndicons = n;
    wm_invalidate();
}

static void dicon_pos(int i, int *x, int *y) {
    int rows = MAX(1, (wm_work_h() - 16) / DI_H);
    *x = 12 + (i / rows) * DI_W;
    *y = 12 + (i % rows) * DI_H;
}

static int dicon_at(int mx, int my) {
    for (int i = 0; i < ndicons; i++) {
        int x, y;
        dicon_pos(i, &x, &y);
        if (mx >= x && mx < x + DI_W - 4 && my >= y && my < y + DI_H - 4) return i;
    }
    return -1;
}

static void dicon_open(int i) {
    if (i < 0 || i >= ndicons) return;
    if (!strncmp(dicons[i].target, "app:", 4)) app_open(dicons[i].target + 4, NULL);
    else open_path(dicons[i].target);
}

// ---------------- pasek zadan ----------------
static int task_buttons(window_t **out, int max) {
    int n = 0;
    for (window_t *w = wm_first(); w && n < max; w = w->next)
        if (!(w->flags & WF_NOTASKBAR) && !w->closing) out[n++] = w;
    return n;
}

static void tb_button_rect(int i, int n, int *x, int *w) {
    int avail = g_scr_w - 56 - 210;
    int bw = n ? MIN(200, avail / n) : 200;
    *x = 56 + i * bw;
    *w = bw - 4;
}

static void draw_taskbar(surface_t *s) {
    int y = g_scr_h - TASKBAR_H;
    gfx_fill(s, 0, y, g_scr_w, TASKBAR_H, RGBA(16, 22, 30, 225));
    gfx_fill(s, 0, y, g_scr_w, 1, RGBA(255, 255, 255, 30));
    // start
    bool sh = g_mouse_y >= y && g_mouse_x < 52;
    if (start_open || sh) gfx_round_rect(s, 4, y + 4, 44, TASKBAR_H - 8, 8, RGBA(255, 255, 255, start_open ? 60 : 35));
    gfx_blit(s, 10, y + 7, icon_get(ICON_LOGO, 32));
    // okna
    window_t *list[24];
    int n = task_buttons(list, 24);
    for (int i = 0; i < n; i++) {
        int bx, bw;
        tb_button_rect(i, n, &bx, &bw);
        window_t *w = list[i];
        bool act = w == wm_focused();
        bool hov = g_mouse_y >= y && g_mouse_x >= bx && g_mouse_x < bx + bw;
        u32 bg = act ? RGBA(255, 255, 255, 50) : hov ? RGBA(255, 255, 255, 30) : RGBA(255, 255, 255, 12);
        gfx_round_rect(s, bx, y + 5, bw, TASKBAR_H - 10, 7, bg);
        if (act) gfx_round_rect(s, bx + bw / 2 - 14, y + TASKBAR_H - 8, 28, 3, 1, g_theme.accent);
        if (w->icon >= 0) gfx_blit_alpha(s, bx + 10, y + 15, icon_get(w->icon, 16), (w->flags & WF_MINIMIZED) ? 150 : 255);
        gfx_set_clip(s, bx, y, bw - 8, TASKBAR_H);
        gfx_text_ellipsis(s, bx + 34, y + 15, bw - 42, FONT_UI, w->title,
                          (w->flags & WF_MINIMIZED) ? RGB(170, 180, 190) : RGB(240, 244, 248));
        gfx_reset(s);
    }
    // zegar
    datetime_t d;
    local_time(&d);
    char t1[16], t2[16];
    snprintf(t1, sizeof t1, "%02d:%02d", d.hour, d.min);
    snprintf(t2, sizeof t2, "%02d.%02d.%d", d.day, d.month, d.year);
    int cx = g_scr_w - 90;
    gfx_text_center(s, cx, y + 6, 84, FONT_BOLD, t1, RGB(240, 244, 248));
    gfx_text_center(s, cx, y + 25, 84, FONT_SMALL, t2, RGB(190, 200, 210));
    // siec
    int nx = g_scr_w - 200;
    u32 nc = net_ready() ? RGB(90, 220, 140) : (net_has_card() ? RGB(240, 190, 70) : RGB(150, 160, 170));
    for (int i = 0; i < 4; i++) gfx_fill(s, nx + i * 5, y + 30 - i * 4, 3, 4 + i * 4, i < (net_ready() ? 4 : 1) ? nc : RGBA(255, 255, 255, 60));
    gfx_text_ellipsis(s, nx + 26, y + 15, 80, FONT_SMALL, net_ready() ? "Internet" : net_has_card() ? "Laczenie..." : "Brak sieci",
                      RGB(200, 208, 216));
}

static void taskbar_click(int mx, bool right) {
    if (mx < 52) { start_open = !start_open; wm_invalidate(); return; }
    window_t *list[24];
    int n = task_buttons(list, 24);
    for (int i = 0; i < n; i++) {
        int bx, bw;
        tb_button_rect(i, n, &bx, &bw);
        if (mx >= bx && mx < bx + bw) {
            window_t *w = list[i];
            if (right) { wm_close(w); return; }
            if (w == wm_focused() && !(w->flags & WF_MINIMIZED)) wm_minimize(w);
            else wm_focus(w);
            return;
        }
    }
    if (mx >= g_scr_w - 90) settings_open("czas");
    else if (mx >= g_scr_w - 200) settings_open("siec");
}

// ---------------- menu Start ----------------
typedef struct { const char *label; int icon; const char *app; } start_item_t;
static const start_item_t start_items[] = {
    { "Pliki", ICON_COMPUTER, "explorer" },
    { "Przegladarka", ICON_GLOBE, "browser" },
    { "Notatnik", ICON_NOTEPAD, "notepad" },
    { "Podglad obrazow", ICON_VIEWER, "viewer" },
    { "Pobieralnia", ICON_DOWNLOAD, "store" },
    { "Aktualizacje", ICON_UPDATE, "updates" },
    { "Ustawienia", ICON_SETTINGS, "settings" },
    { "O systemie", ICON_INFO, "about" },
};
static char progs[12][64];
static int nprogs;

static void start_rect(int *x, int *y, int *w, int *h) {
    *w = 420;
    *h = 470;
    *x = 6;
    *y = g_scr_h - TASKBAR_H - *h - 6;
}

static void start_load_programs(void) {
    nprogs = 0;
    fs_entry_t *list;
    int c = fs_list("/Programy", &list);
    if (c <= 0) return;
    for (int i = 0; i < c && nprogs < 12; i++)
        if (str_ends_with_ci(list[i].name, ".sap")) strlcpy(progs[nprogs++], list[i].name, 64);
    kfree(list);
}

// pozycje kafelkow: 0..n-1 aplikacje, potem programy; -2 restart, -3 wylacz
static int start_hit(int mx, int my, int *kind) {
    int x, y, w, h;
    start_rect(&x, &y, &w, &h);
    int n = ARRAY_LEN(start_items) + nprogs;
    for (int i = 0; i < n; i++) {
        int col = i % 4, row = i / 4;
        int tx = x + 16 + col * 98, ty = y + 92 + row * 92;
        if (mx >= tx && mx < tx + 94 && my >= ty && my < ty + 86) { *kind = 1; return i; }
    }
    int by = y + h - 50;
    if (my >= by && my < by + 36) {
        if (mx >= x + w - 290 && mx < x + w - 124) { *kind = 2; return 0; }
        if (mx >= x + w - 116 && mx < x + w - 16) { *kind = 3; return 0; }
    }
    *kind = 0;
    return -1;
}

static void draw_start(surface_t *s) {
    if (!start_open) return;
    int x, y, w, h;
    start_rect(&x, &y, &w, &h);
    gfx_shadow(s, x, y, w, h, 14, 26, 130);
    gfx_round_rect(s, x, y, w, h, 14, RGB(250, 252, 254));
    gfx_round_outline(s, x, y, w, h, 14, RGBA(0, 0, 0, 50));
    // naglowek
    gfx_round_rect_grad(s, x, y, w, 76, 14, color_lighten(g_theme.accent, 20), g_theme.accent);
    gfx_fill(s, x, y + 60, w, 16, g_theme.accent);
    gfx_blit(s, x + 16, y + 14, icon_get(ICON_USER, 48));
    gfx_text(s, x + 76, y + 18, FONT_TITLE, cfg_get("uzytkownik", "Uzytkownik"), RGB(255, 255, 255));
    char sub[96];
    snprintf(sub, sizeof sub, "Sartax OS %s  •  %s", SARTAX_VERSION, cfg_get("komputer", "Sartax-PC"));
    gfx_text(s, x + 76, y + 44, FONT_SMALL, sub, RGBA(255, 255, 255, 220));
    int kind, hi = start_hit(g_mouse_x, g_mouse_y, &kind);
    int n = ARRAY_LEN(start_items) + nprogs;
    for (int i = 0; i < n; i++) {
        int col = i % 4, row = i / 4;
        int tx = x + 16 + col * 98, ty = y + 92 + row * 92;
        if (kind == 1 && hi == i) gfx_round_rect(s, tx, ty, 94, 86, 10, g_theme.hover);
        const char *label;
        int icon;
        char nm[64];
        if (i < (int)ARRAY_LEN(start_items)) { label = start_items[i].label; icon = start_items[i].icon; }
        else {
            strlcpy(nm, progs[i - ARRAY_LEN(start_items)], sizeof nm);
            char *dot = strrchr(nm, '.');
            if (dot) *dot = 0;
            label = nm;
            icon = ICON_APP;
        }
        gfx_blit(s, tx + 31, ty + 10, icon_get(icon, 32));
        gfx_set_clip(s, tx, ty, 94, 86);
        gfx_text_center(s, tx, ty + 52, 94, FONT_SMALL, label, g_theme.text);
        gfx_reset(s);
    }
    int by = y + h - 50;
    gfx_fill(s, x, by - 12, w, 1, RGB(226, 231, 236));
    char fr[32];
    fmt_size(fr, sizeof fr, fs_free_bytes());
    char info[64];
    snprintf(info, sizeof info, "%s wolne", fr);
    gfx_text(s, x + 18, by + 10, FONT_SMALL, info, g_theme.text_dim);
    bool h1 = kind == 2, h2 = kind == 3;
    gfx_round_rect(s, x + w - 290, by, 166, 36, 8, h1 ? g_theme.hover : RGB(240, 244, 248));
    gfx_blit(s, x + w - 280, by + 10, icon_get(ICON_RESTART, 16));
    gfx_text(s, x + w - 258, by + 10, FONT_UI, "Uruchom ponownie", g_theme.text);
    gfx_round_rect(s, x + w - 116, by, 100, 36, 8, h2 ? RGB(250, 225, 222) : RGB(240, 244, 248));
    gfx_blit(s, x + w - 106, by + 10, icon_get(ICON_POWER, 16));
    gfx_text(s, x + w - 84, by + 10, FONT_UI, "Wylacz", g_theme.text);
}

static void shutdown_screen(const char *msg) {
    surface_t *s = &g_fb;
    gfx_reset(s);
    gfx_fill(s, 0, 0, g_scr_w, g_scr_h, RGB(16, 22, 30));
    gfx_blit(s, g_scr_w / 2 - 24, g_scr_h / 2 - 70, icon_get(ICON_LOGO, 48));
    gfx_text_center(s, 0, g_scr_h / 2, g_scr_w, FONT_TITLE, msg, RGB(240, 244, 248));
    gfx_present_full();
}

static void do_power(void *ctx, bool yes) {
    if (!yes) return;
    bool reboot = ctx != NULL;
    shutdown_screen(reboot ? "Ponowne uruchamianie..." : "Zamykanie systemu...");
    fs_sync();
    sleep_ms(400);
    if (reboot) sys_reboot();
    shutdown_screen("Mozesz teraz bezpiecznie wylaczyc komputer.");
    sys_poweroff();
}

static bool start_mouse(event_t *ev, int prev) {
    if (!start_open) return false;
    bool pressed = (ev->buttons & 1) && !(prev & 1);
    bool rpressed = (ev->buttons & 2) && !(prev & 2);
    if (!pressed && !rpressed) return true;
    int x, y, w, h;
    start_rect(&x, &y, &w, &h);
    bool inside = ev->x >= x && ev->x < x + w && ev->y >= y && ev->y < y + h;
    if (!inside) {
        start_open = false;
        // klik w przycisk Start ma tylko zamknac
        if (ev->y >= g_scr_h - TASKBAR_H && ev->x < 52) return true;
        return false;
    }
    int kind, i = start_hit(ev->x, ev->y, &kind);
    if (!pressed) return true;
    if (kind == 1) {
        start_open = false;
        if (i < (int)ARRAY_LEN(start_items)) app_open(start_items[i].app, NULL);
        else {
            char p[256];
            path_join(p, sizeof p, "/Programy", progs[i - ARRAY_LEN(start_items)]);
            app_run_sap(p);
        }
    } else if (kind == 2) {
        start_open = false;
        dlg_confirm(NULL, "Uruchom ponownie", "Czy na pewno uruchomic komputer ponownie?", "Uruchom", do_power, (void *)1);
    } else if (kind == 3) {
        start_open = false;
        dlg_confirm(NULL, "Wylacz", "Czy na pewno wylaczyc komputer?", "Wylacz", do_power, NULL);
    }
    wm_invalidate();
    return true;
}

// ---------------- ekran blokady ----------------
static void hash_pw(const char *pw, char *out) {
    br_sha256_context c;
    u8 h[32];
    br_sha256_init(&c);
    br_sha256_update(&c, "sartax:", 7);
    br_sha256_update(&c, pw, strlen(pw));
    br_sha256_out(&c, h);
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", h[i]);
}

void password_hash(const char *pw, char *out65) { hash_pw(pw, out65); }

static void draw_lock(surface_t *s) {
    gfx_fill(s, 0, 0, g_scr_w, g_scr_h, RGBA(10, 20, 30, 110));
    int cx = g_scr_w / 2, cy = g_scr_h / 2 - 60;
    datetime_t d;
    local_time(&d);
    char t[16];
    snprintf(t, sizeof t, "%02d:%02d", d.hour, d.min);
    gfx_text_center(s, 0, 60, g_scr_w, FONT_HUGE, t, RGB(255, 255, 255));
    gfx_blit(s, cx - 24, cy - 30, icon_get(ICON_USER, 48));
    gfx_text_center(s, 0, cy + 28, g_scr_w, FONT_TITLE, cfg_get("uzytkownik", "Uzytkownik"), RGB(255, 255, 255));
    int bw = 280, bx = cx - bw / 2, by = cy + 64;
    gfx_round_rect(s, bx, by, bw, 38, 9, RGBA(255, 255, 255, 235));
    char stars[64];
    int n = 0;
    for (int p = 0; lock_pw[p] && n < 40; p = utf8_next(lock_pw, p)) stars[n++] = '*';
    stars[n] = 0;
    if (n) gfx_text(s, bx + 14, by + 11, FONT_BOLD, stars, g_theme.text);
    else gfx_text(s, bx + 14, by + 11, FONT_UI, "Haslo", RGB(150, 158, 168));
    if ((ticks_ms() / 530) % 2 == 0) gfx_fill(s, bx + 14 + gfx_text_width(FONT_BOLD, stars), by + 9, 1, 20, g_theme.text);
    gfx_text_center(s, 0, by + 50, g_scr_w, FONT_UI, lock_msg[0] ? lock_msg : "Wpisz haslo i nacisnij Enter",
                    lock_msg[0] ? RGB(255, 210, 200) : RGB(230, 236, 240));
}

static void lock_key(event_t *ev) {
    if (ev->key == KEY_ENTER) {
        char h[65];
        hash_pw(lock_pw, h);
        if (!strcmp(h, cfg_get("haslo", ""))) { locked = false; desktop_refresh(); }
        else strlcpy(lock_msg, "Nieprawidlowe haslo", sizeof lock_msg);
        memset(lock_pw, 0, sizeof lock_pw);
    } else if (ev->key == KEY_BACKSPACE) {
        int l = strlen(lock_pw);
        if (l) lock_pw[utf8_prev(lock_pw, l)] = 0;
    } else if (ev->key == KEY_CHAR) {
        int l = strlen(lock_pw);
        char enc[4];
        int n = utf8_encode(ev->ch, enc);
        if (l + n < (int)sizeof lock_pw - 1) { memcpy(lock_pw + l, enc, n); lock_pw[l + n] = 0; }
        lock_msg[0] = 0;
    }
}

// ---------------- powloka ----------------
static void sh_draw(surface_t *s) {
    gfx_copy(s, 0, 0, wallpaper, g_scr_w, g_scr_h, g_scr_w);
    if (locked) return;
    for (int i = 0; i < ndicons; i++) {
        int x, y;
        dicon_pos(i, &x, &y);
        bool hov = !wm_first() || 1;
        (void)hov;
        bool h = g_mouse_x >= x && g_mouse_x < x + DI_W - 4 && g_mouse_y >= y && g_mouse_y < y + DI_H - 4;
        if (i == dsel) gfx_round_rect(s, x, y, DI_W - 4, DI_H - 4, 8, RGBA(255, 255, 255, 70));
        else if (h) gfx_round_rect(s, x, y, DI_W - 4, DI_H - 4, 8, RGBA(255, 255, 255, 35));
        gfx_blit(s, x + (DI_W - 4 - 48) / 2, y + 6, icon_get(dicons[i].icon, 48));
        // podpis z cieniem, max 2 linie
        int tw = DI_W - 8;
        const char *lab = dicons[i].label;
        gfx_set_clip(s, x, y, DI_W - 4, DI_H - 4);
        if (gfx_text_width(FONT_SMALL, lab) <= tw) {
            int lx = x + (DI_W - 4 - gfx_text_width(FONT_SMALL, lab)) / 2;
            gfx_text(s, lx + 1, y + 61, FONT_SMALL, lab, RGBA(0, 0, 0, 160));
            gfx_text(s, lx, y + 60, FONT_SMALL, lab, RGB(255, 255, 255));
        } else {
            gfx_text_wrap(s, x + 3, y + 61, tw, FONT_SMALL, lab, RGBA(0, 0, 0, 160), 13);
            gfx_text_wrap(s, x + 2, y + 60, tw, FONT_SMALL, lab, RGB(255, 255, 255), 13);
        }
        gfx_reset(s);
    }
}

static void sh_overlay(surface_t *s) {
    if (locked) { draw_lock(s); return; }
    draw_taskbar(s);
    draw_start(s);
}

static void desk_menu_cb(void *ctx, int id);

static void desk_new_cb(void *ctx, const char *name) {
    if (!path_valid_name(name)) { dlg_message(NULL, "Blad", fs_error(FS_ENAME), ICON_WARNING); return; }
    char p[256];
    path_join(p, sizeof p, "/Pulpit", name);
    int r = ctx ? fs_write_file(p, "", 0) : fs_mkdir(p);
    if (r) dlg_message(NULL, "Blad", fs_error(r), ICON_WARNING);
    desktop_refresh();
    explorer_refresh_all();
}

static void desk_rename_cb(void *ctx, const char *name) {
    int i = (int)(intptr_t)ctx;
    if (i < 0 || i >= ndicons || !dicons[i].file) return;
    if (!path_valid_name(name)) { dlg_message(NULL, "Blad", fs_error(FS_ENAME), ICON_WARNING); return; }
    char p[256];
    path_join(p, sizeof p, "/Pulpit", name);
    int r = fs_rename(dicons[i].target, p);
    if (r) dlg_message(NULL, "Blad", fs_error(r), ICON_WARNING);
    desktop_refresh();
    explorer_refresh_all();
}

enum { DM_OPEN = 1, DM_RENAME, DM_DELETE, DM_NEWFOLDER, DM_NEWFILE, DM_REFRESH, DM_SETTINGS, DM_PASTE, DM_COPY, DM_CUT };

static void desk_menu_cb(void *ctx, int id) {
    int i = dsel;
    switch (id) {
    case DM_OPEN: dicon_open(i); break;
    case DM_RENAME:
        if (i >= 0 && dicons[i].file) dlg_input(NULL, "Zmien nazwe", "Nowa nazwa:", dicons[i].label, desk_rename_cb, (void *)(intptr_t)i);
        break;
    case DM_DELETE:
        if (i >= 0 && dicons[i].file) {
            char p[1][256];
            strlcpy(p[0], dicons[i].target, 256);
            explorer_delete_paths(NULL, p, 1);
        }
        break;
    case DM_COPY: case DM_CUT:
        if (i >= 0 && dicons[i].file) {
            strlcpy(g_clip_paths[0], dicons[i].target, 256);
            g_clip_count = 1;
            g_clip_cut = id == DM_CUT;
        }
        break;
    case DM_PASTE: explorer_paste(NULL, "/Pulpit"); break;
    case DM_NEWFOLDER: dlg_input(NULL, "Nowy folder", "Nazwa folderu:", "Nowy folder", desk_new_cb, NULL); break;
    case DM_NEWFILE: dlg_input(NULL, "Nowy plik", "Nazwa pliku:", "Nowy dokument.txt", desk_new_cb, (void *)1); break;
    case DM_REFRESH: desktop_refresh(); break;
    case DM_SETTINGS: settings_open("wyglad"); break;
    }
}

static bool sh_mouse(event_t *ev, int prev) {
    if (locked) return true;
    bool pressed = (ev->buttons & 1) && !(prev & 1);
    bool rpressed = (ev->buttons & 2) && !(prev & 2);
    if (ev->y >= g_scr_h - TASKBAR_H) {
        if (pressed || rpressed) taskbar_click(ev->x, rpressed);
        return true;
    }
    static u64 last_t;
    static int last_i = -2;
    if (pressed) {
        int i = dicon_at(ev->x, ev->y);
        u64 now = ticks_ms();
        if (i >= 0 && i == last_i && now - last_t < 550) { dicon_open(i); last_i = -2; }
        else { last_i = i; last_t = now; }
        dsel = i;
        wm_invalidate();
    }
    if (rpressed) {
        int i = dicon_at(ev->x, ev->y);
        dsel = i;
        if (i >= 0) {
            menu_item_t it[] = {
                { "Otworz", ICON_FOLDER, DM_OPEN, false },
                { NULL, -1, 0, false },
                { "Kopiuj", -1, DM_COPY, !dicons[i].file },
                { "Wytnij", -1, DM_CUT, !dicons[i].file },
                { "Zmien nazwe", -1, DM_RENAME, !dicons[i].file },
                { "Usun", ICON_TRASH, DM_DELETE, !dicons[i].file },
            };
            menu_open(ev->x, ev->y, it, ARRAY_LEN(it), desk_menu_cb, NULL);
        } else {
            menu_item_t it[] = {
                { "Nowy folder", ICON_NEWFOLDER, DM_NEWFOLDER, false },
                { "Nowy plik tekstowy", ICON_NEWFILE, DM_NEWFILE, false },
                { "Wklej", -1, DM_PASTE, g_clip_count == 0 },
                { NULL, -1, 0, false },
                { "Odswiez", ICON_REFRESH, DM_REFRESH, false },
                { "Zmien tapete...", ICON_SETTINGS, DM_SETTINGS, false },
            };
            menu_open(ev->x, ev->y, it, ARRAY_LEN(it), desk_menu_cb, NULL);
        }
    }
    return true;
}

static void sh_key(event_t *ev) {
    if (locked) { lock_key(ev); wm_invalidate(); return; }
    if (ev->key == KEY_WIN) { start_open = !start_open; return; }
    if (ev->key == KEY_ESC) { start_open = false; return; }
    if (ev->key == KEY_ENTER && dsel >= 0) dicon_open(dsel);
    if (ev->key == KEY_DEL && dsel >= 0) desk_menu_cb(NULL, DM_DELETE);
    if (ev->key == KEY_F2 && dsel >= 0) desk_menu_cb(NULL, DM_RENAME);
    if (ev->key == KEY_F5) desktop_refresh();
    if (ev->key == KEY_RIGHT || ev->key == KEY_DOWN) dsel = MIN(dsel + 1, ndicons - 1);
    if (ev->key == KEY_LEFT || ev->key == KEY_UP) dsel = MAX(dsel - 1, 0);
}

static int last_min = -1;
static void sh_tick(void) {
    datetime_t d;
    u64 now = ticks_ms();
    static u64 last_clock;
    if (now - last_clock > 1000) {
        last_clock = now;
        local_time(&d);
        if (d.min != last_min) { last_min = d.min; wm_invalidate(); }
    }
    if (locked && (now / 530) % 2 != ((now - 16) / 530) % 2) wm_invalidate();
    if (now - last_refresh > 2000 && !locked) {
        last_refresh = now;
        int before = ndicons;
        desktop_refresh();
        start_load_programs();
        (void)before;
    }
    static bool last_net;
    if (net_ready() != last_net) { last_net = net_ready(); wm_invalidate(); }
}

static shell_t shell = { sh_draw, sh_mouse, sh_key, sh_overlay, start_mouse, sh_tick };

void desktop_start(void) {
    cfg_load();
    wm_init();
    desktop_reload_wallpaper();
    const char *pw = cfg_get("haslo", "");
    locked = pw[0] != 0;
    // brakujace foldery
    const char *dirs[] = { "/Pulpit", "/Dokumenty", "/Pobrane", "/Obrazy", "/Programy", "/System" };
    for (int i = 0; i < (int)ARRAY_LEN(dirs); i++)
        if (!fs_exists(dirs[i])) fs_mkdir(dirs[i]);
    desktop_refresh();
    start_load_programs();
    wm_set_shell(&shell);
    if (g_boot->prev_version)
        dlg_message(NULL, "Poprzednia wersja", "Uruchomiono poprzednia wersje systemu (przytrzymany Shift).", ICON_INFO);
    else if (!locked && cfg_get_int("pierwszy_start", 0)) {
        cfg_set("pierwszy_start", "0");
        cfg_save();
        char msg[256];
        snprintf(msg, sizeof msg, "Witaj, %s! Sartax OS jest gotowy. Kliknij logo w lewym dolnym rogu, aby otworzyc menu Start.",
                 cfg_get("uzytkownik", ""));
        dlg_message(NULL, "Witaj w Sartax OS", msg, ICON_LOGO);
    }
    wm_loop();
}

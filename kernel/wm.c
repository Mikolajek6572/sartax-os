// Sartax OS - menedzer okien
#include "ui.h"
#include "fs.h"

ui_t ui;
theme_t g_theme;

static window_t *wins;              // od dolu do gory
static window_t *focused;
static shell_t *shell;
static bool dirty = true;
static u8 mb_prev;
static window_t *drag_win, *resize_win, *capture_win;
static int drag_dx, drag_dy;
static u64 last_click_t;
static int last_click_x, last_click_y;
static u64 last_render;
static bool focus_claimed;
u8 g_last_mouse_mods;
static int tb_caret = -1, tb_scroll;
static u64 tb_caret_for;

__attribute__((weak)) void net_poll(void) {}

// ---------------- motyw i tapety ----------------
const char *g_accent_names[] = { "Morski", "Niebieski", "Zielony", "Pomaranczowy", "Czerwony", "Zolty", "Grafitowy" };
const u32 g_accent_colors[] = { RGB(16, 160, 160), RGB(30, 130, 230), RGB(40, 165, 90), RGB(240, 135, 30),
                                RGB(225, 70, 60), RGB(215, 165, 0), RGB(80, 95, 110) };
const char *g_wallpaper_names[] = { "Tecza", "Ocean", "Zachod slonca", "Las", "Grafit", "Gory" };
int accent_count(void) { return ARRAY_LEN(g_accent_colors); }
int wallpaper_count(void) { return ARRAY_LEN(g_wallpaper_names); }

void theme_apply(int a, int wp) {
    a = CLAMP(a, 0, accent_count() - 1);
    g_theme.accent = g_accent_colors[a];
    g_theme.accent2 = color_lighten(g_theme.accent, -40);
    g_theme.win_bg = RGB(248, 250, 252);
    g_theme.panel = RGB(238, 242, 246);
    g_theme.border = RGB(200, 208, 216);
    g_theme.text = RGB(28, 36, 46);
    g_theme.text_dim = RGB(105, 116, 128);
    g_theme.title_bg = RGB(240, 244, 248);
    g_theme.sel = color_mix(RGB(255, 255, 255), g_theme.accent, 60);
    g_theme.hover = color_mix(RGB(255, 255, 255), g_theme.accent, 28);
    g_theme.wallpaper = CLAMP(wp, 0, wallpaper_count() - 1);
}

typedef struct { int x, y, r; u32 c; } blob_t;

// sinus w stopniach * 1000 (przyblizenie Bhaskary)
static int isin1000(int d) {
    d %= 360;
    if (d < 0) d += 360;
    int sg = 1;
    if (d >= 180) { d -= 180; sg = -1; }
    return sg * 4000 * d * (180 - d) / (40500 - d * (180 - d));
}

void wallpaper_render(u32 *px, int w, int h, int idx) {
    u32 c1, c2, c3;
    blob_t bl[6];
    int nb = 0;
    switch (idx) {
    default:
    case 0: c1 = RGB(20, 170, 160); c2 = RGB(90, 200, 120); c3 = RGB(250, 185, 70);
        bl[nb++] = (blob_t){ w * 8 / 10, h * 2 / 10, h * 6 / 10, RGB(255, 210, 90) };
        bl[nb++] = (blob_t){ w * 15 / 100, h * 85 / 100, h * 7 / 10, RGB(30, 140, 210) };
        bl[nb++] = (blob_t){ w / 2, h / 2, h * 4 / 10, RGB(120, 220, 170) };
        break;
    case 1: c1 = RGB(10, 50, 100); c2 = RGB(20, 110, 170); c3 = RGB(40, 190, 200);
        bl[nb++] = (blob_t){ w * 3 / 10, h * 3 / 10, h * 5 / 10, RGB(50, 160, 230) };
        bl[nb++] = (blob_t){ w * 8 / 10, h * 8 / 10, h * 6 / 10, RGB(60, 220, 200) };
        break;
    case 2: c1 = RGB(250, 120, 50); c2 = RGB(245, 170, 70); c3 = RGB(255, 225, 140);
        bl[nb++] = (blob_t){ w / 2, h * 7 / 10, h * 4 / 10, RGB(255, 240, 180) };
        bl[nb++] = (blob_t){ w * 1 / 10, h * 1 / 10, h * 6 / 10, RGB(220, 70, 50) };
        break;
    case 3: c1 = RGB(20, 80, 50); c2 = RGB(50, 140, 80); c3 = RGB(170, 210, 110);
        bl[nb++] = (blob_t){ w * 7 / 10, h * 2 / 10, h * 5 / 10, RGB(200, 230, 140) };
        bl[nb++] = (blob_t){ w * 2 / 10, h * 9 / 10, h * 6 / 10, RGB(20, 100, 70) };
        break;
    case 4: c1 = RGB(30, 36, 44); c2 = RGB(50, 60, 72); c3 = RGB(80, 95, 110);
        bl[nb++] = (blob_t){ w * 8 / 10, h * 3 / 10, h * 5 / 10, RGB(60, 110, 140) };
        break;
    case 5: c1 = RGB(110, 180, 235); c2 = RGB(170, 215, 245); c3 = RGB(250, 225, 190); break;
    }
    for (int y = 0; y < h; y++) {
        u32 *row = px + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            int t = (x * 128 / w) + (y * 384 / h);   // przekatna 0..511
            u32 c = t < 256 ? color_mix(c1, c2, t) : color_mix(c2, c3, t - 256);
            for (int i = 0; i < nb; i++) {
                int dx = x - bl[i].x, dy = y - bl[i].y;
                int d2 = dx * dx + dy * dy, r2 = bl[i].r * bl[i].r;
                if (d2 >= r2) continue;
                int f = 255 - (int)((i64)d2 * 255 / r2);
                f = f * f / 255 * 160 / 255;
                c = color_mix(c, bl[i].c, f);
            }
            row[x] = c | 0xFF000000;
        }
    }
    if (idx == 5) {
        // warstwy gor
        static const u32 cols[3] = { RGB(120, 150, 180), RGB(70, 125, 110), RGB(40, 90, 70) };
        for (int l = 0; l < 3; l++) {
            int base = h * (55 + l * 13) / 100;
            for (int x = 0; x < w; x++) {
                int d1 = (x * (2 + l) * 360 / w + l * 70) % 360, d2 = (x * (5 + l * 3) * 360 / w + 40 + l * 25) % 360;
                int top = base - isin1000(d1) * h / (12000 + l * 3000) - isin1000(d2) * h / (40000 - l * 6000);
                for (int y = MAX(top, 0); y < h; y++) {
                    u32 *p = &px[(size_t)y * w + x];
                    *p = color_mix(cols[l], RGB(30, 50, 40), (y - top) * 120 / h) | 0xFF000000;
                }
            }
        }
    }
}

// ---------------- czas lokalny ----------------
void local_time(datetime_t *dt) {
    u32 t = rtc_unix();
    int off = cfg_get_int("strefa", 60);         // minuty
    bool dst = cfg_get_int("czas_letni", 1);
    t += off * 60;
    unix_to_datetime(t, dt);
    if (dst) {
        // UE: od ostatniej niedzieli marca 1:00 UTC do ostatniej niedzieli pazdziernika 1:00 UTC
        datetime_t a = { dt->year, 3, 31, 1, 0, 0, 0 }, b = { dt->year, 10, 31, 1, 0, 0, 0 };
        u32 ta = datetime_to_unix(&a), tb = datetime_to_unix(&b);
        datetime_t tmp;
        unix_to_datetime(ta, &tmp);
        ta -= tmp.wday * 86400;
        unix_to_datetime(tb, &tmp);
        tb -= tmp.wday * 86400;
        u32 utc = t - off * 60;
        if (utc >= ta && utc < tb) unix_to_datetime(t + 3600, dt);
    }
}

// ---------------- okna ----------------
void wm_init(void) {
    theme_apply(cfg_get_int("akcent", 0), cfg_get_int("tapeta", 0));
}

void wm_set_shell(shell_t *sh) { shell = sh; }
int wm_work_h(void) { return g_scr_h - TASKBAR_H; }
void wm_invalidate(void) { dirty = true; }
window_t *wm_focused(void) { return focused; }
window_t *wm_first(void) { return wins; }

window_t *wm_find_app(const char *app) {
    for (window_t *w = wins; w; w = w->next)
        if (!w->closing && !strcmp(w->app, app)) return w;
    return NULL;
}

static void unlink_win(window_t *w) {
    window_t **pp = &wins;
    while (*pp && *pp != w) pp = &(*pp)->next;
    if (*pp) *pp = w->next;
    w->next = NULL;
}

static void push_top(window_t *w) {
    window_t **pp = &wins;
    while (*pp) pp = &(*pp)->next;
    *pp = w;
    w->next = NULL;
}

static window_t *modal_child(window_t *w) {
    for (window_t *c = wins; c; c = c->next)
        if (c->owner == w && (c->flags & WF_MODAL) && !c->closing) return c;
    return NULL;
}

bool wm_any_modal_for(window_t *w) { return modal_child(w) != NULL; }

void wm_focus(window_t *w) {
    if (!w) { focused = NULL; dirty = true; return; }
    window_t *m;
    while ((m = modal_child(w))) w = m;
    if (w->flags & WF_MINIMIZED) w->flags &= ~WF_MINIMIZED;
    unlink_win(w);
    push_top(w);
    // dialogi wlasciciela tez na wierzch
    if (focused != w) { ui.focus = 0; tb_caret = -1; }
    focused = w;
    dirty = true;
}

window_t *wm_create(const char *app, const char *title, int icon, int w, int h, u32 flags,
                    void (*frame)(window_t *, surface_t *), void *data) {
    window_t *win = kzalloc(sizeof(window_t));
    strlcpy(win->app, app ? app : "", sizeof win->app);
    strlcpy(win->title, title, sizeof win->title);
    win->icon = icon;
    win->flags = flags;
    win->frame = frame;
    win->data = data;
    win->min_w = MIN(w, 240);
    win->min_h = MIN(h, 160);
    w = MIN(w, g_scr_w);
    h = MIN(h, wm_work_h());
    win->w = w;
    win->h = h;
    static int cascade;
    if (flags & (WF_CENTER | WF_MODAL)) {
        win->x = (g_scr_w - w) / 2;
        win->y = (wm_work_h() - h) / 2;
    } else {
        win->x = 80 + cascade * 28;
        win->y = 40 + cascade * 28;
        cascade = (cascade + 1) % 8;
        if (win->x + w > g_scr_w) win->x = MAX(0, g_scr_w - w);
        if (win->y + h > wm_work_h()) win->y = MAX(0, wm_work_h() - h);
    }
    push_top(win);
    wm_focus(win);
    return win;
}

void wm_close(window_t *w) {
    if (!w || w->closing) return;
    if (w->close_req && !w->close_req(w)) return;
    w->closing = true;
    for (window_t *c = wins; c; c = c->next)
        if (c->owner == w) wm_close(c);
    dirty = true;
}

static void reap(void) {
    bool again = true;
    while (again) {
        again = false;
        for (window_t *w = wins; w; w = w->next) {
            if (!w->closing) continue;
            unlink_win(w);
            if (focused == w) focused = NULL;
            if (capture_win == w) capture_win = NULL;
            if (drag_win == w) drag_win = NULL;
            if (resize_win == w) resize_win = NULL;
            if (w->on_close) w->on_close(w);
            window_t *owner = w->owner;
            kfree(w);
            if (!focused) {
                if (owner && !owner->closing) wm_focus(owner);
                else {
                    window_t *top = NULL;
                    for (window_t *x = wins; x; x = x->next)
                        if (!(x->flags & WF_MINIMIZED) && !x->closing) top = x;
                    focused = top;
                }
            }
            again = true;
            break;
        }
    }
}

void wm_set_title(window_t *w, const char *t) { strlcpy(w->title, t, sizeof w->title); dirty = true; }

void wm_minimize(window_t *w) {
    w->flags |= WF_MINIMIZED;
    if (focused == w) {
        focused = NULL;
        for (window_t *x = wins; x; x = x->next)
            if (!(x->flags & WF_MINIMIZED) && !x->closing) focused = x;
    }
    dirty = true;
}

void wm_toggle_max(window_t *w) {
    if (w->flags & WF_MAXIMIZED) {
        w->flags &= ~WF_MAXIMIZED;
        w->x = w->rx, w->y = w->ry, w->w = w->rw, w->h = w->rh;
    } else {
        w->rx = w->x, w->ry = w->y, w->rw = w->w, w->rh = w->h;
        w->flags |= WF_MAXIMIZED;
        w->x = 0, w->y = 0, w->w = g_scr_w, w->h = wm_work_h();
    }
    dirty = true;
}

// ---------------- odroczone akcje ----------------
static struct { void (*fn)(void *); void *ctx; } deferred[32];
static int ndeferred;

void ui_defer(void (*fn)(void *), void *ctx) {
    if (ndeferred < 32) {
        deferred[ndeferred].fn = fn;
        deferred[ndeferred].ctx = ctx;
        ndeferred++;
    }
}

static void run_deferred(void) {
    while (ndeferred) {
        void (*fn)(void *) = deferred[0].fn;
        void *ctx = deferred[0].ctx;
        ndeferred--;
        memmove(&deferred[0], &deferred[1], ndeferred * sizeof deferred[0]);
        fn(ctx);
        dirty = true;
    }
}

// ---------------- geometria ----------------
static window_t *win_at(int x, int y) {
    window_t *hit = NULL;
    for (window_t *w = wins; w; w = w->next) {
        if ((w->flags & WF_MINIMIZED) || w->closing) continue;
        if (x >= w->x && y >= w->y && x < w->x + w->w && y < w->y + w->h) hit = w;
    }
    return hit;
}

static void client_rect(window_t *w, int *cx, int *cy, int *cw, int *ch) {
    *cx = w->x + 1;
    *cy = w->y + TITLE_H;
    *cw = w->w - 2;
    *ch = w->h - TITLE_H - 1;
}

enum { HIT_NONE, HIT_TITLE, HIT_CLOSE, HIT_MAX, HIT_MIN, HIT_RESIZE, HIT_CLIENT };

static int title_buttons(window_t *w) {
    if (w->flags & WF_MODAL) return 1;
    if (w->flags & WF_NOMAXIMIZE) return 2;
    return 3;
}

static int hit_test(window_t *w, int x, int y) {
    int lx = x - w->x, ly = y - w->y;
    if ((w->flags & WF_RESIZABLE) && !(w->flags & WF_MAXIMIZED) && lx >= w->w - 14 && ly >= w->h - 14) return HIT_RESIZE;
    if (ly < TITLE_H) {
        int bw = 44, nb = title_buttons(w);
        int bx = w->w - bw * nb;
        if (lx >= bx) {
            int i = (lx - bx) / bw;
            if (nb == 1) return HIT_CLOSE;
            if (nb == 2) return i == 0 ? HIT_MIN : HIT_CLOSE;
            return i == 0 ? HIT_MIN : i == 1 ? HIT_MAX : HIT_CLOSE;
        }
        return HIT_TITLE;
    }
    return HIT_CLIENT;
}

// ---------------- przebieg ramki okna ----------------
static surface_t null_surface;

static void run_frame(window_t *w, event_t *ev, bool render) {
    int cx, cy, cw, ch;
    client_rect(w, &cx, &cy, &cw, &ch);
    window_t *prev_win = ui.win;
    ui.win = w;
    ui.mx = g_mouse_x - cx;
    ui.my = g_mouse_y - cy;
    ui.cw = cw;
    ui.ch = ch;
    ui.render = render;
    ui.focused = w == focused;
    window_t *top = win_at(g_mouse_x, g_mouse_y);
    ui.hovered = (top == w && !menu_is_open() && (!capture_win || capture_win == w) && !drag_win && !resize_win
                  && g_mouse_y < g_scr_h - TASKBAR_H) || capture_win == w;
    if (ui.hovered && !(ui.mx >= 0 && ui.my >= 0 && ui.mx < cw && ui.my < ch) && capture_win != w) ui.hovered = false;
    if (wm_any_modal_for(w)) ui.hovered = false;
    surface_t *s;
    if (render) {
        s = &g_fb;
        gfx_reset(s);
        s->ox = cx;
        s->oy = cy;
        gfx_set_clip(s, 0, 0, cw, ch);
    } else {
        null_surface = g_fb;
        s = &null_surface;
        s->ox = cx, s->oy = cy;
        s->cx0 = s->cy0 = 0;
        s->cx1 = s->cy1 = 0;
    }
    ui.s = s;
    if (!render && !ev) { ui.win = prev_win; return; }
    w->frame(w, s);
    gfx_reset(&g_fb);
    ui.win = prev_win;
}

static void dispatch_mouse(window_t *w, bool pressed, bool released, bool rpressed, bool dclick, int wheel, u8 buttons) {
    ui.mpressed = pressed;
    ui.mreleased = released;
    ui.rpressed = rpressed;
    ui.dclick = dclick;
    ui.wheel = wheel;
    ui.mdown = buttons & 1;
    ui.key = NULL;
    focus_claimed = false;
    event_t dummy = { 0 };
    run_frame(w, &dummy, false);
    if (pressed && !focus_claimed && w == focused) { ui.focus = 0; tb_caret = -1; }
    if (released) ui.active = 0;
    ui.mpressed = ui.mreleased = ui.rpressed = ui.dclick = false;
    ui.wheel = 0;
}

// ---------------- obsluga zdarzen ----------------
static bool busy_active;
static bool busy_cancel;
static bool busy_handle_mouse(event_t *ev, bool pressed);
static bool menu_mouse(event_t *ev, bool pressed, bool rpressed);

void wm_handle(event_t *ev) {
    dirty = true;
    if (ev->type == EV_KEY) {
        if (busy_active) return;
        if (menu_is_open()) {
            if (ev->pressed && ev->key == KEY_ESC) menu_open(0, 0, NULL, 0, NULL, NULL);
            return;
        }
        if (!ev->pressed) return;
        if (ev->key == KEY_F4 && (ev->mods & MOD_ALT)) { if (focused) wm_close(focused); return; }
        if (focused) {
            ui.key = ev;
            ui.mpressed = ui.mreleased = ui.rpressed = ui.dclick = false;
            ui.wheel = 0;
            ui.mdown = g_mouse_buttons & 1;
            run_frame(focused, ev, false);
            ui.key = NULL;
        } else if (shell && shell->key) {
            shell->key(ev);
        }
        return;
    }

    u8 b = ev->buttons;
    g_last_mouse_mods = ev->mods;
    bool pressed = (b & 1) && !(mb_prev & 1);
    bool released = !(b & 1) && (mb_prev & 1);
    bool rpressed = (b & 2) && !(mb_prev & 2);
    u8 prevb = mb_prev;
    mb_prev = b;
    bool dclick = false;
    if (pressed) {
        u64 now = ticks_ms();
        int dx = ev->x - last_click_x, dy = ev->y - last_click_y;
        if (now - last_click_t < 550 && dx * dx + dy * dy < 36) { dclick = true; last_click_t = 0; }
        else last_click_t = now;
        last_click_x = ev->x, last_click_y = ev->y;
    }

    if (busy_active) { busy_handle_mouse(ev, pressed); return; }
    if (menu_is_open()) { menu_mouse(ev, pressed, rpressed); return; }
    if (shell && shell->overlay_mouse && shell->overlay_mouse(ev, prevb)) return;

    if (drag_win) {
        drag_win->x = ev->x - drag_dx;
        drag_win->y = CLAMP(ev->y - drag_dy, 0, wm_work_h() - TITLE_H);
        if (!(b & 1)) drag_win = NULL;
        return;
    }
    if (resize_win) {
        resize_win->w = MAX(resize_win->min_w, ev->x - resize_win->x + drag_dx);
        resize_win->h = MAX(resize_win->min_h, ev->y - resize_win->y + drag_dy);
        resize_win->w = MIN(resize_win->w, g_scr_w);
        resize_win->h = MIN(resize_win->h, wm_work_h());
        if (!(b & 1)) resize_win = NULL;
        return;
    }
    if (capture_win) {
        window_t *w = capture_win;
        if (released || !(b & 1)) capture_win = NULL;
        dispatch_mouse(w, false, released, false, false, ev->wheel, b);
        return;
    }

    if (ev->y >= g_scr_h - TASKBAR_H) {
        if (shell && shell->mouse) shell->mouse(ev, prevb);
        return;
    }
    window_t *w = win_at(ev->x, ev->y);
    if (!w) {
        if ((pressed || rpressed) && focused) { focused = NULL; }
        if (shell && shell->mouse) shell->mouse(ev, prevb);
        return;
    }
    if (pressed || rpressed) {
        window_t *m = modal_child(w);
        if (m) { wm_focus(m); return; }
        if (w != focused || w->next) wm_focus(w);
    }
    int h = hit_test(w, ev->x, ev->y);
    if (pressed) {
        switch (h) {
        case HIT_CLOSE: wm_close(w); return;
        case HIT_MAX: wm_toggle_max(w); return;
        case HIT_MIN: wm_minimize(w); return;
        case HIT_RESIZE:
            resize_win = w;
            drag_dx = w->x + w->w - ev->x;
            drag_dy = w->y + w->h - ev->y;
            return;
        case HIT_TITLE:
            if (dclick && !(w->flags & (WF_MODAL | WF_NOMAXIMIZE))) { wm_toggle_max(w); return; }
            if (w->flags & WF_MAXIMIZED) {
                // odklej od maksymalizacji
                int rel = (ev->x - w->x) * w->rw / MAX(1, w->w);
                wm_toggle_max(w);
                w->x = ev->x - rel;
                w->y = 0;
            }
            drag_win = w;
            drag_dx = ev->x - w->x;
            drag_dy = ev->y - w->y;
            return;
        }
    }
    if (h == HIT_CLIENT) {
        if (pressed) capture_win = w;
        dispatch_mouse(w, pressed, released, rpressed, dclick, ev->wheel, b);
    }
}

// ---------------- rysowanie ----------------
static void draw_title_glyph(surface_t *s, int kind, int cx, int cy, u32 c) {
    if (kind == HIT_CLOSE) {
        for (int i = -5; i <= 5; i++) {
            gfx_fill(s, cx + i, cy + i, 2, 1, c);
            gfx_fill(s, cx + i, cy - i, 2, 1, c);
        }
    } else if (kind == HIT_MAX) {
        gfx_rect(s, cx - 5, cy - 5, 11, 11, c);
    } else {
        gfx_fill(s, cx - 5, cy, 11, 1, c);
    }
}

static void draw_window(window_t *w) {
    surface_t *s = &g_fb;
    gfx_reset(s);
    bool act = w == focused;
    bool maxd = w->flags & WF_MAXIMIZED;
    int r = maxd ? 0 : 10;
    if (!maxd) gfx_shadow(s, w->x, w->y, w->w, w->h, r, act ? 22 : 12, act ? 110 : 60);
    gfx_round_rect(s, w->x, w->y, w->w, w->h, r, g_theme.win_bg);
    // pasek tytulu
    u32 tb = act ? g_theme.title_bg : RGB(246, 248, 250);
    gfx_round_rect(s, w->x, w->y, w->w, TITLE_H, r, tb);
    gfx_fill(s, w->x, w->y + TITLE_H - r, w->w, r, tb);
    gfx_fill(s, w->x, w->y + TITLE_H - 1, w->w, 1, RGB(222, 228, 234));
    if (act) gfx_fill(s, w->x + r, w->y, w->w - 2 * r, 2, g_theme.accent);
    if (w->icon >= 0) gfx_blit(s, w->x + 12, w->y + 10, icon_get(w->icon, 16));
    int nb = title_buttons(w), bw = 44;
    int tx = w->x + (w->icon >= 0 ? 36 : 14);
    gfx_set_clip(s, tx, w->y, w->w - (tx - w->x) - nb * bw - 6, TITLE_H);
    gfx_text_ellipsis(s, tx, w->y + 10, w->w - (tx - w->x) - nb * bw - 8, FONT_BOLD, w->title, act ? g_theme.text : g_theme.text_dim);
    gfx_reset(s);
    int kinds3[3] = { HIT_MIN, HIT_MAX, HIT_CLOSE }, kinds2[2] = { HIT_MIN, HIT_CLOSE }, kinds1[1] = { HIT_CLOSE };
    int *kinds = nb == 3 ? kinds3 : nb == 2 ? kinds2 : kinds1;
    bool over = win_at(g_mouse_x, g_mouse_y) == w && !drag_win && !capture_win && !menu_is_open();
    for (int i = 0; i < nb; i++) {
        int bx = w->x + w->w - bw * (nb - i);
        bool hov = over && g_mouse_x >= bx && g_mouse_x < bx + bw && g_mouse_y >= w->y && g_mouse_y < w->y + TITLE_H;
        u32 gc = act ? g_theme.text : g_theme.text_dim;
        if (hov) {
            u32 hc = kinds[i] == HIT_CLOSE ? RGB(230, 70, 60) : RGB(222, 228, 234);
            if (kinds[i] == HIT_CLOSE && i == nb - 1 && !maxd) {
                gfx_set_clip(s, bx, w->y, bw, TITLE_H - 1);
                gfx_round_rect(s, bx, w->y, bw + r, TITLE_H + r, r, hc);
                gfx_reset(s);
            } else gfx_fill(s, bx, w->y, bw, TITLE_H - 1, hc);
            if (kinds[i] == HIT_CLOSE) gc = RGB(255, 255, 255);
        }
        draw_title_glyph(s, kinds[i], bx + bw / 2, w->y + TITLE_H / 2, gc);
    }
    gfx_round_outline(s, w->x, w->y, w->w, w->h, r, act ? RGBA(0, 0, 0, 70) : RGBA(0, 0, 0, 45));
    // obszar klienta
    ui.mpressed = ui.mreleased = ui.rpressed = ui.dclick = false;
    ui.wheel = 0;
    ui.key = NULL;
    ui.mdown = g_mouse_buttons & 1;
    run_frame(w, NULL, true);
    if ((w->flags & WF_RESIZABLE) && !maxd) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j <= i; j++) gfx_fill(s, w->x + w->w - 6 - (i - j) * 4, w->y + w->h - 6 - j * 4, 2, 2, RGB(170, 178, 188));
    }
}

static void draw_cursor(surface_t *s, int x, int y) {
    static const char *arrow[] = {
        "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
        "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX",
        "X...X..X    ", "X..XX..X    ", "X.X  X..X   ", "XX   X..X   ", "X     X..X  ", "      X..X  ",
        "       XX   ",
    };
    gfx_reset(s);
    for (int j = 0; j < 19; j++)
        for (int i = 0; i < 12; i++) {
            char c = arrow[j][i];
            if (c == 'X') gfx_pixel(s, x + i, y + j, RGB(20, 24, 30));
            else if (c == '.') gfx_pixel(s, x + i, y + j, RGB(255, 255, 255));
        }
}

static void menu_draw(surface_t *s);
static void busy_draw(surface_t *s);

void wm_render(void) {
    surface_t *s = &g_fb;
    gfx_reset(s);
    if (shell && shell->draw) shell->draw(s);
    for (window_t *w = wins; w; w = w->next)
        if (!(w->flags & WF_MINIMIZED) && !w->closing) draw_window(w);
    gfx_reset(s);
    if (shell && shell->overlay) shell->overlay(s);
    gfx_reset(s);
    menu_draw(s);
    busy_draw(s);
    draw_cursor(s, g_mouse_x, g_mouse_y);
    gfx_present_full();
    dirty = false;
    last_render = ticks_ms();
}

static void step(void) {
    mouse_poll_absolute();
    net_poll();
    event_t ev;
    int n = 0;
    while (n < 64 && input_poll(&ev)) {
        // scal ruchy myszy bez zmiany przyciskow
        if (ev.type == EV_MOUSE && !ev.wheel && ev.buttons == mb_prev && !capture_win && !drag_win && !resize_win) {
            event_t nx;
            while (input_poll(&nx)) {
                if (nx.type == EV_MOUSE && !nx.wheel && nx.buttons == ev.buttons) { ev = nx; continue; }
                wm_handle(&ev);
                ev = nx;
                break;
            }
        }
        wm_handle(&ev);
        reap();
        run_deferred();
        reap();
        n++;
    }
    for (window_t *w = wins; w; w = w->next)
        if (w->tick && !w->closing && w->tick(w)) dirty = true;
    if (shell && shell->tick) shell->tick();
    reap();
    run_deferred();
    reap();
}

void wm_loop(void) {
    for (;;) {
        step();
        if (dirty && ticks_ms() - last_render >= 15) wm_render();
        hlt();
    }
}

// ---------------- kontrolki ----------------
bool ui_hit(int x, int y, int w, int h) {
    return ui.hovered && ui.mx >= x && ui.my >= y && ui.mx < x + w && ui.my < y + h;
}

bool ui_clicked(int x, int y, int w, int h) { return ui.mpressed && ui_hit(x, y, w, h); }

bool ui_key(u16 key) {
    if (ui.key && ui.key->key == key) { ui.key = NULL; return true; }
    return false;
}

bool ui_ctrl_key(char c) {
    if (ui.key && ui.key->key == KEY_CHAR && (ui.key->mods & MOD_CTRL) && !(ui.key->mods & (MOD_ALT | MOD_ALTGR))) {
        u32 ch = ui.key->ch;
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        if (ch == (u32)c) { ui.key = NULL; return true; }
    }
    return false;
}

void ui_focus_clear(void) { ui.focus = 0; tb_caret = -1; }

void ui_label(int x, int y, int font, const char *t, u32 c) { gfx_text(ui.s, x, y, font, t, c); }

bool ui_button_id(u64 id, int x, int y, int w, int h, const char *label, int icon, int style) {
    surface_t *s = ui.s;
    bool hov = ui_hit(x, y, w, h);
    if (ui.mpressed && hov) ui.active = id;
    bool down = ui.active == id && ui.mdown && hov;
    bool clicked = ui.mreleased && ui.active == id && hov;
    u32 bg, fg, br;
    switch (style) {
    case BTN_PRIMARY:
        bg = g_theme.accent; fg = RGB(255, 255, 255); br = g_theme.accent2;
        if (hov) bg = color_lighten(bg, 25);
        if (down) bg = g_theme.accent2;
        break;
    case BTN_DANGER:
        bg = RGB(225, 70, 60); fg = RGB(255, 255, 255); br = RGB(190, 50, 40);
        if (hov) bg = color_lighten(bg, 25);
        if (down) bg = br;
        break;
    case BTN_FLAT:
        bg = hov ? (down ? g_theme.sel : g_theme.hover) : 0; fg = g_theme.text; br = 0;
        break;
    case BTN_LIGHT:
        bg = hov ? RGBA(255, 255, 255, down ? 90 : 60) : RGBA(255, 255, 255, 30); fg = RGB(255, 255, 255); br = 0;
        break;
    default:
        bg = hov ? RGB(236, 241, 246) : RGB(255, 255, 255); fg = g_theme.text; br = RGB(196, 204, 212);
        if (down) bg = RGB(222, 228, 236);
    }
    if (C_A(bg)) gfx_round_rect(s, x, y, w, h, 7, bg);
    if (br) gfx_round_outline(s, x, y, w, h, 7, br);
    int tw = label ? gfx_text_width(FONT_UI, label) : 0;
    int iw = icon >= 0 ? (h >= 34 ? 24 : 16) : 0;
    int total = tw + iw + (iw && tw ? 8 : 0);
    int cx = x + (w - total) / 2;
    if (icon >= 0) {
        gfx_blit(s, cx, y + (h - iw) / 2, icon_get(icon, iw));
        cx += iw + 8;
    }
    if (label) {
        int sx0 = s->cx0, sy0 = s->cy0, sx1 = s->cx1, sy1 = s->cy1;
        gfx_set_clip(s, x, y, w, h);
        gfx_text(s, cx, y + (h - gfx_font_height(FONT_UI)) / 2, FONT_UI, label, fg);
        s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
    }
    return clicked;
}

bool ui_checkbox_id(u64 id, int x, int y, const char *label, bool *v) {
    surface_t *s = ui.s;
    int w = 22 + gfx_text_width(FONT_UI, label);
    bool hov = ui_hit(x, y, w, 20);
    if (ui.mpressed && hov) ui.active = id;
    bool clicked = ui.mreleased && ui.active == id && hov;
    if (clicked) *v = !*v;
    if (*v) {
        gfx_round_rect(s, x, y + 1, 18, 18, 5, g_theme.accent);
        gfx_line(s, x + 4, y + 10, x + 7, y + 13, RGB(255, 255, 255));
        gfx_line(s, x + 5, y + 10, x + 8, y + 13, RGB(255, 255, 255));
        gfx_line(s, x + 7, y + 13, x + 13, y + 6, RGB(255, 255, 255));
        gfx_line(s, x + 8, y + 13, x + 14, y + 6, RGB(255, 255, 255));
    } else {
        gfx_round_rect(s, x, y + 1, 18, 18, 5, hov ? g_theme.hover : RGB(255, 255, 255));
        gfx_round_outline(s, x, y + 1, 18, 18, 5, RGB(160, 170, 180));
    }
    gfx_text(s, x + 26, y + 2, FONT_UI, label, g_theme.text);
    return clicked;
}

bool ui_textbox_id(u64 id, int x, int y, int w, char *buf, int cap, int flags, const char *ph) {
    surface_t *s = ui.s;
    int h = 32;
    bool hov = ui_hit(x, y, w, h);
    bool entered = false;
    if ((flags & TB_AUTOFOCUS) && !ui.focus && ui.focused) ui.focus = id;
    if (ui.mpressed && hov) {
        ui.focus = id;
        focus_claimed = true;
        // ustaw karetke wg klikniecia
        int px = ui.mx - x - 10 + tb_scroll;
        int pos = 0, len = strlen(buf);
        while (pos < len) {
            int nx = utf8_next(buf, pos);
            int cw = (flags & TB_PASSWORD) ? 9 : gfx_text_width_n(FONT_UI, buf + pos, nx - pos);
            if (px < cw / 2) break;
            px -= cw;
            pos = nx;
        }
        tb_caret = pos;
        tb_caret_for = id;
    }
    bool foc = ui.focus == id && ui.focused;
    int len = strlen(buf);
    if (foc && (tb_caret < 0 || tb_caret_for != id || tb_caret > len)) { tb_caret = len; tb_caret_for = id; tb_scroll = 0; }
    if (foc && ui.key) {
        event_t *k = ui.key;
        bool used = true;
        if (k->key == KEY_CHAR && (!(k->mods & MOD_CTRL) || (k->mods & (MOD_ALT | MOD_ALTGR)))) {
            char enc[4];
            int n = utf8_encode(k->ch, enc);
            if (len + n < cap) {
                memmove(buf + tb_caret + n, buf + tb_caret, len - tb_caret + 1);
                memcpy(buf + tb_caret, enc, n);
                tb_caret += n;
            }
        } else if (k->key == KEY_BACKSPACE) {
            if (tb_caret > 0) {
                int p = utf8_prev(buf, tb_caret);
                memmove(buf + p, buf + tb_caret, len - tb_caret + 1);
                tb_caret = p;
            }
        } else if (k->key == KEY_DEL) {
            if (tb_caret < len) {
                int nx = utf8_next(buf, tb_caret);
                memmove(buf + tb_caret, buf + nx, len - nx + 1);
            }
        } else if (k->key == KEY_LEFT) tb_caret = utf8_prev(buf, tb_caret);
        else if (k->key == KEY_RIGHT) tb_caret = utf8_next(buf, tb_caret);
        else if (k->key == KEY_HOME) tb_caret = 0;
        else if (k->key == KEY_END) tb_caret = len;
        else if (k->key == KEY_ENTER) entered = true;
        else if (k->key == KEY_CHAR && (k->mods & MOD_CTRL) && (k->ch == 'a' || k->ch == 'A')) { buf[0] = 0; tb_caret = 0; }
        else used = false;
        if (used) ui.key = NULL;
    }
    if (!(flags & TB_NOFRAME)) {
        gfx_round_rect(s, x, y, w, h, 7, RGB(255, 255, 255));
        gfx_round_outline(s, x, y, w, h, 7, foc ? g_theme.accent : (hov ? RGB(150, 160, 172) : RGB(196, 204, 212)));
        if (foc) gfx_round_outline(s, x - 1, y - 1, w + 2, h + 2, 8, WITH_ALPHA(g_theme.accent, 70));
    }
    // tekst do wyswietlenia
    char shown[512];
    if (flags & TB_PASSWORD) {
        int n = 0;
        for (int p = 0; buf[p] && n < 120; p = utf8_next(buf, p)) { shown[n++] = '*'; }
        shown[n] = 0;
    } else strlcpy(shown, buf, sizeof shown);
    int caret_px = 0;
    if (foc) {
        if (flags & TB_PASSWORD) {
            int n = 0;
            for (int p = 0; p < tb_caret; p = utf8_next(buf, p)) n++;
            caret_px = gfx_text_width_n(FONT_UI, shown, n);
        } else caret_px = gfx_text_width_n(FONT_UI, buf, tb_caret);
        if (caret_px - tb_scroll > w - 24) tb_scroll = caret_px - (w - 24);
        if (caret_px - tb_scroll < 0) tb_scroll = caret_px;
    }
    int scroll = foc ? tb_scroll : 0;
    int ty = y + (h - gfx_font_height(FONT_UI)) / 2;
    int sx0 = s->cx0, sy0 = s->cy0, sx1 = s->cx1, sy1 = s->cy1;
    gfx_set_clip(s, x + 8, y, w - 16, h);
    if (!buf[0] && ph && !foc) gfx_text(s, x + 10, ty, FONT_UI, ph, RGB(150, 158, 168));
    else gfx_text(s, x + 10 - scroll, ty, FONT_UI, shown, g_theme.text);
    if (foc && (ticks_ms() / 530) % 2 == 0) gfx_fill(s, x + 10 + caret_px - scroll, y + 7, 1, h - 14, g_theme.text);
    s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
    return entered;
}

void ui_scrollbar_id(u64 id, int x, int y, int h, int *scroll, int content, int view) {
    surface_t *s = ui.s;
    int maxs = content - view;
    if (maxs <= 0) { *scroll = 0; return; }
    *scroll = CLAMP(*scroll, 0, maxs);
    int th = MAX(30, h * view / content);
    int ty = y + (h - th) * *scroll / maxs;
    bool hov = ui_hit(x, y, 12, h);
    static int grab;
    if (ui.mpressed && hov) {
        ui.active = id;
        if (ui.my >= ty && ui.my < ty + th) grab = ui.my - ty;
        else grab = th / 2;
    }
    if (ui.active == id && ui.mdown) {
        int ny = ui.my - grab - y;
        *scroll = CLAMP(ny * maxs / MAX(1, h - th), 0, maxs);
        ty = y + (h - th) * *scroll / maxs;
    }
    gfx_round_rect(s, x + 2, y, 8, h, 4, RGBA(0, 0, 0, 18));
    gfx_round_rect(s, x + 2, ty, 8, th, 4, (hov || ui.active == id) ? RGBA(0, 0, 0, 120) : RGBA(0, 0, 0, 70));
}

void ui_progress(int x, int y, int w, int h, int pm) {
    surface_t *s = ui.s;
    pm = CLAMP(pm, 0, 1000);
    gfx_round_rect(s, x, y, w, h, h / 2, RGB(222, 228, 234));
    int fw = w * pm / 1000;
    if (fw > 0) gfx_round_rect_grad(s, x, y, MAX(fw, h), h, h / 2, color_lighten(g_theme.accent, 30), g_theme.accent);
}

// ---------------- dialogi ----------------
typedef struct {
    char text[600];
    int icon, kind;              // 0 info, 1 tak/nie, 2 wpisz
    char yes[32];
    char value[256];
    void (*cb_yes)(void *, bool);
    void (*cb_in)(void *, const char *);
    void *ctx;
    bool answered;
} dlg_t;

typedef struct { dlg_t *d; bool yes; char value[256]; } dlg_result_t;

static void dlg_fire(void *p) {
    dlg_result_t *r = p;
    if (r->d->kind == 1 && r->d->cb_yes) r->d->cb_yes(r->d->ctx, r->yes);
    if (r->d->kind == 2 && r->d->cb_in && r->yes) r->d->cb_in(r->d->ctx, r->value);
    kfree(r->d);
    kfree(r);
}

static void dlg_answer(window_t *w, bool yes) {
    dlg_t *d = w->data;
    if (d->answered) return;
    d->answered = true;
    dlg_result_t *r = kzalloc(sizeof *r);
    r->d = d;
    r->yes = yes;
    strlcpy(r->value, d->value, sizeof r->value);
    w->data = NULL;
    wm_close(w);
    ui_defer(dlg_fire, r);
}

static void dlg_on_close(window_t *w) {
    if (w->data) {
        dlg_t *d = w->data;
        if (!d->answered && d->kind) {
            d->answered = true;
            dlg_result_t *r = kzalloc(sizeof *r);
            r->d = d;
            r->yes = false;
            ui_defer(dlg_fire, r);
        } else kfree(d);
    }
}

static void dlg_frame(window_t *w, surface_t *s) {
    dlg_t *d = w->data;
    if (!d) return;
    gfx_fill(s, 0, 0, ui.cw, ui.ch, g_theme.win_bg);
    int tx = 20;
    if (d->icon >= 0) { gfx_blit(s, 20, 18, icon_get(d->icon, 48)); tx = 84; }
    int th = gfx_text_wrap(s, tx, 22, ui.cw - tx - 20, FONT_UI, d->text, g_theme.text, 20);
    int by = ui.ch - 52;
    gfx_fill(s, 0, by - 10, ui.cw, ui.ch - by + 10, g_theme.panel);
    if (d->kind == 2) {
        if (ui_textbox(tx, 22 + th + 8, ui.cw - tx - 20, d->value, sizeof d->value, TB_AUTOFOCUS, NULL)) { dlg_answer(w, true); return; }
    }
    if (d->kind == 0) {
        if (ui_button(ui.cw - 110, by, 92, 34, "OK", BTN_PRIMARY) || ui_key(KEY_ENTER) || ui_key(KEY_ESC)) dlg_answer(w, true);
    } else {
        if (ui_button(ui.cw - 220, by, 100, 34, d->yes, BTN_PRIMARY) || (d->kind == 1 && ui_key(KEY_ENTER))) { dlg_answer(w, true); return; }
        if (ui_button(ui.cw - 110, by, 92, 34, "Anuluj", BTN_NORMAL) || ui_key(KEY_ESC)) dlg_answer(w, false);
    }
}

static window_t *dlg_make(window_t *owner, const char *title, const char *text, int icon, int kind) {
    dlg_t *d = kzalloc(sizeof *d);
    strlcpy(d->text, text, sizeof d->text);
    d->icon = icon;
    d->kind = kind;
    strlcpy(d->yes, "OK", sizeof d->yes);
    int tw = 420 - (icon >= 0 ? 84 : 20) - 20;
    int th = gfx_text_wrap(NULL, 0, 0, tw, FONT_UI, text, 0, 20);
    int h = MAX(icon >= 0 ? 90 : 60, th + 40) + 64 + (kind == 2 ? 44 : 0) + TITLE_H;
    window_t *w = wm_create("dialog", title, -1, 440, h, WF_MODAL | WF_NOTASKBAR, dlg_frame, d);
    w->owner = owner;
    w->on_close = dlg_on_close;
    return w;
}

void dlg_message(window_t *owner, const char *title, const char *text, int icon) { dlg_make(owner, title, text, icon, 0); }

void dlg_confirm(window_t *owner, const char *title, const char *text, const char *yes, void (*cb)(void *, bool), void *ctx) {
    window_t *w = dlg_make(owner, title, text, ICON_WARNING, 1);
    dlg_t *d = w->data;
    strlcpy(d->yes, yes, sizeof d->yes);
    d->cb_yes = cb;
    d->ctx = ctx;
}

void dlg_input(window_t *owner, const char *title, const char *text, const char *initial, void (*cb)(void *, const char *), void *ctx) {
    window_t *w = dlg_make(owner, title, text, -1, 2);
    dlg_t *d = w->data;
    strlcpy(d->value, initial ? initial : "", sizeof d->value);
    strlcpy(d->yes, "OK", sizeof d->yes);
    d->cb_in = cb;
    d->ctx = ctx;
}

// ---------------- menu kontekstowe ----------------
static struct {
    bool open;
    int x, y, w, h;
    menu_item_t items[24];
    int n;
    void (*cb)(void *, int);
    void *ctx;
} menu;

typedef struct { void (*cb)(void *, int); void *ctx; int id; } menu_fire_t;
static void menu_fire(void *p) {
    menu_fire_t *f = p;
    f->cb(f->ctx, f->id);
    kfree(f);
}

bool menu_is_open(void) { return menu.open; }

void menu_open(int x, int y, const menu_item_t *items, int n, void (*cb)(void *, int), void *ctx) {
    if (!items || !n) { menu.open = false; dirty = true; return; }
    menu.n = MIN(n, 24);
    memcpy(menu.items, items, menu.n * sizeof(menu_item_t));
    menu.cb = cb;
    menu.ctx = ctx;
    int w = 160;
    for (int i = 0; i < menu.n; i++)
        if (menu.items[i].label) w = MAX(w, gfx_text_width(FONT_UI, menu.items[i].label) + 64);
    int h = 8;
    for (int i = 0; i < menu.n; i++) h += menu.items[i].label ? 30 : 9;
    if (x + w > g_scr_w) x = g_scr_w - w - 2;
    if (y + h > g_scr_h - TASKBAR_H) y = MAX(0, y - h);
    menu.x = x, menu.y = y, menu.w = w, menu.h = h;
    menu.open = true;
    dirty = true;
}

static int menu_item_at(int mx, int my) {
    if (mx < menu.x || mx >= menu.x + menu.w) return -1;
    int y = menu.y + 4;
    for (int i = 0; i < menu.n; i++) {
        int h = menu.items[i].label ? 30 : 9;
        if (my >= y && my < y + h) return menu.items[i].label && !menu.items[i].disabled ? i : -1;
        y += h;
    }
    return -1;
}

static bool menu_mouse(event_t *ev, bool pressed, bool rpressed) {
    if (!pressed && !rpressed) return true;
    int i = menu_item_at(ev->x, ev->y);
    bool inside = ev->x >= menu.x && ev->x < menu.x + menu.w && ev->y >= menu.y && ev->y < menu.y + menu.h;
    if (inside && i < 0) return true;
    menu.open = false;
    if (i >= 0 && menu.cb) {
        menu_fire_t *f = kmalloc(sizeof *f);
        f->cb = menu.cb, f->ctx = menu.ctx, f->id = menu.items[i].id;
        ui_defer(menu_fire, f);
    }
    return true;
}

static void menu_draw(surface_t *s) {
    if (!menu.open) return;
    gfx_shadow(s, menu.x, menu.y, menu.w, menu.h, 8, 14, 90);
    gfx_round_rect(s, menu.x, menu.y, menu.w, menu.h, 8, RGB(252, 253, 254));
    gfx_round_outline(s, menu.x, menu.y, menu.w, menu.h, 8, RGBA(0, 0, 0, 50));
    int y = menu.y + 4;
    int hov = menu_item_at(g_mouse_x, g_mouse_y);
    for (int i = 0; i < menu.n; i++) {
        menu_item_t *it = &menu.items[i];
        if (!it->label) {
            gfx_fill(s, menu.x + 10, y + 4, menu.w - 20, 1, RGB(225, 230, 236));
            y += 9;
            continue;
        }
        if (i == hov) gfx_round_rect(s, menu.x + 4, y, menu.w - 8, 30, 6, g_theme.hover);
        if (it->icon >= 0) gfx_blit_alpha(s, menu.x + 12, y + 7, icon_get(it->icon, 16), it->disabled ? 110 : 255);
        gfx_text(s, menu.x + 38, y + 7, FONT_UI, it->label, it->disabled ? RGB(170, 176, 184) : g_theme.text);
        y += 30;
    }
}

// ---------------- operacje dlugie ----------------
static char busy_title[96], busy_text[160];
static u64 busy_cur, busy_total, busy_last;

static void busy_rect(int *x, int *y, int *w, int *h) {
    *w = 460, *h = 170;
    *x = (g_scr_w - *w) / 2;
    *y = (g_scr_h - *h) / 2;
}

void busy_begin(const char *title) {
    strlcpy(busy_title, title, sizeof busy_title);
    busy_text[0] = 0;
    busy_cur = busy_total = 0;
    busy_active = true;
    busy_cancel = false;
    busy_last = 0;
    wm_render();
}

void busy_update(const char *text, u64 cur, u64 total) {
    if (text) strlcpy(busy_text, text, sizeof busy_text);
    busy_cur = cur;
    busy_total = total;
    sys_idle();
}

bool busy_cancelled(void) { return busy_cancel; }

void busy_end(void) {
    busy_active = false;
    dirty = true;
    // wyrzuc klikniecia z czasu operacji
    event_t ev;
    while (input_poll(&ev)) {
        if (ev.type == EV_MOUSE) { g_mouse_x = ev.x; g_mouse_y = ev.y; mb_prev = ev.buttons; }
    }
}

static bool busy_handle_mouse(event_t *ev, bool pressed) {
    int x, y, w, h;
    busy_rect(&x, &y, &w, &h);
    int bx = x + w - 120, by = y + h - 50;
    if (pressed && ev->x >= bx && ev->x < bx + 100 && ev->y >= by && ev->y < by + 34) busy_cancel = true;
    return true;
}

static void busy_draw(surface_t *s) {
    if (!busy_active) return;
    gfx_fill(s, 0, 0, g_scr_w, g_scr_h, RGBA(10, 20, 30, 90));
    int x, y, w, h;
    busy_rect(&x, &y, &w, &h);
    gfx_shadow(s, x, y, w, h, 12, 24, 120);
    gfx_round_rect(s, x, y, w, h, 12, g_theme.win_bg);
    gfx_text(s, x + 24, y + 20, FONT_TITLE, busy_title, g_theme.text);
    gfx_text_ellipsis(s, x + 24, y + 54, w - 48, FONT_UI, busy_text, g_theme.text_dim);
    int pm = busy_total ? (int)(busy_cur * 1000 / busy_total) : (int)((ticks_ms() / 3) % 1000);
    ui_t save = ui;
    ui.s = s;
    ui.win = NULL;
    if (busy_total) ui_progress(x + 24, y + 82, w - 48, 12, pm);
    else {
        gfx_round_rect(s, x + 24, y + 82, w - 48, 12, 6, RGB(222, 228, 234));
        int seg = (w - 48) / 4, pos = (w - 48 + seg) * pm / 1000 - seg;
        gfx_set_clip(s, x + 24, y + 82, w - 48, 12);
        gfx_round_rect(s, x + 24 + pos, y + 82, seg, 12, 6, g_theme.accent);
        gfx_reset(s);
    }
    ui = save;
    if (busy_total) {
        char a[32], b[32], t[80];
        fmt_size(a, sizeof a, busy_cur);
        fmt_size(b, sizeof b, busy_total);
        snprintf(t, sizeof t, "%s z %s", a, b);
        gfx_text(s, x + 24, y + 104, FONT_SMALL, t, g_theme.text_dim);
    }
    int bx = x + w - 120, by = y + h - 50;
    bool hov = g_mouse_x >= bx && g_mouse_x < bx + 100 && g_mouse_y >= by && g_mouse_y < by + 34;
    gfx_round_rect(s, bx, by, 100, 34, 7, hov ? RGB(236, 241, 246) : RGB(255, 255, 255));
    gfx_round_outline(s, bx, by, 100, 34, 7, RGB(196, 204, 212));
    gfx_text_center(s, bx, by + 9, 100, FONT_UI, busy_cancel ? "Anulowanie..." : "Anuluj", g_theme.text);
}

void sys_idle(void) {
    if (!busy_active) return;
    mouse_poll_absolute();
    event_t ev;
    while (input_poll(&ev)) wm_handle(&ev);
    u64 now = ticks_ms();
    if (now - busy_last >= 50) {
        busy_last = now;
        wm_render();
    }
}

bool ui_textbox_focused(u64 id) { return ui.focus == id && ui.focused; }

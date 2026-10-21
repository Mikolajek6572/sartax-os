// Sartax OS - menedzer okien i kontrolki (tryb natychmiastowy)
#pragma once
#include "gfx.h"

#define TITLE_H 36
#define TASKBAR_H 46

enum {
    WF_RESIZABLE = 1, WF_MAXIMIZED = 2, WF_MINIMIZED = 4, WF_MODAL = 8, WF_NOTASKBAR = 16,
    WF_NOMAXIMIZE = 32, WF_CENTER = 64,
};

typedef struct window window_t;
struct window {
    int x, y, w, h;
    int min_w, min_h;
    int rx, ry, rw, rh;            // pozycja przed maksymalizacja
    char title[96];
    int icon;
    u32 flags;
    void (*frame)(window_t *w, surface_t *s);
    bool (*close_req)(window_t *w); // false = nie zamykaj teraz
    void (*on_close)(window_t *w);
    bool (*tick)(window_t *w);      // co petle; true = przerysuj
    void *data;
    window_t *owner;                // okno dialogowe -> wlasciciel
    bool closing;
    char app[16];
    window_t *next;                 // kolejnosc rysowania (od dolu)
};

typedef struct {
    int mx, my;             // mysz wzgledem obszaru klienta
    bool mdown, mpressed, mreleased, rpressed, dclick;
    int wheel;
    event_t *key;           // biezacy klawisz (NULL po obsluzeniu)
    bool focused, hovered;
    bool render;            // przebieg rysowania (bez zdarzen)
    int cw, ch;             // rozmiar obszaru klienta
    window_t *win;
    u64 hot, active, focus;
    surface_t *s;
} ui_t;
extern ui_t ui;

// motyw
typedef struct {
    u32 accent, accent2;
    u32 win_bg, panel, border, text, text_dim, title_bg, sel, hover;
    int wallpaper;
} theme_t;
extern theme_t g_theme;
void theme_apply(int accent_idx, int wallpaper_idx);
extern const char *g_accent_names[];
extern const u32 g_accent_colors[];
extern const char *g_wallpaper_names[];
int accent_count(void);
int wallpaper_count(void);
void wallpaper_render(u32 *px, int w, int h, int idx);

// okna
void wm_init(void);
window_t *wm_create(const char *app, const char *title, int icon, int w, int h, u32 flags,
                    void (*frame)(window_t *, surface_t *), void *data);
void wm_close(window_t *w);
void wm_focus(window_t *w);
window_t *wm_focused(void);
window_t *wm_find_app(const char *app);
window_t *wm_first(void);
void wm_invalidate(void);
void wm_handle(event_t *ev);
void wm_render(void);
void wm_loop(void) __attribute__((noreturn));
void wm_set_title(window_t *w, const char *t);
void wm_minimize(window_t *w);
void wm_toggle_max(window_t *w);
void ui_defer(void (*fn)(void *), void *ctx);
bool wm_any_modal_for(window_t *w);

// pulpit (rysowany pod oknami)
typedef struct {
    void (*draw)(surface_t *s);
    bool (*mouse)(event_t *ev, int buttons_prev);  // true = obsluzone
    void (*key)(event_t *ev);
    void (*overlay)(surface_t *s);                  // menu start itp.
    bool (*overlay_mouse)(event_t *ev, int buttons_prev);
    void (*tick)(void);
} shell_t;
void wm_set_shell(shell_t *sh);
int wm_work_h(void);             // wysokosc obszaru bez paska zadan

// kontrolki
#define UI_ID(n) ((u64)(uintptr_t)ui.win * 2654435761ull + (u64)__LINE__ * 7919ull + (u64)(n))
bool ui_hit(int x, int y, int w, int h);
bool ui_clicked(int x, int y, int w, int h);
enum { BTN_NORMAL, BTN_PRIMARY, BTN_FLAT, BTN_DANGER, BTN_LIGHT };
bool ui_button_id(u64 id, int x, int y, int w, int h, const char *label, int icon, int style);
#define ui_button(x, y, w, h, label, style) ui_button_id(UI_ID(0), x, y, w, h, label, -1, style)
#define ui_button_icon(x, y, w, h, icon, label, style) ui_button_id(UI_ID(0), x, y, w, h, label, icon, style)
enum { TB_PASSWORD = 1, TB_AUTOFOCUS = 2 };
bool ui_textbox_id(u64 id, int x, int y, int w, char *buf, int cap, int flags, const char *placeholder);
#define ui_textbox(x, y, w, buf, cap, flags, ph) ui_textbox_id(UI_ID(0), x, y, w, buf, cap, flags, ph)
bool ui_checkbox_id(u64 id, int x, int y, const char *label, bool *v);
#define ui_checkbox(x, y, label, v) ui_checkbox_id(UI_ID(0), x, y, label, v)
void ui_scrollbar_id(u64 id, int x, int y, int h, int *scroll, int content, int view);
#define ui_scrollbar(x, y, h, scroll, content, view) ui_scrollbar_id(UI_ID(0), x, y, h, scroll, content, view)
void ui_progress(int x, int y, int w, int h, int permille);
void ui_label(int x, int y, int font, const char *t, u32 c);
void ui_focus_clear(void);
bool ui_key(u16 key);              // czy biezacy klawisz to 'key' (i zuzyj go)
bool ui_ctrl_key(char c);          // Ctrl+litera

// dialogi
void dlg_message(window_t *owner, const char *title, const char *text, int icon);
void dlg_confirm(window_t *owner, const char *title, const char *text, const char *yes,
                 void (*cb)(void *ctx, bool yes), void *ctx);
void dlg_input(window_t *owner, const char *title, const char *text, const char *initial,
               void (*cb)(void *ctx, const char *value), void *ctx);

// menu kontekstowe
typedef struct { const char *label; int icon; int id; bool disabled; } menu_item_t;
void menu_open(int x, int y, const menu_item_t *items, int n, void (*cb)(void *ctx, int id), void *ctx);
bool menu_is_open(void);

// zajety (pasek postepu podczas dlugich operacji)
void busy_begin(const char *title);
void busy_update(const char *text, u64 cur, u64 total);
bool busy_cancelled(void);
void busy_end(void);
void sys_idle(void);               // wolane w petlach oczekiwania (siec)

// czas lokalny
void local_time(datetime_t *dt);

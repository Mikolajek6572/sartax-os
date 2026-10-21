// Sartax OS SDK - naglowek dla programow .sap
// Program to plik ELF64 (PIE) z funkcja: int sap_main(const sartax_api_t *api)
#pragma once
#include <stdint.h>
#include <stddef.h>

#define SARTAX_API_VERSION 1

#define SX_RGB(r, g, b) (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define SX_RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

enum { SX_FONT_SMALL, SX_FONT_UI, SX_FONT_BOLD, SX_FONT_TITLE, SX_FONT_BIG, SX_FONT_MONO, SX_FONT_HUGE };
enum { SX_BTN_NORMAL, SX_BTN_PRIMARY, SX_BTN_FLAT, SX_BTN_DANGER };
enum {
    SX_KEY_ESC = 0x100, SX_KEY_ENTER, SX_KEY_BACKSPACE, SX_KEY_TAB, SX_KEY_LEFT, SX_KEY_RIGHT, SX_KEY_UP, SX_KEY_DOWN,
    SX_KEY_HOME, SX_KEY_END, SX_KEY_PGUP, SX_KEY_PGDN, SX_KEY_DEL, SX_KEY_INS, SX_KEY_CHAR = 0x121,
};

typedef struct {
    int mx, my;             // mysz wzgledem okna
    int mdown, mpressed, mreleased, rpressed, dclick, wheel;
    int key;                // SX_KEY_* (0 = brak); SX_KEY_CHAR -> znak w 'ch'
    uint32_t ch;
    int mods;               // 1 shift, 2 ctrl, 4 alt
    int width, height;      // rozmiar obszaru okna
    int focused, render;
} sx_input_t;

typedef struct sx_window sx_window_t;
typedef void (*sx_frame_fn)(sx_window_t *win, void *user);
typedef int (*sx_tick_fn)(sx_window_t *win, void *user);    // 1 = przerysuj

typedef struct {
    uint32_t version;
    // okna
    sx_window_t *(*window_create)(const char *title, int w, int h, sx_frame_fn frame, void *user);
    void (*window_close)(sx_window_t *win);
    void (*window_set_title)(sx_window_t *win, const char *title);
    void (*window_set_tick)(sx_window_t *win, sx_tick_fn tick);
    void (*redraw)(void);
    const sx_input_t *(*input)(void);
    void (*consume_key)(void);
    // rysowanie (w funkcji frame)
    void (*fill)(int x, int y, int w, int h, uint32_t color);
    void (*round_rect)(int x, int y, int w, int h, int r, uint32_t color);
    void (*circle)(int cx, int cy, int r, uint32_t color);
    void (*line)(int x0, int y0, int x1, int y1, uint32_t color);
    void (*gradient)(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
    int (*text)(int x, int y, int font, const char *s, uint32_t color);
    int (*text_width)(int font, const char *s);
    void (*text_center)(int x, int y, int w, int font, const char *s, uint32_t color);
    // kontrolki
    int (*button)(int id, int x, int y, int w, int h, const char *label, int style);
    int (*textbox)(int id, int x, int y, int w, char *buf, int cap);
    // system
    uint64_t (*ticks_ms)(void);
    uint32_t (*random)(void);
    void *(*malloc)(size_t n);
    void (*free)(void *p);
    int (*read_file)(const char *path, uint8_t **data, uint64_t *size);
    int (*write_file)(const char *path, const void *data, uint64_t size);
    void (*message)(const char *title, const char *text);
    int (*snprintf)(char *buf, size_t n, const char *fmt, ...);
    void (*memcpy)(void *d, const void *s, size_t n);
    void (*memset)(void *d, int c, size_t n);
    uint32_t accent;
} sartax_api_t;

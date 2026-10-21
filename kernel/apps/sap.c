// Sartax OS - ladowanie i uruchamianie programow .sap (ELF64 PIE)
#include "../apps.h"
#include "../../sdk/sartax.h"

struct sx_window {
    window_t *w;
    sx_frame_fn frame;
    sx_tick_fn tick;
    void *user;
};

static sx_input_t cur_in;

static void fill_input(void) {
    memset(&cur_in, 0, sizeof cur_in);
    cur_in.mx = ui.mx, cur_in.my = ui.my;
    cur_in.mdown = ui.mdown, cur_in.mpressed = ui.mpressed && ui.hovered, cur_in.mreleased = ui.mreleased;
    cur_in.rpressed = ui.rpressed && ui.hovered, cur_in.dclick = ui.dclick, cur_in.wheel = ui.wheel;
    if (ui.key) {
        cur_in.key = ui.key->key;
        cur_in.ch = ui.key->ch;
        cur_in.mods = ui.key->mods;
    }
    cur_in.width = ui.cw, cur_in.height = ui.ch;
    cur_in.focused = ui.focused;
    cur_in.render = ui.render;
}

static void sap_frame(window_t *w, surface_t *s) {
    struct sx_window *sw = w->data;
    fill_input();
    sw->frame(sw, sw->user);
}

static bool sap_tick(window_t *w) {
    struct sx_window *sw = w->data;
    return sw->tick ? sw->tick(sw, sw->user) != 0 : false;
}

static void sap_close(window_t *w) { kfree(w->data); }

static sx_window_t *api_window_create(const char *title, int ww, int hh, sx_frame_fn frame, void *user) {
    struct sx_window *sw = kzalloc(sizeof *sw);
    sw->frame = frame;
    sw->user = user;
    sw->w = wm_create("sap", title, ICON_APP, ww + 2, hh + TITLE_H + 1, WF_NOMAXIMIZE, sap_frame, sw);
    sw->w->on_close = sap_close;
    sw->w->tick = sap_tick;
    return sw;
}
static void api_window_close(sx_window_t *w) { wm_close(w->w); }
static void api_window_set_title(sx_window_t *w, const char *t) { wm_set_title(w->w, t); }
static void api_window_set_tick(sx_window_t *w, sx_tick_fn t) { w->tick = t; }
static const sx_input_t *api_input(void) { return &cur_in; }
static void api_consume_key(void) { ui.key = NULL; cur_in.key = 0; }
static void api_fill(int x, int y, int w, int h, u32 c) { gfx_fill(ui.s, x, y, w, h, c); }
static void api_round_rect(int x, int y, int w, int h, int r, u32 c) { gfx_round_rect(ui.s, x, y, w, h, r, c); }
static void api_circle(int x, int y, int r, u32 c) { gfx_circle(ui.s, x, y, r, c); }
static void api_line(int a, int b, int c, int d, u32 col) { gfx_line(ui.s, a, b, c, d, col); }
static void api_gradient(int x, int y, int w, int h, u32 a, u32 b) { gfx_gradient_v(ui.s, x, y, w, h, a, b); }
static int api_text(int x, int y, int f, const char *t, u32 c) { return gfx_text(ui.s, x, y, CLAMP(f, 0, FONT_COUNT - 1), t, c); }
static int api_text_width(int f, const char *t) { return gfx_text_width(CLAMP(f, 0, FONT_COUNT - 1), t); }
static void api_text_center(int x, int y, int w, int f, const char *t, u32 c) { gfx_text_center(ui.s, x, y, w, CLAMP(f, 0, FONT_COUNT - 1), t, c); }
static int api_button(int id, int x, int y, int w, int h, const char *l, int st) {
    return ui_button_id((u64)(uintptr_t)ui.win * 31 + id * 977 + 5, x, y, w, h, l, -1, st);
}
static int api_textbox(int id, int x, int y, int w, char *b, int cap) {
    return ui_textbox_id((u64)(uintptr_t)ui.win * 37 + id * 1193 + 11, x, y, w, b, cap, 0, NULL);
}
static void api_message(const char *t, const char *m) { dlg_message(wm_focused(), t, m, ICON_INFO); }
static void api_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
static void api_memset(void *d, int c, size_t n) { memset(d, c, n); }
static u64 api_ticks(void) { return ticks_ms(); }
static int api_read(const char *p, u8 **d, u64 *s) { return fs_read_file(p, d, s); }
static int api_write(const char *p, const void *d, u64 s) { int r = fs_write_file(p, d, s); explorer_refresh_all(); return r; }

static sartax_api_t api;

static void api_init(void) {
    api.version = SARTAX_API_VERSION;
    api.window_create = api_window_create;
    api.window_close = api_window_close;
    api.window_set_title = api_window_set_title;
    api.window_set_tick = api_window_set_tick;
    api.redraw = wm_invalidate;
    api.input = api_input;
    api.consume_key = api_consume_key;
    api.fill = api_fill;
    api.round_rect = api_round_rect;
    api.circle = api_circle;
    api.line = api_line;
    api.gradient = api_gradient;
    api.text = api_text;
    api.text_width = api_text_width;
    api.text_center = api_text_center;
    api.button = api_button;
    api.textbox = api_textbox;
    api.ticks_ms = api_ticks;
    api.random = rng_u32;
    api.malloc = kmalloc;
    api.free = kfree;
    api.read_file = api_read;
    api.write_file = api_write;
    api.message = api_message;
    api.snprintf = snprintf;
    api.memcpy = api_memcpy;
    api.memset = api_memset;
}

// ---------------- ELF ----------------
typedef struct PACKED {
    u8 ident[16];
    u16 type, machine;
    u32 version;
    u64 entry, phoff, shoff;
    u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} ehdr_t;
typedef struct PACKED { u32 type, flags; u64 offset, vaddr, paddr, filesz, memsz, align; } phdr_t;
typedef struct PACKED { i64 tag; u64 val; } dyn_t;
typedef struct PACKED { u64 off, info; i64 addend; } rela_t;
typedef struct PACKED { u32 name; u8 info, other; u16 shndx; u64 value, size; } sym_t;

static int load_elf(const u8 *d, u64 n, u64 *entry, char *err) {
    const ehdr_t *e = (const ehdr_t *)d;
    if (n < sizeof(ehdr_t) || memcmp(e->ident, "\x7f" "ELF", 4) || e->ident[4] != 2 || e->machine != 62) {
        strlcpy(err, "To nie jest program Sartax OS (zly format)", 128);
        return -1;
    }
    if (e->type != 3) { strlcpy(err, "Program musi byc skompilowany jako PIE", 128); return -1; }
    u64 lo = ~0ull, hi = 0;
    for (int i = 0; i < e->phnum; i++) {
        const phdr_t *p = (const phdr_t *)(d + e->phoff + i * e->phentsize);
        if ((u8 *)(p + 1) > d + n) return -1;
        if (p->type != 1) continue;
        lo = MIN(lo, p->vaddr & ~0xFFFull);
        hi = MAX(hi, p->vaddr + p->memsz);
    }
    if (lo >= hi || hi - lo > 64 * 1024 * 1024) { strlcpy(err, "Uszkodzony program", 128); return -1; }
    u8 *mem = kzalloc(hi - lo + 4096);
    u8 *base = (u8 *)(((u64)mem + 4095) & ~4095ull) - lo;
    const dyn_t *dyn = NULL;
    for (int i = 0; i < e->phnum; i++) {
        const phdr_t *p = (const phdr_t *)(d + e->phoff + i * e->phentsize);
        if (p->type == 1) {
            if (p->offset + p->filesz > n) { strlcpy(err, "Uszkodzony program", 128); return -1; }
            memcpy(base + p->vaddr, d + p->offset, p->filesz);
        } else if (p->type == 2) dyn = (const dyn_t *)(base + p->vaddr);
    }
    if (dyn) {
        u64 rela = 0, relasz = 0, jmprel = 0, pltsz = 0, symtab = 0;
        for (const dyn_t *x = dyn; x->tag; x++) {
            if (x->tag == 7) rela = x->val;
            else if (x->tag == 8) relasz = x->val;
            else if (x->tag == 23) jmprel = x->val;
            else if (x->tag == 2) pltsz = x->val;
            else if (x->tag == 6) symtab = x->val;
        }
        u64 tabs[2][2] = { { rela, relasz }, { jmprel, pltsz } };
        for (int t = 0; t < 2; t++) {
            const rela_t *r = (const rela_t *)(base + tabs[t][0]);
            for (u64 k = 0; tabs[t][0] && k < tabs[t][1] / sizeof(rela_t); k++) {
                u32 type = r[k].info & 0xFFFFFFFF, si = r[k].info >> 32;
                u64 *where = (u64 *)(base + r[k].off);
                if (type == 8) *where = (u64)base + r[k].addend;                 // RELATIVE
                else if (type == 1 || type == 6 || type == 7) {                   // 64, GLOB_DAT, JUMP_SLOT
                    const sym_t *sym = (const sym_t *)(base + symtab) + si;
                    if (!symtab || !sym->shndx) { strlcpy(err, "Program wymaga nieznanej funkcji", 128); return -1; }
                    *where = (u64)base + sym->value + (type == 1 ? r[k].addend : 0);
                } else if (type != 0) { strlcpy(err, "Nieobslugiwana relokacja w programie", 128); return -1; }
            }
        }
    }
    *entry = (u64)base + e->entry;
    return 0;
}

int app_run_sap(const char *path) {
    if (!api.version) api_init();
    api.accent = g_theme.accent;
    u8 *d;
    u64 n;
    int r = fs_read_file(path, &d, &n);
    if (r) { dlg_message(NULL, "Program", fs_error(r), ICON_WARNING); return -1; }
    u64 entry;
    char err[128];
    r = load_elf(d, n, &entry, err);
    kfree(d);
    if (r) { dlg_message(NULL, "Program", err, ICON_WARNING); return -1; }
    kprintf("[sap] uruchamiam %s @ %p\n", path, (void *)entry);
    int (*main_fn)(const sartax_api_t *) = (void *)entry;
    return main_fn(&api);
}

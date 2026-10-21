// Sartax OS - klawiatura i mysz PS/2 (+ mysz bezwzgledna VirtualBox)
#include "kernel.h"
#include "gfx.h"

#define QSIZE 256
static event_t queue[QSIZE];
static volatile int q_head, q_tail;

int g_mouse_x, g_mouse_y;
u8 g_mouse_buttons;
bool g_mouse_absolute;

void input_push(const event_t *ev) {
    u64 f = irq_save();
    int n = (q_head + 1) % QSIZE;
    if (n != q_tail) {
        queue[q_head] = *ev;
        q_head = n;
    }
    irq_restore(f);
}

bool input_poll(event_t *ev) {
    u64 f = irq_save();
    bool ok = q_tail != q_head;
    if (ok) {
        *ev = queue[q_tail];
        q_tail = (q_tail + 1) % QSIZE;
    }
    irq_restore(f);
    return ok;
}

// ---------------- klawiatura ----------------
static const char map_lo[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
};
static const char map_hi[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0,
};

static bool e0, lshift, rshift, ctrl, alt, altgr, caps;

static u32 polish(char c, bool upper) {
    switch (c) {
    case 'a': return upper ? 0x104 : 0x105;
    case 'c': return upper ? 0x106 : 0x107;
    case 'e': return upper ? 0x118 : 0x119;
    case 'l': return upper ? 0x141 : 0x142;
    case 'n': return upper ? 0x143 : 0x144;
    case 'o': return upper ? 0xD3 : 0xF3;
    case 's': return upper ? 0x15A : 0x15B;
    case 'x': return upper ? 0x179 : 0x17A;
    case 'z': return upper ? 0x17B : 0x17C;
    }
    return 0;
}

static void key_event(u16 key, u32 ch, bool pressed) {
    event_t ev = { 0 };
    ev.type = EV_KEY;
    ev.key = key;
    ev.ch = ch;
    ev.pressed = pressed;
    ev.mods = ((lshift || rshift) ? MOD_SHIFT : 0) | (ctrl ? MOD_CTRL : 0) | (alt ? MOD_ALT : 0) | (altgr ? MOD_ALTGR : 0);
    input_push(&ev);
}

static void kbd_irq(void) {
    u8 sc = inb(0x60);
    if (sc == 0xE0) { e0 = true; return; }
    bool rel = sc & 0x80;
    u8 code = sc & 0x7F;
    bool ext = e0;
    e0 = false;
    u16 key = 0;
    if (ext) {
        switch (code) {
        case 0x48: key = KEY_UP; break;
        case 0x50: key = KEY_DOWN; break;
        case 0x4B: key = KEY_LEFT; break;
        case 0x4D: key = KEY_RIGHT; break;
        case 0x47: key = KEY_HOME; break;
        case 0x4F: key = KEY_END; break;
        case 0x49: key = KEY_PGUP; break;
        case 0x51: key = KEY_PGDN; break;
        case 0x53: key = KEY_DEL; break;
        case 0x52: key = KEY_INS; break;
        case 0x1C: key = KEY_ENTER; break;
        case 0x1D: ctrl = !rel; return;
        case 0x38: altgr = !rel; return;
        case 0x5B: case 0x5C: key = KEY_WIN; break;
        case 0x35: key = KEY_CHAR; if (!rel) { key_event(KEY_CHAR, '/', true); } return;
        default: return;
        }
        key_event(key, 0, !rel);
        return;
    }
    switch (code) {
    case 0x2A: lshift = !rel; return;
    case 0x36: rshift = !rel; return;
    case 0x1D: ctrl = !rel; return;
    case 0x38: alt = !rel; return;
    case 0x3A: if (!rel) caps = !caps; return;
    case 0x01: key = KEY_ESC; break;
    case 0x1C: key = KEY_ENTER; break;
    case 0x0E: key = KEY_BACKSPACE; break;
    case 0x0F: key = KEY_TAB; break;
    case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
        key = KEY_F1 + (code - 0x3B); break;
    case 0x57: key = KEY_F11; break;
    case 0x58: key = KEY_F12; break;
    // klawiatura numeryczna bez NumLock traktowana jak strzalki
    case 0x48: key = KEY_UP; break;
    case 0x50: key = KEY_DOWN; break;
    case 0x4B: key = KEY_LEFT; break;
    case 0x4D: key = KEY_RIGHT; break;
    case 0x47: key = KEY_HOME; break;
    case 0x4F: key = KEY_END; break;
    case 0x49: key = KEY_PGUP; break;
    case 0x51: key = KEY_PGDN; break;
    case 0x53: key = KEY_DEL; break;
    case 0x4A: key = KEY_CHAR; if (!rel) key_event(KEY_CHAR, '-', true); return;
    case 0x4E: key = KEY_CHAR; if (!rel) key_event(KEY_CHAR, '+', true); return;
    }
    if (key) { key_event(key, 0, !rel); return; }
    if (code >= 128) return;
    char c = map_lo[code];
    if (!c) return;
    bool shift = lshift || rshift;
    bool letter = c >= 'a' && c <= 'z';
    bool upper = letter ? (shift ^ caps) : shift;
    u32 ch = upper ? (u32)map_hi[code] : (u32)c;
    if (letter && !upper) ch = c;
    if ((altgr || (ctrl && alt)) && letter) {
        u32 p = polish(c, shift ^ caps);
        if (p) ch = p;
    }
    if (rel) return;
    key_event(KEY_CHAR, ch, true);
}

// ---------------- mysz ----------------
static u8 mpacket[4];
static int mcycle, mpacket_len = 3;

static void ps2_wait_w(void) { for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) {} }
static void ps2_wait_r(void) { for (int i = 0; i < 100000 && !(inb(0x64) & 1); i++) {} }
static void mouse_write(u8 v) {
    ps2_wait_w();
    outb(0x64, 0xD4);
    ps2_wait_w();
    outb(0x60, v);
    ps2_wait_r();
    inb(0x60);       // ACK
}
static u8 mouse_read(void) { ps2_wait_r(); return inb(0x60); }

extern bool vbox_mouse_get(int *x, int *y);

static void push_mouse(int wheel) {
    event_t ev = { 0 };
    ev.type = EV_MOUSE;
    ev.x = g_mouse_x;
    ev.y = g_mouse_y;
    ev.buttons = g_mouse_buttons;
    ev.wheel = wheel;
    ev.mods = ((lshift || rshift) ? MOD_SHIFT : 0) | (ctrl ? MOD_CTRL : 0) | (alt ? MOD_ALT : 0);
    input_push(&ev);
}

void mouse_poll_absolute(void) {
    if (!g_mouse_absolute) return;
    int x, y;
    if (vbox_mouse_get(&x, &y)) {
        x = (int)((i64)x * g_scr_w / 0x10000);
        y = (int)((i64)y * g_scr_h / 0x10000);
        x = CLAMP(x, 0, g_scr_w - 1);
        y = CLAMP(y, 0, g_scr_h - 1);
        if (x != g_mouse_x || y != g_mouse_y) {
            g_mouse_x = x;
            g_mouse_y = y;
            push_mouse(0);
        }
    }
}

static void mouse_irq(void) {
    u8 st = inb(0x64);
    if (!(st & 0x20)) { inb(0x60); return; }
    u8 b = inb(0x60);
    if (mcycle == 0 && !(b & 0x08)) return;     // synchronizacja
    mpacket[mcycle++] = b;
    if (mcycle < mpacket_len) return;
    mcycle = 0;
    if (mpacket[0] & 0xC0) return;               // przepelnienie
    int dx = (int)mpacket[1] - ((mpacket[0] << 4) & 0x100);
    int dy = (int)mpacket[2] - ((mpacket[0] << 3) & 0x100);
    int wheel = 0;
    if (mpacket_len == 4) {
        wheel = mpacket[3] & 0x0F;
        if (wheel & 0x08) wheel -= 16;
    }
    g_mouse_buttons = mpacket[0] & 7;
    int ax, ay;
    if (g_mouse_absolute && vbox_mouse_get(&ax, &ay)) {
        g_mouse_x = CLAMP((int)((i64)ax * g_scr_w / 0x10000), 0, g_scr_w - 1);
        g_mouse_y = CLAMP((int)((i64)ay * g_scr_h / 0x10000), 0, g_scr_h - 1);
    } else {
        g_mouse_x = CLAMP(g_mouse_x + dx, 0, g_scr_w - 1);
        g_mouse_y = CLAMP(g_mouse_y - dy, 0, g_scr_h - 1);
    }
    push_mouse(wheel);
}

void input_init(void) {
    // wyczysc bufor kontrolera
    for (int i = 0; i < 32 && (inb(0x64) & 1); i++) inb(0x60);
    // wlacz port myszy i przerwania
    ps2_wait_w();
    outb(0x64, 0xA8);
    ps2_wait_w();
    outb(0x64, 0x20);
    ps2_wait_r();
    u8 cfg = inb(0x60);
    cfg |= 0x03;        // IRQ1 + IRQ12
    cfg &= ~0x20;       // zegar myszy wlaczony
    cfg |= 0x40;        // translacja na set 1
    ps2_wait_w();
    outb(0x64, 0x60);
    ps2_wait_w();
    outb(0x60, cfg);

    mouse_write(0xF6);
    // IntelliMouse (kolko): 200, 100, 80
    mouse_write(0xF3); mouse_write(200);
    mouse_write(0xF3); mouse_write(100);
    mouse_write(0xF3); mouse_write(80);
    ps2_wait_w();
    outb(0x64, 0xD4);
    ps2_wait_w();
    outb(0x60, 0xF2);
    ps2_wait_r();
    inb(0x60);
    u8 id = mouse_read();
    mpacket_len = (id == 3 || id == 4) ? 4 : 3;
    mouse_write(0xF3); mouse_write(100);
    mouse_write(0xF4);
    for (int i = 0; i < 32 && (inb(0x64) & 1); i++) inb(0x60);

    g_mouse_x = g_scr_w / 2;
    g_mouse_y = g_scr_h / 2;
    irq_set_handler(1, kbd_irq);
    irq_set_handler(12, mouse_irq);
    irq_unmask(1);
    irq_unmask(2);
    irq_unmask(12);
    kprintf("[input] mysz id=%d pakiet=%d\n", id, mpacket_len);
}

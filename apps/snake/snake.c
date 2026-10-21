// Snake - przykladowy program dla Sartax OS
#include "../../sdk/sartax.h"

#define CW 24
#define CH 18
#define CELL 22
#define TOP 44

static const sartax_api_t *api;

typedef struct {
    int x[CW * CH], y[CW * CH];
    int len, dir, next_dir;
    int fx, fy;
    int score, best;
    int over, paused, started;
    uint64_t last;
} game_t;

static game_t G;

static void save_best(void) {
    char buf[32];
    int n = api->snprintf(buf, sizeof buf, "%d", G.best);
    api->write_file("/Dokumenty/snake_rekord.txt", buf, n);
}

static void load_best(void) {
    uint8_t *d;
    uint64_t n;
    G.best = 0;
    if (api->read_file("/Dokumenty/snake_rekord.txt", &d, &n) == 0) {
        for (uint64_t i = 0; i < n && d[i] >= '0' && d[i] <= '9'; i++) G.best = G.best * 10 + (d[i] - '0');
        api->free(d);
    }
}

static void place_food(void) {
    for (;;) {
        int fx = api->random() % CW, fy = api->random() % CH, ok = 1;
        for (int i = 0; i < G.len; i++)
            if (G.x[i] == fx && G.y[i] == fy) ok = 0;
        if (ok) { G.fx = fx; G.fy = fy; return; }
    }
}

static void reset(void) {
    G.len = 4;
    for (int i = 0; i < G.len; i++) { G.x[i] = 8 - i; G.y[i] = CH / 2; }
    G.dir = G.next_dir = 0;
    G.score = 0;
    G.over = 0;
    G.paused = 0;
    G.started = 0;
    place_food();
}

static void step(void) {
    static const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    if ((G.next_dir + 2) % 4 != G.dir) G.dir = G.next_dir;
    int nx = G.x[0] + dx[G.dir], ny = G.y[0] + dy[G.dir];
    if (nx < 0 || ny < 0 || nx >= CW || ny >= CH) { G.over = 1; }
    for (int i = 0; i < G.len - 1 && !G.over; i++)
        if (G.x[i] == nx && G.y[i] == ny) G.over = 1;
    if (G.over) {
        if (G.score > G.best) { G.best = G.score; save_best(); }
        return;
    }
    int grow = nx == G.fx && ny == G.fy;
    if (grow && G.len < CW * CH) G.len++;
    for (int i = G.len - 1; i > 0; i--) { G.x[i] = G.x[i - 1]; G.y[i] = G.y[i - 1]; }
    G.x[0] = nx;
    G.y[0] = ny;
    if (grow) { G.score += 10; place_food(); }
}

static int tick(sx_window_t *w, void *u) {
    uint64_t now = api->ticks_ms();
    int speed = 130 - (G.score / 50) * 8;
    if (speed < 60) speed = 60;
    if (G.started && !G.over && !G.paused && now - G.last >= (uint64_t)speed) {
        G.last = now;
        step();
        return 1;
    }
    return 0;
}

static void frame(sx_window_t *w, void *u) {
    const sx_input_t *in = api->input();
    if (in->key) {
        int k = in->key, d = -1;
        if (k == SX_KEY_RIGHT) d = 0; else if (k == SX_KEY_DOWN) d = 1; else if (k == SX_KEY_LEFT) d = 2; else if (k == SX_KEY_UP) d = 3;
        if (k == SX_KEY_CHAR) {
            uint32_t c = in->ch | 32;
            if (c == 'd') d = 0; else if (c == 's') d = 1; else if (c == 'a') d = 2; else if (c == 'w') d = 3;
            else if (c == 'p') G.paused = !G.paused;
            else if (c == ' ' || c == ('r' | 32)) { if (G.over) reset(); }
        }
        if (k == SX_KEY_ENTER && G.over) reset();
        if (d >= 0 && !G.over) { G.next_dir = d; G.started = 1; }
        api->consume_key();
    }
    // tlo
    api->gradient(0, 0, in->width, TOP, SX_RGB(30, 160, 110), SX_RGB(20, 120, 90));
    char t[64];
    api->snprintf(t, sizeof t, "Wynik: %d", G.score);
    api->text(14, 12, SX_FONT_TITLE, t, SX_RGB(255, 255, 255));
    api->snprintf(t, sizeof t, "Rekord: %d", G.best);
    api->text(in->width - api->text_width(SX_FONT_BOLD, t) - 14, 14, SX_FONT_BOLD, t, SX_RGB(230, 255, 240));
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            api->fill(x * CELL, TOP + y * CELL, CELL, CELL, (x + y) & 1 ? SX_RGB(170, 215, 80) : SX_RGB(162, 208, 72));
    api->circle(G.fx * CELL + CELL / 2, TOP + G.fy * CELL + CELL / 2, CELL / 2 - 2, SX_RGB(230, 60, 50));
    api->fill(G.fx * CELL + CELL / 2, TOP + G.fy * CELL + 1, 2, 5, SX_RGB(90, 60, 30));
    for (int i = G.len - 1; i >= 0; i--) {
        uint32_t c = i == 0 ? SX_RGB(40, 90, 200) : (i & 1 ? SX_RGB(70, 130, 235) : SX_RGB(60, 115, 225));
        api->round_rect(G.x[i] * CELL + 1, TOP + G.y[i] * CELL + 1, CELL - 2, CELL - 2, 6, c);
    }
    // oczy
    api->circle(G.x[0] * CELL + 8, TOP + G.y[0] * CELL + 8, 3, SX_RGB(255, 255, 255));
    api->circle(G.x[0] * CELL + 15, TOP + G.y[0] * CELL + 8, 3, SX_RGB(255, 255, 255));
    if (!G.started || G.over || G.paused) {
        int bw = 340, bh = 130, bx = (in->width - bw) / 2, by = TOP + (CH * CELL - bh) / 2;
        api->round_rect(bx, by, bw, bh, 14, SX_RGBA(0, 0, 0, 170));
        const char *m1 = G.over ? "Koniec gry!" : G.paused ? "Pauza" : "Snake";
        const char *m2 = G.over ? "Enter albo spacja - zagraj jeszcze raz" : G.paused ? "P - wznow" : "Strzalki albo WASD - start, P - pauza";
        api->text_center(bx, by + 24, bw, SX_FONT_BIG, m1, SX_RGB(255, 255, 255));
        api->text_center(bx, by + 76, bw, SX_FONT_UI, m2, SX_RGB(230, 240, 235));
        if (G.over && api->button(1, bx + bw / 2 - 60, by + bh + 10, 120, 34, "Jeszcze raz", SX_BTN_PRIMARY)) reset();
    }
}

int sap_main(const sartax_api_t *a) {
    api = a;
    load_best();
    reset();
    sx_window_t *w = api->window_create("Snake", CW * CELL, TOP + CH * CELL, frame, 0);
    api->window_set_tick(w, tick);
    return 0;
}

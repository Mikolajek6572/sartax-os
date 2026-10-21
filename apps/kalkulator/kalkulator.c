// Kalkulator - przykladowy program dla Sartax OS (liczby stalopozycyjne, 4 miejsca po przecinku)
#include "../../sdk/sartax.h"

#define SCALE 10000LL

static const sartax_api_t *api;
static long long acc, cur;
static int op;              // 0 brak, '+', '-', '*', '/'
static int typing, frac_digits, has_frac, error;
static char expr[64];

static void fmt(long long v, char *out, int cap) {
    int neg = v < 0;
    if (neg) v = -v;
    long long ip = v / SCALE, fp = v % SCALE;
    char f[8];
    int n = 0;
    if (fp) {
        f[n++] = ',';
        for (long long d = SCALE / 10; d && fp; d /= 10) { f[n++] = '0' + fp / d; fp %= d; }
    }
    f[n] = 0;
    api->snprintf(out, cap, "%s%lld%s", neg ? "-" : "", ip, f);
}

static long long apply(long long a, long long b, int o) {
    switch (o) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b / SCALE;
    case '/': if (!b) { error = 1; return 0; } return a * SCALE / b;
    }
    return b;
}

static void digit(int d) {
    if (error) return;
    if (!typing) { cur = 0; typing = 1; has_frac = 0; frac_digits = 0; }
    if (has_frac) {
        if (frac_digits >= 4) return;
        long long m = SCALE;
        for (int i = 0; i <= frac_digits; i++) m /= 10;
        cur += (cur < 0 ? -d : d) * m;
        frac_digits++;
    } else if (cur < 100000000000LL * SCALE) cur = cur * 10 + (cur < 0 ? -d : d) * SCALE;
}

static void press(const char *k) {
    char c = k[0];
    if (c >= '0' && c <= '9') { digit(c - '0'); return; }
    if (c == 'C') { acc = cur = 0; op = 0; typing = 0; error = 0; expr[0] = 0; return; }
    if (error) return;
    if (c == ',') { if (!typing) { cur = 0; typing = 1; frac_digits = 0; } has_frac = 1; return; }
    if (c == 'n') { cur = -cur; return; }
    if (c == '%') { cur = cur / 100; return; }
    if (c == '<') {   // backspace
        if (!typing) return;
        if (has_frac && frac_digits) {
            frac_digits--;
            long long m = SCALE;
            for (int i = 0; i < frac_digits; i++) m /= 10;
            cur = cur / m * m;
        } else if (has_frac) has_frac = 0;
        else cur = cur / (10 * SCALE) * SCALE;
        return;
    }
    if (c == '=') {
        if (op) {
            char a[32], b[32];
            fmt(acc, a, sizeof a);
            fmt(cur, b, sizeof b);
            api->snprintf(expr, sizeof expr, "%s %c %s =", a, op == '*' ? 'x' : op == '/' ? ':' : op, b);
            cur = apply(acc, cur, op);
            op = 0;
        }
        typing = 0;
        return;
    }
    // operator
    if (op && typing) cur = apply(acc, cur, op);
    acc = cur;
    op = c;
    typing = 0;
    char a[32];
    fmt(acc, a, sizeof a);
    api->snprintf(expr, sizeof expr, "%s %c", a, op == '*' ? 'x' : op == '/' ? ':' : op);
}

static const char *keys[5][4] = {
    { "C", "<", "%", "/" },
    { "7", "8", "9", "*" },
    { "4", "5", "6", "-" },
    { "1", "2", "3", "+" },
    { "n", "0", ",", "=" },
};

static void frame(sx_window_t *w, void *u) {
    const sx_input_t *in = api->input();
    if (in->key == SX_KEY_CHAR) {
        char c = (char)in->ch;
        char s[2] = { c, 0 };
        if (c == '.') s[0] = ',';
        if (c == 'x') s[0] = '*';
        if (c == ':') s[0] = '/';
        if ((s[0] >= '0' && s[0] <= '9') || s[0] == '+' || s[0] == '-' || s[0] == '*' || s[0] == '/' || s[0] == ',' ||
            s[0] == '=' || s[0] == '%')
            press(s);
        if (c == 'c' || c == 'C') press("C");
        api->consume_key();
    } else if (in->key == SX_KEY_ENTER) { press("="); api->consume_key(); }
    else if (in->key == SX_KEY_BACKSPACE) { press("<"); api->consume_key(); }
    else if (in->key == SX_KEY_ESC) { press("C"); api->consume_key(); }

    api->fill(0, 0, in->width, in->height, SX_RGB(32, 38, 46));
    api->text(in->width - 16 - api->text_width(SX_FONT_UI, expr), 16, SX_FONT_UI, expr, SX_RGB(150, 162, 175));
    char d[40];
    if (error) api->snprintf(d, sizeof d, "Nie dziel przez 0");
    else fmt(cur, d, sizeof d);
    int f = api->text_width(SX_FONT_HUGE, d) > in->width - 32 ? SX_FONT_BIG : SX_FONT_HUGE;
    api->text(in->width - 16 - api->text_width(f, d), f == SX_FONT_HUGE ? 40 : 56, f, d, SX_RGB(255, 255, 255));
    int bw = (in->width - 20) / 4, bh = 58, y0 = 118;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            const char *k = keys[r][c];
            char lab[4] = { k[0], 0, 0, 0 };
            if (k[0] == 'n') { lab[0] = '+'; lab[1] = '/'; lab[2] = '-'; }
            if (k[0] == '<') { lab[0] = '<'; lab[1] = '-'; }
            if (k[0] == '*') lab[0] = 'x';
            if (k[0] == '/') lab[0] = ':';
            int style = (c == 3) ? SX_BTN_PRIMARY : (r == 0 ? SX_BTN_DANGER * 0 + SX_BTN_NORMAL : SX_BTN_NORMAL);
            if (k[0] == 'C') style = SX_BTN_DANGER;
            if (api->button(r * 4 + c, 10 + c * bw + 2, y0 + r * (bh + 4), bw - 4, bh, lab, style)) press(k);
        }
}

int sap_main(const sartax_api_t *a) {
    api = a;
    acc = cur = 0;
    op = 0;
    expr[0] = 0;
    api->window_create("Kalkulator", 320, 118 + 5 * 62 + 10, frame, 0);
    return 0;
}

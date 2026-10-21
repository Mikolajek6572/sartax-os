// Sartax OS - Przegladarka Sartax (wlasny silnik HTML)
// Karty, historia, obrazki PNG/JPEG/GIF/BMP, kolory i wysrodkowanie (style inline, <font>, <center>),
// formularze GET, encje, UTF-8 / ISO-8859-2 / Windows-1250, wlasna wyszukiwarka (wyniki z DuckDuckGo).
// Bez arkuszy CSS i JavaScriptu.
#include "../apps.h"
#include "../net.h"

enum { F_TEXT, F_BR, F_BLOCK, F_HR, F_LI, F_INDENT, F_IMG, F_INPUT, F_BUTTON, F_SPACE, F_MAIN };
enum { B_TEXT, B_HR, B_IMG, B_INPUT, B_BUTTON, B_ALT };
enum { AL_LEFT, AL_CENTER, AL_RIGHT };

typedef struct {
    u8 type, font, pre, align;
    u32 color;          // 0 = domyslny
    int text, len;      // w arenie
    int link;
    int val, w, h;
} frag_t;

typedef struct {
    int x, y, w, h;
    u8 kind, font;
    u32 color;
    int text, len;
    int link;
    int val;
} box_t;

typedef struct {
    int form;
    int type;           // 0 tekst, 1 ukryte, 2 przycisk
    char name[64];
    char value[256];
} field_t;

typedef struct {
    int url;            // w arenie
    image_t *img;
    u8 state;           // 0 czeka, 1 gotowy, 2 blad
} pimg_t;

typedef struct {
    char *arena;
    int alen, acap;
    frag_t *frags;
    int nfrags, cfrags;
    int *links;
    int nlinks, clinks;
    field_t *fields;
    int nfields;
    char (*forms)[512];
    int nforms;
    pimg_t *imgs;
    int nimgs, cimgs;
    box_t *boxes;
    int nboxes, cboxes;
    int doc_h, laid_w;
    int main_prio, main_y;
    u32 bg, text_col, link_col;
    char title[128];
} page_t;

typedef struct {
    char url[512];
    char addr[512];
    page_t *pg;
    int scroll;
    char hist[24][512];
    int nhist, hpos;
    int focus_field;
    bool jump_main;
    int img_next;
} tab_t;

typedef struct {
    tab_t *tabs[8];
    int ntabs, cur;
    window_t *win;
    tab_t *pend_tab;
    char pending[600];
    bool pending_record;
    char status[300];
} browser_t;

#define TABS_H 40
#define TB_H 48
#define MARGIN 24
#define TOP_H (TABS_H + TB_H)
#define LINK_COL RGB(26, 92, 200)
#define TEXT_COL RGB(32, 33, 36)
void browser_load_pending(void *ctx);
#define SEARCH_URL "https://html.duckduckgo.com/html/?kl=pl-pl&q="

// ---------------- strony wbudowane ----------------
static const char *LOGO_HTML =
    "<font color=\"#14a0a0\">S</font><font color=\"#f08c1e\">a</font><font color=\"#28a55a\">r</font>"
    "<font color=\"#1e82e6\">t</font><font color=\"#e1463c\">a</font><font color=\"#d7a500\">x</font>";

static char *start_page_html(void) {
    char *h = kmalloc(4096);
    snprintf(h, 4096,
        "<title>Nowa karta</title><body>"
        "<br><br><br><center><font size=\"7\">%s</font><br>"
        "<font color=\"#5f6368\">Szukaj w internecie</font><br><br>"
        "<form action=\"sartax:szukaj\"><input type=\"text\" name=\"q\" size=\"60\"><br><br>"
        "<input type=\"submit\" value=\"Szukaj w Sartax\"> &nbsp; <input type=\"submit\" name=\"traf\" value=\"Szczesliwy traf\"></form>"
        "<br><br><font color=\"#5f6368\" size=\"2\">Na skroty:</font><br><br>"
        "<a href=\"https://pl.wikipedia.org/wiki/Wikipedia:Strona_g%%C5%%82%%C3%%B3wna\">Wikipedia</a> &nbsp;&bull;&nbsp; "
        "<a href=\"https://github.com/Mikolajek6572/sartax-os\">Sartax OS na GitHubie</a> &nbsp;&bull;&nbsp; "
        "<a href=\"https://wttr.in/Warszawa?lang=pl\">Pogoda</a> &nbsp;&bull;&nbsp; "
        "<a href=\"https://text.npr.org/\">NPR</a> &nbsp;&bull;&nbsp; "
        "<a href=\"https://lite.cnn.com/\">CNN Lite</a> &nbsp;&bull;&nbsp; "
        "<a href=\"https://example.com/\">example.com</a>"
        "<br><br><br><br><font size=\"2\" color=\"#80868b\">Przegladarka Sartax %s - wlasny silnik stron, bez JavaScriptu</font>"
        "</center></body>",
        LOGO_HTML, SARTAX_VERSION);
    return h;
}

// ---------------- pomocnicze ----------------
static int arena_add(page_t *p, const char *s, int n) {
    if (p->alen + n + 1 > p->acap) {
        p->acap = (p->alen + n + 1) * 2;
        p->arena = krealloc(p->arena, p->acap);
    }
    int off = p->alen;
    memcpy(p->arena + off, s, n);
    p->alen += n;
    p->arena[p->alen] = 0;
    return off;
}

static int arena_str(page_t *p, const char *s) { return arena_add(p, s, strlen(s) + 1); }

static frag_t *frag_new(page_t *p, int type) {
    if (p->nfrags == p->cfrags) {
        p->cfrags = p->cfrags ? p->cfrags * 2 : 256;
        p->frags = krealloc(p->frags, p->cfrags * sizeof(frag_t));
    }
    frag_t *f = &p->frags[p->nfrags++];
    memset(f, 0, sizeof *f);
    f->type = type;
    f->link = -1;
    return f;
}

static void page_free(page_t *p) {
    if (!p) return;
    for (int i = 0; i < p->nimgs; i++) image_free(p->imgs[i].img);
    kfree(p->imgs);
    kfree(p->arena);
    kfree(p->frags);
    kfree(p->links);
    kfree(p->fields);
    kfree(p->forms);
    kfree(p->boxes);
    kfree(p);
}

static bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f'; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static bool starts_ci(const char *s, const char *end, const char *pre) {
    for (; *pre; pre++, s++)
        if (s >= end || lower(*s) != *pre) return false;
    return true;
}

static const struct { const char *n; u32 cp; } entities[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' },
    { "copy", 0xA9 }, { "reg", 0xAE }, { "raquo", 0xBB }, { "laquo", 0xAB }, { "mdash", 0x2014 }, { "ndash", 0x2013 },
    { "hellip", 0x2026 }, { "bull", 0x2022 }, { "middot", 0xB7 }, { "deg", 0xB0 }, { "euro", 0x20AC }, { "times", 0xD7 },
    { "rarr", 0x2192 }, { "larr", 0x2190 }, { "uarr", 0x2191 }, { "darr", 0x2193 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
    { "bdquo", 0x201E }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "oacute", 0xF3 }, { "Oacute", 0xD3 }, { "eacute", 0xE9 },
    { "aacute", 0xE1 }, { "uuml", 0xFC }, { "ouml", 0xF6 }, { "auml", 0xE4 }, { "szlig", 0xDF }, { "sect", 0xA7 },
    { "shy", 0 }, { "zwj", 0 }, { "zwnj", 0 }, { "thinsp", ' ' }, { "ensp", ' ' }, { "emsp", ' ' }, { "star", 0x2605 },
    { "trade", 0x2122 }, { "minus", 0x2212 }, { "le", 0x2264 }, { "ge", 0x2265 }, { "ne", 0x2260 }, { "infin", 0x221E },
};

static int decode_text(const char *s, int n, char *out, int cap) {
    int o = 0;
    for (int i = 0; i < n && o < cap - 5;) {
        if (s[i] == '&') {
            int j = i + 1;
            while (j < n && j - i < 12 && s[j] != ';' && !is_space(s[j]) && s[j] != '&') j++;
            if (j < n && s[j] == ';') {
                u32 cp = 0xFFFFFFFF;
                if (s[i + 1] == '#') {
                    cp = 0;
                    if (s[i + 2] == 'x' || s[i + 2] == 'X') {
                        for (int k = i + 3; k < j; k++) {
                            char c = lower(s[k]);
                            cp = cp * 16 + (c >= 'a' ? c - 'a' + 10 : c - '0');
                        }
                    } else for (int k = i + 2; k < j; k++) cp = cp * 10 + (s[k] - '0');
                    if (cp == 160) cp = ' ';
                } else {
                    for (int e = 0; e < (int)ARRAY_LEN(entities); e++)
                        if ((int)strlen(entities[e].n) == j - i - 1 && !strncmp(entities[e].n, s + i + 1, j - i - 1)) { cp = entities[e].cp; break; }
                }
                if (cp != 0xFFFFFFFF) {
                    if (cp) o += utf8_encode(cp, out + o);
                    i = j + 1;
                    continue;
                }
            }
        }
        if ((u8)s[i] == 0xC2 && i + 1 < n && (u8)s[i + 1] == 0xA0) { out[o++] = ' '; i += 2; continue; }
        out[o++] = s[i++];
    }
    out[o] = 0;
    return o;
}

static bool get_attr(const char *a, int n, const char *name, char *out, int cap) {
    int nl = strlen(name);
    for (int i = 0; i < n; i++) {
        if ((i == 0 || is_space(a[i - 1])) && starts_ci(a + i, a + n, name)) {
            int j = i + nl;
            while (j < n && is_space(a[j])) j++;
            if (j >= n || a[j] != '=') continue;
            j++;
            while (j < n && is_space(a[j])) j++;
            char q = 0;
            if (j < n && (a[j] == '"' || a[j] == '\'')) q = a[j++];
            int k = j;
            while (k < n && (q ? a[k] != q : !is_space(a[k]))) k++;
            decode_text(a + j, k - j, out, cap);
            return true;
        }
    }
    out[0] = 0;
    return false;
}

// ---------------- adresy ----------------
static bool resolve_url(const char *base, const char *href, char *out, int cap) {
    while (is_space(*href)) href++;
    if (!*href || href[0] == '#') return false;
    if (!strncmp(href, "javascript:", 11) || !strncmp(href, "mailto:", 7) || !strncmp(href, "tel:", 4)) return false;
    char tmp[1024];
    if (!strncmp(href, "data:", 5)) { strlcpy(out, href, cap); return true; }
    if (strstr(href, "://") || !strncmp(href, "sartax:", 7) || !strncmp(href, "plik:", 5)) strlcpy(tmp, href, sizeof tmp);
    else if (href[0] == '/' && href[1] == '/') snprintf(tmp, sizeof tmp, "%s%s", !strncmp(base, "http:", 5) ? "http:" : "https:", href);
    else {
        const char *hs = strstr(base, "://");
        if (!hs) {
            if (!strncmp(base, "plik:", 5)) {
                if (href[0] == '/') snprintf(tmp, sizeof tmp, "plik:%s", href);
                else {
                    char dir[512];
                    path_parent(dir, sizeof dir, base + 5);
                    snprintf(tmp, sizeof tmp, "plik:%s/%s", strcmp(dir, "/") ? dir : "", href);
                }
                strlcpy(out, tmp, cap);
                return true;
            }
            if (!strncmp(base, "sartax:", 7) && href[0] == '?') {
                const char *q = strchr(base, '?');
                snprintf(tmp, sizeof tmp, "%.*s%s", q ? (int)(q - base) : (int)strlen(base), base, href);
                strlcpy(out, tmp, cap);
                return true;
            }
            return false;
        }
        const char *path = strchr(hs + 3, '/');
        int hostlen = path ? path - base : (int)strlen(base);
        if (href[0] == '/') snprintf(tmp, sizeof tmp, "%.*s%s", hostlen, base, href);
        else if (href[0] == '?') {
            const char *q = strchr(base, '?');
            int bl = q ? q - base : (int)strlen(base);
            snprintf(tmp, sizeof tmp, "%.*s%s", bl, base, href);
        } else {
            const char *q = strchr(base, '?');
            int bl = q ? q - base : (int)strlen(base);
            int last = hostlen;
            for (int i = hostlen; i < bl; i++)
                if (base[i] == '/') last = i;
            snprintf(tmp, sizeof tmp, "%.*s/%s", last, base, href);
        }
    }
    char *h = strchr(tmp, '#');
    if (h) *h = 0;
    char *pstart = strstr(tmp, "://");
    pstart = pstart ? strchr(pstart + 3, '/') : NULL;
    if (pstart) {
        char *q = strchr(pstart, '?');
        char query[600] = "";
        if (q) { strlcpy(query, q, sizeof query); *q = 0; }
        static char segs[64][128];
        int ns = 0;
        char *p = pstart + 1;
        bool trailing = tmp[strlen(tmp) - 1] == '/';
        while (*p && ns < 64) {
            char *e = strchr(p, '/');
            int l = e ? e - p : (int)strlen(p);
            if (l == 2 && !strncmp(p, "..", 2)) { if (ns) ns--; }
            else if (!(l == 1 && p[0] == '.') && l > 0) { strlcpy(segs[ns], p, MIN(l + 1, 128)); ns++; }
            if (!e) break;
            p = e + 1;
        }
        char rebuilt[1024];
        int o = pstart - tmp;
        memcpy(rebuilt, tmp, o);
        rebuilt[o] = 0;
        for (int i = 0; i < ns; i++) { strlcat(rebuilt, "/", sizeof rebuilt); strlcat(rebuilt, segs[i], sizeof rebuilt); }
        if (!ns || trailing) strlcat(rebuilt, "/", sizeof rebuilt);
        strlcat(rebuilt, query, sizeof rebuilt);
        strlcpy(tmp, rebuilt, sizeof tmp);
    }
    strlcpy(out, tmp, cap);
    return true;
}

static void url_encode(const char *s, char *out, int cap) {
    int o = 0;
    for (; *s && o < cap - 4; s++) {
        u8 c = *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = c;
        else if (c == ' ') out[o++] = '+';
        else { snprintf(out + o, 4, "%%%02X", c); o += 3; }
    }
    out[o] = 0;
}

static int hexv(char c) {
    c = lower(c);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static void url_decode(const char *s, int n, char *out, int cap) {
    int o = 0;
    for (int i = 0; i < n && o < cap - 1; i++) {
        if (s[i] == '%' && i + 2 < n && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) {
            out[o++] = hexv(s[i + 1]) * 16 + hexv(s[i + 2]);
            i += 2;
        } else out[o++] = s[i] == '+' ? ' ' : s[i];
    }
    out[o] = 0;
}

// wartosc parametru z adresu (?a=b&c=d)
static bool url_param(const char *url, const char *name, char *out, int cap) {
    const char *q = strchr(url, '?');
    if (!q) return false;
    int nl = strlen(name);
    for (const char *p = q + 1; *p;) {
        const char *e = strchr(p, '&');
        int l = e ? e - p : (int)strlen(p);
        if (l > nl && !strncmp(p, name, nl) && p[nl] == '=') { url_decode(p + nl + 1, l - nl - 1, out, cap); return true; }
        if (!e) break;
        p = e + 1;
    }
    out[0] = 0;
    return false;
}

// ---------------- kodowanie znakow ----------------
static char *to_utf8(const u8 *d, size_t n, const char *ctype) {
    char cs = 0;
    char probe[1100];
    size_t pl = MIN(n, 1024);
    for (size_t i = 0; i < pl; i++) probe[i] = lower(d[i]);
    probe[pl] = 0;
    char ct[64] = "";
    for (int i = 0; i < 63 && ctype[i]; i++) ct[i] = lower(ctype[i]), ct[i + 1] = 0;
    if (strstr(ct, "8859-2") || strstr(probe, "charset=iso-8859-2") || strstr(probe, "charset=\"iso-8859-2")) cs = 2;
    else if (strstr(ct, "1250") || strstr(probe, "charset=windows-1250") || strstr(probe, "charset=\"windows-1250")) cs = 5;
    else if (strstr(ct, "8859-1") || strstr(ct, "1252")) cs = 1;
    char *out = kmalloc(n * 3 + 1);
    if (!cs) { memcpy(out, d, n); out[n] = 0; return out; }
    static const u16 iso2[96] = {
        0xA0, 0x104, 0x2D8, 0x141, 0xA4, 0x13D, 0x15A, 0xA7, 0xA8, 0x160, 0x15E, 0x164, 0x179, 0xAD, 0x17D, 0x17B,
        0xB0, 0x105, 0x2DB, 0x142, 0xB4, 0x13E, 0x15B, 0x2C7, 0xB8, 0x161, 0x15F, 0x165, 0x17A, 0x2DD, 0x17E, 0x17C,
        0x154, 0xC1, 0xC2, 0x102, 0xC4, 0x139, 0x106, 0xC7, 0x10C, 0xC9, 0x118, 0xCB, 0x11A, 0xCD, 0xCE, 0x10E,
        0x110, 0x143, 0x147, 0xD3, 0xD4, 0x150, 0xD6, 0xD7, 0x158, 0x16E, 0xDA, 0x170, 0xDC, 0xDD, 0x162, 0xDF,
        0x155, 0xE1, 0xE2, 0x103, 0xE4, 0x13A, 0x107, 0xE7, 0x10D, 0xE9, 0x119, 0xEB, 0x11B, 0xED, 0xEE, 0x10F,
        0x111, 0x144, 0x148, 0xF3, 0xF4, 0x151, 0xF6, 0xF7, 0x159, 0x16F, 0xFA, 0x171, 0xFC, 0xFD, 0x163, 0x2D9,
    };
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        u8 c = d[i];
        u32 cp = c;
        if (c >= 0xA0 && cs == 2) cp = iso2[c - 0xA0];
        else if (cs == 5 && c >= 0x80) {
            switch (c) {
            case 0x8C: cp = 0x15A; break; case 0x9C: cp = 0x15B; break; case 0x8F: cp = 0x179; break; case 0x9F: cp = 0x17A; break;
            case 0xA5: cp = 0x104; break; case 0xB9: cp = 0x105; break; case 0xBC: cp = 0x13D; break; case 0xBE: cp = 0x13E; break;
            case 0x84: cp = 0x201E; break; case 0x94: cp = 0x201D; break; case 0x96: cp = 0x2013; break; case 0x97: cp = 0x2014; break;
            default: cp = c >= 0xA0 ? iso2[c - 0xA0] : '?';
            }
        }
        o += utf8_encode(cp, out + o);
    }
    out[o] = 0;
    return out;
}

// ---------------- kolory i style ----------------
static const struct { const char *n; u32 c; } named_colors[] = {
    { "black", RGB(0, 0, 0) }, { "white", RGB(255, 255, 255) }, { "red", RGB(220, 30, 30) }, { "green", RGB(0, 128, 0) },
    { "blue", RGB(0, 0, 230) }, { "gray", RGB(128, 128, 128) }, { "grey", RGB(128, 128, 128) }, { "orange", RGB(255, 140, 0) },
    { "yellow", RGB(230, 200, 0) }, { "navy", RGB(0, 0, 128) }, { "maroon", RGB(128, 0, 0) }, { "teal", RGB(0, 128, 128) },
    { "silver", RGB(192, 192, 192) }, { "darkgreen", RGB(0, 100, 0) }, { "darkred", RGB(139, 0, 0) }, { "darkblue", RGB(0, 0, 139) },
    { "darkgray", RGB(169, 169, 169) }, { "lightgray", RGB(211, 211, 211) }, { "brown", RGB(150, 75, 0) }, { "olive", RGB(128, 128, 0) },
    { "purple", RGB(128, 0, 128) }, { "crimson", RGB(220, 20, 60) }, { "gold", RGB(255, 200, 0) }, { "dimgray", RGB(105, 105, 105) },
};

static u32 parse_color(const char *v) {
    while (is_space(*v)) v++;
    if (v[0] == '#') {
        int l = 0;
        while (hexv(v[1 + l]) >= 0) l++;
        if (l == 3) return RGB(hexv(v[1]) * 17, hexv(v[2]) * 17, hexv(v[3]) * 17);
        if (l >= 6) return RGB(hexv(v[1]) * 16 + hexv(v[2]), hexv(v[3]) * 16 + hexv(v[4]), hexv(v[5]) * 16 + hexv(v[6]));
        return 0;
    }
    if (!strncmp(v, "rgb", 3)) {
        const char *p = strchr(v, '(');
        if (!p) return 0;
        int c[3] = { 0 };
        for (int i = 0; i < 3; i++) {
            p++;
            while (is_space(*p)) p++;
            c[i] = CLAMP(katoi(p), 0, 255);
            while (*p && *p != ',' && *p != ')' && *p != ' ') p++;
            while (*p == ' ') p++;
            if (*p != ',' && i < 2) { if (*p == ')') break; }
        }
        return RGB(c[0], c[1], c[2]);
    }
    char n[24];
    int k = 0;
    while (v[k] && k < 23 && !is_space(v[k]) && v[k] != ';') { n[k] = lower(v[k]); k++; }
    n[k] = 0;
    for (int i = 0; i < (int)ARRAY_LEN(named_colors); i++)
        if (!strcmp(n, named_colors[i].n)) return named_colors[i].c;
    return 0;
}

// rozmiar czcionki w px z wartosci CSS (0 = nie rozpoznano)
static int parse_size(const char *v) {
    while (is_space(*v)) v++;
    if (!strncmp(v, "xx-large", 8)) return 32;
    if (!strncmp(v, "x-large", 7)) return 24;
    if (!strncmp(v, "large", 5)) return 18;
    if (!strncmp(v, "small", 5) || !strncmp(v, "x-small", 7)) return 13;
    int n = 0, frac = 0, fd = 0;
    const char *p = v;
    while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { if (fd < 2) { frac = frac * 10 + (*p - '0'); fd++; } p++; } }
    if (p == v) return 0;
    int x100 = n * 100 + (fd == 1 ? frac * 10 : frac);
    if (!strncmp(p, "px", 2)) return x100 / 100;
    if (!strncmp(p, "pt", 2)) return x100 * 4 / 300;
    if (!strncmp(p, "em", 2) || !strncmp(p, "rem", 3)) return x100 * 16 / 100;
    if (*p == '%') return x100 * 16 / 10000;
    return 0;
}

typedef struct {
    char name[12];
    u32 color;
    u8 align;
    u8 size;      // px, 0 = dziedziczony
    i8 bold;      // -1 dziedziczony
} sstyle_t;

typedef struct {
    int bold, mono, pre, small, heading;
    int link_stack[32], nlink;
    int list_type[16], list_n[16], nlist;
    int cur_form;
    bool in_title, in_button;
    char button_label[96];
    sstyle_t st[64];
    int nst;
    int center;
} pstate_t;

static u32 cur_color(pstate_t *s) {
    for (int i = s->nst - 1; i >= 0; i--)
        if (s->st[i].color) return s->st[i].color;
    return 0;
}

static u8 cur_align(pstate_t *s) {
    for (int i = s->nst - 1; i >= 0; i--)
        if (s->st[i].align) return s->st[i].align;
    return s->center ? AL_CENTER : AL_LEFT;
}

static int cur_font(pstate_t *s) {
    int size = 0, bold = -1;
    for (int i = s->nst - 1; i >= 0; i--) {
        if (!size && s->st[i].size) size = s->st[i].size;
        if (bold < 0 && s->st[i].bold >= 0) bold = s->st[i].bold;
    }
    bool b = bold >= 0 ? bold : (s->bold > 0);
    if (s->pre || s->mono) return FONT_WEB_MONO;
    if (!size) {
        if (s->heading == 1) return FONT_WEB_H1;
        if (s->heading == 2) return FONT_WEB_H2;
        if (s->heading == 3) return FONT_WEB_H3;
        if (s->heading >= 4) return FONT_WEB_BOLD;
        if (s->small) return FONT_WEB_SMALL;
        return b ? FONT_WEB_BOLD : FONT_WEB;
    }
    if (size <= 13) return FONT_WEB_SMALL;
    if (size <= 16) return b ? FONT_WEB_BOLD : FONT_WEB;
    if (size <= 20) return b ? FONT_WEB_H3 : FONT_WEB_TITLE;
    if (size <= 26) return FONT_WEB_H2;
    if (size <= 40) return FONT_WEB_H1;
    return FONT_LOGO;
}

// styl z atrybutow elementu: style="", align, color (font), size (font)
static bool read_style(const char *name, const char *a, int n, sstyle_t *out) {
    memset(out, 0, sizeof *out);
    out->bold = -1;
    strlcpy(out->name, name, sizeof out->name);
    bool any = false;
    char v[512];
    if (get_attr(a, n, "align", v, sizeof v)) {
        if (!strcmp(v, "center") || !strcmp(v, "middle")) { out->align = AL_CENTER; any = true; }
        else if (!strcmp(v, "right")) { out->align = AL_RIGHT; any = true; }
        else if (!strcmp(v, "left")) { out->align = AL_LEFT; }
    }
    if (!strcmp(name, "font")) {
        if (get_attr(a, n, "color", v, sizeof v)) { out->color = parse_color(v); any = true; }
        if (get_attr(a, n, "size", v, sizeof v)) {
            static const u8 sz[8] = { 0, 10, 13, 16, 18, 24, 32, 48 };
            int k = katoi(v);
            if (v[0] == '+') k = 3 + katoi(v + 1);
            if (v[0] == '-') k = 3 - katoi(v + 1);
            out->size = sz[CLAMP(k, 1, 7)];
            any = true;
        }
    }
    if (get_attr(a, n, "style", v, sizeof v)) {
        // proste przejscie po deklaracjach "wlasciwosc: wartosc;"
        char *p = v;
        while (*p) {
            char *colon = strchr(p, ':');
            if (!colon) break;
            char *semi = strchr(colon, ';');
            if (semi) *semi = 0;
            *colon = 0;
            char prop[32];
            int k = 0;
            for (char *q = p; *q && k < 31; q++) if (!is_space(*q)) prop[k++] = lower(*q);
            prop[k] = 0;
            char *val = colon + 1;
            while (is_space(*val)) val++;
            for (char *q = val; *q; q++) *q = lower(*q);
            if (!strcmp(prop, "color")) { out->color = parse_color(val); any = true; }
            else if (!strcmp(prop, "text-align")) {
                if (!strncmp(val, "center", 6)) out->align = AL_CENTER;
                else if (!strncmp(val, "right", 5)) out->align = AL_RIGHT;
                any = true;
            } else if (!strcmp(prop, "font-size")) { int sz = parse_size(val); if (sz) { out->size = CLAMP(sz, 8, 90); any = true; } }
            else if (!strcmp(prop, "font-weight")) {
                out->bold = (!strncmp(val, "bold", 4) || katoi(val) >= 600) ? 1 : 0;
                any = true;
            }
            if (!semi) break;
            p = semi + 1;
        }
    }
    return any;
}

// ---------------- parser HTML ----------------
static void emit_block(page_t *p, int margin) {
    if (p->nfrags && p->frags[p->nfrags - 1].type == F_BLOCK) {
        frag_t *f = &p->frags[p->nfrags - 1];
        f->val = MAX(f->val, margin);
        return;
    }
    frag_new(p, F_BLOCK)->val = margin;
}

static void emit_text(page_t *p, pstate_t *s, const char *t, int n) {
    if (s->in_title) {
        int l = strlen(p->title);
        char buf[256];
        int dn = decode_text(t, MIN(n, 200), buf, sizeof buf);
        for (int i = 0; i < dn && l < (int)sizeof p->title - 1; i++) p->title[l++] = is_space(buf[i]) ? ' ' : buf[i];
        p->title[l] = 0;
        return;
    }
    char *buf = kmalloc(n * 2 + 8);
    int dn = decode_text(t, n, buf, n * 2 + 8);
    if (s->in_button) {
        int l = strlen(s->button_label);
        for (int i = 0; i < dn && l < 90; i++)
            if (!is_space(buf[i]) || (l && s->button_label[l - 1] != ' ')) s->button_label[l++] = is_space(buf[i]) ? ' ' : buf[i];
        s->button_label[l] = 0;
        kfree(buf);
        return;
    }
    if (!s->pre) {
        bool any = false;
        for (int i = 0; i < dn; i++) if (!is_space(buf[i])) any = true;
        if (!any) {
            if (p->nfrags && p->frags[p->nfrags - 1].type == F_TEXT) {
                frag_t *f = frag_new(p, F_TEXT);
                f->text = arena_add(p, " ", 1);
                f->len = 1;
                f->font = cur_font(s);
                f->align = cur_align(s);
            }
            kfree(buf);
            return;
        }
    }
    frag_t *f = frag_new(p, F_TEXT);
    f->text = arena_add(p, buf, dn);
    f->len = dn;
    f->font = cur_font(s);
    f->pre = s->pre > 0;
    f->color = cur_color(s);
    f->align = cur_align(s);
    f->link = s->nlink ? s->link_stack[s->nlink - 1] : -1;
    kfree(buf);
}

static int add_link(page_t *p, const char *href) {
    if (p->nlinks == p->clinks) {
        p->clinks = p->clinks ? p->clinks * 2 : 64;
        p->links = krealloc(p->links, p->clinks * sizeof(int));
    }
    p->links[p->nlinks] = arena_str(p, href);
    return p->nlinks++;
}

static int add_image(page_t *p, const char *src) {
    for (int i = 0; i < p->nimgs; i++)
        if (!strcmp(p->arena + p->imgs[i].url, src)) return i;
    if (p->nimgs >= 80) return -1;
    if (p->nimgs == p->cimgs) {
        p->cimgs = p->cimgs ? p->cimgs * 2 : 16;
        p->imgs = krealloc(p->imgs, p->cimgs * sizeof(pimg_t));
    }
    pimg_t *im = &p->imgs[p->nimgs];
    memset(im, 0, sizeof *im);
    im->url = arena_str(p, src);
    return p->nimgs++;
}

static int add_field(page_t *p, int form, int type, const char *name, const char *value) {
    if (p->nfields >= 64) return -1;
    if (!p->fields) p->fields = kzalloc(64 * sizeof(field_t));
    field_t *f = &p->fields[p->nfields];
    f->form = form;
    f->type = type;
    strlcpy(f->name, name, sizeof f->name);
    strlcpy(f->value, value, sizeof f->value);
    return p->nfields++;
}

static const char *skip_raw(const char *s, const char *end, const char *tag) {
    int tl = strlen(tag);
    for (; s + tl + 2 < end; s++)
        if (s[0] == '<' && s[1] == '/' && starts_ci(s + 2, end, tag)) {
            const char *g = memchr(s, '>', end - s);
            return g ? g + 1 : end;
        }
    return end;
}

static bool is_void(const char *n) {
    static const char *v[] = { "input", "img", "br", "hr", "meta", "link", "source", "wbr", "area", "col", "embed" };
    for (int i = 0; i < (int)ARRAY_LEN(v); i++)
        if (!strcmp(n, v[i])) return true;
    return false;
}

static bool is_hidden(const char *a, int n) {
    for (int i = 0; i < n; i++) {
        if ((i == 0 || is_space(a[i - 1])) && starts_ci(a + i, a + n, "hidden")) {
            char c = i + 6 < n ? a[i + 6] : ' ';
            if (is_space(c) || c == '=' || c == '/' || c == '>') return true;
        }
    }
    char v[256];
    if (get_attr(a, n, "aria-hidden", v, sizeof v) && !strcmp(v, "true")) return true;
    if (get_attr(a, n, "class", v, sizeof v) &&
        (strstr(v, "interlanguage") || strstr(v, "mw-portlet-lang") || strstr(v, "visually-hidden") || strstr(v, "sr-only") ||
         strstr(v, "screen-reader") || strstr(v, "skip-link")))
        return true;
    if (get_attr(a, n, "style", v, sizeof v)) {
        char *q = v, *o = v;
        for (; *q; q++) if (!is_space(*q)) *o++ = lower(*q);
        *o = 0;
        if (strstr(v, "display:none") || strstr(v, "visibility:hidden")) return true;
    }
    return false;
}

static const char *skip_element(const char *s, const char *end, const char *tag) {
    int depth = 1, tl = strlen(tag);
    while (s < end) {
        const char *lt = memchr(s, '<', end - s);
        if (!lt) return end;
        bool close = lt + 1 < end && lt[1] == '/';
        const char *nm = lt + 1 + close;
        if (starts_ci(nm, end, tag) && nm + tl < end && !((nm[tl] >= 'a' && nm[tl] <= 'z') || (nm[tl] >= '0' && nm[tl] <= '9') || nm[tl] == '-')) {
            const char *gt = memchr(nm, '>', end - nm);
            if (!gt) return end;
            if (close) { if (--depth == 0) return gt + 1; }
            else if (gt[-1] != '/') depth++;
            s = gt + 1;
        } else s = lt + 1;
    }
    return end;
}

static void handle_tag(page_t *p, pstate_t *s, const char *name, bool close, const char *attrs, int alen) {
    char a[600];
#define IS(x) (!strcmp(name, x))
    // style elementu (stos)
    if (close) {
        for (int i = s->nst - 1; i >= 0; i--)
            if (!strcmp(s->st[i].name, name)) { s->nst = i; break; }
    } else if (!is_void(name) && s->nst < 64) {
        sstyle_t st;
        if (read_style(name, attrs, alen, &st)) s->st[s->nst++] = st;
    }
    if (!close && p->main_prio < 3) {
        int m = (IS("main") || IS("article")) ? 1 : 0;
        if (get_attr(attrs, alen, "id", a, sizeof a)) {
            if (!strcmp(a, "content") || !strcmp(a, "main") || !strcmp(a, "main-content")) m = MAX(m, 2);
            if (!strcmp(a, "mw-content-text") || !strcmp(a, "readme")) m = 3;
        }
        if (m > p->main_prio) { p->main_prio = m; frag_new(p, F_MAIN); }
    }
    if (IS("body") && !close) {
        if (get_attr(attrs, alen, "bgcolor", a, sizeof a)) p->bg = parse_color(a);
        if (get_attr(attrs, alen, "text", a, sizeof a)) p->text_col = parse_color(a);
        if (get_attr(attrs, alen, "link", a, sizeof a)) p->link_col = parse_color(a);
        if (get_attr(attrs, alen, "style", a, sizeof a)) {
            char *bgp = strstr(a, "background");
            if (bgp) { char *c = strchr(bgp, ':'); if (c) { u32 col = parse_color(c + 1); if (col) p->bg = col; } }
        }
        return;
    }
    if (IS("center")) { s->center += close ? -1 : 1; if (s->center < 0) s->center = 0; emit_block(p, 2); return; }
    if (IS("b") || IS("strong") || IS("th") || IS("dt")) {
        s->bold += close ? -1 : 1;
        if (s->bold < 0) s->bold = 0;
        if (IS("th") && !close) frag_new(p, F_SPACE)->val = 16;
        if (IS("dt")) emit_block(p, 4);
        return;
    }
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) {
        emit_block(p, name[1] <= '2' ? 18 : 12);
        s->heading = close ? 0 : name[1] - '0';
        return;
    }
    if (IS("p")) { emit_block(p, 12); return; }
    if (IS("div") || IS("section") || IS("article") || IS("header") || IS("footer") || IS("nav") || IS("main") || IS("aside") ||
        IS("figure") || IS("table") || IS("dl") || IS("address") || IS("fieldset") || IS("details") ||
        IS("summary") || IS("figcaption") || IS("caption") || IS("tbody") || IS("thead")) {
        emit_block(p, 4);
        return;
    }
    if (IS("form")) {
        emit_block(p, 6);
        if (!close) {
            if (!p->forms) p->forms = kzalloc(32 * 512);
            if (p->nforms < 32) {
                get_attr(attrs, alen, "action", a, sizeof a);
                strlcpy(p->forms[p->nforms], a, 512);
                s->cur_form = p->nforms++;
            }
        } else s->cur_form = -1;
        return;
    }
    if (IS("br")) { frag_new(p, F_BR); return; }
    if (IS("hr")) { emit_block(p, 8); frag_new(p, F_HR); emit_block(p, 8); return; }
    if (IS("ul") || IS("ol") || IS("menu")) {
        emit_block(p, s->nlist ? 2 : 10);
        if (!close && s->nlist < 16) {
            s->list_type[s->nlist] = IS("ol");
            s->list_n[s->nlist] = 0;
            s->nlist++;
            frag_new(p, F_INDENT)->val = 28;
        } else if (close && s->nlist) {
            s->nlist--;
            frag_new(p, F_INDENT)->val = -28;
        }
        return;
    }
    if (IS("li")) {
        if (close) return;
        emit_block(p, 4);
        frag_t *f = frag_new(p, F_LI);
        char b[16];
        if (s->nlist && s->list_type[s->nlist - 1]) snprintf(b, sizeof b, "%d.", ++s->list_n[s->nlist - 1]);
        else strlcpy(b, s->nlist > 1 ? "–" : "•", sizeof b);
        f->text = arena_add(p, b, strlen(b));
        f->len = strlen(b);
        return;
    }
    if (IS("blockquote") || IS("dd")) {
        emit_block(p, IS("dd") ? 2 : 10);
        frag_new(p, F_INDENT)->val = close ? -28 : 28;
        return;
    }
    if (IS("pre")) {
        emit_block(p, 10);
        s->pre += close ? -1 : 1;
        if (s->pre < 0) s->pre = 0;
        return;
    }
    if (IS("code") || IS("tt") || IS("kbd") || IS("samp")) { s->mono += close ? -1 : 1; if (s->mono < 0) s->mono = 0; return; }
    if (IS("small") || IS("sup") || IS("sub")) { s->small += close ? -1 : 1; if (s->small < 0) s->small = 0; return; }
    if (IS("a")) {
        if (close) { if (s->nlink) s->nlink--; return; }
        int idx = -1;
        if (get_attr(attrs, alen, "href", a, sizeof a) && a[0]) idx = add_link(p, a);
        if (s->nlink < 32) s->link_stack[s->nlink++] = idx;
        return;
    }
    if (IS("img") && !close) {
        char src[600], alt[256], w[16], h[16];
        get_attr(attrs, alen, "src", src, sizeof src);
        if ((!src[0] || !strncmp(src, "data:image/gif", 14) || !strncmp(src, "data:image/svg", 14)) && !get_attr(attrs, alen, "data-src", src, sizeof src))
            src[0] = 0;
        get_attr(attrs, alen, "alt", alt, sizeof alt);
        get_attr(attrs, alen, "width", w, sizeof w);
        get_attr(attrs, alen, "height", h, sizeof h);
        int iw = katoi(w), ih = katoi(h);
        if ((iw && iw <= 2) || (ih && ih <= 2)) return;      // piksele sledzace
        bool svg = str_ends_with_ci(src, ".svg") || strstr(src, ".svg?");
        frag_t *f = frag_new(p, F_IMG);
        f->val = src[0] && !svg ? add_image(p, src) : -1;
        f->text = arena_add(p, alt, strlen(alt));
        f->len = strlen(alt);
        f->w = iw;
        f->h = ih;
        f->align = cur_align(s);
        f->link = s->nlink ? s->link_stack[s->nlink - 1] : -1;
        return;
    }
    if (IS("tr")) { if (!close) frag_new(p, F_BR); return; }
    if (IS("td")) { if (!close) frag_new(p, F_SPACE)->val = 16; return; }
    if (IS("title")) { s->in_title = !close; return; }
    if (IS("input") && !close) {
        char type[32], nm[64], val[256], sz[16];
        get_attr(attrs, alen, "type", type, sizeof type);
        get_attr(attrs, alen, "name", nm, sizeof nm);
        get_attr(attrs, alen, "value", val, sizeof val);
        get_attr(attrs, alen, "size", sz, sizeof sz);
        for (char *c = type; *c; c++) *c = lower(*c);
        if (!strcmp(type, "hidden")) { add_field(p, s->cur_form, 1, nm, val); return; }
        if (!strcmp(type, "submit") || !strcmp(type, "button") || !strcmp(type, "image")) {
            int fi = add_field(p, s->cur_form, 2, nm, val[0] ? val : "Wyslij");
            if (fi < 0) return;
            frag_t *f = frag_new(p, F_BUTTON);
            f->val = fi;
            f->align = cur_align(s);
            return;
        }
        if (!strcmp(type, "checkbox") || !strcmp(type, "radio")) {
            if (s->cur_form < 0) return;
            frag_t *f = frag_new(p, F_TEXT);
            f->text = arena_add(p, "[ ] ", 4);
            f->len = 4;
            f->font = FONT_WEB_MONO;
            return;
        }
        int fi = add_field(p, s->cur_form, 0, nm, val);
        if (fi < 0) return;
        frag_t *f = frag_new(p, F_INPUT);
        f->val = fi;
        f->w = katoi(sz) > 0 ? katoi(sz) * 9 : 280;
        f->align = cur_align(s);
        return;
    }
    if (IS("button")) {
        if (!close) { s->in_button = true; s->button_label[0] = 0; return; }
        s->in_button = false;
        if (!s->button_label[0] && s->cur_form < 0) return;
        int fi = add_field(p, s->cur_form, 2, "", s->button_label[0] ? s->button_label : "Wyslij");
        if (fi >= 0) { frag_t *f = frag_new(p, F_BUTTON); f->val = fi; f->align = cur_align(s); }
        return;
    }
#undef IS
}

static page_t *parse_html(const char *html, size_t n, bool plain) {
    page_t *p = kzalloc(sizeof *p);
    p->bg = RGB(255, 255, 255);
    if (plain) {
        frag_t *f = frag_new(p, F_TEXT);
        f->text = arena_add(p, html, n);
        f->len = n;
        f->font = FONT_WEB_MONO;
        f->pre = 1;
        return p;
    }
    pstate_t *st = kzalloc(sizeof(pstate_t));
    st->cur_form = -1;
    const char *s = html, *end = html + n;
    while (s < end) {
        if (*s == '<') {
            if (starts_ci(s, end, "<!--")) {
                const char *e = strstr(s + 4, "-->");
                s = e ? e + 3 : end;
                continue;
            }
            if (s + 1 < end && (s[1] == '!' || s[1] == '?')) {
                const char *e = memchr(s, '>', end - s);
                s = e ? e + 1 : end;
                continue;
            }
            const char *q = s + 1;
            bool close = false;
            if (q < end && *q == '/') { close = true; q++; }
            if (q >= end || !((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'))) {
                emit_text(p, st, s, 1);
                s++;
                continue;
            }
            char name[16];
            int nl = 0;
            while (q < end && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '-')) {
                if (nl < 15) name[nl++] = lower(*q);
                q++;
            }
            name[nl] = 0;
            const char *as = q;
            char quote = 0;
            while (q < end && (quote || *q != '>')) {
                if (quote) { if (*q == quote) quote = 0; }
                else if (*q == '"' || *q == '\'') quote = *q;
                q++;
            }
            int alen = q - as;
            s = q < end ? q + 1 : end;
            if (!close && (!strcmp(name, "script") || !strcmp(name, "style") || !strcmp(name, "svg") ||
                           !strcmp(name, "template") || !strcmp(name, "iframe") || !strcmp(name, "object") ||
                           !strcmp(name, "canvas") || !strcmp(name, "select") || !strcmp(name, "textarea") ||
                           !strcmp(name, "math") || !strcmp(name, "video") || !strcmp(name, "audio"))) {
                if (!strcmp(name, "textarea")) {
                    int fi = add_field(p, st->cur_form, 0, "", "");
                    if (fi >= 0) { frag_t *f = frag_new(p, F_INPUT); f->val = fi; f->w = 400; }
                }
                if (!alen || as[alen - 1] != '/') s = skip_raw(s, end, name);
                continue;
            }
            if (!close && is_hidden(as, alen) && !is_void(name)) {
                if (!alen || as[alen - 1] != '/') s = skip_element(s, end, name);
                continue;
            }
            handle_tag(p, st, name, close, as, alen);
        } else {
            const char *e = memchr(s, '<', end - s);
            if (!e) e = end;
            emit_text(p, st, s, e - s);
            s = e;
        }
    }
    kfree(st);
    char *t = p->title;
    while (*t == ' ') t++;
    memmove(p->title, t, strlen(t) + 1);
    for (int l = strlen(p->title); l && p->title[l - 1] == ' '; l--) p->title[l - 1] = 0;
    return p;
}

// ---------------- ukladanie ----------------
static box_t *box_new(page_t *p) {
    if (p->nboxes == p->cboxes) {
        p->cboxes = p->cboxes ? p->cboxes * 2 : 512;
        p->boxes = krealloc(p->boxes, p->cboxes * sizeof(box_t));
    }
    box_t *b = &p->boxes[p->nboxes++];
    memset(b, 0, sizeof *b);
    b->link = -1;
    return b;
}

typedef struct {
    page_t *p;
    int left, right, x, y, lineh, line_start;
    bool pending_space, line_empty;
    u8 align;
} lay_t;

static void lay_newline(lay_t *L, int min_h) {
    int h = L->lineh ? L->lineh : min_h;
    int shift = 0;
    if (L->align == AL_CENTER) shift = MAX(0, (L->right - L->x) / 2);
    else if (L->align == AL_RIGHT) shift = MAX(0, L->right - L->x);
    for (int i = L->line_start; i < L->p->nboxes; i++) {
        box_t *b = &L->p->boxes[i];
        b->y += h - b->h - 4;
        if (b->y < L->y) b->y = L->y;
        if (b->kind != B_HR) b->x += shift;
    }
    L->y += h;
    L->x = L->left;
    L->lineh = 0;
    L->line_start = L->p->nboxes;
    L->pending_space = false;
    L->line_empty = true;
    L->align = AL_LEFT;
}

static void lay_inline(lay_t *L, int w, int h, u8 align) {
    int sw = L->pending_space && !L->line_empty ? 5 : 0;
    if (!L->line_empty && L->x + sw + w > L->right) { lay_newline(L, 20); sw = 0; }
    if (L->line_empty) L->align = align;
    L->x += sw;
    L->pending_space = false;
    L->lineh = MAX(L->lineh, h + 6);
    L->line_empty = false;
}

static void img_size(page_t *p, frag_t *f, int maxw, int *w, int *h) {
    image_t *im = f->val >= 0 && p->imgs[f->val].state == 1 ? p->imgs[f->val].img : NULL;
    int iw = f->w, ih = f->h;
    if (im) {
        if (!iw && !ih) { iw = im->w; ih = im->h; }
        else if (!ih) ih = im->h * iw / MAX(1, im->w);
        else if (!iw) iw = im->w * ih / MAX(1, im->h);
    }
    if (iw > maxw && iw) { ih = ih * maxw / iw; iw = maxw; }
    *w = iw;
    *h = ih;
}

static void layout(page_t *p, int width) {
    p->nboxes = 0;
    lay_t L = { p, MARGIN, MAX(width - MARGIN, MARGIN + 100), MARGIN, 18, 0, 0, false, true, AL_LEFT };
    int last_margin = 0;
    p->main_y = 0;
    for (int i = 0; i < p->nfrags; i++) {
        frag_t *f = &p->frags[i];
        switch (f->type) {
        case F_TEXT: {
            int fh = gfx_font_height(f->font);
            const char *t = p->arena + f->text;
            if (f->pre) {
                int a = 0;
                for (int k = 0; k <= f->len; k++) {
                    if (k == f->len || t[k] == '\n') {
                        if (k > a) {
                            box_t *b = box_new(p);
                            b->kind = B_TEXT, b->font = f->font, b->text = f->text + a, b->len = k - a, b->color = f->color;
                            b->x = L.x, b->y = L.y, b->w = gfx_text_width_n(f->font, t + a, k - a), b->h = fh;
                            b->link = f->link;
                            L.x += b->w;
                            L.lineh = MAX(L.lineh, fh + 4);
                            L.line_empty = false;
                        }
                        if (k < f->len) lay_newline(&L, fh + 4);
                        a = k + 1;
                    }
                }
                last_margin = 0;
                break;
            }
            int k = 0;
            int sp = gfx_char_width(f->font, ' ');
            while (k < f->len) {
                if (is_space(t[k])) { L.pending_space = true; k++; continue; }
                int a = k;
                while (k < f->len && !is_space(t[k])) k++;
                int w = gfx_text_width_n(f->font, t + a, k - a);
                int gap = L.pending_space && !L.line_empty ? sp : 0;
                if (!L.line_empty && L.x + gap + w > L.right) { lay_newline(&L, fh + 7); gap = 0; }
                if (L.line_empty) L.align = f->align;
                L.x += gap;
                L.pending_space = false;
                box_t *b = box_new(p);
                b->kind = B_TEXT, b->font = f->font, b->text = f->text + a, b->len = k - a, b->color = f->color;
                b->x = L.x, b->y = L.y, b->w = w, b->h = fh;
                b->link = f->link;
                L.x += w;
                L.lineh = MAX(L.lineh, fh + 7);
                L.line_empty = false;
            }
            last_margin = 0;
            break;
        }
        case F_SPACE:
            if (!L.line_empty) L.x += f->val;
            break;
        case F_MAIN:
            p->main_y = L.y;
            break;
        case F_BR:
            lay_newline(&L, 20);
            break;
        case F_BLOCK:
            if (!L.line_empty) lay_newline(&L, 20);
            if (f->val > last_margin) L.y += f->val - last_margin;
            last_margin = MAX(last_margin, f->val);
            break;
        case F_HR: {
            box_t *b = box_new(p);
            b->kind = B_HR, b->x = L.left, b->y = L.y + 4, b->w = L.right - L.left, b->h = 1;
            L.y += 10;
            L.line_start = p->nboxes;
            break;
        }
        case F_INDENT:
            L.left = CLAMP(L.left + f->val, MARGIN, L.right - 100);
            if (L.line_empty) L.x = L.left;
            break;
        case F_LI: {
            box_t *b = box_new(p);
            const char *t = p->arena + f->text;
            b->kind = B_TEXT, b->font = FONT_WEB, b->text = f->text, b->len = f->len, b->color = RGB(110, 118, 128);
            b->w = gfx_text_width_n(FONT_WEB, t, f->len);
            b->x = L.left - b->w - 9, b->y = L.y, b->h = gfx_font_height(FONT_WEB);
            break;
        }
        case F_IMG: {
            int iw, ih;
            img_size(p, f, L.right - L.left, &iw, &ih);
            bool loaded = f->val >= 0 && p->imgs[f->val].state == 1;
            if (loaded || (iw > 0 && ih > 0 && f->val >= 0 && p->imgs[f->val].state == 0)) {
                if (iw <= 0 || ih <= 0) break;
                lay_inline(&L, iw, ih, f->align);
                box_t *b = box_new(p);
                b->kind = B_IMG, b->x = L.x, b->y = L.y, b->w = iw, b->h = ih, b->val = f->val, b->link = f->link;
                b->text = f->text, b->len = f->len;
                L.x += iw;
            } else if (f->len) {
                const char *t = p->arena + f->text;
                int w = MIN(gfx_text_width_n(FONT_WEB_SMALL, t, f->len) + 34, L.right - L.left);
                lay_inline(&L, w, 24, f->align);
                box_t *b = box_new(p);
                b->kind = B_ALT, b->x = L.x, b->y = L.y, b->w = w, b->h = 24, b->text = f->text, b->len = f->len, b->link = f->link;
                L.x += w;
            } else break;
            L.pending_space = true;
            last_margin = 0;
            break;
        }
        case F_INPUT: {
            int w = MIN(f->w ? f->w : 280, L.right - L.left);
            lay_inline(&L, w, 36, f->align);
            box_t *b = box_new(p);
            b->kind = B_INPUT, b->x = L.x, b->y = L.y, b->w = w, b->h = 36, b->val = f->val;
            L.x += w;
            L.pending_space = true;
            last_margin = 0;
            break;
        }
        case F_BUTTON: {
            field_t *fl = &p->fields[f->val];
            int w = gfx_text_width(FONT_WEB, fl->value) + 32;
            lay_inline(&L, w, 36, f->align);
            box_t *b = box_new(p);
            b->kind = B_BUTTON, b->x = L.x, b->y = L.y, b->w = w, b->h = 36, b->val = f->val;
            L.x += w;
            L.pending_space = true;
            last_margin = 0;
            break;
        }
        }
    }
    if (!L.line_empty) lay_newline(&L, 20);
    p->doc_h = L.y + MARGIN;
    p->laid_w = width;
}

// ---------------- karty i nawigacja ----------------
static window_t *browsers[8];

static bool br_alive(browser_t *b) {
    for (int i = 0; i < 8; i++)
        if (browsers[i] && browsers[i]->data == b) return true;
    return false;
}

static bool tab_alive(browser_t *b, tab_t *t) {
    for (int i = 0; i < b->ntabs; i++)
        if (b->tabs[i] == t) return true;
    return false;
}

static tab_t *cur_tab(browser_t *b) { return b->tabs[b->cur]; }

static void update_title(browser_t *b) {
    tab_t *t = cur_tab(b);
    char s[200];
    snprintf(s, sizeof s, "%s - Przegladarka Sartax", t->pg && t->pg->title[0] ? t->pg->title : "Nowa karta");
    wm_set_title(b->win, s);
}

static void set_page(browser_t *b, tab_t *t, page_t *p, const char *url) {
    page_free(t->pg);
    t->pg = p;
    t->scroll = 0;
    t->focus_field = -1;
    t->jump_main = p->main_prio > 0;
    t->img_next = 0;
    strlcpy(t->url, url, sizeof t->url);
    strlcpy(t->addr, !strncmp(url, "sartax:start", 12) ? "" : url, sizeof t->addr);
    if (!strncmp(url, "sartax:szukaj", 13)) url_param(url, "q", t->addr, sizeof t->addr);
    update_title(b);
}

static void error_page(browser_t *b, tab_t *t, const char *url, const char *err) {
    char *h = kmalloc(3000);
    char e1[300], e2[600];
    int o = 0;
    for (const char *s = err; *s && o < 290; s++) { if (*s == '<') { strlcpy(e1 + o, "&lt;", 6); o += 4; } else e1[o++] = *s; }
    e1[o] = 0;
    o = 0;
    for (const char *s = url; *s && o < 590; s++) { if (*s == '<' || *s == '>') continue; e2[o++] = *s; }
    e2[o] = 0;
    snprintf(h, 3000, "<title>Nie mozna otworzyc strony</title><br><br><center><font size=\"6\" color=\"#5f6368\">:(</font><br><br>"
                      "<h2>Nie mozna otworzyc tej strony</h2><font color=\"#5f6368\">%s</font><br><br>"
                      "<font size=\"2\" color=\"#80868b\">%s</font><br><br>"
                      "<font color=\"#5f6368\">Sprawdz adres i polaczenie z internetem. Niektore strony wymagaja TLS 1.3 albo JavaScriptu, "
                      "ktorych Przegladarka Sartax jeszcze nie obsluguje.</font><br><br><a href=\"sartax:start\">Nowa karta</a></center>",
             e1, e2);
    page_t *p = parse_html(h, strlen(h), false);
    kfree(h);
    set_page(b, t, p, url);
}

static void prog_cb(void *ctx, size_t done, size_t total) { busy_update("Wczytywanie strony...", done, total); }

static void save_download(browser_t *b, const char *url, const u8 *d, size_t n) {
    const char *q = strrchr(url, '/');
    char name[96];
    strlcpy(name, q && q[1] ? q + 1 : "plik", sizeof name);
    char *qq = strchr(name, '?');
    if (qq) *qq = 0;
    char dec[96];
    url_decode(name, strlen(name), dec, sizeof dec);
    if (!path_valid_name(dec)) strlcpy(dec, "pobrany_plik", sizeof dec);
    if (!fs_exists("/Pobrane")) fs_mkdir("/Pobrane");
    char path[256];
    path_join(path, sizeof path, "/Pobrane", dec);
    int r = fs_write_file(path, d, n);
    char msg[320], sz[32];
    fmt_size(sz, sizeof sz, n);
    if (r) snprintf(msg, sizeof msg, "Nie udalo sie zapisac pliku: %s", fs_error(r));
    else snprintf(msg, sizeof msg, "Pobrano plik %s (%s).", path, sz);
    dlg_message(b->win, "Pobieranie", msg, r ? ICON_WARNING : ICON_DOWNLOAD);
    explorer_refresh_all();
}

// --- wyszukiwarka: wyniki DuckDuckGo w wygladzie Sartax ---
static void html_escape(const char *s, char *out, int cap) {
    int o = 0;
    for (; *s && o < cap - 7; s++) {
        if (*s == '<') { memcpy(out + o, "&lt;", 4); o += 4; }
        else if (*s == '>') { memcpy(out + o, "&gt;", 4); o += 4; }
        else if (*s == '&') { memcpy(out + o, "&amp;", 5); o += 5; }
        else if (*s == '"') { memcpy(out + o, "&quot;", 6); o += 6; }
        else out[o++] = *s;
    }
    out[o] = 0;
}

// tekst elementu od 'p' (po '>') do '</a>' bez znacznikow
static void inner_text(const char *p, const char *end, char *out, int cap) {
    char raw[1200];
    int o = 0;
    bool tag = false;
    for (; p < end && o < (int)sizeof raw - 1; p++) {
        if (!tag && p[0] == '<' && p + 3 < end && p[1] == '/' && p[2] == 'a' && p[3] == '>') break;
        if (*p == '<') tag = true;
        else if (*p == '>') tag = false;
        else if (!tag) raw[o++] = is_space(*p) ? ' ' : *p;
    }
    raw[o] = 0;
    char dec[1200];
    decode_text(raw, o, dec, sizeof dec);
    // przytnij spacje
    char *s = dec;
    while (*s == ' ') s++;
    int l = strlen(s);
    while (l && s[l - 1] == ' ') s[--l] = 0;
    strlcpy(out, s, cap);
}

typedef struct { char url[600], title[200], snippet[600], disp[200]; } sresult_t;

static int parse_ddg(const char *h, size_t n, sresult_t *res, int max) {
    int c = 0;
    const char *end = h + n, *p = h;
    while (c < max) {
        const char *a = strstr(p, "class=\"result__a\"");
        if (!a) break;
        const char *next = strstr(a + 10, "class=\"result__a\"");
        if (!next) next = end;
        sresult_t *r = &res[c];
        memset(r, 0, sizeof *r);
        const char *gt = memchr(a, '>', end - a);
        if (!gt) break;
        char href[800];
        get_attr(a, gt - a, "href", href, sizeof href);
        p = gt + 1;
        if (!url_param(href, "uddg", r->url, sizeof r->url) || strstr(r->url, "duckduckgo.com/y.js")) continue;
        inner_text(gt + 1, next, r->title, sizeof r->title);
        const char *sn = strstr(gt, "class=\"result__snippet\"");
        if (sn && sn < next) { const char *g2 = memchr(sn, '>', end - sn); if (g2) inner_text(g2 + 1, next, r->snippet, sizeof r->snippet); }
        const char *du = strstr(gt, "class=\"result__url\"");
        if (du && du < next) { const char *g3 = memchr(du, '>', end - du); if (g3) inner_text(g3 + 1, next, r->disp, sizeof r->disp); }
        if (!r->disp[0]) strlcpy(r->disp, r->url, sizeof r->disp);
        c++;
    }
    return c;
}

static void search_page(browser_t *b, tab_t *t, const char *url) {
    char q[300], qs[16], lucky[16];
    url_param(url, "q", q, sizeof q);
    url_param(url, "s", qs, sizeof qs);
    url_param(url, "traf", lucky, sizeof lucky);
    int start = katoi(qs);
    char enc[600], u[900];
    url_encode(q, enc, sizeof enc);
    if (start) snprintf(u, sizeof u, SEARCH_URL "%s&s=%d&dc=%d", enc, start, start + 1);
    else snprintf(u, sizeof u, SEARCH_URL "%s", enc);
    busy_begin("Szukanie");
    http_resp_t r;
    int rc = http_get(u, &r, prog_cb, NULL);
    busy_end();
    if (!br_alive(b) || !tab_alive(b, t)) { http_free(&r); return; }
    if (rc) { error_page(b, t, url, r.error); http_free(&r); return; }
    sresult_t *res = kmalloc(sizeof(sresult_t) * 30);
    int n = parse_ddg((char *)r.body, r.len, res, 30);
    http_free(&r);
    if (lucky[0] && n) {
        char first[600];
        strlcpy(first, res[0].url, sizeof first);
        kfree(res);
        strlcpy(b->pending, first, sizeof b->pending);
        b->pending_record = false;
        b->pend_tab = t;

        ui_defer(browser_load_pending, b);
        return;
    }
    size_t cap = 4096 + n * 2400;
    char *h = kmalloc(cap);
    char qe[700];
    html_escape(q, qe, sizeof qe);
    size_t o = snprintf(h, cap,
        "<title>%s - Szukaj w Sartax</title>"
        "<form action=\"sartax:szukaj\"><font size=\"5\"><a href=\"sartax:start\">%s</a></font> &nbsp;&nbsp; "
        "<input type=\"text\" name=\"q\" value=\"%s\" size=\"56\"> <input type=\"submit\" value=\"Szukaj\"></form><hr>"
        "<font size=\"2\" color=\"#70757a\">%s</font><br>",
        qe, LOGO_HTML, qe, n ? "Wyniki wyszukiwania" : "Brak wynikow - sprobuj innych slow.");
    for (int i = 0; i < n && o < cap - 2400; i++) {
        char t1[400], t2[1200], t3[400], t4[1200];
        html_escape(res[i].title, t1, sizeof t1);
        html_escape(res[i].snippet, t2, sizeof t2);
        html_escape(res[i].disp, t3, sizeof t3);
        html_escape(res[i].url, t4, sizeof t4);
        o += snprintf(h + o, cap - o,
                      "<p><font size=\"2\" color=\"#202124\">%s</font><br><a href=\"%s\"><font size=\"4\">%s</font></a><br>"
                      "<font color=\"#4d5156\">%s</font></p>",
                      t3, t4, t1, t2);
    }
    if (n >= 8) o += snprintf(h + o, cap - o, "<br><center><a href=\"sartax:szukaj?q=%s&s=%d\"><font size=\"4\">Nastepna strona wynikow &rarr;</font></a></center>", enc, start + n);
    o += snprintf(h + o, cap - o, "<br><center><font size=\"2\" color=\"#80868b\">Wyniki: DuckDuckGo</font></center>");
    kfree(res);
    page_t *p = parse_html(h, o, false);
    kfree(h);
    set_page(b, t, p, url);
}

static void load_url(browser_t *b, tab_t *t, const char *url, bool record) {
    if (record) {
        if (t->hpos < t->nhist - 1) t->nhist = t->hpos + 1;
        if (t->nhist == 24) { memmove(t->hist[0], t->hist[1], 23 * 512); t->nhist--; }
        strlcpy(t->hist[t->nhist++], url, 512);
        t->hpos = t->nhist - 1;
    }
    if (!strncmp(url, "sartax:start", 12)) {
        char *h = start_page_html();
        set_page(b, t, parse_html(h, strlen(h), false), "sartax:start");
        kfree(h);
        return;
    }
    if (!strncmp(url, "sartax:szukaj", 13)) { search_page(b, t, url); return; }
    if (!strncmp(url, "plik:", 5)) {
        u8 *d;
        u64 n;
        int r = fs_read_file(url + 5, &d, &n);
        if (r) { error_page(b, t, url, fs_error(r)); return; }
        bool html = str_ends_with_ci(url, ".html") || str_ends_with_ci(url, ".htm");
        char *u = to_utf8(d, n, "");
        kfree(d);
        set_page(b, t, parse_html(u, strlen(u), !html), url);
        kfree(u);
        return;
    }
    busy_begin("Wczytywanie strony");
    http_resp_t r;
    int rc = http_get(url, &r, prog_cb, NULL);
    busy_end();
    if (!br_alive(b) || !tab_alive(b, t)) { http_free(&r); return; }
    const char *final = r.final_url[0] ? r.final_url : url;
    if (rc) { error_page(b, t, final, r.error); http_free(&r); return; }
    char ct[64];
    int i = 0;
    for (; r.ctype[i] && i < 63; i++) ct[i] = lower(r.ctype[i]);
    ct[i] = 0;
    bool html = strstr(ct, "html") != NULL;
    bool text = !strncmp(ct, "text/", 5) || strstr(ct, "json") || strstr(ct, "xml");
    if (!strncmp(ct, "image/", 6)) {
        // sam obrazek - pokaz go na stronie
        image_t *im = image_load_any(r.body, r.len);
        http_free(&r);
        if (!im) { error_page(b, t, final, "Nieobslugiwany format obrazka"); return; }
        page_t *p = kzalloc(sizeof *p);
        p->bg = RGB(40, 44, 50);
        int idx = add_image(p, final);
        p->imgs[idx].img = im;
        p->imgs[idx].state = 1;
        frag_t *f = frag_new(p, F_IMG);
        f->val = idx;
        f->align = AL_CENTER;
        snprintf(p->title, sizeof p->title, "Obraz %dx%d", im->w, im->h);
        set_page(b, t, p, final);
        return;
    }
    if (!ct[0]) {
        size_t k = 0;
        while (k < r.len && is_space(r.body[k])) k++;
        html = k < r.len && r.body[k] == '<';
        text = true;
    }
    if (!html && !text) {
        save_download(b, final, r.body, r.len);
        http_free(&r);
        return;
    }
    char *u = to_utf8(r.body, r.len, r.ctype);
    http_free(&r);
    page_t *p = parse_html(u, strlen(u), !html);
    kfree(u);
    if (record && t->nhist) strlcpy(t->hist[t->hpos], final, 512);
    set_page(b, t, p, final);
}

void browser_load_pending(void *ctx) {
    browser_t *b = ctx;
    if (!br_alive(b) || !tab_alive(b, b->pend_tab)) return;
    char url[600];
    strlcpy(url, b->pending, sizeof url);
    load_url(b, b->pend_tab, url, b->pending_record);
}

static void navigate(browser_t *b, tab_t *t, const char *url, bool record) {
    strlcpy(b->pending, url, sizeof b->pending);
    b->pending_record = record;
    b->pend_tab = t;
    ui_defer(browser_load_pending, b);
}

static void go_address(browser_t *b, const char *text) {
    while (*text == ' ') text++;
    tab_t *t = cur_tab(b);
    if (!*text) { navigate(b, t, "sartax:start", true); return; }
    char u[600];
    if (strstr(text, "://") || !strncmp(text, "sartax:", 7) || !strncmp(text, "plik:", 5)) strlcpy(u, text, sizeof u);
    else if (text[0] == '/') snprintf(u, sizeof u, "plik:%s", text);
    else if (strchr(text, '.') && !strchr(text, ' ')) snprintf(u, sizeof u, "https://%s", text);
    else {
        char q[400];
        url_encode(text, q, sizeof q);
        snprintf(u, sizeof u, "sartax:szukaj?q=%s", q);
    }
    navigate(b, t, u, true);
}

static void submit_form(browser_t *b, int field) {
    tab_t *t = cur_tab(b);
    page_t *p = t->pg;
    int form = p->fields[field].form;
    char action[512] = "";
    if (form >= 0) strlcpy(action, p->forms[form], sizeof action);
    char base[600];
    if (!action[0]) strlcpy(base, t->url, sizeof base);
    else if (!resolve_url(t->url, action, base, sizeof base)) return;
    char *q = strchr(base, '?');
    if (q) *q = 0;
    char url[1200];
    snprintf(url, sizeof url, "%s?", base);
    bool first = true;
    for (int i = 0; i < p->nfields; i++) {
        field_t *f = &p->fields[i];
        if (f->form != form || !f->name[0]) continue;
        if (f->type == 2 && i != field) continue;
        char enc[600];
        url_encode(f->value, enc, sizeof enc);
        if (!first) strlcat(url, "&", sizeof url);
        strlcat(url, f->name, sizeof url);
        strlcat(url, "=", sizeof url);
        strlcat(url, enc, sizeof url);
        first = false;
    }
    navigate(b, t, url, true);
}

static tab_t *tab_new(browser_t *b, const char *url) {
    if (b->ntabs >= 8) return NULL;
    tab_t *t = kzalloc(sizeof *t);
    t->focus_field = -1;
    b->tabs[b->ntabs++] = t;
    b->cur = b->ntabs - 1;
    navigate(b, t, url ? url : "sartax:start", true);
    return t;
}

static void tab_close(browser_t *b, int i) {
    if (b->ntabs == 1) { wm_close(b->win); return; }
    page_free(b->tabs[i]->pg);
    kfree(b->tabs[i]);
    memmove(&b->tabs[i], &b->tabs[i + 1], (b->ntabs - i - 1) * sizeof(tab_t *));
    b->ntabs--;
    if (b->cur >= b->ntabs) b->cur = b->ntabs - 1;
    else if (b->cur > i) b->cur--;
    update_title(b);
}

// ---------------- obrazki (po jednym na petle) ----------------
static bool load_one_image(browser_t *b) {
    tab_t *t = cur_tab(b);
    page_t *p = t->pg;
    if (!p || t->img_next >= p->nimgs || busy_cancelled()) return false;
    pimg_t *im = &p->imgs[t->img_next++];
    if (im->state) return true;
    const char *src = p->arena + im->url;
    char url[900];
    if (!resolve_url(t->url, src, url, sizeof url)) { im->state = 2; return true; }
    snprintf(b->status, sizeof b->status, "Wczytywanie obrazkow %d z %d...", t->img_next, p->nimgs);
    wm_render();
    u8 *data = NULL;
    size_t len = 0;
    http_resp_t r;
    memset(&r, 0, sizeof r);
    if (!strncmp(url, "data:", 5)) {
        const char *comma = strchr(url, ',');
        if (comma && strstr(url, ";base64")) {
            const char *s = comma + 1;
            size_t n = strlen(s);
            data = kmalloc(n);
            u32 acc = 0;
            int bits = 0;
            for (size_t i = 0; i < n; i++) {
                char c = s[i];
                int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
                        c == '+' ? 62 : c == '/' ? 63 : -1;
                if (v < 0) continue;
                acc = (acc << 6) | v;
                bits += 6;
                if (bits >= 8) { bits -= 8; data[len++] = acc >> bits; }
            }
        }
    } else if (!strncmp(url, "http", 4)) {
        if (http_get(url, &r, NULL, NULL) == 0) { data = r.body; len = r.len; r.body = NULL; }
    } else if (!strncmp(url, "plik:", 5)) {
        u64 n;
        if (fs_read_file(url + 5, &data, &n) == 0) len = n;
    }
    http_free(&r);
    if (!br_alive(b) || cur_tab(b)->pg != p) { kfree(data); return true; }
    im->img = data ? image_load_any(data, len) : NULL;
    kfree(data);
    im->state = im->img ? 1 : 2;
    if (im->img && (long)im->img->w * im->img->h <= 4) { image_free(im->img); im->img = NULL; im->state = 2; }
    p->laid_w = 0;      // przeloz strone
    if (t->img_next >= p->nimgs) b->status[0] = 0;
    return true;
}

// ---------------- widok ----------------
static u64 addr_id(browser_t *b) { return (u64)(uintptr_t)b * 7 + 3; }

static void draw_tabs(browser_t *b, surface_t *s, int W) {
    gfx_fill(s, 0, 0, W, TABS_H, RGB(222, 227, 233));
    int avail = W - 60;
    int tw = MIN(230, avail / MAX(1, b->ntabs));
    for (int i = 0; i < b->ntabs; i++) {
        tab_t *t = b->tabs[i];
        int x = 8 + i * tw, y = 6;
        bool act = i == b->cur;
        bool hov = ui_hit(x, y, tw - 2, TABS_H - y);
        if (act) {
            gfx_round_rect(s, x, y, tw - 2, TABS_H - y + 10, 10, RGB(255, 255, 255));
        } else {
            if (hov) gfx_round_rect(s, x + 2, y + 2, tw - 6, TABS_H - y - 6, 8, RGB(236, 239, 243));
            if (i + 1 != b->cur && i + 1 < b->ntabs) gfx_fill(s, x + tw - 2, y + 8, 1, 18, RGB(180, 188, 196));
        }
        gfx_blit(s, x + 12, y + 9, icon_get(ICON_GLOBE, 16));
        const char *title = t->pg && t->pg->title[0] ? t->pg->title : (t->url[0] && strncmp(t->url, "sartax:start", 12) ? t->url : "Nowa karta");
        int sx0 = s->cx0, sx1 = s->cx1, sy0 = s->cy0, sy1 = s->cy1;
        gfx_set_clip(s, x, 0, tw - 30, TABS_H);
        gfx_text_ellipsis(s, x + 34, y + 9, tw - 70, FONT_UI, title, act ? TEXT_COL : RGB(80, 86, 94));
        s->cx0 = sx0, s->cx1 = sx1, s->cy0 = sy0, s->cy1 = sy1;
        // zamknij karte
        int cx = x + tw - 26, cy = y + 8;
        bool ch = ui_hit(cx, cy, 18, 18);
        if (ch) gfx_circle(s, cx + 9, cy + 9, 9, RGB(214, 219, 225));
        for (int k = -3; k <= 3; k++) {
            gfx_pixel(s, cx + 9 + k, cy + 9 + k, RGB(90, 96, 104));
            gfx_pixel(s, cx + 9 + k, cy + 9 - k, RGB(90, 96, 104));
        }
        if (ui.mpressed && ch) { tab_close(b, i); return; }
        if (ui.mpressed && hov) { b->cur = i; ui_focus_clear(); update_title(b); }
    }
    // nowa karta
    int px = 8 + b->ntabs * tw + 6;
    if (b->ntabs < 8) {
        bool ph = ui_hit(px, 10, 28, 28);
        if (ph) gfx_circle(s, px + 14, 24, 14, RGB(236, 239, 243));
        gfx_fill(s, px + 8, 23, 13, 2, RGB(80, 86, 94));
        gfx_fill(s, px + 13, 18, 2, 13, RGB(80, 86, 94));
        if (ui.mpressed && ph) tab_new(b, NULL);
    }
}

static void round_icon_button(surface_t *s, int x, int y, int icon, bool enabled, bool *clicked) {
    bool hov = enabled && ui_hit(x, y, 34, 34);
    if (hov) gfx_circle(s, x + 17, y + 17, 17, RGB(236, 239, 243));
    gfx_blit_alpha(s, x + 9, y + 9, icon_get(icon, 16), enabled ? 230 : 80);
    *clicked = enabled && ui.mpressed && hov;
}

enum { M_NEWTAB = 1, M_CLOSETAB, M_HOME, M_RELOAD, M_SAVE, M_NEWWIN };

static void save_page_cb(void *ctx, const char *path) {
    browser_t *b = ctx;
    if (!br_alive(b)) return;
    tab_t *t = cur_tab(b);
    // zapisz adres strony jako maly plik HTML z przekierowaniem (zakladka)
    char h[1200];
    int n = snprintf(h, sizeof h, "<title>%s</title><a href=\"%s\">%s</a>", t->pg ? t->pg->title : "", t->url, t->url);
    int r = fs_write_file(path, h, n);
    if (r) dlg_message(b->win, "Zapisz", fs_error(r), ICON_WARNING);
    explorer_refresh_all();
}

static void menu_cb(void *ctx, int id) {
    browser_t *b = ctx;
    if (!br_alive(b)) return;
    tab_t *t = cur_tab(b);
    switch (id) {
    case M_NEWTAB: tab_new(b, NULL); break;
    case M_CLOSETAB: tab_close(b, b->cur); break;
    case M_HOME: navigate(b, t, "sartax:start", true); break;
    case M_RELOAD: navigate(b, t, t->url, false); break;
    case M_NEWWIN: browser_open(NULL); break;
    case M_SAVE: dlg_input(b->win, "Zapisz zakladke", "Zapisz link do tej strony jako plik:", "/Dokumenty/zakladka.html", save_page_cb, b); break;
    }
}

static void br_frame(window_t *w, surface_t *s) {
    browser_t *b = w->data;
    tab_t *t = cur_tab(b);
    int W = ui.cw, H = ui.ch;
    int vh = H - TOP_H, vw = W - 14;
    page_t *p = t->pg;
    if (p && p->laid_w != vw) layout(p, vw);
    if (p && t->jump_main) { t->jump_main = false; t->scroll = MAX(0, p->main_y - 30); }

    // klawiatura
    if (ui.key && !ui.focus) {
        event_t *k = ui.key;
        if (ui_ctrl_key('t')) { tab_new(b, NULL); return; }
        if (ui_ctrl_key('w')) { tab_close(b, b->cur); return; }
        if (ui.key && k->key == KEY_TAB && (k->mods & MOD_CTRL)) { b->cur = (b->cur + 1) % b->ntabs; ui.key = NULL; update_title(b); return; }
        if (t->focus_field >= 0 && p && ui.key) {
            field_t *f = &p->fields[t->focus_field];
            int l = strlen(f->value);
            if (k->key == KEY_CHAR && (!(k->mods & MOD_CTRL) || (k->mods & (MOD_ALT | MOD_ALTGR)))) {
                char enc[4];
                int n = utf8_encode(k->ch, enc);
                if (l + n < (int)sizeof f->value - 1) { memcpy(f->value + l, enc, n); f->value[l + n] = 0; }
                ui.key = NULL;
            } else if (k->key == KEY_BACKSPACE) { if (l) f->value[utf8_prev(f->value, l)] = 0; ui.key = NULL; }
            else if (k->key == KEY_ENTER) { ui.key = NULL; submit_form(b, t->focus_field); return; }
            else if (k->key == KEY_ESC) { t->focus_field = -1; ui.key = NULL; }
        }
        if (ui.key) {
            int step = 0;
            if (k->key == KEY_DOWN) step = 40;
            else if (k->key == KEY_UP) step = -40;
            else if (k->key == KEY_PGDN || (k->key == KEY_CHAR && k->ch == ' ')) step = vh - 40;
            else if (k->key == KEY_PGUP) step = -(vh - 40);
            else if (k->key == KEY_HOME) step = -1000000;
            else if (k->key == KEY_END) step = 1000000;
            else if (k->key == KEY_F5) { ui.key = NULL; navigate(b, t, t->url, false); return; }
            else if (k->key == KEY_LEFT && (k->mods & MOD_ALT) && t->hpos > 0) { ui.key = NULL; t->hpos--; navigate(b, t, t->hist[t->hpos], false); return; }
            if (step) { t->scroll += step; ui.key = NULL; }
        }
    }

    // karty
    draw_tabs(b, s, W);
    if (cur_tab(b) != t || !br_alive(b)) return;
    // pasek narzedzi
    gfx_fill(s, 0, TABS_H, W, TB_H, RGB(255, 255, 255));
    gfx_fill(s, 0, TOP_H - 1, W, 1, RGB(222, 227, 233));
    bool c;
    round_icon_button(s, 8, TABS_H + 7, ICON_BACK, t->hpos > 0, &c);
    if (c) { t->hpos--; navigate(b, t, t->hist[t->hpos], false); }
    round_icon_button(s, 44, TABS_H + 7, ICON_FORWARD, t->hpos < t->nhist - 1, &c);
    if (c) { t->hpos++; navigate(b, t, t->hist[t->hpos], false); }
    round_icon_button(s, 80, TABS_H + 7, ICON_REFRESH, true, &c);
    if (c) navigate(b, t, t->url, false);
    round_icon_button(s, 116, TABS_H + 7, ICON_HOME, true, &c);
    if (c) navigate(b, t, "sartax:start", true);
    int ax = 158, aw = W - ax - 56, ay = TABS_H + 7;
    bool afoc = ui_textbox_focused(addr_id(b));
    bool ahov = ui_hit(ax, ay, aw, 34);
    if (afoc) {
        gfx_round_rect(s, ax, ay, aw, 34, 17, RGB(255, 255, 255));
        gfx_round_outline(s, ax, ay, aw, 34, 17, g_theme.accent);
        gfx_round_outline(s, ax - 1, ay - 1, aw + 2, 36, 18, WITH_ALPHA(g_theme.accent, 80));
    } else gfx_round_rect(s, ax, ay, aw, 34, 17, ahov ? RGB(232, 235, 238) : RGB(241, 243, 244));
    // ikona po lewej w pasku: klodka / lupa / plik
    if (!strncmp(t->url, "https://", 8)) {
        gfx_round_outline(s, ax + 14, ay + 9, 10, 10, 5, RGB(80, 86, 94));
        gfx_round_rect(s, ax + 12, ay + 15, 14, 11, 2, RGB(80, 86, 94));
    } else gfx_blit_alpha(s, ax + 11, ay + 9, icon_get(!strncmp(t->url, "plik:", 5) ? ICON_FILE : ICON_SEARCH, 16), 200);
    if (ui_textbox_id(addr_id(b), ax + 30, ay + 1, aw - 40, t->addr, sizeof t->addr, TB_NOFRAME, "Wyszukaj w Sartax lub wpisz adres")) {
        ui_focus_clear();
        go_address(b, t->addr);
    }
    // menu (trzy kropki)
    int mx = W - 46;
    bool mh = ui_hit(mx, ay, 34, 34);
    if (mh) gfx_circle(s, mx + 17, ay + 17, 17, RGB(236, 239, 243));
    for (int i = 0; i < 3; i++) gfx_circle(s, mx + 17, ay + 9 + i * 8, 2, RGB(80, 86, 94));
    if (ui.mpressed && mh) {
        menu_item_t it[] = {
            { "Nowa karta  (Ctrl+T)", -1, M_NEWTAB, b->ntabs >= 8 },
            { "Nowe okno", -1, M_NEWWIN, false },
            { "Zamknij karte  (Ctrl+W)", -1, M_CLOSETAB, false },
            { NULL, -1, 0, false },
            { "Strona startowa", ICON_HOME, M_HOME, false },
            { "Odswiez  (F5)", ICON_REFRESH, M_RELOAD, false },
            { "Zapisz zakladke...", ICON_DOWNLOAD, M_SAVE, false },
        };
        menu_open(w->x + 1 + mx - 200, w->y + TITLE_H + ay + 36, it, ARRAY_LEN(it), menu_cb, b);
    }

    // tresc
    u32 bg = p ? p->bg : RGB(255, 255, 255);
    gfx_fill(s, 0, TOP_H, W, vh, bg);
    if (!p) return;
    if (ui.wheel && ui_hit(0, TOP_H, W, vh)) t->scroll += ui.wheel * 60;
    t->scroll = CLAMP(t->scroll, 0, MAX(0, p->doc_h - vh));
    int sx0 = s->cx0, sy0 = s->cy0, sx1 = s->cx1, sy1 = s->cy1;
    gfx_set_clip(s, 0, TOP_H, vw, vh);
    int oy = TOP_H - t->scroll;
    int lo = 0, hi = p->nboxes;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (p->boxes[m].y + 120 < t->scroll) lo = m + 1; else hi = m;
    }
    int hover_link = -1;
    bool in_content = ui_hit(0, TOP_H, vw, vh);
    u32 tcol = p->text_col ? p->text_col : TEXT_COL, lcol = p->link_col ? p->link_col : LINK_COL;
    for (int i = MAX(0, lo - 8); i < p->nboxes; i++) {
        box_t *bx = &p->boxes[i];
        int y = bx->y + oy;
        if (y > TOP_H + vh + 120) break;
        if (y + bx->h < TOP_H) continue;
        bool hov = in_content && ui.mx >= bx->x && ui.mx < bx->x + bx->w + 4 && ui.my >= y && ui.my < y + bx->h + 3;
        if (bx->link >= 0 && hov) hover_link = bx->link;
        switch (bx->kind) {
        case B_TEXT: {
            u32 col = bx->color ? bx->color : bx->link >= 0 ? lcol : tcol;
            if (bx->link >= 0 && bx->color && bx->color == RGB(0, 0, 0)) col = lcol;
            gfx_text_n(s, bx->x, y, bx->font, p->arena + bx->text, bx->len, col);
            if (bx->link >= 0 && (hover_link == bx->link)) {
                int ux = bx->x;
                if (i > 0 && p->boxes[i - 1].link == bx->link && p->boxes[i - 1].y == bx->y && p->boxes[i - 1].kind == B_TEXT)
                    ux = p->boxes[i - 1].x + p->boxes[i - 1].w;
                gfx_fill(s, ux, y + bx->h - 1, bx->x + bx->w - ux, 1, col);
            }
            break;
        }
        case B_HR:
            gfx_fill(s, bx->x, y, bx->w, 1, RGB(218, 220, 224));
            break;
        case B_IMG: {
            pimg_t *pi = bx->val >= 0 ? &p->imgs[bx->val] : NULL;
            if (pi && pi->state == 1) gfx_blit_scaled(s, bx->x, y, bx->w, bx->h, pi->img);
            else gfx_round_rect(s, bx->x, y, bx->w, bx->h, 4, RGB(238, 240, 243));
            if (bx->link >= 0 && hov) gfx_rect(s, bx->x, y, bx->w, bx->h, WITH_ALPHA(lcol, 160));
            break;
        }
        case B_ALT:
            gfx_round_rect(s, bx->x, y, bx->w, bx->h, 6, RGB(241, 243, 244));
            gfx_blit(s, bx->x + 7, y + 4, icon_get(ICON_IMAGE, 16));
            gfx_text_n(s, bx->x + 29, y + 4, FONT_WEB_SMALL, p->arena + bx->text, bx->len, bx->link >= 0 ? lcol : RGB(95, 99, 104));
            break;
        case B_INPUT: {
            field_t *f = &p->fields[bx->val];
            bool foc = t->focus_field == bx->val && ui.focused;
            gfx_round_rect(s, bx->x, y, bx->w, bx->h, 18, RGB(255, 255, 255));
            gfx_round_outline(s, bx->x, y, bx->w, bx->h, 18, foc ? g_theme.accent : hov ? RGB(160, 166, 172) : RGB(218, 220, 224));
            if (foc) gfx_shadow(s, bx->x, y, bx->w, bx->h, 18, 6, 40);
            int a = s->cx0, bb = s->cx1, a2 = s->cy0, b2 = s->cy1;
            gfx_set_clip(s, bx->x + 14, TOP_H, bx->w - 28, vh);
            int tw = gfx_text_width(FONT_WEB, f->value);
            int sc = MAX(0, tw - (bx->w - 34));
            gfx_text(s, bx->x + 16 - sc, y + 9, FONT_WEB, f->value, TEXT_COL);
            if (foc && (ticks_ms() / 530) % 2 == 0) gfx_fill(s, bx->x + 16 + tw - sc, y + 8, 1, 20, TEXT_COL);
            s->cx0 = a, s->cx1 = bb, s->cy0 = a2, s->cy1 = b2;
            break;
        }
        case B_BUTTON: {
            field_t *f = &p->fields[bx->val];
            gfx_round_rect(s, bx->x, y, bx->w, bx->h, 8, hov ? RGB(232, 235, 238) : RGB(241, 243, 244));
            if (hov) gfx_round_outline(s, bx->x, y, bx->w, bx->h, 8, RGB(218, 220, 224));
            gfx_text_center(s, bx->x, y + 9, bx->w, FONT_WEB, f->value, RGB(60, 64, 67));
            break;
        }
        }
        if (ui.mpressed && hov) {
            if (bx->kind == B_INPUT) { t->focus_field = bx->val; ui_focus_clear(); }
            else if (bx->kind == B_BUTTON) { submit_form(b, bx->val); s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1; return; }
            else if (bx->link >= 0) {
                char u[900];
                if (resolve_url(t->url, p->arena + p->links[bx->link], u, sizeof u)) navigate(b, t, u, true);
                s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
                return;
            }
        }
    }
    if (ui.mpressed && in_content && hover_link < 0) {
        bool on_input = false;
        for (int i = 0; i < p->nboxes; i++) {
            box_t *bx = &p->boxes[i];
            if (bx->kind == B_INPUT && ui.mx >= bx->x && ui.mx < bx->x + bx->w && ui.my >= bx->y + oy && ui.my < bx->y + oy + bx->h) on_input = true;
        }
        if (!on_input) t->focus_field = -1;
    }
    // automatyczne skupienie pola wyszukiwania na stronie startowej
    if (!strncmp(t->url, "sartax:start", 12) && t->focus_field < 0 && p->nfields && !ui.focus && ui.render) t->focus_field = 0;
    s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
    ui_scrollbar(W - 14, TOP_H + 2, vh - 4, &t->scroll, p->doc_h, vh);

    // dymek z adresem linku / postep obrazkow (jak w Chrome)
    char st[600];
    st[0] = 0;
    if (hover_link >= 0) {
        if (!resolve_url(t->url, p->arena + p->links[hover_link], st, sizeof st)) strlcpy(st, p->arena + p->links[hover_link], sizeof st);
        if (!strncmp(st, "sartax:szukaj", 13)) st[0] = 0;
    } else if (b->status[0]) strlcpy(st, b->status, sizeof st);
    if (st[0]) {
        int tw = MIN(gfx_text_width(FONT_SMALL, st) + 20, W - 20);
        gfx_round_rect(s, 0, H - 24, tw, 24, 6, RGB(241, 243, 244));
        gfx_round_outline(s, -6, H - 24, tw + 6, 30, 6, RGB(218, 220, 224));
        gfx_text_ellipsis(s, 10, H - 19, tw - 16, FONT_SMALL, st, RGB(60, 64, 67));
    }
}

static bool br_tick(window_t *w) {
    browser_t *b = w->data;
    tab_t *t = cur_tab(b);
    static u64 last;
    u64 tt = ticks_ms() / 530;
    bool redraw = false;
    if (t->focus_field >= 0 && w == wm_focused() && tt != last) { last = tt; redraw = true; }
    if (t->pg && t->img_next < t->pg->nimgs && !menu_is_open()) {
        if (load_one_image(b)) redraw = true;
    } else if (b->status[0]) { b->status[0] = 0; redraw = true; }
    return redraw;
}

static void br_close(window_t *w) {
    browser_t *b = w->data;
    for (int i = 0; i < 8; i++)
        if (browsers[i] == w) browsers[i] = NULL;
    for (int i = 0; i < b->ntabs; i++) { page_free(b->tabs[i]->pg); kfree(b->tabs[i]); }
    kfree(b);
}

void browser_open(const char *url) {
    int slot = -1;
    for (int i = 0; i < 8; i++)
        if (!browsers[i]) { slot = i; break; }
    if (slot < 0) return;
    browser_t *b = kzalloc(sizeof *b);
    window_t *w = wm_create("browser", "Przegladarka Sartax", ICON_GLOBE, 1000, 680, WF_RESIZABLE, br_frame, b);
    w->min_w = 520;
    w->min_h = 320;
    w->on_close = br_close;
    w->tick = br_tick;
    b->win = w;
    browsers[slot] = w;
    tab_new(b, url);
}

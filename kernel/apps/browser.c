// Sartax OS - Przegladarka (wlasny silnik HTML)
// Obsluguje: naglowki, akapity, linki, listy, proste tabele, pre/code, obrazki (jako opis),
// formularze GET (pola tekstowe + przycisk), encje HTML, UTF-8 / ISO-8859-2 / Windows-1250.
// Bez CSS i JavaScriptu.
#include "../apps.h"
#include "../net.h"

enum { F_TEXT, F_BR, F_BLOCK, F_HR, F_LI, F_INDENT, F_IMG, F_INPUT, F_BUTTON, F_SPACE, F_MAIN };
enum { B_TEXT, B_HR, B_IMG, B_INPUT, B_BUTTON };

typedef struct {
    u8 type, font, pre;
    int text, len;      // w arenie
    int link;
    int val;
} frag_t;

typedef struct {
    int x, y, w, h;
    u8 kind, font;
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
    char *arena;
    int alen, acap;
    frag_t *frags;
    int nfrags, cfrags;
    int *links;         // przesuniecia w arenie
    int nlinks, clinks;
    field_t *fields;
    int nfields;
    char (*forms)[512];
    int nforms;
    box_t *boxes;
    int nboxes, cboxes;
    int doc_h, laid_w;
    int main_prio;
    int main_y;
    char title[128];
} page_t;

typedef struct {
    char url[512];
    char addr[512];
    page_t *pg;
    int scroll;
    char hist[32][512];
    int nhist, hpos;
    int focus_field;
    char status[256];
    window_t *win;
    char pending[512];
    bool pending_record;
    bool jump_main;
} browser_t;

#define TB_H 48
#define ST_H 24
#define MARGIN 22
#define LINK_COL RGB(26, 100, 210)
#define TEXT_COL RGB(30, 36, 44)

static const char *START_PAGE =
    "<title>Strona startowa</title>"
    "<h1>Przegladarka Sartax</h1>"
    "<p>Wlasna przegladarka systemu Sartax OS. Wpisz adres strony albo to, czego szukasz, w pasku u gory.</p>"
    "<form action=\"https://html.duckduckgo.com/html/\"><input type=\"text\" name=\"q\"> <input type=\"submit\" value=\"Szukaj w DuckDuckGo\"></form>"
    "<h2>Ulubione strony</h2><ul>"
    "<li><a href=\"https://github.com/Mikolajek6572/sartax-os\">Sartax OS na GitHubie</a></li>"
    "<li><a href=\"https://pl.m.wikipedia.org/wiki/System_operacyjny\">Wikipedia: System operacyjny</a></li>"
    "<li><a href=\"https://pl.m.wikipedia.org/wiki/Specjalna:Losowa_strona\">Wikipedia: losowy artykul</a></li>"
    "<li><a href=\"https://text.npr.org/\">NPR - wiadomosci w wersji tekstowej</a></li>"
    "<li><a href=\"https://lite.cnn.com/\">CNN Lite</a></li>"
    "<li><a href=\"https://example.com/\">example.com</a></li>"
    "</ul>"
    "<h2>Co potrafi ta przegladarka?</h2>"
    "<p>Pobiera strony przez wlasny stos TCP/IP i TLS, sama czyta HTML i uklada tekst. Obsluguje naglowki, akapity, "
    "<b>pogrubienia</b>, <code>kod</code>, linki, listy, proste tabele i formularze wyszukiwania. "
    "Nie obsluguje CSS ani JavaScriptu, wiec niektore strony wygladaja prosto - ale da sie je czytac.</p>"
    "<p>Pliki, ktore nie sa stronami (np. obrazy BMP albo programy), zapisuja sie w folderze <b>/Pobrane</b>.</p>"
    "<hr><p><small>Sartax OS " SARTAX_VERSION "</small></p>";

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
};

// dekoduje tekst z encjami do bufora (UTF-8), zwraca dlugosc
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
        // twarda spacja w UTF-8 -> zwykla
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
    if (strstr(href, "://") || !strncmp(href, "sartax:", 7) || !strncmp(href, "plik:", 5)) strlcpy(tmp, href, sizeof tmp);
    else if (href[0] == '/' && href[1] == '/') snprintf(tmp, sizeof tmp, "%s%s", !strncmp(base, "http:", 5) ? "http:" : "https:", href);
    else {
        const char *hs = strstr(base, "://");
        if (!hs) {
            if (!strncmp(base, "plik:", 5)) {
                // plik lokalny
                if (href[0] == '/') snprintf(tmp, sizeof tmp, "plik:%s", href);
                else {
                    char dir[512];
                    path_parent(dir, sizeof dir, base + 5);
                    snprintf(tmp, sizeof tmp, "plik:%s/%s", strcmp(dir, "/") ? dir : "", href);
                }
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
            // katalog bazowy
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
    // usun "./" i "../"
    char *pstart = strstr(tmp, "://");
    pstart = pstart ? strchr(pstart + 3, '/') : NULL;
    if (pstart) {
        char *q = strchr(pstart, '?');
        char query[512] = "";
        if (q) { strlcpy(query, q, sizeof query); *q = 0; }
        char segs[64][128];
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

// ---------------- kodowanie znakow ----------------
static char *to_utf8(const u8 *d, size_t n, const char *ctype) {
    char cs = 0;   // 2 = iso-8859-2, 5 = windows-1250, 1 = latin1
    char probe[1100];
    size_t pl = MIN(n, 1024);
    for (size_t i = 0; i < pl; i++) probe[i] = lower(d[i]);
    probe[pl] = 0;
    char ct[64];
    for (int i = 0; i < 63 && ctype[i]; i++) ct[i] = lower(ctype[i]), ct[i + 1] = 0;
    if (!ctype[0]) ct[0] = 0;
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
                if (c == 0xA5 || c == 0xB9) {}
            }
            if (c == 0xA1) cp = 0x2C7;
        }
        o += utf8_encode(cp, out + o);
    }
    out[o] = 0;
    return out;
}

// ---------------- parser HTML ----------------
typedef struct {
    int bold, mono, pre, small, heading;
    int link_stack[32], nlink;
    int list_type[16], list_n[16], nlist;
    int cur_form;
    bool in_title, in_button;
    char button_label[96];
    int indent;
} pstate_t;

static int cur_font(pstate_t *s) {
    if (s->heading == 1) return FONT_BIG;
    if (s->heading == 2) return FONT_TITLE;
    if (s->heading >= 3 || s->bold) return FONT_BOLD;
    if (s->mono || s->pre) return FONT_MONO;
    if (s->small) return FONT_SMALL;
    return FONT_UI;
}

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
        int dn = decode_text(t, n, buf, sizeof buf);
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
            // sama spacja - zaznacz odstep
            if (p->nfrags && p->frags[p->nfrags - 1].type == F_TEXT) {
                frag_t *f = frag_new(p, F_TEXT);
                f->text = arena_add(p, " ", 1);
                f->len = 1;
                f->font = cur_font(s);
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
    f->link = s->nlink ? s->link_stack[s->nlink - 1] : -1;
    kfree(buf);
}

static int add_link(page_t *p, const char *href) {
    if (p->nlinks == p->clinks) {
        p->clinks = p->clinks ? p->clinks * 2 : 64;
        p->links = krealloc(p->links, p->clinks * sizeof(int));
    }
    p->links[p->nlinks] = arena_add(p, href, strlen(href) + 1);   // z zerem na koncu
    return p->nlinks++;
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
    // szukaj </tag
    int tl = strlen(tag);
    for (; s + tl + 2 < end; s++)
        if (s[0] == '<' && s[1] == '/' && starts_ci(s + 2, end, tag)) {
            const char *g = memchr(s, '>', end - s);
            return g ? g + 1 : end;
        }
    return end;
}

static void handle_tag(page_t *p, pstate_t *s, const char *name, bool close, const char *attrs, int alen) {
    char a[512];
#define IS(x) (!strcmp(name, x))
    // poczatek wlasciwej tresci strony (pomijamy menu)
    if (!close && p->main_prio < 3) {
        int m = (IS("main") || IS("article")) ? 1 : 0;
        if (get_attr(attrs, alen, "id", a, sizeof a)) {
            if (!strcmp(a, "content") || !strcmp(a, "main") || !strcmp(a, "main-content")) m = MAX(m, 2);
            if (!strcmp(a, "mw-content-text") || !strcmp(a, "readme")) m = 3;
        }
        if (m > p->main_prio) { p->main_prio = m; frag_new(p, F_MAIN); }
    }
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
    if (IS("p")) { emit_block(p, 10); return; }
    if (IS("div") || IS("section") || IS("article") || IS("header") || IS("footer") || IS("nav") || IS("main") || IS("aside") ||
        IS("figure") || IS("table") || IS("center") || IS("dl") || IS("address") || IS("fieldset") || IS("details") ||
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
        emit_block(p, s->nlist ? 2 : 8);
        if (!close && s->nlist < 16) {
            s->list_type[s->nlist] = IS("ol");
            s->list_n[s->nlist] = 0;
            s->nlist++;
            frag_new(p, F_INDENT)->val = 26;
        } else if (close && s->nlist) {
            s->nlist--;
            frag_new(p, F_INDENT)->val = -26;
        }
        return;
    }
    if (IS("li")) {
        if (close) return;
        emit_block(p, 3);
        frag_t *f = frag_new(p, F_LI);
        char b[16];
        if (s->nlist && s->list_type[s->nlist - 1]) snprintf(b, sizeof b, "%d.", ++s->list_n[s->nlist - 1]);
        else strlcpy(b, "•", sizeof b);
        if (s->nlist > 1 && !s->list_type[s->nlist - 1]) strlcpy(b, "–", sizeof b);
        f->text = arena_add(p, b, strlen(b));
        f->len = strlen(b);
        return;
    }
    if (IS("blockquote") || IS("dd")) {
        emit_block(p, IS("dd") ? 2 : 8);
        frag_new(p, F_INDENT)->val = close ? -26 : 26;
        return;
    }
    if (IS("pre")) {
        emit_block(p, 8);
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
        get_attr(attrs, alen, "alt", a, sizeof a);
        if (!a[0]) return;    // obrazki bez opisu pomijamy (ozdobniki)
        frag_t *f = frag_new(p, F_IMG);
        f->text = arena_add(p, a, strlen(a));
        f->len = strlen(a);
        f->link = s->nlink ? s->link_stack[s->nlink - 1] : -1;
        return;
    }
    if (IS("tr")) { if (!close) frag_new(p, F_BR); return; }
    if (IS("td")) { if (!close) frag_new(p, F_SPACE)->val = 16; return; }
    if (IS("title")) { s->in_title = !close; return; }
    if (IS("input") && !close) {
        char type[32], nm[64], val[256];
        get_attr(attrs, alen, "type", type, sizeof type);
        get_attr(attrs, alen, "name", nm, sizeof nm);
        get_attr(attrs, alen, "value", val, sizeof val);
        for (char *c = type; *c; c++) *c = lower(*c);
        if (!strcmp(type, "hidden")) { add_field(p, s->cur_form, 1, nm, val); return; }
        if (!strcmp(type, "submit") || !strcmp(type, "button") || !strcmp(type, "image")) {
            int fi = add_field(p, s->cur_form, 2, nm, val[0] ? val : "Wyslij");
            if (fi < 0) return;
            frag_new(p, F_BUTTON)->val = fi;
            return;
        }
        if (!strcmp(type, "checkbox") || !strcmp(type, "radio")) {
            if (s->cur_form < 0) return;   // przelaczniki menu stron (bez formularza)
            frag_t *f = frag_new(p, F_TEXT);
            f->text = arena_add(p, "[ ] ", 4);
            f->len = 4;
            f->font = FONT_MONO;
            return;
        }
        int fi = add_field(p, s->cur_form, 0, nm, val);
        if (fi < 0) return;
        frag_new(p, F_INPUT)->val = fi;
        return;
    }
    if (IS("button")) {
        if (!close) { s->in_button = true; s->button_label[0] = 0; return; }
        s->in_button = false;
        // przyciski bez napisu (ikonki sterowane JavaScriptem) pomijamy
        if (!s->button_label[0] && s->cur_form < 0) return;
        int fi = add_field(p, s->cur_form, 2, "", s->button_label[0] ? s->button_label : "Wyslij");
        if (fi >= 0) frag_new(p, F_BUTTON)->val = fi;
        return;
    }
#undef IS
}

static bool is_void(const char *n) {
    static const char *v[] = { "input", "img", "br", "hr", "meta", "link", "source", "wbr", "area", "col", "embed" };
    for (int i = 0; i < (int)ARRAY_LEN(v); i++)
        if (!strcmp(n, v[i])) return true;
    return false;
}

// element ukryty: atrybut hidden, aria-hidden="true" albo style z display:none
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

// pomin caly element (z zagniezdzeniami tego samego znacznika)
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

static page_t *parse_html(const char *html, size_t n, bool plain) {
    page_t *p = kzalloc(sizeof *p);
    if (plain) {
        frag_t *f = frag_new(p, F_TEXT);
        f->text = arena_add(p, html, n);
        f->len = n;
        f->font = FONT_MONO;
        f->pre = 1;
        return p;
    }
    pstate_t st;
    memset(&st, 0, sizeof st);
    st.cur_form = -1;
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
                emit_text(p, &st, s, 1);
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
                if (!strcmp(name, "textarea")) { int fi = add_field(p, st.cur_form, 0, "", ""); if (fi >= 0) frag_new(p, F_INPUT)->val = fi; }
                if (!alen || as[alen - 1] != '/') s = skip_raw(s, end, name);
                continue;
            }
            if (!close && is_hidden(as, alen) && !is_void(name)) {
                if (!alen || as[alen - 1] != '/') s = skip_element(s, end, name);
                continue;
            }
            handle_tag(p, &st, name, close, as, alen);
        } else {
            const char *e = memchr(s, '<', end - s);
            if (!e) e = end;
            emit_text(p, &st, s, e - s);
            s = e;
        }
    }
    // tytul bez spacji na koncach
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
} lay_t;

static void lay_newline(lay_t *L, int min_h) {
    int h = L->lineh ? L->lineh : min_h;
    // wyrownaj elementy linii do dolu (przyblizenie linii bazowej)
    for (int i = L->line_start; i < L->p->nboxes; i++) {
        box_t *b = &L->p->boxes[i];
        b->y += h - b->h - 3;
        if (b->y < L->y) b->y = L->y;
    }
    L->y += h;
    L->x = L->left;
    L->lineh = 0;
    L->line_start = L->p->nboxes;
    L->pending_space = false;
    L->line_empty = true;
}

static void lay_inline(lay_t *L, int w, int h) {
    int sw = L->pending_space && !L->line_empty ? 4 : 0;
    if (!L->line_empty && L->x + sw + w > L->right) { lay_newline(L, 18); sw = 0; }
    L->x += sw;
    L->pending_space = false;
    L->lineh = MAX(L->lineh, h + 4);
    L->line_empty = false;
}

static void layout(page_t *p, int width) {
    p->nboxes = 0;
    lay_t L = { p, MARGIN, MAX(width - MARGIN, MARGIN + 100), MARGIN, 14, 0, 0, false, true };
    int last_margin = 0;
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
                            b->kind = B_TEXT, b->font = f->font, b->text = f->text + a, b->len = k - a;
                            b->x = L.x, b->y = L.y, b->w = gfx_text_width_n(f->font, t + a, k - a), b->h = fh;
                            b->link = f->link;
                            L.x += b->w;
                            L.lineh = MAX(L.lineh, fh + 3);
                            L.line_empty = false;
                        }
                        if (k < f->len) lay_newline(&L, fh + 3);
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
                if (!L.line_empty && L.x + gap + w > L.right) { lay_newline(&L, fh + 5); gap = 0; }
                L.x += gap;
                L.pending_space = false;
                box_t *b = box_new(p);
                b->kind = B_TEXT, b->font = f->font, b->text = f->text + a, b->len = k - a;
                b->x = L.x, b->y = L.y, b->w = w, b->h = fh;
                b->link = f->link;
                L.x += w;
                L.lineh = MAX(L.lineh, fh + 5);
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
            lay_newline(&L, 18);
            break;
        case F_BLOCK:
            if (!L.line_empty) lay_newline(&L, 18);
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
            b->kind = B_TEXT, b->font = FONT_UI, b->text = f->text, b->len = f->len;
            b->w = gfx_text_width_n(FONT_UI, t, f->len);
            b->x = L.left - b->w - 8, b->y = L.y, b->h = gfx_font_height(FONT_UI);
            b->link = -2;     // znacznik listy (szary)
            break;
        }
        case F_IMG: {
            const char *t = p->arena + f->text;
            int w = MIN(gfx_text_width_n(FONT_SMALL, t, f->len) + 34, L.right - L.left);
            lay_inline(&L, w, 22);
            box_t *b = box_new(p);
            b->kind = B_IMG, b->x = L.x, b->y = L.y, b->w = w, b->h = 22, b->text = f->text, b->len = f->len, b->link = f->link;
            L.x += w;
            L.pending_space = true;
            last_margin = 0;
            break;
        }
        case F_INPUT: {
            int w = MIN(260, L.right - L.left);
            lay_inline(&L, w, 28);
            box_t *b = box_new(p);
            b->kind = B_INPUT, b->x = L.x, b->y = L.y, b->w = w, b->h = 28, b->val = f->val;
            L.x += w;
            L.pending_space = true;
            last_margin = 0;
            break;
        }
        case F_BUTTON: {
            field_t *fl = &p->fields[f->val];
            int w = gfx_text_width(FONT_UI, fl->value) + 28;
            lay_inline(&L, w, 28);
            box_t *b = box_new(p);
            b->kind = B_BUTTON, b->x = L.x, b->y = L.y, b->w = w, b->h = 28, b->val = f->val;
            L.x += w;
            L.pending_space = true;
            last_margin = 0;
            break;
        }
        }
    }
    if (!L.line_empty) lay_newline(&L, 18);
    p->doc_h = L.y + MARGIN;
    p->laid_w = width;
}

// ---------------- nawigacja ----------------
static window_t *browsers[8];

static bool br_alive(browser_t *b) {
    for (int i = 0; i < 8; i++)
        if (browsers[i] && browsers[i]->data == b) return true;
    return false;
}

static void set_page(browser_t *b, page_t *p, const char *url) {
    page_free(b->pg);
    b->pg = p;
    b->scroll = 0;
    b->focus_field = -1;
    b->jump_main = p->main_prio > 0;
    strlcpy(b->url, url, sizeof b->url);
    strlcpy(b->addr, !strcmp(url, "sartax:start") ? "" : url, sizeof b->addr);
    char t[200];
    snprintf(t, sizeof t, "%s - Przegladarka", p->title[0] ? p->title : url);
    wm_set_title(b->win, t);
    b->status[0] = 0;
}

static void error_page(browser_t *b, const char *url, const char *err) {
    char *h = kmalloc(2048);
    char e1[300], e2[600];
    // escapowanie < >
    int o = 0;
    for (const char *s = err; *s && o < 290; s++) { if (*s == '<') { strlcpy(e1 + o, "&lt;", 6); o += 4; } else e1[o++] = *s; }
    e1[o] = 0;
    o = 0;
    for (const char *s = url; *s && o < 590; s++) { if (*s == '<' || *s == '>') continue; e2[o++] = *s; }
    e2[o] = 0;
    snprintf(h, 2048, "<title>Blad</title><h1>Nie mozna otworzyc strony</h1><p><b>%s</b></p><p>Adres: <code>%s</code></p>"
                      "<p>Sprawdz adres i polaczenie z internetem. Niektore strony wymagaja TLS 1.3 albo JavaScriptu, "
                      "ktorych Przegladarka Sartax jeszcze nie obsluguje.</p><p><a href=\"sartax:start\">Strona startowa</a></p>",
             e1, e2);
    page_t *p = parse_html(h, strlen(h), false);
    kfree(h);
    set_page(b, p, url);
}

static void prog_cb(void *ctx, size_t done, size_t total) { busy_update("Wczytywanie strony...", done, total); }

static void save_download(browser_t *b, const char *url, const u8 *d, size_t n) {
    const char *q = strrchr(url, '/');
    char name[96];
    strlcpy(name, q && q[1] ? q + 1 : "plik", sizeof name);
    char *qq = strchr(name, '?');
    if (qq) *qq = 0;
    if (!path_valid_name(name)) strlcpy(name, "pobrany_plik", sizeof name);
    if (!fs_exists("/Pobrane")) fs_mkdir("/Pobrane");
    char path[256];
    path_join(path, sizeof path, "/Pobrane", name);
    int r = fs_write_file(path, d, n);
    char msg[320];
    char sz[32];
    fmt_size(sz, sizeof sz, n);
    if (r) snprintf(msg, sizeof msg, "Nie udalo sie zapisac pliku: %s", fs_error(r));
    else snprintf(msg, sizeof msg, "Ten adres to plik, a nie strona. Zapisano go jako %s (%s).", path, sz);
    dlg_message(b->win, "Pobrano plik", msg, r ? ICON_WARNING : ICON_DOWNLOAD);
    explorer_refresh_all();
}

static void load_task(void *ctx) {
    browser_t *b = ctx;
    if (!br_alive(b)) return;
    char url[512];
    strlcpy(url, b->pending, sizeof url);
    bool record = b->pending_record;
    if (record) {
        if (b->hpos < b->nhist - 1) b->nhist = b->hpos + 1;
        if (b->nhist == 32) { memmove(b->hist[0], b->hist[1], 31 * 512); b->nhist--; }
        strlcpy(b->hist[b->nhist++], url, 512);
        b->hpos = b->nhist - 1;
    }
    if (!strcmp(url, "sartax:start")) { set_page(b, parse_html(START_PAGE, strlen(START_PAGE), false), url); return; }
    if (!strncmp(url, "plik:", 5)) {
        u8 *d;
        u64 n;
        int r = fs_read_file(url + 5, &d, &n);
        if (r) { error_page(b, url, fs_error(r)); return; }
        bool html = str_ends_with_ci(url, ".html") || str_ends_with_ci(url, ".htm");
        char *u = to_utf8(d, n, "");
        kfree(d);
        set_page(b, parse_html(u, strlen(u), !html), url);
        kfree(u);
        return;
    }
    busy_begin("Przegladarka");
    http_resp_t r;
    int rc = http_get(url, &r, prog_cb, NULL);
    busy_end();
    if (!br_alive(b)) { http_free(&r); return; }
    const char *final = r.final_url[0] ? r.final_url : url;
    if (rc) { error_page(b, final, r.error); http_free(&r); return; }
    char ct[64];
    int i = 0;
    for (; r.ctype[i] && i < 63; i++) ct[i] = lower(r.ctype[i]);
    ct[i] = 0;
    bool html = strstr(ct, "html") != NULL;
    bool text = !strncmp(ct, "text/", 5) || strstr(ct, "json") || strstr(ct, "xml");
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
    if (record && b->nhist) strlcpy(b->hist[b->hpos], final, 512);
    set_page(b, p, final);
}

static void navigate(browser_t *b, const char *url, bool record) {
    strlcpy(b->pending, url, sizeof b->pending);
    b->pending_record = record;
    ui_defer(load_task, b);
}

static void go_address(browser_t *b, const char *text) {
    while (*text == ' ') text++;
    if (!*text) { navigate(b, "sartax:start", true); return; }
    char u[512];
    if (strstr(text, "://") || !strncmp(text, "sartax:", 7) || !strncmp(text, "plik:", 5)) strlcpy(u, text, sizeof u);
    else if (text[0] == '/') snprintf(u, sizeof u, "plik:%s", text);
    else if (strchr(text, '.') && !strchr(text, ' ')) snprintf(u, sizeof u, "https://%s", text);
    else {
        char q[400];
        url_encode(text, q, sizeof q);
        snprintf(u, sizeof u, "https://html.duckduckgo.com/html/?q=%s", q);
    }
    navigate(b, u, true);
}

static void submit_form(browser_t *b, int field) {
    page_t *p = b->pg;
    int form = p->fields[field].form;
    char action[512] = "";
    if (form >= 0) strlcpy(action, p->forms[form], sizeof action);
    char base[512];
    if (!action[0]) strlcpy(base, b->url, sizeof base);
    else if (!resolve_url(b->url, action, base, sizeof base)) return;
    char *q = strchr(base, '?');
    if (q) *q = 0;
    char url[1024];
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
    navigate(b, url, true);
}

// ---------------- widok ----------------
static void br_frame(window_t *w, surface_t *s) {
    browser_t *b = w->data;
    int W = ui.cw, H = ui.ch;
    int vh = H - TB_H - ST_H, vw = W - 14;
    page_t *p = b->pg;
    if (p && p->laid_w != vw) layout(p, vw);
    if (p && b->jump_main) { b->jump_main = false; b->scroll = MAX(0, p->main_y - 30); }

    // klawiatura (gdy nie piszemy w polu adresu)
    if (ui.key && !ui.focus) {
        event_t *k = ui.key;
        if (b->focus_field >= 0 && p) {
            field_t *f = &p->fields[b->focus_field];
            int l = strlen(f->value);
            if (k->key == KEY_CHAR && (!(k->mods & MOD_CTRL) || (k->mods & (MOD_ALT | MOD_ALTGR)))) {
                char enc[4];
                int n = utf8_encode(k->ch, enc);
                if (l + n < (int)sizeof f->value - 1) { memcpy(f->value + l, enc, n); f->value[l + n] = 0; }
                ui.key = NULL;
            } else if (k->key == KEY_BACKSPACE) { if (l) f->value[utf8_prev(f->value, l)] = 0; ui.key = NULL; }
            else if (k->key == KEY_ENTER) { ui.key = NULL; submit_form(b, b->focus_field); return; }
            else if (k->key == KEY_ESC) { b->focus_field = -1; ui.key = NULL; }
        }
        if (ui.key) {
            int step = 0;
            if (k->key == KEY_DOWN) step = 40;
            else if (k->key == KEY_UP) step = -40;
            else if (k->key == KEY_PGDN || (k->key == KEY_CHAR && k->ch == ' ')) step = vh - 40;
            else if (k->key == KEY_PGUP) step = -(vh - 40);
            else if (k->key == KEY_HOME) step = -1000000;
            else if (k->key == KEY_END) step = 1000000;
            else if (k->key == KEY_F5) { ui.key = NULL; navigate(b, b->url, false); return; }
            else if (k->key == KEY_LEFT && (k->mods & MOD_ALT) && b->hpos > 0) { ui.key = NULL; b->hpos--; navigate(b, b->hist[b->hpos], false); return; }
            if (step) { b->scroll += step; ui.key = NULL; }
        }
    }

    // pasek narzedzi
    gfx_fill(s, 0, 0, W, TB_H, g_theme.panel);
    gfx_fill(s, 0, TB_H - 1, W, 1, RGB(222, 228, 234));
    if (ui_button_icon(8, 7, 34, 34, ICON_BACK, NULL, BTN_FLAT) && b->hpos > 0) { b->hpos--; navigate(b, b->hist[b->hpos], false); }
    if (ui_button_icon(44, 7, 34, 34, ICON_FORWARD, NULL, BTN_FLAT) && b->hpos < b->nhist - 1) { b->hpos++; navigate(b, b->hist[b->hpos], false); }
    if (ui_button_icon(80, 7, 34, 34, ICON_REFRESH, NULL, BTN_FLAT)) navigate(b, b->url, false);
    if (ui_button_icon(116, 7, 34, 34, ICON_HOME, NULL, BTN_FLAT)) navigate(b, "sartax:start", true);
    int ax = 158, aw = W - ax - 70;
    if (!strncmp(b->url, "https://", 8)) {
        gfx_round_rect(s, ax, 8, 30, 32, 7, RGB(220, 244, 228));
        // klodka
        gfx_round_outline(s, ax + 10, 13, 10, 12, 5, RGB(40, 140, 80));
        gfx_round_rect(s, ax + 8, 20, 14, 11, 2, RGB(40, 140, 80));
        ax += 34;
        aw -= 34;
    }
    if (ui_textbox(ax, 8, aw, b->addr, sizeof b->addr, 0, "Wpisz adres albo szukaj w DuckDuckGo")) {
        ui_focus_clear();
        go_address(b, b->addr);
    }
    if (ui_button(W - 62, 8, 54, 32, "Idz", BTN_PRIMARY)) go_address(b, b->addr);

    // tresc
    gfx_fill(s, 0, TB_H, W, vh, RGB(255, 255, 255));
    if (!p) return;
    if (ui.wheel && ui_hit(0, TB_H, W, vh)) b->scroll += ui.wheel * 60;
    b->scroll = CLAMP(b->scroll, 0, MAX(0, p->doc_h - vh));
    int sx0 = s->cx0, sy0 = s->cy0, sx1 = s->cx1, sy1 = s->cy1;
    gfx_set_clip(s, 0, TB_H, vw, vh);
    int oy = TB_H - b->scroll;
    // pierwsze widoczne pudelko (bisekcja - pudelka sa w kolejnosci y, z drobnymi wyjatkami)
    int lo = 0, hi = p->nboxes;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (p->boxes[m].y + 60 < b->scroll) lo = m + 1; else hi = m;
    }
    int hover_link = -1;
    bool in_content = ui_hit(0, TB_H, vw, vh);
    for (int i = MAX(0, lo - 8); i < p->nboxes; i++) {
        box_t *bx = &p->boxes[i];
        int y = bx->y + oy;
        if (y > TB_H + vh + 60) break;
        if (y + bx->h < TB_H) continue;
        bool hov = in_content && ui.mx >= bx->x && ui.mx < bx->x + bx->w + 4 && ui.my >= y && ui.my < y + bx->h + 3;
        switch (bx->kind) {
        case B_TEXT: {
            u32 c = bx->link >= 0 ? LINK_COL : bx->link == -2 ? RGB(110, 120, 130) : TEXT_COL;
            gfx_text_n(s, bx->x, y, bx->font, p->arena + bx->text, bx->len, c);
            if (bx->link >= 0 && hov) hover_link = bx->link;
            if (bx->link >= 0) {
                // podkreslenie ciagle przez spacje miedzy slowami tego samego linku
                int ux = bx->x;
                if (i > 0 && p->boxes[i - 1].link == bx->link && p->boxes[i - 1].y == bx->y && p->boxes[i - 1].kind == B_TEXT)
                    ux = p->boxes[i - 1].x + p->boxes[i - 1].w;
                gfx_fill(s, ux, y + bx->h - 1, bx->x + bx->w - ux, 1, WITH_ALPHA(LINK_COL, 110));
            }
            break;
        }
        case B_HR:
            gfx_fill(s, bx->x, y, bx->w, 1, RGB(214, 220, 226));
            break;
        case B_IMG:
            gfx_round_rect(s, bx->x, y, bx->w, bx->h, 5, RGB(240, 244, 248));
            gfx_round_outline(s, bx->x, y, bx->w, bx->h, 5, RGB(214, 220, 226));
            gfx_blit(s, bx->x + 6, y + 3, icon_get(ICON_IMAGE, 16));
            gfx_text_n(s, bx->x + 28, y + 4, FONT_SMALL, p->arena + bx->text, bx->len, bx->link >= 0 ? LINK_COL : g_theme.text_dim);
            if (bx->link >= 0 && hov) hover_link = bx->link;
            break;
        case B_INPUT: {
            field_t *f = &p->fields[bx->val];
            bool foc = b->focus_field == bx->val && ui.focused;
            gfx_round_rect(s, bx->x, y, bx->w, bx->h, 6, RGB(255, 255, 255));
            gfx_round_outline(s, bx->x, y, bx->w, bx->h, 6, foc ? g_theme.accent : RGB(170, 180, 190));
            int a = s->cx0, bb = s->cx1;
            gfx_set_clip(s, bx->x + 6, TB_H, bx->w - 12, vh);
            int tw = gfx_text_width(FONT_UI, f->value);
            int sc = MAX(0, tw - (bx->w - 20));
            gfx_text(s, bx->x + 8 - sc, y + 7, FONT_UI, f->value, TEXT_COL);
            if (foc && (ticks_ms() / 530) % 2 == 0) gfx_fill(s, bx->x + 8 + tw - sc, y + 6, 1, 16, TEXT_COL);
            s->cx0 = a, s->cx1 = bb;
            break;
        }
        case B_BUTTON: {
            field_t *f = &p->fields[bx->val];
            gfx_round_rect(s, bx->x, y, bx->w, bx->h, 6, hov ? RGB(228, 234, 240) : RGB(240, 244, 248));
            gfx_round_outline(s, bx->x, y, bx->w, bx->h, 6, RGB(170, 180, 190));
            gfx_text_center(s, bx->x, y + 7, bx->w, FONT_UI, f->value, TEXT_COL);
            break;
        }
        }
        if (ui.mpressed && hov) {
            if (bx->kind == B_INPUT) { b->focus_field = bx->val; ui_focus_clear(); }
            else if (bx->kind == B_BUTTON) { submit_form(b, bx->val); s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1; return; }
            else if (bx->link >= 0) {
                char u[512];
                if (resolve_url(b->url, p->arena + p->links[bx->link], u, sizeof u)) navigate(b, u, true);
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
        if (!on_input) b->focus_field = -1;
    }
    s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
    ui_scrollbar(W - 14, TB_H + 2, vh - 4, &b->scroll, p->doc_h, vh);

    // pasek stanu
    gfx_fill(s, 0, H - ST_H, W, ST_H, g_theme.panel);
    gfx_fill(s, 0, H - ST_H, W, 1, RGB(228, 233, 238));
    char st[600];
    if (hover_link >= 0) {
        char u[512];
        if (!resolve_url(b->url, p->arena + p->links[hover_link], u, sizeof u)) strlcpy(u, p->arena + p->links[hover_link], sizeof u);
        snprintf(st, sizeof st, "%s", u);
    } else snprintf(st, sizeof st, "Gotowe   |   %d linkow", p->nlinks);
    gfx_text_ellipsis(s, 10, H - ST_H + 5, W - 20, FONT_SMALL, st, g_theme.text_dim);
}

static bool br_tick(window_t *w) {
    browser_t *b = w->data;
    static u64 last;
    u64 t = ticks_ms() / 530;
    if (b->focus_field >= 0 && w == wm_focused() && t != last) { last = t; return true; }
    return false;
}

static void br_close(window_t *w) {
    browser_t *b = w->data;
    for (int i = 0; i < 8; i++)
        if (browsers[i] == w) browsers[i] = NULL;
    page_free(b->pg);
    kfree(b);
}

void browser_open(const char *url) {
    int slot = -1;
    for (int i = 0; i < 8; i++)
        if (!browsers[i]) { slot = i; break; }
    if (slot < 0) return;
    browser_t *b = kzalloc(sizeof *b);
    b->focus_field = -1;
    window_t *w = wm_create("browser", "Przegladarka", ICON_GLOBE, 960, 640, WF_RESIZABLE, br_frame, b);
    w->min_w = 480;
    w->min_h = 300;
    w->on_close = br_close;
    w->tick = br_tick;
    b->win = w;
    browsers[slot] = w;
    navigate(b, url ? url : "sartax:start", true);
}

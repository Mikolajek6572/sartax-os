// Sartax OS - drzewo DOM: parser HTML, atrybuty, innerHTML, selektory CSS
#include "web.h"

#define MAX_NODES 200000
static int node_count;

static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f'; }

static bool starts_ci(const char *s, const char *end, const char *pre) {
    for (; *pre; pre++, s++)
        if (s >= end || lower(*s) != *pre) return false;
    return true;
}

dnode_t *dom_new(int type, const char *tag) {
    dnode_t *n = kzalloc(sizeof(dnode_t));
    n->type = type;
    if (tag) strlcpy(n->tag, tag, sizeof n->tag);
    node_count++;
    return n;
}

void dom_append(dnode_t *p, dnode_t *c) {
    c->parent = p;
    c->next = NULL;
    c->prev = p->last;
    if (p->last) p->last->next = c; else p->first = c;
    p->last = c;
}

void dom_detach(dnode_t *n) {
    dnode_t *p = n->parent;
    if (!p) return;
    if (n->prev) n->prev->next = n->next; else p->first = n->next;
    if (n->next) n->next->prev = n->prev; else p->last = n->prev;
    n->parent = n->next = n->prev = NULL;
}

void dom_insert_before(dnode_t *p, dnode_t *c, dnode_t *ref) {
    if (!ref || ref->parent != p) { dom_append(p, c); return; }
    c->parent = p;
    c->next = ref;
    c->prev = ref->prev;
    if (ref->prev) ref->prev->next = c; else p->first = c;
    ref->prev = c;
}

// wezly z obiektem JS sa zwalniane razem ze srodowiskiem JS - tu tylko odcinamy
void dom_free(dnode_t *n) {
    if (!n) return;
    dnode_t *c = n->first;
    while (c) {
        dnode_t *nx = c->next;
        dom_free(c);
        c = nx;
    }
    kfree(n->attrs);
    kfree(n->text);
    node_count--;
    kfree(n);
}

void dom_clear_children(dnode_t *n) {
    while (n->first) {
        dnode_t *c = n->first;
        dom_detach(c);
        // dziecko moze byc jeszcze trzymane przez JS - zostawiamy je wtedy zywe (odlaczone)
        if (!c->js) dom_free(c);
    }
}

bool dom_is_void(const char *n) {
    static const char *v[] = { "input", "img", "br", "hr", "meta", "link", "source", "wbr", "area", "col", "embed", "base", "param", "track" };
    for (int i = 0; i < (int)ARRAY_LEN(v); i++)
        if (!strcmp(n, v[i])) return true;
    return false;
}

static bool is_raw(const char *n) {
    return !strcmp(n, "script") || !strcmp(n, "style") || !strcmp(n, "textarea") || !strcmp(n, "title") || !strcmp(n, "xmp");
}

// ---------------- encje ----------------
static const struct { const char *n; u32 cp; } ent[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
    { "copy", 0xA9 }, { "reg", 0xAE }, { "raquo", 0xBB }, { "laquo", 0xAB }, { "mdash", 0x2014 }, { "ndash", 0x2013 },
    { "hellip", 0x2026 }, { "bull", 0x2022 }, { "middot", 0xB7 }, { "deg", 0xB0 }, { "euro", 0x20AC }, { "times", 0xD7 },
    { "rarr", 0x2192 }, { "larr", 0x2190 }, { "uarr", 0x2191 }, { "darr", 0x2193 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
    { "bdquo", 0x201E }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "oacute", 0xF3 }, { "Oacute", 0xD3 }, { "eacute", 0xE9 },
    { "aacute", 0xE1 }, { "uuml", 0xFC }, { "ouml", 0xF6 }, { "auml", 0xE4 }, { "szlig", 0xDF }, { "sect", 0xA7 },
    { "shy", 0 }, { "zwj", 0 }, { "zwnj", 0 }, { "thinsp", ' ' }, { "ensp", ' ' }, { "emsp", ' ' }, { "star", 0x2605 },
    { "trade", 0x2122 }, { "minus", 0x2212 }, { "le", 0x2264 }, { "ge", 0x2265 }, { "ne", 0x2260 }, { "infin", 0x221E },
};

static char *decode(const char *s, int n) {
    char *out = kmalloc(n * 2 + 8);
    int o = 0;
    for (int i = 0; i < n;) {
        if (s[i] == '&') {
            int j = i + 1;
            while (j < n && j - i < 12 && s[j] != ';' && !is_space(s[j]) && s[j] != '&' && s[j] != '<') j++;
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
                    if (cp > 0x10FFFF) cp = '?';
                } else {
                    for (int e = 0; e < (int)ARRAY_LEN(ent); e++)
                        if ((int)strlen(ent[e].n) == j - i - 1 && !strncmp(ent[e].n, s + i + 1, j - i - 1)) { cp = ent[e].cp; break; }
                }
                if (cp != 0xFFFFFFFF) {
                    if (cp) o += utf8_encode(cp, out + o);
                    i = j + 1;
                    continue;
                }
            }
        }
        out[o++] = s[i++];
    }
    out[o] = 0;
    return out;
}

// ---------------- atrybuty ----------------
// iteracja po ' nazwa="wartosc"' (wartosci moga byc w ' albo " albo bez cudzyslowu)
typedef struct { const char *name; int nlen; const char *val; int vlen; } attr_t;

static const char *next_attr(const char *p, attr_t *a) {
    while (*p && (is_space(*p) || *p == '/')) p++;
    if (!*p) return NULL;
    a->name = p;
    while (*p && !is_space(*p) && *p != '=' && *p != '/' && *p != '>') p++;
    a->nlen = p - a->name;
    while (is_space(*p)) p++;
    a->val = "";
    a->vlen = 0;
    if (*p == '=') {
        p++;
        while (is_space(*p)) p++;
        char q = 0;
        if (*p == '"' || *p == '\'') q = *p++;
        a->val = p;
        while (*p && (q ? *p != q : !is_space(*p))) p++;
        a->vlen = p - a->val;
        if (q && *p) p++;
    }
    if (!a->nlen) return *p ? p + 1 : NULL;
    return p;
}

static bool name_eq(const attr_t *a, const char *name) {
    int l = strlen(name);
    if (a->nlen != l) return false;
    for (int i = 0; i < l; i++)
        if (lower(a->name[i]) != lower(name[i])) return false;
    return true;
}

bool dom_get_attr(dnode_t *n, const char *name, char *out, int cap) {
    out[0] = 0;
    if (!n || !n->attrs) return false;
    attr_t a;
    for (const char *p = n->attrs; (p = next_attr(p, &a));) {
        if (name_eq(&a, name)) {
            char *d = decode(a.val, a.vlen);
            strlcpy(out, d, cap);
            kfree(d);
            return true;
        }
    }
    return false;
}

char *dom_get_attr_dup(dnode_t *n, const char *name) {
    if (!n || !n->attrs) return NULL;
    attr_t a;
    for (const char *p = n->attrs; (p = next_attr(p, &a));)
        if (name_eq(&a, name)) return decode(a.val, a.vlen);
    return NULL;
}

static void append(char **buf, int *len, int *cap, const char *s, int n) {
    if (*len + n + 1 > *cap) {
        *cap = (*len + n + 1) * 2 + 64;
        *buf = krealloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
}

static void append_escaped(char **buf, int *len, int *cap, const char *s, bool attr) {
    for (; *s; s++) {
        if (*s == '&') append(buf, len, cap, "&amp;", 5);
        else if (*s == '<') append(buf, len, cap, "&lt;", 4);
        else if (*s == '>' && !attr) append(buf, len, cap, "&gt;", 4);
        else if (*s == '"' && attr) append(buf, len, cap, "&quot;", 6);
        else append(buf, len, cap, s, 1);
    }
}

void dom_set_attr(dnode_t *n, const char *name, const char *val) {
    char *buf = NULL;
    int len = 0, cap = 0;
    append(&buf, &len, &cap, "", 0);
    bool done = false;
    attr_t a;
    if (n->attrs)
        for (const char *p = n->attrs; (p = next_attr(p, &a));) {
            append(&buf, &len, &cap, " ", 1);
            append(&buf, &len, &cap, a.name, a.nlen);
            if (name_eq(&a, name)) {
                append(&buf, &len, &cap, "=\"", 2);
                append_escaped(&buf, &len, &cap, val, true);
                append(&buf, &len, &cap, "\"", 1);
                done = true;
            } else {
                append(&buf, &len, &cap, "=\"", 2);
                // przepisz wartosc w "", zamieniajac " na &quot;
                for (int i = 0; i < a.vlen; i++) {
                    if (a.val[i] == '"') append(&buf, &len, &cap, "&quot;", 6);
                    else append(&buf, &len, &cap, a.val + i, 1);
                }
                append(&buf, &len, &cap, "\"", 1);
            }
        }
    if (!done) {
        append(&buf, &len, &cap, " ", 1);
        append(&buf, &len, &cap, name, strlen(name));
        append(&buf, &len, &cap, "=\"", 2);
        append_escaped(&buf, &len, &cap, val, true);
        append(&buf, &len, &cap, "\"", 1);
    }
    kfree(n->attrs);
    n->attrs = buf;
}

void dom_remove_attr(dnode_t *n, const char *name) {
    if (!n->attrs) return;
    char *buf = NULL;
    int len = 0, cap = 0;
    append(&buf, &len, &cap, "", 0);
    attr_t a;
    for (const char *p = n->attrs; (p = next_attr(p, &a));) {
        if (name_eq(&a, name)) continue;
        append(&buf, &len, &cap, " ", 1);
        append(&buf, &len, &cap, a.name, a.nlen);
        append(&buf, &len, &cap, "=\"", 2);
        for (int i = 0; i < a.vlen; i++) {
            if (a.val[i] == '"') append(&buf, &len, &cap, "&quot;", 6);
            else append(&buf, &len, &cap, a.val + i, 1);
        }
        append(&buf, &len, &cap, "\"", 1);
    }
    kfree(n->attrs);
    n->attrs = buf;
}

bool dom_class_has(dnode_t *n, const char *cls) {
    char *c = dom_get_attr_dup(n, "class");
    if (!c) return false;
    int l = strlen(cls);
    bool r = false;
    for (char *p = c; *p;) {
        while (is_space(*p)) p++;
        char *s = p;
        while (*p && !is_space(*p)) p++;
        if (p - s == l && !strncmp(s, cls, l)) { r = true; break; }
    }
    kfree(c);
    return r;
}

// ---------------- parser ----------------
static bool in_scope(dnode_t *cur, const char *tag, const char *stop1, const char *stop2) {
    for (dnode_t *n = cur; n && n->type == DN_ELEM; n = n->parent) {
        if (!strcmp(n->tag, tag)) return true;
        if ((stop1 && !strcmp(n->tag, stop1)) || (stop2 && !strcmp(n->tag, stop2))) return false;
    }
    return false;
}

static dnode_t *close_tag(dnode_t *cur, const char *tag) {
    int depth = 0;
    for (dnode_t *n = cur; n && n->type == DN_ELEM && depth < 200; n = n->parent, depth++)
        if (!strcmp(n->tag, tag)) return n->parent;
    return cur;
}

static bool closes_p(const char *t) {
    static const char *b[] = { "p", "div", "ul", "ol", "table", "h1", "h2", "h3", "h4", "h5", "h6", "pre", "blockquote", "form",
                               "hr", "section", "article", "header", "footer", "nav", "aside", "main", "figure", "dl", "address",
                               "fieldset", "details", "menu" };
    for (int i = 0; i < (int)ARRAY_LEN(b); i++)
        if (!strcmp(t, b[i])) return true;
    return false;
}

static void add_text(dnode_t *cur, const char *s, int n, bool raw) {
    if (n <= 0) return;
    if (node_count > MAX_NODES) return;
    char *t;
    if (raw) {
        t = kmalloc(n + 1);
        memcpy(t, s, n);
        t[n] = 0;
    } else t = decode(s, n);
    if (cur->last && cur->last->type == DN_TEXT) {
        dnode_t *l = cur->last;
        size_t a = strlen(l->text), b = strlen(t);
        l->text = krealloc(l->text, a + b + 1);
        memcpy(l->text + a, t, b + 1);
        kfree(t);
        return;
    }
    dnode_t *tn = dom_new(DN_TEXT, "#text");
    tn->text = t;
    dom_append(cur, tn);
}

static void parse_into(dnode_t *root, const char *html, size_t n) {
    dnode_t *cur = root;
    const char *s = html, *end = html + n;
    while (s < end) {
        if (*s != '<') {
            const char *e = memchr(s, '<', end - s);
            if (!e) e = end;
            add_text(cur, s, e - s, false);
            s = e;
            continue;
        }
        if (starts_ci(s, end, "<!--")) {
            const char *e = NULL;
            for (const char *q = s + 4; q + 2 < end; q++)
                if (q[0] == '-' && q[1] == '-' && q[2] == '>') { e = q; break; }
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
            add_text(cur, s, 1, true);
            s++;
            continue;
        }
        char name[16];
        int nl = 0;
        while (q < end && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '-' || *q == ':')) {
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
        bool selfclose = alen > 0 && as[alen - 1] == '/';
        s = q < end ? q + 1 : end;
        if (close) {
            if (!strcmp(name, "p") && !in_scope(cur, "p", "div", "td")) continue;
            cur = close_tag(cur, name);
            continue;
        }
        // automatyczne domykanie
        if (closes_p(name) && in_scope(cur, "p", "div", "td")) cur = close_tag(cur, "p");
        if (!strcmp(name, "li") && in_scope(cur, "li", "ul", "ol")) cur = close_tag(cur, "li");
        if ((!strcmp(name, "dt") || !strcmp(name, "dd"))) {
            if (in_scope(cur, "dt", "dl", NULL)) cur = close_tag(cur, "dt");
            if (in_scope(cur, "dd", "dl", NULL)) cur = close_tag(cur, "dd");
        }
        if (!strcmp(name, "option") && in_scope(cur, "option", "select", NULL)) cur = close_tag(cur, "option");
        if (!strcmp(name, "tr")) {
            if (in_scope(cur, "td", "table", NULL)) cur = close_tag(cur, "td");
            if (in_scope(cur, "th", "table", NULL)) cur = close_tag(cur, "th");
            if (in_scope(cur, "tr", "table", NULL)) cur = close_tag(cur, "tr");
        }
        if (!strcmp(name, "td") || !strcmp(name, "th")) {
            if (in_scope(cur, "td", "tr", "table")) cur = close_tag(cur, "td");
            if (in_scope(cur, "th", "tr", "table")) cur = close_tag(cur, "th");
        }
        if (node_count > MAX_NODES) continue;
        dnode_t *el = dom_new(DN_ELEM, name);
        if (alen > 0) {
            el->attrs = kmalloc(alen + 2);
            el->attrs[0] = ' ';
            memcpy(el->attrs + 1, as, alen);
            el->attrs[alen + 1] = 0;
            if (selfclose) el->attrs[alen] = 0;
        }
        dom_append(cur, el);
        if (is_raw(name) && !selfclose) {
            // tekst do </nazwa>
            const char *e = s;
            int tl = strlen(name);
            while (e + tl + 2 < end && !(e[0] == '<' && e[1] == '/' && starts_ci(e + 2, end, name))) e++;
            if (e + tl + 2 >= end) e = end;
            bool rawtext = !strcmp(name, "script") || !strcmp(name, "style") || !strcmp(name, "xmp");
            add_text(el, s, e - s, rawtext);
            if (e < end) {
                const char *g = memchr(e, '>', end - e);
                s = g ? g + 1 : end;
            } else s = end;
            continue;
        }
        if (!dom_is_void(name) && !selfclose) cur = el;
    }
}

dnode_t *dom_parse(const char *html, size_t n) {
    dnode_t *doc = dom_new(DN_DOC, "#document");
    parse_into(doc, html, n);
    return doc;
}

void dom_parse_into(dnode_t *parent, const char *html, size_t n) { parse_into(parent, html, n); }

dnode_t *dom_clone(dnode_t *n, bool deep) {
    dnode_t *c = dom_new(n->type, n->tag);
    if (n->attrs) c->attrs = kstrdup(n->attrs);
    if (n->text) c->text = kstrdup(n->text);
    if (deep)
        for (dnode_t *k = n->first; k; k = k->next) dom_append(c, dom_clone(k, true));
    return c;
}

// ---------------- tekst i HTML ----------------
static void text_rec(dnode_t *n, char **buf, int *len, int *cap) {
    if (n->type == DN_TEXT) { append(buf, len, cap, n->text, strlen(n->text)); return; }
    for (dnode_t *c = n->first; c; c = c->next) text_rec(c, buf, len, cap);
}

char *dom_text_content(dnode_t *n) {
    char *buf = NULL;
    int len = 0, cap = 0;
    append(&buf, &len, &cap, "", 0);
    text_rec(n, &buf, &len, &cap);
    return buf;
}

void dom_set_text(dnode_t *n, const char *text) {
    if (n->type == DN_TEXT) {
        kfree(n->text);
        n->text = kstrdup(text);
        return;
    }
    dom_clear_children(n);
    if (*text) {
        dnode_t *t = dom_new(DN_TEXT, "#text");
        t->text = kstrdup(text);
        dom_append(n, t);
    }
}

static void html_rec(dnode_t *n, char **buf, int *len, int *cap, bool self) {
    if (n->type == DN_TEXT) {
        bool raw = n->parent && (!strcmp(n->parent->tag, "script") || !strcmp(n->parent->tag, "style"));
        if (raw) append(buf, len, cap, n->text, strlen(n->text));
        else append_escaped(buf, len, cap, n->text, false);
        return;
    }
    if (self && n->type == DN_ELEM) {
        append(buf, len, cap, "<", 1);
        append(buf, len, cap, n->tag, strlen(n->tag));
        if (n->attrs) append(buf, len, cap, n->attrs, strlen(n->attrs));
        append(buf, len, cap, ">", 1);
        if (dom_is_void(n->tag)) return;
    }
    for (dnode_t *c = n->first; c; c = c->next) html_rec(c, buf, len, cap, true);
    if (self && n->type == DN_ELEM) {
        append(buf, len, cap, "</", 2);
        append(buf, len, cap, n->tag, strlen(n->tag));
        append(buf, len, cap, ">", 1);
    }
}

char *dom_inner_html(dnode_t *n, bool outer) {
    char *buf = NULL;
    int len = 0, cap = 0;
    append(&buf, &len, &cap, "", 0);
    html_rec(n, &buf, &len, &cap, outer);
    return buf;
}

// ---------------- wyszukiwanie ----------------
static dnode_t *next_in_tree(dnode_t *n, dnode_t *root) {
    if (n->first) return n->first;
    while (n && n != root) {
        if (n->next) return n->next;
        n = n->parent;
    }
    return NULL;
}

dnode_t *dom_find_tag(dnode_t *root, const char *tag) {
    for (dnode_t *n = root; n; n = next_in_tree(n, root))
        if (n->type == DN_ELEM && !strcmp(n->tag, tag)) return n;
    return NULL;
}

dnode_t *dom_by_id(dnode_t *root, const char *id) {
    char v[256];
    for (dnode_t *n = root; n; n = next_in_tree(n, root))
        if (n->type == DN_ELEM && n->attrs && dom_get_attr(n, "id", v, sizeof v) && !strcmp(v, id)) return n;
    return NULL;
}

// ---------------- selektory CSS ----------------
// obslugiwane: tag, *, #id, .klasa, [atr], [atr=wart], [atr^=..], [atr$=..], [atr*=..], :first-child, :last-child,
// laczenie bez spacji, potomek (spacja), dziecko (>), lista (przecinek)
static bool match_compound(dnode_t *n, const char *s, int len) {
    if (!n || n->type != DN_ELEM) return false;
    int i = 0;
    char buf[128];
    if (i < len && s[i] != '#' && s[i] != '.' && s[i] != '[' && s[i] != ':') {
        int k = 0;
        while (i < len && s[i] != '#' && s[i] != '.' && s[i] != '[' && s[i] != ':' && k < 127) buf[k++] = lower(s[i++]);
        buf[k] = 0;
        if (strcmp(buf, "*") && strcmp(buf, n->tag)) return false;
    }
    while (i < len) {
        char t = s[i++];
        int k = 0;
        if (t == '[') {
            while (i < len && s[i] != ']' && k < 127) buf[k++] = s[i++];
            buf[k] = 0;
            i++;
            char *eq = strchr(buf, '=');
            char op = 0;
            if (eq) {
                if (eq > buf && strchr("^$*~|", eq[-1])) { op = eq[-1]; eq[-1] = 0; }
                *eq = 0;
            }
            char *v = dom_get_attr_dup(n, buf);
            if (!v) return false;
            bool ok = true;
            if (eq) {
                char *want = eq + 1;
                int wl = strlen(want);
                if (wl >= 2 && (want[0] == '"' || want[0] == '\'')) { want++; wl -= 2; want[wl] = 0; }
                int vl = strlen(v);
                if (!op) ok = !strcmp(v, want);
                else if (op == '^') ok = !strncmp(v, want, wl);
                else if (op == '$') ok = vl >= wl && !strcmp(v + vl - wl, want);
                else if (op == '*') ok = strstr(v, want) != NULL;
                else if (op == '~') ok = dom_class_has(n, want) || !strcmp(v, want);
                else ok = !strncmp(v, want, wl);
            }
            kfree(v);
            if (!ok) return false;
            continue;
        }
        while (i < len && s[i] != '#' && s[i] != '.' && s[i] != '[' && s[i] != ':' && k < 127) buf[k++] = s[i++];
        buf[k] = 0;
        if (t == '#') {
            char id[128];
            if (!dom_get_attr(n, "id", id, sizeof id) || strcmp(id, buf)) return false;
        } else if (t == '.') {
            if (!dom_class_has(n, buf)) return false;
        } else if (t == ':') {
            if (!strcmp(buf, "first-child")) { dnode_t *p = n->prev; while (p && p->type != DN_ELEM) p = p->prev; if (p) return false; }
            else if (!strcmp(buf, "last-child")) { dnode_t *p = n->next; while (p && p->type != DN_ELEM) p = p->next; if (p) return false; }
            else if (!strcmp(buf, "checked")) { char c[8]; if (!dom_get_attr(n, "checked", c, sizeof c)) return false; }
            else if (strncmp(buf, "not", 3)) return false;   // inne pseudoklasy - brak dopasowania
        }
    }
    return true;
}

// dopasowanie jednego selektora (bez przecinkow), od prawej
static bool match_complex(dnode_t *n, const char *sel, int len) {
    while (len && is_space(sel[len - 1])) len--;
    while (len && is_space(*sel)) sel++, len--;
    if (!len) return false;
    // ostatni zlozony fragment
    int i = len;
    int depth = 0;
    while (i > 0) {
        char c = sel[i - 1];
        if (c == ']') depth++;
        else if (c == '[') depth--;
        if (!depth && (is_space(c) || c == '>' || c == '+' || c == '~')) break;
        i--;
    }
    if (!match_compound(n, sel + i, len - i)) return false;
    if (i == 0) return true;
    int j = i;
    while (j > 0 && is_space(sel[j - 1])) j--;
    char comb = ' ';
    if (j > 0 && (sel[j - 1] == '>' || sel[j - 1] == '+' || sel[j - 1] == '~')) { comb = sel[j - 1]; j--; }
    if (comb == '>') return n->parent && n->parent->type == DN_ELEM && match_complex(n->parent, sel, j);
    if (comb == '+' || comb == '~') {
        for (dnode_t *p = n->prev; p; p = p->prev) {
            if (p->type != DN_ELEM) continue;
            if (match_complex(p, sel, j)) return true;
            if (comb == '+') return false;
        }
        return false;
    }
    for (dnode_t *p = n->parent; p && p->type == DN_ELEM; p = p->parent)
        if (match_complex(p, sel, j)) return true;
    return false;
}

bool dom_matches(dnode_t *n, const char *selector) {
    const char *s = selector;
    while (*s) {
        const char *c = s;
        int depth = 0;
        while (*c && !(*c == ',' && !depth)) { if (*c == '[' || *c == '(') depth++; if (*c == ']' || *c == ')') depth--; c++; }
        if (match_complex(n, s, c - s)) return true;
        s = *c ? c + 1 : c;
    }
    return false;
}

int dom_query_all(dnode_t *root, const char *selector, dnode_t ***out, int max) {
    int cap = 16, cnt = 0;
    dnode_t **arr = kmalloc(cap * sizeof(dnode_t *));
    for (dnode_t *n = root->first ? root->first : NULL; n; n = next_in_tree(n, root)) {
        if (n->type != DN_ELEM || !dom_matches(n, selector)) continue;
        if (cnt == cap) { cap *= 2; arr = krealloc(arr, cap * sizeof(dnode_t *)); }
        arr[cnt++] = n;
        if (cnt >= max) break;
    }
    *out = arr;
    return cnt;
}

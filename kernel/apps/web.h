// Sartax OS - wspolne typy przegladarki: drzewo DOM, strona, JavaScript
#pragma once
#include "../kernel.h"
#include "../gfx.h"

// ---------------- DOM ----------------
enum { DN_ELEM = 1, DN_TEXT = 3, DN_DOC = 9 };

typedef struct dnode {
    u8 type;
    char tag[16];               // male litery
    char *attrs;                // ' nazwa="wartosc" ...' (kmalloc) albo NULL
    char *text;                 // tekst wezla tekstowego (zdekodowany, UTF-8)
    struct dnode *parent, *first, *last, *next, *prev;
    void *js;                   // obiekt JS (JSObject*), jesli powstal
    u32 flags;
} dnode_t;

dnode_t *dom_new(int type, const char *tag);
dnode_t *dom_parse(const char *html, size_t n);                 // zwraca wezel dokumentu
void dom_parse_into(dnode_t *parent, const char *html, size_t n); // fragment (innerHTML)
void dom_free(dnode_t *n);                                      // rekurencyjnie (bez rodzica)
void dom_append(dnode_t *parent, dnode_t *child);
void dom_insert_before(dnode_t *parent, dnode_t *child, dnode_t *ref);
void dom_detach(dnode_t *n);
void dom_clear_children(dnode_t *n);
dnode_t *dom_clone(dnode_t *n, bool deep);
bool dom_get_attr(dnode_t *n, const char *name, char *out, int cap);
char *dom_get_attr_dup(dnode_t *n, const char *name);          // kfree; NULL gdy brak
void dom_set_attr(dnode_t *n, const char *name, const char *val);
void dom_remove_attr(dnode_t *n, const char *name);
char *dom_text_content(dnode_t *n);                            // kfree
void dom_set_text(dnode_t *n, const char *text);
char *dom_inner_html(dnode_t *n, bool outer);                  // kfree
bool dom_is_void(const char *tag);
dnode_t *dom_find_tag(dnode_t *root, const char *tag);
dnode_t *dom_by_id(dnode_t *root, const char *id);
bool dom_matches(dnode_t *n, const char *selector);
int dom_query_all(dnode_t *root, const char *selector, dnode_t ***out, int max);  // kfree(*out)
bool dom_class_has(dnode_t *n, const char *cls);

// ---------------- strona ----------------
enum { F_TEXT, F_BR, F_BLOCK, F_HR, F_LI, F_INDENT, F_IMG, F_INPUT, F_BUTTON, F_SPACE, F_MAIN };
enum { B_TEXT, B_HR, B_IMG, B_INPUT, B_BUTTON, B_ALT };
enum { AL_LEFT, AL_CENTER, AL_RIGHT };

typedef struct {
    u8 type, font, pre, align;
    u32 color;
    int text, len;
    int link;
    int val, w, h;
    dnode_t *node;
} frag_t;

typedef struct {
    int x, y, w, h;
    u8 kind, font;
    u32 color;
    int text, len;
    int link;
    int val;
    dnode_t *node;
} box_t;

typedef struct {
    int form;
    int type;           // 0 tekst, 1 ukryte, 2 przycisk
    char name[64];
    char value[256];
    dnode_t *node;
} field_t;

typedef struct {
    char *url;
    image_t *img;
    u8 state;           // 0 czeka, 1 gotowy, 2 blad
} pimg_t;

typedef struct js_page js_page_t;

typedef struct page {
    char *arena;
    int alen, acap;
    frag_t *frags;
    int nfrags, cfrags;
    int *links;
    int nlinks, clinks;
    field_t *fields;
    int nfields;
    char (*forms)[512];
    dnode_t *form_nodes[32];
    int nforms;
    pimg_t *imgs;
    int nimgs, cimgs;
    box_t *boxes;
    int nboxes, cboxes;
    int doc_h, laid_w;
    int main_prio, main_y;
    u32 bg, text_col, link_col;
    char title[128];
    // DOM + JavaScript
    dnode_t *doc;
    bool dirty;                 // DOM zmieniony - przerysuj
    char url[600];
    js_page_t *js;
    char nav_request[600];      // location.href = ...
    int nav_back;               // history.back()
    char *console;              // ostatnie linie konsoli
    int console_len;
} page_t;

void page_rerender(page_t *p);  // drzewo DOM -> fragmenty (browser.c)
void page_console(page_t *p, const char *kind, const char *msg);

// ---------------- JavaScript (kernel/js/jsdom.c) ----------------
void js_page_start(page_t *p);              // uruchamia skrypty strony
void js_page_free(page_t *p);
bool js_tick(page_t *p);                    // timery i obietnice; true = cos sie dzialo
bool js_dispatch(page_t *p, dnode_t *target, const char *type);   // true = preventDefault
bool js_dispatch_key(page_t *p, dnode_t *target, const char *type, const char *key, int code);
bool js_eval_url(page_t *p, const char *code);  // href="javascript:..."
bool js_enabled(void);
bool web_resolve_url(const char *base, const char *href, char *out, int cap);

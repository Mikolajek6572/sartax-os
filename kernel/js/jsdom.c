// Sartax OS - JavaScript na stronach: QuickJS + drzewo DOM przegladarki
#include "quickjs.h"
#include <stdlib.h>
#include "../kernel.h"
#include "../net.h"
#include "../ui.h"
#include "../apps/web.h"

struct js_page {
    JSRuntime *rt;
    JSContext *ctx;
    page_t *p;
    u64 deadline;
    dnode_t **wrapped;          // wezly z obiektem JS (do sprzatania)
    int nwrapped, cwrapped;
    struct { int id; JSValue fn; u64 due; u32 every; bool used; } timers[128];
    int next_timer;
    JSValue dispatch;           // __sxDispatch z bootstrapu
};

static JSClassID node_class;
static bool enabled = true;

bool js_enabled(void) { return enabled; }

static js_page_t *JP(JSContext *ctx) { return JS_GetContextOpaque(ctx); }
static page_t *PG(JSContext *ctx) { return JP(ctx)->p; }
static void dirty(JSContext *ctx) { PG(ctx)->dirty = true; }

static int interrupt_cb(JSRuntime *rt, void *opaque) {
    js_page_t *jp = opaque;
    return jp->deadline && ticks_ms() > jp->deadline;
}

static void arm(js_page_t *jp, u32 ms) {
    jp->deadline = ticks_ms() + ms;
    JS_UpdateStackTop(jp->rt);
}

static void report_exception(JSContext *ctx) {
    JSValue e = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, e);
    char line[600];
    snprintf(line, sizeof line, "%s", msg ? msg : "blad");
    if (msg) JS_FreeCString(ctx, msg);
    if (JS_IsObject(e)) {
        JSValue st = JS_GetPropertyStr(ctx, e, "stack");
        const char *s0 = JS_ToCString(ctx, st), *s = s0;
        while (s && (*s == ' ' || *s == '\n')) s++;
        if (s && s[0]) {
            const char *nl = strchr(s, '\n');
            char first[200];
            strlcpy(first, s, MIN((size_t)(nl ? nl - s + 1 : 199), sizeof first));
            strlcat(line, "  @ ", sizeof line);
            strlcat(line, first, sizeof line);
        }
        if (s0) JS_FreeCString(ctx, s0);
        JS_FreeValue(ctx, st);
    }
    JS_FreeValue(ctx, e);
    page_console(PG(ctx), "blad", line);
}

// ---------------- opakowania wezlow ----------------
static void node_finalizer(JSRuntime *rt, JSValue val) {
    dnode_t *n = JS_GetOpaque(val, node_class);
    if (n) n->js = NULL;
}

static JSValue wrap(JSContext *ctx, dnode_t *n) {
    if (!n) return JS_NULL;
    if (n->js) return JS_DupValue(ctx, JS_MKPTR(JS_TAG_OBJECT, n->js));
    JSValue o = JS_NewObjectClass(ctx, node_class);
    if (JS_IsException(o)) return o;
    JS_SetOpaque(o, n);
    n->js = JS_VALUE_GET_PTR(o);
    js_page_t *jp = JP(ctx);
    if (jp->nwrapped == jp->cwrapped) {
        jp->cwrapped = jp->cwrapped ? jp->cwrapped * 2 : 256;
        jp->wrapped = krealloc(jp->wrapped, jp->cwrapped * sizeof(dnode_t *));
    }
    jp->wrapped[jp->nwrapped++] = n;
    return JS_DupValue(ctx, o);       // jedna referencja zostaje przy wezle
}

static dnode_t *N(JSContext *ctx, JSValueConst v) { return JS_GetOpaque(v, node_class); }

static JSValue str(JSContext *ctx, const char *s) { return JS_NewString(ctx, s ? s : ""); }

static JSValue str_free(JSContext *ctx, char *s) {
    JSValue v = JS_NewString(ctx, s ? s : "");
    kfree(s);
    return v;
}

static char *arg_str(JSContext *ctx, JSValueConst v) {
    const char *s = JS_ToCString(ctx, v);
    char *r = kstrdup(s ? s : "");
    if (s) JS_FreeCString(ctx, s);
    return r;
}

static void lowercase(char *s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32; }

// ---------------- wlasciwosci wezla ----------------
#define GETTER(name) static JSValue name(JSContext *ctx, JSValueConst this)
#define SETTER(name) static JSValue name(JSContext *ctx, JSValueConst this, JSValueConst v)
#define METHOD(name) static JSValue name(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
#define NODE_OR_NULL dnode_t *n = N(ctx, this); if (!n) return JS_UNDEFINED

GETTER(g_nodeType) { NODE_OR_NULL; return JS_NewInt32(ctx, n->type); }
GETTER(g_nodeName) {
    NODE_OR_NULL;
    if (n->type != DN_ELEM) return str(ctx, n->tag);
    char t[16];
    strlcpy(t, n->tag, sizeof t);
    for (char *c = t; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    return str(ctx, t);
}
GETTER(g_localName) { NODE_OR_NULL; return str(ctx, n->tag); }
GETTER(g_parentNode) { NODE_OR_NULL; return wrap(ctx, n->parent); }
GETTER(g_parentElement) { NODE_OR_NULL; return n->parent && n->parent->type == DN_ELEM ? wrap(ctx, n->parent) : JS_NULL; }
GETTER(g_firstChild) { NODE_OR_NULL; return wrap(ctx, n->first); }
GETTER(g_lastChild) { NODE_OR_NULL; return wrap(ctx, n->last); }
GETTER(g_nextSibling) { NODE_OR_NULL; return wrap(ctx, n->next); }
GETTER(g_previousSibling) { NODE_OR_NULL; return wrap(ctx, n->prev); }
GETTER(g_firstElementChild) { NODE_OR_NULL; dnode_t *c = n->first; while (c && c->type != DN_ELEM) c = c->next; return wrap(ctx, c); }
GETTER(g_lastElementChild) { NODE_OR_NULL; dnode_t *c = n->last; while (c && c->type != DN_ELEM) c = c->prev; return wrap(ctx, c); }
GETTER(g_nextElementSibling) { NODE_OR_NULL; dnode_t *c = n->next; while (c && c->type != DN_ELEM) c = c->next; return wrap(ctx, c); }
GETTER(g_previousElementSibling) { NODE_OR_NULL; dnode_t *c = n->prev; while (c && c->type != DN_ELEM) c = c->prev; return wrap(ctx, c); }

static JSValue children_array(JSContext *ctx, dnode_t *n, bool elems) {
    JSValue a = JS_NewArray(ctx);
    u32 i = 0;
    for (dnode_t *c = n->first; c; c = c->next)
        if (!elems || c->type == DN_ELEM) JS_SetPropertyUint32(ctx, a, i++, wrap(ctx, c));
    return a;
}
GETTER(g_childNodes) { NODE_OR_NULL; return children_array(ctx, n, false); }
GETTER(g_children) { NODE_OR_NULL; return children_array(ctx, n, true); }
GETTER(g_childElementCount) { NODE_OR_NULL; int k = 0; for (dnode_t *c = n->first; c; c = c->next) k += c->type == DN_ELEM; return JS_NewInt32(ctx, k); }

GETTER(g_textContent) { NODE_OR_NULL; return str_free(ctx, dom_text_content(n)); }
SETTER(s_textContent) {
    NODE_OR_NULL;
    char *t = arg_str(ctx, v);
    dom_set_text(n, t);
    kfree(t);
    dirty(ctx);
    return JS_UNDEFINED;
}
GETTER(g_innerHTML) { NODE_OR_NULL; return str_free(ctx, dom_inner_html(n, false)); }
SETTER(s_innerHTML) {
    NODE_OR_NULL;
    char *t = arg_str(ctx, v);
    dom_clear_children(n);
    dom_parse_into(n, t, strlen(t));
    kfree(t);
    dirty(ctx);
    return JS_UNDEFINED;
}
GETTER(g_outerHTML) { NODE_OR_NULL; return str_free(ctx, dom_inner_html(n, true)); }

static JSValue attr_get(JSContext *ctx, dnode_t *n, const char *name) {
    char *v = dom_get_attr_dup(n, name);
    return str_free(ctx, v);
}
static void attr_set(JSContext *ctx, dnode_t *n, const char *name, JSValueConst v) {
    char *t = arg_str(ctx, v);
    dom_set_attr(n, name, t);
    kfree(t);
    dirty(ctx);
}
GETTER(g_id) { NODE_OR_NULL; return attr_get(ctx, n, "id"); }
SETTER(s_id) { NODE_OR_NULL; attr_set(ctx, n, "id", v); return JS_UNDEFINED; }
GETTER(g_className) { NODE_OR_NULL; return attr_get(ctx, n, "class"); }
SETTER(s_className) { NODE_OR_NULL; attr_set(ctx, n, "class", v); return JS_UNDEFINED; }
GETTER(g_value) {
    NODE_OR_NULL;
    if (!strcmp(n->tag, "textarea")) {
        char *a = dom_get_attr_dup(n, "value");
        if (a) return str_free(ctx, a);
        return str_free(ctx, dom_text_content(n));
    }
    if (!strcmp(n->tag, "select")) {
        // zaznaczona albo pierwsza opcja
        dnode_t **opts;
        int k = dom_query_all(n, "option", &opts, 500);
        dnode_t *sel = k ? opts[0] : NULL;
        for (int i = 0; i < k; i++) { char x[4]; if (dom_get_attr(opts[i], "selected", x, sizeof x)) sel = opts[i]; }
        kfree(opts);
        if (!sel) return str(ctx, "");
        char *val = dom_get_attr_dup(sel, "value");
        return val ? str_free(ctx, val) : str_free(ctx, dom_text_content(sel));
    }
    return attr_get(ctx, n, "value");
}
SETTER(s_value) { NODE_OR_NULL; attr_set(ctx, n, "value", v); return JS_UNDEFINED; }
GETTER(g_checked) { NODE_OR_NULL; char x[8]; return JS_NewBool(ctx, dom_get_attr(n, "checked", x, sizeof x)); }
SETTER(s_checked) {
    NODE_OR_NULL;
    if (JS_ToBool(ctx, v)) dom_set_attr(n, "checked", "checked");
    else dom_remove_attr(n, "checked");
    dirty(ctx);
    return JS_UNDEFINED;
}
GETTER(g_nodeValue) { NODE_OR_NULL; return n->type == DN_TEXT ? str(ctx, n->text) : JS_NULL; }
SETTER(s_nodeValue) {
    NODE_OR_NULL;
    if (n->type != DN_TEXT) return JS_UNDEFINED;
    char *t = arg_str(ctx, v);
    kfree(n->text);
    n->text = t;
    dirty(ctx);
    return JS_UNDEFINED;
}
static JSValue url_attr(JSContext *ctx, dnode_t *n, const char *name) {
    char *a = dom_get_attr_dup(n, name);
    if (!a) return str(ctx, "");
    char out[900];
    if (web_resolve_url(PG(ctx)->url, a, out, sizeof out)) { kfree(a); return str(ctx, out); }
    return str_free(ctx, a);
}
GETTER(g_href) { NODE_OR_NULL; return url_attr(ctx, n, "href"); }
SETTER(s_href) { NODE_OR_NULL; attr_set(ctx, n, "href", v); return JS_UNDEFINED; }
GETTER(g_src) { NODE_OR_NULL; return url_attr(ctx, n, "src"); }
SETTER(s_src) { NODE_OR_NULL; attr_set(ctx, n, "src", v); return JS_UNDEFINED; }
GETTER(g_isConnected) {
    NODE_OR_NULL;
    dnode_t *r = n;
    while (r->parent) r = r->parent;
    return JS_NewBool(ctx, r == PG(ctx)->doc);
}

// ---------------- metody wezla ----------------
METHOD(m_getAttribute) {
    NODE_OR_NULL;
    char *name = arg_str(ctx, argv[0]);
    lowercase(name);
    char *v = dom_get_attr_dup(n, name);
    kfree(name);
    if (!v) return JS_NULL;
    return str_free(ctx, v);
}
METHOD(m_setAttribute) {
    NODE_OR_NULL;
    if (argc < 2) return JS_UNDEFINED;
    char *name = arg_str(ctx, argv[0]);
    lowercase(name);
    attr_set(ctx, n, name, argv[1]);
    kfree(name);
    return JS_UNDEFINED;
}
METHOD(m_removeAttribute) {
    NODE_OR_NULL;
    char *name = arg_str(ctx, argv[0]);
    lowercase(name);
    dom_remove_attr(n, name);
    kfree(name);
    dirty(ctx);
    return JS_UNDEFINED;
}
METHOD(m_hasAttribute) {
    NODE_OR_NULL;
    char *name = arg_str(ctx, argv[0]);
    lowercase(name);
    char *v = dom_get_attr_dup(n, name);
    kfree(name);
    bool r = v != NULL;
    kfree(v);
    return JS_NewBool(ctx, r);
}

static bool is_ancestor(dnode_t *a, dnode_t *n) {
    for (; n; n = n->parent)
        if (n == a) return true;
    return false;
}

// fragment dokumentu: przenies jego dzieci
static void insert_node(dnode_t *parent, dnode_t *c, dnode_t *ref) {
    if (!strcmp(c->tag, "#fragment")) {
        while (c->first) {
            dnode_t *k = c->first;
            dom_detach(k);
            dom_insert_before(parent, k, ref);
        }
        return;
    }
    dom_detach(c);
    dom_insert_before(parent, c, ref);
}

METHOD(m_appendChild) {
    NODE_OR_NULL;
    dnode_t *c = argc ? N(ctx, argv[0]) : NULL;
    if (!c || is_ancestor(c, n)) return JS_ThrowTypeError(ctx, "appendChild: zly wezel");
    insert_node(n, c, NULL);
    dirty(ctx);
    return JS_DupValue(ctx, argv[0]);
}
METHOD(m_insertBefore) {
    NODE_OR_NULL;
    dnode_t *c = argc ? N(ctx, argv[0]) : NULL;
    dnode_t *ref = argc > 1 ? N(ctx, argv[1]) : NULL;
    if (!c || is_ancestor(c, n)) return JS_ThrowTypeError(ctx, "insertBefore: zly wezel");
    insert_node(n, c, ref);
    dirty(ctx);
    return JS_DupValue(ctx, argv[0]);
}
METHOD(m_removeChild) {
    NODE_OR_NULL;
    dnode_t *c = argc ? N(ctx, argv[0]) : NULL;
    if (!c || c->parent != n) return JS_ThrowTypeError(ctx, "removeChild: to nie jest dziecko");
    dom_detach(c);
    dirty(ctx);
    return JS_DupValue(ctx, argv[0]);
}
METHOD(m_replaceChild) {
    NODE_OR_NULL;
    dnode_t *nw = argc ? N(ctx, argv[0]) : NULL, *old = argc > 1 ? N(ctx, argv[1]) : NULL;
    if (!nw || !old || old->parent != n) return JS_ThrowTypeError(ctx, "replaceChild: zly wezel");
    dnode_t *ref = old->next;
    dom_detach(old);
    insert_node(n, nw, ref);
    dirty(ctx);
    return JS_DupValue(ctx, argv[1]);
}
METHOD(m_remove) {
    NODE_OR_NULL;
    if (n->parent) { dom_detach(n); dirty(ctx); }
    return JS_UNDEFINED;
}
METHOD(m_cloneNode) {
    NODE_OR_NULL;
    bool deep = argc && JS_ToBool(ctx, argv[0]);
    return wrap(ctx, dom_clone(n, deep));
}
METHOD(m_contains) {
    NODE_OR_NULL;
    dnode_t *c = argc ? N(ctx, argv[0]) : NULL;
    return JS_NewBool(ctx, c && is_ancestor(n, c));
}
METHOD(m_hasChildNodes) { NODE_OR_NULL; return JS_NewBool(ctx, n->first != NULL); }

static JSValue query(JSContext *ctx, dnode_t *n, JSValueConst sel, int max) {
    char *s = arg_str(ctx, sel);
    dnode_t **res;
    int k = dom_query_all(n, s, &res, max);
    kfree(s);
    JSValue r;
    if (max == 1) r = k ? wrap(ctx, res[0]) : JS_NULL;
    else {
        r = JS_NewArray(ctx);
        for (int i = 0; i < k; i++) JS_SetPropertyUint32(ctx, r, i, wrap(ctx, res[i]));
    }
    kfree(res);
    return r;
}
METHOD(m_querySelector) { NODE_OR_NULL; return argc ? query(ctx, n, argv[0], 1) : JS_NULL; }
METHOD(m_querySelectorAll) { NODE_OR_NULL; return argc ? query(ctx, n, argv[0], 100000) : JS_NewArray(ctx); }
METHOD(m_getElementsByTagName) { NODE_OR_NULL; return argc ? query(ctx, n, argv[0], 100000) : JS_NewArray(ctx); }
METHOD(m_getElementsByClassName) {
    NODE_OR_NULL;
    if (!argc) return JS_NewArray(ctx);
    char *c = arg_str(ctx, argv[0]);
    char sel[300] = "";
    // "a b" -> ".a.b"
    for (char *p = c; *p;) {
        while (*p == ' ') p++;
        if (!*p) break;
        char *e = p;
        while (*e && *e != ' ') e++;
        strlcat(sel, ".", sizeof sel);
        char tmp[100];
        strlcpy(tmp, p, MIN((size_t)(e - p + 1), sizeof tmp));
        strlcat(sel, tmp, sizeof sel);
        p = e;
    }
    kfree(c);
    JSValue s = JS_NewString(ctx, sel);
    JSValue r = sel[0] ? query(ctx, n, s, 100000) : JS_NewArray(ctx);
    JS_FreeValue(ctx, s);
    return r;
}
METHOD(m_matches) {
    NODE_OR_NULL;
    if (!argc) return JS_FALSE;
    char *s = arg_str(ctx, argv[0]);
    bool r = n->type == DN_ELEM && dom_matches(n, s);
    kfree(s);
    return JS_NewBool(ctx, r);
}
METHOD(m_closest) {
    NODE_OR_NULL;
    if (!argc) return JS_NULL;
    char *s = arg_str(ctx, argv[0]);
    dnode_t *r = n;
    while (r && !(r->type == DN_ELEM && dom_matches(r, s))) r = r->parent;
    kfree(s);
    return wrap(ctx, r);
}
METHOD(m_insertAdjacentHTML) {
    NODE_OR_NULL;
    if (argc < 2) return JS_UNDEFINED;
    char *pos = arg_str(ctx, argv[0]);
    lowercase(pos);
    char *html = arg_str(ctx, argv[1]);
    dnode_t *frag = dom_new(DN_ELEM, "#fragment");
    dom_parse_into(frag, html, strlen(html));
    if (!strcmp(pos, "beforeend")) insert_node(n, frag, NULL);
    else if (!strcmp(pos, "afterbegin")) insert_node(n, frag, n->first);
    else if (!strcmp(pos, "beforebegin") && n->parent) insert_node(n->parent, frag, n);
    else if (!strcmp(pos, "afterend") && n->parent) insert_node(n->parent, frag, n->next);
    dom_free(frag);
    kfree(pos);
    kfree(html);
    dirty(ctx);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry node_proto[] = {
    JS_CGETSET_DEF("nodeType", g_nodeType, NULL),
    JS_CGETSET_DEF("nodeName", g_nodeName, NULL),
    JS_CGETSET_DEF("tagName", g_nodeName, NULL),
    JS_CGETSET_DEF("localName", g_localName, NULL),
    JS_CGETSET_DEF("parentNode", g_parentNode, NULL),
    JS_CGETSET_DEF("parentElement", g_parentElement, NULL),
    JS_CGETSET_DEF("firstChild", g_firstChild, NULL),
    JS_CGETSET_DEF("lastChild", g_lastChild, NULL),
    JS_CGETSET_DEF("nextSibling", g_nextSibling, NULL),
    JS_CGETSET_DEF("previousSibling", g_previousSibling, NULL),
    JS_CGETSET_DEF("firstElementChild", g_firstElementChild, NULL),
    JS_CGETSET_DEF("lastElementChild", g_lastElementChild, NULL),
    JS_CGETSET_DEF("nextElementSibling", g_nextElementSibling, NULL),
    JS_CGETSET_DEF("previousElementSibling", g_previousElementSibling, NULL),
    JS_CGETSET_DEF("childNodes", g_childNodes, NULL),
    JS_CGETSET_DEF("children", g_children, NULL),
    JS_CGETSET_DEF("childElementCount", g_childElementCount, NULL),
    JS_CGETSET_DEF("textContent", g_textContent, s_textContent),
    JS_CGETSET_DEF("innerText", g_textContent, s_textContent),
    JS_CGETSET_DEF("innerHTML", g_innerHTML, s_innerHTML),
    JS_CGETSET_DEF("outerHTML", g_outerHTML, NULL),
    JS_CGETSET_DEF("id", g_id, s_id),
    JS_CGETSET_DEF("className", g_className, s_className),
    JS_CGETSET_DEF("value", g_value, s_value),
    JS_CGETSET_DEF("checked", g_checked, s_checked),
    JS_CGETSET_DEF("nodeValue", g_nodeValue, s_nodeValue),
    JS_CGETSET_DEF("data", g_nodeValue, s_nodeValue),
    JS_CGETSET_DEF("href", g_href, s_href),
    JS_CGETSET_DEF("src", g_src, s_src),
    JS_CGETSET_DEF("isConnected", g_isConnected, NULL),
    JS_CFUNC_DEF("getAttribute", 1, m_getAttribute),
    JS_CFUNC_DEF("setAttribute", 2, m_setAttribute),
    JS_CFUNC_DEF("removeAttribute", 1, m_removeAttribute),
    JS_CFUNC_DEF("hasAttribute", 1, m_hasAttribute),
    JS_CFUNC_DEF("appendChild", 1, m_appendChild),
    JS_CFUNC_DEF("insertBefore", 2, m_insertBefore),
    JS_CFUNC_DEF("removeChild", 1, m_removeChild),
    JS_CFUNC_DEF("replaceChild", 2, m_replaceChild),
    JS_CFUNC_DEF("remove", 0, m_remove),
    JS_CFUNC_DEF("cloneNode", 1, m_cloneNode),
    JS_CFUNC_DEF("contains", 1, m_contains),
    JS_CFUNC_DEF("hasChildNodes", 0, m_hasChildNodes),
    JS_CFUNC_DEF("querySelector", 1, m_querySelector),
    JS_CFUNC_DEF("querySelectorAll", 1, m_querySelectorAll),
    JS_CFUNC_DEF("getElementsByTagName", 1, m_getElementsByTagName),
    JS_CFUNC_DEF("getElementsByClassName", 1, m_getElementsByClassName),
    JS_CFUNC_DEF("matches", 1, m_matches),
    JS_CFUNC_DEF("closest", 1, m_closest),
    JS_CFUNC_DEF("insertAdjacentHTML", 2, m_insertAdjacentHTML),
};

// ---------------- funkcje systemowe (__sx) ----------------
METHOD(f_log) {
    char *kind = argc > 0 ? arg_str(ctx, argv[0]) : kstrdup("log");
    char *msg = argc > 1 ? arg_str(ctx, argv[1]) : kstrdup("");
    page_console(PG(ctx), kind, msg);
    kfree(kind);
    kfree(msg);
    return JS_UNDEFINED;
}
METHOD(f_alert) {
    char *msg = argc ? arg_str(ctx, argv[0]) : kstrdup("");
    page_console(PG(ctx), "alert", msg);
    dlg_message(wm_focused(), "Strona mowi", msg, ICON_INFO);
    kfree(msg);
    return JS_UNDEFINED;
}
METHOD(f_navigate) {
    char *u = argc ? arg_str(ctx, argv[0]) : kstrdup("");
    strlcpy(PG(ctx)->nav_request, u, sizeof PG(ctx)->nav_request);
    kfree(u);
    return JS_UNDEFINED;
}
METHOD(f_back) { PG(ctx)->nav_back = 1; return JS_UNDEFINED; }
METHOD(f_url) { return str(ctx, PG(ctx)->url); }
METHOD(f_doc) { return wrap(ctx, PG(ctx)->doc); }
METHOD(f_byId) {
    if (!argc) return JS_NULL;
    char *id = arg_str(ctx, argv[0]);
    dnode_t *n = dom_by_id(PG(ctx)->doc, id);
    kfree(id);
    return wrap(ctx, n);
}
METHOD(f_createElement) {
    char *t = argc ? arg_str(ctx, argv[0]) : kstrdup("div");
    lowercase(t);
    dnode_t *n = dom_new(DN_ELEM, t);
    kfree(t);
    return wrap(ctx, n);
}
METHOD(f_createText) {
    char *t = argc ? arg_str(ctx, argv[0]) : kstrdup("");
    dnode_t *n = dom_new(DN_TEXT, "#text");
    n->text = t;
    return wrap(ctx, n);
}
METHOD(f_createFragment) { return wrap(ctx, dom_new(DN_ELEM, "#fragment")); }
METHOD(f_now) { return JS_NewFloat64(ctx, (double)ticks_ms()); }
METHOD(f_viewport) {
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "w", JS_NewInt32(ctx, g_scr_w - 40));
    JS_SetPropertyStr(ctx, o, "h", JS_NewInt32(ctx, g_scr_h - 200));
    return o;
}
METHOD(f_httpGet) {
    char *u = argc ? arg_str(ctx, argv[0]) : kstrdup("");
    char full[900];
    bool okr = web_resolve_url(PG(ctx)->url, u, full, sizeof full);
    if (!okr) strlcpy(full, u, sizeof full);
    kfree(u);
    js_page_t *jp = JP(ctx);
    u64 dl = jp->deadline;
    jp->deadline = 0;                 // pobieranie nie liczy sie do limitu czasu skryptu
    http_resp_t r;
    int rc = http_get(full, &r, NULL, NULL);
    jp->deadline = dl ? ticks_ms() + 3000 : 0;
    if (rc && !r.status) {
        JSValue e = JS_ThrowTypeError(ctx, "fetch: %s", r.error);
        http_free(&r);
        return e;
    }
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, r.status));
    JS_SetPropertyStr(ctx, o, "text", JS_NewStringLen(ctx, r.body ? (char *)r.body : "", r.len));
    JS_SetPropertyStr(ctx, o, "ctype", str(ctx, r.ctype));
    JS_SetPropertyStr(ctx, o, "url", str(ctx, r.final_url[0] ? r.final_url : full));
    http_free(&r);
    return o;
}
METHOD(f_setTimer) {
    js_page_t *jp = JP(ctx);
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) return JS_NewInt32(ctx, 0);
    int ms = 0, rep = 0;
    if (argc > 1) JS_ToInt32(ctx, &ms, argv[1]);
    if (argc > 2) rep = JS_ToBool(ctx, argv[2]);
    if (ms < 0) ms = 0;
    for (int i = 0; i < 128; i++) {
        if (jp->timers[i].used) continue;
        jp->timers[i].used = true;
        jp->timers[i].id = ++jp->next_timer;
        jp->timers[i].fn = JS_DupValue(ctx, argv[0]);
        jp->timers[i].due = ticks_ms() + ms;
        jp->timers[i].every = rep ? MAX(ms, 10) : 0;
        return JS_NewInt32(ctx, jp->timers[i].id);
    }
    return JS_NewInt32(ctx, 0);
}
METHOD(f_clearTimer) {
    js_page_t *jp = JP(ctx);
    int id = 0;
    if (argc) JS_ToInt32(ctx, &id, argv[0]);
    for (int i = 0; i < 128; i++)
        if (jp->timers[i].used && jp->timers[i].id == id) {
            JS_FreeValue(ctx, jp->timers[i].fn);
            jp->timers[i].used = false;
        }
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry sx_funcs[] = {
    JS_CFUNC_DEF("log", 2, f_log),
    JS_CFUNC_DEF("alert", 1, f_alert),
    JS_CFUNC_DEF("navigate", 1, f_navigate),
    JS_CFUNC_DEF("back", 0, f_back),
    JS_CFUNC_DEF("url", 0, f_url),
    JS_CFUNC_DEF("doc", 0, f_doc),
    JS_CFUNC_DEF("byId", 1, f_byId),
    JS_CFUNC_DEF("createElement", 1, f_createElement),
    JS_CFUNC_DEF("createText", 1, f_createText),
    JS_CFUNC_DEF("createFragment", 0, f_createFragment),
    JS_CFUNC_DEF("now", 0, f_now),
    JS_CFUNC_DEF("viewport", 0, f_viewport),
    JS_CFUNC_DEF("httpGet", 1, f_httpGet),
    JS_CFUNC_DEF("setTimer", 3, f_setTimer),
    JS_CFUNC_DEF("clearTimer", 1, f_clearTimer),
};

// ---------------- srodowisko przegladarki napisane w JS ----------------
static const char BOOT[] =
"(function(){\n"
"const sx = globalThis.__sx, P = globalThis.__nodeProto;\n"
"const G = globalThis;\n"
"G.window = G; G.self = G; G.top = G; G.parent = G; G.frames = G;\n"
"const fmt = a => Array.from(a).map(x => { if (typeof x === 'string') return x; try { return x instanceof Error ? x.message : JSON.stringify(x); } catch(e) { return String(x); } }).join(' ');\n"
"G.console = { log(){ sx.log('log', fmt(arguments)) }, info(){ sx.log('log', fmt(arguments)) }, debug(){ sx.log('log', fmt(arguments)) },\n"
"  warn(){ sx.log('warn', fmt(arguments)) }, error(){ sx.log('blad', fmt(arguments)) }, trace(){}, group(){}, groupEnd(){}, time(){}, timeEnd(){}, table(x){ sx.log('log', fmt([x])) }, assert(c,...m){ if(!c) sx.log('blad','Assertion: '+fmt(m)) } };\n"
"G.alert = m => sx.alert(String(m === undefined ? '' : m));\n"
"G.confirm = m => { sx.alert(String(m)); return true; };\n"
"G.prompt = () => null;\n"
"G.setTimeout = (f, ms, ...a) => sx.setTimer(typeof f === 'function' ? () => f(...a) : () => (0, eval)(String(f)), +ms || 0, false);\n"
"G.setInterval = (f, ms, ...a) => sx.setTimer(typeof f === 'function' ? () => f(...a) : () => (0, eval)(String(f)), +ms || 0, true);\n"
"G.clearTimeout = G.clearInterval = id => sx.clearTimer(id | 0);\n"
"G.requestAnimationFrame = f => sx.setTimer(() => f(sx.now()), 16, false);\n"
"G.cancelAnimationFrame = id => sx.clearTimer(id | 0);\n"
"G.queueMicrotask = f => Promise.resolve().then(f);\n"
"G.requestIdleCallback = f => sx.setTimer(() => f({ timeRemaining: () => 10, didTimeout: false }), 1, false);\n"
"G.performance = { now: () => sx.now(), mark(){}, measure(){}, getEntriesByName: () => [], timing: {} };\n"
"// --- zdarzenia ---\n"
"class Event { constructor(type, o) { o = o || {}; this.type = type; this.bubbles = !!o.bubbles; this.cancelable = o.cancelable !== false;\n"
"  this.defaultPrevented = false; this.target = null; this.currentTarget = null; this._stop = false; this._stopNow = false; this.timeStamp = sx.now(); this.isTrusted = false; }\n"
"  preventDefault() { if (this.cancelable) this.defaultPrevented = true; } stopPropagation() { this._stop = true; } stopImmediatePropagation() { this._stop = this._stopNow = true; }\n"
"  get returnValue() { return !this.defaultPrevented } set returnValue(v) { if (!v) this.preventDefault() }\n"
"  composedPath() { const p = []; for (let n = this.target; n; n = n.parentNode) p.push(n); p.push(G); return p; } }\n"
"G.Event = Event;\n"
"G.CustomEvent = class CustomEvent extends Event { constructor(t, o) { super(t, o); this.detail = o ? o.detail : null; } };\n"
"G.MouseEvent = class MouseEvent extends Event {}; G.KeyboardEvent = class KeyboardEvent extends Event {}; G.InputEvent = class InputEvent extends Event {};\n"
"G.FocusEvent = class FocusEvent extends Event {}; G.UIEvent = Event; G.PointerEvent = G.MouseEvent; G.SubmitEvent = Event;\n"
"function listeners(t, type) { const m = t.__ev || (t.__ev = {}); return m[type] || (m[type] = []); }\n"
"const ET = { addEventListener(type, f, o) { if (!f) return; const l = listeners(this, type); if (!l.some(x => x.f === f)) l.push({ f, once: !!(o && o.once) }); },\n"
"  removeEventListener(type, f) { const l = listeners(this, type); const i = l.findIndex(x => x.f === f); if (i >= 0) l.splice(i, 1); },\n"
"  dispatchEvent(e) { return fire(this, e); } };\n"
"Object.assign(P, ET); Object.assign(G, ET);\n"
"function callOne(t, e) {\n"
"  e.currentTarget = t;\n"
"  for (const x of listeners(t, e.type).slice()) { if (x.once) t.removeEventListener(e.type, x.f);\n"
"    try { if (typeof x.f === 'function') x.f.call(t, e); else if (x.f && x.f.handleEvent) x.f.handleEvent(e); } catch (err) { reportError(err); }\n"
"    if (e._stopNow) return; }\n"
"  let h = t['on' + e.type];\n"
"  if (h === undefined && t.getAttribute) { const code = t.getAttribute('on' + e.type); if (code) { try { h = new Function('event', code); } catch (err) { reportError(err); } } }\n"
"  if (typeof h === 'function') { try { if (h.call(t, e) === false) e.preventDefault(); } catch (err) { reportError(err); } }\n"
"}\n"
"function fire(target, e) { e.target = target; const path = [];\n"
"  for (let n = target; n; n = n.parentNode) path.push(n);\n"
"  if (target !== G) path.push(G);\n"
"  callOne(path[0], e);\n"
"  if (e.bubbles) for (let i = 1; i < path.length && !e._stop; i++) callOne(path[i], e);\n"
"  return !e.defaultPrevented; }\n"
"function reportError(err) { sx.log('blad', (err && err.message ? err.message : String(err)) + (err && err.stack ? '  @' + String(err.stack).split('\\n')[0] : '')); }\n"
"G.__sxDispatch = function(node, type, key, code) {\n"
"  const bubbles = !/^(focus|blur|load|mouseenter|mouseleave)$/.test(type);\n"
"  const E = key !== undefined ? G.KeyboardEvent : /click|mouse/.test(type) ? G.MouseEvent : Event;\n"
"  const e = new E(type, { bubbles, cancelable: true });\n"
"  e.isTrusted = true;\n"
"  if (key !== undefined) { e.key = key; e.keyCode = e.which = code; e.code = key.length === 1 ? 'Key' + key.toUpperCase() : key; }\n"
"  if (type === 'click') { e.button = 0; e.buttons = 1; e.clientX = e.clientY = e.pageX = e.pageY = 0; }\n"
"  fire(node, e); return e.defaultPrevented; };\n"
"// --- styl, klasy, dataset ---\n"
"const kebab = k => k.replace(/[A-Z]/g, m => '-' + m.toLowerCase());\n"
"const camel = k => k.replace(/-([a-z])/g, (_, c) => c.toUpperCase());\n"
"function parseStyle(s) { const m = {}; String(s || '').split(';').forEach(d => { const i = d.indexOf(':'); if (i > 0) m[d.slice(0, i).trim().toLowerCase()] = d.slice(i + 1).trim(); }); return m; }\n"
"function writeStyle(el, m) { el.setAttribute('style', Object.keys(m).map(k => k + ': ' + m[k]).join('; ')); }\n"
"Object.defineProperty(P, 'style', { get() { const el = this; return new Proxy({}, {\n"
"  get(_, k) { if (typeof k !== 'string') return undefined; const m = parseStyle(el.getAttribute('style'));\n"
"    if (k === 'cssText') return el.getAttribute('style') || ''; if (k === 'setProperty') return (a, b) => { m[a] = b; writeStyle(el, m); };\n"
"    if (k === 'getPropertyValue') return a => m[a] || ''; if (k === 'removeProperty') return a => { delete m[a]; writeStyle(el, m); };\n"
"    return m[kebab(k)] || ''; },\n"
"  set(_, k, v) { const m = parseStyle(el.getAttribute('style')); if (k === 'cssText') { el.setAttribute('style', String(v)); return true; }\n"
"    const kk = kebab(String(k)); if (v === '' || v == null) delete m[kk]; else m[kk] = String(v); writeStyle(el, m); return true; } }); },\n"
"  set(v) { this.setAttribute('style', String(v)); } });\n"
"Object.defineProperty(P, 'classList', { get() { const el = this; const list = () => (el.getAttribute('class') || '').split(/\\s+/).filter(Boolean);\n"
"  const save = l => el.setAttribute('class', l.join(' '));\n"
"  return { add(...c) { const l = list(); c.forEach(x => { if (!l.includes(x)) l.push(x); }); save(l); },\n"
"    remove(...c) { save(list().filter(x => !c.includes(x))); },\n"
"    toggle(c, f) { const l = list(); const has = l.includes(c); const want = f === undefined ? !has : !!f; if (want && !has) l.push(c); if (!want && has) l.splice(l.indexOf(c), 1); save(l); return want; },\n"
"    contains(c) { return list().includes(c); }, replace(a, b) { save(list().map(x => x === a ? b : x)); },\n"
"    item(i) { return list()[i] || null; }, get length() { return list().length; }, toString() { return el.getAttribute('class') || ''; },\n"
"    forEach(f) { list().forEach(f); }, [Symbol.iterator]() { return list()[Symbol.iterator](); } }; } });\n"
"Object.defineProperty(P, 'dataset', { get() { const el = this; return new Proxy({}, {\n"
"  get(_, k) { if (typeof k !== 'string') return undefined; const v = el.getAttribute('data-' + kebab(k)); return v === null ? undefined : v; },\n"
"  set(_, k, v) { el.setAttribute('data-' + kebab(String(k)), String(v)); return true; } }); } });\n"
"// --- dodatkowe metody elementow ---\n"
"P.append = function(...a) { a.forEach(x => this.appendChild(typeof x === 'object' ? x : sx.createText(String(x)))); };\n"
"P.prepend = function(...a) { const f = this.firstChild; a.forEach(x => this.insertBefore(typeof x === 'object' ? x : sx.createText(String(x)), f)); };\n"
"P.before = function(...a) { const p = this.parentNode; if (p) a.forEach(x => p.insertBefore(typeof x === 'object' ? x : sx.createText(String(x)), this)); };\n"
"P.after = function(...a) { const p = this.parentNode, n = this.nextSibling; if (p) a.forEach(x => p.insertBefore(typeof x === 'object' ? x : sx.createText(String(x)), n)); };\n"
"P.replaceWith = function(x) { const p = this.parentNode; if (p) p.replaceChild(typeof x === 'object' ? x : sx.createText(String(x)), this); };\n"
"P.replaceChildren = function(...a) { this.textContent = ''; this.append(...a); };\n"
"P.insertAdjacentElement = function(pos, el) { pos = pos.toLowerCase(); if (pos === 'beforeend') this.appendChild(el); else if (pos === 'afterbegin') this.insertBefore(el, this.firstChild); else if (pos === 'beforebegin') this.before(el); else this.after(el); return el; };\n"
"P.insertAdjacentText = function(pos, t) { this.insertAdjacentElement(pos, sx.createText(String(t))); };\n"
"P.click = function() { G.__sxDispatch(this, 'click'); };\n"
"P.focus = function() {}; P.blur = function() {}; P.scrollIntoView = function() {}; P.select = function() {};\n"
"P.getBoundingClientRect = function() { return { x: 0, y: 0, top: 0, left: 0, right: 100, bottom: 20, width: 100, height: 20 }; };\n"
"P.getClientRects = function() { return [this.getBoundingClientRect()]; };\n"
"for (const k of ['offsetWidth', 'clientWidth', 'scrollWidth']) Object.defineProperty(P, k, { get() { return 100; } });\n"
"for (const k of ['offsetHeight', 'clientHeight', 'scrollHeight']) Object.defineProperty(P, k, { get() { return 20; } });\n"
"for (const k of ['offsetTop', 'offsetLeft', 'scrollTop', 'scrollLeft']) Object.defineProperty(P, k, { get() { return 0; }, set() {} });\n"
"Object.defineProperty(P, 'ownerDocument', { get() { return G.document; } });\n"
"Object.defineProperty(P, 'hidden', { get() { return this.hasAttribute('hidden'); }, set(v) { if (v) this.setAttribute('hidden', ''); else this.removeAttribute('hidden'); } });\n"
"for (const a of ['title', 'name', 'type', 'alt', 'placeholder', 'target', 'rel', 'action', 'method', 'lang', 'dir', 'role', 'htmlFor', 'width', 'height', 'min', 'max', 'step', 'pattern', 'colSpan', 'rowSpan'])\n"
"  if (!(a in P)) Object.defineProperty(P, a, { get() { return this.getAttribute(a === 'htmlFor' ? 'for' : a.toLowerCase()) || ''; }, set(v) { this.setAttribute(a === 'htmlFor' ? 'for' : a.toLowerCase(), String(v)); } });\n"
"for (const a of ['disabled', 'readOnly', 'required', 'selected', 'multiple', 'autofocus', 'open'])\n"
"  Object.defineProperty(P, a, { get() { return this.hasAttribute(a.toLowerCase()); }, set(v) { if (v) this.setAttribute(a.toLowerCase(), ''); else this.removeAttribute(a.toLowerCase()); } });\n"
"Object.defineProperty(P, 'options', { get() { return this.querySelectorAll('option'); } });\n"
"Object.defineProperty(P, 'elements', { get() { return this.querySelectorAll('input,select,textarea,button'); } });\n"
"Object.defineProperty(P, 'form', { get() { return this.closest ? this.closest('form') : null; } });\n"
"P.submit = function() { G.__sxSubmit && G.__sxSubmit(this); }; P.reset = function() {};\n"
"P.attachShadow = function() { return this; };\n"
"P.getRootNode = function() { let n = this; while (n.parentNode) n = n.parentNode; return n; };\n"
"P.toString = function() { return '[object HTML' + (this.tagName || 'Unknown') + 'Element]'; };\n"
"P.animate = function() { return { finished: Promise.resolve(), cancel() {}, play() {}, pause() {} }; };\n"
"P.getContext = function() { return null; };\n"
"P.ELEMENT_NODE = 1; P.TEXT_NODE = 3; P.DOCUMENT_NODE = 9;\n"
"// --- document ---\n"
"const D = sx.doc();\n"
"G.document = D;\n"
"D.getElementById = id => sx.byId(String(id));\n"
"D.createElement = t => sx.createElement(String(t));\n"
"D.createElementNS = (ns, t) => sx.createElement(String(t));\n"
"D.createTextNode = t => sx.createText(String(t));\n"
"D.createComment = () => sx.createText('');\n"
"D.createDocumentFragment = () => sx.createFragment();\n"
"D.createEvent = () => new Event('');\n"
"D.createRange = () => ({ selectNodeContents() {}, setStart() {}, setEnd() {}, collapse() {}, createContextualFragment(h) { const f = sx.createFragment(); const d = sx.createElement('div'); d.innerHTML = h; while (d.firstChild) f.appendChild(d.firstChild); return f; } });\n"
"Object.defineProperty(D, 'documentElement', { get() { return D.querySelector('html') || D.firstElementChild; } });\n"
"Object.defineProperty(D, 'head', { get() { return D.querySelector('head'); } });\n"
"Object.defineProperty(D, 'body', { get() { let b = D.querySelector('body'); if (!b) { b = sx.createElement('body'); (D.documentElement || D).appendChild(b); } return b; } });\n"
"Object.defineProperty(D, 'title', { get() { const t = D.querySelector('title'); return t ? t.textContent : ''; }, set(v) { let t = D.querySelector('title'); if (!t) { t = sx.createElement('title'); (D.head || D.body).appendChild(t); } t.textContent = String(v); } });\n"
"Object.defineProperty(D, 'readyState', { get() { return G.__sxReady ? 'complete' : 'loading'; } });\n"
"Object.defineProperty(D, 'cookie', { get() { return G.__cookie || ''; }, set(v) { const kv = String(v).split(';')[0]; const m = {}; (G.__cookie || '').split('; ').filter(Boolean).forEach(x => { const i = x.indexOf('='); m[x.slice(0, i)] = x.slice(i + 1); }); const i = kv.indexOf('='); if (i > 0) m[kv.slice(0, i).trim()] = kv.slice(i + 1); G.__cookie = Object.keys(m).map(k => k + '=' + m[k]).join('; '); } });\n"
"Object.defineProperty(D, 'location', { get() { return G.location; }, set(v) { sx.navigate(String(v)); } });\n"
"Object.defineProperty(D, 'activeElement', { get() { return D.body; } });\n"
"Object.defineProperty(D, 'forms', { get() { return D.querySelectorAll('form'); } });\n"
"Object.defineProperty(D, 'images', { get() { return D.querySelectorAll('img'); } });\n"
"Object.defineProperty(D, 'links', { get() { return D.querySelectorAll('a[href]'); } });\n"
"Object.defineProperty(D, 'scripts', { get() { return D.querySelectorAll('script'); } });\n"
"Object.defineProperty(D, 'defaultView', { get() { return G; } });\n"
"D.visibilityState = 'visible'; D.hidden = false; D.characterSet = 'UTF-8'; D.compatMode = 'CSS1Compat'; D.referrer = '';\n"
"D.hasFocus = () => true;\n"
"D.write = D.writeln = (...a) => { const d = sx.createElement('div'); d.innerHTML = a.join(''); const b = D.body; while (d.firstChild) b.appendChild(d.firstChild); };\n"
"D.open = D.close = () => {};\n"
"D.execCommand = () => false;\n"
"D.getSelection = G.getSelection = () => ({ toString: () => '', removeAllRanges() {}, addRange() {}, rangeCount: 0 });\n"
"D.elementFromPoint = () => null;\n"
"Object.defineProperty(D, 'nodeName', { value: '#document' });\n"
"// --- location, history, navigator ---\n"
"function parseURL(u) { const m = /^([a-z]+:)\\/\\/([^\\/?#:]*)(:\\d+)?([^?#]*)(\\?[^#]*)?(#.*)?$/i.exec(u) || [];\n"
"  return { href: u, protocol: m[1] || '', hostname: m[2] || '', host: (m[2] || '') + (m[3] || ''), port: (m[3] || '').slice(1), pathname: m[4] || '/', search: m[5] || '', hash: m[6] || '', origin: (m[1] || '') + '//' + (m[2] || '') + (m[3] || '') }; }\n"
"G.location = new Proxy({}, { get(_, k) { const u = parseURL(sx.url());\n"
"    if (k === 'reload') return () => sx.navigate(sx.url()); if (k === 'assign' || k === 'replace') return v => sx.navigate(String(v));\n"
"    if (k === 'toString') return () => sx.url(); if (k === Symbol.toPrimitive) return () => sx.url(); return u[k]; },\n"
"  set(_, k, v) { if (k === 'href') sx.navigate(String(v)); else if (k === 'hash') {} else if (k === 'search') { const u = parseURL(sx.url()); sx.navigate(u.origin + u.pathname + (String(v)[0] === '?' ? v : '?' + v)); } return true; } });\n"
"G.history = { length: 1, state: null, back() { sx.back(); }, forward() {}, go(n) { if (n < 0) sx.back(); }, pushState(s) { this.state = s; }, replaceState(s) { this.state = s; } };\n"
"G.navigator = { userAgent: 'Mozilla/5.0 (Sartax OS; x86_64) SartaxBrowser/1.0', language: 'pl-PL', languages: ['pl-PL', 'pl', 'en'], platform: 'Sartax',\n"
"  onLine: true, cookieEnabled: true, vendor: 'Sartax', hardwareConcurrency: 1, maxTouchPoints: 0, doNotTrack: '1', webdriver: false,\n"
"  clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve('') }, sendBeacon: () => true, serviceWorker: undefined,\n"
"  geolocation: { getCurrentPosition(ok, err) { if (err) err({ code: 1, message: 'brak' }); } } };\n"
"const vp = sx.viewport();\n"
"G.innerWidth = G.outerWidth = vp.w; G.innerHeight = G.outerHeight = vp.h; G.devicePixelRatio = 1;\n"
"G.screen = { width: vp.w, height: vp.h, availWidth: vp.w, availHeight: vp.h, colorDepth: 32 };\n"
"G.scrollX = G.scrollY = G.pageXOffset = G.pageYOffset = 0;\n"
"G.scrollTo = G.scroll = G.scrollBy = () => {};\n"
"G.matchMedia = q => ({ matches: /prefers-color-scheme:\\s*light|min-width:\\s*(\\d+)/.test(q) && (!/min-width/.test(q) || +/min-width:\\s*(\\d+)/.exec(q)[1] <= vp.w), media: q, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} });\n"
"G.getComputedStyle = el => el && el.style ? new Proxy({}, { get(_, k) { if (k === 'getPropertyValue') return a => el.style.getPropertyValue(a); const v = el.style[k]; return v || (k === 'display' ? 'block' : ''); } }) : {};\n"
"G.focus = G.blur = G.print = G.stop = () => {};\n"
"G.open = u => { if (u) sx.navigate(String(u)); return null; };\n"
"G.close = () => {};\n"
"G.postMessage = () => {};\n"
"G.name = ''; G.closed = false; G.isSecureContext = true; G.origin = parseURL(sx.url()).origin;\n"
"// --- magazyn ---\n"
"function mkStorage() { const m = new Map(); return new Proxy({ getItem: k => m.has(String(k)) ? m.get(String(k)) : null, setItem: (k, v) => m.set(String(k), String(v)),\n"
"  removeItem: k => m.delete(String(k)), clear: () => m.clear(), key: i => Array.from(m.keys())[i] || null, get length() { return m.size; } },\n"
"  { get(t, k) { return k in t ? t[k] : (m.has(k) ? m.get(k) : undefined); }, set(t, k, v) { m.set(String(k), String(v)); return true; } }); }\n"
"G.localStorage = mkStorage(); G.sessionStorage = mkStorage();\n"
"// --- siec ---\n"
"class Headers { constructor(h) { this._h = {}; if (h) Object.keys(h).forEach(k => this._h[k.toLowerCase()] = h[k]); } get(k) { return this._h[String(k).toLowerCase()] || null; } has(k) { return String(k).toLowerCase() in this._h; } set(k, v) { this._h[String(k).toLowerCase()] = String(v); } append(k, v) { this.set(k, v); } forEach(f) { Object.keys(this._h).forEach(k => f(this._h[k], k)); } }\n"
"G.Headers = Headers;\n"
"class Response { constructor(body, o) { o = o || {}; this._t = body == null ? '' : String(body); this.status = o.status || 200; this.ok = this.status >= 200 && this.status < 300; this.statusText = ''; this.headers = new Headers(o.headers); this.url = o.url || ''; this.redirected = false; this.type = 'basic'; }\n"
"  text() { return Promise.resolve(this._t); } json() { return Promise.resolve().then(() => JSON.parse(this._t)); }\n"
"  blob() { return Promise.resolve({ size: this._t.length, type: this.headers.get('content-type') || '', text: () => Promise.resolve(this._t) }); } arrayBuffer() { return Promise.resolve(new ArrayBuffer(0)); } clone() { return new Response(this._t, { status: this.status, url: this.url }); } }\n"
"G.Response = Response;\n"
"G.Request = class Request { constructor(u, o) { this.url = String(u && u.url ? u.url : u); this.method = (o && o.method) || 'GET'; } };\n"
"G.fetch = (u, o) => new Promise((ok, err) => { try { const url = String(u && u.url ? u.url : u);\n"
"  if (o && o.method && o.method.toUpperCase() !== 'GET') { err(new TypeError('fetch: tylko GET')); return; }\n"
"  const r = sx.httpGet(url); ok(new Response(r.text, { status: r.status, headers: { 'content-type': r.ctype }, url: r.url })); } catch (e) { err(new TypeError(e.message)); } });\n"
"G.XMLHttpRequest = class XMLHttpRequest { constructor() { this.readyState = 0; this.status = 0; this.responseText = ''; this.response = ''; this.responseType = ''; this._h = {}; this.__ev = {}; }\n"
"  open(m, u) { this._m = m; this._u = u; this.readyState = 1; } setRequestHeader() {} getResponseHeader(k) { return String(k).toLowerCase() === 'content-type' ? this._ct : null; } getAllResponseHeaders() { return 'content-type: ' + (this._ct || ''); } abort() {} overrideMimeType() {}\n"
"  send() { setTimeout(() => { try { if (String(this._m).toUpperCase() !== 'GET') throw new Error('tylko GET'); const r = sx.httpGet(String(this._u)); this.status = r.status; this.responseText = r.text; this._ct = r.ctype; this.responseURL = r.url;\n"
"    this.response = this.responseType === 'json' ? (() => { try { return JSON.parse(r.text); } catch (e) { return null; } })() : r.text; } catch (e) { this.status = 0; }\n"
"    this.readyState = 4; const e = new Event('readystatechange'); if (this.onreadystatechange) this.onreadystatechange(e); fire(this, e);\n"
"    const t = this.status ? 'load' : 'error'; const e2 = new Event(t); if (this['on' + t]) this['on' + t](e2); fire(this, e2); const e3 = new Event('loadend'); if (this.onloadend) this.onloadend(e3); }, 0); }\n"
"  addEventListener(t, f) { listeners(this, t).push({ f }); } removeEventListener() {} };\n"
"Object.assign(G.XMLHttpRequest.prototype, { UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 });\n"
"G.WebSocket = class WebSocket { constructor() { setTimeout(() => { if (this.onerror) this.onerror(new Event('error')); if (this.onclose) this.onclose(new Event('close')); }, 0); } send() {} close() {} addEventListener() {} };\n"
"G.EventSource = G.WebSocket;\n"
"// --- rozne API ---\n"
"G.atob = s => { const c = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'; let o = '', b = 0, n = 0; for (const ch of String(s).replace(/[^A-Za-z0-9+\\/]/g, '')) { b = (b << 6) | c.indexOf(ch); n += 6; if (n >= 8) { n -= 8; o += String.fromCharCode((b >> n) & 255); } } return o; };\n"
"G.btoa = s => { const c = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'; s = String(s); let o = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), d = s.charCodeAt(i + 2);\n"
"  o += c[a >> 2] + c[((a & 3) << 4) | (b >> 4 || 0)] + (isNaN(b) ? '=' : c[((b & 15) << 2) | (d >> 6 || 0)]) + (isNaN(d) ? '=' : c[d & 63]); } return o; };\n"
"G.URLSearchParams = class URLSearchParams { constructor(q) { this._p = []; if (typeof q === 'string') q.replace(/^\\?/, '').split('&').filter(Boolean).forEach(kv => { const i = kv.indexOf('='); const d = x => decodeURIComponent(x.replace(/\\+/g, ' ')); this._p.push(i < 0 ? [d(kv), ''] : [d(kv.slice(0, i)), d(kv.slice(i + 1))]); }); else if (q) Object.keys(q).forEach(k => this._p.push([k, String(q[k])])); }\n"
"  get(k) { const e = this._p.find(x => x[0] === k); return e ? e[1] : null; } getAll(k) { return this._p.filter(x => x[0] === k).map(x => x[1]); } has(k) { return this._p.some(x => x[0] === k); }\n"
"  set(k, v) { this.delete(k); this._p.push([k, String(v)]); } append(k, v) { this._p.push([k, String(v)]); } delete(k) { this._p = this._p.filter(x => x[0] !== k); }\n"
"  forEach(f) { this._p.forEach(x => f(x[1], x[0])); } entries() { return this._p[Symbol.iterator](); } [Symbol.iterator]() { return this._p[Symbol.iterator](); } keys() { return this._p.map(x => x[0])[Symbol.iterator](); }\n"
"  toString() { return this._p.map(x => encodeURIComponent(x[0]) + '=' + encodeURIComponent(x[1])).join('&'); } };\n"
"G.URL = class URL { constructor(u, base) { u = String(u); if (base && !/^[a-z]+:/i.test(u)) { const b = parseURL(String(base)); u = u[0] === '/' ? b.origin + u : b.origin + b.pathname.replace(/[^\\/]*$/, '') + u; }\n"
"  Object.assign(this, parseURL(u)); this.searchParams = new G.URLSearchParams(this.search); } toString() { return this.href; } toJSON() { return this.href; } static createObjectURL() { return 'blob:sartax'; } static revokeObjectURL() {} };\n"
"G.TextEncoder = class TextEncoder { encode(s) { const u = unescape(encodeURIComponent(String(s))); const a = new Uint8Array(u.length); for (let i = 0; i < u.length; i++) a[i] = u.charCodeAt(i); return a; } };\n"
"G.TextDecoder = class TextDecoder { decode(a) { let s = ''; for (const b of (a || [])) s += String.fromCharCode(b); try { return decodeURIComponent(escape(s)); } catch (e) { return s; } } };\n"
"G.MutationObserver = class MutationObserver { observe() {} disconnect() {} takeRecords() { return []; } };\n"
"G.IntersectionObserver = class IntersectionObserver { constructor(cb) { this.cb = cb; } observe(el) { setTimeout(() => this.cb([{ isIntersecting: true, target: el, intersectionRatio: 1 }], this), 0); } unobserve() {} disconnect() {} };\n"
"G.ResizeObserver = class ResizeObserver { observe() {} unobserve() {} disconnect() {} };\n"
"G.Image = class Image { constructor() { const i = sx.createElement('img'); return i; } };\n"
"G.Audio = class Audio { play() { return Promise.resolve(); } pause() {} };\n"
"G.Node = { ELEMENT_NODE: 1, TEXT_NODE: 3, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_FRAGMENT_NODE: 11 };\n"
"G.Element = G.HTMLElement = G.HTMLDivElement = G.HTMLInputElement = G.HTMLFormElement = G.HTMLAnchorElement = G.HTMLImageElement = G.HTMLButtonElement = G.Text = G.DocumentFragment = function() {};\n"
"G.customElements = { define() {}, get() { return undefined; }, whenDefined: () => Promise.resolve() };\n"
"G.crypto = { getRandomValues(a) { for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 256); return a; }, randomUUID: () => 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => { const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); }) };\n"
"G.indexedDB = undefined; G.Notification = undefined; G.speechSynthesis = undefined;\n"
"G.structuredClone = v => JSON.parse(JSON.stringify(v));\n"
"G.FormData = class FormData { constructor(f) { this._p = []; if (f && f.querySelectorAll) f.querySelectorAll('input,textarea,select').forEach(e => { if (e.name) this._p.push([e.name, e.value]); }); } append(k, v) { this._p.push([k, String(v)]); } get(k) { const e = this._p.find(x => x[0] === k); return e ? e[1] : null; } entries() { return this._p[Symbol.iterator](); } };\n"
"})();\n";

// ---------------- start / koniec ----------------
static int run_code(js_page_t *jp, const char *code, size_t len, const char *name) {
    arm(jp, 5000);
    JSValue r = JS_Eval(jp->ctx, code, len, name, JS_EVAL_TYPE_GLOBAL);
    int ok = 0;
    if (JS_IsException(r)) { report_exception(jp->ctx); ok = -1; }
    JS_FreeValue(jp->ctx, r);
    jp->deadline = 0;
    return ok;
}

static void run_jobs(js_page_t *jp) {
    JSContext *c;
    arm(jp, 3000);
    for (int i = 0; i < 2000; i++) {
        int r = JS_ExecutePendingJob(jp->rt, &c);
        if (r <= 0) { if (r < 0) report_exception(c); if (!r) break; }
    }
    jp->deadline = 0;
}

static bool script_is_js(dnode_t *s) {
    char t[64];
    if (!dom_get_attr(s, "type", t, sizeof t) || !t[0]) return true;
    for (char *c = t; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    return !strcmp(t, "text/javascript") || !strcmp(t, "application/javascript") || !strcmp(t, "module") ||
           !strcmp(t, "text/ecmascript") || !strcmp(t, "application/x-javascript");
}

void js_page_start(page_t *p) {
    if (!enabled || !p->doc || p->js) return;
    js_page_t *jp = kzalloc(sizeof *jp);
    jp->p = p;
    jp->rt = JS_NewRuntime();
    if (!jp->rt) { kfree(jp); return; }
    JS_SetMemoryLimit(jp->rt, 96u * 1024 * 1024);
    JS_SetMaxStackSize(jp->rt, 512 * 1024);
    JS_SetInterruptHandler(jp->rt, interrupt_cb, jp);
    jp->ctx = JS_NewContext(jp->rt);
    if (!jp->ctx) { JS_FreeRuntime(jp->rt); kfree(jp); return; }
    p->js = jp;
    JSContext *ctx = jp->ctx;
    JS_SetContextOpaque(ctx, jp);
    if (!node_class) JS_NewClassID(&node_class);
    JSClassDef cd = { .class_name = "HTMLElement", .finalizer = node_finalizer };
    JS_NewClass(jp->rt, node_class, &cd);
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, node_proto, ARRAY_LEN(node_proto));
    JS_SetClassProto(ctx, node_class, JS_DupValue(ctx, proto));
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "__nodeProto", proto);
    JSValue sx = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, sx, sx_funcs, ARRAY_LEN(sx_funcs));
    JS_SetPropertyStr(ctx, g, "__sx", sx);
    run_code(jp, BOOT, sizeof BOOT - 1, "<sartax>");
    jp->dispatch = JS_GetPropertyStr(ctx, g, "__sxDispatch");
    JS_FreeValue(ctx, g);

    // skrypty strony w kolejnosci
    dnode_t **scripts;
    int ns = dom_query_all(p->doc, "script", &scripts, 300);
    int external = 0;
    for (int i = 0; i < ns; i++) { char s[8]; if (dom_get_attr(scripts[i], "src", s, sizeof s) && script_is_js(scripts[i])) external++; }
    if (external) busy_begin("Wczytywanie skryptow");
    int done = 0;
    for (int i = 0; i < ns && p->js == jp; i++) {
        dnode_t *s = scripts[i];
        if (!script_is_js(s)) continue;
        char *src = dom_get_attr_dup(s, "src");
        if (src) {
            char url[900];
            done++;
            if (busy_cancelled()) { kfree(src); break; }
            if (web_resolve_url(p->url, src, url, sizeof url)) {
                char t[120];
                snprintf(t, sizeof t, "Skrypt %d z %d", done, external);
                busy_update(t, done - 1, external);
                http_resp_t r;
                if (http_get(url, &r, NULL, NULL) == 0) run_code(jp, (char *)r.body, r.len, url);
                else {
                    char m[300];
                    snprintf(m, sizeof m, "Nie udalo sie pobrac skryptu %s (%s)", url, r.error);
                    page_console(p, "warn", m);
                }
                http_free(&r);
            }
            kfree(src);
        } else {
            char *code = dom_text_content(s);
            run_code(jp, code, strlen(code), p->url);
            kfree(code);
        }
        run_jobs(jp);
    }
    kfree(scripts);
    if (external) busy_end();
    // strona gotowa
    static const char LOAD[] = "window.__sxReady=true;document.dispatchEvent(new Event('readystatechange'));"
                               "document.dispatchEvent(new Event('DOMContentLoaded',{bubbles:true}));window.dispatchEvent(new Event('load'));";
    run_code(jp, LOAD, sizeof LOAD - 1, "<load>");
    run_jobs(jp);
}

void js_page_free(page_t *p) {
    js_page_t *jp = p->js;
    if (!jp) return;
    JSContext *ctx = jp->ctx;
    for (int i = 0; i < 128; i++)
        if (jp->timers[i].used) JS_FreeValue(ctx, jp->timers[i].fn);
    JS_FreeValue(ctx, jp->dispatch);
    // odlaczone wezly (stworzone/usuniete przez JS) - zwolnimy je po srodowisku JS
    dnode_t **roots = kmalloc((jp->nwrapped + 1) * sizeof(dnode_t *));
    int nr = 0;
    for (int i = 0; i < jp->nwrapped; i++) {
        dnode_t *n = jp->wrapped[i];
        dnode_t *r = n;
        while (r->parent) r = r->parent;
        if (r == p->doc) continue;
        bool dup = false;
        for (int k = 0; k < nr; k++) if (roots[k] == r) dup = true;
        if (!dup) roots[nr++] = r;
    }
    for (int i = 0; i < jp->nwrapped; i++) {
        dnode_t *n = jp->wrapped[i];
        if (n->js) {
            JSValue v = JS_MKPTR(JS_TAG_OBJECT, n->js);
            JS_SetOpaque(v, NULL);
            n->js = NULL;
            JS_FreeValue(ctx, v);
        }
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(jp->rt);
    for (int i = 0; i < nr; i++) dom_free(roots[i]);
    kfree(roots);
    kfree(jp->wrapped);
    kfree(jp);
    p->js = NULL;
}

bool js_tick(page_t *p) {
    js_page_t *jp = p->js;
    if (!jp) return false;
    bool any = false;
    u64 now = ticks_ms();
    for (int i = 0; i < 128; i++) {
        if (!jp->timers[i].used || jp->timers[i].due > now) continue;
        JSValue fn = JS_DupValue(jp->ctx, jp->timers[i].fn);
        if (jp->timers[i].every) jp->timers[i].due = now + jp->timers[i].every;
        else { JS_FreeValue(jp->ctx, jp->timers[i].fn); jp->timers[i].used = false; }
        arm(jp, 3000);
        JSValue r = JS_Call(jp->ctx, fn, JS_UNDEFINED, 0, NULL);
        if (JS_IsException(r)) report_exception(jp->ctx);
        JS_FreeValue(jp->ctx, r);
        JS_FreeValue(jp->ctx, fn);
        jp->deadline = 0;
        any = true;
        if (p->js != jp) return true;
    }
    if (any) run_jobs(jp);
    return any;
}

static bool dispatch(page_t *p, dnode_t *target, const char *type, const char *key, int code) {
    js_page_t *jp = p->js;
    if (!jp || !target || !JS_IsFunction(jp->ctx, jp->dispatch)) return false;
    JSContext *ctx = jp->ctx;
    JSValue args[4];
    args[0] = wrap(ctx, target);
    args[1] = JS_NewString(ctx, type);
    int argc = 2;
    if (key) { args[2] = JS_NewString(ctx, key); args[3] = JS_NewInt32(ctx, code); argc = 4; }
    arm(jp, 3000);
    JSValue r = JS_Call(ctx, jp->dispatch, JS_UNDEFINED, argc, args);
    bool prevented = false;
    if (JS_IsException(r)) report_exception(ctx);
    else prevented = JS_ToBool(ctx, r);
    JS_FreeValue(ctx, r);
    for (int i = 0; i < argc; i++) JS_FreeValue(ctx, args[i]);
    jp->deadline = 0;
    run_jobs(jp);
    return prevented;
}

bool js_dispatch(page_t *p, dnode_t *target, const char *type) { return dispatch(p, target, type, NULL, 0); }
bool js_dispatch_key(page_t *p, dnode_t *target, const char *type, const char *key, int code) { return dispatch(p, target, type, key, code); }

bool js_eval_url(page_t *p, const char *code) {
    if (!p->js) return false;
    char dec[2000];
    int o = 0;
    for (int i = 0; code[i] && o < (int)sizeof dec - 1; i++) {
        if (code[i] == '%' && code[i + 1] && code[i + 2]) {
            char h[3] = { code[i + 1], code[i + 2], 0 };
            dec[o++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else dec[o++] = code[i];
    }
    dec[o] = 0;
    run_code(p->js, dec, o, "javascript:");
    run_jobs(p->js);
    return true;
}

// Sartax OS - klient HTTP/HTTPS (TLS przez BearSSL)
#include "../kernel.h"
#include "../net.h"
#include "../ui.h"
#include "bearssl.h"

extern const br_x509_trust_anchor *g_TAs;
extern const size_t g_TAs_num;

typedef struct {
    tcp_sock_t *sock;
    bool tls;
    br_ssl_client_context *sc;
    br_x509_minimal_context *xc;
    br_sslio_context io;
    u8 *iobuf;
} conn_t;

static int low_read(void *ctx, unsigned char *buf, size_t len) {
    int r = tcp_recv((tcp_sock_t *)ctx, buf, len, 30000);
    return r <= 0 ? -1 : r;
}

static int low_write(void *ctx, const unsigned char *buf, size_t len) {
    int r = tcp_send((tcp_sock_t *)ctx, buf, len);
    return r < 0 ? -1 : r;
}

static void conn_free(conn_t *c) {
    if (c->sock) tcp_close(c->sock);
    kfree(c->sc);
    kfree(c->xc);
    kfree(c->iobuf);
    memset(c, 0, sizeof *c);
}

static int conn_open(conn_t *c, const char *host, u16 port, bool tls, char *err) {
    memset(c, 0, sizeof *c);
    u32 ip;
    if (!net_ready()) { strlcpy(err, "Brak polaczenia z siecia", 128); return -1; }
    busy_update("Szukanie serwera...", 0, 0);
    if (dns_resolve(host, &ip)) { snprintf(err, 128, "Nie znaleziono serwera %s (DNS)", host); return -1; }
    busy_update("Laczenie...", 0, 0);
    c->sock = tcp_connect(ip, port, 10000);
    if (!c->sock) { snprintf(err, 128, "Nie mozna polaczyc sie z %s", host); return -1; }
    c->tls = tls;
    if (!tls) return 0;
    busy_update("Bezpieczne polaczenie (TLS)...", 0, 0);
    c->sc = kzalloc(sizeof(br_ssl_client_context));
    c->xc = kzalloc(sizeof(br_x509_minimal_context));
    c->iobuf = kmalloc(BR_SSL_BUFSIZE_BIDI);
    br_ssl_client_init_full(c->sc, c->xc, g_TAs, g_TAs_num);
    u32 now = rtc_unix();
    br_x509_minimal_set_time(c->xc, now / 86400 + 719528, now % 86400);
    br_ssl_engine_set_buffer(&c->sc->eng, c->iobuf, BR_SSL_BUFSIZE_BIDI, 1);
    u8 seed[48];
    rng_bytes(seed, sizeof seed);
    br_ssl_engine_inject_entropy(&c->sc->eng, seed, sizeof seed);
    if (!br_ssl_client_reset(c->sc, host, 0)) {
        snprintf(err, 128, "Blad TLS (reset %d)", br_ssl_engine_last_error(&c->sc->eng));
        conn_free(c);
        return -1;
    }
    br_sslio_init(&c->io, &c->sc->eng, low_read, c->sock, low_write, c->sock);
    return 0;
}

static int conn_write(conn_t *c, const void *d, size_t n) {
    if (!c->tls) return tcp_send(c->sock, d, n) < 0 ? -1 : 0;
    if (br_sslio_write_all(&c->io, d, n) < 0) return -1;
    return br_sslio_flush(&c->io) < 0 ? -1 : 0;
}

// >0 bajty, 0 koniec, <0 blad
static int conn_read(conn_t *c, void *buf, size_t n) {
    if (!c->tls) {
        int r = tcp_recv(c->sock, buf, n, 30000);
        return r;
    }
    int r = br_sslio_read(&c->io, buf, n);
    if (r < 0) {
        int e = br_ssl_engine_last_error(&c->sc->eng);
        if (e == 0 || e == BR_ERR_IO) return 0;     // zamkniecie polaczenia
        return -100 - e;
    }
    return r;
}

static const char *tls_err(int e) {
    switch (e) {
    case BR_ERR_X509_EXPIRED: return "Certyfikat serwera wygasl lub zegar komputera jest zle ustawiony";
    case BR_ERR_X509_NOT_TRUSTED: return "Certyfikat serwera nie jest zaufany";
    case BR_ERR_X509_BAD_SERVER_NAME: return "Certyfikat nie pasuje do nazwy serwera";
    case BR_ERR_NO_RANDOM: return "Brak zrodla losowosci";
    }
    return NULL;
}

static bool parse_url(const char *url, bool *tls, char *host, u16 *port, char *path) {
    const char *p = url;
    if (!strncmp(p, "https://", 8)) { *tls = true; *port = 443; p += 8; }
    else if (!strncmp(p, "http://", 7)) { *tls = false; *port = 80; p += 7; }
    else return false;
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < 127) host[i++] = *p++;
    host[i] = 0;
    if (*p == ':') { p++; *port = katoi(p); while (*p >= '0' && *p <= '9') p++; }
    if (!*p) strlcpy(path, "/", 1024);
    else strlcpy(path, p, 1024);
    return host[0] != 0;
}

static bool hdr_match(const char *line, const char *name) {
    size_t l = strlen(name);
    for (size_t i = 0; i < l; i++) {
        char a = line[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return line[l] == ':';
}

typedef struct {
    conn_t *c;
    u8 buf[16384];
    int pos, len;
    bool eof;
    int err;
} rd_t;

static int rd_fill(rd_t *r) {
    if (r->eof) return 0;
    int n = conn_read(r->c, r->buf, sizeof r->buf);
    if (n <= 0) { r->eof = true; if (n < 0) r->err = n; return 0; }
    r->pos = 0;
    r->len = n;
    return n;
}

static int rd_byte(rd_t *r) {
    if (r->pos >= r->len && !rd_fill(r)) return -1;
    return r->buf[r->pos++];
}

static int rd_line(rd_t *r, char *out, int cap) {
    int n = 0;
    for (;;) {
        int c = rd_byte(r);
        if (c < 0) { out[n] = 0; return n ? n : -1; }
        if (c == '\n') break;
        if (c != '\r' && n < cap - 1) out[n++] = c;
    }
    out[n] = 0;
    return n;
}

static int one_request(const char *url, http_resp_t *resp, char *location, http_progress_fn prog, void *ctx) {
    bool tls;
    char host[128];
    char *path = kmalloc(1024);
    u16 port;
    location[0] = 0;
    if (!parse_url(url, &tls, host, &port, path)) { strlcpy(resp->error, "Zly adres URL", sizeof resp->error); kfree(path); return -1; }
    conn_t c;
    if (conn_open(&c, host, port, tls, resp->error)) { kfree(path); return -1; }
    char *req = kmalloc(1400);
    snprintf(req, 1400, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: SartaxOS/%s\r\nAccept: text/html,text/plain,*/*\r\nAccept-Language: pl,en\r\nConnection: close\r\n\r\n",
             path, host, SARTAX_VERSION);
    kfree(path);
    busy_update("Wysylanie zapytania...", 0, 0);
    if (conn_write(&c, req, strlen(req))) {
        int e = c.tls ? br_ssl_engine_last_error(&c.sc->eng) : 0;
        const char *m = tls_err(e);
        if (m) strlcpy(resp->error, m, sizeof resp->error);
        else snprintf(resp->error, sizeof resp->error, "Blad polaczenia (TLS %d)", e);
        kfree(req);
        conn_free(&c);
        return -1;
    }
    kfree(req);
    rd_t *r = kzalloc(sizeof *r);
    r->c = &c;
    char line[1024];
    if (rd_line(r, line, sizeof line) < 0 || strncmp(line, "HTTP/", 5)) {
        int e = c.tls ? br_ssl_engine_last_error(&c.sc->eng) : 0;
        const char *m = tls_err(e);
        if (m) strlcpy(resp->error, m, sizeof resp->error);
        else snprintf(resp->error, sizeof resp->error, "Serwer nie odpowiedzial poprawnie (%d)", e);
        kfree(r);
        conn_free(&c);
        return -1;
    }
    char *sp = strchr(line, ' ');
    resp->status = sp ? katoi(sp + 1) : 0;
    i64 clen = -1;
    bool chunked = false;
    for (;;) {
        int n = rd_line(r, line, sizeof line);
        if (n <= 0) break;
        char *v = strchr(line, ':');
        if (!v) continue;
        v++;
        while (*v == ' ') v++;
        if (hdr_match(line, "content-length")) clen = katoi(v);
        else if (hdr_match(line, "transfer-encoding") && strstr(v, "chunked")) chunked = true;
        else if (hdr_match(line, "location")) strlcpy(location, v, 1024);
        else if (hdr_match(line, "content-type")) strlcpy(resp->ctype, v, sizeof resp->ctype);
    }
    if (resp->status >= 300 && resp->status < 400 && location[0]) { kfree(r); conn_free(&c); return 1; }
    // tresc
    size_t cap = clen > 0 ? (size_t)clen + 1 : 65536, len = 0;
    if (clen > 256 * 1024 * 1024) { strlcpy(resp->error, "Plik jest za duzy", sizeof resp->error); kfree(r); conn_free(&c); return -1; }
    u8 *body = kmalloc(cap);
    u64 last_prog = 0;
    i64 chunk_left = chunked ? -1 : clen;
    for (;;) {
        if (busy_cancelled()) { strlcpy(resp->error, "Anulowano", sizeof resp->error); kfree(body); kfree(r); conn_free(&c); return -1; }
        if (chunked && chunk_left <= 0) {
            if (chunk_left == 0) rd_line(r, line, sizeof line);    // CRLF po kawalku
            if (rd_line(r, line, sizeof line) < 0) break;
            chunk_left = 0;
            for (char *q = line; *q; q++) {
                int d = (*q >= '0' && *q <= '9') ? *q - '0' : (*q >= 'a' && *q <= 'f') ? *q - 'a' + 10 : (*q >= 'A' && *q <= 'F') ? *q - 'A' + 10 : -1;
                if (d < 0) break;
                chunk_left = chunk_left * 16 + d;
            }
            if (chunk_left == 0) break;
        }
        if (!chunked && clen >= 0 && (i64)len >= clen) break;
        if (r->pos >= r->len && !rd_fill(r)) break;
        size_t avail = r->len - r->pos;
        if (chunk_left >= 0 && (i64)avail > chunk_left) avail = chunk_left;
        if (!chunked && clen >= 0 && (i64)(len + avail) > clen) avail = clen - len;
        if (len + avail + 1 > cap) {
            cap = (len + avail + 1) * 2;
            body = krealloc(body, cap);
        }
        memcpy(body + len, r->buf + r->pos, avail);
        r->pos += avail;
        len += avail;
        if (chunk_left > 0) chunk_left -= avail;
        if (!chunked) chunk_left = clen >= 0 ? clen - len : -1;
        if (prog && ticks_ms() - last_prog > 100) { last_prog = ticks_ms(); prog(ctx, len, clen > 0 ? clen : 0); }
    }
    if (!chunked && clen >= 0 && (i64)len < clen) {
        snprintf(resp->error, sizeof resp->error, "Polaczenie przerwane (%u z %u B)", (u32)len, (u32)clen);
        kfree(body);
        kfree(r);
        conn_free(&c);
        return -1;
    }
    body[len] = 0;
    resp->body = body;
    resp->len = len;
    if (prog) prog(ctx, len, len);
    kfree(r);
    conn_free(&c);
    return 0;
}

int http_get(const char *url, http_resp_t *resp, http_progress_fn prog, void *ctx) {
    memset(resp, 0, sizeof *resp);
    char *cur = kmalloc(1024), *loc = kmalloc(1024);
    strlcpy(cur, url, 1024);
    int r = -1;
    for (int hop = 0; hop < 6; hop++) {
        kprintf("[http] GET %s\n", cur);
        r = one_request(cur, resp, loc, prog, ctx);
        strlcpy(resp->final_url, cur, sizeof resp->final_url);
        if (r != 1) break;
        if (loc[0] == '/' && loc[1] == '/') {
            snprintf(cur, 1024, "%s%s", !strncmp(cur, "https", 5) ? "https:" : "http:", loc);
        } else if (loc[0] == '/') {
            // adres wzgledny
            char base[256];
            const char *p = strstr(cur, "://");
            p = p ? strchr(p + 3, '/') : NULL;
            size_t bl = p ? (size_t)(p - cur) : strlen(cur);
            strlcpy(base, cur, MIN(bl + 1, sizeof base));
            snprintf(cur, 1024, "%s%s", base, loc);
        } else strlcpy(cur, loc, 1024);
        r = -1;
    }
    if (r == 0 && (resp->status < 200 || resp->status >= 300)) {
        snprintf(resp->error, sizeof resp->error, "Serwer zwrocil blad HTTP %d", resp->status);
        r = -1;
    }
    kprintf("[http] wynik %d status %d len %u %s\n", r, resp->status, (u32)resp->len, resp->error);
    kfree(cur);
    kfree(loc);
    return r;
}

void http_free(http_resp_t *r) {
    kfree(r->body);
    r->body = NULL;
}

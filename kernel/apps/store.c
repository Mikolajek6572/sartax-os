// Sartax OS - Pobieralnia (pliki i programy z GitHuba) + Aktualizacje systemu
#include "../apps.h"
#include "../net.h"
#include "bearssl.h"

#define REPO_BASE "https://raw.githubusercontent.com/Mikolajek6572/sartax-os/main/repo/"

typedef struct {
    char type[12];          // system / program / plik
    char name[64];
    char version[16];
    char target[160];
    char url[300];
    u32 size;
    char sha[65];
    char desc[200];
} item_t;

typedef struct {
    int tab;
    item_t *items;
    int n;
    bool loaded;
    char error[160];
    int scroll;
    window_t *win;
    char status[160];
    bool status_ok;
    char local_path[200];
} store_t;

static window_t *store_win;

static int ver_code(const char *v) {
    int a = katoi(v), b = 0, c = 0;
    const char *p = strchr(v, '.');
    if (p) { b = katoi(p + 1); p = strchr(p + 1, '.'); if (p) c = katoi(p + 1); }
    return a * 10000 + b * 100 + c;
}

static void sha256_hex(const u8 *d, size_t n, char *out) {
    br_sha256_context c;
    u8 h[32];
    br_sha256_init(&c);
    br_sha256_update(&c, d, n);
    br_sha256_out(&c, h);
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", h[i]);
}

static void full_url(const char *u, char *out, size_t n) {
    if (strstr(u, "://")) strlcpy(out, u, n);
    else snprintf(out, n, "%s%s", REPO_BASE, u);
}

static void parse_index(store_t *st, char *txt) {
    kfree(st->items);
    st->items = kzalloc(sizeof(item_t) * 64);
    st->n = 0;
    char *p = txt;
    while (*p && st->n < 64) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *cr = strchr(p, '\r');
        if (cr) *cr = 0;
        if (*p && *p != '#') {
            char *f[8] = { 0 };
            int k = 0;
            f[k++] = p;
            for (char *q = p; *q && k < 8; q++)
                if (*q == '|') { *q = 0; f[k++] = q + 1; }
            if (k >= 7) {
                item_t *it = &st->items[st->n++];
                strlcpy(it->type, f[0], sizeof it->type);
                strlcpy(it->name, f[1], sizeof it->name);
                strlcpy(it->version, f[2], sizeof it->version);
                strlcpy(it->target, f[3], sizeof it->target);
                full_url(f[4], it->url, sizeof it->url);
                it->size = katoi(f[5]);
                strlcpy(it->sha, f[6], sizeof it->sha);
                if (k >= 8) strlcpy(it->desc, f[7], sizeof it->desc);
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

static bool store_alive(store_t *st) { return store_win && store_win->data == st; }

static void prog_cb(void *ctx, size_t done, size_t total) {
    busy_update(ctx ? (const char *)ctx : "Pobieranie...", done, total);
}

static void load_index(void *p) {
    store_t *st = p;
    if (!store_alive(st)) return;
    busy_begin("Pobieralnia");
    http_resp_t r;
    char url[256];
    snprintf(url, sizeof url, "%sindex.txt", REPO_BASE);
    int rc = http_get(url, &r, prog_cb, "Pobieranie listy plikow...");
    busy_end();
    if (!store_alive(st)) { http_free(&r); return; }
    if (rc) {
        snprintf(st->error, sizeof st->error, "Nie udalo sie pobrac listy: %s", r.error);
        st->loaded = false;
    } else {
        parse_index(st, (char *)r.body);
        st->loaded = true;
        st->error[0] = 0;
    }
    http_free(&r);
}

static bool verify(const u8 *d, size_t n, const item_t *it, char *err) {
    if (it->size && n != it->size) { snprintf(err, 160, "Zly rozmiar pliku (%u zamiast %u B)", (u32)n, it->size); return false; }
    if (strlen(it->sha) == 64) {
        char h[65];
        sha256_hex(d, n, h);
        if (strcmp(h, it->sha)) { strlcpy(err, "Suma kontrolna SHA-256 sie nie zgadza - plik uszkodzony", 160); return false; }
    }
    return true;
}

static void mkdirs(const char *path) {
    char p[256];
    strlcpy(p, path, sizeof p);
    for (char *q = p + 1; *q; q++)
        if (*q == '/') { *q = 0; if (!fs_exists(p)) fs_mkdir(p); *q = '/'; }
}

// ---------------- instalacja jadra ----------------
int install_kernel(const u8 *d, size_t n, const char *version, char *err) {
    if (g_boot->boot_type != BOOT_HDD) { strlcpy(err, "Aktualizacje dzialaja tylko w systemie zainstalowanym na dysku", 160); return -1; }
    if (n < 64 || memcmp(d + 8, "SXKN", 4) || *(u32 *)(d + 16) != n) { strlcpy(err, "To nie jest poprawne jadro Sartax OS", 160); return -1; }
    if (n > DISK_SLOT_SECT * 512) { strlcpy(err, "Jadro jest za duze", 160); return -1; }
    boot_cfg_t bc;
    if (bootcfg_read(&bc)) { strlcpy(err, "Nie mozna odczytac konfiguracji rozruchu", 160); return -1; }
    ata_disk_t *disk = ata_boot_disk();
    int slot = g_boot->kernel_slot ^ 1;
    u32 lba = slot ? DISK_SLOT1_LBA : DISK_SLOT0_LBA;
    u32 sect = (n + 511) / 512;
    u8 *buf = kzalloc((size_t)sect * 512);
    memcpy(buf, d, n);
    busy_update("Zapisywanie nowego jadra na dysk...", 0, 0);
    int r = ata_write(disk, lba, sect, buf);
    // sprawdz zapis
    if (!r) {
        u8 *chk = kmalloc((size_t)sect * 512);
        r = ata_read(disk, lba, sect, chk);
        if (!r && memcmp(chk, buf, n)) r = -1;
        kfree(chk);
    }
    kfree(buf);
    if (r) { strlcpy(err, "Blad zapisu na dysk", 160); return -1; }
    bc.slot[slot].lba = lba;
    bc.slot[slot].sectors = sect;
    bc.slot[slot].bytes = n;
    strlcpy(bc.slot_ver[slot], version, 16);
    bc.active_slot = slot;
    if (bootcfg_write(&bc)) { strlcpy(err, "Blad zapisu konfiguracji rozruchu", 160); return -1; }
    return 0;
}

static void reboot_cb(void *ctx, bool yes) {
    if (yes) { fs_sync(); sys_reboot(); }
}

typedef struct { store_t *st; int idx; } dl_t;

static void do_download(void *p) {
    dl_t *dl = p;
    store_t *st = dl->st;
    int idx = dl->idx;
    kfree(dl);
    if (!store_alive(st) || idx >= st->n) return;
    item_t it = st->items[idx];
    bool sys = !strcmp(it.type, "system");
    busy_begin(sys ? "Aktualizacja systemu" : "Pobieranie");
    char msg[96];
    snprintf(msg, sizeof msg, "Pobieranie: %s", it.name);
    http_resp_t r;
    int rc = http_get(it.url, &r, prog_cb, msg);
    char err[160] = "";
    if (rc) strlcpy(err, r.error, sizeof err);
    else if (!verify(r.body, r.len, &it, err)) rc = -1;
    if (!rc) {
        if (sys) {
            rc = install_kernel(r.body, r.len, it.version, err);
        } else {
            busy_update("Zapisywanie...", r.len, r.len);
            mkdirs(it.target);
            int fr = fs_write_file(it.target, r.body, r.len);
            if (fr) { strlcpy(err, fs_error(fr), sizeof err); rc = -1; }
        }
    }
    busy_end();
    http_free(&r);
    if (!store_alive(st)) return;
    if (rc) {
        snprintf(st->status, sizeof st->status, "Blad: %s", err);
        st->status_ok = false;
        return;
    }
    explorer_refresh_all();
    if (sys) {
        char m[200];
        snprintf(m, sizeof m, "Sartax OS %s zostal zainstalowany. Uruchomic komputer ponownie teraz?\n\n"
                              "(Gdyby cos nie dzialalo, przytrzymaj Shift przy starcie, aby wrocic do wersji %s.)",
                 it.version, SARTAX_VERSION);
        dlg_confirm(st->win, "Aktualizacja gotowa", m, "Uruchom", reboot_cb, NULL);
    } else {
        snprintf(st->status, sizeof st->status, "Pobrano: %s", it.target);
        st->status_ok = true;
    }
}

static void install_local(void *p) {
    store_t *st = p;
    if (!store_alive(st)) return;
    u8 *d;
    u64 n;
    char err[160] = "";
    int fr = fs_read_file(st->local_path, &d, &n);
    if (fr) { snprintf(st->status, sizeof st->status, "Blad: %s", fs_error(fr)); st->status_ok = false; return; }
    char ver[17] = "?";
    if (n > 40) { memcpy(ver, d + 24, 16); ver[16] = 0; }
    busy_begin("Aktualizacja z pliku");
    int rc = install_kernel(d, n, ver, err);
    busy_end();
    kfree(d);
    if (rc) { snprintf(st->status, sizeof st->status, "Blad: %s", err); st->status_ok = false; return; }
    char m[160];
    snprintf(m, sizeof m, "Zainstalowano jadro w wersji %s. Uruchomic ponownie?", ver);
    dlg_confirm(st->win, "Aktualizacja gotowa", m, "Uruchom", reboot_cb, NULL);
}

static void local_cb(void *ctx, const char *path) {
    store_t *st = ctx;
    if (!store_alive(st)) return;
    strlcpy(st->local_path, path, sizeof st->local_path);
    ui_defer(install_local, st);
}

// ---------------- widok ----------------
static void draw_card(store_t *st, surface_t *s, int i, int x, int y, int w) {
    item_t *it = &st->items[i];
    bool have = fs_exists(it->target);
    gfx_round_rect(s, x, y, w, 84, 10, RGB(255, 255, 255));
    gfx_round_outline(s, x, y, w, 84, 10, RGB(222, 228, 234));
    int ic = !strcmp(it->type, "program") ? ICON_APP : file_icon(it->target, FT_FILE);
    gfx_blit(s, x + 16, y + 18, icon_get(ic, 48));
    gfx_text(s, x + 80, y + 14, FONT_BOLD, it->name, g_theme.text);
    char sz[32], meta[128];
    fmt_size(sz, sizeof sz, it->size);
    snprintf(meta, sizeof meta, "%s  •  wersja %s  •  %s", !strcmp(it->type, "program") ? "Program" : "Plik", it->version, sz);
    gfx_text(s, x + 80, y + 34, FONT_SMALL, meta, g_theme.text_dim);
    gfx_text_ellipsis(s, x + 80, y + 54, w - 240, FONT_UI, it->desc, g_theme.text);
    if (have && ui_button_id(UI_ID(i * 2), x + w - 250, y + 25, 100, 34, "Otworz", -1, BTN_NORMAL)) {
        if (!strcmp(it->type, "program")) app_run_sap(it->target);
        else open_path(it->target);
    }
    if (ui_button_id(UI_ID(i * 2 + 1), x + w - 140, y + 25, 124, 34, have ? "Pobierz znowu" : "Pobierz", -1, have ? BTN_NORMAL : BTN_PRIMARY)) {
        dl_t *dl = kmalloc(sizeof *dl);
        dl->st = st;
        dl->idx = i;
        ui_defer(do_download, dl);
    }
}

static void st_frame(window_t *w, surface_t *s) {
    store_t *st = w->data;
    int W = ui.cw, H = ui.ch;
    gfx_fill(s, 0, 0, W, H, RGB(242, 245, 248));
    // naglowek z zakladkami
    gfx_gradient_h(s, 0, 0, W, 86, color_lighten(g_theme.accent, 10), color_lighten(g_theme.accent, -25));
    gfx_blit(s, 20, 16, icon_get(st->tab ? ICON_UPDATE : ICON_DOWNLOAD, 48));
    gfx_text(s, 80, 18, FONT_BIG, st->tab ? "Aktualizacje" : "Pobieralnia", RGB(255, 255, 255));
    gfx_text(s, 82, 54, FONT_UI, "Pliki i programy dla Sartax OS prosto z GitHuba", RGBA(255, 255, 255, 220));
    const char *tabs[] = { "Programy i pliki", "Aktualizacje systemu" };
    for (int i = 0; i < 2; i++) {
        int tx = W - 380 + i * 186;
        if (ui_button_id(UI_ID(i), tx, 26, 178, 34, tabs[i], -1, st->tab == i ? BTN_NORMAL : BTN_LIGHT)) { st->tab = i; st->status[0] = 0; }
    }
    int y = 104;
    if (!st->loaded) {
        if (!net_ready()) {
            gfx_text_center(s, 0, y + 40, W, FONT_TITLE, "Brak polaczenia z internetem", g_theme.text);
            gfx_text_center(s, 0, y + 72, W, FONT_UI, net_status_text(), g_theme.text_dim);
            if (ui_button(W / 2 - 80, y + 104, 160, 36, "Ustawienia sieci", BTN_NORMAL)) settings_open("siec");
        } else {
            if (st->error[0]) gfx_text_center(s, 0, y + 40, W, FONT_UI, st->error, RGB(190, 60, 50));
            if (ui_button(W / 2 - 90, y + 80, 180, 38, st->error[0] ? "Sprobuj ponownie" : "Polacz z GitHubem", BTN_PRIMARY))
                ui_defer(load_index, st);
        }
    } else if (st->tab == 0) {
        int vh = H - y - 40;
        int n = 0;
        for (int i = 0; i < st->n; i++) n += strcmp(st->items[i].type, "system") != 0;
        int content = n * 94;
        if (ui.wheel) st->scroll += ui.wheel * 60;
        st->scroll = CLAMP(st->scroll, 0, MAX(0, content - vh));
        int sx0 = s->cx0, sy0 = s->cy0, sx1 = s->cx1, sy1 = s->cy1;
        gfx_set_clip(s, 0, y, W, vh);
        int k = 0;
        for (int i = 0; i < st->n; i++) {
            if (!strcmp(st->items[i].type, "system")) continue;
            int cy = y + k * 94 - st->scroll;
            k++;
            if (cy + 84 < y || cy > y + vh) continue;
            draw_card(st, s, i, 20, cy, W - 52);
        }
        if (!n) gfx_text_center(s, 0, y + 40, W, FONT_UI, "Na razie nic tu nie ma", g_theme.text_dim);
        s->cx0 = sx0, s->cy0 = sy0, s->cx1 = sx1, s->cy1 = sy1;
        ui_scrollbar(W - 20, y, vh, &st->scroll, content, vh);
    } else {
        // aktualizacje
        int best = -1;
        for (int i = 0; i < st->n; i++)
            if (!strcmp(st->items[i].type, "system") && (best < 0 || ver_code(st->items[i].version) > ver_code(st->items[best].version))) best = i;
        gfx_round_rect(s, 20, y, W - 40, 150, 12, RGB(255, 255, 255));
        gfx_round_outline(s, 20, y, W - 40, 150, 12, RGB(222, 228, 234));
        char t[200];
        snprintf(t, sizeof t, "Zainstalowana wersja: %s", SARTAX_VERSION);
        gfx_text(s, 40, y + 20, FONT_UI, t, g_theme.text_dim);
        if (best >= 0 && ver_code(st->items[best].version) > SARTAX_VERSION_CODE) {
            item_t *it = &st->items[best];
            snprintf(t, sizeof t, "Dostepna nowa wersja: %s", it->version);
            gfx_text(s, 40, y + 46, FONT_TITLE, t, g_theme.text);
            gfx_text_ellipsis(s, 40, y + 76, W - 120, FONT_UI, it->desc, g_theme.text);
            if (ui_button_icon(40, y + 102, 230, 36, ICON_UPDATE, "Pobierz i zainstaluj", BTN_PRIMARY)) {
                dl_t *dl = kmalloc(sizeof *dl);
                dl->st = st;
                dl->idx = best;
                ui_defer(do_download, dl);
            }
        } else {
            gfx_text(s, 40, y + 46, FONT_TITLE, "Masz najnowsza wersje systemu", RGB(40, 140, 80));
            gfx_text(s, 40, y + 76, FONT_UI, "Nie ma nowszych aktualizacji na GitHubie.", g_theme.text);
            if (ui_button_icon(40, y + 102, 180, 36, ICON_REFRESH, "Sprawdz ponownie", BTN_NORMAL)) ui_defer(load_index, st);
        }
        y += 170;
        gfx_text(s, 20, y, FONT_BOLD, "Aktualizacja z pliku", g_theme.text);
        gfx_text(s, 20, y + 22, FONT_SMALL, "Masz jadro Sartax OS (.sxk) na dysku? Mozesz je zainstalowac bez internetu.", g_theme.text_dim);
        if (ui_button(20, y + 46, 200, 34, "Wybierz plik .sxk...", BTN_NORMAL))
            dlg_input(w, "Aktualizacja z pliku", "Sciezka do pliku jadra:", "/Pobrane/sartax-kernel.sxk", local_cb, st);
        y += 100;
        boot_cfg_t bc;
        if (g_boot->boot_type == BOOT_HDD && bootcfg_read(&bc) == 0) {
            int other = g_boot->kernel_slot ^ 1;
            snprintf(t, sizeof t, "Uruchomiono slot %c. Druga wersja: %s (przytrzymaj Shift przy starcie, aby ja uruchomic).",
                     'A' + g_boot->kernel_slot, bc.slot[other].bytes ? bc.slot_ver[other] : "brak");
            gfx_text_wrap(s, 20, y, W - 40, FONT_SMALL, t, g_theme.text_dim, 16);
        }
    }
    if (st->status[0]) {
        gfx_fill(s, 0, H - 36, W, 36, st->status_ok ? RGB(220, 244, 228) : RGB(252, 226, 222));
        gfx_text_ellipsis(s, 20, H - 26, W - 40, FONT_UI, st->status, st->status_ok ? RGB(30, 110, 60) : RGB(170, 50, 40));
    }
}

static void st_close(window_t *w) {
    store_t *st = w->data;
    store_win = NULL;
    kfree(st->items);
    kfree(st);
}

void store_open(int tab) {
    if (store_win) {
        ((store_t *)store_win->data)->tab = tab;
        wm_focus(store_win);
        return;
    }
    store_t *st = kzalloc(sizeof *st);
    st->tab = tab;
    window_t *w = wm_create("store", tab ? "Aktualizacje" : "Pobieralnia", tab ? ICON_UPDATE : ICON_DOWNLOAD, 820, 600,
                            WF_RESIZABLE, st_frame, st);
    w->on_close = st_close;
    st->win = w;
    store_win = w;
    if (net_ready()) ui_defer(load_index, st);
}

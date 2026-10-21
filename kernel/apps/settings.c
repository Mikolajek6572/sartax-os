// Sartax OS - Ustawienia
#include "../apps.h"
#include "../net.h"

void password_hash(const char *pw, char *out65);

typedef struct {
    int page;
    char user[64], comp[64], pw1[64], pw2[64];
    char msg[128];
    bool msg_ok;
    int res_sel;
    window_t *win;
} settings_t;

static window_t *settings_win;
static u32 *thumbs[8];
#define TH_W 150
#define TH_H 92

static const char *pages[] = { "Wyglad", "Konto", "Ekran", "Data i czas", "Siec", "Informacje" };
static const int page_icons[] = { ICON_DESKTOP, ICON_USER, ICON_COMPUTER, ICON_RESTART, ICON_DOWNLOAD, ICON_INFO };
static const struct { int w, h; } resolutions[] = {
    { 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 }, { 1440, 900 }, { 1600, 900 }, { 1920, 1080 },
};

static void set_msg(settings_t *st, const char *m, bool ok) {
    strlcpy(st->msg, m, sizeof st->msg);
    st->msg_ok = ok;
}

static void cpu_name(char *out) {
    u32 r[12];
    for (int i = 0; i < 3; i++)
        __asm__ volatile("cpuid" : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]), "=d"(r[i * 4 + 3]) : "a"(0x80000002 + i), "c"(0));
    memcpy(out, r, 48);
    out[48] = 0;
    char *p = out;
    while (*p == ' ') p++;
    memmove(out, p, strlen(p) + 1);
}

static void page_wyglad(settings_t *st, surface_t *s, int x, int y, int w) {
    gfx_text(s, x, y, FONT_TITLE, "Tapeta", g_theme.text);
    y += 32;
    int cols = MAX(1, (w + 12) / (TH_W + 12));
    bool custom = cfg_get("tapeta_plik", "")[0];
    for (int i = 0; i < wallpaper_count(); i++) {
        if (!thumbs[i]) {
            thumbs[i] = kmalloc(TH_W * TH_H * 4);
            wallpaper_render(thumbs[i], TH_W, TH_H, i);
        }
        int tx = x + (i % cols) * (TH_W + 12), ty = y + (i / cols) * (TH_H + 34);
        bool sel = !custom && g_theme.wallpaper == i;
        bool hov = ui_hit(tx, ty, TH_W, TH_H);
        if (sel) gfx_round_rect(s, tx - 4, ty - 4, TH_W + 8, TH_H + 8, 10, g_theme.accent);
        else if (hov) gfx_round_rect(s, tx - 4, ty - 4, TH_W + 8, TH_H + 8, 10, g_theme.sel);
        gfx_copy(s, tx, ty, thumbs[i], TH_W, TH_H, TH_W);
        gfx_text_center(s, tx, ty + TH_H + 8, TH_W, FONT_UI, g_wallpaper_names[i], g_theme.text);
        if (ui.mpressed && hov) {
            cfg_set("tapeta", (char[2]){ '0' + i, 0 });
            cfg_set("tapeta_plik", "");
            cfg_save();
            theme_apply(cfg_get_int("akcent", 0), i);
            desktop_reload_wallpaper();
        }
    }
    y += ((wallpaper_count() + cols - 1) / cols) * (TH_H + 34) + 4;
    if (custom) {
        char t[300];
        snprintf(t, sizeof t, "Teraz uzywasz wlasnej tapety: %s", cfg_get("tapeta_plik", ""));
        gfx_text_ellipsis(s, x, y, w, FONT_UI, t, g_theme.text_dim);
        y += 26;
    } else {
        gfx_text(s, x, y, FONT_SMALL, "Wlasna tapete ustawisz w Podgladzie obrazow albo w eksploratorze (prawy przycisk na pliku .bmp).", g_theme.text_dim);
        y += 26;
    }
    y += 10;
    gfx_text(s, x, y, FONT_TITLE, "Kolor akcentu", g_theme.text);
    y += 34;
    int cur = cfg_get_int("akcent", 0);
    for (int i = 0; i < accent_count(); i++) {
        int cx = x + i * 52;
        bool hov = ui_hit(cx, y, 40, 40);
        if (i == cur) gfx_circle(s, cx + 20, y + 20, 22, g_theme.text);
        gfx_circle(s, cx + 20, y + 20, hov ? 20 : 18, g_accent_colors[i]);
        if (ui.mpressed && hov) {
            cfg_set("akcent", (char[2]){ '0' + i, 0 });
            cfg_save();
            theme_apply(i, g_theme.wallpaper);
        }
    }
    gfx_text(s, x, y + 50, FONT_SMALL, g_accent_names[cur], g_theme.text_dim);
}

static void page_konto(settings_t *st, surface_t *s, int x, int y, int w) {
    gfx_blit(s, x, y, icon_get(ICON_USER, 48));
    gfx_text(s, x + 64, y + 4, FONT_TITLE, cfg_get("uzytkownik", ""), g_theme.text);
    gfx_text(s, x + 64, y + 28, FONT_UI, cfg_get("haslo", "")[0] ? "Konto chronione haslem" : "Konto bez hasla", g_theme.text_dim);
    y += 70;
    int fw = MIN(320, w);
    gfx_text(s, x, y, FONT_BOLD, "Nazwa uzytkownika", g_theme.text);
    ui_textbox(x, y + 22, fw, st->user, sizeof st->user, 0, NULL);
    gfx_text(s, x, y + 66, FONT_BOLD, "Nazwa komputera", g_theme.text);
    ui_textbox(x, y + 88, fw, st->comp, sizeof st->comp, 0, NULL);
    if (ui_button(x, y + 132, 140, 34, "Zapisz nazwy", BTN_PRIMARY)) {
        if (!st->user[0]) set_msg(st, "Nazwa uzytkownika nie moze byc pusta", false);
        else {
            cfg_set("uzytkownik", st->user);
            cfg_set("komputer", st->comp[0] ? st->comp : "Sartax-PC");
            cfg_save();
            set_msg(st, "Zapisano", true);
        }
    }
    y += 190;
    gfx_text(s, x, y, FONT_BOLD, "Nowe haslo (puste = bez hasla)", g_theme.text);
    ui_textbox(x, y + 22, fw, st->pw1, sizeof st->pw1, TB_PASSWORD, NULL);
    gfx_text(s, x, y + 66, FONT_BOLD, "Powtorz haslo", g_theme.text);
    ui_textbox(x, y + 88, fw, st->pw2, sizeof st->pw2, TB_PASSWORD, NULL);
    if (ui_button(x, y + 132, 140, 34, "Zmien haslo", BTN_NORMAL)) {
        if (strcmp(st->pw1, st->pw2)) set_msg(st, "Hasla nie sa takie same", false);
        else {
            char h[65] = "";
            if (st->pw1[0]) password_hash(st->pw1, h);
            cfg_set("haslo", h);
            cfg_save();
            set_msg(st, st->pw1[0] ? "Haslo zmienione" : "Haslo usuniete", true);
            memset(st->pw1, 0, sizeof st->pw1);
            memset(st->pw2, 0, sizeof st->pw2);
        }
    }
}

static void page_ekran(settings_t *st, surface_t *s, int x, int y, int w) {
    char t[96];
    snprintf(t, sizeof t, "Obecna rozdzielczosc: %d x %d", g_scr_w, g_scr_h);
    gfx_text(s, x, y, FONT_TITLE, t, g_theme.text);
    y += 36;
    boot_cfg_t bc;
    bool hdd = g_boot->boot_type == BOOT_HDD && bootcfg_read(&bc) == 0;
    if (!hdd) {
        gfx_text(s, x, y, FONT_UI, "Zmiana rozdzielczosci dziala po zainstalowaniu systemu na dysku.", g_theme.text_dim);
        return;
    }
    gfx_text(s, x, y, FONT_UI, "Wybierz rozdzielczosc (zadziala po ponownym uruchomieniu):", g_theme.text_dim);
    y += 30;
    for (int i = 0; i < (int)ARRAY_LEN(resolutions); i++) {
        int ry = y + i * 38;
        bool cur = bc.width == resolutions[i].w && bc.height == resolutions[i].h;
        bool hov = ui_hit(x, ry, 300, 34);
        gfx_round_rect(s, x, ry, 300, 34, 8, cur ? g_theme.sel : hov ? g_theme.hover : RGB(255, 255, 255));
        gfx_round_outline(s, x, ry, 300, 34, 8, cur ? g_theme.accent : RGB(214, 220, 226));
        snprintf(t, sizeof t, "%d x %d%s", resolutions[i].w, resolutions[i].h, i == 0 ? "  (zalecana)" : "");
        gfx_text(s, x + 14, ry + 9, FONT_UI, t, g_theme.text);
        if (ui.mpressed && hov) {
            bc.width = resolutions[i].w;
            bc.height = resolutions[i].h;
            if (bootcfg_write(&bc) == 0) set_msg(st, "Zapisano. Uruchom komputer ponownie.", true);
            else set_msg(st, "Nie udalo sie zapisac konfiguracji rozruchu", false);
        }
    }
    gfx_text(s, x, y + ARRAY_LEN(resolutions) * 38 + 8, FONT_SMALL,
             "Jesli wybrana rozdzielczosc nie jest dostepna, system uzyje 1024 x 768.", g_theme.text_dim);
}

static void page_czas(settings_t *st, surface_t *s, int x, int y, int w) {
    datetime_t d;
    local_time(&d);
    static const char *dni[] = { "niedziela", "poniedzialek", "wtorek", "sroda", "czwartek", "piatek", "sobota" };
    char t[96];
    snprintf(t, sizeof t, "%02d:%02d:%02d", d.hour, d.min, d.sec);
    gfx_text(s, x, y, FONT_BIG, t, g_theme.text);
    snprintf(t, sizeof t, "%s, %02d.%02d.%d", dni[d.wday % 7], d.day, d.month, d.year);
    gfx_text(s, x, y + 42, FONT_UI, t, g_theme.text_dim);
    y += 86;
    gfx_text(s, x, y, FONT_BOLD, "Strefa czasowa", g_theme.text);
    y += 26;
    int off = cfg_get_int("strefa", 60);
    snprintf(t, sizeof t, "UTC%s%d:%02d", off >= 0 ? "+" : "-", (off < 0 ? -off : off) / 60, (off < 0 ? -off : off) % 60);
    if (ui_button(x, y, 40, 34, "-", BTN_NORMAL)) off -= 60;
    gfx_text_center(s, x + 44, y + 9, 110, FONT_BOLD, t, g_theme.text);
    if (ui_button(x + 158, y, 40, 34, "+", BTN_NORMAL)) off += 60;
    off = CLAMP(off, -12 * 60, 14 * 60);
    if (off != cfg_get_int("strefa", 60)) {
        snprintf(t, sizeof t, "%d", off);
        cfg_set("strefa", t);
        cfg_save();
    }
    y += 50;
    bool dst = cfg_get_int("czas_letni", 1);
    if (ui_checkbox(x, y, "Automatycznie zmieniaj czas letni/zimowy (Europa)", &dst)) {
        cfg_set("czas_letni", dst ? "1" : "0");
        cfg_save();
    }
    gfx_text(s, x, y + 40, FONT_SMALL, "Polska: UTC+1:00 z czasem letnim. Zegar sprzetowy powinien byc w UTC.", g_theme.text_dim);
}

static void page_siec(settings_t *st, surface_t *s, int x, int y, int w) {
    gfx_blit(s, x, y, icon_get(ICON_DOWNLOAD, 48));
    gfx_text(s, x + 64, y + 4, FONT_TITLE, net_ready() ? "Polaczono z internetem" : net_has_card() ? "Laczenie..." : "Brak karty sieciowej",
             g_theme.text);
    gfx_text_ellipsis(s, x + 64, y + 30, w - 70, FONT_UI, net_status_text(), g_theme.text_dim);
    y += 76;
    if (!net_has_card()) {
        gfx_text_wrap(s, x, y, w, FONT_UI,
                      "Sartax OS obsluguje karte Intel PRO/1000 MT Desktop (82540EM). W VirtualBox: Ustawienia maszyny -> Siec -> "
                      "Karta 1 -> Zaawansowane -> Typ karty: Intel PRO/1000 MT Desktop, Podlaczona do: NAT.",
                      g_theme.text, 20);
    }
}

static void page_info(settings_t *st, surface_t *s, int x, int y, int w) {
    gfx_blit(s, x, y, icon_get(ICON_LOGO, 48));
    gfx_text(s, x + 64, y, FONT_BIG, "Sartax OS", g_theme.text);
    char t[160];
    snprintf(t, sizeof t, "Wersja %s", SARTAX_VERSION);
    gfx_text(s, x + 64, y + 36, FONT_UI, t, g_theme.text_dim);
    y += 76;
    char cpu[64], mem[32], fr[32], tot[32];
    cpu_name(cpu);
    fmt_size(mem, sizeof mem, heap_total_bytes());
    fmt_size(fr, sizeof fr, fs_free_bytes());
    fmt_size(tot, sizeof tot, fs_total_bytes());
    struct { const char *k; char v[128]; } rows[7];
    int n = 0;
    rows[n].k = "Procesor"; strlcpy(rows[n++].v, cpu, 128);
    rows[n].k = "Pamiec RAM"; strlcpy(rows[n++].v, mem, 128);
    rows[n].k = "Dysk"; snprintf(rows[n++].v, 128, "SartaxFS, wolne %s z %s", fr, tot);
    rows[n].k = "Ekran"; snprintf(rows[n++].v, 128, "%d x %d, 32 bity", g_scr_w, g_scr_h);
    rows[n].k = "Uruchomiono z"; snprintf(rows[n++].v, 128, "%s%s", g_boot->boot_type == BOOT_CD ? "plyty" : "dysku",
                                          g_boot->boot_type == BOOT_HDD ? (g_boot->kernel_slot ? " (slot B)" : " (slot A)") : "");
    rows[n].k = "Czas pracy"; snprintf(rows[n++].v, 128, "%u min", (u32)(ticks_ms() / 60000));
    rows[n].k = "Mysz"; strlcpy(rows[n++].v, g_mouse_absolute ? "zintegrowana z VirtualBox" : "PS/2", 128);
    for (int i = 0; i < n; i++) {
        gfx_text(s, x, y + i * 28, FONT_BOLD, rows[i].k, g_theme.text);
        gfx_text_ellipsis(s, x + 150, y + i * 28, w - 150, FONT_UI, rows[i].v, g_theme.text_dim);
    }
    y += n * 28 + 20;
    gfx_text_wrap(s, x, y, w, FONT_UI,
                  "Sartax OS to samodzielny system operacyjny: wlasny bootloader, jadro 64-bit, system plikow SartaxFS, "
                  "menedzer okien, stos sieciowy TCP/IP i HTTPS (BearSSL). Nie korzysta z Linuksa ani Windowsa.",
                  g_theme.text_dim, 20);
    if (ui_button_icon(x, s ? y + 70 : 0, 200, 36, ICON_UPDATE, "Sprawdz aktualizacje", BTN_PRIMARY)) store_open(1);
}

static void st_frame(window_t *w, surface_t *s) {
    settings_t *st = w->data;
    int W = ui.cw, H = ui.ch, SW = 200;
    gfx_fill(s, 0, 0, W, H, RGB(255, 255, 255));
    gfx_fill(s, 0, 0, SW, H, RGB(246, 248, 250));
    gfx_fill(s, SW - 1, 0, 1, H, RGB(228, 233, 238));
    for (int i = 0; i < (int)ARRAY_LEN(pages); i++) {
        int y = 14 + i * 40;
        bool hov = ui_hit(8, y, SW - 16, 36);
        if (st->page == i) gfx_round_rect(s, 8, y, SW - 16, 36, 8, g_theme.sel);
        else if (hov) gfx_round_rect(s, 8, y, SW - 16, 36, 8, g_theme.hover);
        gfx_blit(s, 18, y + 6, icon_get(page_icons[i], 24));
        gfx_text(s, 52, y + 10, FONT_UI, pages[i], g_theme.text);
        if (ui.mpressed && hov) { st->page = i; st->msg[0] = 0; ui_focus_clear(); }
    }
    int x = SW + 28, y = 22, cw = W - x - 24;
    gfx_text(s, x, y, FONT_BIG, pages[st->page], g_theme.text);
    y += 54;
    switch (st->page) {
    case 0: page_wyglad(st, s, x, y, cw); break;
    case 1: page_konto(st, s, x, y, cw); break;
    case 2: page_ekran(st, s, x, y, cw); break;
    case 3: page_czas(st, s, x, y, cw); break;
    case 4: page_siec(st, s, x, y, cw); break;
    case 5: page_info(st, s, x, y, cw); break;
    }
    if (st->msg[0]) {
        int mw = gfx_text_width(FONT_UI, st->msg) + 32;
        gfx_round_rect(s, W - mw - 20, H - 50, mw, 34, 8, st->msg_ok ? RGB(220, 244, 228) : RGB(252, 226, 222));
        gfx_text(s, W - mw - 4, H - 41, FONT_UI, st->msg, st->msg_ok ? RGB(30, 110, 60) : RGB(170, 50, 40));
    }
}

static bool st_tick(window_t *w) {
    settings_t *st = w->data;
    static u32 last;
    u32 t = ticks_ms() / 1000;
    if ((st->page == 3 || st->page == 4) && t != last) { last = t; return true; }
    return false;
}

static void st_close(window_t *w) {
    settings_win = NULL;
    kfree(w->data);
}

void settings_open(const char *page) {
    int p = 0;
    if (page) {
        if (!strcmp(page, "konto")) p = 1;
        else if (!strcmp(page, "ekran")) p = 2;
        else if (!strcmp(page, "czas")) p = 3;
        else if (!strcmp(page, "siec")) p = 4;
        else if (!strcmp(page, "info")) p = 5;
    }
    if (settings_win) {
        ((settings_t *)settings_win->data)->page = p;
        wm_focus(settings_win);
        return;
    }
    settings_t *st = kzalloc(sizeof *st);
    st->page = p;
    strlcpy(st->user, cfg_get("uzytkownik", ""), sizeof st->user);
    strlcpy(st->comp, cfg_get("komputer", ""), sizeof st->comp);
    window_t *w = wm_create("settings", "Ustawienia", ICON_SETTINGS, 860, 600, WF_RESIZABLE, st_frame, st);
    w->on_close = st_close;
    w->tick = st_tick;
    st->win = w;
    settings_win = w;
}

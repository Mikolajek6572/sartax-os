// Sartax OS - instalator
#include "../apps.h"

void password_hash(const char *pw, char *out65);
extern const u8 blob_mbr[], blob_stage2[], blob_stage2_end[];

typedef struct {
    int step;
    int disk;
    char user[64], comp[64], pw1[64], pw2[64];
    int accent, wallpaper, res;
    char err[160];
    int progress;
    char ptext[128];
    bool done, failed;
    window_t *win;
} inst_t;

static inst_t I;
static u32 *bg;
static u32 *thumbs[8];

static const struct { int w, h; } res_opts[] = { { 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1440, 900 }, { 1920, 1080 } };
static const char *step_names[] = { "Witaj", "Dysk", "Konto", "Wyglad", "Podsumowanie", "Instalacja" };

static void sh_draw(surface_t *s) {
    gfx_copy(s, 0, 0, bg, g_scr_w, g_scr_h, g_scr_w);
    gfx_blit(s, 24, 20, icon_get(ICON_LOGO, 48));
    gfx_text(s, 84, 24, FONT_BIG, "Sartax OS", RGB(255, 255, 255));
    gfx_text(s, 86, 58, FONT_UI, "Instalator systemu " SARTAX_VERSION, RGBA(255, 255, 255, 220));
}

static shell_t inst_shell = { sh_draw, NULL, NULL, NULL, NULL, NULL };

static void progress(int pct, const char *t) {
    I.progress = pct;
    strlcpy(I.ptext, t, sizeof I.ptext);
    kprintf("[instalator] %d%% %s\n", pct, t);
    wm_render();
}

static int fail(const char *m) {
    strlcpy(I.err, m, sizeof I.err);
    I.failed = true;
    wm_invalidate();
    return -1;
}

static int do_install(void) {
    ata_disk_t *d = ata_get(I.disk);
    u8 sec[512];
    progress(2, "Przygotowanie dysku...");
    // 1. MBR + tablica partycji
    memcpy(sec, blob_mbr, 512);
    u32 sig = rng_u32();
    memcpy(sec + 440, &sig, 4);
    u8 *pe = sec + 446;
    memset(pe, 0, 64);
    u32 fs_sect = (u32)MIN(d->sectors - DISK_FS_LBA, 0xFFFFFFFFull);
    pe[0] = 0x80;
    pe[1] = 0xFE; pe[2] = 0xFF; pe[3] = 0xFF;
    pe[4] = 0x7F;
    pe[5] = 0xFE; pe[6] = 0xFF; pe[7] = 0xFF;
    u32 start = DISK_FS_LBA;
    memcpy(pe + 8, &start, 4);
    memcpy(pe + 12, &fs_sect, 4);
    // najpierw wyczysc stara konfiguracje, zeby przerwana instalacja nie startowala
    u8 zero[512] = { 0 };
    if (ata_write(d, DISK_CFG_LBA, 1, zero)) return fail("Nie mozna zapisac na dysk");
    if (ata_write(d, 0, 1, sec)) return fail("Nie mozna zapisac MBR");
    progress(6, "Zapisywanie programu rozruchowego...");
    u32 st2 = (blob_stage2_end - blob_stage2) / 512;
    if (ata_write(d, DISK_STAGE2_LBA, st2, blob_stage2)) return fail("Nie mozna zapisac programu rozruchowego");
    // 2. jadro
    u32 ksize = *(u32 *)(pristine_image + 16);
    u32 ksect = (ksize + 511) / 512;
    for (u32 done = 0; done < ksect;) {
        u32 n = MIN(ksect - done, 512);
        char t[96];
        snprintf(t, sizeof t, "Kopiowanie jadra systemu... %u%%", done * 100 / ksect);
        progress(8 + done * 30 / ksect, t);
        u8 *buf = kzalloc(n * 512);
        u32 bytes = MIN(n * 512, ksize - done * 512);
        memcpy(buf, pristine_image + (size_t)done * 512, bytes);
        int r = ata_write(d, DISK_SLOT0_LBA + done, n, buf);
        kfree(buf);
        if (r) return fail("Blad zapisu jadra");
        done += n;
    }
    // weryfikacja
    progress(40, "Sprawdzanie zapisanego jadra...");
    u8 *chk = kmalloc((size_t)ksect * 512);
    if (ata_read(d, DISK_SLOT0_LBA, ksect, chk) || memcmp(chk, pristine_image, ksize)) { kfree(chk); return fail("Jadro zapisalo sie z bledami"); }
    kfree(chk);
    // 3. system plikow
    progress(45, "Formatowanie dysku (SartaxFS)...");
    int r = fs_format(d, DISK_FS_LBA, fs_sect, "Sartax");
    if (r) return fail(fs_error(r));
    progress(60, "Tworzenie folderow...");
    const char *dirs[] = { "/Pulpit", "/Dokumenty", "/Pobrane", "/Obrazy", "/Programy", "/System" };
    for (int i = 0; i < (int)ARRAY_LEN(dirs); i++) fs_mkdir(dirs[i]);
    int nf = 0;
    while (g_initfs[nf].path) nf++;
    for (int i = 0; i < nf; i++) {
        char t[128];
        snprintf(t, sizeof t, "Kopiowanie plikow: %s", path_name(g_initfs[i].path));
        progress(62 + i * 25 / MAX(1, nf), t);
        char parent[256];
        path_parent(parent, sizeof parent, g_initfs[i].path);
        if (!fs_exists(parent)) fs_mkdir(parent);
        fs_write_file(g_initfs[i].path, g_initfs[i].data, g_initfs[i].size);
    }
    // 4. ustawienia
    progress(90, "Zapisywanie ustawien...");
    char tmp[80];
    cfg_set("uzytkownik", I.user);
    cfg_set("komputer", I.comp[0] ? I.comp : "Sartax-PC");
    if (I.pw1[0]) { password_hash(I.pw1, tmp); cfg_set("haslo", tmp); } else cfg_set("haslo", "");
    snprintf(tmp, sizeof tmp, "%d", I.accent); cfg_set("akcent", tmp);
    snprintf(tmp, sizeof tmp, "%d", I.wallpaper); cfg_set("tapeta", tmp);
    cfg_set("tapeta_plik", "");
    cfg_set("strefa", "60");
    cfg_set("czas_letni", "1");
    cfg_set("pierwszy_start", "1");
    snprintf(tmp, sizeof tmp, "%u", rtc_unix()); cfg_set("zainstalowano", tmp);
    cfg_save();
    char info[256];
    snprintf(info, sizeof info, "Sartax OS %s\nZainstalowano dla: %s\n", SARTAX_VERSION, I.user);
    fs_write_file("/System/wersja.txt", info, strlen(info));
    // 5. konfiguracja rozruchu - na koncu
    progress(96, "Konfiguracja rozruchu...");
    boot_cfg_t bc;
    memset(&bc, 0, sizeof bc);
    memcpy(bc.magic, "SXCF", 4);
    bc.version = 1;
    bc.width = res_opts[I.res].w;
    bc.height = res_opts[I.res].h;
    bc.active_slot = 0;
    bc.slot[0].lba = DISK_SLOT0_LBA;
    bc.slot[0].sectors = ksect;
    bc.slot[0].bytes = ksize;
    bc.fs_lba = DISK_FS_LBA;
    strlcpy(bc.slot_ver[0], SARTAX_VERSION, 16);
    if (bootcfg_write(&bc)) return fail("Nie mozna zapisac konfiguracji rozruchu");
    fs_sync();
    ata_flush(d);
    progress(100, "Gotowe!");
    I.done = true;
    return 0;
}

static void install_task(void *p) { do_install(); }

static void step_dots(surface_t *s, int W) {
    int n = ARRAY_LEN(step_names);
    int total = 0;
    for (int i = 0; i < n; i++) total += gfx_text_width(FONT_SMALL, step_names[i]) + 40;
    int x = (W - total) / 2;
    for (int i = 0; i < n; i++) {
        bool cur = i == I.step, done = i < I.step;
        gfx_circle(s, x + 9, 27, 9, cur ? g_theme.accent : done ? color_lighten(g_theme.accent, 60) : RGB(214, 220, 226));
        if (done) gfx_text(s, x + 4, 20, FONT_SMALL, "✓", RGB(255, 255, 255));
        gfx_text(s, x + 24, 20, cur ? FONT_BOLD : FONT_SMALL, step_names[i], cur ? g_theme.text : g_theme.text_dim);
        x += gfx_text_width(FONT_SMALL, step_names[i]) + 40;
    }
}

static void inst_frame(window_t *w, surface_t *s) {
    int W = ui.cw, H = ui.ch;
    gfx_fill(s, 0, 0, W, H, g_theme.win_bg);
    step_dots(s, W);
    gfx_fill(s, 0, 54, W, 1, RGB(226, 231, 236));
    int x = 40, y = 80, cw = W - 80;
    int by = H - 58;
    bool can_next = true;
    char t[200];

    switch (I.step) {
    case 0:
        gfx_blit(s, x, y, icon_get(ICON_LOGO, 48));
        gfx_text(s, x + 66, y + 4, FONT_BIG, "Witaj w Sartax OS", g_theme.text);
        gfx_text_wrap(s, x, y + 76, cw, FONT_UI,
                      "Ten kreator zainstaluje Sartax OS na dysku twardym tego komputera.\n\n"
                      "Sartax OS to calkowicie wlasny system: ma swoj program rozruchowy, swoje jadro 64-bit, "
                      "swoj system plikow (SartaxFS), pulpit i eksplorator plikow. Nie korzysta z Linuksa ani z Windowsa.\n\n"
                      "Po instalacji mozesz pobierac pliki, programy i aktualizacje systemu z GitHuba w aplikacji Pobieralnia.\n\n"
                      "Kliknij Dalej, aby zaczac.",
                      g_theme.text, 21);
        break;
    case 1: {
        gfx_text(s, x, y, FONT_TITLE, "Wybierz dysk, na ktorym zainstalowac system", g_theme.text);
        int ny = y + 40, cnt = 0;
        for (int i = 0; i < ata_count(); i++) {
            ata_disk_t *d = ata_get(i);
            if (d->atapi) continue;
            bool small = d->sectors < DISK_FS_LBA + 65536;
            bool sel = I.disk == i;
            bool hov = ui_hit(x, ny, cw, 64);
            gfx_round_rect(s, x, ny, cw, 64, 10, sel ? g_theme.sel : hov ? g_theme.hover : RGB(255, 255, 255));
            gfx_round_outline(s, x, ny, cw, 64, 10, sel ? g_theme.accent : RGB(214, 220, 226));
            gfx_blit(s, x + 14, ny + 8, icon_get(ICON_DISK, 48));
            char sz[32];
            fmt_size(sz, sizeof sz, d->sectors * 512);
            snprintf(t, sizeof t, "Dysk %d: %s", cnt, d->model[0] ? d->model : "dysk ATA");
            gfx_text(s, x + 76, ny + 14, FONT_BOLD, t, g_theme.text);
            snprintf(t, sizeof t, "%s%s", sz, small ? "  -  za maly (min. 48 MB)" : "");
            gfx_text(s, x + 76, ny + 36, FONT_UI, t, small ? RGB(190, 60, 50) : g_theme.text_dim);
            if (ui.mpressed && hov && !small) I.disk = i;
            ny += 74;
            cnt++;
        }
        if (!cnt) {
            gfx_blit(s, x, ny, icon_get(ICON_WARNING, 48));
            gfx_text_wrap(s, x + 66, ny + 4, cw - 70, FONT_UI,
                          "Nie znaleziono dysku twardego. W VirtualBox dodaj dysk (VDI, min. 1 GB) do kontrolera IDE "
                          "w ustawieniach maszyny i uruchom ponownie.", g_theme.text, 20);
            can_next = false;
        } else {
            ata_disk_t *d = ata_get(I.disk);
            if (!d || d->atapi || d->sectors < DISK_FS_LBA + 65536) can_next = false;
            gfx_blit(s, x, by - 50, icon_get(ICON_WARNING, 24));
            gfx_text(s, x + 32, by - 46, FONT_UI, "Wszystkie dane na wybranym dysku zostana usuniete!", RGB(190, 90, 20));
        }
        break;
    }
    case 2:
        gfx_text(s, x, y, FONT_TITLE, "Utworz swoje konto", g_theme.text);
        gfx_text(s, x, y + 40, FONT_BOLD, "Twoje imie / nazwa uzytkownika", g_theme.text);
        ui_textbox(x, y + 62, 360, I.user, sizeof I.user, TB_AUTOFOCUS, "np. Mikolaj");
        gfx_text(s, x, y + 106, FONT_BOLD, "Nazwa komputera", g_theme.text);
        ui_textbox(x, y + 128, 360, I.comp, sizeof I.comp, 0, "Sartax-PC");
        gfx_text(s, x, y + 172, FONT_BOLD, "Haslo (opcjonalne)", g_theme.text);
        ui_textbox(x, y + 194, 360, I.pw1, sizeof I.pw1, TB_PASSWORD, "zostaw puste = bez hasla");
        gfx_text(s, x, y + 238, FONT_BOLD, "Powtorz haslo", g_theme.text);
        ui_textbox(x, y + 260, 360, I.pw2, sizeof I.pw2, TB_PASSWORD, NULL);
        gfx_blit(s, x + 420, y + 60, icon_get(ICON_USER, 48));
        gfx_text_wrap(s, x + 420, y + 120, cw - 420, FONT_SMALL, "Jesli ustawisz haslo, Sartax OS poprosi o nie przy kazdym starcie.",
                      g_theme.text_dim, 16);
        can_next = I.user[0] && !strcmp(I.pw1, I.pw2);
        if (strcmp(I.pw1, I.pw2) && I.pw2[0]) gfx_text(s, x, y + 302, FONT_UI, "Hasla nie sa takie same", RGB(190, 60, 50));
        break;
    case 3: {
        gfx_text(s, x, y, FONT_TITLE, "Wybierz wyglad", g_theme.text);
        int tw = 106, th = 68;
        for (int i = 0; i < wallpaper_count(); i++) {
            if (!thumbs[i]) { thumbs[i] = kmalloc(tw * th * 4); wallpaper_render(thumbs[i], tw, th, i); }
            int tx = x + i * (tw + 12), ty = y + 36;
            bool hov = ui_hit(tx, ty, tw, th);
            if (I.wallpaper == i) gfx_round_rect(s, tx - 4, ty - 4, tw + 8, th + 8, 9, g_theme.accent);
            else if (hov) gfx_round_rect(s, tx - 4, ty - 4, tw + 8, th + 8, 9, g_theme.sel);
            gfx_copy(s, tx, ty, thumbs[i], tw, th, tw);
            gfx_text_center(s, tx, ty + th + 6, tw, FONT_SMALL, g_wallpaper_names[i], g_theme.text);
            if (ui.mpressed && hov) {
                I.wallpaper = i;
                wallpaper_render(bg, g_scr_w, g_scr_h, i);
            }
        }
        int ay = y + 150;
        gfx_text(s, x, ay, FONT_BOLD, "Kolor akcentu", g_theme.text);
        for (int i = 0; i < accent_count(); i++) {
            int cx = x + i * 48;
            bool hov = ui_hit(cx, ay + 24, 38, 38);
            if (i == I.accent) gfx_circle(s, cx + 19, ay + 43, 21, g_theme.text);
            gfx_circle(s, cx + 19, ay + 43, hov ? 19 : 17, g_accent_colors[i]);
            if (ui.mpressed && hov) { I.accent = i; theme_apply(i, I.wallpaper); }
        }
        int ry = ay + 84;
        gfx_text(s, x, ry, FONT_BOLD, "Rozdzielczosc ekranu", g_theme.text);
        for (int i = 0; i < (int)ARRAY_LEN(res_opts); i++) {
            int rx = x + i * 128;
            snprintf(t, sizeof t, "%dx%d", res_opts[i].w, res_opts[i].h);
            if (ui_button_id(UI_ID(i), rx, ry + 24, 120, 34, t, -1, I.res == i ? BTN_PRIMARY : BTN_NORMAL)) I.res = i;
        }
        gfx_text(s, x, ry + 66, FONT_SMALL, "Mozesz to pozniej zmienic w Ustawieniach.", g_theme.text_dim);
        break;
    }
    case 4: {
        gfx_text(s, x, y, FONT_TITLE, "Wszystko gotowe do instalacji", g_theme.text);
        ata_disk_t *d = ata_get(I.disk);
        char sz[32];
        fmt_size(sz, sizeof sz, d->sectors * 512);
        const char *k[] = { "Dysk", "Uzytkownik", "Komputer", "Haslo", "Tapeta", "Kolor", "Rozdzielczosc" };
        char v[7][96];
        snprintf(v[0], 96, "%s (%s)", d->model, sz);
        strlcpy(v[1], I.user, 96);
        strlcpy(v[2], I.comp[0] ? I.comp : "Sartax-PC", 96);
        strlcpy(v[3], I.pw1[0] ? "tak" : "brak", 96);
        strlcpy(v[4], g_wallpaper_names[I.wallpaper], 96);
        strlcpy(v[5], g_accent_names[I.accent], 96);
        snprintf(v[6], 96, "%d x %d", res_opts[I.res].w, res_opts[I.res].h);
        for (int i = 0; i < 7; i++) {
            gfx_text(s, x, y + 44 + i * 30, FONT_BOLD, k[i], g_theme.text);
            gfx_text(s, x + 160, y + 44 + i * 30, FONT_UI, v[i], g_theme.text_dim);
        }
        gfx_blit(s, x, by - 50, icon_get(ICON_WARNING, 24));
        gfx_text(s, x + 32, by - 46, FONT_UI, "Po kliknieciu Zainstaluj dane na dysku zostana usuniete.", RGB(190, 90, 20));
        break;
    }
    case 5:
        if (I.failed) {
            gfx_blit(s, x, y, icon_get(ICON_WARNING, 48));
            gfx_text(s, x + 66, y + 4, FONT_TITLE, "Instalacja nie powiodla sie", g_theme.text);
            gfx_text_wrap(s, x + 66, y + 34, cw - 66, FONT_UI, I.err, RGB(190, 60, 50), 20);
            if (ui_button(W - 180, by, 140, 36, "Sprobuj ponownie", BTN_PRIMARY)) { I.failed = false; I.step = 4; }
            return;
        }
        if (I.done) {
            gfx_blit(s, x, y, icon_get(ICON_LOGO, 48));
            gfx_text(s, x + 66, y + 4, FONT_BIG, "Gotowe!", g_theme.text);
            gfx_text_wrap(s, x, y + 76, cw, FONT_UI,
                          "Sartax OS zostal zainstalowany. Kliknij Uruchom ponownie.\n\n"
                          "Jesli plyta instalacyjna nadal jest w napedzie, przy starcie pojawi sie pytanie - "
                          "po prostu nic nie naciskaj, a system sam uruchomi sie z dysku. Mozesz tez wyjac plyte "
                          "(Urzadzenia -> Napedy optyczne -> Usun dysk z napedu).",
                          g_theme.text, 21);
            if (ui_button_icon(W - 220, by, 180, 38, ICON_RESTART, "Uruchom ponownie", BTN_PRIMARY)) sys_reboot();
            return;
        }
        gfx_text(s, x, y, FONT_TITLE, "Instalowanie Sartax OS...", g_theme.text);
        gfx_text(s, x, y + 40, FONT_UI, I.ptext, g_theme.text_dim);
        ui_progress(x, y + 70, cw, 14, I.progress * 10);
        snprintf(t, sizeof t, "%d%%", I.progress);
        gfx_text(s, x, y + 94, FONT_BOLD, t, g_theme.text);
        gfx_text(s, x, y + 140, FONT_SMALL, "Nie wylaczaj komputera.", g_theme.text_dim);
        return;
    }

    gfx_fill(s, 0, by - 14, W, 1, RGB(226, 231, 236));
    if (I.step > 0 && ui_button(W - 300, by, 120, 38, "Wstecz", BTN_NORMAL)) { I.step--; ui_focus_clear(); }
    const char *next = I.step == 4 ? "Zainstaluj" : "Dalej";
    if (ui_button(W - 170, by, 130, 38, next, can_next ? BTN_PRIMARY : BTN_NORMAL) && can_next) {
        ui_focus_clear();
        I.step++;
        if (I.step == 5) ui_defer(install_task, NULL);
    }
    if (ui.key && ui.key->key == KEY_ENTER && can_next && I.step < 4) { ui.key = NULL; I.step++; ui_focus_clear(); }
}

static bool inst_close(window_t *w) {
    if (I.step == 5 && !I.done && !I.failed) return false;
    dlg_message(w, "Instalator", "Instalatora nie mozna zamknac. Wylacz maszyne wirtualna, jesli nie chcesz instalowac.", ICON_INFO);
    return false;
}

void installer_run(void) {
    memset(&I, 0, sizeof I);
    I.disk = -1;
    for (int i = 0; i < ata_count(); i++)
        if (!ata_get(i)->atapi && ata_get(i)->sectors >= DISK_FS_LBA + 65536) { I.disk = i; break; }
    theme_apply(0, 0);
    bg = kmalloc((size_t)g_scr_w * g_scr_h * 4);
    wallpaper_render(bg, g_scr_w, g_scr_h, 0);
    wm_set_shell(&inst_shell);
    int w = MIN(800, g_scr_w - 40), h = MIN(560, g_scr_h - 120);
    I.win = wm_create("installer", "Instalator Sartax OS", ICON_LOGO, w, h, WF_MODAL | WF_NOTASKBAR, inst_frame, &I);
    I.win->y = MAX(96, I.win->y);
    I.win->close_req = inst_close;
    wm_loop();
}

// Sartax OS - start jadra
#include "kernel.h"
#include "gfx.h"
#include "fs.h"
#include "apps.h"
#include "net.h"

bootinfo_t *g_boot;

static void splash(const char *msg, int pct) {
    surface_t *s = &g_fb;
    gfx_reset(s);
    gfx_fill(s, 0, 0, g_scr_w, g_scr_h, RGB(14, 20, 28));
    int cx = g_scr_w / 2, cy = g_scr_h / 2;
    gfx_blit(s, cx - 24, cy - 90, icon_get(ICON_LOGO, 48));
    gfx_text_center(s, 0, cy - 26, g_scr_w, FONT_BIG, "Sartax OS", RGB(240, 244, 248));
    gfx_round_rect(s, cx - 120, cy + 30, 240, 6, 3, RGB(40, 50, 62));
    gfx_round_rect(s, cx - 120, cy + 30, MAX(6, 240 * pct / 100), 6, 3, RGB(30, 190, 170));
    gfx_text_center(s, 0, cy + 50, g_scr_w, FONT_SMALL, msg, RGB(150, 162, 175));
    gfx_present_full();
}

static void fatal_screen(const char *title, const char *msg) {
    surface_t *s = &g_fb;
    gfx_reset(s);
    gfx_fill(s, 0, 0, g_scr_w, g_scr_h, RGB(14, 20, 28));
    gfx_blit(s, g_scr_w / 2 - 24, g_scr_h / 2 - 100, icon_get(ICON_WARNING, 48));
    gfx_text_center(s, 0, g_scr_h / 2 - 30, g_scr_w, FONT_TITLE, title, RGB(255, 220, 160));
    gfx_text_center(s, 0, g_scr_h / 2 + 6, g_scr_w, FONT_UI, msg, RGB(220, 226, 232));
    gfx_present_full();
    for (;;) hlt();
}

void kmain(bootinfo_t *bi) {
    g_boot = bi;
    serial_init();
    kprintf("\nSartax OS %s - start jadra (boot %s, %ux%u, slot %u)\n", SARTAX_VERSION, bi->boot_type == BOOT_CD ? "CD" : "HDD",
            bi->fb_width, bi->fb_height, bi->kernel_slot);
    heap_init();
    gfx_init();
    splash("Uruchamianie...", 10);
    idt_init();
    rng_init();
    splash("Wykrywanie urzadzen...", 30);
    pci_init();
    vbox_init();
    input_init();
    ata_init();
    splash("Siec...", 55);
    net_init();

    if (bi->boot_type == BOOT_CD) {
        splash("Uruchamianie instalatora...", 90);
        installer_run();
    }

    splash("Montowanie dysku...", 75);
    ata_disk_t *d = ata_boot_disk();
    if (!d) fatal_screen("Nie znaleziono dysku z systemem", "Upewnij sie, ze dysk jest podlaczony do kontrolera IDE.");
    boot_cfg_t bc;
    u32 fs_lba = DISK_FS_LBA;
    u8 sec[512];
    if (ata_read(d, DISK_CFG_LBA, 1, sec) == 0) {
        memcpy(&bc, sec, sizeof bc);
        if (bc.fs_lba) fs_lba = bc.fs_lba;
    }
    int r = fs_mount(d, fs_lba);
    if (r) fatal_screen("Nie mozna zamontowac systemu plikow", fs_error(r));
    splash("Witaj!", 100);
    desktop_start();
}

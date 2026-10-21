// Sartax OS - aplikacje wbudowane
#pragma once
#include "ui.h"
#include "fs.h"

void app_open(const char *app, const char *arg);
void open_path(const char *path);
int file_icon(const char *name, int type);
const char *file_type_name(const char *name, int type);
void fmt_datetime(char *buf, size_t n, u32 unix_time);

void desktop_start(void);
void desktop_reload_wallpaper(void);
void desktop_refresh(void);
void installer_run(void) __attribute__((noreturn));

void explorer_open(const char *path);
void notepad_open(const char *path);
void viewer_open(const char *path);
void settings_open(const char *page);
void store_open(int tab);
int app_run_sap(const char *path);

// schowek plikow
extern char g_clip_paths[16][256];
extern int g_clip_count;
extern bool g_clip_cut;
void explorer_paste(window_t *owner, const char *dir);
void explorer_delete_paths(window_t *owner, char paths[][256], int n);
void explorer_refresh_all(void);

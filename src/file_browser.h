#pragma once

#define FB_MAX_ENTRIES 256
#define FB_MAX_PATH    512
#define FB_MAX_NAME    256

typedef struct {
    char name[FB_MAX_NAME];
    char full_path[FB_MAX_PATH];
    int  is_dir;
} FBEntry;

typedef struct {
    FBEntry entries[FB_MAX_ENTRIES];
    int     count;
    int     selected;
    int     scroll_offset;
    char    current_path[FB_MAX_PATH];
} FileBrowser;

/** Inizializza il browser a partire dalla directory radice (es. "ux0:") */
void fb_init(FileBrowser *fb, const char *root_path);

/** Entra nella directory selezionata o sale di livello */
int  fb_enter(FileBrowser *fb);

/** Sposta la selezione verso l'alto/basso */
void fb_move(FileBrowser *fb, int delta);

/** Restituisce il path dell'entry selezionata */
const char *fb_selected_path(const FileBrowser *fb);

/** Restituisce 1 se l'entry selezionata è una directory */
int fb_selected_is_dir(const FileBrowser *fb);

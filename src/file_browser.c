#include "file_browser.h"
#include <psp2/io/dirent.h>
#include <string.h>
#include <stdlib.h>

/* Numero di voci visibili sul pannello */
#define FB_VISIBLE_ROWS 14

static int cmp_entries(const void *a, const void *b) {
    const FBEntry *ea = (const FBEntry *)a;
    const FBEntry *eb = (const FBEntry *)b;
    /* Directory prima dei file */
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return strcmp(ea->name, eb->name);
}

static int is_selectable(const FBEntry *e) {
    if (e->is_dir) return 1;
    const char *ext = strrchr(e->name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".m3u8") == 0 || strcasecmp(ext, ".m3u") == 0);
}

static void fb_load(FileBrowser *fb) {
    fb->count = 0;
    fb->selected = 0;
    fb->scroll_offset = 0;

    /* Aggiungi voce ".." per risalire (salvo se siamo alla radice) */
    int has_parent = 0;
    const char *p = fb->current_path;
    /* La radice Vita è tipo "ux0:" */
    if (strchr(p, '/') != NULL) {
        has_parent = 1;
    }

    if (has_parent) {
        FBEntry *up = &fb->entries[fb->count++];
        strncpy(up->name, "..", FB_MAX_NAME - 1);
        /* Calcola parent path */
        strncpy(up->full_path, fb->current_path, FB_MAX_PATH - 1);
        char *slash = strrchr(up->full_path, '/');
        if (slash) *slash = '\0';
        else {
            /* Rimuovi ultimo ':' path se siamo in radice device */
            strncpy(up->full_path, fb->current_path, FB_MAX_PATH - 1);
        }
        up->is_dir = 1;
    }

    SceUID dfd = sceIoDopen(fb->current_path);
    if (dfd < 0) return;

    SceIoDirent dir;
    while (sceIoDread(dfd, &dir) > 0 && fb->count < FB_MAX_ENTRIES) {
        /* Salta file nascosti */
        if (dir.d_name[0] == '.') continue;

        FBEntry tmp;
        strncpy(tmp.name, dir.d_name, FB_MAX_NAME - 1);
        tmp.name[FB_MAX_NAME - 1] = '\0';
        snprintf(tmp.full_path, FB_MAX_PATH, "%s/%s", fb->current_path, dir.d_name);
        tmp.is_dir = SCE_S_ISDIR(dir.d_stat.st_mode);

        if (!tmp.is_dir && !is_selectable(&tmp)) continue;

        fb->entries[fb->count++] = tmp;
    }
    sceIoDclose(dfd);

    /* Ordina: skip la voce ".." che è la prima */
    int offset = has_parent ? 1 : 0;
    if (fb->count - offset > 1) {
        qsort(&fb->entries[offset], fb->count - offset, sizeof(FBEntry), cmp_entries);
    }
}

void fb_init(FileBrowser *fb, const char *root_path) {
    memset(fb, 0, sizeof(FileBrowser));
    strncpy(fb->current_path, root_path, FB_MAX_PATH - 1);
    fb_load(fb);
}

int fb_enter(FileBrowser *fb) {
    if (fb->count == 0) return 0;
    FBEntry *e = &fb->entries[fb->selected];

    if (e->is_dir) {
        strncpy(fb->current_path, e->full_path, FB_MAX_PATH - 1);
        fb_load(fb);
        return 0; /* ancora in navigazione */
    }
    return 1; /* file selezionato */
}

void fb_move(FileBrowser *fb, int delta) {
    fb->selected += delta;
    if (fb->selected < 0) fb->selected = 0;
    if (fb->selected >= fb->count) fb->selected = fb->count - 1;

    /* Aggiusta scroll */
    if (fb->selected < fb->scroll_offset) fb->scroll_offset = fb->selected;
    if (fb->selected >= fb->scroll_offset + FB_VISIBLE_ROWS)
        fb->scroll_offset = fb->selected - FB_VISIBLE_ROWS + 1;
}

const char *fb_selected_path(const FileBrowser *fb) {
    if (fb->count == 0) return NULL;
    return fb->entries[fb->selected].full_path;
}

int fb_selected_is_dir(const FileBrowser *fb) {
    if (fb->count == 0) return 0;
    return fb->entries[fb->selected].is_dir;
}

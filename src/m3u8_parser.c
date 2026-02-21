#include "m3u8_parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Rimuove newline e spazi finali da una stringa */
static void trim(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' '))
        s[--len] = '\0';
}

/* Costruisce un path assoluto: se 'path' è già assoluto (inizia con una
   lettera di device tipo "ux0:") lo usa così com'è, altrimenti lo risolve
   relativo alla directory del file M3U8. */
static void resolve_path(const char *base_dir, const char *entry_path,
                          char *out, int out_size) {
    /* percorso assoluto Vita: contiene ':' */
    if (strchr(entry_path, ':') != NULL) {
        strncpy(out, entry_path, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        snprintf(out, out_size, "%s/%s", base_dir, entry_path);
    }
}

/* Estrae la directory da un filepath (modifica la copia) */
static void extract_dir(const char *filepath, char *out, int out_size) {
    strncpy(out, filepath, out_size - 1);
    out[out_size - 1] = '\0';
    char *last_slash = strrchr(out, '/');
    if (last_slash) *last_slash = '\0';
    else out[0] = '\0';
}

int m3u8_parse(const char *filepath, M3U8Playlist *playlist) {
    if (!filepath || !playlist) return -1;

    memset(playlist, 0, sizeof(M3U8Playlist));

    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    char base_dir[M3U8_MAX_PATH];
    extract_dir(filepath, base_dir, sizeof(base_dir));

    char line[M3U8_MAX_PATH * 2];
    int  first_line = 1;
    char pending_title[M3U8_MAX_TITLE] = "";
    float pending_duration = -1.0f;

    while (fgets(line, sizeof(line), f)) {
        trim(line);

        if (first_line) {
            first_line = 0;
            if (strncmp(line, "#EXTM3U", 7) != 0) {
                /* Non è un file M3U standard, proviamo comunque */
            }
            continue;
        }

        if (line[0] == '#') {
            /* Tag #EXTINF:<duration>,<title> */
            if (strncmp(line, "#EXTINF:", 8) == 0) {
                char *comma = strchr(line + 8, ',');
                if (comma) {
                    pending_duration = (float)atof(line + 8);
                    strncpy(pending_title, comma + 1, M3U8_MAX_TITLE - 1);
                    pending_title[M3U8_MAX_TITLE - 1] = '\0';
                } else {
                    pending_duration = (float)atof(line + 8);
                    pending_title[0] = '\0';
                }
            }
            /* #EXT-X-TARGETDURATION indica playlist HLS */
            else if (strncmp(line, "#EXT-X-TARGETDURATION", 21) == 0) {
                playlist->is_hls = 1;
            }
            /* Ignora altri tag (#EXT-X-VERSION, #EXT-X-MEDIA-SEQUENCE, ecc.) */
            continue;
        }

        if (line[0] == '\0') continue;

        if (playlist->count >= M3U8_MAX_ENTRIES) break;

        M3U8Entry *e = &playlist->entries[playlist->count];

        resolve_path(base_dir, line, e->path, M3U8_MAX_PATH);

        if (pending_title[0] != '\0') {
            strncpy(e->title, pending_title, M3U8_MAX_TITLE - 1);
            e->title[M3U8_MAX_TITLE - 1] = '\0';
        } else {
            /* Usa il nome del file come titolo */
            const char *fname = strrchr(line, '/');
            strncpy(e->title, fname ? fname + 1 : line, M3U8_MAX_TITLE - 1);
            e->title[M3U8_MAX_TITLE - 1] = '\0';
        }

        e->duration = pending_duration;

        /* Detecta segmenti TS */
        const char *ext = strrchr(line, '.');
        if (ext && strcasecmp(ext, ".ts") == 0) {
            playlist->is_hls = 1;
        }

        pending_title[0] = '\0';
        pending_duration = -1.0f;
        playlist->count++;
    }

    fclose(f);
    return (playlist->count > 0) ? 0 : -1;
}

void m3u8_free(M3U8Playlist *playlist) {
    if (playlist) memset(playlist, 0, sizeof(M3U8Playlist));
}

#pragma once

#define M3U8_MAX_ENTRIES 256
#define M3U8_MAX_PATH    512
#define M3U8_MAX_TITLE   256

typedef struct {
    char path[M3U8_MAX_PATH];   /* percorso assoluto del media */
    char title[M3U8_MAX_TITLE]; /* titolo dal tag #EXTINF */
    float duration;             /* durata in secondi (-1 = sconosciuta) */
} M3U8Entry;

typedef struct {
    M3U8Entry entries[M3U8_MAX_ENTRIES];
    int count;
    int is_hls; /* 1 se contiene segmenti .ts */
} M3U8Playlist;

/**
 * Analizza un file M3U8/M3U e riempie la struttura playlist.
 * @param filepath  percorso assoluto del file .m3u8 o .m3u
 * @param playlist  struttura da riempire
 * @return 0 su successo, -1 su errore
 */
int m3u8_parse(const char *filepath, M3U8Playlist *playlist);

/** Libera le risorse della playlist */
void m3u8_free(M3U8Playlist *playlist);

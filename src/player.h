#pragma once
#include <vita2d.h>
#include "m3u8_parser.h"

typedef enum {
    PLAYER_STOPPED = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_FINISHED,
    PLAYER_ERROR
} PlayerState;

typedef struct {
    PlayerState state;
    int         current_track; /* indice nella playlist */
    uint64_t    duration_ms;
    uint64_t    position_ms;
} PlayerStatus;

/**
 * Inizializza il player (alloca risorse SceAvPlayer).
 * @return 0 su successo, <0 su errore SceAvPlayer
 */
int  player_init(void);

/**
 * Carica e avvia la riproduzione di un file media.
 * @param filepath percorso assoluto del file (MP4, TS, ecc.)
 * @return 0 su successo, <0 su errore
 */
int  player_play(const char *filepath);

/** Mette in pausa / riprende */
void player_toggle_pause(void);

/** Ferma la riproduzione e libera le risorse del track corrente */
void player_stop(void);

/** Aggiorna audio/video, chiama ogni frame. Restituisce stato corrente. */
PlayerState player_update(void);

/** Renderizza il frame video corrente nella texture vita2d fornita. */
void player_render_frame(vita2d_texture *tex);

/** Restituisce lo stato corrente del player */
PlayerStatus player_get_status(void);

/** Chiude e libera tutte le risorse SceAvPlayer */
void player_shutdown(void);

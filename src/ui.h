#pragma once
#include <vita2d.h>

/* Colori tema scuro */
#define UI_COLOR_BG          RGBA8(18,  18,  18,  255)
#define UI_COLOR_PANEL       RGBA8(30,  30,  30,  240)
#define UI_COLOR_ACCENT      RGBA8(0,   120, 215, 255)
#define UI_COLOR_ACCENT2     RGBA8(0,   80,  160, 255)
#define UI_COLOR_TEXT        RGBA8(230, 230, 230, 255)
#define UI_COLOR_TEXT_DIM    RGBA8(140, 140, 140, 255)
#define UI_COLOR_SELECTED    RGBA8(0,   100, 200, 180)
#define UI_COLOR_BAR_BG      RGBA8(50,  50,  50,  255)
#define UI_COLOR_BAR_FG      RGBA8(0,   120, 215, 255)
#define UI_COLOR_ERROR       RGBA8(220, 50,  50,  255)

/* Dimensioni schermo Vita */
#define SCREEN_W 960
#define SCREEN_H 544

/* Font di sistema caricato da ui_init */
extern vita2d_pgf *g_font;

/** Inizializza il sistema UI (carica font, ecc.) */
void ui_init(void);

/** Termina il sistema UI */
void ui_shutdown(void);

/** Disegna testo con ombra per leggibilità */
void ui_draw_text(int x, int y, unsigned int color, const char *text);

/** Disegna testo in grassetto/più grande */
void ui_draw_title(int x, int y, unsigned int color, const char *text);

/** Disegna rettangolo pieno */
void ui_draw_rect(int x, int y, int w, int h, unsigned int color);

/** Disegna barra di progresso */
void ui_draw_progress(int x, int y, int w, int h, float progress,
                      unsigned int bg_color, unsigned int fg_color);

/** Converte millisecondi in stringa "mm:ss" */
void ui_ms_to_str(uint64_t ms, char *buf, int buf_size);

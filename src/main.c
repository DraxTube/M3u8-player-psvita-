#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>
#include <string.h>
#include <stdio.h>

#include "m3u8_parser.h"
#include "player.h"
#include "file_browser.h"
#include "ui.h"

/* -----------------------------------------------------------------------
   Costanti layout
   ----------------------------------------------------------------------- */
#define HEADER_H      40
#define FOOTER_H      30
#define PANEL_X       16
#define PANEL_Y       (HEADER_H + 28)   /* spazio per path label */
#define PANEL_W       (SCREEN_W - 32)
#define PANEL_H       (SCREEN_H - PANEL_Y - FOOTER_H - 8)
#define ROW_H         28
#define VISIBLE_ROWS  ((PANEL_H - 8) / ROW_H)

/* Padding interno testo nella riga */
#define ROW_TEXT_PAD  6

/* -----------------------------------------------------------------------
   Stato applicazione
   ----------------------------------------------------------------------- */
typedef enum {
    STATE_BROWSER,
    STATE_PLAYLIST,
    STATE_PLAYING,
    STATE_ERROR
} AppState;

static AppState     g_state     = STATE_BROWSER;
static FileBrowser  g_fb;
static M3U8Playlist g_playlist;
static int          g_pl_sel    = 0;
static int          g_pl_scroll = 0;
static char         g_error_msg[256] = "";

/* -----------------------------------------------------------------------
   Helper: avvia traccia
   ----------------------------------------------------------------------- */
static void play_track(int idx) {
    if (idx < 0 || idx >= g_playlist.count) return;
    g_pl_sel = idx;
    int ret = player_play(g_playlist.entries[idx].path);
    if (ret < 0) {
        snprintf(g_error_msg, sizeof(g_error_msg),
                 "Errore avvio: %d\n%s", ret, g_playlist.entries[idx].path);
        g_state = STATE_ERROR;
    } else {
        g_state = STATE_PLAYING;
    }
}

/* -----------------------------------------------------------------------
   UI helpers
   ----------------------------------------------------------------------- */
static void draw_header(const char *title) {
    vita2d_draw_rectangle(0, 0, SCREEN_W, HEADER_H, UI_COLOR_PANEL);
    vita2d_draw_rectangle(0, HEADER_H - 2, SCREEN_W, 2, UI_COLOR_ACCENT);
    ui_draw_title(14, HEADER_H - 8, UI_COLOR_ACCENT, title);
}

static void draw_footer(const char *hints) {
    int fy = SCREEN_H - FOOTER_H;
    vita2d_draw_rectangle(0, fy, SCREEN_W, FOOTER_H, UI_COLOR_PANEL);
    vita2d_draw_rectangle(0, fy, SCREEN_W, 2, UI_COLOR_ACCENT2);
    ui_draw_text(14, fy + 8, UI_COLOR_TEXT_DIM, hints);
}

/* Disegna una riga selezionabile */
static void draw_row(int row_index, int is_selected, const char *text, unsigned int col) {
    int ry = PANEL_Y + 4 + row_index * ROW_H;
    if (is_selected) {
        vita2d_draw_rectangle(PANEL_X + 2, ry, PANEL_W - 4, ROW_H - 2, UI_COLOR_SELECTED);
    }
    ui_draw_text(PANEL_X + 10, ry + ROW_TEXT_PAD, col, text);
}

/* -----------------------------------------------------------------------
   Rendering stati
   ----------------------------------------------------------------------- */
static void render_browser(void) {
    draw_header("M3U8 Player  -  Seleziona playlist");

    /* Label percorso corrente (sopra il pannello) */
    char path_label[FB_MAX_PATH + 16];
    snprintf(path_label, sizeof(path_label), "%s", g_fb.current_path);
    ui_draw_text(PANEL_X, HEADER_H + 6, UI_COLOR_TEXT_DIM, path_label);

    vita2d_draw_rectangle(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, UI_COLOR_PANEL);

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = i + g_fb.scroll_offset;
        if (idx >= g_fb.count) break;

        FBEntry *e = &g_fb.entries[idx];
        char disp[FB_MAX_NAME + 8];
        if (e->is_dir)
            snprintf(disp, sizeof(disp), "[DIR]  %s", e->name);
        else
            snprintf(disp, sizeof(disp), "       %s", e->name);

        unsigned int col = (idx == g_fb.selected) ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM;
        draw_row(i, idx == g_fb.selected, disp, col);
    }

    draw_footer("[D-PAD] Muovi  [X] Entra/Apri  [O] Radice  [START] Esci");
}

static void render_playlist(void) {
    draw_header("Playlist");

    char pl_info[64];
    snprintf(pl_info, sizeof(pl_info), "%d tracce  |  %s",
             g_playlist.count, g_playlist.is_hls ? "HLS" : "M3U");
    ui_draw_text(PANEL_X, HEADER_H + 6, UI_COLOR_TEXT_DIM, pl_info);

    vita2d_draw_rectangle(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, UI_COLOR_PANEL);

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = i + g_pl_scroll;
        if (idx >= g_playlist.count) break;

        M3U8Entry *e = &g_playlist.entries[idx];

        char dur[12] = "--:--";
        if (e->duration > 0)
            ui_ms_to_str((uint64_t)(e->duration * 1000.0f), dur, sizeof(dur));

        char line[M3U8_MAX_TITLE + 24];
        snprintf(line, sizeof(line), "%3d. %-44s %s", idx + 1, e->title, dur);

        unsigned int col = (idx == g_pl_sel) ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM;
        draw_row(i, idx == g_pl_sel, line, col);
    }

    draw_footer("[D-PAD] Muovi  [X] Riproduci  [Quadrato] Indietro");
}

static void render_playing(void) {
    /* Video background */
    player_render_frame(NULL);

    /* Overlay in basso */
    int ov_h = 110;
    int ov_y = SCREEN_H - FOOTER_H - ov_h;
    vita2d_draw_rectangle(0, ov_y, SCREEN_W, ov_h, RGBA8(0, 0, 0, 190));

    M3U8Entry *e = &g_playlist.entries[g_pl_sel];

    /* Linea 1: titolo */
    ui_draw_text(16, ov_y + 8, UI_COLOR_TEXT, e->title);

    /* Linea 2: barra di progresso */
    PlayerStatus st = player_get_status();
    float prog = (st.duration_ms > 0)
        ? (float)st.position_ms / (float)st.duration_ms : 0.0f;
    ui_draw_progress(16, ov_y + 36, SCREEN_W - 32, 6,
                     prog, UI_COLOR_BAR_BG, UI_COLOR_BAR_FG);

    /* Linea 3: tempo + stato + traccia */
    char pos_s[12], dur_s[12];
    ui_ms_to_str(st.position_ms, pos_s, sizeof(pos_s));
    ui_ms_to_str(st.duration_ms, dur_s, sizeof(dur_s));

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%s / %s", pos_s, dur_s);
    ui_draw_text(16, ov_y + 52, UI_COLOR_TEXT_DIM, time_str);

    const char *state_lbl = (st.state == PLAYER_PAUSED) ? "|| PAUSA" : ">  PLAY";
    ui_draw_text(SCREEN_W / 2 - 30, ov_y + 52, UI_COLOR_ACCENT, state_lbl);

    char track_info[24];
    snprintf(track_info, sizeof(track_info), "%d/%d", g_pl_sel + 1, g_playlist.count);
    ui_draw_text(SCREEN_W - 70, ov_y + 52, UI_COLOR_TEXT_DIM, track_info);

    draw_footer("[X] Pausa  [Quadrato] Stop  [L] Prec  [R] Prox");
}

static void render_error(void) {
    draw_header("Errore");
    vita2d_draw_rectangle(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, UI_COLOR_PANEL);
    ui_draw_text(PANEL_X + 16, PANEL_Y + 20, UI_COLOR_ERROR, "Si e' verificato un errore:");
    /* Testo errore su righe separate per evitare uscita dallo schermo */
    int line_y = PANEL_Y + 50;
    char *msg = g_error_msg;
    char linebuf[128];
    int li = 0;
    while (*msg && line_y < PANEL_Y + PANEL_H - 20) {
        if (*msg == '\n' || li >= 120) {
            linebuf[li] = '\0';
            ui_draw_text(PANEL_X + 16, line_y, UI_COLOR_TEXT, linebuf);
            line_y += 22;
            li = 0;
            if (*msg == '\n') msg++;
        } else {
            linebuf[li++] = *msg++;
        }
    }
    if (li > 0) {
        linebuf[li] = '\0';
        ui_draw_text(PANEL_X + 16, line_y, UI_COLOR_TEXT, linebuf);
    }
    draw_footer("[O] Indietro");
}

/* -----------------------------------------------------------------------
   Input
   ----------------------------------------------------------------------- */
static SceCtrlData g_ctrl_prev;

static int btn_pressed(SceCtrlData *curr, unsigned int btn) {
    return (curr->buttons & btn) && !(g_ctrl_prev.buttons & btn);
}

static void handle_browser_input(SceCtrlData *ctrl) {
    if (btn_pressed(ctrl, SCE_CTRL_UP))    fb_move(&g_fb, -1);
    if (btn_pressed(ctrl, SCE_CTRL_DOWN))  fb_move(&g_fb,  1);
    if (btn_pressed(ctrl, SCE_CTRL_LEFT))  fb_move(&g_fb, -VISIBLE_ROWS);
    if (btn_pressed(ctrl, SCE_CTRL_RIGHT)) fb_move(&g_fb,  VISIBLE_ROWS);

    if (btn_pressed(ctrl, SCE_CTRL_CROSS)) {
        if (fb_selected_is_dir(&g_fb)) {
            fb_enter(&g_fb);
        } else {
            const char *path = fb_selected_path(&g_fb);
            if (path) {
                int ret = m3u8_parse(path, &g_playlist);
                if (ret < 0) {
                    snprintf(g_error_msg, sizeof(g_error_msg),
                             "Impossibile aprire:\n%s", path);
                    g_state = STATE_ERROR;
                } else {
                    g_pl_sel    = 0;
                    g_pl_scroll = 0;
                    g_state     = STATE_PLAYLIST;
                }
            }
        }
    }
    if (btn_pressed(ctrl, SCE_CTRL_CIRCLE)) {
        fb_init(&g_fb, "ux0:");
    }
}

static void handle_playlist_input(SceCtrlData *ctrl) {
    if (btn_pressed(ctrl, SCE_CTRL_UP)) {
        if (g_pl_sel > 0) g_pl_sel--;
        if (g_pl_sel < g_pl_scroll) g_pl_scroll = g_pl_sel;
    }
    if (btn_pressed(ctrl, SCE_CTRL_DOWN)) {
        if (g_pl_sel < g_playlist.count - 1) g_pl_sel++;
        if (g_pl_sel >= g_pl_scroll + VISIBLE_ROWS)
            g_pl_scroll = g_pl_sel - VISIBLE_ROWS + 1;
    }
    if (btn_pressed(ctrl, SCE_CTRL_CROSS)) {
        play_track(g_pl_sel);
    }
    if (btn_pressed(ctrl, SCE_CTRL_SQUARE)) {
        m3u8_free(&g_playlist);
        g_state = STATE_BROWSER;
    }
}

static void handle_playing_input(SceCtrlData *ctrl) {
    if (btn_pressed(ctrl, SCE_CTRL_CROSS))     player_toggle_pause();
    if (btn_pressed(ctrl, SCE_CTRL_SQUARE)) {
        player_stop();
        g_state = STATE_PLAYLIST;
    }
    if (btn_pressed(ctrl, SCE_CTRL_LTRIGGER)) {
        player_stop();
        play_track(g_pl_sel - 1);
    }
    if (btn_pressed(ctrl, SCE_CTRL_RTRIGGER)) {
        player_stop();
        play_track(g_pl_sel + 1);
    }
}

static void handle_error_input(SceCtrlData *ctrl) {
    if (btn_pressed(ctrl, SCE_CTRL_CIRCLE)) {
        g_state = (g_playlist.count > 0) ? STATE_PLAYLIST : STATE_BROWSER;
    }
}

/* -----------------------------------------------------------------------
   Main
   ----------------------------------------------------------------------- */
int main(void) {
    sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);

    vita2d_init();
    vita2d_set_clear_color(UI_COLOR_BG);
    ui_init();

    if (player_init() < 0) {
        snprintf(g_error_msg, sizeof(g_error_msg), "player_init() fallito!");
        g_state = STATE_ERROR;
    }

    fb_init(&g_fb, "ux0:");

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    memset(&g_ctrl_prev, 0, sizeof(g_ctrl_prev));

    while (1) {
        SceCtrlData ctrl;
        sceCtrlPeekBufferPositive(0, &ctrl, 1);

        switch (g_state) {
        case STATE_BROWSER:  handle_browser_input(&ctrl);  break;
        case STATE_PLAYLIST: handle_playlist_input(&ctrl); break;
        case STATE_PLAYING:  handle_playing_input(&ctrl);  break;
        case STATE_ERROR:    handle_error_input(&ctrl);    break;
        }

        g_ctrl_prev = ctrl;

        /* Aggiorna player e auto-avanza traccia */
        if (g_state == STATE_PLAYING) {
            PlayerState ps = player_update();
            if (ps == PLAYER_FINISHED) {
                player_stop();
                if (g_pl_sel + 1 < g_playlist.count) {
                    play_track(g_pl_sel + 1);
                } else {
                    g_state = STATE_PLAYLIST;
                }
            } else if (ps == PLAYER_ERROR) {
                snprintf(g_error_msg, sizeof(g_error_msg),
                         "Errore durante la riproduzione.\n%s",
                         g_playlist.entries[g_pl_sel].path);
                player_stop();
                g_state = STATE_ERROR;
            }
        }

        vita2d_start_drawing();
        vita2d_clear_screen();

        switch (g_state) {
        case STATE_BROWSER:  render_browser();  break;
        case STATE_PLAYLIST: render_playlist(); break;
        case STATE_PLAYING:  render_playing();  break;
        case STATE_ERROR:    render_error();    break;
        }

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    player_shutdown();
    ui_shutdown();
    vita2d_fini();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
    sceKernelExitProcess(0);
    return 0;
}

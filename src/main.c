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
#define HEADER_H      48
#define FOOTER_H      36
#define PANEL_X       20
#define PANEL_Y       (HEADER_H + 16)
#define PANEL_W       (SCREEN_W - 40)
#define PANEL_H       (SCREEN_H - HEADER_H - FOOTER_H - 24)
#define ROW_H         36
#define PLAYLIST_ROWS ((PANEL_H - 8) / ROW_H)

/* -----------------------------------------------------------------------
   Stato applicazione
   ----------------------------------------------------------------------- */
typedef enum {
    STATE_BROWSER,    /* Navigazione file system */
    STATE_PLAYLIST,   /* Visualizzazione tracce della playlist */
    STATE_PLAYING,    /* Riproduzione in corso */
    STATE_ERROR       /* Errore */
} AppState;

static AppState     g_state    = STATE_BROWSER;
static FileBrowser  g_fb;
static M3U8Playlist g_playlist;
static int          g_pl_sel   = 0;     /* traccia selezionata nella playlist */
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
                 "Errore avvio riproduzione: %d\n%s",
                 ret, g_playlist.entries[idx].path);
        g_state = STATE_ERROR;
    } else {
        g_state = STATE_PLAYING;
    }
}

/* -----------------------------------------------------------------------
   Rendering stati
   ----------------------------------------------------------------------- */
static void draw_header(const char *title) {
    ui_draw_rect(0, 0, SCREEN_W, HEADER_H, UI_COLOR_PANEL);
    /* Linea accent */
    ui_draw_rect(0, HEADER_H - 2, SCREEN_W, 2, UI_COLOR_ACCENT);
    ui_draw_title(16, HEADER_H - 10, UI_COLOR_ACCENT, title);
}

static void draw_footer(const char *hints) {
    ui_draw_rect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, UI_COLOR_PANEL);
    ui_draw_rect(0, SCREEN_H - FOOTER_H, SCREEN_W, 2, UI_COLOR_ACCENT2);
    ui_draw_text(16, SCREEN_H - FOOTER_H + 10, UI_COLOR_TEXT_DIM, hints);
}

static void render_browser(void) {
    draw_header("M3U8 Player  —  Seleziona una playlist");

    /* Percorso corrente */
    char path_label[FB_MAX_PATH + 16];
    snprintf(path_label, sizeof(path_label), "Posizione: %s", g_fb.current_path);
    ui_draw_text(PANEL_X, PANEL_Y - 18, UI_COLOR_TEXT_DIM, path_label);

    ui_draw_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, UI_COLOR_PANEL);

    int visible = PLAYLIST_ROWS;
    for (int i = 0; i < visible && (i + g_fb.scroll_offset) < g_fb.count; i++) {
        int idx = i + g_fb.scroll_offset;
        FBEntry *e = &g_fb.entries[idx];
        int ry = PANEL_Y + 4 + i * ROW_H;

        if (idx == g_fb.selected) {
            ui_draw_rect(PANEL_X + 2, ry, PANEL_W - 4, ROW_H - 2, UI_COLOR_SELECTED);
        }

        char disp[FB_MAX_NAME + 4];
        if (e->is_dir)
            snprintf(disp, sizeof(disp), "[DIR] %s", e->name);
        else
            snprintf(disp, sizeof(disp), "      %s", e->name);

        unsigned int col = (idx == g_fb.selected) ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM;
        ui_draw_text(PANEL_X + 8, ry + 8, col, disp);
    }

    draw_footer("[D-PAD] Muovi  [CROCE] Entra/Seleziona  [CERCHIO] Radice  [HOME] Esci");
}

static void render_playlist(void) {
    draw_header("Playlist");

    char pl_info[128];
    snprintf(pl_info, sizeof(pl_info), "%d tracce  |  %s",
             g_playlist.count, g_playlist.is_hls ? "HLS" : "M3U");
    ui_draw_text(PANEL_X, PANEL_Y - 18, UI_COLOR_TEXT_DIM, pl_info);

    ui_draw_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, UI_COLOR_PANEL);

    for (int i = 0; i < PLAYLIST_ROWS && (i + g_pl_scroll) < g_playlist.count; i++) {
        int idx = i + g_pl_scroll;
        M3U8Entry *e = &g_playlist.entries[idx];
        int ry = PANEL_Y + 4 + i * ROW_H;

        if (idx == g_pl_sel) {
            ui_draw_rect(PANEL_X + 2, ry, PANEL_W - 4, ROW_H - 2, UI_COLOR_SELECTED);
        }

        char dur[16] = "--:--";
        if (e->duration > 0) {
            ui_ms_to_str((uint64_t)(e->duration * 1000.0f), dur, sizeof(dur));
        }

        char line[M3U8_MAX_TITLE + 32];
        snprintf(line, sizeof(line), "%3d. %-50s %s", idx + 1, e->title, dur);

        unsigned int col = (idx == g_pl_sel) ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM;
        ui_draw_text(PANEL_X + 8, ry + 8, col, line);
    }

    draw_footer("[D-PAD] Muovi  [CROCE] Riproduci  [QUADRATO] Indietro");
}

static void render_playing(void) {
    /* Video occupa tutto lo schermo, overlay UI sopra */
    player_render_frame(NULL);

    /* Overlay semi-trasparente in basso, sopra il footer */
    int overlay_h = 100;
    int overlay_y = SCREEN_H - FOOTER_H - overlay_h;
    ui_draw_rect(0, overlay_y, SCREEN_W, overlay_h, RGBA8(0, 0, 0, 180));

    /* Titolo traccia */
    M3U8Entry *e = &g_playlist.entries[g_pl_sel];
    ui_draw_text(16, overlay_y + 10, UI_COLOR_TEXT, e->title);

    /* Progresso */
    PlayerStatus st = player_get_status();
    float prog = (st.duration_ms > 0)
                 ? (float)st.position_ms / (float)st.duration_ms
                 : 0.0f;

    ui_draw_progress(16, overlay_y + 45, SCREEN_W - 32, 6,
                     prog, UI_COLOR_BAR_BG, UI_COLOR_BAR_FG);

    char pos_str[16], dur_str[16];
    ui_ms_to_str(st.position_ms, pos_str, sizeof(pos_str));
    ui_ms_to_str(st.duration_ms, dur_str, sizeof(dur_str));

    char time_str[48];
    snprintf(time_str, sizeof(time_str), "%s / %s", pos_str, dur_str);
    ui_draw_text(16, overlay_y + 65, UI_COLOR_TEXT_DIM, time_str);

    /* Stato */
    const char *state_label = (st.state == PLAYER_PAUSED) ? "||  PAUSA" : ">  PLAY";
    ui_draw_text(SCREEN_W - 120, overlay_y + 65, UI_COLOR_ACCENT, state_label);

    /* Traccia N/M */
    char track_info[32];
    snprintf(track_info, sizeof(track_info), "%d / %d", g_pl_sel + 1, g_playlist.count);
    ui_draw_text(16, overlay_y + 85, UI_COLOR_TEXT_DIM, track_info);

    draw_footer("[CROCE] Pausa  [QUADRATO] Stop  [L] Precedente  [R] Prossima");
}

static void render_error(void) {
    draw_header("Errore");
    ui_draw_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, UI_COLOR_PANEL);
    ui_draw_text(PANEL_X + 16, PANEL_Y + 30, UI_COLOR_ERROR, "Si e' verificato un errore:");
    ui_draw_text(PANEL_X + 16, PANEL_Y + 60, UI_COLOR_TEXT, g_error_msg);
    draw_footer("[CERCHIO] Indietro");
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
    if (btn_pressed(ctrl, SCE_CTRL_LEFT))  fb_move(&g_fb, -PLAYLIST_ROWS);
    if (btn_pressed(ctrl, SCE_CTRL_RIGHT)) fb_move(&g_fb,  PLAYLIST_ROWS);

    if (btn_pressed(ctrl, SCE_CTRL_CROSS)) {
        if (fb_selected_is_dir(&g_fb)) {
            fb_enter(&g_fb);
        } else {
            /* Carica playlist */
            const char *path = fb_selected_path(&g_fb);
            if (path) {
                int ret = m3u8_parse(path, &g_playlist);
                if (ret < 0) {
                    snprintf(g_error_msg, sizeof(g_error_msg),
                             "Impossibile aprire la playlist:\n%s", path);
                    g_state = STATE_ERROR;
                } else {
                    g_pl_sel = 0;
                    g_pl_scroll = 0;
                    g_state = STATE_PLAYLIST;
                }
            }
        }
    }

    if (btn_pressed(ctrl, SCE_CTRL_CIRCLE)) {
        /* Torna alla radice */
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
        if (g_pl_sel >= g_pl_scroll + PLAYLIST_ROWS)
            g_pl_scroll = g_pl_sel - PLAYLIST_ROWS + 1;
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
    if (btn_pressed(ctrl, SCE_CTRL_CROSS)) {
        player_toggle_pause();
    }
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
    /* Carica moduli necessari */
    sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    /* SceAudioOut non richiede sysmodule separato */

    vita2d_init();
    vita2d_set_clear_color(UI_COLOR_BG);

    ui_init();

    if (player_init() < 0) {
        /* Errore grave: impossibile inizializzare il player */
        snprintf(g_error_msg, sizeof(g_error_msg), "player_init() fallito!");
        g_state = STATE_ERROR;
    }

    fb_init(&g_fb, "ux0:");

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    memset(&g_ctrl_prev, 0, sizeof(g_ctrl_prev));

    /* Loop principale */
    while (1) {
        /* Input */
        SceCtrlData ctrl;
        sceCtrlPeekBufferPositive(0, &ctrl, 1);

        switch (g_state) {
        case STATE_BROWSER:   handle_browser_input(&ctrl);   break;
        case STATE_PLAYLIST:  handle_playlist_input(&ctrl);  break;
        case STATE_PLAYING:   handle_playing_input(&ctrl);   break;
        case STATE_ERROR:     handle_error_input(&ctrl);     break;
        }

        g_ctrl_prev = ctrl;

        /* Aggiorna player */
        if (g_state == STATE_PLAYING) {
            PlayerState ps = player_update();
            if (ps == PLAYER_FINISHED) {
                player_stop();
                /* Auto-prossima traccia */
                if (g_pl_sel + 1 < g_playlist.count) {
                    play_track(g_pl_sel + 1);
                } else {
                    g_state = STATE_PLAYLIST;
                }
            }
        }

        /* Rendering */
        vita2d_start_drawing();
        vita2d_clear_screen();

        switch (g_state) {
        case STATE_BROWSER:   render_browser();   break;
        case STATE_PLAYLIST:  render_playlist();  break;
        case STATE_PLAYING:   render_playing();   break;
        case STATE_ERROR:     render_error();     break;
        }

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    /* Cleanup (normalmente non raggiunto) */
    player_shutdown();
    ui_shutdown();
    vita2d_fini();

    sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
    sceKernelExitProcess(0);
    return 0;
}

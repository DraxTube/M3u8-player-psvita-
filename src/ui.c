#include "ui.h"
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

vita2d_pgf *g_font = NULL;

void ui_init(void) {
    g_font = vita2d_load_default_pgf();
}

void ui_shutdown(void) {
    if (g_font) {
        vita2d_free_pgf(g_font);
        g_font = NULL;
    }
}

void ui_draw_text(int x, int y, unsigned int color, const char *text) {
    /* Ombra per leggibilità */
    vita2d_pgf_draw_text(g_font, x + 1, y + 1, RGBA8(0, 0, 0, 180), 1.0f, text);
    vita2d_pgf_draw_text(g_font, x, y, color, 1.0f, text);
}

void ui_draw_title(int x, int y, unsigned int color, const char *text) {
    vita2d_pgf_draw_text(g_font, x + 1, y + 1, RGBA8(0, 0, 0, 200), 1.3f, text);
    vita2d_pgf_draw_text(g_font, x, y, color, 1.3f, text);
}

void ui_draw_rect(int x, int y, int w, int h, unsigned int color) {
    vita2d_draw_rectangle((float)x, (float)y, (float)w, (float)h, color);
}

void ui_draw_progress(int x, int y, int w, int h, float progress,
                      unsigned int bg_color, unsigned int fg_color) {
    vita2d_draw_rectangle((float)x, (float)y, (float)w, (float)h, bg_color);
    if (progress > 0.0f && progress <= 1.0f) {
        vita2d_draw_rectangle((float)x, (float)y, (float)(w * progress), (float)h, fg_color);
    }
}

void ui_ms_to_str(uint64_t ms, char *buf, int buf_size) {
    uint64_t total_s = ms / 1000;
    uint64_t m = total_s / 60;
    uint64_t s = total_s % 60;
    snprintf(buf, buf_size, "%02llu:%02llu", (unsigned long long)m,
             (unsigned long long)s);
}

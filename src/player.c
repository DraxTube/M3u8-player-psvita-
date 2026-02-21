#include "player.h"
#include "ui.h"
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <vita2d.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
   Memoria
   ----------------------------------------------------------------------- */
#define PLAYER_MEM_SIZE   (32 * 1024 * 1024)  /* 32 MB RAM */
#define PLAYER_CDRAM_SIZE (16 * 1024 * 1024)  /* 16 MB CDRAM (GPU) */

static SceUID g_mem_uid   = -1;
static SceUID g_cdram_uid = -1;
static void  *g_mem_base  = NULL;
static void  *g_cdram_base = NULL;

/* -----------------------------------------------------------------------
   SceAvPlayer
   ----------------------------------------------------------------------- */
static SceAvPlayerHandle g_player  = NULL;
static int               g_audio_port = -1;
static SceUID            g_audio_thread = -1;
static volatile int      g_audio_running = 0;

static PlayerStatus g_status;

/* Texture per il frame video (NV12 960x544 max) */
static vita2d_texture *g_video_tex = NULL;

/* -----------------------------------------------------------------------
   Callback memoria richieste da SceAvPlayer
   ----------------------------------------------------------------------- */
static void *mem_alloc(void *arg, uint32_t alignment, uint32_t size) {
    (void)arg; (void)alignment;
    if (!g_mem_base) return NULL;
    /* Allocazione semplificata: bump allocator non free-able
       Sufficiente per la vita del singolo playback. */
    static uint8_t *ptr = NULL;
    if (!ptr) ptr = (uint8_t *)g_mem_base;
    void *ret = ptr;
    ptr += (size + 63) & ~63;
    return ret;
}

static void mem_free(void *arg, void *ptr) {
    (void)arg; (void)ptr; /* bump allocator – non liberiamo */
}

static void *gpu_alloc(void *arg, uint32_t alignment, uint32_t size) {
    (void)arg; (void)alignment;
    if (!g_cdram_base) return NULL;
    static uint8_t *ptr = NULL;
    if (!ptr) ptr = (uint8_t *)g_cdram_base;
    void *ret = ptr;
    ptr += (size + 255) & ~255;
    return ret;
}

static void gpu_free(void *arg, void *ptr) {
    (void)arg; (void)ptr;
}

/* -----------------------------------------------------------------------
   Thread audio
   ----------------------------------------------------------------------- */
static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp;

    while (g_audio_running) {
        if (!g_player) { sceKernelDelayThread(5000); continue; }

        SceAvPlayerFrameInfo frame;
        memset(&frame, 0, sizeof(frame));

        if (sceAvPlayerGetAudioData(g_player, &frame)) {
            if (g_audio_port < 0) {
                g_audio_port = sceAudioOutOpenPort(
                    SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                    frame.details.audio.sampleCount,
                    frame.details.audio.sampleRate,
                    (frame.details.audio.channelCount == 1)
                        ? SCE_AUDIO_OUT_MODE_MONO
                        : SCE_AUDIO_OUT_MODE_STEREO
                );
            }
            if (g_audio_port >= 0) {
                sceAudioOutOutput(g_audio_port, frame.pData);
            }
        } else {
            sceKernelDelayThread(1000);
        }
    }

    if (g_audio_port >= 0) {
        sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
    }
    return 0;
}

/* -----------------------------------------------------------------------
   API pubblica
   ----------------------------------------------------------------------- */
int player_init(void) {
    memset(&g_status, 0, sizeof(g_status));
    g_status.state = PLAYER_STOPPED;

    /* Alloca blocchi di memoria */
    g_mem_uid = sceKernelAllocMemBlock("AvPlayerRam",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, PLAYER_MEM_SIZE, NULL);
    if (g_mem_uid < 0) return g_mem_uid;
    sceKernelGetMemBlockBase(g_mem_uid, &g_mem_base);

    g_cdram_uid = sceKernelAllocMemBlock("AvPlayerCdram",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, PLAYER_CDRAM_SIZE, NULL);
    if (g_cdram_uid < 0) return g_cdram_uid;
    sceKernelGetMemBlockBase(g_cdram_uid, &g_cdram_base);

    /* Avvia thread audio */
    g_audio_running = 1;
    g_audio_thread = sceKernelCreateThread("audio_thread", audio_thread,
        0x10000100, 0x10000, 0, 0, NULL);
    if (g_audio_thread >= 0)
        sceKernelStartThread(g_audio_thread, 0, NULL);

    return 0;
}

int player_play(const char *filepath) {
    player_stop();

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.objectPointer    = NULL;
    init.memoryReplacement.allocate         = mem_alloc;
    init.memoryReplacement.deallocate       = mem_free;
    init.memoryReplacement.allocateTexture  = gpu_alloc;
    init.memoryReplacement.deallocateTexture = gpu_free;
    init.basePriority   = 0xA0;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart      = SCE_TRUE;

    g_player = sceAvPlayerInit(&init);
    if (!g_player) return -1;

    int ret = sceAvPlayerAddSource(g_player, filepath);
    if (ret < 0) {
        sceAvPlayerClose(g_player);
        g_player = NULL;
        return ret;
    }

    /* Crea texture video se non esiste */
    if (!g_video_tex) {
        g_video_tex = vita2d_create_empty_texture_format(
            SCREEN_W, SCREEN_H,
            SCE_GXM_TEXTURE_FORMAT_YUV420P2_CSC0);
    }

    g_status.state = PLAYER_PLAYING;
    g_status.position_ms = 0;
    return 0;
}

void player_toggle_pause(void) {
    if (!g_player) return;
    if (g_status.state == PLAYER_PLAYING) {
        sceAvPlayerPause(g_player);
        g_status.state = PLAYER_PAUSED;
    } else if (g_status.state == PLAYER_PAUSED) {
        sceAvPlayerResume(g_player);
        g_status.state = PLAYER_PLAYING;
    }
}

void player_stop(void) {
    if (g_player) {
        sceAvPlayerStop(g_player);
        sceAvPlayerClose(g_player);
        g_player = NULL;
    }
    if (g_video_tex) {
        vita2d_free_texture(g_video_tex);
        g_video_tex = NULL;
    }
    g_status.state = PLAYER_STOPPED;
    g_status.position_ms = 0;
    g_status.duration_ms = 0;
}

PlayerState player_update(void) {
    if (!g_player) return g_status.state;

    if (!sceAvPlayerIsActive(g_player)) {
        g_status.state = PLAYER_FINISHED;
        return g_status.state;
    }

    g_status.position_ms = (uint64_t)sceAvPlayerCurrentTime(g_player);
    return g_status.state;
}

void player_render_frame(vita2d_texture *tex) {
    (void)tex;
    if (!g_player || !g_video_tex) return;

    SceAvPlayerFrameInfo frame;
    memset(&frame, 0, sizeof(frame));
    if (sceAvPlayerGetVideoData(g_player, &frame)) {
        /* Copia i dati YUV nella texture */
        void *tex_data = vita2d_texture_get_datap(g_video_tex);
        memcpy(tex_data, frame.pData,
               frame.details.video.width * frame.details.video.height * 3 / 2);
        /* Disegna la texture a schermo intero */
        vita2d_draw_texture_scale(g_video_tex,
            0.0f, 0.0f,
            (float)SCREEN_W / (float)frame.details.video.width,
            (float)SCREEN_H / (float)frame.details.video.height);
    }
}

PlayerStatus player_get_status(void) {
    return g_status;
}

void player_shutdown(void) {
    player_stop();

    g_audio_running = 0;
    if (g_audio_thread >= 0) {
        sceKernelWaitThreadEnd(g_audio_thread, NULL, NULL);
        sceKernelDeleteThread(g_audio_thread);
        g_audio_thread = -1;
    }

    if (g_mem_uid >= 0) {
        sceKernelFreeMemBlock(g_mem_uid);
        g_mem_uid = -1;
        g_mem_base = NULL;
    }
    if (g_cdram_uid >= 0) {
        sceKernelFreeMemBlock(g_cdram_uid);
        g_cdram_uid = -1;
        g_cdram_base = NULL;
    }
}

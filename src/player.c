#include "player.h"
#include "ui.h"
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <vita2d.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
   GPU memory tracking
   ----------------------------------------------------------------------- */
#define MAX_GPU_ALLOCS 64

typedef struct {
    SceUID uid;
    void  *base;
} GpuAlloc;

static GpuAlloc g_gpu_allocs[MAX_GPU_ALLOCS];
static int      g_gpu_alloc_count = 0;

/* -----------------------------------------------------------------------
   SceAvPlayer state
   ----------------------------------------------------------------------- */
static SceAvPlayerHandle g_player     = 0;
static int               g_audio_port = -1;
static SceUID            g_audio_thid = -1;
static volatile int      g_audio_run  = 0;
static PlayerStatus      g_status;
static int               g_has_video  = 0;

/* Video texture */
static vita2d_texture   *g_video_tex  = NULL;
static int               g_video_w    = 0;
static int               g_video_h    = 0;

/* -----------------------------------------------------------------------
   Memory callbacks for SceAvPlayer
   ----------------------------------------------------------------------- */
static void *av_alloc(void *p, uint32_t align, uint32_t size) {
    (void)p;
    if (align < 4) align = 4;
    size = (size + align - 1) & ~(align - 1);
    void *ptr = memalign(align, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

static void av_free(void *p, void *ptr) {
    (void)p;
    if (ptr) free(ptr);
}

static void *av_gpu_alloc(void *p, uint32_t align, uint32_t size) {
    (void)p;
    if (g_gpu_alloc_count >= MAX_GPU_ALLOCS) return NULL;

    if (align < (256 * 1024)) align = 256 * 1024;
    size = (size + align - 1) & ~(align - 1);

    SceUID uid = sceKernelAllocMemBlock("AvGpu",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, NULL);

    if (uid < 0) {
        /* Fallback a LPDDR fisicamente contigua */
        SceKernelAllocMemBlockOpt opt;
        memset(&opt, 0, sizeof(opt));
        opt.size      = sizeof(opt);
        opt.attr      = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
        opt.alignment = align;
        uid = sceKernelAllocMemBlock("AvGpuLPDDR",
            SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, size, &opt);
        if (uid < 0) return NULL;
    }

    void *base = NULL;
    if (sceKernelGetMemBlockBase(uid, &base) < 0) {
        sceKernelFreeMemBlock(uid);
        return NULL;
    }

    g_gpu_allocs[g_gpu_alloc_count].uid  = uid;
    g_gpu_allocs[g_gpu_alloc_count].base = base;
    g_gpu_alloc_count++;
    return base;
}

static void av_gpu_free(void *p, void *ptr) {
    (void)p;
    for (int i = 0; i < g_gpu_alloc_count; i++) {
        if (g_gpu_allocs[i].base == ptr) {
            sceKernelFreeMemBlock(g_gpu_allocs[i].uid);
            g_gpu_allocs[i] = g_gpu_allocs[g_gpu_alloc_count - 1];
            g_gpu_alloc_count--;
            return;
        }
    }
}

/* -----------------------------------------------------------------------
   Event callback — campo corretto: eventReplacement (non eventManager)
   ----------------------------------------------------------------------- */
static void av_event_cb(void *p, int32_t eventId,
                         int32_t sourceId, void *eventData) {
    (void)p; (void)sourceId; (void)eventData;
    /* 0x80 = SCE_AVPLAYER_STATE_ERROR */
    if (eventId == 0x80) {
        g_status.state = PLAYER_ERROR;
    }
}

/* -----------------------------------------------------------------------
   Audio thread
   ----------------------------------------------------------------------- */
static int audio_thread_func(SceSize args, void *argp) {
    (void)args; (void)argp;

    while (g_audio_run) {
        if (g_player <= 0) {
            sceKernelDelayThread(10000);
            continue;
        }

        SceAvPlayerFrameInfo frame;
        memset(&frame, 0, sizeof(frame));

        if (sceAvPlayerGetAudioData(g_player, &frame) &&
            frame.pData && frame.details.audio.sampleRate > 0) {

            if (g_audio_port < 0) {
                int ch = (int)frame.details.audio.channelCount;
                if (ch < 1) ch = 2;
                g_audio_port = sceAudioOutOpenPort(
                    SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                    1024,
                    (int)frame.details.audio.sampleRate,
                    (ch == 1) ? SCE_AUDIO_OUT_MODE_MONO
                              : SCE_AUDIO_OUT_MODE_STEREO
                );
            }
            if (g_audio_port >= 0) {
                sceAudioOutOutput(g_audio_port, frame.pData);
            }
        } else {
            sceKernelDelayThread(5000);
        }
    }

    if (g_audio_port >= 0) {
        sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
    }
    return 0;
}

/* -----------------------------------------------------------------------
   Internal helpers
   ----------------------------------------------------------------------- */
static void free_video_texture(void) {
    if (g_video_tex) {
        vita2d_free_texture(g_video_tex);
        g_video_tex = NULL;
    }
    g_video_w = 0;
    g_video_h = 0;
}

/* -----------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------- */
int player_init(void) {
    memset(&g_status, 0, sizeof(g_status));
    g_status.state    = PLAYER_STOPPED;
    g_gpu_alloc_count = 0;

    g_audio_run  = 1;
    g_audio_thid = sceKernelCreateThread("m3u8_audio",
        audio_thread_func, 0x10000100, 0x10000, 0, 0, NULL);
    if (g_audio_thid < 0) return g_audio_thid;
    sceKernelStartThread(g_audio_thid, 0, NULL);
    return 0;
}

int player_play(const char *filepath) {
    player_stop();

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));

    /* Allocatori memoria */
    init.memoryReplacement.objectPointer     = NULL;
    init.memoryReplacement.allocate          = av_alloc;
    init.memoryReplacement.deallocate        = av_free;
    init.memoryReplacement.allocateTexture   = av_gpu_alloc;
    init.memoryReplacement.deallocateTexture = av_gpu_free;

    /* Event callback — nome corretto dal header VitaSDK: eventReplacement */
    init.eventReplacement.objectPointer      = NULL;
    init.eventReplacement.eventCallback      = av_event_cb;

    init.basePriority               = 0xA0;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart                  = SCE_TRUE;

    g_player = sceAvPlayerInit(&init);
    if (g_player <= 0) {
        g_status.state = PLAYER_ERROR;
        return (int)g_player;
    }

    int ret = sceAvPlayerAddSource(g_player, filepath);
    if (ret < 0) {
        sceAvPlayerClose(g_player);
        g_player       = 0;
        g_status.state = PLAYER_ERROR;
        return ret;
    }

    g_has_video          = 0;
    g_status.state       = PLAYER_PLAYING;
    g_status.position_ms = 0;
    g_status.duration_ms = 0;
    return 0;
}

void player_toggle_pause(void) {
    if (g_player <= 0) return;
    if (g_status.state == PLAYER_PLAYING) {
        sceAvPlayerPause(g_player);
        g_status.state = PLAYER_PAUSED;
    } else if (g_status.state == PLAYER_PAUSED) {
        sceAvPlayerResume(g_player);
        g_status.state = PLAYER_PLAYING;
    }
}

void player_stop(void) {
    if (g_player > 0) {
        sceAvPlayerStop(g_player);
        sceAvPlayerClose(g_player);
        g_player = 0;
    }
    if (g_audio_port >= 0) {
        sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
    }
    free_video_texture();
    g_has_video          = 0;
    g_status.state       = PLAYER_STOPPED;
    g_status.position_ms = 0;
    g_status.duration_ms = 0;
}

PlayerState player_update(void) {
    if (g_player <= 0) return g_status.state;
    if (g_status.state == PLAYER_ERROR) return g_status.state;

    if (!sceAvPlayerIsActive(g_player)) {
        g_status.state = PLAYER_FINISHED;
        return g_status.state;
    }

    g_status.position_ms = (uint64_t)sceAvPlayerCurrentTime(g_player);
    return g_status.state;
}

void player_render_frame(vita2d_texture *tex) {
    (void)tex;
    if (g_player <= 0) return;

    SceAvPlayerFrameInfo frame;
    memset(&frame, 0, sizeof(frame));

    if (!sceAvPlayerGetVideoData(g_player, &frame)) return;
    if (!frame.pData) return;

    g_has_video = 1;

    int fw = (int)frame.details.video.width;
    int fh = (int)frame.details.video.height;
    if (fw <= 0 || fh <= 0) return;

    /* (Re)crea texture se le dimensioni cambiano */
    if (!g_video_tex || g_video_w != fw || g_video_h != fh) {
        free_video_texture();
        g_video_tex = vita2d_create_empty_texture(fw, fh);
        if (!g_video_tex) return;
        g_video_w = fw;
        g_video_h = fh;
    }

    /* Conversione SW YUV420 NV12 -> RGBA */
    void *tex_data = vita2d_texture_get_datap(g_video_tex);
    if (!tex_data) return;

    const uint8_t *y_plane  = (const uint8_t *)frame.pData;
    const uint8_t *uv_plane = y_plane + fw * fh;
    uint32_t      *rgba     = (uint32_t *)tex_data;

#define CLAMP8(x) ((x) < 0 ? 0 : (x) > 255 ? 255 : (x))
    for (int row = 0; row < fh; row++) {
        for (int col = 0; col < fw; col++) {
            int Y = y_plane[row * fw + col];
            int U = uv_plane[(row / 2) * fw + (col & ~1)]     - 128;
            int V = uv_plane[(row / 2) * fw + (col & ~1) + 1] - 128;
            int r = Y + (1402 * V) / 1000;
            int g = Y - (344  * U) / 1000 - (714 * V) / 1000;
            int b = Y + (1772 * U) / 1000;
            rgba[row * fw + col] = RGBA8(CLAMP8(r), CLAMP8(g), CLAMP8(b), 255);
        }
    }
#undef CLAMP8

    float sx = (float)SCREEN_W / (float)fw;
    float sy = (float)SCREEN_H / (float)fh;
    vita2d_draw_texture_scale(g_video_tex, 0.0f, 0.0f, sx, sy);
}

PlayerStatus player_get_status(void) {
    return g_status;
}

void player_shutdown(void) {
    player_stop();

    g_audio_run = 0;
    if (g_audio_thid >= 0) {
        sceKernelWaitThreadEnd(g_audio_thid, NULL, NULL);
        sceKernelDeleteThread(g_audio_thid);
        g_audio_thid = -1;
    }

    for (int i = 0; i < g_gpu_alloc_count; i++) {
        sceKernelFreeMemBlock(g_gpu_allocs[i].uid);
    }
    g_gpu_alloc_count = 0;
}

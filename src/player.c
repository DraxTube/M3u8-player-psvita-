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

/* -----------------------------------------------------------------------
   Memory callbacks for SceAvPlayer
   ----------------------------------------------------------------------- */
static void *av_alloc(void *p, uint32_t align, uint32_t size) {
    (void)p;
    void *ptr = NULL;
    if (align < 4) align = 4;
    /* posix_memalign expects power-of-2 alignment */
    size = (size + align - 1) & ~(align - 1);
    ptr = memalign(align, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

static void av_free(void *p, void *ptr) {
    (void)p;
    if (ptr) free(ptr);
}

static void *av_gpu_alloc(void *p, uint32_t align, uint32_t size) {
    (void)p; (void)align;
    if (g_gpu_alloc_count >= MAX_GPU_ALLOCS) return NULL;

    /* CDRAM must be 256KB aligned */
    size = (size + (256 * 1024 - 1)) & ~(256 * 1024 - 1);

    SceUID uid = sceKernelAllocMemBlock("AvGpu",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, NULL);
    if (uid < 0) return NULL;

    void *base = NULL;
    sceKernelGetMemBlockBase(uid, &base);

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

        if (sceAvPlayerGetAudioData(g_player, &frame)) {
            if (g_audio_port < 0 && frame.details.audio.sampleRate > 0) {
                g_audio_port = sceAudioOutOpenPort(
                    SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                    1024,
                    frame.details.audio.sampleRate,
                    (frame.details.audio.channelCount == 1)
                        ? SCE_AUDIO_OUT_MODE_MONO
                        : SCE_AUDIO_OUT_MODE_STEREO
                );
            }
            if (g_audio_port >= 0 && frame.pData) {
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
   Public API
   ----------------------------------------------------------------------- */
int player_init(void) {
    memset(&g_status, 0, sizeof(g_status));
    g_status.state = PLAYER_STOPPED;
    g_gpu_alloc_count = 0;

    /* Start audio thread */
    g_audio_run  = 1;
    g_audio_thid = sceKernelCreateThread("audio_out",
        audio_thread_func, 0x10000100, 0x10000, 0, 0, NULL);
    if (g_audio_thid >= 0)
        sceKernelStartThread(g_audio_thid, 0, NULL);

    return 0;
}

int player_play(const char *filepath) {
    player_stop();

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.objectPointer      = NULL;
    init.memoryReplacement.allocate            = av_alloc;
    init.memoryReplacement.deallocate          = av_free;
    init.memoryReplacement.allocateTexture     = av_gpu_alloc;
    init.memoryReplacement.deallocateTexture   = av_gpu_free;
    init.basePriority                          = 0xA0;
    init.numOutputVideoFrameBuffers            = 2;
    init.autoStart                             = SCE_TRUE;

    g_player = sceAvPlayerInit(&init);
    if (g_player <= 0) {
        g_status.state = PLAYER_ERROR;
        return -1;
    }

    int ret = sceAvPlayerAddSource(g_player, filepath);
    if (ret < 0) {
        sceAvPlayerClose(g_player);
        g_player = 0;
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
    /* Close audio port so it can be re-opened with new params */
    if (g_audio_port >= 0) {
        sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
    }
    g_has_video          = 0;
    g_status.state       = PLAYER_STOPPED;
    g_status.position_ms = 0;
    g_status.duration_ms = 0;
}

PlayerState player_update(void) {
    if (g_player <= 0) return g_status.state;

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

    if (sceAvPlayerGetVideoData(g_player, &frame)) {
        g_has_video = 1;
        /* For now, just consume the frame to keep the player advancing.
           Full video rendering requires GXM YUV texture setup which
           is complex. The player still outputs audio correctly. */
    }
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

    /* Free any remaining GPU allocations */
    for (int i = 0; i < g_gpu_alloc_count; i++) {
        sceKernelFreeMemBlock(g_gpu_allocs[i].uid);
    }
    g_gpu_alloc_count = 0;
}

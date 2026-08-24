#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <cstdint>
#include "../../Shared/SharedFrame.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("fsr-camera-source", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return "GPU shared-texture source for WebcamUpscaler.";
}

struct FsrCameraSource {
    obs_source_t *source = nullptr;
    fvc::SharedGpuFrameReader reader;

    uint64_t announcedHandle = 0;
    uint64_t lastFrame = UINT64_MAX;
    uint32_t width = 1920;
    uint32_t height = 1080;

    uint64_t cachedHandles[2]{};
    gs_texture_t *cachedTextures[2]{};
    gs_texture_t *currentTexture = nullptr;
};

static const char *fsr_source_name(void *)
{
    return "WebcamUpscaler";
}

static void *fsr_source_create(obs_data_t *, obs_source_t *source)
{
    auto *ctx = new FsrCameraSource();
    ctx->source = source;
    ctx->reader.Open();
    return ctx;
}

static void destroy_textures(FsrCameraSource *ctx)
{
    obs_enter_graphics();
    for (int i = 0; i < 2; ++i) {
        if (ctx->cachedTextures[i]) {
            gs_texture_destroy(ctx->cachedTextures[i]);
            ctx->cachedTextures[i] = nullptr;
        }
        ctx->cachedHandles[i] = 0;
    }
    ctx->currentTexture = nullptr;
    obs_leave_graphics();
}

static void fsr_source_destroy(void *data)
{
    auto *ctx = static_cast<FsrCameraSource *>(data);
    if (!ctx)
        return;
    destroy_textures(ctx);
    ctx->reader.Close();
    delete ctx;
}

static void fsr_video_tick(void *data, float)
{
    auto *ctx = static_cast<FsrCameraSource *>(data);
    if (!ctx)
        return;

    fvc::SharedGpuFrameHeader header{};
    if (!ctx->reader.Read(header)) {
        // The processor may have been started after the OBS source. Re-open
        // periodically until the shared metadata mapping exists.
        if (!ctx->reader.Open() || !ctx->reader.Read(header))
            return;
    }
    if (header.frameNumber == ctx->lastFrame)
        return;

    ctx->lastFrame = header.frameNumber;
    ctx->width = header.width;
    ctx->height = header.height;
    ctx->announcedHandle = header.sharedHandle;
}

static gs_texture_t *open_or_find_texture(FsrCameraSource *ctx, uint64_t handle)
{
    if (!handle)
        return nullptr;

    for (int i = 0; i < 2; ++i) {
        if (ctx->cachedHandles[i] == handle && ctx->cachedTextures[i])
            return ctx->cachedTextures[i];
    }

    int slot = -1;
    for (int i = 0; i < 2; ++i) {
        if (!ctx->cachedTextures[i]) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    if (ctx->cachedTextures[slot]) {
        gs_texture_destroy(ctx->cachedTextures[slot]);
        ctx->cachedTextures[slot] = nullptr;
        ctx->cachedHandles[slot] = 0;
    }

    // FSR Camera creates a legacy D3D11 shared handle. OBS opens the texture
    // directly on its graphics device: no 4K BGRA copy through system RAM.
    gs_texture_t *tex = gs_texture_open_shared(static_cast<uint32_t>(handle));
    if (!tex)
        return nullptr;

    ctx->cachedHandles[slot] = handle;
    ctx->cachedTextures[slot] = tex;
    return tex;
}

static void fsr_video_render(void *data, gs_effect_t *)
{
    auto *ctx = static_cast<FsrCameraSource *>(data);
    if (!ctx)
        return;

    if (ctx->announcedHandle)
        ctx->currentTexture = open_or_find_texture(ctx, ctx->announcedHandle);
    if (!ctx->currentTexture)
        return;

    obs_source_draw(ctx->currentTexture, 0, 0, ctx->width, ctx->height, false);
}

static uint32_t fsr_get_width(void *data)
{
    auto *ctx = static_cast<FsrCameraSource *>(data);
    return ctx ? ctx->width : 1920;
}

static uint32_t fsr_get_height(void *data)
{
    auto *ctx = static_cast<FsrCameraSource *>(data);
    return ctx ? ctx->height : 1080;
}

static obs_source_info fsr_source_info = {
    .id = "fsr_camera_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name = fsr_source_name,
    .create = fsr_source_create,
    .destroy = fsr_source_destroy,
    .get_width = fsr_get_width,
    .get_height = fsr_get_height,
    .video_tick = fsr_video_tick,
    .video_render = fsr_video_render,
    .icon_type = OBS_ICON_TYPE_CAMERA,
};

bool obs_module_load(void)
{
    obs_register_source(&fsr_source_info);
    blog(LOG_INFO, "WebcamUpscaler GPU shared-texture source loaded");
    return true;
}

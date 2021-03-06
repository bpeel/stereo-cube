/*
 * Stereoscopic cube example
 *
 * Based on the modesetting example written in 2012
 *  by David Herrmann <dh.herrmann@googlemail.com>
 * Modified 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <signal.h>

#include "gbm-winsys.h"
#include "util.h"

struct gbm_dev {
        int fd;
        uint32_t width;
        uint32_t height;
        uint32_t stride;

        drmModeModeInfo mode;
        uint32_t conn;
        uint32_t crtc;
        drmModeCrtc *saved_crtc;

        int pending_swap;
};

struct gbm_context {
        struct gbm_dev *dev;
        struct gbm_device *gbm;
        struct gbm_surface *gbm_surface;
        EGLDisplay edpy;
        EGLConfig egl_config;
        EGLSurface egl_surface;
        EGLContext egl_context;

        uint32_t current_fb_id;
        struct gbm_bo *current_bo;
};

struct gbm_options {
        const struct stereo_renderer *renderer;
        const char *card;
        const char *stereo_layout;
        int connector;
};

struct gbm_winsys {
        int fd;
        struct gbm_options options;
        struct gbm_dev *dev;
        struct gbm_context *context;
        const struct stereo_winsys_callbacks *callbacks;
        void *cb_data;
};

static int quit = 0;

#define MULTIVIEW_WINDOW_EXTENSION "EGL_EXT_multiview_window"

static int
stereo_find_crtc(drmModeRes *res, drmModeConnector *conn,
                 struct gbm_dev *dev)
{
        drmModeEncoder *enc;
        unsigned int i, j;
        int32_t crtc;

        /* first try the currently conected encoder+crtc */
        if (conn->encoder_id) {
                enc = drmModeGetEncoder(dev->fd, conn->encoder_id);
                if (enc->crtc_id >= 0) {
                        drmModeFreeEncoder(enc);
                        dev->crtc = enc->crtc_id;
                        return 0;
                }
        }

        /* If the connector is not currently bound to an encoder
         * iterate all other available encoders to find a matching
         * CRTC. */
        for (i = 0; i < conn->count_encoders; ++i) {
                enc = drmModeGetEncoder(dev->fd, conn->encoders[i]);
                if (!enc) {
                        fprintf(stderr,
                                "cannot retrieve encoder %u:%u (%d): %m\n",
                                i, conn->encoders[i], errno);
                        continue;
                }

                /* iterate all global CRTCs */
                for (j = 0; j < res->count_crtcs; ++j) {
                        /* check whether this CRTC works with the encoder */
                        if (!(enc->possible_crtcs & (1 << j)))
                                continue;

                        /* check that no other device already uses this CRTC */
                        crtc = res->crtcs[j];

                        /* we have found a CRTC, so save it and return */
                        if (crtc >= 0) {
                                drmModeFreeEncoder(enc);
                                dev->crtc = crtc;
                                return 0;
                        }
                }

                drmModeFreeEncoder(enc);
        }

        fprintf(stderr, "cannot find suitable CRTC for connector %u\n",
                conn->connector_id);
        return -ENOENT;
}

static int
get_mode_rank(const drmModeModeInfo *mode)
{
        int layout;
        int i;

        if (mode == NULL)
                return -1;

        layout = mode->flags & DRM_MODE_FLAG_3D_MASK;

        static const int ranks[] = {
                DRM_MODE_FLAG_3D_NONE,
                DRM_MODE_FLAG_3D_LINE_ALTERNATIVE,
                DRM_MODE_FLAG_3D_TOP_AND_BOTTOM,
                DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF,
                DRM_MODE_FLAG_3D_SIDE_BY_SIDE_FULL,
                DRM_MODE_FLAG_3D_FRAME_PACKING,
        };

        for (i = 0; i < sizeof(ranks) / sizeof(ranks[0]); i++)
                if (ranks[i] == layout)
                        return i;

        return -1;
}

static int
is_chosen_mode(const drmModeModeInfo *mode,
               const struct gbm_options *options,
               const drmModeModeInfo *old_mode)
{
        switch ((mode->flags & DRM_MODE_FLAG_3D_MASK)) {
        case DRM_MODE_FLAG_3D_NONE:
                return ((options->stereo_layout == NULL ||
                         !strcmp(options->stereo_layout, "none")) &&
                        get_mode_rank(mode) > get_mode_rank(old_mode));
        case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
                return ((options->stereo_layout == NULL ||
                         !strcmp(options->stereo_layout, "tb")) &&
                        get_mode_rank(mode) > get_mode_rank(old_mode));
        case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
                return ((options->stereo_layout == NULL ||
                         !strcmp(options->stereo_layout, "sbsh")) &&
                        get_mode_rank(mode) > get_mode_rank(old_mode));
        case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_FULL:
                return ((options->stereo_layout == NULL ||
                         !strcmp(options->stereo_layout, "sbsf")) &&
                        get_mode_rank(mode) > get_mode_rank(old_mode));
        case DRM_MODE_FLAG_3D_FRAME_PACKING:
                return ((options->stereo_layout == NULL ||
                         !strcmp(options->stereo_layout, "fp")) &&
                        get_mode_rank(mode) > get_mode_rank(old_mode));
        case DRM_MODE_FLAG_3D_LINE_ALTERNATIVE:
                return ((options->stereo_layout == NULL ||
                         !strcmp(options->stereo_layout, "la")) &&
                        get_mode_rank(mode) > get_mode_rank(old_mode));
        default:
                return 0;
        }
}

static int
find_mode(struct gbm_dev *dev, drmModeConnector *conn,
          const struct gbm_options *options)
{
        const drmModeModeInfo *old_mode = NULL;
        int i;

        for (i = 0; i < conn->count_modes; i++) {
                if (is_chosen_mode(conn->modes + i, options, old_mode)) {
                        dev->mode = conn->modes[i];
                        old_mode = &conn->modes[i];
                }
        }

        return old_mode ? 0 : -ENOENT;
}

static const char *
get_stereo_mode_name(int stereo_flags)
{
        switch (stereo_flags) {
        case DRM_MODE_FLAG_3D_NONE:
                return "none";
        case DRM_MODE_FLAG_3D_FRAME_PACKING:
                return "frame packing";
        case DRM_MODE_FLAG_3D_FIELD_ALTERNATIVE:
                return "field alternative";
        case DRM_MODE_FLAG_3D_LINE_ALTERNATIVE:
                return "line alternative";
        case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_FULL:
                return "side by side full";
        case DRM_MODE_FLAG_3D_L_DEPTH:
                return "l depth";
        case DRM_MODE_FLAG_3D_L_DEPTH_GFX_GFX_DEPTH:
                return "l depth gfx gfx depth";
        case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
                return "top and bottom";
        case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
                return "side by side half";
        default:
                return "unknown";
        }
}

static int
stereo_setup_dev(drmModeRes *res, drmModeConnector *conn,
                 const struct gbm_options *options,
                 struct gbm_dev *dev)
{
        int ret;

        /* check if a monitor is connected */
        if (conn->connection != DRM_MODE_CONNECTED) {
                fprintf(stderr, "ignoring unused connector %u\n",
                        conn->connector_id);
                return -ENOENT;
        }

        ret = find_mode(dev, conn, options);
        if (ret) {
                fprintf(stderr, "no valid mode for connector %u\n",
                        conn->connector_id);
                return ret;
        }

        /* copy the mode information into our device structure */
        dev->width = dev->mode.hdisplay;
        dev->height = dev->mode.vdisplay;
        fprintf(stderr, "mode for connector %u is %ux%u (%s)\n",
                conn->connector_id,
                dev->width, dev->height,
                get_stereo_mode_name(dev->mode.flags & DRM_MODE_FLAG_3D_MASK));

        /* find a crtc for this connector */
        ret = stereo_find_crtc(res, conn, dev);
        if (ret) {
                fprintf(stderr, "no valid crtc for connector %u\n",
                        conn->connector_id);
                return ret;
        }

        return 0;
}

static int
stereo_open(int *out, const struct gbm_options *options)
{
        const char *card = options->card;
        int fd, ret;

        if (card == NULL)
                card = "/dev/dri/card0";

        fd = open(card, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
                ret = -errno;
                fprintf(stderr, "cannot open '%s': %m\n", card);
                return ret;
        }

        if (drmSetClientCap(fd, DRM_CLIENT_CAP_STEREO_3D, 1)) {
                fprintf(stderr, "error setting stereo client cap: %m\n");
                close(fd);
                return -errno;
        }

        *out = fd;
        return 0;
}

static drmModeConnector *
get_connector(int fd, drmModeRes *res,
              const struct gbm_options *options)
{
        drmModeConnector *conn;
        int i;

        for (i = 0; i < res->count_connectors; i++) {
                conn = drmModeGetConnector(fd, res->connectors[i]);

                if (conn == NULL) {
                        fprintf(stderr,
                                "cannot retrieve DRM connector "
                                "%u:%u (%d): %m\n",
                                i, res->connectors[i], errno);
                        return NULL;
                }

                if (options->connector == -1 ||
                    conn->connector_id == options->connector)
                        return conn;
                drmModeFreeConnector(conn);
        }

        fprintf(stderr,
                "couldn't find connector with id %i\n",
                options->connector);

        return NULL;
}

static struct gbm_dev *
stereo_prepare_dev(int fd, const struct gbm_options *options)
{
        drmModeRes *res;
        drmModeConnector *conn;
        struct gbm_dev *dev;
        int ret;

        /* retrieve resources */
        res = drmModeGetResources(fd);
        if (!res) {
                fprintf(stderr, "cannot retrieve DRM resources (%d): %m\n",
                        errno);
                goto error;
        }

        conn = get_connector(fd, res, options);
        if (!conn)
                goto error_resources;

        /* create a device structure */
        dev = xmalloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));
        dev->conn = conn->connector_id;
        dev->fd = fd;

        /* call helper function to prepare this connector */
        ret = stereo_setup_dev(res, conn, options, dev);
        if (ret) {
                if (ret != -ENOENT) {
                        errno = -ret;
                        fprintf(stderr,
                                "cannot setup device for connector "
                                "%u:%u (%d): %m\n",
                                0, res->connectors[0], errno);
                }
                goto error_dev;
        }

        drmModeFreeConnector(conn);
        drmModeFreeResources(res);

        return dev;

error_dev:
        free(dev);
        drmModeFreeConnector(conn);
error_resources:
        drmModeFreeResources(res);
error:
        return NULL;
}

static void
restore_saved_crtc(struct gbm_dev *dev)
{
        /* restore saved CRTC configuration */
        if (dev->saved_crtc) {
                drmModeSetCrtc(dev->fd,
                               dev->saved_crtc->crtc_id,
                               dev->saved_crtc->buffer_id,
                               dev->saved_crtc->x,
                               dev->saved_crtc->y,
                               &dev->conn,
                               1,
                               &dev->saved_crtc->mode);
                drmModeFreeCrtc(dev->saved_crtc);

                dev->saved_crtc = NULL;
        }
}

static void
stereo_cleanup_dev(struct gbm_dev *dev)
{
        restore_saved_crtc(dev);

        /* free allocated memory */
        free(dev);
}

static void
free_current_bo(struct gbm_context *context)
{
        if (context->current_fb_id) {
                drmModeRmFB(context->dev->fd, context->current_fb_id);
                context->current_fb_id = 0;
        }
        if (context->current_bo) {
                gbm_surface_release_buffer(context->gbm_surface,
                                           context->current_bo);
                context->current_bo = NULL;
        }
}

static int
create_gbm_surface(struct gbm_context *context)
{
        const uint32_t flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
        const drmModeModeInfo *drm_mode = &context->dev->mode;
        struct gbm_bo_mode mode;

        switch ((drm_mode->flags & DRM_MODE_FLAG_3D_MASK)) {
        case DRM_MODE_FLAG_3D_NONE:
                mode.layout = GBM_BO_STEREO_LAYOUT_SIDE_BY_SIDE_HALF;
                break;
        case DRM_MODE_FLAG_3D_FRAME_PACKING:
                mode.layout = GBM_BO_STEREO_LAYOUT_FRAME_PACKING;
                break;
        case DRM_MODE_FLAG_3D_LINE_ALTERNATIVE:
                mode.layout = GBM_BO_STEREO_LAYOUT_LINE_ALTERNATIVE;
                break;
        case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_FULL:
                mode.layout = GBM_BO_STEREO_LAYOUT_SIDE_BY_SIDE_FULL;
                break;
        case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
                mode.layout = GBM_BO_STEREO_LAYOUT_TOP_AND_BOTTOM;
                break;
        case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
                mode.layout = GBM_BO_STEREO_LAYOUT_SIDE_BY_SIDE_HALF;
                break;
        default:
                fprintf(stderr,
                        "unknown DRM mode layout 0x%x\n",
                        (drm_mode->flags & DRM_MODE_FLAG_3D_MASK));
                return -ENOENT;
        }

        mode.hdisplay = drm_mode->hdisplay;
        mode.hsync_start = drm_mode->hsync_start;
        mode.hsync_end = drm_mode->hsync_end;
        mode.htotal = drm_mode->htotal;
        mode.vdisplay = drm_mode->vdisplay;
        mode.vsync_start = drm_mode->vsync_start;
        mode.vsync_end = drm_mode->vsync_end;
        mode.vtotal = drm_mode->vtotal;
        mode.format = GBM_BO_FORMAT_XRGB8888;

        context->gbm_surface =
                gbm_surface_create_with_mode(context->gbm, &mode, flags);

        if (context->gbm_surface == NULL) {
                fprintf(stderr, "error creating GBM surface\n");
                return -ENOENT;
        }

        return 0;
}

static int
choose_egl_config(struct gbm_context *context)
{
        static const EGLint attribs[] = {
                EGL_RED_SIZE, 1,
                EGL_GREEN_SIZE, 1,
                EGL_BLUE_SIZE, 1,
                EGL_ALPHA_SIZE, EGL_DONT_CARE,
                EGL_DEPTH_SIZE, 1,
                EGL_BUFFER_SIZE, EGL_DONT_CARE,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_NONE
        };
        EGLBoolean status;
        EGLint config_count;

        status = eglChooseConfig(context->edpy,
                                 attribs,
                                 &context->egl_config, 1,
                                 &config_count);
        if (status != EGL_TRUE || config_count < 1) {
                fprintf(stderr, "Unable to find a usable EGL configuration\n");
                return -ENOENT;
        }

        return 0;
}

static int
create_egl_surface(struct gbm_context *context,
                   const struct gbm_options *options)
{
        static const EGLint attribs_3d[] = {
                EGL_MULTIVIEW_VIEW_COUNT_EXT, 2,
                EGL_NONE
        };
        context->egl_surface =
                eglCreateWindowSurface(context->edpy,
                                       context->egl_config,
                                       (NativeWindowType) context->gbm_surface,
                                       attribs_3d);
        if (context->egl_surface == EGL_NO_SURFACE) {
                fprintf(stderr, "Failed to create EGL surface\n");
                return -ENOENT;
        }

        return 0;
}

static int
create_egl_context(struct gbm_context *context)
{
        static const EGLint attribs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL_NONE
        };

        context->egl_context = eglCreateContext(context->edpy,
                                                context->egl_config,
                                                EGL_NO_CONTEXT,
                                                attribs);
        if (context->egl_context == EGL_NO_CONTEXT) {
                fprintf(stderr, "Error creating EGL context\n");
                return -ENOENT;
        }

        return 0;
}

static int
extension_supported(EGLDisplay edpy, const char *ext)
{
        const char *exts = eglQueryString(edpy, EGL_EXTENSIONS);

        return extension_in_list(ext, exts);
}

static struct gbm_context *
stereo_prepare_context(struct gbm_dev *dev,
                       const struct gbm_options *options)
{
        struct gbm_context *context;
        EGLint multiview_view_count = 0;

        context = xmalloc(sizeof(*context));
        context->dev = dev;

        context->gbm = gbm_create_device(dev->fd);
        if (context->gbm == NULL) {
                fprintf(stderr, "error creating GBM device\n");
                goto error;
        }

        context->edpy = eglGetDisplay((EGLNativeDisplayType) context->gbm);
        if (context->edpy == EGL_NO_DISPLAY) {
                fprintf(stderr, "error getting EGL display\n");
                goto error_gbm_device;
        }

        if (!eglInitialize(context->edpy, NULL, NULL)) {
                fprintf(stderr, "error intializing EGL display\n");
                goto error_gbm_device;
        }

        if (!extension_supported(context->edpy, MULTIVIEW_WINDOW_EXTENSION)) {
                fprintf(stderr, MULTIVIEW_WINDOW_EXTENSION " not supported\n");
                goto error_egl_display;
        }

        if (create_gbm_surface(context))
                goto error_egl_display;

        if (choose_egl_config(context))
                goto error_gbm_surface;

        if (create_egl_surface(context, options))
                goto error_gbm_surface;

        if (create_egl_context(context))
                goto error_egl_surface;

        if (!eglMakeCurrent(context->edpy,
                            context->egl_surface,
                            context->egl_surface,
                            context->egl_context)) {
                fprintf(stderr, "failed to make EGL context current\n");
                goto error_egl_context;
        }

        if ((!eglQueryContext(context->edpy,
                              context->egl_context,
                              EGL_MULTIVIEW_VIEW_COUNT_EXT,
                              &multiview_view_count) ||
             multiview_view_count < 2)) {
                fprintf(stderr,
                        "EGL created a multiview surface with only %i %s\n",
                        multiview_view_count,
                        multiview_view_count == 1 ? "view" : "views");
                goto error_unbind;
        }

        return context;

error_unbind:
        eglMakeCurrent(context->edpy,
                       EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
error_egl_context:
        eglDestroyContext(context->edpy, context->egl_context);
error_egl_surface:
        eglDestroySurface(context->edpy, context->egl_surface);
error_gbm_surface:
        gbm_surface_destroy(context->gbm_surface);
error_egl_display:
        eglTerminate(context->edpy);
error_gbm_device:
        gbm_device_destroy(context->gbm);
error:
        free(context);
        return NULL;
}

static void
stereo_cleanup_context(struct gbm_context *context)
{
        restore_saved_crtc(context->dev);
        free_current_bo(context);
        eglMakeCurrent(context->edpy,
                       EGL_NO_SURFACE,
                       EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        eglDestroyContext(context->edpy, context->egl_context);
        eglDestroySurface(context->edpy, context->egl_surface);
        gbm_surface_destroy(context->gbm_surface);
        eglTerminate(context->edpy);
        gbm_device_destroy(context->gbm);
        free(context);
}

static void
page_flip_handler(int fd,
                  unsigned int frame,
                  unsigned int sec,
                  unsigned int usec,
                  void *data)
{
        struct gbm_dev *dev = data;

        dev->pending_swap = 0;
}

static void
wait_swap(struct gbm_dev *dev)
{
        drmEventContext evctx;

        while (dev->pending_swap) {
                memset(&evctx, 0, sizeof(evctx));
                evctx.version = DRM_EVENT_CONTEXT_VERSION;
                evctx.page_flip_handler = page_flip_handler;
                drmHandleEvent(dev->fd, &evctx);
        }
}

static int
set_initial_crtc(struct gbm_dev *dev, uint32_t fb_id)
{
        dev->saved_crtc = drmModeGetCrtc(dev->fd, dev->crtc);

        if (drmModeSetCrtc(dev->fd,
                           dev->crtc,
                           fb_id,
                           0, 0, /* x/y */
                           &dev->conn, 1,
                           &dev->mode)) {
                fprintf(stderr, "Failed to set drm mode: %m\n");
                return errno;
        }

        return 0;
}

static void
swap(struct gbm_winsys *winsys)
{
        struct gbm_dev *dev = winsys->dev;
        struct gbm_context *context = winsys->context;
        struct gbm_bo *bo;
        uint32_t handle, stride;
        uint32_t width, height;
        uint32_t fb_id;

        eglSwapBuffers(context->edpy, context->egl_surface);

        bo = gbm_surface_lock_front_buffer(context->gbm_surface);
        width = gbm_bo_get_width(bo);
        height = gbm_bo_get_height(bo);
        stride = gbm_bo_get_stride(bo);
        handle = gbm_bo_get_handle(bo).u32;

        if (drmModeAddFB(dev->fd,
                         width, height,
                         24, /* depth */
                         32, /* bpp */
                         stride,
                         handle,
                         &fb_id)) {
                fprintf(stderr,
                        "Failed to create new back buffer handle: %m\n");
        } else {
                if (dev->saved_crtc == NULL &&
                    set_initial_crtc(dev, fb_id))
                        return;

                if (drmModePageFlip(dev->fd,
                                    dev->crtc,
                                    fb_id,
                                    DRM_MODE_PAGE_FLIP_EVENT,
                                    dev)) {
                        fprintf(stderr, "Failed to page flip: %m\n");
                        return;
                }

                dev->pending_swap = 1;

                wait_swap(dev);

                free_current_bo(context);
                context->current_bo = bo;
                context->current_fb_id = fb_id;
        }
}

static void *
gbm_winsys_new(const struct stereo_winsys_callbacks *callbacks,
               void *cb_data)
{
        struct gbm_winsys *winsys = xmalloc(sizeof *winsys);

        memset(winsys, 0, sizeof *winsys);

        winsys->fd = -1;
        winsys->options.connector = -1;
        winsys->callbacks = callbacks;
        winsys->cb_data = cb_data;

        return winsys;
}

static int
gbm_winsys_handle_option(void *data, int opt)
{
        struct gbm_winsys *winsys = data;
        struct gbm_options *options = &winsys->options;

        switch (opt) {
        case 'd':
                options->card = optarg;
                return 1;
        case 'c':
                options->connector = atoi(optarg);
                return 1;
        case 'l':
                options->stereo_layout = optarg;
                return 1;
        }

        return 0;
}

static void
gbm_winsys_disconnect(struct gbm_winsys *winsys)
{
        if (winsys->context) {
                stereo_cleanup_context(winsys->context);
                winsys->context = NULL;
        }
        if (winsys->dev) {
                stereo_cleanup_dev(winsys->dev);
                winsys->dev = NULL;
        }
        if (winsys->fd != -1) {
                close(winsys->fd);
                winsys->fd = -1;
        }
}

static int
gbm_winsys_connect(void *data)
{
        struct gbm_winsys *winsys = data;
        int ret;

        /* open the DRM device */
        ret = stereo_open(&winsys->fd, &winsys->options);
        if (ret)
                goto error;

        /* prepare all connectors and CRTCs */
        winsys->dev = stereo_prepare_dev(winsys->fd, &winsys->options);
        if (winsys->dev == NULL) {
                ret = -ENOENT;
                goto error;
        }

        winsys->context = stereo_prepare_context(winsys->dev, &winsys->options);
        if (winsys->context == NULL) {
                ret = -ENOENT;
                goto error;
        }

        return 0;

error:
        gbm_winsys_disconnect(winsys);
        return ret;
}

static void
sigint_handler(int sig)
{
        quit = 1;
}

static void
update_size(struct gbm_winsys *winsys)
{
        struct gbm_context *context = winsys->context;
        EGLint width, height;

        eglQuerySurface(context->edpy, context->egl_surface,
                        EGL_WIDTH, &width);
        eglQuerySurface(context->edpy, context->egl_surface,
                        EGL_HEIGHT, &height);
        winsys->callbacks->update_size(winsys->cb_data, width, height);
}

static void
gbm_winsys_main_loop(void *data)
{
        struct gbm_winsys *winsys = data;
        struct sigaction action = {
                .sa_handler = sigint_handler,
        };
        struct sigaction old_action;

        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, &old_action);

        update_size(winsys);

        while (!quit) {
                winsys->callbacks->draw(winsys->cb_data);
                swap(winsys);
        }

        sigaction(SIGINT, &old_action, NULL);
}

static void
gbm_winsys_free(void *data)
{
        struct gbm_winsys *winsys = data;

        gbm_winsys_disconnect(winsys);
        free(winsys);
}

const struct stereo_winsys
gbm_winsys = {
        .name = "gbm",
        .options = "d:c:l:",
        .options_desc =
        "  -d <DEV>        Set the dri device to open\n"
        "  -c <CONNECTOR>  Use the given connector\n"
        "  -l <MODE>       Use a particular stereo mode "
        "(none/fp/la/sbsf/tb/sbsh)\n",
        .new = gbm_winsys_new,
        .handle_option = gbm_winsys_handle_option,
        .connect = gbm_winsys_connect,
        .main_loop = gbm_winsys_main_loop,
        .free = gbm_winsys_free
};

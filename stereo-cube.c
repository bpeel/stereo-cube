/*
 * Stereoscopic cube example
 *
 * Based on the modesetting example written in 2012
 *  by David Herrmann <dh.herrmann@googlemail.com>
 * Modified 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#define _GNU_SOURCE
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
#include <GLES2/gl2.h>

struct stereo_dev {
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

struct stereo_context {
        struct stereo_dev *dev;
        struct gbm_device *gbm;
        struct gbm_surface *gbm_surface;
        EGLDisplay edpy;
        EGLConfig egl_config;
        EGLSurface egl_surface;
        EGLContext egl_context;

        uint32_t current_fb_id;
        struct gbm_bo *current_bo;
};

struct options {
        const char *card;
        int connector;
};

static void *xmalloc(size_t size)
{
        void *res = malloc(size);

        if (res)
                return res;

        abort();
}

static int stereo_find_crtc(drmModeRes *res, drmModeConnector *conn,
                            struct stereo_dev *dev)
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

static int stereo_setup_dev(drmModeRes *res, drmModeConnector *conn,
                            struct stereo_dev *dev)
{
        int ret;

        /* check if a monitor is connected */
        if (conn->connection != DRM_MODE_CONNECTED) {
                fprintf(stderr, "ignoring unused connector %u\n",
                        conn->connector_id);
                return -ENOENT;
        }

        /* check if there is at least one valid mode */
        if (conn->count_modes == 0) {
                fprintf(stderr, "no valid mode for connector %u\n",
                        conn->connector_id);
                return -EFAULT;
        }

        /* copy the mode information into our device structure */
        memcpy(&dev->mode, &conn->modes[0], sizeof(dev->mode));
        dev->width = conn->modes[0].hdisplay;
        dev->height = conn->modes[0].vdisplay;
        fprintf(stderr, "mode for connector %u is %ux%u\n",
                conn->connector_id, dev->width, dev->height);

        /* find a crtc for this connector */
        ret = stereo_find_crtc(res, conn, dev);
        if (ret) {
                fprintf(stderr, "no valid crtc for connector %u\n",
                        conn->connector_id);
                return ret;
        }

        return 0;
}

static int stereo_open(int *out, const char *node)
{
        int fd, ret;
        uint64_t has_dumb;

        fd = open(node, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
                ret = -errno;
                fprintf(stderr, "cannot open '%s': %m\n", node);
                return ret;
        }

        if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
            !has_dumb) {
                fprintf(stderr,
                        "drm device '%s' does not support dumb buffers\n",
                        node);
                close(fd);
                return -EOPNOTSUPP;
        }

        *out = fd;
        return 0;
}

static drmModeConnector *get_connector(int fd, drmModeRes *res,
                                       int connector_id)
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

                if (connector_id == -1 || conn->connector_id == connector_id)
                        return conn;
                drmModeFreeConnector(conn);
        }

        fprintf(stderr, "couldn't find connector with id %i\n", connector_id);

        return NULL;
}

static struct stereo_dev *stereo_prepare_dev(int fd, int connector)
{
        drmModeRes *res;
        drmModeConnector *conn;
        struct stereo_dev *dev;
        int ret;

        /* retrieve resources */
        res = drmModeGetResources(fd);
        if (!res) {
                fprintf(stderr, "cannot retrieve DRM resources (%d): %m\n",
                        errno);
                goto error;
        }

        conn = get_connector(fd, res, connector);
        if (!conn)
                goto error_resources;

        /* create a device structure */
        dev = xmalloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));
        dev->conn = conn->connector_id;
        dev->fd = fd;

        /* call helper function to prepare this connector */
        ret = stereo_setup_dev(res, conn, dev);
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

static void restore_saved_crtc(struct stereo_dev *dev)
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

static void stereo_cleanup_dev(struct stereo_dev *dev)
{
        restore_saved_crtc(dev);

        /* free allocated memory */
        free(dev);
}

static void free_current_bo(struct stereo_context *context)
{
  if (context->current_fb_id) {
          drmModeRmFB(context->dev->fd, context->current_fb_id);
          context->current_fb_id = 0;
  }
  if (context->current_bo) {
          gbm_surface_release_buffer(context->gbm_surface, context->current_bo);
          context->current_bo = NULL;
  }
}

static int create_gbm_surface(struct stereo_context *context)
{
        const uint32_t flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
        const drmModeModeInfo *drm_mode = &context->dev->mode;
        struct gbm_bo_mode mode;

        switch ((drm_mode->flags & DRM_MODE_FLAG_3D_MASK)) {
        case DRM_MODE_FLAG_3D_NONE:
                mode.layout = GBM_BO_STEREO_LAYOUT_NONE;
                break;
        case DRM_MODE_FLAG_3D_FRAME_PACKING:
                mode.layout = GBM_BO_STEREO_LAYOUT_FRAME_PACKING;
                break;
        case DRM_MODE_FLAG_3D_FIELD_ALTERNATIVE:
                mode.layout = GBM_BO_STEREO_LAYOUT_FIELD_ALTERNATIVE;
                break;
        case DRM_MODE_FLAG_3D_LINE_ALTERNATIVE:
                mode.layout = GBM_BO_STEREO_LAYOUT_LINE_ALTERNATIVE;
                break;
        case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_FULL:
                mode.layout = GBM_BO_STEREO_LAYOUT_SIDE_BY_SIDE_FULL;
                break;
        case DRM_MODE_FLAG_3D_L_DEPTH:
                mode.layout = GBM_BO_STEREO_LAYOUT_L_DEPTH;
                break;
        case DRM_MODE_FLAG_3D_L_DEPTH_GFX_GFX_DEPTH:
                mode.layout = GBM_BO_STEREO_LAYOUT_L_DEPTH_GFX_GFX_DEPTH;
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

static int choose_egl_config(struct stereo_context *context)
{
        static const EGLint attribs[] = {
                EGL_RED_SIZE, 1,
                EGL_GREEN_SIZE, 1,
                EGL_BLUE_SIZE, 1,
                EGL_ALPHA_SIZE, EGL_DONT_CARE,
                EGL_DEPTH_SIZE, 0,
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

static int create_egl_surface(struct stereo_context *context)
{
        context->egl_surface =
                eglCreateWindowSurface(context->edpy,
                                       context->egl_config,
                                       (NativeWindowType) context->gbm_surface,
                                       NULL);
        if (context->egl_surface == EGL_NO_SURFACE) {
                fprintf(stderr, "Failed to create EGL surface\n");
                return -ENOENT;
        }

        return 0;
}

static int create_egl_context(struct stereo_context *context)
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

static struct stereo_context *stereo_prepare_context(struct stereo_dev *dev)
{
        struct stereo_context *context;

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

        if (create_gbm_surface(context))
                goto error_egl_display;

        if (choose_egl_config(context))
                goto error_gbm_surface;

        if (create_egl_surface(context))
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

        return context;

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

static void stereo_cleanup_context(struct stereo_context *context)
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
        struct stereo_dev *dev = data;

        dev->pending_swap = 0;
}

static void wait_swap(struct stereo_dev *dev)
{
        drmEventContext evctx;

        while (dev->pending_swap) {
                memset(&evctx, 0, sizeof(evctx));
                evctx.version = DRM_EVENT_CONTEXT_VERSION;
                evctx.page_flip_handler = page_flip_handler;
                drmHandleEvent(dev->fd, &evctx);
        }
}

static int set_initial_crtc(struct stereo_dev *dev, uint32_t fb_id)
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

static void swap(struct stereo_context *context)
{
        struct stereo_dev *dev = context->dev;
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

static void draw(struct stereo_context *context)
{
        int i;

        for (i = 0; i < 256; i++) {
                glClearColor(1.0f - i / 255.0f, 0.0, 0.0, 0.0);
                glClear(GL_COLOR_BUFFER_BIT);
                swap(context);
        }
}

static void usage(void)
{
        printf("usage: stereo-cube [OPTION]...\n"
               "\n"
               "  -h              Show this help message\n"
               "  -d <DEV>        Set the dri device to open\n"
               "  -c <CONNECTOR>  Use the given connector\n");
        exit(0);
}

static int process_options(struct options *options, int argc, char **argv)
{
        static const char args[] = "-hd:c:";
        int opt;

        memset(options, 0, sizeof(*options));

        options->connector = -1;

        while ((opt = getopt(argc, argv, args)) != -1) {
                switch (opt) {
                case 'h':
                        usage();
                        break;
                case 'd':
                        options->card = optarg;
                        break;
                case 'c':
                        options->connector = atoi(optarg);
                        break;
                case '?':
                case ':':
                        return -ENOENT;
                case '\1':
                        fprintf(stderr, "unexpected argument \"%s\"\n", optarg);
                        return -ENOENT;
                }
        }

        if (options->card == NULL)
                options->card = "/dev/dri/card0";

        return 0;
}

int main(int argc, char **argv)
{
        int ret, fd;
        struct options options;
        struct stereo_dev *dev;
        struct stereo_context *context;

        ret = process_options(&options, argc, argv);
        if (ret)
                goto out_return;

        /* open the DRM device */
        ret = stereo_open(&fd, options.card);
        if (ret)
                goto out_return;

        /* prepare all connectors and CRTCs */
        dev = stereo_prepare_dev(fd, options.connector);
        if (dev == NULL) {
                ret = -ENOENT;
                goto out_close;
        }

        context = stereo_prepare_context(dev);
        if (context == NULL) {
                ret = -ENOENT;
                goto out_dev;
        }

        draw(context);

        ret = 0;

        /* cleanup everything */
        stereo_cleanup_context(context);
out_dev:
        stereo_cleanup_dev(dev);
out_close:
        close(fd);
out_return:
        return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

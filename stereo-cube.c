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

struct stereo_dev {
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t size;
        uint32_t handle;
        uint8_t *map;

        drmModeModeInfo mode;
        uint32_t fb;
        uint32_t conn;
        uint32_t crtc;
        drmModeCrtc *saved_crtc;
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

static int stereo_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
                            struct stereo_dev *dev)
{
        drmModeEncoder *enc;
        unsigned int i, j;
        int32_t crtc;

        /* first try the currently conected encoder+crtc */
        if (conn->encoder_id) {
                enc = drmModeGetEncoder(fd, conn->encoder_id);
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
                enc = drmModeGetEncoder(fd, conn->encoders[i]);
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

static int stereo_create_fb(int fd, struct stereo_dev *dev)
{
        struct drm_mode_create_dumb creq;
        struct drm_mode_destroy_dumb dreq;
        struct drm_mode_map_dumb mreq;
        int ret;

        /* create dumb buffer */
        memset(&creq, 0, sizeof(creq));
        creq.width = dev->width;
        creq.height = dev->height;
        creq.bpp = 32;
        ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
        if (ret < 0) {
                fprintf(stderr, "cannot create dumb buffer (%d): %m\n",
                        errno);
                return -errno;
        }
        dev->stride = creq.pitch;
        dev->size = creq.size;
        dev->handle = creq.handle;

        /* create framebuffer object for the dumb-buffer */
        ret = drmModeAddFB(fd, dev->width, dev->height, 24, 32, dev->stride,
                           dev->handle, &dev->fb);
        if (ret) {
                fprintf(stderr, "cannot create framebuffer (%d): %m\n",
                        errno);
                ret = -errno;
                goto err_destroy;
        }

        /* prepare buffer for memory mapping */
        memset(&mreq, 0, sizeof(mreq));
        mreq.handle = dev->handle;
        ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
        if (ret) {
                fprintf(stderr, "cannot map dumb buffer (%d): %m\n",
                        errno);
                ret = -errno;
                goto err_fb;
        }

        /* perform actual memory mapping */
        dev->map = mmap(0, dev->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        fd, mreq.offset);
        if (dev->map == MAP_FAILED) {
                fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n",
                        errno);
                ret = -errno;
                goto err_fb;
        }

        /* clear the framebuffer to 0 */
        memset(dev->map, 0, dev->size);

        return 0;

err_fb:
        drmModeRmFB(fd, dev->fb);
err_destroy:
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = dev->handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return ret;
}

static int stereo_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
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
        ret = stereo_find_crtc(fd, res, conn, dev);
        if (ret) {
                fprintf(stderr, "no valid crtc for connector %u\n",
                        conn->connector_id);
                return ret;
        }

        /* create a framebuffer for this CRTC */
        ret = stereo_create_fb(fd, dev);
        if (ret) {
                fprintf(stderr, "cannot create framebuffer for connector %u\n",
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

        /* call helper function to prepare this connector */
        ret = stereo_setup_dev(fd, res, conn, dev);
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

static uint8_t next_color(bool *up, uint8_t cur, unsigned int mod)
{
        uint8_t next;

        next = cur + (*up ? 1 : -1) * (rand() % mod);
        if ((*up && next < cur) || (!*up && next > cur)) {
                *up = !*up;
                next = cur;
        }

        return next;
}

static void stereo_draw(struct stereo_dev *dev)
{
        uint8_t r, g, b;
        bool r_up, g_up, b_up;
        unsigned int i, j, k, off;

        srand(time(NULL));
        r = rand() % 0xff;
        g = rand() % 0xff;
        b = rand() % 0xff;
        r_up = g_up = b_up = true;

        for (i = 0; i < 50; ++i) {
                r = next_color(&r_up, r, 20);
                g = next_color(&g_up, g, 10);
                b = next_color(&b_up, b, 5);

                for (j = 0; j < dev->height; ++j) {
                        for (k = 0; k < dev->width; ++k) {
                                off = dev->stride * j + k * 4;
                                *(uint32_t*)&dev->map[off] =
                                        (r << 16) | (g << 8) | b;
                        }
                }

                usleep(100000);
        }
}

static void stereo_cleanup_dev(int fd, struct stereo_dev *dev)
{
        struct drm_mode_destroy_dumb dreq;

        /* restore saved CRTC configuration */
        drmModeSetCrtc(fd,
                       dev->saved_crtc->crtc_id,
                       dev->saved_crtc->buffer_id,
                       dev->saved_crtc->x,
                       dev->saved_crtc->y,
                       &dev->conn,
                       1,
                       &dev->saved_crtc->mode);
        drmModeFreeCrtc(dev->saved_crtc);

        /* unmap buffer */
        munmap(dev->map, dev->size);

        /* delete framebuffer */
        drmModeRmFB(fd, dev->fb);

        /* delete dumb buffer */
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = dev->handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

        /* free allocated memory */
        free(dev);
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

        ret = process_options(&options, argc, argv);
        if (ret)
                goto out_return;

        /* open the DRM device */
        ret = stereo_open(&fd, options.card);
        if (ret)
                goto out_return;

        /* prepare all connectors and CRTCs */
        dev = stereo_prepare_dev(fd, options.connector);
        if (dev == NULL)
                goto out_close;

        /* perform actual modesetting on the dev */
        dev->saved_crtc = drmModeGetCrtc(fd, dev->crtc);
        ret = drmModeSetCrtc(fd, dev->crtc, dev->fb, 0, 0,
                             &dev->conn, 1, &dev->mode);
        if (ret) {
                fprintf(stderr,
                        "cannot set CRTC for connector %u (%d): %m\n",
                        dev->conn, errno);
                goto out_dev;
        }

        /* draw some colors for 5seconds */
        stereo_draw(dev);

        ret = 0;

out_dev:
        /* cleanup everything */
        stereo_cleanup_dev(fd, dev);
out_close:
        close(fd);
out_return:
        return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

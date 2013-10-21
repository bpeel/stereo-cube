/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <stdlib.h>
#include <stdio.h>

#include "stereo-renderer.h"
#include "util.h"

#define BOX_SIZE 128

struct stereo_renderer {
        PFNGLDRAWBUFFERSINDEXEDEXTPROC draw_buffers_indexed;
        int width, height;
};

struct stereo_renderer *stereo_renderer_new(void)
{
        struct stereo_renderer *renderer;
        const char *exts = (const char *) glGetString(GL_EXTENSIONS);

        if (!extension_in_list("GL_EXT_multiview_draw_buffers", exts)) {
                fprintf(stderr,
                        "missing GL_EXT_multiview_draw_buffers extension\n");
                return NULL;
        }

        renderer = xmalloc(sizeof *renderer);

        renderer->draw_buffers_indexed =
                (void *) eglGetProcAddress("glDrawBuffersIndexedEXT");

        return renderer;
}

static void draw_box(int x, int y)
{
        int width, height;

        x -= BOX_SIZE / 2;
        y -= BOX_SIZE / 2;

        if (x < 0) {
                width = BOX_SIZE + x;
                x = 0;
        } else {
                width = BOX_SIZE;
        }
        if (y < 0) {
                height = BOX_SIZE + y;
                y = 0;
        } else {
                height = BOX_SIZE;
        }
        if (width <= 0 || height <= 0)
                return;

        glScissor(x, y, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
}

void set_eye(struct stereo_renderer *renderer, int eye)
{
        GLenum locations[] = { GL_MULTIVIEW_EXT };
        GLint indexes[] = { eye };

        renderer->draw_buffers_indexed(1, locations, indexes);
}

void stereo_renderer_draw_frame(struct stereo_renderer *renderer,
                                int frame_num)
{
        glClearColor(0.0, 0.0, 1.0, 0.0);
        set_eye(renderer, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        set_eye(renderer, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST);

        set_eye(renderer, 0);
        glClearColor(1.0, 0.0, 0.0, 0.0);
        draw_box(renderer->width / 2 - BOX_SIZE * 3 / 8, renderer->height / 2);

        set_eye(renderer, 1);
        glClearColor(0.0, 1.0, 0.0, 0.0);
        draw_box(renderer->width / 2 + BOX_SIZE * 3 / 8, renderer->height / 2);

        glDisable(GL_SCISSOR_TEST);
}

void stereo_renderer_resize(struct stereo_renderer *renderer,
                            int width, int height)
{
        renderer->width = width;
        renderer->height = height;
}

void stereo_renderer_free(struct stereo_renderer *renderer)
{
        free(renderer);
}

/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include <GLES2/gl2.h>
#include <stdlib.h>

#include "stereo-renderer.h"
#include "util.h"

#define BOX_SIZE 128

struct stereo_renderer {
};

struct stereo_renderer *stereo_renderer_new(void)
{
        struct stereo_renderer *renderer = xmalloc(sizeof *renderer);

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

void stereo_renderer_draw_frame(struct stereo_renderer *renderer,
                                const struct stereo_renderer_box *left_box,
                                const struct stereo_renderer_box *right_box,
                                int frame_num)
{
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST);
        glClearColor(1.0, 0.0, 0.0, 0.0);

        draw_box(left_box->x + left_box->width / 2 - frame_num,
                 left_box->y + left_box->height / 2);
        draw_box(right_box->x + right_box->width / 2 + frame_num,
                 right_box->y + right_box->height / 2);

        glDisable(GL_SCISSOR_TEST);
}

void stereo_renderer_free(struct stereo_renderer *renderer)
{
        free(renderer);
}

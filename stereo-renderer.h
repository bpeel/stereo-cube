/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#ifndef STEREO_RENDERER_H
#define STEREO_RENDERER_H

struct stereo_renderer;

struct stereo_renderer_box {
   uint32_t x, y;
   uint32_t width, height;
};

struct stereo_renderer *stereo_renderer_new(void);
void stereo_renderer_draw_frame(struct stereo_renderer *renderer,
                                const struct stereo_renderer_box *left_box,
                                const struct stereo_renderer_box *right_box,
                                int frame_num);
void stereo_renderer_free(struct stereo_renderer *renderer);

#endif /* STEREO_RENDERER_H */

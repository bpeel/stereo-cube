/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#ifndef STEREO_RENDERER_H
#define STEREO_RENDERER_H

struct stereo_renderer
{
        const char *name;
        const char *options;
        const char *options_desc;
        void *(* new)(void);
        int (* handle_option)(void *renderer, int opt);
        int (* connect)(void *renderer);
        void (* draw_frame)(void *renderer,
                            int frame_num);
        void (* resize)(void *renderer,
                        int width, int height);
        void (* free)(void *renderer);
};

extern const struct stereo_renderer const *
stereo_renderers[];

#endif /* STEREO_RENDERER_H */

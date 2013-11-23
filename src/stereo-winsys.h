/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#ifndef STEREO_WINSYS_H
#define STEREO_WINSYS_H

struct stereo_winsys_callbacks
{
        void (* update_size)(void *data,
                             int width,
                             int height);
        void (* draw)(void *data);
};

struct stereo_winsys
{
        const char *name;
        const char *options;
        const char *options_desc;
        void *(* new)(const struct stereo_winsys_callbacks *callbacks,
                      void *cb_data);
        int (* handle_option)(void *winsys, int opt);
        int (* connect)(void *winsys);
        void (* main_loop)(void *winsys);
        void (* free)(void *winsys);
};

extern const struct stereo_winsys const *
stereo_winsyss[];

#endif /* STEREO_WINSYS_H */

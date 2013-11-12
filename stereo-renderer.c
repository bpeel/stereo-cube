/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include <stdlib.h>

#include "stereo-renderer.h"

#include "gears-renderer.h"

const struct stereo_renderer const *
stereo_renderers[] = {
        &gears_renderer,
        NULL
};

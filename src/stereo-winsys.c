/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include "config.h"

#include <stdlib.h>

#include "stereo-winsys.h"

#include "gbm-winsys.h"
#include "wayland-winsys.h"

const struct stereo_winsys const *
stereo_winsyss[] = {
        &wayland_winsys,
        &gbm_winsys,
        NULL
};

/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include <stdlib.h>

#include "util.h"

void *xmalloc(size_t size)
{
        void *res = malloc(size);

        if (res)
                return res;

        abort();
}

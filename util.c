/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include <stdlib.h>
#include <string.h>

#include "util.h"

void *xmalloc(size_t size)
{
        void *res = malloc(size);

        if (res)
                return res;

        abort();
}

int extension_in_list(const char *ext, const char *exts)
{
        int ext_len = strlen(ext);

        while (1) {
                char *end;

                while (*exts == ' ')
                        exts++;

                if (*exts == '\0')
                        return 0;

                end = strchr(exts, ' ');

                if (end == NULL)
                        return !strcmp(exts, ext);

                if (end - exts == ext_len && !memcmp(exts, ext, ext_len))
                        return 1;

                exts = end + 1;
        }
}

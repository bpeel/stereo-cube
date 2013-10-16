/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

void *xmalloc(size_t size);

int extension_in_list(const char *ext, const char *exts);

#endif /* UTIL_H */

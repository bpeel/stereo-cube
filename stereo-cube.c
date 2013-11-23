/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stereo-winsys.h"
#include "stereo-renderer.h"
#include "util.h"

struct stereo_cube {
        const struct stereo_winsys *winsys;
        void *winsys_data;

        const struct stereo_renderer *renderer;
        void *renderer_data;

        int frame_num;
};

static void
usage(void)
{
        int i;

        printf("usage: stereo-cube [OPTION]...\n"
               "\n"
               "  -h              Show this help message\n"
               "  -L              List available renderers\n"
               "  -r <RENDERER>   Select a renderer\n"
               "  -w <WINSYS>     Pick a winsys\n");

        for (i = 0; stereo_winsyss[i]; i++) {
                if (stereo_winsyss[i]->options_desc) {
                        printf("\n"
                               "Options for the %s winsys:\n"
                               "%s\n",
                               stereo_winsyss[i]->name,
                               stereo_winsyss[i]->options_desc);
                }
        }

        for (i = 0; stereo_renderers[i]; i++) {
                if (stereo_renderers[i]->options_desc) {
                        printf("\n"
                               "Options for the %s renderer:\n"
                               "%s\n",
                               stereo_renderers[i]->name,
                               stereo_renderers[i]->options_desc);
                }
        }

        exit(0);
}

static void
list_renderers(void)
{
        int i;

        printf("Available renderers:\n");

        for (i = 0; stereo_renderers[i]; i++)
                printf("%s\n", stereo_renderers[i]->name);

        exit(0);
}

static const struct stereo_renderer *
select_renderer(const char *name)
{
        int i;

        for (i = 0; stereo_renderers[i]; i++)
                if (!strcmp(stereo_renderers[i]->name, name))
                        return stereo_renderers[i];

        return NULL;
}

static const struct stereo_winsys *
select_winsys(const char *name)
{
        int i;

        for (i = 0; stereo_winsyss[i]; i++)
                if (!strcmp(stereo_winsyss[i]->name, name))
                        return stereo_winsyss[i];

        return NULL;
}

static void
update_size(void *data,
            int width,
            int height)
{
        struct stereo_cube *cube = data;

        cube->renderer->resize(cube->renderer_data, width, height);
}

static void
draw(void *data)
{
        struct stereo_cube *cube = data;

        cube->renderer->draw_frame(cube->renderer_data, cube->frame_num++);
}

static struct stereo_winsys_callbacks winsys_callbacks = {
        .update_size = update_size,
        .draw = draw,
};

static void
create_winsys(struct stereo_cube *cube)
{
        if (cube->winsys_data == NULL)
                cube->winsys_data = cube->winsys->new(&winsys_callbacks, cube);
}

static void
create_renderer(struct stereo_cube *cube)
{
        if (cube->renderer_data == NULL)
                cube->renderer_data =
                        cube->renderer->new();
}

static int
process_options(struct stereo_cube *cube, int argc, char **argv)
{
        static const char default_args[] = "-hLr:w:";
        char args[256];
        int i, opt;

        strcpy(args, default_args);

        for (i = 0; stereo_winsyss[i]; i++)
                if (stereo_winsyss[i]->options)
                        strcat(args, stereo_winsyss[i]->options);
        for (i = 0; stereo_renderers[i]; i++)
                if (stereo_renderers[i]->options)
                        strcat(args, stereo_renderers[i]->options);

        while ((opt = getopt(argc, argv, args)) != -1) {
                switch (opt) {
                case 'h':
                        usage();
                        break;
                case 'L':
                        list_renderers();
                        break;
                case 'w':
                        if (cube->winsys_data) {
                                fprintf(stderr,
                                        "Winsys chosen seen after "
                                        "winsys option seen\n");
                                return -ENOENT;
                        }
                        cube->winsys = select_winsys(optarg);
                        if (cube->winsys == NULL) {
                                fprintf(stderr,
                                        "unknown winsys \"%s\"\n",
                                        optarg);
                                return -ENOENT;
                        }
                        break;
                case 'r':
                        if (cube->renderer_data) {
                                fprintf(stderr,
                                        "Renderer chosen seen after "
                                        "renderer option seen\n");
                                return -ENOENT;
                        }
                        cube->renderer = select_renderer(optarg);
                        if (cube->renderer == NULL) {
                                fprintf(stderr,
                                        "unknown renderer \"%s\"\n",
                                        optarg);
                                return -ENOENT;
                        }
                        break;
                case ':':
                case '?':
                        return -ENOENT;

                case '\1':
                        fprintf(stderr, "unexpected argument \"%s\"\n", optarg);
                        return -ENOENT;
                default:
                        create_winsys(cube);
                        if (cube->winsys->handle_option &&
                            cube->winsys->handle_option(cube->winsys_data, opt))
                                break;
                        create_renderer(cube);
                        if (cube->renderer->handle_option &&
                            cube->renderer->handle_option(cube->renderer_data,
                                                          opt))
                                break;
                        fprintf(stderr, "unexpected argument \"-%c\"\n", opt);
                        return -ENOENT;
                }
        }

        return 0;
}

int
main(int argc, char **argv)
{
        int ret;
        struct stereo_cube cube;

        memset(&cube, 0, sizeof(cube));

        cube.renderer = stereo_renderers[0];
        cube.winsys = stereo_winsyss[0];

        ret = process_options(&cube, argc, argv);
        if (ret)
                goto out;

        create_winsys(&cube);
        create_renderer(&cube);

        ret = cube.winsys->connect(cube.winsys_data);
        if (ret)
                goto out;

        ret = cube.renderer->connect(cube.renderer_data);
        if (ret)
                goto out;

        cube.winsys->main_loop(cube.winsys_data);

out:
        /* cleanup everything */
        if (cube.renderer_data)
                cube.renderer->free(cube.renderer_data);
        if (cube.winsys_data)
                cube.winsys->free(cube.winsys_data);
        return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "stereo-renderer.h"
#include "util.h"

struct image_renderer {
        GLuint program;
        GLuint textures[2];

        const char *image_names[2];
};

static const char image_vertex_source[] =
        "attribute mediump vec2 pos;\n"
        "varying mediump vec2 tex_coord;\n"
        "\n"
        "void main()\n"
        "{\n"
        "        gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
        "        tex_coord = vec2(pos.x, 1.0 - pos.y);\n"
        "}\n";
static const char image_fragment_source[] =
        "uniform sampler2D tex[2];\n"
        "varying mediump vec2 tex_coord;\n"
        "\n"
        "void main()\n"
        "{\n"
        "        gl_FragData[0] = texture2D(tex[0], tex_coord);\n"
        "        gl_FragData[1] = texture2D(tex[1], tex_coord);\n"
        "}\n";

void *
image_renderer_new(void)
{
        struct image_renderer *renderer = xmalloc(sizeof *renderer);

        memset(renderer, 0, sizeof *renderer);

        return renderer;
}

static int
next_p2 (int a)
{
        int rval = 1;

        while (rval < a)
                rval <<= 1;

        return rval;
}

static GLuint
load_texture(const char *image_name, GError **error)
{
        GdkPixbuf *pixbuf;
        int width, height, p2_width, p2_height;
        GLuint tex;
        GLenum format;

        pixbuf = gdk_pixbuf_new_from_file(image_name, error);
        if (pixbuf == NULL)
                return 0;

        format = gdk_pixbuf_get_has_alpha(pixbuf) ? GL_RGBA : GL_RGB;

        width = gdk_pixbuf_get_width(pixbuf);
        height = gdk_pixbuf_get_height(pixbuf);
        p2_width = next_p2(width);
        p2_height = next_p2(height);

        if (width != p2_width || height != p2_height) {
                GdkPixbuf *scaled_pixbuf =
                        gdk_pixbuf_scale_simple(pixbuf,
                                                p2_width,
                                                p2_height,
                                                GDK_INTERP_BILINEAR);
                g_object_unref(pixbuf);
                pixbuf = scaled_pixbuf;
                width = p2_width;
                height = p2_height;
        }

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D,
                     0, /* level */
                     format, /* internal format */
                     width, height,
                     0, /* border */
                     format,
                     GL_UNSIGNED_BYTE,
                     gdk_pixbuf_get_pixels(pixbuf));
        glTexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_2D);

        g_object_unref(pixbuf);

        return tex;
}

static int
image_renderer_connect(void *data)
{
        struct image_renderer *renderer = data;
        const char *exts = (const char *) glGetString(GL_EXTENSIONS);
        PFNGLDRAWBUFFERSINDEXEDEXTPROC draw_buffers_indexed;
        static const GLenum locations[] =
                { GL_MULTIVIEW_EXT, GL_MULTIVIEW_EXT };
        static const GLint indices[] = { 0, 1 };
        GLuint tex_location;
        int i;

        if (!extension_in_list("GL_EXT_multiview_draw_buffers", exts)) {
                fprintf(stderr,
                        "missing GL_EXT_multiview_draw_buffers extension\n");
                return -ENOENT;
        }

        for (i = 0; i < 2; i++) {
                GError *error = NULL;

                if (renderer->image_names[i] == NULL) {
                        fprintf(stderr,
                                "Missing -%c option\n",
                                i + '1');
                        return -ENOENT;
                }
                renderer->textures[i] = load_texture(renderer->image_names[i],
                                                     &error);
                if (renderer->textures[i] == 0) {
                        fprintf(stderr,
                                "%s: %s\n",
                                renderer->image_names[i],
                                error->message);
                        g_error_free(error);
                        return -ENOENT;
                }
        }

        draw_buffers_indexed =
                (void *) eglGetProcAddress("glDrawBuffersIndexedEXT");

        draw_buffers_indexed(2, locations, indices);

        renderer->program = create_program(image_vertex_source,
                                           image_fragment_source,
                                           "pos",
                                           NULL);

        glUseProgram(renderer->program);

        tex_location = glGetUniformLocation(renderer->program, "tex");
        glUniform1iv(tex_location, 2, indices);

        for (i = 0; i < 2; i++) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, renderer->textures[i]);
        }

        return 0;
}

static void
image_renderer_draw_frame(void *data, int frame_num)
{
        static const float vertices[] = {
                0.0f, 0.0f,
                1.0f, 0.0f,
                0.0f, 1.0f,
                1.0f, 1.0f
        };

        glVertexAttribPointer(0, /* index */
                              2, /* size */
                              GL_FLOAT,
                              GL_FALSE, /* not normalized */
                              sizeof(float) * 2,
                              vertices);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
}

static void
image_renderer_resize(void *data,
                      int width, int height)
{
        glViewport(0, 0, width, height);
}

static int
image_renderer_handle_option(void *data, int opt)
{
        struct image_renderer *renderer = data;

        switch (opt) {
        case '1':
                renderer->image_names[0] = optarg;
                return 1;
        case '2':
                renderer->image_names[1] = optarg;
                return 1;
        }

        return 0;
}

static void
image_renderer_free(void *data)
{
        struct image_renderer *renderer = data;
        int i;

        for (i = 0; i < 2; i++)
                if (renderer->textures[i])
                        glDeleteTextures(1, renderer->textures + i);

        if (renderer->program)
                glDeleteProgram(renderer->program);

        free(renderer);
}

const struct stereo_renderer image_renderer = {
        .name = "image",
        .options = "1:2:",
        .options_desc =
        "  -1 <LEFT_IMG>   Set the left image file\n"
        "  -2 <RIGHT_IMG>  Set the right image file\n",
        .new = image_renderer_new,
        .handle_option = image_renderer_handle_option,
        .connect = image_renderer_connect,
        .draw_frame = image_renderer_draw_frame,
        .resize = image_renderer_resize,
        .free = image_renderer_free,
};

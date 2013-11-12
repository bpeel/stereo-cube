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

#include "stereo-renderer.h"
#include "util.h"

struct depth_renderer {
        PFNGLDRAWBUFFERSINDEXEDEXTPROC draw_buffers_indexed;
        int width, height;

        GLuint program;
        GLuint color_location;
};

static GLuint create_shader(GLenum type, const char *source)
{
        GLuint shader = glCreateShader(type);
        GLint length = strlen(source);
        GLint status;

        glShaderSource(shader, 1, &source, &length);
        glCompileShader(shader);

        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

        if (status == 0) {
                char info_log[512];
                GLsizei info_log_length;

                glGetShaderInfoLog(shader,
                                    sizeof(info_log) - 1,
                                    &info_log_length,
                                    info_log);
                fprintf(stderr, "%.*s\n", info_log_length, info_log);

                glDeleteShader(shader);

                return 0;
        }

        return shader;
}

static GLuint create_program(void)
{
        GLuint shader, program;
        GLint status;
        static const char vertex_source[] =
                "attribute highp vec3 pos;\n"
                "\n"
                "void main()\n"
                "{\n"
                "        gl_Position = vec4(pos, 1.0);\n"
                "}\n";
        static const char fragment_source[] =
                "uniform highp vec4 color;\n"
                "\n"
                "void main()\n"
                "{\n"
                "        gl_FragColor = color;\n"
                "}\n";

        program = glCreateProgram();

        shader = create_shader(GL_VERTEX_SHADER, vertex_source);
        if (shader) {
                glAttachShader(program, shader);
                glDeleteShader(shader);
        }

        shader = create_shader(GL_FRAGMENT_SHADER, fragment_source);
        if (shader) {
                glAttachShader(program, shader);
                glDeleteShader(shader);
        }

        glBindAttribLocation(program, 0, "pos");

        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &status);

        if (status == 0) {
                char info_log[512];
                GLsizei info_log_length;

                glGetProgramInfoLog(program,
                                    sizeof(info_log) - 1,
                                    &info_log_length,
                                    info_log);
                fprintf(stderr, "%.*s\n", info_log_length, info_log);

                glDeleteProgram(program);

                return 0;
        }

        return program;
}

void *depth_renderer_new(void)
{
        struct depth_renderer *renderer;
        const char *exts = (const char *) glGetString(GL_EXTENSIONS);

        if (!extension_in_list("GL_EXT_multiview_draw_buffers", exts)) {
                fprintf(stderr,
                        "missing GL_EXT_multiview_draw_buffers extension\n");
                return NULL;
        }

        renderer = xmalloc(sizeof *renderer);

        renderer->draw_buffers_indexed =
                (void *) eglGetProcAddress("glDrawBuffersIndexedEXT");

        renderer->program = create_program();

        renderer->color_location =
                glGetUniformLocation(renderer->program, "color");

        return renderer;
}

static void set_eye(struct depth_renderer *renderer, int eye)
{
        GLenum locations[] = { GL_MULTIVIEW_EXT };
        GLint indexes[] = { eye };

        renderer->draw_buffers_indexed(1, locations, indexes);
}

static void draw_square(struct depth_renderer *renderer,
                        float x, float y,
                        uint32_t color,
                        float depth)
{
        float color_array[] = {
                (color >> 24) / 255.0f,
                ((color >> 16) & 0xff) / 255.0f,
                ((color >> 8) & 0xff) / 255.0f,
                (color & 0xff) / 255.0f,
        };
        float vertices[] = {
                x, y, depth,
                x + 1.0f, y, depth,
                x, y + 1.0f, depth,
                x + 1.0f, y + 1.0f, depth
        };

        glUniform4fv(renderer->color_location, 1, color_array);
        glVertexAttribPointer(0, /* index */
                              3, /* size */
                              GL_FLOAT,
                              GL_FALSE, /* not normalized */
                              sizeof(float) * 3,
                              vertices);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
}

void depth_renderer_draw_frame(void *data,
                               int frame_num)
{
        struct depth_renderer *renderer = data;
        glUseProgram(renderer->program);

        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        set_eye(renderer, 0);

        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_TRUE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        draw_square(renderer, -1.0f, -1.0f, 0x000000ff, 0.0f);
        draw_square(renderer, 0.0f, -1.0f, 0x000000ff, 0.25f);
        draw_square(renderer, -1.0f, 0.0f, 0x000000ff, 0.5f);
        draw_square(renderer, 0.0f, 0.0f, 0x000000ff, 0.75f);

        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        draw_square(renderer, -0.5f, -0.5f, 0xff0000ff, 0.3f);

        set_eye(renderer, 1);

        glClear(GL_COLOR_BUFFER_BIT);

        draw_square(renderer, -0.5f, -0.5f, 0x0000ffff, 0.6f);
}

void depth_renderer_resize(void *data,
                           int width, int height)
{
        struct depth_renderer *renderer = data;
        renderer->width = width;
        renderer->height = height;
}

void depth_renderer_free(void *data)
{
        struct depth_renderer *renderer = data;

        glDeleteProgram(renderer->program);
        free(renderer);
}


const struct stereo_renderer depth_renderer = {
        .name = "depth",
        .new = depth_renderer_new,
        .draw_frame = depth_renderer_draw_frame,
        .resize = depth_renderer_resize,
        .free = depth_renderer_free,
};
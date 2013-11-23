/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "util.h"

void *
xmalloc(size_t size)
{
        void *res = malloc(size);

        if (res)
                return res;

        abort();
}

int
extension_in_list(const char *ext, const char *exts)
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

static GLuint
create_shader(GLenum type, const char *source)
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

GLuint
create_program(const char *vertex_source,
               const char *fragment_source,
               ...)
{
        GLuint shader, program;
        GLint status;
        const char *attrib;
        va_list ap;
        int i = 0;

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

        va_start(ap, fragment_source);

        for (i = 0; (attrib = va_arg(ap, const char *)); i++)
                glBindAttribLocation(program, i, attrib);

        va_end(ap);

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

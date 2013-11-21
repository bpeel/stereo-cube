/*
 * Stereoscopic cube example
 *
 * Written 2013 by Neil Roberts <neil@linux.intel.com>
 * Dedicated to the Public Domain.
 */

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <linux/input.h>

#include "wayland-winsys.h"
#include "util.h"

struct geometry {
        int width, height;
};

struct wayland_winsys {
        const struct stereo_winsys_callbacks *callbacks;
        void *cb_data;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;

        EGLDisplay edpy;
        EGLContext ctx;
        EGLConfig egl_config;

	struct wl_egl_window *native_window;
	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;
	EGLSurface egl_surface;

        struct wl_callback *frame_callback;

        struct wl_list seats;

        int fullscreen;
        struct geometry window_size;
        struct geometry old_size;
};

struct seat {
        struct wayland_winsys *winsys;
        uint32_t name;
        struct wl_seat *seat;
        struct wl_list link;
        struct wl_keyboard *keyboard;
};

static int quit = 0;

#define MULTIVIEW_WINDOW_EXTENSION "EGL_EXT_multiview_window"

static void redraw(struct wayland_winsys *winsys);

static int extension_supported(EGLDisplay edpy, const char *ext)
{
        const char *exts = eglQueryString(edpy, EGL_EXTENSIONS);

        return extension_in_list(ext, exts);
}

static void *wayland_winsys_new(const struct stereo_winsys_callbacks *callbacks,
                                void *cb_data)
{
        struct wayland_winsys *winsys = xmalloc(sizeof *winsys);

        memset(winsys, 0, sizeof *winsys);

        winsys->callbacks = callbacks;
        winsys->cb_data = cb_data;

        wl_list_init(&winsys->seats);

        return winsys;
}

static void remove_seat(struct seat *seat)
{
        if (seat->keyboard)
                wl_keyboard_destroy(seat->keyboard);
        wl_seat_destroy(seat->seat);
        wl_list_remove(&seat->link);
        free(seat);
}

static void update_size(struct wayland_winsys *winsys)
{
        winsys->callbacks->update_size(winsys->cb_data,
                                       winsys->window_size.width,
                                       winsys->window_size.height);
}

static void set_size(struct wayland_winsys *winsys,
                     int width,
                     int height)
{
        winsys->window_size.width = width;
        winsys->window_size.height = height;

	if (winsys->native_window)
		wl_egl_window_resize(winsys->native_window,
                                     width, height,
                                     0, 0);

        update_size(winsys);
}

static void handle_ping(void *data,
                        struct wl_shell_surface *shell_surface,
                        uint32_t serial)
{
	wl_shell_surface_pong(shell_surface, serial);
}

static void handle_configure(void *data,
                             struct wl_shell_surface *shell_surface,
                             uint32_t edges, int32_t width, int32_t height)
{
	struct wayland_winsys *winsys = data;

        set_size(winsys, width, height);
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
	handle_ping,
	handle_configure,
	handle_popup_done
};

static void
toggle_fullscreen(struct wayland_winsys *winsys, int fullscreen)
{
        if (!!fullscreen == winsys->fullscreen)
                return;

	winsys->fullscreen = !!fullscreen;

	if (fullscreen) {
                const int method = WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT;
                winsys->old_size = winsys->window_size;
		wl_shell_surface_set_fullscreen(winsys->shell_surface,
                                                method, 0, NULL);
	} else {
		wl_shell_surface_set_toplevel(winsys->shell_surface);
                set_size(winsys,
                         winsys->old_size.width,
                         winsys->old_size.height);
	}
}

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                                   uint32_t format, int fd, uint32_t size)
{
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                                  uint32_t serial, struct wl_surface *surface,
                                  struct wl_array *keys)
{
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                                  uint32_t serial, struct wl_surface *surface)
{
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                                uint32_t serial, uint32_t time, uint32_t key,
                                uint32_t state)
{
	struct wayland_winsys *winsys = data;

	if (key == KEY_F11 && state)
		toggle_fullscreen(winsys, !winsys->fullscreen);
}

static void keyboard_handle_modifiers(void *data,
                                      struct wl_keyboard *keyboard,
                                      uint32_t serial,
                                      uint32_t mods_depressed,
                                      uint32_t mods_latched,
                                      uint32_t mods_locked,
                                      uint32_t group)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,
	keyboard_handle_enter,
	keyboard_handle_leave,
	keyboard_handle_key,
	keyboard_handle_modifiers,
};

static void handle_capabilities(void *data,
                                struct wl_seat *wl_seat,
                                uint32_t capabilities)
{
        struct seat *seat = data;

        if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD)) {
                if (seat->keyboard == NULL) {
                        seat->keyboard = wl_seat_get_keyboard(wl_seat);
                        wl_keyboard_add_listener(seat->keyboard,
                                                 &keyboard_listener,
                                                 seat->winsys);
                }
        } else if (seat->keyboard) {
                wl_keyboard_destroy(seat->keyboard);
                seat->keyboard = NULL;
        }
}

static const struct wl_seat_listener seat_listener = {
        handle_capabilities
};

static void add_seat(struct wayland_winsys *winsys,
                     uint32_t name)
{
        struct seat *seat = xmalloc(sizeof *seat);

        seat->seat = wl_registry_bind(winsys->registry,
                                      name, &wl_seat_interface, 1);
        seat->name = name;
        seat->winsys = winsys;
        seat->keyboard = NULL;
        wl_list_insert(&winsys->seats, &seat->link);

        wl_seat_add_listener(seat->seat, &seat_listener, seat);
}

static void registry_handle_global(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name,
                                   const char *interface,
                                   uint32_t version)
{
	struct wayland_winsys *winsys = data;

	if (strcmp(interface, "wl_compositor") == 0 &&
            winsys->compositor == NULL) {
		winsys->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_shell") == 0 &&
                winsys->shell == NULL) {
		winsys->shell = wl_registry_bind(registry, name,
                                                 &wl_shell_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
                add_seat(winsys, name);
        }
}

static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name)
{
        struct wayland_winsys *winsys = data;
        struct seat *seat;

        wl_list_for_each(seat, &winsys->seats, link) {
                if (seat->name == name) {
                        remove_seat(seat);
                        break;
                }
        }
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void wayland_winsys_disconnect(struct wayland_winsys *winsys)
{
        struct seat *seat, *tmp;

        wl_list_for_each_safe(seat, tmp, &winsys->seats, link)
                remove_seat(seat);

        if (winsys->egl_surface) {
                eglMakeCurrent(winsys->edpy,
                               EGL_NO_SURFACE, EGL_NO_SURFACE,
                               EGL_NO_CONTEXT);
                eglDestroySurface(winsys->edpy, winsys->egl_surface);
                winsys->egl_surface = NULL;
        }

        if (winsys->native_window) {
                wl_egl_window_destroy(winsys->native_window);
                winsys->native_window = NULL;
        }

        if (winsys->shell_surface) {
                wl_shell_surface_destroy(winsys->shell_surface);
                winsys->shell_surface = NULL;
        }

        if (winsys->surface) {
                wl_surface_destroy(winsys->surface);
                winsys->surface = NULL;
        }

        if (winsys->frame_callback) {
                wl_callback_destroy(winsys->frame_callback);
                winsys->frame_callback = NULL;
        }

        if (winsys->ctx) {
                eglDestroyContext(winsys->edpy, winsys->ctx);
                winsys->ctx = NULL;
        }

        if (winsys->edpy) {
                eglTerminate(winsys->edpy);
                winsys->edpy = NULL;
        }

        if (winsys->registry) {
                wl_registry_destroy(winsys->registry);
                winsys->registry = NULL;
        }

        if (winsys->display) {
                wl_display_disconnect(winsys->display);
                winsys->display = NULL;
        }
}

static int init_egl(struct wayland_winsys *winsys)
{
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
                EGL_DEPTH_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLint major, minor, count;
	EGLBoolean ret;

        winsys->edpy = eglGetDisplay((EGLNativeDisplayType) winsys->display);
        if (winsys->edpy == NULL)
                return -1;

        if (!eglInitialize(winsys->edpy, &major, &minor))
                return -1;

	if (!eglBindAPI(EGL_OPENGL_ES_API))
                return -1;

        ret = eglChooseConfig(winsys->edpy,
                              config_attribs,
                              &winsys->egl_config, 1,
                              &count);
        if (ret != EGL_TRUE || count < 1)
                return -1;

	winsys->ctx = eglCreateContext(winsys->edpy,
                                       winsys->egl_config,
                                       EGL_NO_CONTEXT,
                                       context_attribs);
        if (winsys->ctx == NULL)
                return -1;

        return 0;
}

static int create_surface(struct wayland_winsys *winsys)
{
	static const EGLint attribs_3d[] = {
		EGL_MULTIVIEW_VIEW_COUNT_EXT, 2,
		EGL_NONE
	};
	int has_multiview_view_count;

	has_multiview_view_count =
		extension_supported(winsys->edpy, "EGL_EXT_multiview_window");

	winsys->surface = wl_compositor_create_surface(winsys->compositor);
	winsys->shell_surface = wl_shell_get_shell_surface(winsys->shell,
							   winsys->surface);

        wl_shell_surface_add_listener(winsys->shell_surface,
                                      &shell_surface_listener,
                                      winsys);

        winsys->window_size.width = 800;
        winsys->window_size.height = 600;

	winsys->native_window =
		wl_egl_window_create(winsys->surface,
                                     winsys->window_size.width,
                                     winsys->window_size.height);
	winsys->egl_surface =
		eglCreateWindowSurface(winsys->edpy,
				       winsys->egl_config,
				       (EGLNativeWindowType)
                                       winsys->native_window,
				       has_multiview_view_count ?
				       attribs_3d :
				       NULL);

	wl_shell_surface_set_title(winsys->shell_surface, "stereo-cube");
        wl_shell_surface_set_toplevel(winsys->shell_surface);

	if (!eglMakeCurrent(winsys->edpy,
                            winsys->egl_surface,
                            winsys->egl_surface,
                            winsys->ctx))
                return -1;

        return 0;
}

static int wayland_winsys_connect(void *data)
{
        struct wayland_winsys *winsys = data;

        winsys->display = wl_display_connect(NULL);
        if (winsys->display == NULL)
                goto error;

        winsys->registry = wl_display_get_registry(winsys->display);
        wl_registry_add_listener(winsys->registry,
                                 &registry_listener,
                                 winsys);

        wl_display_roundtrip(winsys->display);

        if (winsys->shell == NULL ||
            winsys->compositor == NULL)
                goto error;

        if (init_egl(winsys) == -1 ||
            create_surface(winsys) == -1)
                goto error;

        return 0;

error:
        wayland_winsys_disconnect(winsys);
        return -1;
}

static void sigint_handler(int sig)
{
        quit = 1;
}

static void frame_cb(void *data, struct wl_callback *callback, uint32_t time)
{
        struct wayland_winsys *winsys = data;

        if (callback)
                wl_callback_destroy(callback);
        winsys->frame_callback = NULL;

        redraw(winsys);
}

static const struct wl_callback_listener frame_listener = {
	frame_cb
};

static void redraw(struct wayland_winsys *winsys)
{
        winsys->callbacks->draw(winsys->cb_data);

        winsys->frame_callback = wl_surface_frame(winsys->surface);
        wl_callback_add_listener(winsys->frame_callback,
                                 &frame_listener,
                                 winsys);

        eglSwapBuffers(winsys->edpy, winsys->egl_surface);
}

static void wayland_winsys_main_loop(void *data)
{
        struct wayland_winsys *winsys = data;
        struct sigaction action = {
                .sa_handler = sigint_handler,
        };
        struct sigaction old_action;

        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, &old_action);

        update_size(winsys);

        redraw(winsys);

        while (!quit) {
                if (wl_display_dispatch(winsys->display) == -1)
                        break;
        }

        sigaction(SIGINT, &old_action, NULL);
}

static void wayland_winsys_free(void *data)
{
        struct wayland_winsys *winsys = data;

        wayland_winsys_disconnect(winsys);
        free(winsys);
}

const struct stereo_winsys
wayland_winsys = {
        .name = "wayland",
        .new = wayland_winsys_new,
        .connect = wayland_winsys_connect,
        .main_loop = wayland_winsys_main_loop,
        .free = wayland_winsys_free
};

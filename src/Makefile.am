bin_PROGRAMS = stereo-cube

AM_CPPFLAGS = \
	$(WAYLAND_CFLAGS) \
	$(GBM_CFLAGS) \
	$(DRM_CFLAGS) \
	$(GL_CFLAGS) \
	$(GDK_PIXBUF_CFLAGS) \
	$(STEREO_CUBE_EXTRA_CFLAGS) \
	$(NULL)

stereo_cube_SOURCES = \
	depth-renderer.c \
	depth-renderer.h \
	gbm-winsys.c \
	gbm-winsys.h \
	gears-renderer.c \
	gears-renderer.h \
	image-renderer.c \
	image-renderer.h \
	stereo-cube.c \
	stereo-renderer.c \
	stereo-renderer.h \
	stereo-winsys.c \
	stereo-winsys.h \
	util.c \
	util.h \
	wayland-winsys.c \
	wayland-winsys.h \
	$(NULL)

stereo_cube_LDFLAGS = \
	$(WAYLAND_LIBS) \
	$(GBM_LIBS) \
	$(DRM_LIBS) \
	$(GL_LIBS) \
	$(GDK_PIXBUF_LIBS) \
	$(LIBM) \
	$(NULL)

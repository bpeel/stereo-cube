CC=gcc
PKGS=gbm egl glesv2 gl libdrm wayland-client wayland-egl gdk-pixbuf-2.0
LDFLAGS=`pkg-config $(PKGS) --libs` -lm
CFLAGS=-g -Wall -O0 `pkg-config $(PKGS) --cflags`
OBJS = \
	stereo-cube.o \
	stereo-renderer.o \
	stereo-winsys.o \
	util.o \
	gears-renderer.o \
	depth-renderer.o \
	image-renderer.o \
	gbm-winsys.o \
	wayland-winsys.o

all : stereo-cube

stereo-cube.c : stereo-renderer.h util.h
stereo-renderer.c : stereo-renderer.h util.h
gears-renderer.c : stereo-renderer.h util.h
depth-renderer.c : stereo-renderer.h util.h
image-renderer.c : stereo-renderer.h util.h
gbm-winsys.c : stereo-winsys.h util.h
wayland-winsys.c : stereo-winsys.h util.h
util.c : util.h

stereo-cube : $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

.c.o :
	$(CC) -c $(CFLAGS) -o $@ $<

clean :
	rm -f *.o stereo-cube

.PHONY : clean all

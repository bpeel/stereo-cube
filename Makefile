CC=gcc
PKGS=gbm egl glesv2 gl libdrm
LDFLAGS=`pkg-config $(PKGS) --libs`
CFLAGS=-g -Wall -O0 `pkg-config $(PKGS) --cflags`
OBJS = stereo-cube.o stereo-renderer.o util.o

all : stereo-cube

stereo-cube.c : stereo-renderer.h util.h
stereo-renderer.c : stereo-renderer.h util.h
util.c : util.h

stereo-cube : $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

.c.o :
	$(CC) -c $(CFLAGS) -o $@ $<

clean :
	rm -f *.o stereo-cube

.PHONY : clean all

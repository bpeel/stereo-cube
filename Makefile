CC=gcc
LDFLAGS=`pkg-config libdrm --libs`
CFLAGS=-g -Wall -O0 `pkg-config libdrm --cflags`
OBJS = stereo-cube.o

all : stereo-cube

stereo-cube : $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

.c.o :
	$(CC) -c $(CFLAGS) -o $@ $<

clean :
	rm -f *.o stereo-cube

.PHONY : clean all

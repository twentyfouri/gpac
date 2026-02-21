include ../../../config.mak

vpath %.c $(SRC_PATH)/applications/testapps/mp4muxdemux

CFLAGS= $(OPTFLAGS) -I"$(SRC_PATH)/include"

ifeq ($(DEBUGBUILD),yes)
CFLAGS+=-g
LDFLAGS+=-g
endif

ifeq ($(GPROFBUILD),yes)
CFLAGS+=-pg
LDFLAGS+=-pg
endif

OBJS= main.o
RT_OBJS= realtime_example.o realtime_mp4.o

LINKFLAGS=-L../../../bin/gcc
ifeq ($(CONFIG_WIN32),yes)
EXE=.exe
PROG=mp4muxdemux$(EXE)
RT_PROG=realtime_example$(EXE)
else
EXT=
PROG=mp4muxdemux
RT_PROG=realtime_example
endif
LINKFLAGS+=-lgpac

SRCS := $(OBJS:.o=.c) $(RT_OBJS:.o=.c)

all: $(PROG) $(RT_PROG)

$(PROG): $(OBJS)
	$(CC) -o ../../../bin/gcc/$@ $(OBJS) $(LINKFLAGS) $(LDFLAGS)

$(RT_PROG): $(RT_OBJS)
	$(CC) -o ../../../bin/gcc/$@ $(RT_OBJS) $(LINKFLAGS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(RT_OBJS) ../../../bin/gcc/$(PROG) ../../../bin/gcc/$(RT_PROG)

dep: depend

depend:
	rm -f .depend
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

distclean: clean
	rm -f Makefile.bak .depend

-include .depend

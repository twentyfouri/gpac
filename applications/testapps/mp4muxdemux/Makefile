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

LINKFLAGS=-L../../../bin/gcc
ifeq ($(CONFIG_WIN32),yes)
EXE=.exe
PROG=mp4muxdemux$(EXE)
else
EXT=
PROG=mp4muxdemux
endif
LINKFLAGS+=-lgpac

SRCS := $(OBJS:.o=.c)

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o ../../../bin/gcc/$@ $(OBJS) $(LINKFLAGS) $(LDFLAGS)

clean:
	rm -f $(OBJS) ../../../bin/gcc/$(PROG)

dep: depend

depend:
	rm -f .depend
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

distclean: clean
	rm -f Makefile.bak .depend

-include .depend

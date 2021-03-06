# Makefile for LittleModPlayer
#
# The player is intended to be integrated into a different project, but this
# builds the module as a standalone test thingo.
#


CFLAGS = -O3 -DLMP_TEST_MAIN

all:	lmp


lmp:	littlemodplayer.c littlemodplayer.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f lmp *~


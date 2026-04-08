# Makefile for the feed project
# The C compiler for this project: https://github.com/rui314/chibicc
# Otherwise us gcc instead
CFLAGS=  -D_GNU_SOURCE -Wall -g -std=c99 -Wno-implicit-function-declaration
CC = gcc $(CFLAGS)
LDLIBS = -lreadline
.c.o:
	$(CC) -c $< 
feed: feed.o
	$(CC) -o $@ $< $(LDLIBS)
clean: 
	rm feed *.o
lint:	feed.c
	LARCH_PATH=/usr/local/share/splint/lib splint -weak -posixlib -unrecog +longintegral $<

indent:	feed.c
	indent -nut $<

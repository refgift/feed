# Makefile for the feed project
CFLAGS=  -D_GNU_SOURCE -Wall -g -std=c99 -Wno-implicit-function-declaration 
CC = gcc $(CFLAGS)
LDLIBS = -lreadline

.c.o:
	$(CC) -c $< 

feed: feed.o
	$(CC) -o $@ $< $(LDLIBS)

clean: 
	rm -f feed.exe feed.o
	
lint:	feed.c
	LARCH_PATH=/usr/local/share/splint/lib splint -weak -posixlib -unrecog +longintegral $<

indent:	feed.c
	indent -nut $<

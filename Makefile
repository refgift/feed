# Makefile for the feed project
# The C compiler for this project: https://github.com/rui314/chibicc
# Otherwise us gcc instead
CFLAGS=  -D_GNU_SOURCE -Wall -g -std=c99 -Wno-implicit-function-declaration
CC = gcc $(CFLAGS)
.c.o:
	$(CC) -c $< 
feed: feed.o
	$(CC) -o $@ $< 
clean: 
	rm feed *.o
lint:	feed.c
	splint -weak -posixlib -unrecog $<

indent:	feed.c
	indent -nut $<

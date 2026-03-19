# Makefile for the feed project
# The C compiler for this project: https://github.com/rui314/chibicc
# Otherwise us gcc instead
CFLAGS=  -D_POSIX_C_SOURCE -D_GNU_SOURCE -Wall -g
CC = gcc $(CFLAGS)
.c.o:
	$(CC) -c $< 
feed: feed.o
	$(CC) -o $@ $< 
clean: 
	rm feed *.o
lint:	feed.c
	splint -weak -posixlib -unrecog $<

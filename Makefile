# Makefile for the feed project
# The C compiler for this project: https://github.com/rui314/chibicc
# Otherwise us gcc instead

CC = chibicc

.c.o:
	$(CC) -c $< 

feed: feed.o
	$(CC) -o $@ $< 

clean: 
	rm feed *.o

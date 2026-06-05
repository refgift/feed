# Makefile for the feed project
CFLAGS=  -D_GNU_SOURCE -Wall -Wextra -Wconversion -Werror -g -std=c99 -Wno-implicit-function-declaration 
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

# Convert DOS/Windows CRLF (or lone CR) to Unix LF only.
# Run this (or "make tidy") if you see ^M in files or after cross-platform edits.
# This helps source quality, diff cleanliness, and tools like "quality".
dos2unix: feed.c Makefile README.md feed.1
	dos2unix feed.c Makefile README.md feed.1

# Tidy: line endings + optional indent (if indent(1) is installed)
tidy: dos2unix
	-indent -nut feed.c 2>/dev/null || true

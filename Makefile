# Makefile for the feed project
CFLAGS=  -D_GNU_SOURCE -Wall -Wextra -Wconversion -Werror -g -std=c99 -Wno-implicit-function-declaration 
CC = cc $(CFLAGS)
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

# Autohot / ritual engraft: runs the full heating check after edits.
# Use: make hot-check   (or after your edit)
# This helps "see the difference with quality2" quickly.
hot-check:
	@make clean > /dev/null 2>&1 && make > /dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
	@echo "=== Tests ==="
	@./feed -t | tail -1
	@echo "=== quality2 (global) ==="
	@/usr/local/bin/quality2 feed.c
	@echo "=== perfunc longest (for next target) ==="
	@python3 /tmp/perfunc_quality.py --longest feed.c | head -8
	@echo "=== Done. If temp improved (less negative), log it as hotting edit. ==="

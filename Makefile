CFLAGS = -lm -I ./include -O -ggdb -Wall
CC = gcc


all: xfsr-ls xfsr-dump xfsr-dirfind xfsr-rawsearch
xfsr-ls:
	$(CC) $(CFLAGS) -DBUILDPROGLS xfsr-ls.c xfsr.c xfsr-dump.c -o $@
xfsr-dump:
	$(CC) $(CFLAGS) -DBUILDPROGDUMP xfsr-dump.c xfsr.c -o $@
xfsr-dirfind:
	$(CC) $(CFLAGS) -DBUILDPROGDIRFIND xfsr-dirfind.c xfsr.c -o $@
xfsr-rawsearch:
	$(CC) $(CFLAGS) xfsr-rawsearch.c -o $@
clean:
	rm -f xfsr-ls xfsr-dump xfsr-dirfind xfsr-rawsearch

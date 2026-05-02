all: ttft

CC = gcc
CFLAGS = -O -ggdb3 -Wall -DHAVE_DUP2 -DHAVE_UNISTD_H -DHAVE_STDINT_H -DHAVE_STDLIB_H -DHAVE_STDBOOL_H -DHAVE_TERMIOS_H

OBJS = main.o two_player.o

ttft: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o ttft

main.o: main.c tetrominos.h
two_player.o: two_player.c two_player.h

tetrominos.h: generate-tetrominos.py
	python generate-tetrominos.py > tetrominos.h

clean:
	rm -f $(OBJS) ttft tetrominos.h

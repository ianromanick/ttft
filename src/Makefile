ttft: asdf.o two_player.o
	cc asdf.o two_player.o -o ttft

pre: asdf.o two_player.o
	cc asdf.o two_player.o -o pre

asdf.o: asdf.c tetrominos.h two_player.h
	cc -O -c asdf.c

two_player.o: two_player.c two_player.h
	cc -O -c two_player.c

clean:
	rm -f two_player.o asdf.o asdf_f.o pre

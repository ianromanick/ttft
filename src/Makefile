all: ttft

ttft: main.c tetrominos.h
	gcc -O -ggdb3 -Wall main.c -o ttft

tetrominos.h: generate-tetrominos.py
	python generate-tetrominos.py > tetrominos.h

clean:
	rm -f ttft tetrominos.h

pm0 :
	gcc -Wall -pedantic -std=c99 -O2 -DWITH_LIBCONFIG -D_GNU_SOURCE pm0.c -o pm0

all: pm0

clean:


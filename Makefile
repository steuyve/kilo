kilo: kilo.c
	$(CC) -g kilo.c -o kilo -Wall -Wextra -pedantic -std=c99

install:
	cp kilo ~/dev/bin/.

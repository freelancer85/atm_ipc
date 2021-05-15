CC=gcc
CFLAGS=-Wall -Wextra -g

all: atm main

atm: atm.c atm.h
	$(CC) $(CFLAGS) -o atm atm.c

main: main.c atm.h
	$(CC) $(CFLAGS) -o main main.c

clean:
	rm -rf atm main

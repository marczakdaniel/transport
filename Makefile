CC=g++
CFLAGS=-std=c++17 -Wall -Wextra

all: main.o transport.o 
	$(CC) $(CFLAGS) -o transport main.o transport.o

main: main.cpp transport.h
	$(CC) $(CFLAGS) -c main.cpp -o main.o

transport: transport.cpp transport.h
	$(CC) $(CFLAGS) -c transport.cpp -o transport.o

clean:
	rm -vf *.o

distclean:
	rm -vf *.o
	rm -vf transport
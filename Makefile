CC = gcc
CFLAGS = -Wall -Wextra -pthread -g -O2

all: main pasazer kapitan_promu kapitan_portu

main: main.c common.h
	$(CC) $(CFLAGS) -o main main.c

pasazer: pasazer.c common.h
	$(CC) $(CFLAGS) -o pasazer pasazer.c

kapitan_promu: kapitan_promu.c common.h
	$(CC) $(CFLAGS) -o kapitan_promu kapitan_promu.c

kapitan_portu: kapitan_portu.c common.h
	$(CC) $(CFLAGS) -o kapitan_portu kapitan_portu.c

clean:
	rm -f main pasazer kapitan_promu kapitan_portu symulacja_prom.txt
	ipcrm -a 2>/dev/null || true

run: all
	./main 30
CC = gcc
CFLAGS = -Wall -D_GNU_SOURCE
LDFLAGS = -lrt

TARGETS = main pasazer kapitan_promu kapitan_portu

all: $(TARGETS)

main: main.c common.h
	$(CC) $(CFLAGS) -o main main.c $(LDFLAGS)




pasazer: pasazer.c common.h
	$(CC) $(CFLAGS) -o pasazer pasazer.c $(LDFLAGS)




kapitan_promu: kapitan_promu.c common.h
	$(CC) $(CFLAGS) -o kapitan\_promu kapitan\_promu.c $(LDFLAGS)




kapitan_portu: kapitan_portu.c common.h
	$(CC) $(CFLAGS) -o kapitan\_portu kapitan\_portu.c $(LDFLAGS)




clean:
	rm -f $(TARGETS)**

		ipcrm --all=shm 2>/dev/null || true

		ipcrm --all=sem 2>/dev/null || true

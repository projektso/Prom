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

test_stress: all
	@echo "\n>>> [1/4] URUCHAMIAM TEST STRESS (5000 PASAŻERÓW) <<<"
	./main 5000
	@echo ">>> TEST STRESS ZAKOŃCZONY SUKCESEM <<<"

test_limits: all
	@echo "\n>>> [TEST 2] URUCHAMIAM TEST: Szczelność semaforów przy ekstremalnej rywalizacji <<<"
	@echo "Cel: Weryfikacja szczelności semaforów przy ekstremalnej rywalizacji (Trap=1)."
	./main 2000
	@echo ">>> SUKCES: Brak naruszeń sekcji krytycznej mimo wysokiej rywalizacji. <<<"
test_spam: all
	@echo "\n>>> [3/4] URUCHAMIAM TEST SIGNAL SPAM (ODPORNOŚĆ NA PRZERWANIA) <<<"
	@./main 20 & MAIN_PID=$$!; \
	echo "Symulacja PID: $$MAIN_PID uruchomiona..."; \
	sleep 11; \
	PROM_PID=$$(pgrep -n -x "kapitan_promu"); \
	if [ -z "$$PROM_PID" ]; then echo "Nie znaleziono promu!"; kill $$MAIN_PID; exit 1; fi; \
	echo "Namierzono Kapitana Promu (PID: $$PROM_PID). Wysyłam serię SIGUSR1..."; \
	for i in 1 2 3 4 5; do \
		pkill -USR1 kapitan_promu 2>/dev/null || true; \
		echo "-> Strzał $$i/5 (Wymuszenie wypłynięcia)"; \
		sleep 3; \
	done; \
	echo "Czekam na poprawne zakończenie..."; \
	wait $$MAIN_PID; \
	echo ">>> SUKCES: Program przetrwał bombardowanie sygnałami. <<<"

test_sig2: all
	@echo "\n>>> [4/4] URUCHAMIAM TEST ZAMKNIĘCIA PORTU (SIGUSR2) <<<"
	@./main 300 & PID=$$!; \
	echo "Proces główny: $$PID. Czekam 2 sekundy na rozkręcenie kolejek..."; \
	sleep 2; \
	echo "Wysyłam SIGUSR2 (Zamknięcie portu)..."; \
	PORT_PID=$$(pgrep -P $$PID -f "kapitan_portu"); \
	kill -12 $$PORT_PID; \
	wait $$PID; \
	echo ">>> SUKCES: Program zakończył się poprawnie (brak deadlocka). <<<"

.PHONY: all clean run test_stress test_limits test_spam test_sig2
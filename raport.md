# Raport Projektowy: Symulacja Portu Promowego (Temat 19)

**Autor:** Tomasz Jachowicz
**Przedmiot:** Systemy Operacyjne
**Data:** 02.02.2026r.


## 1. Założenia projektowe
Projekt realizuje symulację działania portu promowego w oparciu o architekturę wieloprocesową w systemie Linux. Głównym założeniem jest brak centralnego zarządcy sterującego każdym krokiem – procesy (pasazer, kapitan_promu, kapitan_portu) działają asynchronicznie i podejmują decyzje na podstawie stanu współdzielonego oraz synchronizują się za pomocą Semaforów Systemu V. Kluczowe parametry symulacji (pojemność promu P, pojemność trapu K, limity wagowe Mp) są konfigurowalne w pliku nagłówkowym common.h.


## 2. Przypadki użycia
Poniżej przedstawiono główne scenariusze zrealizowane w symulacji:

**A. Standardowa ścieżka pasażera**
1.Pasażer przychodzi do portu i przechodzi przez bramkę wejściową.
2.Udaje się do odprawy biletowo-bagażowej (losowanie wagi bagażu).
3.Przechodzi kontrolę bezpieczeństwa (z uwzględnieniem płci i limitu osób na stanowisku).
4.Czeka w poczekalni na podstawienie promu.
5.Wchodzi na trap (zgodnie z limitem K), a następnie na pokład promu.
6.Po odbyciu rejsu proces pasażera kończy działanie.

**B. Przekroczenie wagi bagażu (Odprawa)**
1.Pasażer podchodzi do odprawy z bagażem cięższym niż Mp.
2.Zostaje wycofany z kolejki ("przepakowanie" – symulacja opóźnienia).
3.Pasażer wraca na koniec kolejki do odprawy i próbuje ponownie.

**C. Priorytety i obsługa różnych limitów wagowych promów**
1.Pasażer wchodzi na trap i okazuje się, że jego bagaż przekracza limit Mdi aktualnie podstawionego promu (ale był lżejszy niż Mp).
2.Pasażer schodzi z trapu, zwalniając miejsce.
3.Trafia do specjalnej kolejki priorytetowej HEAVY, czekając na kolejny prom o innej specyfikacji.
4.Przy następnym promie wchodzi na trap przed pasażerami z kolejki normalnej.

**D. Wcześniejsze wypłynięcie (Sygnał SIGUSR1)**
1.Kapitan Portu wysyła sygnał SIGUSR1 do Kapitana Promu.
2.Kapitan Promu, mimo że nie upłynął czas T1 ani prom nie jest pełny, zamyka wejście.
3.Pasażerowie znajdujący się na trapie zostają cofnięci do kolejki priorytetowej RETURN.
4.Prom odpływa natychmiast.


**E. Zamknięcie Portu (Sygnał SIGUSR2)**
1.Kapitan Portu otrzymuje polecenie zamknięcia.
2.Ustawiana jest blokada wejścia dla nowych pasażerów.
3.Pasażerowie oczekujący w kolejkach są budzeni i kończą działanie.
4.System oczekuje na zakończenie wszystkich aktywnych procesów i sprząta zasoby.


## 3. Ogólny opis kodu
Program został podzielony na moduły zgodnie z zasadą separacji odpowiedzialności:
* common.h: Plik nagłówkowy zawierający definicje struktur IPC (pamięć dzielona), klucze, stałe konfiguracyjne oraz makra pomocnicze (np. s_op z obsługą błędów).
* main.c: Punkt wejścia. Odpowiada za walidację argumentów, inicjalizację zasobów IPC (semafory, pamięć), uruchomienie procesów potomnych (fork/exec) oraz sprzątanie końcowe (atexit).
* pasazer.c: Implementuje logikę klienta. Przechodzi przez kolejne etapy (bramka -> odprawa -> kontrola -> trap -> prom). Zawiera logikę obsługi "frustracji" i kolejek priorytetowych.
* kapitan_promu.c: Zarządza cyklem życia promu. Podstawia prom, czeka czas T1, obsługuje sygnały wypłynięcia, tworzy procesy potomne dla rejsów i dba o sprzątanie procesów zombie.
* kapitan_portu.c: Symuluje zarządce portu. Może wysyłać sygnały sterujące i monitoruje stan portu.

**3a. Pseudokody kluczowych algorytmów:** 
* Algorytm Pasażera (Cykl życia):
```
1. START (Inicjalizacja)
2. Wejście na BRAMKĘ DO PORTU
3. ODPRAWA BAGAŻOWA:
   DOPÓKI (waga > Mp):
      Czekaj na losowe opóźnienie (przepakowanie) i wróć na koniec kolejki
4. KONTROLA BEZPIECZEŃSTWA:
   Szukaj bramki (wolna LUB ta sama płeć i max 1 osoba przy bramce). JEŚLI brak -> Czekaj w kolejce.
5. POCZEKALNIA:
   JEŚLI (VIP) -> Idź do kolejki VIP; INACZEJ -> Idź do kolejki NORMALNEJ.
6. WEJŚCIE NA TRAP (Pętla):
   DOPÓKI (nie jestem na promie):
      Czekaj na wezwanie (zgodnie z priorytetem). Zajmij miejsce na trapie.
      JEŚLI (Prom pełny LUB Prom odpływa LUB Bagaż > Mdi):
         Zejdź z trapu -> Idź do kolejki PRIORYTETOWEJ (RETURN/HEAVY).
      W PRZECIWNYM RAZIE:
         Wejdź na pokład. Zwolnij miejsce na trapie. PRZERWIJ PĘTLĘ.
7. KONIEC
```

* Algorytm Kapitana Promu:

```
1. PĘTLA GŁÓWNA:
   Sprzątaj zakończone procesy (Zombie). Pobierz parametry nowego promu.
2. PODSTAWIENIE PROMU:
   Zresetuj semafory. Budź pasażerów w kolejności: RETURN -> HEAVY -> VIP -> NORMAL.
3. ZAŁADUNEK:
   Czekaj na Czas T1 LUB Sygnał SIGUSR1.
4. ZAMKNIĘCIE:
   Zablokuj wejście. Czekaj aż trap będzie pusty.
5. REJS:
   Fork() -> Proces Potomny symuluje rejs i kończy działanie.
6. WRÓĆ DO PĘTLI
```

**3b. Opis kluczowych procedur**
* s_op(int semid, int n, int op) (common.h): Wrapper na funkcję semop(). Obsługuje błąd EINTR (sygnały) i automatycznie raportuje błędy krytyczne (perror), zapewniając stabilność operacji na semaforach.
* handle_sigusr2(int sig) (kapitan_portu.c): Obsługuje sygnał zamknięcia portu. Kluczowy element: po ustawieniu blokady uruchamia procedurę sztucznego podnoszenia semaforów, aby obudzić pasażerów i zapobiec zakleszczeniu.
* main() (pasazer.c): Implementuje maszynę stanów pasażera, w tym logikę wyboru kolejek i obsługę cofania z trapu.
* sprzatanie() (main.c): Funkcja zarejestrowana w atexit. Gwarantuje usunięcie zasobów IPC i zabicie procesów potomnych niezależnie od sposobu zakończenia programu.

## 4. Realizacja i dodane elementy
**Co udało się zrealizować:**
* Pełna synchronizacja: Brak zakleszczeń i wyścigów.
* Obsługa Zombie: Implementacja mechanizmu waitpid z flagą WNOHANG.
* Złożony system kolejek: Zaimplementowano 4 poziomy priorytetów wejścia na trap: RETURN > HEAVY > VIP > NORM.
* Kolorowanie wyjścia: Logi w terminalu są kolorowane w zależności od aktora (Kapitan, Pasażer, Błąd).
* Walidacja: Program weryfikuje poprawność danych wejściowych oraz stosuje górny limit procesów, aby zapobiec awarii systemu.

**Napotkane problemy i zastosowane rozwiązania:**
* Problem Zombie: Początkowo procesy rejsów pozostawały w stanie <defunct>. Rozwiązanie: Implementacja mechanizmu waitpid z flagą WNOHANG w pętli obsługi promu.
* Problem Deadlocka: Pasażerowie oczekujący w kolejkach blokowali zamknięcie portu. Rozwiązanie: Implementacja mechanizmu budzenia procesów przy sygnale SIGUSR2 w kapitan_portu.c.
* Problem Kolejności: Trudność w zapewnieniu logicznej kolejności wchodzenia na trap, szczególnie w przypadku pasażerów cofniętych z trapu. Rozwiązanie: Implementacja systemu 4 kolejek priorytetowych obsługiwanych przez semafory w ścisłej kolejności: RETURN > HEAVY > VIP > NORM.


## 5. Raport z testów
* Test 1:
* Test 2:
* Test 3:
* Test 4:


## 6. Wykorzystane konstrukcje
**Tworzenie i obsługa procesów:** 
* Użycie funkcji fork() i exec() do tworzenia architektury wieloprocesowej.
* Użycie waitpid() z WNOHANG do asynchronicznego sprzątania procesów.

**Obsługa sygnałów:**
* Obsługa co najmniej dwóch różnych sygnałów: SIGUSR1 (wypłynięcie) i SIGUSR2 (zamknięcie).

**Mechanizmy synchronizacji:**
* Zastosowanie Semaforów Systemu V do synchronizacji dostępu (mutexy, kolejki, blokady).
* Programowanie współbieżne dla procesów asynchronicznych.

**Wyjątki i obsługa błędów:**
* Zdefiniowanie własnej funkcji s_op (wrapper), która służy do zgłaszania i obsługi błędów funkcji systemowych.
* Walidacja danych wprowadzanych przez użytkownika zaimplementowana w dedykowanej funkcji validate_process_count.


## 7. Wymagania z 4.1
* 4.1b - Walidacja: Dane wejściowe są sprawdzane (validate_process_count w common.h).
* 4.1c - Obsługa błędów: Funkcje systemowe używają perror (wrappery s_op w common.h).
* 4.1d - Prawa dostępu: Zasoby IPC tworzone z flagą 0600.
* 4.1e - Usuwanie zasobów: Funkcja cleanup_ipc usuwa shm i sem.
* 4.1g - Rozwiązanie niescentralizowane: Użycie fork() i execl().

## 8. Linki do kodu (GitHub)
Poniżej znajdują się odnośniki do fragmentów kodu realizujących wymagane konstrukcje systemowe.

**Adres repozytorium:** https://github.com/projektso/Prom

### a. Tworzenie i obsługa plików
* Użycie `open()`, `write()`, `flock()` do bezpiecznego logowania do pliku.
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/common.h#L150-L173

### b. Tworzenie procesów
* Użycie `fork()`, `execl()` do tworzenia pasażerów i kapitanów.
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/main.c#L155-L167

* Użycie `waitpid()` z `WNOHANG` do sprzątania zombie.
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/kapitan_promu.c#L71-L81

### c. Obsługa sygnałów
* Rejestracja handlerów `sigaction` dla `SIGUSR1`, `SIGUSR2`.
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/kapitan_portu.c#L20-L32

### d. Synchronizacja procesów (Semafory)
* Operacje `semop` (blokujące, nieblokujące, z timeoutem).
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/common.h#L175-L229
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/pasazer.c#L259-L525

### e. Segmenty pamięci dzielonej
* Inicjalizacja `shmget`, dołączenie `shmat`, struktura `SharedData`.
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/main.c#L94-L110

### f. Walidacja i obsługa błędów
* Sprawdzanie limitu procesów i obsługa `perror`.
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/common.h#L231-L238
* https://github.com/projektso/Prom/blob/5ac78acd76c89044f15b035d8faf6c57ebe7e767/main.c#L90-L110
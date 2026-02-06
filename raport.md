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
   Sprzątaj zakończone procesy (Zombie). 
   JEŚLI (liczba statków na morzu >= N_FLOTA):
      Czekaj blokująco na powrót dowolnego statku.
   Pobierz parametry nowego promu.
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
* main.c: Punkt wejścia. Odpowiada za inicjalizację zasobów IPC. Zawiera pętlę tworzenia procesów, która dynamicznie zarządza tworzeniem pasażerów – wykorzystuje semafor z timeoutem (s_op_timed) oraz bieżące sprzątanie procesów zombie (waitpid z WNOHANG), aby zapobiec przeciążeniu tablicy procesów systemu operacyjnego przy dużej liczbie pasażerów (10 000+).

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
**Test 1 - Weryfikacja Trwałości Sygnałów przy Desynchronizacji Stanu Logicznego i Fizycznego:**
* Cel: Analiza zachowania mechanizmów IPC w warunkach utraty atomowości między zgłoszeniem gotowości a faktycznym oczekiwaniem na zasób.
* Scenariusz: W kodzie procesu pasażera zaimplementowano sztuczne opóźnienie (sleep(80s)) w sekcji krytycznej, bezpośrednio po inkrementacji licznika oczekujących w pamięci dzielonej, a przed wywołaniem blokującej funkcji semop() na semaforze kolejki. W rezultacie Kapitan Promu otrzymuje logiczną informację o gotowości pasażerów i otwiera semafory (podnosi ich wartość), podczas gdy procesy pasażerów są wciąż uśpione i nie oczekują na sygnał.
* Weryfikowane aspekty:
1. Pamięć semaforów: Czy semafory Systemu V poprawnie akumulują sygnały otwarcia wysłane przez Kapitana, mimo braku procesów oczekujących w momencie wysłania.
2. Odporność na Hazard (Race Condition): Czy po ustąpieniu opóźnienia pasażerowie natychmiastowo konsumują "zmagazynowane" sygnały i wchodzą na trap, czy też dochodzi do utraty sygnałów i zakleszczenia systemu.
3. Spójność liczników: Czy desynchronizacja czasowa nie powoduje błędów w logice zliczania pasażerów wchodzących na pokład.
* Oczekiwany rezultat: System wykazuje odporność na desynchronizację. Wartości semaforów rosną dodatnio w czasie uśpienia pasażerów. Po zakończeniu funkcji sleep, procesy pasażerów natychmiastowo i bezkolizyjnie zmniejszają wartości semaforów (przechodzą przez trap), nie doprowadzając do zjawiska zagłodzenia. Logi mogą wykazywać chwilową niespójność między stanem 'Czekający' a 'Pozostało' w oknie czasowym wybudzania, co jest naturalną cechą systemów asynchronicznych i nie wpływa na poprawność bilansu końcowego.
* Wynik: POZYTYWNY.

**Test 2 - Test Szczelności Semaforów i Limitów Pojemności:**
* Cel: Praktyczne sprawdzenie, czy program bezwzględnie przestrzega ustawionych limitów (np. pojemności trapu), nawet gdy setki procesów próbują wejść jednocześnie.
* Scenariusz: Uruchomienie symulacji z dużą liczbą pasażerów (2000), co powoduje ogromny tłok przed wejściem na trap. System musi obsłużyć ten tłum, wpuszczając pasażerów małymi grupami, zgodnie z limitem K_TRAP(np. 1).
*Weryfikowane aspekty:
1. Przestrzeganie Limitów: Czy w logach widać, że liczba osób na trapie/promie NIGDY nie przekracza zadeklarowanej wartości (np. 1).
2. Stabilność w Tłoku: Czy program płynnie kolejkuje nadmiarowe procesy, zamiast się zawiesić lub "zgubić" semafor.
* Oczekiwany rezultat: Mimo naporu 2000 procesów, na trapie w każdej chwili znajduje się maksymalnie tyle osób, ile wynosi limit. Nadmiarowi pasażerowie czekają w kolejce na swoją kolej.
* Wynik: POZYTYWNY.

**Test 3 - Weryfikacja Spójności IPC i Obsługi Sygnałów Asynchronicznych:**
* Cel: Weryfikacja obsługi asynchronicznych sygnałów (SIGUSR1) oraz sprawdzenie, czy przerwania systemowe (EINTR) nie powodują błędów w operacjach na semaforach.
* Scenariusz: Uruchomienie symulacji, a następnie wysłanie serii sygnałów SIGUSR1 (wymuszenie wypłynięcia) do procesu Kapitana Promu, podczas trwania sekcji krytycznej (załadunek pasażerów).
* Weryfikowane aspekty:
1. Czy funkcje semop poprawnie wznawiają działanie po przerwaniu sygnałem.
2. Czy Kapitan Promu poprawnie przerywa oczekiwanie na czas T1 i zamyka trap.
3. Czy pasażerowie znajdujący się na trapie w momencie sygnału są poprawnie cofani.
* Oczekiwany rezultat: Promy wypływają wcześniej niż wynika to z czasu T1. Program nie zawiesza się ani nie kończy błędem. Pasażerowie cofnięci z trapu trafiają do kolejki priorytetowej RETURN i wchodzą na kolejny prom.
* Wynik: POZYTYWNY.

**Test 4 - Analiza Zapobiegania Zakleszczeniom i Poprawności Zwalniania Zasobów:**
* Cel: Sprawdzenie, czy system potrafi bezpiecznie się zamknąć w sytuacji awaryjnej, nie doprowadzając do zakleszczenia (Deadlock) ani osierocenia procesów.
* Scenariusz: Symulacja zostaje uruchomiona, a kolejki (odprawa, kontrola, poczekalnia) wypełniają się pasażerami, którzy są zablokowani na semaforach (czekają na obsługę). Następnie do Kapitana Portu wysyłany jest sygnał SIGUSR2 (Zamknięcie Portu).
* Weryfikowane aspekty:
1. Czy Kapitan Portu po odebraniu sygnału ustawia flagę blokada_odprawy.
2. Czy proces zarządczy prewencyjnie podnosi semafory kolejek (SEM_ODPRAWA_QUEUE, SEM_SEC_QUEUE, SEM_BRAMKA), aby zapobiec nieskończonemu oczekiwaniu procesów potomnych.
3. Czy obudzone procesy poprawnie odczytują flagę blokady i kończą działanie (exit), zapobiegając sytuacji "Starvation" (zagłodzenia).
* Oczekiwany rezultat: Wszystkie procesy pasażerów kończą działanie. Zasoby IPC (pamięć dzielona, semafory) zostają poprawnie zwolnione. Program kończy się komunikatem o sukcesie, a w systemie nie pozostają żadne "wiszące" procesy.
* Wynik: POZYTYWNY.


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
* https://github.com/projektso/Prom/blob/87352d820465fb179cc6fd20ba4309591c4400cf/common.h#L156-L180

### b. Tworzenie procesów
* Użycie `fork()`, `execl()` do tworzenia pasażerów i kapitanów.
* https://github.com/projektso/Prom/blob/87352d820465fb179cc6fd20ba4309591c4400cf/main.c#L181-L193

* Użycie `waitpid()` z `WNOHANG` do sprzątania zombie.
* https://github.com/projektso/Prom/blob/87352d820465fb179cc6fd20ba4309591c4400cf/main.c#L208-L219

### c. Obsługa sygnałów
* Rejestracja handlerów `sigaction` dla `SIGUSR1`, `SIGUSR2`.
* https://github.com/projektso/Prom/blob/87352d820465fb179cc6fd20ba4309591c4400cf/kapitan_portu.c#L17-L30

### d. Synchronizacja procesów (Semafory)
* Operacje `semop` (blokujące, nieblokujące, z timeoutem).
* https://github.com/projektso/Prom/blob/87352d820465fb179cc6fd20ba4309591c4400cf/common.h#L182-L236
* https://github.com/projektso/Prom/blob/87352d820465fb179cc6fd20ba4309591c4400cf/pasazer.c#L459-L471

### e. Segmenty pamięci dzielonej
* Inicjalizacja `shmget`, dołączenie `shmat`, struktura `SharedData`.
* https://github.com/projektso/Prom/blob/87352d820465fb179cc6fd20ba4309591c4400cf/main.c#L113-L129

### f. Walidacja i obsługa błędów
* Sprawdzanie limitu procesów i obsługa `perror`.
* https://github.com/projektso/Prom/blob/87352d820465fb179cc6fd20ba4309591c4400cf/common.h#L216-L245

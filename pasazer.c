#include "common.h"

//ZMIENNE GLOBALNE DLA OBSŁUGI SYGNAŁÓW
volatile sig_atomic_t should_exit = 0;
volatile sig_atomic_t received_signal2 = 0;

//HANDLER SYGNAŁU SIGTERM/SIGINT
void handle_term(int sig) {
    (void)sig;
    should_exit = 1; 
}

//HANDLER SYGNAŁU SIGUSR2
void handle_sigusr2(int sig) {
    (void)sig;
    received_signal2 = 1;
    should_exit = 1;
}

//Kończenie podróży pasażera
void zakoncz_podroz(int semid, SharedData *sd, int id) {
    (void)id;
    
    //Zmniejszenie licznika pasażerów w systemie
    s_op(semid, SEM_SYSTEM_MUTEX, -1);
    sd->pasazerowie_w_systemie--;
    s_op(semid, SEM_SYSTEM_MUTEX, 1);
    
    //Zwolnienie slotu procesu
    s_op(semid, SEM_PROCESY, 1);
    
    //Odłączenie pamięci dzielonej i zakończenie
    if (sd) shmdt(sd);
    exit(0);
}

int main(int argc, char* argv[]) {
    //Walidacja argumentów
    if (argc < 2) exit(1);
    
    int id = atoi(argv[1]);
    if (id <= 0) exit(1);

    //REJESTRACJA HANDLERÓW SYGNAŁÓW
    struct sigaction sa, sa2;
    //Handler dla SIGTERM i SIGINT
    sa.sa_handler = handle_term;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    //Handler dla SIGUSR2
    sa2.sa_handler = handle_sigusr2;
    sa2.sa_flags = 0;
    sigemptyset(&sa2.sa_mask);
    sigaction(SIGUSR2, &sa2, NULL);
    
    //Inicjalizacja generatora liczb losowych
    unsigned int seed = time(NULL) ^ (getpid() << 16) ^ (id * 31);
    srand(seed);

    //POŁĄCZENIE Z ZASOBAMI IPC
    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) exit(1);
    
    int semid = semget(key, SEM_COUNT, 0600);
    if (semid == -1) exit(1);
    
    int shmid = shmget(key, sizeof(SharedData), 0600);
    if (shmid == -1) exit(1);
    
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) exit(1);

    //Losowanie atrybutów pasażera
    int waga = 15 + (rand() % 10);
    int plec = (rand() % 2) + 1;
    char c_plec = (plec == PLEC_M) ? 'M' : 'K';

    //ETAP 1: BRAMKA WEJŚCIOWA    
    s_op(semid, SEM_BRAMKA, -1); //Czekanie na otwarcie bramki
    
    //Sprawdzenie czy port nie jest zamknięty
    if (should_exit || sd->blokada_odprawy) {
        logger(C_R, "P%d: Port zamknięty- wracam do domu.", id);
        zakoncz_podroz(semid, sd, id);
    }

    //ETAP 2: ODPRAWA BILETOWO-BAGAŻOWA
    bool odprawiony = false;
    int proby = 1;

    while (!odprawiony && !should_exit) {
        //Sprawdzenie blokady odprawy
        if (sd->blokada_odprawy) zakoncz_podroz(semid, sd, id);

        //Wejście do kolejki oczekujących
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        sd->odprawa_czekajacy++;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        
        //Czekanie na swoją kolej i wolne stanowisko
        s_op(semid, SEM_ODPRAWA_QUEUE, -1);
        s_op(semid, SEM_ODPRAWA, -1);
        
        //Sprawdzenie blokady po zdobyciu stanowiska
        if (sd->blokada_odprawy) {
            s_op(semid, SEM_ODPRAWA, 1);
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            if (sd->odprawa_czekajacy > 0) {
                sd->odprawa_czekajacy--;
                s_op(semid, SEM_ODPRAWA_QUEUE, 1);
            }
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
            zakoncz_podroz(semid, sd, id);
        }
        
        SLEEP_ODPRAWA(); //Symulacja czasu odprawy
        
        //Sprawdzenie wagi bagażu
        if (waga > Mp_LIMIT_ODPRAWY) {
            //Za ciężki bagaż - przepakowanie i powrót na koniec kolejki
            waga -= (2 + rand() % 5); // Usunięcie 2-6 kg
            if (waga < 0) waga = 0;
            proby++;
            
            //Zwolnienie stanowiska i budzenie następnej osoby
            s_op(semid, SEM_ODPRAWA, 1);
            
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            if (sd->odprawa_czekajacy > 0) {
                sd->odprawa_czekajacy--;
                s_op(semid, SEM_ODPRAWA_QUEUE, 1);
            }
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
        } else {
            logger(C_G, "P%d [%c]: Odprawiony (próba %d, bagaż %d kg).", id, c_plec, proby, waga);
            odprawiony = true;

            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            sd->stat_odprawieni++;
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
            
            s_op(semid, SEM_ODPRAWA, 1);
            
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            if (sd->odprawa_czekajacy > 0) {
                sd->odprawa_czekajacy--;
                s_op(semid, SEM_ODPRAWA_QUEUE, 1);
            }
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
        }
    }
    
    if (should_exit) zakoncz_podroz(semid, sd, id);

    //ETAP 3: KONTROLA BEZPIECZEŃSTWA
    bool na_kontroli = false;
    int bramka_nr = -1;
    int przepuszczenia = 0;

    while (!na_kontroli && !should_exit) {
        s_op(semid, SEM_SEC_MUTEX, -1);
        
        //Szukanie pustej bramki
        for (int i = 0; i < LICZBA_STANOWISK_KONTROLI; i++) {
            if (sd->sec_liczba[i] == 0) {
                sd->sec_liczba[i] = 1;
                sd->sec_plec[i] = plec;
                bramka_nr = i;
                na_kontroli = true;
                break;
            }
        }
        
        //Szukanie bramki z 1 osobą tej samej płci
        if (!na_kontroli) {
            for (int i = 0; i < LICZBA_STANOWISK_KONTROLI; i++) {
                if (sd->sec_liczba[i] == 1 && sd->sec_plec[i] == plec) {
                    sd->sec_liczba[i] = 2;
                    bramka_nr = i;
                    na_kontroli = true;
                    break;
                }
            }
        }

        //Brak miejsca - przepuszczenie lub czekanie z frustracją
        if (!na_kontroli) {
            if (przepuszczenia < MAX_PRZEPUSZCZEN) {
                przepuszczenia++;
                logger(C_Y, "P%d [%c]: Przepuszczam kogoś (%d/%d).", 
                       id, c_plec, przepuszczenia, MAX_PRZEPUSZCZEN);
            } else {
                logger(C_R, "P%d [%c]: FRUSTRACJA! Muszę czekać.", id, c_plec);
            }
            sd->sec_czekajacy++;
            s_op(semid, SEM_SEC_MUTEX, 1);
            s_op(semid, SEM_SEC_QUEUE, -1); //Czekanie na zwolnienie
        } else {
            s_op(semid, SEM_SEC_MUTEX, 1);
        }
    }
    
    //Obsługa wyjścia podczas kontroli
    if (should_exit) {
        if (na_kontroli && bramka_nr >= 0) {
            s_op(semid, SEM_SEC_MUTEX, -1);
            sd->sec_liczba[bramka_nr]--;
            if (sd->sec_liczba[bramka_nr] == 0) sd->sec_plec[bramka_nr] = PLEC_BRAK;
            s_op(semid, SEM_SEC_MUTEX, 1);
        }
        zakoncz_podroz(semid, sd, id);
    }

    SLEEP_KONTROLA(); //Symulacja czasu kontroli

    //Opuszczenie bramki kontroli
    s_op(semid, SEM_SEC_MUTEX, -1);
    sd->sec_liczba[bramka_nr]--;
    if (sd->sec_liczba[bramka_nr] == 0) {
        sd->sec_plec[bramka_nr] = PLEC_BRAK;
    }
    
    //Budzenie następnego czekającego
    if (sd->sec_czekajacy > 0) {
        sd->sec_czekajacy--;
        s_op(semid, SEM_SEC_QUEUE, 1);
    }
    s_op(semid, SEM_SEC_MUTEX, 1);

    //ETAP 4: POCZEKALNIA
    bool is_vip = (rand() % 100) < 10; //10% szans na VIP
    
    logger(C_C, "P%d [%c]: Przeszedł kontrolę (Bramka %d, %s).", 
           id, c_plec, bramka_nr, is_vip ? "VIP" : "STD");

    int moja_kolejka; // 0=NORM, 1=VIP, 2=RETURN, 3=HEAVY
    
    if (is_vip) {
        moja_kolejka = 1;
        
        s_op(semid, SEM_TRAP_MUTEX, -1);
        logger(C_M, "P%d [VIP]: Idę do kolejki VIP.", id);
        sd->trap_wait_vip++;
        s_op(semid, SEM_TRAP_MUTEX, 1);
        //sleep(80);
        s_op(semid, SEM_TRAP_Q_VIP, -1); //Czekanie w kolejce VIP
    } else {
        s_op(semid, SEM_POCZEKALNIA, -1); //Wejście do poczekalni
        moja_kolejka = 0;
        
        s_op(semid, SEM_TRAP_MUTEX, -1);
        sd->trap_wait_norm++;
        s_op(semid, SEM_TRAP_MUTEX, 1);
        //sleep(80);
        s_op(semid, SEM_TRAP_Q_NORM, -1); //Czekanie w kolejce zwykłej
    }

    //ETAP 5: WEJŚCIE NA TRAP I PROM
    bool w_srodku = false;
    bool na_trapie = false;
    bool czekam_na_trapie = false;
    int ostatni_prom_za_ciezki = -1;
    
    while (!w_srodku && !should_exit) {
        s_op(semid, SEM_TRAP_MUTEX, -1);
        
        //Sprawdzenie czy załadunek aktywny
        if (!sd->zaladunek_aktywny || !sd->prom_w_porcie) {
            if (na_trapie) {
                //Zejście z trapu
                logger(C_Y, "P%d: Prom odpływa! Schodzę z trapu.", id);
                
                //Powrót do kolejki return
                moja_kolejka = 2;
                sd->trap_wait_return++;
                
                //Zwolnienie miejsca na trapie
                sd->trap_count--;
                na_trapie = false;
                czekam_na_trapie = false;
                s_op(semid, SEM_TRAP_ENTER, 1);
                
                //Sygnalizacja pustego trapu, jeśli to ostatnia osoba
                if (sd->trap_count == 0) {
                    s_op_nowait(semid, SEM_TRAP_EMPTY, 1);
                }
                
                s_op(semid, SEM_TRAP_MUTEX, 1);
                s_op(semid, SEM_TRAP_Q_RETURN, -1);
            } else {
                //Powrót do swojej kolejki
                if (moja_kolejka == 2) {
                    sd->trap_wait_return++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_RETURN, -1);
                } else if (moja_kolejka == 3) {
                    sd->trap_wait_heavy++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_HEAVY, -1);
                } else if (moja_kolejka == 1) {
                    sd->trap_wait_vip++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_VIP, -1);
                } else {
                    sd->trap_wait_norm++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_NORM, -1);
                }
            }
            ostatni_prom_za_ciezki = -1; //Reset - nowy prom
            continue;
        }
        
        int aktualny_limit = sd->limit_bagazu_aktualny;
        int numer_promu = sd->prom_numer;
        
        //Sprawdzenie czy była już próba dostania się na ten prom
        if (ostatni_prom_za_ciezki == numer_promu) {
            if (sd->trap_wait_return > 0) {
                s_op(semid, SEM_TRAP_Q_RETURN, 1);
                sd->trap_wait_return--;
            } else if (sd->trap_wait_vip > 0) {
                s_op(semid, SEM_TRAP_Q_VIP, 1);
                sd->trap_wait_vip--;
            } else if (sd->trap_wait_norm > 0) {
                s_op(semid, SEM_TRAP_Q_NORM, 1);
                sd->trap_wait_norm--;
            } else if (sd->trap_wait_heavy > 0) {
                s_op(semid, SEM_TRAP_Q_HEAVY, 1);
                sd->trap_wait_heavy--;
            }
            
            //Powrót do kolejki heavy
            sd->trap_wait_heavy++;
            s_op(semid, SEM_TRAP_MUTEX, 1);
            s_op(semid, SEM_TRAP_Q_HEAVY, -1);
            continue;
        }
        
        //Sprawdzenie limitu bagażu
        if (waga > aktualny_limit) {
            if (na_trapie) {
                //Bagaż za ciężki - zejście z trapu
                logger(C_Y, "P%d: Bagaż %d kg > limit %d kg. Schodzę z trapu.", 
                       id, waga, aktualny_limit);
                sd->trap_count--;
                na_trapie = false;
                czekam_na_trapie = false;
                s_op(semid, SEM_TRAP_ENTER, 1);
                
                if (sd->trap_count == 0) {
                    s_op_nowait(semid, SEM_TRAP_EMPTY, 1);
                }
                
                //Budzenie następnego pasażera
                if (sd->trap_wait_return > 0) {
                    s_op(semid, SEM_TRAP_Q_RETURN, 1);
                    sd->trap_wait_return--;
                } else if (sd->trap_wait_heavy > 0) {
                    s_op(semid, SEM_TRAP_Q_HEAVY, 1);
                    sd->trap_wait_heavy--;
                } else if (sd->trap_wait_vip > 0) {
                    s_op(semid, SEM_TRAP_Q_VIP, 1);
                    sd->trap_wait_vip--;
                } else if (sd->trap_wait_norm > 0) {
                    s_op(semid, SEM_TRAP_Q_NORM, 1);
                    sd->trap_wait_norm--;
                }
                
                //Przejście do kolejki return
                moja_kolejka = 2;
                sd->trap_wait_return++;
                s_op(semid, SEM_TRAP_MUTEX, 1);
                s_op(semid, SEM_TRAP_Q_RETURN, -1);
            } else {
                //Bagaż za ciężki - czekanie na inny prom
                logger(C_Y, "P%d: Bagaż %d kg > limit %d kg. Czekam na inny prom.", 
                       id, waga, aktualny_limit);
                
                //Zapamiętywanie numeru promu
                ostatni_prom_za_ciezki = numer_promu;
                
                //Budzenie następnego pasażera
                if (sd->trap_wait_return > 0) {
                    s_op(semid, SEM_TRAP_Q_RETURN, 1);
                    sd->trap_wait_return--;
                } else if (sd->trap_wait_heavy > 0) {
                    s_op(semid, SEM_TRAP_Q_HEAVY, 1);
                    sd->trap_wait_heavy--;
                } else if (sd->trap_wait_vip > 0) {
                    s_op(semid, SEM_TRAP_Q_VIP, 1);
                    sd->trap_wait_vip--;
                } else if (sd->trap_wait_norm > 0) {
                    s_op(semid, SEM_TRAP_Q_NORM, 1);
                    sd->trap_wait_norm--;
                } else {
                }
                
                //Przejście do kolejki heavy
                moja_kolejka = 3;
                sd->trap_wait_heavy++;
                s_op(semid, SEM_TRAP_MUTEX, 1);
                s_op(semid, SEM_TRAP_Q_HEAVY, -1);
            }
            continue;
        }

        //Wejście na trap
        if (!na_trapie) {
            s_op(semid, SEM_TRAP_MUTEX, 1);
            
            //Czekanie na miejsce na trapie
            s_op(semid, SEM_TRAP_ENTER, -1);
            
            s_op(semid, SEM_TRAP_MUTEX, -1);
            
            //Ponowne sprawdzenie czy załadunek aktywny
            if (!sd->zaladunek_aktywny || !sd->prom_w_porcie) {
                s_op(semid, SEM_TRAP_ENTER, 1);
                
                if (moja_kolejka == 2) {
                    sd->trap_wait_return++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_RETURN, -1);
                } else if (moja_kolejka == 3) {
                    sd->trap_wait_heavy++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_HEAVY, -1);
                } else if (moja_kolejka == 1) {
                    sd->trap_wait_vip++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_VIP, -1);
                } else {
                    sd->trap_wait_norm++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_NORM, -1);
                }
                continue;
            }
             
            //Wchodzenie na trap
            sd->trap_count++;
            na_trapie = true;
            czekam_na_trapie = false;
            
            logger(C_C, "P%d [%s]: Wchodzę na trap.", 
                   id, is_vip ? "VIP" : "STD");
            
            s_op(semid, SEM_TRAP_MUTEX, 1);
            
            SLEEP_TRAP_WALK(); //Symulacja przejścia po trapie
            
            s_op(semid, SEM_TRAP_MUTEX, -1);
        }
        
        //Próba wejścia do promu
        if (s_op_nowait(semid, SEM_FERRY_CAPACITY, -1) == -1) {
            //Prom pełny - czekanie na trapie
            if (!czekam_na_trapie) {
                logger(C_Y, "P%d: Prom pełny! Czekam na trapie.", id);
                czekam_na_trapie = true;
            }
            s_op(semid, SEM_TRAP_MUTEX, 1);
            
            //Czekanie z timeoutem
            s_op_timed(semid, SEM_TIMER_SIGNAL, -1, 1);
            continue;
        }
        
        //Sprawdzanie czy prom nie odpłynął podczas wchodzenia
        if (!sd->zaladunek_aktywny || sd->prom_numer != numer_promu) {
            s_op(semid, SEM_FERRY_CAPACITY, 1);
            
            logger(C_Y, "P%d: Prom %d odpływa! Schodzę z trapu.", id, numer_promu);
            sd->trap_count--;
            na_trapie = false;
            czekam_na_trapie = false;
            s_op(semid, SEM_TRAP_ENTER, 1);
            
            if (sd->trap_count == 0) {
                s_op_nowait(semid, SEM_TRAP_EMPTY, 1);
            }
            
            moja_kolejka = 2;
            sd->trap_wait_return++;
            s_op(semid, SEM_TRAP_MUTEX, 1);
            s_op(semid, SEM_TRAP_Q_RETURN, -1);
            continue;
        }
        
        //WEJŚCIE NA PROM
        SLEEP_BOARDING(); //Symulacja wchodzenia
        
        logger(C_G, "P%d [%s]: Wsiadłem do promu nr %d.", 
               id, is_vip ? "VIP" : "STD", numer_promu);
        
        //Opuszczenie trapu
        sd->trap_count--;
        na_trapie = false;
        czekam_na_trapie = false;
        s_op(semid, SEM_TRAP_ENTER, 1);
        
        if (sd->trap_count == 0) {
            s_op_nowait(semid, SEM_TRAP_EMPTY, 1);
        }
        
        //Budzenie następnej osoby
        if (sd->trap_wait_return > 0) {
            s_op(semid, SEM_TRAP_Q_RETURN, 1);
            sd->trap_wait_return--;
        } else if (sd->trap_wait_heavy > 0) {
            s_op(semid, SEM_TRAP_Q_HEAVY, 1);
            sd->trap_wait_heavy--;
        } else if (sd->trap_wait_vip > 0) {
            s_op(semid, SEM_TRAP_Q_VIP, 1);
            sd->trap_wait_vip--;
        } else if (sd->trap_wait_norm > 0) {
            s_op(semid, SEM_TRAP_Q_NORM, 1);
            sd->trap_wait_norm--;
        }

        s_op(semid, SEM_TRAP_MUTEX, 1);
        w_srodku = true;
    }

    zakoncz_podroz(semid, sd, id);
    return 0;
}
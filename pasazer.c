#include "common.h"

volatile sig_atomic_t should_exit = 0;

void handle_term(int sig) {
    (void)sig;
    should_exit = 1;
}

void zakoncz_podroz(int semid, SharedData *sd, int id) {
    (void)id;
    
    s_op(semid, SEM_SYSTEM_MUTEX, -1);
    sd->pasazerowie_w_systemie--;
    s_op(semid, SEM_SYSTEM_MUTEX, 1);
    
    s_op(semid, SEM_PROCESY, 1);
    
    if (sd) shmdt(sd);
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) exit(1);
    
    int id = atoi(argv[1]);
    if (id <= 0) exit(1);
    
    struct sigaction sa;
    sa.sa_handler = handle_term;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    unsigned int seed = time(NULL) ^ (getpid() << 16) ^ (id * 31);
    srand(seed);

    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) exit(1);
    
    int semid = semget(key, SEM_COUNT, 0600);
    if (semid == -1) exit(1);
    
    int shmid = shmget(key, sizeof(SharedData), 0600);
    if (shmid == -1) exit(1);
    
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) exit(1);

    int waga = 15 + (rand() % 20);
    int plec = (rand() % 2) + 1;
    char c_plec = (plec == PLEC_M) ? 'M' : 'K';

    s_op(semid, SEM_BRAMKA, -1);
    
    if (should_exit || sd->blokada_odprawy) {
        if (sd->blokada_odprawy) {
            logger(C_R, "P%d [%c]: Port zamknięty! Wracam.", id, c_plec);
        }
        zakoncz_podroz(semid, sd, id);
    }

    bool odprawiony = false;
    int proby = 1;

    while (!odprawiony && !should_exit) {
        if (sd->blokada_odprawy) zakoncz_podroz(semid, sd, id);

        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        sd->odprawa_czekajacy++;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        
        s_op(semid, SEM_ODPRAWA_QUEUE, -1);
        s_op(semid, SEM_ODPRAWA, -1);
        
        SLEEP_ODPRAWA();
        
        if (waga > Mp_LIMIT_ODPRAWY) {
            waga -= (2 + rand() % 5);
            if (waga < 0) waga = 0;
            proby++;
            
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

    bool na_kontroli = false;
    int bramka_nr = -1;
    int przepuszczenia = 0;

    while (!na_kontroli && !should_exit) {
        s_op(semid, SEM_SEC_MUTEX, -1);
        
        for (int i = 0; i < LICZBA_STANOWISK_KONTROLI; i++) {
            if (sd->sec_liczba[i] == 0) {
                sd->sec_liczba[i] = 1;
                sd->sec_plec[i] = plec;
                bramka_nr = i;
                na_kontroli = true;
                break;
            }
        }
        
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
            s_op(semid, SEM_SEC_QUEUE, -1);
        } else {
            s_op(semid, SEM_SEC_MUTEX, 1);
        }
    }
    
    if (should_exit) {
        if (na_kontroli && bramka_nr >= 0) {
            s_op(semid, SEM_SEC_MUTEX, -1);
            sd->sec_liczba[bramka_nr]--;
            if (sd->sec_liczba[bramka_nr] == 0) sd->sec_plec[bramka_nr] = PLEC_BRAK;
            s_op(semid, SEM_SEC_MUTEX, 1);
        }
        zakoncz_podroz(semid, sd, id);
    }

    SLEEP_KONTROLA();

    s_op(semid, SEM_SEC_MUTEX, -1);
    sd->sec_liczba[bramka_nr]--;
    if (sd->sec_liczba[bramka_nr] == 0) {
        sd->sec_plec[bramka_nr] = PLEC_BRAK;
    }
    
    if (sd->sec_czekajacy > 0) {
        sd->sec_czekajacy--;
        s_op(semid, SEM_SEC_QUEUE, 1);
    }
    s_op(semid, SEM_SEC_MUTEX, 1);

    bool is_vip = (rand() % 100) < 10;
    
    logger(C_C, "P%d [%c]: Przeszedł kontrolę (Bramka %d, %s).", 
           id, c_plec, bramka_nr, is_vip ? "VIP" : "STD");

    int moja_kolejka;
    
    if (is_vip) {
        logger(C_M, "P%d [VIP]: Idę do kolejki VIP.", id);
        moja_kolejka = 1;
        
        s_op(semid, SEM_TRAP_MUTEX, -1);
        sd->trap_wait_vip++;
        s_op(semid, SEM_TRAP_MUTEX, 1);
        
        s_op(semid, SEM_TRAP_Q_VIP, -1);
    } else {
        s_op(semid, SEM_POCZEKALNIA, -1);
        moja_kolejka = 0;
        
        s_op(semid, SEM_TRAP_MUTEX, -1);
        sd->trap_wait_norm++;
        s_op(semid, SEM_TRAP_MUTEX, 1);
        
        s_op(semid, SEM_TRAP_Q_NORM, -1);
    }
    
    bool w_srodku = false;
    bool na_trapie = false;
    bool czekam_na_trapie = false;
    int ostatni_prom_za_ciezki = -1;
    
    while (!w_srodku && !should_exit) {
        s_op(semid, SEM_TRAP_MUTEX, -1);
        
        if (!sd->zaladunek_aktywny || !sd->prom_w_porcie) {
            if (na_trapie) {
                logger(C_Y, "P%d: Prom odpłynął! Schodzę z trapu.", id);
                
                moja_kolejka = 2;
                sd->trap_wait_return++;
                
                sd->trap_count--;
                na_trapie = false;
                czekam_na_trapie = false;
                s_op(semid, SEM_TRAP_ENTER, 1);
                
                if (sd->trap_count == 0) {
                    s_op_nowait(semid, SEM_TRAP_EMPTY, 1);
                }
                
                s_op(semid, SEM_TRAP_MUTEX, 1);
                s_op(semid, SEM_TRAP_Q_RETURN, -1);
            } else {
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
            ostatni_prom_za_ciezki = -1;
            continue;
        }
        
        int aktualny_limit = sd->limit_bagazu_aktualny;
        int numer_promu = sd->prom_numer;
        
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
            }
            
            sd->trap_wait_heavy++;
            s_op(semid, SEM_TRAP_MUTEX, 1);
            s_op(semid, SEM_TRAP_Q_HEAVY, -1);
            continue;
        }
        
        if (waga > aktualny_limit) {
            if (na_trapie) {
                logger(C_Y, "P%d: Bagaż %d kg > limit %d kg. Schodzę z trapu.", 
                       id, waga, aktualny_limit);
                sd->trap_count--;
                na_trapie = false;
                czekam_na_trapie = false;
                s_op(semid, SEM_TRAP_ENTER, 1);
                
                if (sd->trap_count == 0) {
                    s_op_nowait(semid, SEM_TRAP_EMPTY, 1);
                }
                
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
                
                moja_kolejka = 2;
                sd->trap_wait_return++;
                s_op(semid, SEM_TRAP_MUTEX, 1);
                s_op(semid, SEM_TRAP_Q_RETURN, -1);
            } else {
                logger(C_Y, "P%d: Bagaż %d kg > limit %d kg. Czekam na inny prom.", 
                       id, waga, aktualny_limit);
                
                ostatni_prom_za_ciezki = numer_promu;
                
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
                
                moja_kolejka = 3;
                sd->trap_wait_heavy++;
                s_op(semid, SEM_TRAP_MUTEX, 1);
                s_op(semid, SEM_TRAP_Q_HEAVY, -1);
            }
            continue;
        }

        if (!na_trapie) {
            s_op(semid, SEM_TRAP_MUTEX, 1);
            
            s_op(semid, SEM_TRAP_ENTER, -1);
            
            s_op(semid, SEM_TRAP_MUTEX, -1);
            
            if (!sd->zaladunek_aktywny || !sd->prom_w_porcie) {
                s_op(semid, SEM_TRAP_ENTER, 1);
                
                if (moja_kolejka == 2) {
                    sd->trap_wait_return++;
                    s_op(semid, SEM_TRAP_MUTEX, 1);
                    s_op(semid, SEM_TRAP_Q_RETURN, -1);
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
            
            sd->trap_count++;
            na_trapie = true;
            czekam_na_trapie = false;
            
            logger(C_C, "P%d [%s]: Wchodzę na trap (pozycja %d/%d).", 
                   id, is_vip ? "VIP" : "STD", sd->trap_count, K_TRAP);
            
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
            
            SLEEP_TRAP_WALK();
            
            s_op(semid, SEM_TRAP_MUTEX, -1);
        }
        
        if (s_op_nowait(semid, SEM_FERRY_CAPACITY, -1) == -1) {
            if (!czekam_na_trapie) {
                logger(C_Y, "P%d: Prom pełny! Czekam na trapie.", id);
                czekam_na_trapie = true;
            }
            s_op(semid, SEM_TRAP_MUTEX, 1);
            
            usleep(50000);
            continue;
        }
        
        if (!sd->zaladunek_aktywny || sd->prom_numer != numer_promu) {
            s_op(semid, SEM_FERRY_CAPACITY, 1);
            
            logger(C_Y, "P%d: Prom %d odpłynął! Schodzę z trapu.", id, numer_promu);
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
        
        SLEEP_BOARDING();
        
        logger(C_G, "P%d [%s]: Wsiadłem do promu nr %d.", 
               id, is_vip ? "VIP" : "STD", numer_promu);
        
        sd->trap_count--;
        na_trapie = false;
        czekam_na_trapie = false;
        s_op(semid, SEM_TRAP_ENTER, 1);
        
        if (sd->trap_count == 0) {
            s_op_nowait(semid, SEM_TRAP_EMPTY, 1);
        }
        
        s_op(semid, SEM_TRAP_MUTEX, 1);
        w_srodku = true;
    }

    zakoncz_podroz(semid, sd, id);
    return 0;
}
#include "common.h"

void zakoncz_podroz(int semid, SharedData *sd, const char* powod, int id) {
    s_op(semid, SEM_SYSTEM_MUTEX, -1);
    sd->pasazerowie_w_systemie--;
    s_op(semid, SEM_SYSTEM_MUTEX, 1);
    
    s_op(semid, SEM_PROCESY, 1);
    exit(0);
}

int main(int argc, char* argv[]) {
    int id = atoi(argv[1]);
    srand(time(NULL) ^ (getpid() << 16) ^ id);

    key_t key = ftok(PATH_NAME, PROJECT_ID);
    int semid = semget(key, SEM_COUNT, 0600);
    int shmid = shmget(key, sizeof(SharedData), 0600);
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);

    int waga = 15 + (rand() % 20); 

    s_op(semid, SEM_BRAMKA, -1);

    s_op(semid, SEM_SYSTEM_MUTEX, -1);
    if (sd->blokada_odprawy) {
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        logger(C_R, "P%d: Port zamknięty! Wracam.", id);
        zakoncz_podroz(semid, sd, "Blokada", id);
    }
    s_op(semid, SEM_SYSTEM_MUTEX, 1);

    bool odprawiony = false;
    int proby = 1;

    while (!odprawiony) {
        if (sd->blokada_odprawy) {
            zakoncz_podroz(semid, sd, "Blokada", id);
        }

        s_op(semid, SEM_ODPRAWA, -1); 
        custom_sleep(50); 
        
        if (waga > sd->limit_bagazu_aktualny) {
            s_op(semid, SEM_ODPRAWA, 1); 
            waga -= (2 + rand() % 5);
            if (waga < 0) waga = 0;
            proby++;
            custom_sleep(100); 
        } else {
            logger(C_G, "P%d: Odprawiony (próba %d, bagaż %d kg).", id, proby, waga);
            odprawiony = true;
            s_op(semid, SEM_ODPRAWA, 1); 
        }
    }

    int plec = (rand() % 2) + 1; 
    
    char c_plec;
    int sem_kolejka;
    int sem_prio;

    if (plec == PLEC_M) {
        c_plec = 'M';
        sem_kolejka = SEM_SEC_Q_M;
        sem_prio = SEM_SEC_PRIO_M;
    } else {
        c_plec = 'K';
        sem_kolejka = SEM_SEC_Q_K;
        sem_prio = SEM_SEC_PRIO_K;
    }

    bool na_kontroli = false;
    int bramka_nr = -1;
    int frustracja = 0;

    while (!na_kontroli) {
        s_op(semid, SEM_SEC_MUTEX, -1); 
        
        for (int i = 0; i < LICZBA_STANOWISK_KONTROLI; i++) {
            if (sd->sec_liczba[i] == 0) {
                sd->sec_liczba[i]++; sd->sec_plec[i] = plec;
                bramka_nr = i; na_kontroli = true; break;
            }
        }
        
        if (!na_kontroli) {
            for (int i = 0; i < LICZBA_STANOWISK_KONTROLI; i++) {
                if (sd->sec_liczba[i] < MAX_OS_NA_KONTROLI && sd->sec_plec[i] == plec) {
                    sd->sec_liczba[i]++; bramka_nr = i; na_kontroli = true; break;
                }
            }
        }

        if (na_kontroli) {
            s_op(semid, SEM_SEC_MUTEX, 1); 
        } else {
            if (frustracja >= 3) {
                 if (plec == PLEC_M) sd->czekajacy_prio_m++; else sd->czekajacy_prio_k++;
                 s_op(semid, SEM_SEC_MUTEX, 1); s_op(semid, sem_prio, -1); 
            } else {
                 frustracja++;
                 if (plec == PLEC_M) sd->czekajacy_m++; else sd->czekajacy_k++;
                 s_op(semid, SEM_SEC_MUTEX, 1); s_op(semid, sem_kolejka, -1); 
            }
        }
    }

    custom_sleep(50);

    s_op(semid, SEM_SEC_MUTEX, -1);
    sd->sec_liczba[bramka_nr]--;
    if (sd->sec_liczba[bramka_nr] == 0) sd->sec_plec[bramka_nr] = PLEC_BRAK;
    
    int kogo_budzic = 0; 
    bool budzic_prio = false;
    bool pasuje_M = (sd->sec_liczba[bramka_nr] == 0 || sd->sec_plec[bramka_nr] == PLEC_M);
    bool pasuje_K = (sd->sec_liczba[bramka_nr] == 0 || sd->sec_plec[bramka_nr] == PLEC_K);

    if (pasuje_M && sd->czekajacy_prio_m > 0) { kogo_budzic = PLEC_M; budzic_prio = true; }
    else if (pasuje_K && sd->czekajacy_prio_k > 0) { kogo_budzic = PLEC_K; budzic_prio = true; }
    else if (pasuje_M && sd->czekajacy_m > 0) { kogo_budzic = PLEC_M; }
    else if (pasuje_K && sd->czekajacy_k > 0) { kogo_budzic = PLEC_K; }
    
    if (kogo_budzic == PLEC_M) {
        if (budzic_prio) { sd->czekajacy_prio_m--; s_op(semid, SEM_SEC_PRIO_M, 1); }
        else { sd->czekajacy_m--; s_op(semid, SEM_SEC_Q_M, 1); }
    }
    else if (kogo_budzic == PLEC_K) {
        if (budzic_prio) { sd->czekajacy_prio_k--; s_op(semid, SEM_SEC_PRIO_K, 1); }
        else { sd->czekajacy_k--; s_op(semid, SEM_SEC_Q_K, 1); }
    }
    s_op(semid, SEM_SEC_MUTEX, 1);

    bool is_vip = (rand() % 100) < 30;
    logger(C_C, "P%d [%c]: Przeszedł kontrolę (Bramka %d, VIP: %s).", 
           id, c_plec, bramka_nr, is_vip ? "TAK" : "NIE");

    s_op(semid, SEM_POCZEKALNIA, -1);
    
    bool w_srodku = false;
    while (!w_srodku) {
        s_op(semid, SEM_TRAP_MUTEX, -1);
        
        if (!sd->zaladunek_aktywny || !sd->prom_w_porcie) {
            s_op(semid, SEM_TRAP_MUTEX, 1); custom_sleep(200); continue;
        }
        
        if (waga > sd->limit_bagazu_aktualny) {
             logger(C_R, "P%d: Bagaż (%d) za ciężki na ten prom (Limit %d). Czekam.", 
                    id, waga, sd->limit_bagazu_aktualny);
             s_op(semid, SEM_TRAP_MUTEX, 1); sleep(2); continue;
        }

        s_op(semid, SEM_TRAP_MUTEX, 1);
        
        struct sembuf sb = {SEM_FERRY_CAPACITY, -1, 0};
        if (semop(semid, &sb, 1) == -1) continue;

        bool na_trapie = false;
        while (!na_trapie) {
            s_op(semid, SEM_TRAP_MUTEX, -1);
            
            if (!sd->zaladunek_aktywny || waga > sd->limit_bagazu_aktualny) {
                s_op(semid, SEM_FERRY_CAPACITY, 1); 
                s_op(semid, SEM_TRAP_MUTEX, 1);
                break; 
            }

            if (sd->trap_count < K_TRAP) {
                sd->trap_count++; na_trapie = true; s_op(semid, SEM_TRAP_MUTEX, 1);
            } else {
                if (is_vip) { 
                    sd->trap_wait_vip++; s_op(semid, SEM_TRAP_MUTEX, 1); s_op(semid, SEM_TRAP_Q_VIP, -1); 
                } else { 
                    sd->trap_wait_norm++; s_op(semid, SEM_TRAP_MUTEX, 1); s_op(semid, SEM_TRAP_Q_NORM, -1); 
                }
            }
        }

        if (na_trapie) {
            custom_sleep(50);
            
            s_op(semid, SEM_TRAP_MUTEX, -1);
            
            
            if (is_vip) {
                logger(C_G, "P%d [VIP]: Siedzę w promie nr %d.", id, sd->prom_numer);
            } else {
                logger(C_G, "P%d [STD]: Siedzę w promie nr %d.", id, sd->prom_numer);
            }

            sd->trap_count--;
            
            if (sd->trap_wait_vip > 0) { 
                sd->trap_wait_vip--; s_op(semid, SEM_TRAP_Q_VIP, 1); 
            } else if (sd->trap_wait_norm > 0) { 
                sd->trap_wait_norm--; s_op(semid, SEM_TRAP_Q_NORM, 1); 
            }
            s_op(semid, SEM_TRAP_MUTEX, 1);
            w_srodku = true;
        }
    }

    zakoncz_podroz(semid, sd, "Sukces", id);
    return 0;
}
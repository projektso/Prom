#include "common.h"

typedef struct {
    int id;
    int limit_bagazu;
} StatekInfo;

volatile bool force_departure = false;

void handle_sigusr1(int sig) {
    (void)sig;
    force_departure = true; 
    logger(C_M, "[PROM] Otrzymano SYGNAŁ 1 (SIGUSR1)!");
}

int main() {
    signal(SIGUSR1, handle_sigusr1);

    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) {
        perror("ftok failed in kapitan_promu");
        exit(1);
    }
    
    int semid = semget(key, SEM_COUNT, 0600);
    if (semid == -1) {
        perror("semget failed in kapitan_promu");
        exit(1);
    }
    
    int shmid = shmget(key, sizeof(SharedData), 0600);
    if (shmid == -1) {
        perror("shmget failed in kapitan_promu");
        exit(1);
    }
    
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) {
        perror("shmat failed in kapitan_promu");
        exit(1);
    }

    sd->pid_kapitan_promu = getpid();
    srand(time(NULL));

    StatekInfo flota[N_FLOTA];
    for (int i = 0; i < N_FLOTA; i++) {
        flota[i].id = i + 1; 
        flota[i].limit_bagazu = 15 + (rand() % 15); 
    }

    int current_ship_idx = 0; 

    while (1) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        int pozostalo = sd->pasazerowie_w_systemie;
        bool zamkniete = sd->blokada_odprawy;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);

        if (pozostalo <= 0 && zamkniete) {
            logger(C_B, "[KAPITAN] Brak pasażerów i odprawa zamknięta. Zamykam dok.");
            break;
        }

        struct sembuf sb_fleet = {SEM_FLOTA, -1, 0};
        if (semop(semid, &sb_fleet, 1) == -1) {
            if (errno == EINTR || errno == EIDRM) break;
            perror("semop SEM_FLOTA failed");
            break;
        }

        StatekInfo *statek = &flota[current_ship_idx];

        sd->prom_numer = statek->id;
        sd->limit_bagazu_aktualny = statek->limit_bagazu;
        sd->prom_w_porcie = true;
        sd->zaladunek_aktywny = true;
        force_departure = false;
        
        if (semctl(semid, SEM_FERRY_CAPACITY, SETVAL, P_POJEMNOSC) == -1) {
            perror("semctl SEM_FERRY_CAPACITY failed");
        }
        
        if (semctl(semid, SEM_TIMER_SIGNAL, SETVAL, 0) == -1) {
            perror("semctl SEM_TIMER_SIGNAL failed");
        }

        logger(C_B, "[PROM %d] Podstawiony (Limit %d kg). Pozostało: %d. Czekam T1=%ds.", 
               statek->id, statek->limit_bagazu, pozostalo, T1_OCZEKIWANIE);

        static bool first_run = true;
        if (first_run) {
            s_op(semid, SEM_FERRY_READY, 1);
            first_run = false;
        }
        
        s_op(semid, SEM_TRAP_MUTEX, -1);
        
        int ile_budzic = K_TRAP;
        
        for (int i = 0; i < ile_budzic; i++) {
            if (sd->trap_wait_return > 0) {
                s_op(semid, SEM_TRAP_Q_RETURN, 1);
                sd->trap_wait_return--;             
            } else if (sd->trap_wait_vip > 0) {
                s_op(semid, SEM_TRAP_Q_VIP, 1);
                sd->trap_wait_vip--;
            } else if (sd->trap_wait_norm > 0) {
                s_op(semid, SEM_TRAP_Q_NORM, 1);
                sd->trap_wait_norm--;
            } else {
                break;  
            }
        }
        s_op(semid, SEM_TRAP_MUTEX, 1);

        struct timespec timeout;
        timeout.tv_sec = T1_OCZEKIWANIE;
        timeout.tv_nsec = 0;
        
        struct sembuf sb_timer = {SEM_TIMER_SIGNAL, -1, 0};
        
        bool time_up = false;
        while (!time_up) {
            int ret = semtimedop(semid, &sb_timer, 1, &timeout);
            
            if (ret == -1) {
                if (errno == EAGAIN || errno == ETIMEDOUT) {
                    logger(C_B, "[PROM %d] Timer T1 zakończony (timeout).", statek->id);
                    time_up = true;
                    break;
                } else if (errno == EINTR) {
                    if (force_departure) {
                        logger(C_M, "[PROM %d] SYGNAŁ 1! Wypływam przed czasem.", statek->id);
                        time_up = true;
                        break;
                    }
                    continue;
                } else if (errno == EIDRM) {
                    break;
                } else {
                    perror("semtimedop failed");
                    time_up = true;
                    break;
                }
            } else {
                logger(C_B, "[PROM %d] Ktoś obudził timer wcześnie.", statek->id);
                time_up = true;
                break;
            }
        }
        
        int wolne = semctl(semid, SEM_FERRY_CAPACITY, GETVAL);
        if (wolne == 0) {
            logger(C_B, "[PROM %d] Komplet! Zamykam wejście.", statek->id);
        } else {
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            int pozostali = sd->pasazerowie_w_systemie;
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
            
            if (pozostali <= 0 && wolne == P_POJEMNOSC) {
                logger(C_B, "[PROM %d] Brak oczekujących. Wypływam.", statek->id);
            }
        }

        s_op(semid, SEM_TRAP_MUTEX, -1);
        sd->zaladunek_aktywny = false; 
        s_op(semid, SEM_TRAP_MUTEX, 1);

        logger(C_B, "[PROM %d] Załadunek zamknięty. Czekam na zejście z trapu...", statek->id);

        while (1) {
            s_op(semid, SEM_TRAP_MUTEX, -1);
            int on_trap = sd->trap_count;
            s_op(semid, SEM_TRAP_MUTEX, 1);
            if (on_trap == 0) break;
        }

        sd->prom_w_porcie = false;
        int on_board = P_POJEMNOSC - semctl(semid, SEM_FERRY_CAPACITY, GETVAL);

        pid_t rejs_pid = fork();
        if (rejs_pid == -1) {
            perror("fork rejs failed");
            logger(C_R, "[PROM %d] BŁĄD fork rejsu!", statek->id);
            s_op(semid, SEM_FLOTA, 1);
        } else if (rejs_pid == 0) {
            int moje_id = statek->id; 
            logger(C_B, "   >>> [PROM %d] ODPŁYWA (Pasażerów: %d). Rejs %ds.", 
                   moje_id, on_board, Ti_REJS);
            
            time_t start_time;
            time(&start_time);
            time_t current_time;
            time(&current_time);
            
            while ((current_time - start_time) < Ti_REJS) {
                time(&current_time);
            }
            
            logger(C_B, "   <<< [PROM %d] WRÓCIŁ do bazy.", moje_id);
            
            s_op(semid, SEM_FLOTA, 1);
            
            exit(0);
        }

        current_ship_idx++;
        if (current_ship_idx >= N_FLOTA) current_ship_idx = 0;
    }

    logger(C_B, "[KAPITAN] Czekam na powrót reszty floty...");
    while (wait(NULL) > 0);

    logger(C_B, "[KAPITAN] Cała flota w bazie. Koniec warty.");
    shmdt(sd);
    return 0;
}
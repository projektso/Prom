#include "common.h"

volatile sig_atomic_t received_signal2 = 0;

void handle_sigusr2(int sig) {
    (void)sig;
    received_signal2 = 1;
}

int main() {
    struct sigaction sa;
    sa.sa_handler = handle_sigusr2;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);
    
    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) { perror("ftok"); exit(1); }
    
    int semid = semget(key, SEM_COUNT, 0600);
    if (semid == -1) { perror("semget"); exit(1); }
    
    int shmid = shmget(key, sizeof(SharedData), 0600);
    if (shmid == -1) { perror("shmget"); exit(1); }
    
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) { perror("shmat"); exit(1); }

    sd->pid_kapitan_portu = getpid();

    logger(C_C, "[KAPITAN PORTU] Otwieram port!");
    
    for (int i = 0; i < 1000; i++) {
        s_op(semid, SEM_BRAMKA, 1);
    }
    for (int i = 0; i < 1000; i++) {
        s_op(semid, SEM_POCZEKALNIA, 1);
    }
    
    s_op(semid, SEM_SYSTEM_MUTEX, -1);
    for (int i = 0; i < LICZBA_STANOWISK_ODPRAWY; i++) {
        s_op(semid, SEM_ODPRAWA_QUEUE, 1);
    }
    s_op(semid, SEM_SYSTEM_MUTEX, 1);

    logger(C_C, "[KAPITAN PORTU] Pierwszy prom za %d sekund...", T_START_DELAY);
    
    time_t start_time, current_time;
    time(&start_time);
    while (1) {
        time(&current_time);
        if (current_time - start_time >= T_START_DELAY) break;
    }
    
    logger(C_C, "[KAPITAN PORTU] Wysyłam sygnał - można podstawić prom!");
    sd->pierwszy_prom_podstawiony = true;
    s_op(semid, SEM_FERRY_READY, 1);

    logger(C_C, "[KAPITAN PORTU] Monitoruję port...");
    
    while (!received_signal2) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        int pozostalo = sd->pasazerowie_w_systemie;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        
        if (pozostalo <= 0) {
            logger(C_C, "[KAPITAN PORTU] Brak pasażerów. Zamykam dok.");
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            sd->blokada_odprawy = true;
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
            break;
        }
        
        s_op_timed(semid, SEM_SYSTEM_MUTEX, 0, 1);
    }

    if (received_signal2) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        sd->blokada_odprawy = true;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        logger(C_R, "[KAPITAN PORTU] SYGNAŁ 2! Odprawa ZAMKNIĘTA.");
    }

    while (1) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        bool koniec = sd->wszyscy_obsluzeni;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        
        if (koniec) break;
        
        if (s_op_timed(semid, SEM_SYSTEM_MUTEX, 0, 2) == -2) break;
    }

    logger(C_C, "[KAPITAN PORTU] Czekam na powrót floty...");
    
    for (int i = 0; i < N_FLOTA; i++) {
         s_op(semid, SEM_FLOTA, -1);
     }
     for (int i = 0; i < N_FLOTA; i++) {
         s_op(semid, SEM_FLOTA, 1);
     }
    
    logger(C_C, "[KAPITAN PORTU] Cała flota w bazie. Koniec warty.");

    shmdt(sd);
    return 0;
}
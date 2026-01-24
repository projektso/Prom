#include "common.h"

volatile bool received_signal2 = false;

void handle_sigusr2(int sig) { 
    received_signal2 = true;
    logger(C_R, "[PORT] Otrzymano SYGNAŁ 2 (SIGUSR2)!");
}

int main() {
    signal(SIGUSR2, handle_sigusr2);
    
    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) {
        perror("ftok failed in kapitan_portu");
        exit(1);
    }
    
    int semid = semget(key, SEM_COUNT, 0600);
    if (semid == -1) {
        perror("semget failed in kapitan_portu");
        exit(1);
    }
    
    int shmid = shmget(key, sizeof(SharedData), 0600);
    if (shmid == -1) {
        perror("shmget failed in kapitan_portu");
        exit(1);
    }
    
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) {
        perror("shmat failed in kapitan_portu");
        exit(1);
    }

    sd->pid_kapitan_portu = getpid();

    s_op(semid, SEM_FERRY_READY, -1); 
    
    logger(C_W, "[PORT] Otwieram bramki, odprawę i poczekalnię!");
    s_op(semid, SEM_BRAMKA, 1000); 
    s_op(semid, SEM_POCZEKALNIA, 1000);

    logger(C_W, "[PORT] Oczekuję na SYGNAŁ 2 (zamknięcie odprawy)...");
    
    while (!received_signal2) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        int pozostalo = sd->pasazerowie_w_systemie;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        
        if (pozostalo <= 0) {
            logger(C_W, "[PORT] Wszyscy pasażerowie obsłużeni. Zamykam port.");
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            sd->blokada_odprawy = true;
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
            break;
        }
        
        struct timespec ts = {1, 0};
        struct sembuf sb_dummy = {SEM_SYSTEM_MUTEX, 0, 0};
        semtimedop(semid, &sb_dummy, 0, &ts);
    }

    if (received_signal2) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        sd->blokada_odprawy = true;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        logger(C_R, "[PORT] Odprawa ZAMKNIĘTA. Nowi pasażerowie nie mogą wejść.");
    }

    while (1) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        bool koniec = sd->wszyscy_obsluzeni;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        
        if (koniec) {
            logger(C_W, "[PORT] System się zakończył. Kończę pracę.");
            break;
        }
        
        struct timespec ts = {2, 0};
        struct sembuf sb_dummy = {SEM_SYSTEM_MUTEX, 0, 0};
        semtimedop(semid, &sb_dummy, 0, &ts);
    }

    shmdt(sd);
    return 0;
}
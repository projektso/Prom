#include "common.h"
void handle_sigusr2(int sig) { }

int main() {
    signal(SIGUSR2, handle_sigusr2);
    key_t key = ftok(PATH_NAME, PROJECT_ID);
    int semid = semget(key, SEM_COUNT, 0600);
    int shmid = shmget(key, sizeof(SharedData), 0600);
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);

    sd->pid_kapitan_portu = getpid();

    s_op(semid, SEM_FERRY_READY, -1); 
    logger(C_W, "[PORT] Otwieram bramki, odprawę i trap!");
    s_op(semid, SEM_BRAMKA, 1000); 
    s_op(semid, SEM_POCZEKALNIA, 1000);

    int czas_do_blokady = 25; 
    
    sleep(czas_do_blokady);

    if (!sd->wszyscy_obsluzeni) {
        logger(C_R, "[PORT] OTRZYMANO SYGNAŁ 2 (POLECENIE ZAMKNIĘCIA)!");
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        sd->blokada_odprawy = true;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        logger(C_R, "[PORT] Odprawa ZAMKNIĘTA.");
    }

    while(1) sleep(10);
    return 0;
}
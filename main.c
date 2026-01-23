#include "common.h"

int shmid, semid;
SharedData *sd;
pid_t pid_prom = -1, pid_port = -1;

void sprzatanie() {
    if (pid_port > 0) {
        kill(pid_port, SIGKILL);
    }
    
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
    }
    if (semid != -1) {
        semctl(semid, 0, IPC_RMID);
    }
    printf("\n[MAIN] Sprzątanie zakończone.\n");
}

void handle_sigint(int sig) { 
    kill(0, SIGKILL); 
    exit(0); 
}

int main(int argc, char* argv[]) {
    int liczba_pasazerow = 30; 
    if (argc > 1) {
        liczba_pasazerow = atoi(argv[1]);
    }

    signal(SIGINT, handle_sigint);
    atexit(sprzatanie);
    
    FILE *f = fopen(LOG_FILE, "w"); 
    if (f) {
        fclose(f);
    }

    key_t key = ftok(PATH_NAME, PROJECT_ID);
    shmid = shmget(key, sizeof(SharedData), IPC_CREAT | 0600);
    semid = semget(key, SEM_COUNT, IPC_CREAT | 0600);
    sd = (SharedData*)shmat(shmid, NULL, 0);
    memset(sd, 0, sizeof(SharedData));

    sd->pasazerowie_w_systemie = liczba_pasazerow;
    sd->blokada_odprawy = false;
    sd->wszyscy_obsluzeni = false;

    semctl(semid, SEM_PROCESY, SETVAL, 50); 
    semctl(semid, SEM_ODPRAWA, SETVAL, LICZBA_STANOWISK_ODPRAWY); 
    semctl(semid, SEM_BRAMKA, SETVAL, 0);       
    semctl(semid, SEM_FERRY_READY, SETVAL, 0);  
    semctl(semid, SEM_SYSTEM_MUTEX, SETVAL, 1);
    
    semctl(semid, SEM_SEC_MUTEX, SETVAL, 1); 
    semctl(semid, SEM_SEC_Q_M, SETVAL, 0);   
    semctl(semid, SEM_SEC_Q_K, SETVAL, 0);   
    semctl(semid, SEM_SEC_PRIO_M, SETVAL, 0);
    semctl(semid, SEM_SEC_PRIO_K, SETVAL, 0);
    
    semctl(semid, SEM_POCZEKALNIA, SETVAL, 0); 
    semctl(semid, SEM_TRAP_MUTEX, SETVAL, 1);
    semctl(semid, SEM_TRAP_Q_VIP, SETVAL, 0);
    semctl(semid, SEM_TRAP_Q_NORM, SETVAL, 0);
    semctl(semid, SEM_FERRY_CAPACITY, SETVAL, P_POJEMNOSC);

    semctl(semid, SEM_FLOTA, SETVAL, N_FLOTA);

    logger(C_G, "[MAIN] Start. Pasażerów: %d. Flota: %d. Czas Ti: %ds.", 
           liczba_pasazerow, N_FLOTA, Ti_REJS);

    pid_prom = fork();
    if (pid_prom == 0) { 
        execl("./kapitan_promu", "kapitan_promu", NULL); 
        exit(0); 
    }

    pid_port = fork();
    if (pid_port == 0) { 
        execl("./kapitan_portu", "kapitan_portu", NULL); 
        exit(0); 
    }

    char buff[32];
    for (int i = 1; i <= liczba_pasazerow; i++) {
        s_op(semid, SEM_PROCESY, -1);
        pid_t p = fork();
        
        if (p == 0) {
            sprintf(buff, "%d", i);
            execl("./pasazer", "pasazer", buff, NULL);
            exit(0);
        } 
        else if (p < 0) {
            s_op(semid, SEM_PROCESY, 1);
            perror("Błąd fork");
        }
        
        custom_sleep(50);
    }

    for (int i = 0; i < liczba_pasazerow; i++) {
        wait(NULL);
    }
    
    logger(C_G, "[MAIN] Pasażerowie zniknęli z listy procesów.");
    
    sd->wszyscy_obsluzeni = true;

    waitpid(pid_prom, NULL, 0);
    logger(C_G, "[MAIN] Symulacja zakończona poprawnie.");
    return 0;
}
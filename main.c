#include "common.h"

int shmid = -1, semid = -1;
SharedData *sd = NULL;
pid_t pid_prom = -1, pid_port = -1;
pid_t *pid_pasazerowie = NULL;
int liczba_utworzonych = 0;

void kill_all_processes() {
    if (pid_port > 0) {
        kill(pid_port, SIGTERM);
    }
    if (pid_prom > 0) {
        kill(pid_prom, SIGTERM);
    }
    
    if (pid_pasazerowie) {
        for (int i = 0; i < liczba_utworzonych; i++) {
            if (pid_pasazerowie[i] > 0) {
                kill(pid_pasazerowie[i], SIGKILL);
            }
        }
        free(pid_pasazerowie);
        pid_pasazerowie = NULL;
    }
}

void cleanup_ipc() {
    if (sd) {
        shmdt(sd);
        sd = NULL;
    }
    
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1) {
            perror("shmctl IPC_RMID failed");
        }
        shmid = -1;
    }
    
    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) {
            perror("semctl IPC_RMID failed");
        }
        semid = -1;
    }
}

void sprzatanie() {
    logger(C_Y, "[MAIN] Sprzątanie...");
    kill_all_processes();
    
    struct timespec ts = {2, 0};
    struct sembuf sb_dummy = {0, 0, 0};
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        semtimedop(semid, &sb_dummy, 0, &ts);
    }
    
    cleanup_ipc();
    logger(C_G, "[MAIN] Sprzątanie zakończone.");
}

void handle_sigint(int sig) { 
    logger(C_R, "\n[MAIN] Przerwano (Ctrl+C).");
    exit(0); 
}

void handle_error_and_exit(const char* msg) {
    perror(msg);
    logger(C_R, "[MAIN] Błąd krytyczny: %s (errno=%d)", msg, errno);
    exit(1);
}

int main(int argc, char* argv[]) {
    int liczba_pasazerow = 30; 
    
    if (argc > 1) {
        char *endptr;
        errno = 0;
        long val = strtol(argv[1], &endptr, 10);
        
        if (errno != 0 || *endptr != '\0' || val <= 0 || val > 100000) {
            fprintf(stderr, "BŁĄD: Nieprawidłowa liczba pasażerów: %s\n", argv[1]);
            fprintf(stderr, "Użycie: %s [liczba_pasażerów (1-100000)]\n", argv[0]);
            return 1;
        }
        
        liczba_pasazerow = (int)val;
    }
    
    if (validate_process_count(liczba_pasazerow) != 0) {
        return 1;
    }

    signal(SIGINT, handle_sigint);
    atexit(sprzatanie);
    
    FILE *f = fopen(LOG_FILE, "w"); 
    if (f) {
        fclose(f);
    } else {
        perror("fopen LOG_FILE failed");
    }

    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) {
        handle_error_and_exit("ftok failed");
    }
    
    shmid = shmget(key, sizeof(SharedData), IPC_CREAT | IPC_EXCL | 0600);
    if (shmid == -1) {
        if (errno == EEXIST) {
            fprintf(stderr, "BŁĄD: Segment pamięci już istnieje. Uruchom 'make clean'\n");
        }
        handle_error_and_exit("shmget failed");
    }
    
    semid = semget(key, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0600);
    if (semid == -1) {
        if (errno == EEXIST) {
            fprintf(stderr, "BŁĄD: Semafory już istnieją. Uruchom 'make clean'\n");
        }
        handle_error_and_exit("semget failed");
    }
    
    sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) {
        handle_error_and_exit("shmat failed");
    }
    
    memset(sd, 0, sizeof(SharedData));
    sd->pasazerowie_w_systemie = liczba_pasazerow;
    sd->blokada_odprawy = false;
    sd->wszyscy_obsluzeni = false;

    if (semctl(semid, SEM_PROCESY, SETVAL, liczba_pasazerow + 10) == -1) {
        handle_error_and_exit("semctl SEM_PROCESY failed");
    }
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
    semctl(semid, SEM_TRAP_Q_RETURN, SETVAL, 0);
    semctl(semid, SEM_FERRY_CAPACITY, SETVAL, P_POJEMNOSC);
    semctl(semid, SEM_FLOTA, SETVAL, N_FLOTA);
    semctl(semid, SEM_TIMER_SIGNAL, SETVAL, 0);

    logger(C_G, "[MAIN] ========== START SYMULACJI ==========");
    logger(C_G, "[MAIN] Pasażerów: %d | Flota: %d promów | Czas rejsu: %ds", 
           liczba_pasazerow, N_FLOTA, Ti_REJS);
    logger(C_G, "[MAIN] Pojemność promu: %d | Trap: %d | Stanowiska kontroli: %d", 
           P_POJEMNOSC, K_TRAP, LICZBA_STANOWISK_KONTROLI);

    pid_prom = fork();
    if (pid_prom == -1) {
        handle_error_and_exit("fork kapitan_promu failed");
    }
    if (pid_prom == 0) { 
        execl("./kapitan_promu", "kapitan_promu", NULL); 
        perror("execl kapitan_promu failed");
        exit(1); 
    }

    pid_port = fork();
    if (pid_port == -1) {
        handle_error_and_exit("fork kapitan_portu failed");
    }
    if (pid_port == 0) { 
        execl("./kapitan_portu", "kapitan_portu", NULL); 
        perror("execl kapitan_portu failed");
        exit(1); 
    }

    pid_pasazerowie = malloc(liczba_pasazerow * sizeof(pid_t));
    if (!pid_pasazerowie) {
        handle_error_and_exit("malloc pid_pasazerowie failed");
    }
    memset(pid_pasazerowie, 0, liczba_pasazerow * sizeof(pid_t));

    struct timespec ts_init = {1, 0};
    struct sembuf sb_dummy = {0, 0, 0};
    semtimedop(semid, &sb_dummy, 0, &ts_init);

    char buff[32];
    for (int i = 1; i <= liczba_pasazerow; i++) {
        if (s_op_nowait(semid, SEM_PROCESY, -1) == -1) {
            logger(C_R, "[MAIN] BŁĄD: Brak wolnych slotów procesów przy pasażerze %d", i);
            logger(C_R, "[MAIN] Uruchomiono %d/%d pasażerów. Kończę z błędem.", 
                   liczba_utworzonych, liczba_pasazerow);
            exit(1);
        }
        
        pid_t p = fork();
        
        if (p == -1) {
            s_op(semid, SEM_PROCESY, 1);
            
            perror("fork pasażera failed");
            logger(C_R, "[MAIN] BŁĄD FORK przy pasażerze %d (errno=%d)", i, errno);
            logger(C_R, "[MAIN] Utworzono %d/%d pasażerów przed błędem.", 
                   liczba_utworzonych, liczba_pasazerow);
            logger(C_R, "[MAIN] Rozpoczynam awaryjne czyszczenie...");
            
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            sd->pasazerowie_w_systemie = liczba_utworzonych;
            sd->blokada_odprawy = true;
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
            
            exit(1);
        }
        
        if (p == 0) {
            sprintf(buff, "%d", i);
            execl("./pasazer", "pasazer", buff, NULL);
            perror("execl pasazer failed");
            exit(1);
        }
        
        pid_pasazerowie[liczba_utworzonych++] = p;
    }

    logger(C_G, "[MAIN] Utworzono %d pasażerów. Czekam na zakończenie...", liczba_utworzonych);

    int completed = 0;
    while (completed < liczba_utworzonych) {
        int status;
        pid_t done = wait(&status);
        
        if (done == -1) {
            if (errno == ECHILD) break;
            if (errno == EINTR) continue;
            perror("wait failed");
            break;
        }
        
        bool is_passenger = false;
        for (int i = 0; i < liczba_utworzonych; i++) {
            if (pid_pasazerowie[i] == done) {
                is_passenger = true;
                break;
            }
        }
        
        if (is_passenger) {
            completed++;
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                logger(C_Y, "[MAIN] Pasażer (PID %d) zakończył się z kodem %d", 
                       done, WEXITSTATUS(status));
            }
        }
    }
    
    logger(C_G, "[MAIN] Wszyscy pasażerowie zakończyli podróż (%d/%d).", 
           completed, liczba_utworzonych);
    
    sd->wszyscy_obsluzeni = true;

    logger(C_G, "[MAIN] Czekam na zakończenie kapitanów...");
    
    if (pid_prom > 0) {
        int status;
        pid_t ret = waitpid(pid_prom, &status, 0);
        if (ret == -1) {
            perror("waitpid kapitan_promu failed");
        }
    }
    
    if (pid_port > 0) {
        int status;
        pid_t ret = waitpid(pid_port, &status, 0);
        if (ret == -1) {
            perror("waitpid kapitan_portu failed");
        }
    }
    
    logger(C_G, "[MAIN] ========== SYMULACJA ZAKOŃCZONA POMYŚLNIE ==========");
    return 0;
}
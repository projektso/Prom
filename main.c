#include "common.h"

int shmid = -1, semid = -1;
SharedData *sd = NULL;
pid_t pid_prom = -1, pid_port = -1;
pid_t *pid_pasazerowie = NULL;
int liczba_utworzonych = 0;

void kill_all_processes() {
    if (pid_port > 0) kill(pid_port, SIGTERM);
    if (pid_prom > 0) kill(pid_prom, SIGTERM);
    
    if (pid_pasazerowie) {
        for (int i = 0; i < liczba_utworzonych; i++) {
            if (pid_pasazerowie[i] > 0) kill(pid_pasazerowie[i], SIGKILL);
        }
        free(pid_pasazerowie);
        pid_pasazerowie = NULL;
    }
}

void cleanup_ipc() {
    if (sd) { shmdt(sd); sd = NULL; }
    if (shmid != -1) { shmctl(shmid, IPC_RMID, NULL); shmid = -1; }
    if (semid != -1) { semctl(semid, 0, IPC_RMID); semid = -1; }
}

void sprzatanie() {
    logger(C_Y, "[MAIN] Sprzątanie...");
    kill_all_processes();
    struct timespec ts = {1, 0};
    nanosleep(&ts, NULL);
    while (waitpid(-1, NULL, WNOHANG) > 0);
    cleanup_ipc();
    logger(C_G, "[MAIN] Zakończono.");
}

void handle_sigint(int sig) {
    (void)sig;
    logger(C_R, "\n[MAIN] Przerwano (Ctrl+C).");
    exit(0);
}

int main(int argc, char* argv[]) {
    int liczba_pasazerow = 30;
    
    if (argc > 1) {
        liczba_pasazerow = atoi(argv[1]);
        if (liczba_pasazerow <= 0 || liczba_pasazerow > 100000) {
            fprintf(stderr, "Użycie: %s [1-100000]\n", argv[0]);
            return 1;
        }
    }
    
    if (validate_process_count(liczba_pasazerow) != 0) return 1;

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    
    atexit(sprzatanie);
    
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);

    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) { perror("ftok"); exit(1); }
    
    shmid = shmget(key, sizeof(SharedData), IPC_CREAT | IPC_EXCL | 0600);
    if (shmid == -1) {
        if (errno == EEXIST) fprintf(stderr, "Uruchom 'make clean'\n");
        perror("shmget"); exit(1);
    }
    
    semid = semget(key, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0600);
    if (semid == -1) {
        if (errno == EEXIST) fprintf(stderr, "Uruchom 'make clean'\n");
        perror("semget"); exit(1);
    }
    
    sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) { perror("shmat"); exit(1); }
    
    memset(sd, 0, sizeof(SharedData));
    sd->pasazerowie_w_systemie = liczba_pasazerow;

    semctl(semid, SEM_PROCESY, SETVAL, liczba_pasazerow + 10);
    semctl(semid, SEM_ODPRAWA, SETVAL, LICZBA_STANOWISK_ODPRAWY);
    semctl(semid, SEM_ODPRAWA_QUEUE, SETVAL, 0);
    semctl(semid, SEM_BRAMKA, SETVAL, 0);
    semctl(semid, SEM_FERRY_READY, SETVAL, 0);
    semctl(semid, SEM_SYSTEM_MUTEX, SETVAL, 1);
    
    semctl(semid, SEM_SEC_MUTEX, SETVAL, 1);
    semctl(semid, SEM_SEC_QUEUE, SETVAL, 0);
    
    semctl(semid, SEM_POCZEKALNIA, SETVAL, 0);
    semctl(semid, SEM_TRAP_MUTEX, SETVAL, 1);
    semctl(semid, SEM_TRAP_ENTER, SETVAL, K_TRAP);
    semctl(semid, SEM_TRAP_Q_RETURN, SETVAL, 0);
    semctl(semid, SEM_TRAP_Q_HEAVY, SETVAL, 0);
    semctl(semid, SEM_TRAP_Q_VIP, SETVAL, 0);
    semctl(semid, SEM_TRAP_Q_NORM, SETVAL, 0);
    
    semctl(semid, SEM_FERRY_CAPACITY, SETVAL, P_POJEMNOSC);
    semctl(semid, SEM_FLOTA, SETVAL, N_FLOTA);
    semctl(semid, SEM_TIMER_SIGNAL, SETVAL, 0);
    semctl(semid, SEM_TRAP_EMPTY, SETVAL, 0);

    logger(C_G, "========== START SYMULACJI ==========");
    logger(C_G, "Pasażerów: %d | Flota: %d | Pojemność: %d | Trap: %d", 
           liczba_pasazerow, N_FLOTA, P_POJEMNOSC, K_TRAP);
    logger(C_G, "T1: %ds | Rejs: %ds | Start delay: %ds", 
           T1_OCZEKIWANIE, Ti_REJS, T_START_DELAY);

    pid_prom = fork();
    if (pid_prom == 0) {
        execl("./kapitan_promu", "kapitan_promu", NULL);
        exit(1);
    }

    pid_port = fork();
    if (pid_port == 0) {
        execl("./kapitan_portu", "kapitan_portu", NULL);
        exit(1);
    }

    pid_pasazerowie = malloc(liczba_pasazerow * sizeof(pid_t));
    memset(pid_pasazerowie, 0, liczba_pasazerow * sizeof(pid_t));

    struct timespec ts_init = {0, 100000000};
    nanosleep(&ts_init, NULL);

    char buff[32];
    for (int i = 1; i <= liczba_pasazerow; i++) {
        if (s_op_nowait(semid, SEM_PROCESY, -1) == -1) {
            logger(C_R, "[MAIN] Brak slotów przy P%d", i);
            sd->pasazerowie_w_systemie = liczba_utworzonych;
            sd->blokada_odprawy = true;
            exit(1);
        }
        
        pid_t p = fork();
        if (p == -1) {
            s_op(semid, SEM_PROCESY, 1);
            logger(C_R, "[MAIN] Fork failed przy P%d", i);
            sd->pasazerowie_w_systemie = liczba_utworzonych;
            sd->blokada_odprawy = true;
            exit(1);
        }
        
        if (p == 0) {
            sprintf(buff, "%d", i);
            execl("./pasazer", "pasazer", buff, NULL);
            exit(1);
        }
        
        pid_pasazerowie[liczba_utworzonych++] = p;
    }

    int completed = 0;
    while (completed < liczba_utworzonych) {
        int status;
        pid_t done = wait(&status);
        
        if (done == -1) {
            if (errno == ECHILD) break;
            if (errno == EINTR) continue;
            break;
        }
        
        for (int i = 0; i < liczba_utworzonych; i++) {
            if (pid_pasazerowie[i] == done) {
                pid_pasazerowie[i] = 0;
                completed++;
                break;
            }
        }
    }
    
    sd->wszyscy_obsluzeni = true;

    if (pid_prom > 0) { waitpid(pid_prom, NULL, 0); pid_prom = -1; }
    if (pid_port > 0) { waitpid(pid_port, NULL, 0); pid_port = -1; }
    
    logger(C_G, "========== SYMULACJA ZAKOŃCZONA ==========");
    return 0;
}
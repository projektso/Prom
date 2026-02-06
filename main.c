#include "common.h"


//ZMIENNE GLOBALNE
int shmid = -1, semid = -1; //Identyfikatory IPC
SharedData *sd = NULL; //Wskaźnik na pamięć dzieloną
pid_t pid_prom = -1, pid_port = -1; //PID-y kapitanów
pid_t *pid_pasazerowie = NULL; //Tablica PID-ów pasażerów
int liczba_utworzonych = 0; //Liczba utworzonych procesów pasażerów

//Wysyłanie sygnałów zakończenia do wszystkich procesów potomnych
void kill_all_processes() {
    //Zakończenie kapitanów
    if (pid_prom > 0) {
        kill(pid_prom, SIGTERM);
        waitpid(pid_prom, NULL, 0);
        pid_prom = -1;
    }
    if (pid_port > 0) {
        kill(pid_port, SIGTERM);
        waitpid(pid_port, NULL, 0);
        pid_port = -1;
    }
    
    //Zakończenie pasażerów
    if (pid_pasazerowie) {
        for (int i = 0; i < liczba_utworzonych; i++) {
            if (pid_pasazerowie[i] > 0) kill(pid_pasazerowie[i], SIGKILL);
        }
        free(pid_pasazerowie);
        pid_pasazerowie = NULL;
    }
}

//Zwalnianie zasobów IPC 
void cleanup_ipc() {
    if (sd) { shmdt(sd); sd = NULL; } //Odłączenie pamięci
    if (shmid != -1) { shmctl(shmid, IPC_RMID, NULL); shmid = -1; } //Usunięcie pamięci
    if (semid != -1) { semctl(semid, 0, IPC_RMID); semid = -1; } //Usunięcie semaforów
}

//Zabijanie procesów, czekanie na ich zakończenie, zwalnianie zasobów
void sprzatanie() {
    logger(C_Y, "[MAIN] Sprzątanie...");
    
    kill_all_processes();
    
    //Danie czasu procesom na zakończenie
    struct timespec ts = {1, 0};
    nanosleep(&ts, NULL);
    
    //Zbieranie procesów zombie
    while (waitpid(-1, NULL, WNOHANG) > 0);
    
    if (sd) {
    printf(" Odprawieni pasażerowie:   %ld\n", sd->stat_odprawieni);
    printf(" Przepłynęli (sukces):    %ld\n", sd->stat_przeplyneli);
    }
    cleanup_ipc();
    
    logger(C_G, "[MAIN] Zakończono.");
}

//Obsługa sygnału SIGINT
void handle_sigint(int sig) {
    (void)sig;
    logger(C_R, "\n[MAIN] Przerwano (Ctrl+C).");
    exit(0);
}

int main(int argc, char* argv[]) {
    int liczba_pasazerow = 30;
    
    //Odczytanie argumentów
    if (argc > 1) {
        liczba_pasazerow = atoi(argv[1]);
        if (liczba_pasazerow <= 0 || liczba_pasazerow > 100000) {
            fprintf(stderr, "Użycie: %s [1-100000]\n", argv[0]);
            return 1;
        }
    }
    
    //Walidacja liczby procesów
    if (validate_process_count(liczba_pasazerow) != 0) return 1;

    //Walidacja parametrów
    if (K_TRAP >= P_POJEMNOSC) {
        fprintf(stderr, "BŁĄD: Pojemność trapu (%d) musi być mniejsza niż pojemność promu (%d)\n", 
            K_TRAP, P_POJEMNOSC);
    return 1;
    }

    //Rejestracja obsługi sygnału SIGINT
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    
    //Rejestracja funkcji sprzątającej
    atexit(sprzatanie);
    
    //Utworzenie/wyczyszczenie pliku logu
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);

    //INICJALIZACJA ZASOBÓW IPC

    //Utworzenie klucza IPC
    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) { perror("ftok"); exit(1); }
    
    //Utworzenie segmentu pamięci dzielonej
    shmid = shmget(key, sizeof(SharedData), IPC_CREAT | IPC_EXCL | 0600);
    if (shmid == -1) {
        if (errno == EEXIST) fprintf(stderr, "Uruchom 'make clean'\n");
        perror("shmget"); exit(1);
    }
    
    //Utworzenie zestawu semaforów
    semid = semget(key, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0600);
    if (semid == -1) {
        if (errno == EEXIST) fprintf(stderr, "Uruchom 'make clean'\n");
        perror("semget"); exit(1);
    }
    
    //Dołączenie pamięci dzielonej
    sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) { perror("shmat"); exit(1); }
    
    //Inicjalizacja pamięci dzielonej
    memset(sd, 0, sizeof(SharedData));
    sd->pasazerowie_w_systemie = liczba_pasazerow;

    sd->stat_odprawieni = 0;
    sd->stat_przeplyneli = 0;

    //INICJALIZACJA SEMAFORÓW

    //Semafory systemowe
    semctl(semid, SEM_PROCESY, SETVAL, liczba_pasazerow + 10);
    semctl(semid, SEM_SYSTEM_MUTEX, SETVAL, 1);
    
    //Semafory odprawy
    semctl(semid, SEM_ODPRAWA, SETVAL, LICZBA_STANOWISK_ODPRAWY);
    semctl(semid, SEM_ODPRAWA_QUEUE, SETVAL, 0);
    semctl(semid, SEM_BRAMKA, SETVAL, 0);
    
    //Semafory kontroli bezpieczeństwa
    semctl(semid, SEM_SEC_MUTEX, SETVAL, 1);
    semctl(semid, SEM_SEC_QUEUE, SETVAL, 0);
    
    //Semafory trapu i poczekalni
    semctl(semid, SEM_POCZEKALNIA, SETVAL, 0);
    semctl(semid, SEM_TRAP_MUTEX, SETVAL, 1);
    semctl(semid, SEM_TRAP_ENTER, SETVAL, K_TRAP);
    semctl(semid, SEM_TRAP_Q_RETURN, SETVAL, 0);
    semctl(semid, SEM_TRAP_Q_HEAVY, SETVAL, 0);
    semctl(semid, SEM_TRAP_Q_VIP, SETVAL, 0);
    semctl(semid, SEM_TRAP_Q_NORM, SETVAL, 0);
    
    //Semafory promu
    semctl(semid, SEM_FERRY_READY, SETVAL, 0);
    semctl(semid, SEM_FERRY_CAPACITY, SETVAL, P_POJEMNOSC);
    semctl(semid, SEM_FLOTA, SETVAL, N_FLOTA);
    semctl(semid, SEM_TIMER_SIGNAL, SETVAL, 0);
    semctl(semid, SEM_TRAP_EMPTY, SETVAL, 0);
    semctl(semid, SEM_REJS_WAIT, SETVAL, 0);
    for (int i = 0; i < N_FLOTA; i++) {
        semctl(semid, SEM_PROM_START_BASE + i, SETVAL, 0);
    }


    //START SYMULACJI
    logger(C_G, "========== START SYMULACJI ==========");
    logger(C_G, "Pasażerów: %d | Flota: %d | Pojemność: %d | Trap: %d", 
           liczba_pasazerow, N_FLOTA, P_POJEMNOSC, K_TRAP);
    logger(C_G, "T1: %ds | Rejs: %ds | Start delay: %ds", 
           T1_OCZEKIWANIE, Ti_REJS, T_START_DELAY);

    //Utworzenie procesu kapitana promu
    pid_prom = fork();
    if (pid_prom == 0) {
        execl("./kapitan_promu", "kapitan_promu", NULL);
        exit(1);
    }

    //Utworzenie procesu kapitana portu
    pid_port = fork();
    if (pid_port == 0) {
        execl("./kapitan_portu", "kapitan_portu", NULL);
        exit(1);
    }

    //Alokacja tablicy PID-ów pasażerów
    pid_pasazerowie = malloc(liczba_pasazerow * sizeof(pid_t));
    memset(pid_pasazerowie, 0, liczba_pasazerow * sizeof(pid_t));

    //Krótkie opóźnienie na inicjalizację kapitanów
    struct timespec ts_init = {0, 100000000};
    nanosleep(&ts_init, NULL);

    //TWORZENIE PROCESÓW PASAŻERÓW
    char buff[32];
    int nastepny_id = 1;
    int completed = 0;
    while (nastepny_id <= liczba_pasazerow || completed < liczba_utworzonych) {
    //Zbieranie zakończonych procesów
    int status;
    pid_t done;
    while ((done = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < liczba_utworzonych; i++) {
            if (pid_pasazerowie[i] == done) {
                pid_pasazerowie[i] = 0;
                completed++;
                break;
            }
        }
    }
    
    //Tworzenie nowych pasażerów
    if (nastepny_id <= liczba_pasazerow) {
        int ret = s_op_timed(semid, SEM_PROCESY, -1, 1);  // 1s timeout
        
        if (ret == 0) {
            //Jest slot - tworzenie pasażera
            if (sd->blokada_odprawy) {
                s_op(semid, SEM_PROCESY, 1);
                
                int nieutworzeni = liczba_pasazerow - nastepny_id + 1;
                s_op(semid, SEM_SYSTEM_MUTEX, -1);
                sd->pasazerowie_w_systemie -= nieutworzeni;
                s_op(semid, SEM_SYSTEM_MUTEX, 1);
                
                nastepny_id = liczba_pasazerow + 1;
                continue;
            }
            
            pid_t p = fork();
            if (p == -1) {
                s_op(semid, SEM_PROCESY, 1);
                continue;
            }
            
            if (p == 0) {
                sprintf(buff, "%d", nastepny_id);
                execl("./pasazer", "pasazer", buff, NULL);
                exit(1);
            }
            
            pid_pasazerowie[liczba_utworzonych++] = p;
            if (liczba_utworzonych <= 100000) {
                sd->pidy_pasazerow[liczba_utworzonych - 1] = p;
                sd->liczba_pasazerow_pidy = liczba_utworzonych;
            }
            nastepny_id++;
        }
        // Jeśli timeout - pętla się powtórzy i zbierze zombie
    } else {
        //Wszyscy utworzeni - blokujące czekanie na zakończenie
        done = wait(&status);
        if (done > 0) {
            for (int i = 0; i < liczba_utworzonych; i++) {
                if (pid_pasazerowie[i] == done) {
                    pid_pasazerowie[i] = 0;
                    completed++;
                    break;
                }
            }
        } else if (done == -1 && errno == ECHILD) {
            break;
        }
    }
}

    sd->wszyscy_obsluzeni = true;

    //OCZEKIWANIE NA ZAKOŃCZENIE KAPITANÓW
    if (pid_prom > 0) { waitpid(pid_prom, NULL, 0); pid_prom = -1; }
    if (pid_port > 0) { waitpid(pid_port, NULL, 0); pid_port = -1; }
    
    logger(C_G, "========== SYMULACJA ZAKOŃCZONA ==========");
    return 0;
}
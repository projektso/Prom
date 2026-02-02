#include "common.h"

//ZMIENNE GLOBALNE DLA OBSŁUGI SYGNAŁÓW
volatile sig_atomic_t send_signal1 = 0;
volatile sig_atomic_t send_signal2 = 0;

void handle_sigusr1(int sig) {
    (void)sig;
    send_signal1 = 1;
}

void handle_sigusr2(int sig) {
    (void)sig;
    send_signal2 = 1;
}

int main() {
    struct sigaction sa1, sa2;
    
    //Handler dla SIGUSR1 (wcześniejszy wypływ promu)
    sa1.sa_handler = handle_sigusr1;
    sa1.sa_flags = 0;
    sigemptyset(&sa1.sa_mask);
    sigaction(SIGUSR1, &sa1, NULL);
    
    //Handler dla SIGUSR2 (zamknięcie odprawy)
    sa2.sa_handler = handle_sigusr2;
    sa2.sa_flags = 0;
    sigemptyset(&sa2.sa_mask);
    sigaction(SIGUSR2, &sa2, NULL);
    
    //POŁĄCZENIE Z ZASOBAMI IPC
    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) { perror("ftok"); exit(1); }
    
    int semid = semget(key, SEM_COUNT, 0600);
    if (semid == -1) { perror("semget"); exit(1); }
    
    int shmid = shmget(key, sizeof(SharedData), 0600);
    if (shmid == -1) { perror("shmget"); exit(1); }
    
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) { perror("shmat"); exit(1); }

    //Zapisanie PID w pamięci dzielonej
    sd->pid_kapitan_portu = getpid();

    //Otwarcie portu
    logger(C_C, "[KAPITAN PORTU] Otwieram port!");
    
    int max_pasazerow = sd->pasazerowie_w_systemie;

    //Otwarcie bramek wejściowych
    for (int i = 0; i < max_pasazerow; i++) {
        s_op(semid, SEM_BRAMKA, 1);
    }

    //Otwarcie poczekalni
    for (int i = 0; i < max_pasazerow; i++) {
        s_op(semid, SEM_POCZEKALNIA, 1);
    }
    
    //Inicjalizacja kolejki odprawy
    s_op(semid, SEM_SYSTEM_MUTEX, -1);
    for (int i = 0; i < LICZBA_STANOWISK_ODPRAWY; i++) {
        s_op(semid, SEM_ODPRAWA_QUEUE, 1);
    }
    s_op(semid, SEM_SYSTEM_MUTEX, 1);

    //OCZEKIWANIE NA START
    logger(C_C, "[KAPITAN PORTU] Pierwszy prom za %d sekund...", T_START_DELAY);
    
    struct sembuf sb_wait = {SEM_REJS_WAIT, -1, 0};
    struct timespec ts_delay = {T_START_DELAY, 0};
    semtimedop(semid, &sb_wait, 1, &ts_delay);
    
    logger(C_C, "[KAPITAN PORTU] Wysyłam sygnał - można podstawić prom!");
    sd->pierwszy_prom_podstawiony = true;
    s_op(semid, SEM_FERRY_READY, 1);

    //MONITOROWANIE PORTU
    logger(C_C, "[KAPITAN PORTU] Monitoruję port...");
    logger(C_C, "[KAPITAN PORTU] Wyślij SIGUSR1 (kill -USR1 %d) aby prom wypłynął wcześniej.", getpid());
    logger(C_C, "[KAPITAN PORTU] Wyślij SIGUSR2 (kill -USR2 %d) aby zamknąć odprawę.", getpid());
    
    while (1) {
        //Obsługa sygnału 1: wcześniejszy wypływ promu
        if (send_signal1) {
            send_signal1 = 0;
            if (sd->pid_kapitan_promu > 0) {
                logger(C_M, "[KAPITAN PORTU] SYGNAŁ 1! Wysyłam do kapitana promu - wcześniejszy wypływ.");
                kill(sd->pid_kapitan_promu, SIGUSR1);
            }
        }
        
        //Obsługa sygnału 2: zamknięcie odprawy
        if (send_signal2) {
            send_signal2 = 0;
            logger(C_R, "[KAPITAN PORTU] SYGNAŁ 2! Zamykam odprawę - nowi pasażerowie nie mogą wejść.");
            
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            sd->blokada_odprawy = true;
            s_op(semid, SEM_SYSTEM_MUTEX, 1);
            
            //Budzenie pasażerów śpiących w kolejkach
            for (int i = 0; i < 20000; i++) { 
                s_op_nowait(semid, SEM_ODPRAWA_QUEUE, 1);
                s_op_nowait(semid, SEM_BRAMKA, 1);
            }
        }
        
        //Sprawdzenie czy są jeszcze pasażerowie
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

    //OCZEKIWANIE NA ZAKOŃCZENIE OBSŁUGI
    while (1) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        bool koniec = sd->wszyscy_obsluzeni;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);
        
        if (koniec) break;
        
        if (s_op_timed(semid, SEM_SYSTEM_MUTEX, 0, 2) == -2) break;
    }

    //OCZEKIWANIE NA POWRÓT FLOTY
    logger(C_C, "[KAPITAN PORTU] Czekam na powrót floty...");
    
    //Czekanie aż wszystkie promy wrócą
    for (int i = 0; i < N_FLOTA; i++) {
        s_op(semid, SEM_FLOTA, -1);
    }
    //Przywrócenie wartości semaforów
    for (int i = 0; i < N_FLOTA; i++) {
        s_op(semid, SEM_FLOTA, 1);
    }
    
    logger(C_C, "[KAPITAN PORTU] Cała flota w bazie. Koniec warty.");
    
    shmdt(sd);
    return 0;
}

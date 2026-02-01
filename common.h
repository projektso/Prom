#ifndef COMMON_H
#define COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <fcntl.h>

#define PROJECT_ID 'C'
#define PATH_NAME "."
#define LOG_FILE "symulacja_prom.txt"

//PARAMETRY SYMULACJI - PROMY
#define N_FLOTA 3
#define P_POJEMNOSC 6
#define K_TRAP 3
#define T1_OCZEKIWANIE 5
#define Ti_REJS 6
#define T_START_DELAY 10

//PARAMETRY SYMULACJI - ODPRAWA I KONTROLA
#define LICZBA_STANOWISK_ODPRAWY 2
#define LICZBA_STANOWISK_KONTROLI 3
#define MAX_OS_NA_KONTROLI 2
#define MAX_PRZEPUSZCZEN 3
#define Mp_LIMIT_ODPRAWY 20

#define PLEC_BRAK 0
#define PLEC_M 1
#define PLEC_K 2

#define VISUAL_SLEEP_ENABLED 1

#if VISUAL_SLEEP_ENABLED
    #define SLEEP_ODPRAWA()      usleep(100000)
    #define SLEEP_KONTROLA()     usleep(150000)
    #define SLEEP_TRAP_WALK()    usleep(200000)
    #define SLEEP_BOARDING()     usleep(100000)
#else
    #define SLEEP_ODPRAWA()
    #define SLEEP_KONTROLA()
    #define SLEEP_TRAP_WALK()
    #define SLEEP_BOARDING()
#endif

//DEFINICJE SEMAFORÓW
enum {
    //Semafory systemowe
    SEM_PROCESY, //Kontrola maksymalnej liczby procesów
    SEM_SYSTEM_MUTEX, //Mutex do ochrony danych dzielonych
    
    //Semafory odprawy biletowo-bagażowej
    SEM_ODPRAWA, //Dostęp do stanowisk odprawy
    SEM_ODPRAWA_QUEUE, //Kolejka FIFO do odprawy
    SEM_BRAMKA, //Bramka wejściowa do portu
    
    //Semafory kontroli bezpieczeństwa
    SEM_SEC_MUTEX, //Mutex do ochrony stanu bramek kontroli
    SEM_SEC_QUEUE, //Kolejka oczekujących na kontrolę
    
    //Semafory poczekalni i trapu
    SEM_POCZEKALNIA, //Wejście do poczekalni
    SEM_TRAP_MUTEX, //Mutex do ochrony stanu trapu
    SEM_TRAP_ENTER, //Kontrola pojemności trapu
    SEM_TRAP_Q_RETURN, //Kolejka priorytet 1: pasażerowie którzy zeszli z trapu
    SEM_TRAP_Q_HEAVY, //Kolejka priorytet 2: za ciężki bagaż na aktualny prom
    SEM_TRAP_Q_VIP, //Kolejka priorytet 3: pasażerowie VIP
    SEM_TRAP_Q_NORM, //Kolejka priorytet 4: zwykli pasażerowie
    
    //Semafory promu
    SEM_FERRY_READY, //Sygnał gotowości do podstawienia promu
    SEM_FERRY_CAPACITY, //Kontrola pojemności promu
    SEM_FLOTA, //Liczba dostępnych promów
    SEM_TIMER_SIGNAL, //Sygnał zakończenia załadunku
    SEM_TRAP_EMPTY, //Sygnał że trap jest pusty
    SEM_REJS_WAIT, //Sygnał oczekiwania na pierwszy rejs

    SEM_COUNT //Łączna liczba semaforów
};

//KOLORY TERMINALA
#define C_R "\033[1;31m" //Czerwony - błędy, frustracja
#define C_G "\033[1;32m" //Zielony - sukces, start/koniec
#define C_B "\033[1;34m" //Niebieski - kapitan promu, promy
#define C_Y "\033[1;33m" //Żółty - ostrzeżenia
#define C_C "\033[1;36m" //Cyjan - kapitan portu, kontrola
#define C_M "\033[1;35m" //Magenta - VIP, sygnały
#define C_W "\033[1;37m" //Biały
#define C_0 "\033[0m" //Reset koloru

//STRUKTURA DANYCH DZIELONYCH
typedef struct {
    //Stan aktualnego promu
    int prom_numer;
    int limit_bagazu_aktualny;
    
    //Flagi stanu systemu (volatile dla widoczności między procesami)
    volatile bool prom_w_porcie;
    volatile bool zaladunek_aktywny;
    volatile bool blokada_odprawy;
    volatile bool wszyscy_obsluzeni;
    volatile bool pierwszy_prom_podstawiony;
    
    //PID-y procesów zarządzających
    pid_t pid_kapitan_promu;
    pid_t pid_kapitan_portu;

    //Liczniki pasażerów
    volatile int pasazerowie_w_systemie;

    //Stan odprawy biletowo-bagażowej
    int odprawa_czekajacy;

    //Stan stanowisk kontroli bezpieczeństwa
    int sec_liczba[LICZBA_STANOWISK_KONTROLI];
    int sec_plec[LICZBA_STANOWISK_KONTROLI];
    int sec_czekajacy;

    //Stan trapu i kolejek priorytetowych
    volatile int trap_count;
    int trap_wait_return;
    int trap_wait_heavy;
    int trap_wait_vip;
    int trap_wait_norm;

    //Tablica PID-ów pasażerów (do wysyłania sygnałów)
    pid_t pidy_pasazerow[100000];
    int liczba_pasazerow_pidy;

} SharedData;

//Wypisanie kolorowego komunikatu na terminal i zapisanie go do pliku logu
static inline void logger(const char* color, const char* fmt, ...) {
    va_list args;
    char buffer[1024];
    
    //Formatowanie wiadomości
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    //Wypisanie na terminal z kolorem
    printf("%s%s%s\n", color, buffer, C_0);
    fflush(stdout);
    
    //Zapis do pliku logu
    int fd = open(LOG_FILE, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd >= 0) {
        flock(fd, LOCK_EX);  //Blokada wyłączna
        char full_buffer[1100];
        int len = snprintf(full_buffer, sizeof(full_buffer), "%s\n", buffer);
        if (write(fd, full_buffer, len) < 0) { }
        flock(fd, LOCK_UN);  //Zwolnienie blokady
        close(fd);
    }
}

//OPERACJA NA SEMAFORZE BLOKUJĄCA
static inline void s_op(int semid, int n, int op) {
    struct sembuf sb;
    sb.sem_num = n;
    sb.sem_op = op;
    sb.sem_flg = 0;
    
    while (1) {
        if (semop(semid, &sb, 1) == 0) return; //Sukces
        if (errno == EINTR) continue; //Przerwane
        if (errno == EIDRM) return; //Semafor usunięty
        perror("semop failed");
        fprintf(stderr, "sem_num=%d, sem_op=%d, errno=%d\n", n, op, errno);
        exit(1);
    }
}

//OPERACJA NA SEMAFORZE NIEBLOKUJĄCA
static inline int s_op_nowait(int semid, int n, int op) {
    struct sembuf sb;
    sb.sem_num = n;
    sb.sem_op = op;
    sb.sem_flg = IPC_NOWAIT;
    
    int ret = semop(semid, &sb, 1);
    if (ret == -1) {
        if (errno == EAGAIN) return -1;
        if (errno == EINTR || errno == EIDRM) return -1;
        perror("semop_nowait failed");
        exit(1);
    }
    return 0;
}

//OPERACJA NA SEMAFORZE Z TIMEOUTEM
static inline int s_op_timed(int semid, int n, int op, int timeout_sec) {
    struct sembuf sb;
    sb.sem_num = n;
    sb.sem_op = op;
    sb.sem_flg = 0;
    
    struct timespec ts;
    ts.tv_sec = timeout_sec;
    ts.tv_nsec = 0;
    
    while (1) {
        int ret = semtimedop(semid, &sb, 1, &ts);
        if (ret == 0) return 0; //Sukces
        if (errno == EAGAIN || errno == ETIMEDOUT) return -1; //Timeout
        if (errno == EINTR) continue; //Przerwane
        if (errno == EIDRM) return -2; //Semafor usunięty
        perror("semtimedop failed");
        return -1;
    }
}

//WALIDACJA LICZBY PROCESÓW
static inline int validate_process_count(int count) {
    if (count <= 0 || count > 100000) {
        fprintf(stderr, "BŁĄD: Liczba pasażerów musi być między 1 a 100000\n");
        return -1;
    }
    return 0;
}

#endif
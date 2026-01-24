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
#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <sys/resource.h>

#define PROJECT_ID 'C'
#define PATH_NAME "."
#define LOG_FILE "symulacja_prom.txt"

#define N_FLOTA 3                 
#define P_POJEMNOSC 6             
#define K_TRAP 3                  
#define T1_OCZEKIWANIE 5          
#define Ti_REJS 6                 

#define LICZBA_STANOWISK_ODPRAWY 2 
#define LICZBA_STANOWISK_KONTROLI 3
#define MAX_OS_NA_KONTROLI 2
#define Mp_LIMIT_ODPRAWY 20    

#define PLEC_BRAK 0
#define PLEC_M 1
#define PLEC_K 2

enum {
    SEM_PROCESY, 
    SEM_ODPRAWA, 
    SEM_BRAMKA, 
    SEM_FERRY_READY, 
    
    SEM_SEC_MUTEX, 
    SEM_SEC_Q_M, 
    SEM_SEC_Q_K, 
    SEM_SEC_PRIO_M, 
    SEM_SEC_PRIO_K,

    SEM_POCZEKALNIA, 
    SEM_TRAP_MUTEX, 
    SEM_TRAP_Q_VIP, 
    SEM_TRAP_Q_NORM,
    SEM_TRAP_Q_RETURN,
    
    SEM_FERRY_CAPACITY, 
    SEM_SYSTEM_MUTEX,
    SEM_FLOTA,
    
    SEM_TIMER_SIGNAL,

    SEM_COUNT
};

#define C_R "\033[1;31m" 
#define C_G "\033[1;32m" 
#define C_B "\033[1;34m" 
#define C_Y "\033[1;33m" 
#define C_C "\033[1;36m" 
#define C_M "\033[1;35m" 
#define C_W "\033[1;37m" 
#define C_0 "\033[0m"

typedef struct {
    int prom_numer;
    int limit_bagazu_aktualny;
    bool prom_w_porcie;       
    bool zaladunek_aktywny;   
    pid_t pid_kapitan_promu;  
    pid_t pid_kapitan_portu;

    int pasazerowie_w_systemie; 
    bool blokada_odprawy;       
    bool wszyscy_obsluzeni; 

    int sec_liczba[LICZBA_STANOWISK_KONTROLI]; 
    int sec_plec[LICZBA_STANOWISK_KONTROLI];   
    int czekajacy_m; 
    int czekajacy_k; 
    int czekajacy_prio_m; 
    int czekajacy_prio_k;

    int trap_count;         
    int trap_wait_vip;      
    int trap_wait_norm;
    int trap_wait_return;

} SharedData;

static inline void logger(const char* color, const char* fmt, ...) {
    va_list args;
    char buffer[1024];
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    printf("%s%s%s\n", color, buffer, C_0);
    fflush(stdout);
    
    FILE *f = fopen(LOG_FILE, "a");
    if (f) { 
        fprintf(f, "%s\n", buffer); 
        fclose(f); 
    }
}

static inline void s_op(int semid, int n, int op) {
    struct sembuf sb;
    sb.sem_num = n;
    sb.sem_op = op;
    sb.sem_flg = 0;
    
    if (semop(semid, &sb, 1) == -1) {
        if (errno == EINTR || errno == EIDRM) {
            return;
        }
        perror("semop failed");
        fprintf(stderr, "sem_num=%d, sem_op=%d, errno=%d\n", n, op, errno);
        exit(1);
    }
}

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

static inline int validate_process_count(int count) {
    if (count <= 0) {
        fprintf(stderr, "BŁĄD: Liczba pasażerów musi być > 0\n");
        return -1;
    }
    
    if (count > 100000) {
        fprintf(stderr, "BŁĄD: Liczba pasażerów nie może przekraczać 100000\n");
        return -1;
    }
    
    struct rlimit rl;
    if (getrlimit(RLIMIT_NPROC, &rl) == 0) {
        int max_safe = rl.rlim_cur - 50;
        if (count > max_safe) {
            fprintf(stderr, "BŁĄD: Limit procesów (%d) przekroczony. Max bezpieczna wartość: %d\n", 
                    (int)rl.rlim_cur, max_safe);
            fprintf(stderr, "Zwiększ limit: ulimit -u %d\n", count + 100);
            return -1;
        }
    }
    
    return 0;
}

#endif
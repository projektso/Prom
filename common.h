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
#define Mp_START 20       

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
    
    SEM_FERRY_CAPACITY, 
    SEM_SYSTEM_MUTEX,
    SEM_FLOTA,

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

} SharedData;

static inline void custom_sleep(int ms) { 
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

static inline void logger(const char* color, const char* fmt, ...) {
    va_list args;
    char buffer[1024];
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    printf("%s%s%s\n", color, buffer, C_0);
    
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
        if (errno != EINTR && errno != EIDRM) {
            perror("Semop error"); 
            exit(1);
        }
    }
}
#endif
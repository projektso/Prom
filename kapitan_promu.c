#include "common.h"

typedef struct {
    int id;
    int limit_bagazu;
} StatekInfo;

volatile bool force_departure = false;

void handle_sigusr1(int sig) { 
    force_departure = true; 
}

int main() {
    signal(SIGUSR1, handle_sigusr1);

    key_t key = ftok(PATH_NAME, PROJECT_ID);
    int semid = semget(key, SEM_COUNT, 0600);
    int shmid = shmget(key, sizeof(SharedData), 0600);
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);

    sd->pid_kapitan_promu = getpid();
    srand(time(NULL));

    StatekInfo flota[N_FLOTA];
    for (int i = 0; i < N_FLOTA; i++) {
        flota[i].id = i + 1; 
        flota[i].limit_bagazu = 15 + (rand() % 15); 
    }

    int current_ship_idx = 0; 

    while (1) {
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        int pozostalo = sd->pasazerowie_w_systemie;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);

        if (pozostalo <= 0) {
            logger(C_B, "[KAPITAN] Brak pasażerów oczekujących. Zamykam dok.");
            break;
        }

        struct sembuf sb_fleet = {SEM_FLOTA, -1, 0};
        if (semop(semid, &sb_fleet, 1) == -1) break;

        StatekInfo *statek = &flota[current_ship_idx];

        sd->prom_numer = statek->id;
        sd->limit_bagazu_aktualny = statek->limit_bagazu;
        sd->prom_w_porcie = true;
        sd->zaladunek_aktywny = true;
        force_departure = false;
        
        semctl(semid, SEM_FERRY_CAPACITY, SETVAL, P_POJEMNOSC);

        logger(C_B, "[PROM %d] Podstawiony (Limit %d). Pozostało pasażerów: %d. Czekam T1=%ds.", 
               statek->id, statek->limit_bagazu, pozostalo, T1_OCZEKIWANIE);

        static bool first_run = true;
        if (first_run) {
            struct sembuf sb = {SEM_FERRY_READY, 1, 0};
            semop(semid, &sb, 1);
            first_run = false;
        }

        int elapsed = 0;
        while (elapsed < T1_OCZEKIWANIE) {
            if (force_departure) {
                logger(C_M, "[PROM %d] Sygnał 1! Wypływam.", statek->id);
                break;
            }
            
            int wolne = semctl(semid, SEM_FERRY_CAPACITY, GETVAL);
            if (wolne == 0) {
                logger(C_B, "[PROM %d] Komplet biletów sprzedany. Zamykam wejście.", statek->id);
                break;
            }
            
            s_op(semid, SEM_SYSTEM_MUTEX, -1);
            if (sd->pasazerowie_w_systemie <= 0 && wolne == P_POJEMNOSC) {
                 s_op(semid, SEM_SYSTEM_MUTEX, 1); 
                 break; 
            }
            s_op(semid, SEM_SYSTEM_MUTEX, 1);

            sleep(1);
            if (!force_departure) elapsed++;
        }

        s_op(semid, SEM_TRAP_MUTEX, -1);
        sd->zaladunek_aktywny = false; 
        s_op(semid, SEM_TRAP_MUTEX, 1);

        // Czekanie na zejście z trapu
        while (1) {
            s_op(semid, SEM_TRAP_MUTEX, -1);
            int on_trap = sd->trap_count;
            s_op(semid, SEM_TRAP_MUTEX, 1);
            if (on_trap == 0) break;
            custom_sleep(100);
        }

        sd->prom_w_porcie = false;
        int on_board = P_POJEMNOSC - semctl(semid, SEM_FERRY_CAPACITY, GETVAL);

        pid_t rejs_pid = fork();
        
        if (rejs_pid == 0) {
            int moje_id = statek->id; 
            logger(C_B, "   >>> [PROM %d] ODPŁYWA (Liczba os: %d). Rejs %ds.", moje_id, on_board, Ti_REJS);
            
            sleep(Ti_REJS); 
            
            logger(C_B, "   <<< [PROM %d] WRÓCIŁ do bazy.", moje_id);
            
            struct sembuf sb_back = {SEM_FLOTA, 1, 0};
            semop(semid, &sb_back, 1);
            
            exit(0);
        }

        current_ship_idx++;
        if (current_ship_idx >= N_FLOTA) current_ship_idx = 0;
    }

    logger(C_B, "[KAPITAN] Czekam na powrót reszty floty...");
    while (wait(NULL) > 0);

    logger(C_B, "[KAPITAN] Cała flota w bazie. Koniec warty.");
    return 0;
}
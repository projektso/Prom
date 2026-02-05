#include "common.h"

//STRUKTURA INFORMACJI O STATKU
typedef struct {
    int id;
    int limit_bagazu;
} StatekInfo;

//ZMIENNA GLOBALNA DLA OBSŁUGI SYGNAŁU
volatile sig_atomic_t force_departure = 0;

void handle_sigusr1(int sig) {
    (void)sig;
    force_departure = 1;
}

int main() {
    int statki_na_morzu=0;
    //REJESTRACJA HANDLERA SYGNAŁU 
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    //POŁĄCZENIE Z ZASOBAMI IPC
    key_t key = ftok(PATH_NAME, PROJECT_ID);
    if (key == -1) { perror("ftok"); exit(1); }
    
    int semid = semget(key, SEM_COUNT, 0600);
    if (semid == -1) { perror("semget"); exit(1); }
    
    int shmid = shmget(key, sizeof(SharedData), 0600);
    if (shmid == -1) { perror("shmget"); exit(1); }
    
    SharedData *sd = (SharedData*)shmat(shmid, NULL, 0);
    if (sd == (void*)-1) { perror("shmat"); exit(1); }

    //Zapisanie PID i zainicjowanie generatora liczb losowych
    sd->pid_kapitan_promu = getpid();
    srand(time(NULL) ^ getpid());

    //INICJALIZACJA FLOTY
    StatekInfo flota[N_FLOTA];
    for (int i = 0; i < N_FLOTA; i++) {
        flota[i].id = i + 1;
        flota[i].limit_bagazu = 15 + (rand() % 15);
    }
    
    //Upewnienie się że minimum 1 prom ma limit >= Mp_LIMIT_ODPRAWY
    bool jest_wystarczajacy = false;
    for (int i = 0; i < N_FLOTA; i++) {
        if (flota[i].limit_bagazu >= Mp_LIMIT_ODPRAWY) {
            jest_wystarczajacy = true;
            break;
        }
    }
    if (!jest_wystarczajacy) {
        flota[rand() % N_FLOTA].limit_bagazu = Mp_LIMIT_ODPRAWY;
    }

    int current_ship_idx = 0;

    //OCZEKIWANIE NA SYGNAŁ OD KAPITANA PORTU
    logger(C_B, "[KAPITAN PROMU] Czekam na sygnał od kapitana portu...");
    s_op(semid, SEM_FERRY_READY, -1);
    logger(C_B, "[KAPITAN PROMU] Otrzymano sygnał. Rozpoczynam obsługę.");

    //GŁÓWNA PĘTLA OBSŁUGI PROMÓW
    while (1) {
        //Sprzątanie zombie
        int status;
        pid_t w;
        //Zdejmowanie martwych procesów
        while ((w = waitpid(-1, &status, WNOHANG)) > 0) {
            statki_na_morzu--;
        }

        if (w == -1 && errno == ECHILD) {
            statki_na_morzu = 0;
        }

        while (statki_na_morzu >= N_FLOTA) {
            w = wait(&status); 
            if (w > 0) {
                statki_na_morzu--;
            } else if (w == -1 && errno == ECHILD) {
                statki_na_morzu = 0;
                break;
            } else if (errno == EINTR) {
                continue; 
            }
        }

        //Sprawdzenie warunków zakończenia
        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        int pozostalo = sd->pasazerowie_w_systemie;
        bool zamkniete = sd->blokada_odprawy;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);

        //Jeśli koniec pracy i statki wróciły
        if (pozostalo <= 0 && zamkniete && statki_na_morzu == 0) {
            break;
        }

        //Jeśli koniec pracy, ale statki wciąż płyną
        if (pozostalo <= 0 && zamkniete) {
            continue;
        }

        //Oczekiwanie na dostępny prom
        struct sembuf sb_fleet = {SEM_FLOTA, -1, 0};
        if (semop(semid, &sb_fleet, 1) == -1) {
            if (errno == EINTR || errno == EIDRM) break;
            break;
        }


        StatekInfo *statek = &flota[current_ship_idx];

        //Podstawienie promu
        s_op(semid, SEM_TRAP_MUTEX, -1);
        
        //Ustawienie informacji o promie
        sd->prom_numer = statek->id;
        sd->limit_bagazu_aktualny = statek->limit_bagazu;
        sd->prom_w_porcie = true;
        sd->zaladunek_aktywny = true;
        sd->trap_count = 0;
        
        //Odczyt liczby czekających pasażerów
        int czekajacy_return = sd->trap_wait_return;
        int czekajacy_heavy = sd->trap_wait_heavy;
        int czekajacy_vip = sd->trap_wait_vip;
        int czekajacy_norm = sd->trap_wait_norm;
        
        s_op(semid, SEM_TRAP_MUTEX, 1);
        
        //Reset flagi sygnału
        force_departure = 0;
        
        //Reset semaforów dla nowego załadunku
        semctl(semid, SEM_FERRY_CAPACITY, SETVAL, P_POJEMNOSC);
        semctl(semid, SEM_TIMER_SIGNAL, SETVAL, 0);
        semctl(semid, SEM_TRAP_EMPTY, SETVAL, 0);
        semctl(semid, SEM_TRAP_ENTER, SETVAL, K_TRAP);

        logger(C_B, "[PROM %d] Podstawiony (Limit %d kg). Pozostało: %d pasażerów.", 
               statek->id, statek->limit_bagazu, pozostalo);
        logger(C_B, "[PROM %d] Czekający: %d (ret:%d, heavy:%d, vip:%d, norm:%d). Załadunek: %ds...", 
               statek->id, czekajacy_return + czekajacy_heavy + czekajacy_vip + czekajacy_norm,
               czekajacy_return, czekajacy_heavy, czekajacy_vip, czekajacy_norm, T1_OCZEKIWANIE);
        
        //BUDZENIE PASAŻERÓW
        s_op(semid, SEM_TRAP_MUTEX, -1);
        
        if (sd->trap_wait_return > 0 && semctl(semid, SEM_TRAP_Q_RETURN, GETNCNT, 0) > 0) {
            s_op(semid, SEM_TRAP_Q_RETURN, 1);
            sd->trap_wait_return--;
        } 
        else if (sd->trap_wait_heavy > 0 && semctl(semid, SEM_TRAP_Q_HEAVY, GETNCNT, 0) > 0) {
            s_op(semid, SEM_TRAP_Q_HEAVY, 1);
            sd->trap_wait_heavy--;
        } 
        else if (sd->trap_wait_vip > 0 && semctl(semid, SEM_TRAP_Q_VIP, GETNCNT, 0) > 0) {
            s_op(semid, SEM_TRAP_Q_VIP, 1);
            sd->trap_wait_vip--;
        } 
        else if (sd->trap_wait_norm > 0 && semctl(semid, SEM_TRAP_Q_NORM, GETNCNT, 0) > 0) {
            s_op(semid, SEM_TRAP_Q_NORM, 1);
            sd->trap_wait_norm--;
        }
        
        s_op(semid, SEM_TRAP_MUTEX, 1);

        //OCZEKIWANIE NA CZAS T1 (LUB SYGNAŁ)
        struct timespec timeout = {T1_OCZEKIWANIE, 0};
        struct sembuf sb_timer = {SEM_TIMER_SIGNAL, -1, 0};
        
        bool time_up = false;
        while (!time_up) {
            int ret = semtimedop(semid, &sb_timer, 1, &timeout);
            
            if (ret == -1) {
                if (errno == EAGAIN || errno == ETIMEDOUT) {
                    time_up = true; //Czas T1 minął
                } else if (errno == EINTR) {
                    //Sprawdzenie czy to sygnał wcześniejszego wypłynięcia
                    if (force_departure) {
                        logger(C_M, "[PROM %d] SYGNAŁ 1! Wypływam wcześniej.", statek->id);
                        time_up = true;
                    }
                    continue;
                } else if (errno == EIDRM) {
                    goto cleanup; //Semafory usunięte
                } else {
                    time_up = true;
                }
            } else {
                time_up = true;
            }
        }
        
        //Sprawdzenie stanu załadunku
        int wolne = semctl(semid, SEM_FERRY_CAPACITY, GETVAL);
        int na_pokladzie = P_POJEMNOSC - wolne;
        
        logger(C_B, "[PROM %d] Czas T1 minął. Na pokładzie: %d/%d.", 
               statek->id, na_pokladzie, P_POJEMNOSC);

        //ZAMKNIĘCIE ZAŁADUNKU
        s_op(semid, SEM_TRAP_MUTEX, -1);
        sd->zaladunek_aktywny = false;

        int na_trapie = sd->trap_count;
        s_op(semid, SEM_TRAP_MUTEX, 1);

        if (na_trapie > 0) {
            logger(C_B, "[PROM %d] Zamknięty. Na trapie: %d - czekam...", 
                   statek->id, na_trapie);
        }

        //OCZEKIWANIE NA PUSTY TRAP
        while (1) {
            s_op(semid, SEM_TRAP_MUTEX, -1);
            int on_trap = sd->trap_count;
            s_op(semid, SEM_TRAP_MUTEX, 1);
            
            if (on_trap == 0) break;
            
            int ret = s_op_timed(semid, SEM_TRAP_EMPTY, -1, 1);
            if (ret == -2) goto cleanup;
        }

        sd->prom_w_porcie = false;
        int final_passengers = P_POJEMNOSC - semctl(semid, SEM_FERRY_CAPACITY, GETVAL);

        //OBSŁUGA PUSTEGO PROMU
        if (final_passengers == 0) {
            logger(C_Y, "[PROM %d] Brak pasażerów - prom wraca do kolejki.", statek->id);
            s_op(semid, SEM_FLOTA, 1);
            current_ship_idx = (current_ship_idx + 1) % N_FLOTA;
            continue;
        }

        s_op(semid, SEM_SYSTEM_MUTEX, -1);
        sd->stat_przeplyneli += final_passengers;
        s_op(semid, SEM_SYSTEM_MUTEX, 1);

        //REJS - UTWORZENIE PROCESU POTOMNEGO
        pid_t rejs_pid = fork();
        if (rejs_pid == -1) {
            logger(C_R, "[PROM %d] BŁĄD fork!", statek->id);
            s_op(semid, SEM_FLOTA, 1);
        } else if (rejs_pid == 0) {
            //Proces potomny - symulacja rejsu
            int moje_id = statek->id;
            logger(C_B, ">>> [PROM %d] ODPŁYWA (Pasażerów: %d). Rejs: %ds.", 
                   moje_id, final_passengers, Ti_REJS);
            
            //Czekanie Ti sekund
            struct sembuf sb_wait = {SEM_REJS_WAIT, -1, 0};
            struct timespec ts_rejs = {Ti_REJS, 0};
            semtimedop(semid, &sb_wait, 1, &ts_rejs);
            
            logger(C_B, "<<< [PROM %d] WRÓCIŁ do bazy.", moje_id);
            
            //Zwrot promu do puli
            s_op(semid, SEM_FLOTA, 1);
            exit(0);
        } else {
        statki_na_morzu++;
        }
        //Przejście do następnego promu
        current_ship_idx = (current_ship_idx + 1) % N_FLOTA;
    }

cleanup:
    //Zbieranie procesów potomnych - zapobieganie zombie
    while (wait(NULL) > 0);
    
    shmdt(sd);
    return 0;
}
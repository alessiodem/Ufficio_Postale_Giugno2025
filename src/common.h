#ifndef COMMON_H
#define COMMON_H

//Librerie
#define _GNU_SOURCE
#include <semaphore.h>
#include <time.h>

//IPC keys
#define KEY_TICKET_REQUEST_MGQ (0x11111111)
#define KEY_BREAK_MGQ (0x11111110)
#define KEY_TICKETS_TBE_MGQ (0x11111101)
#define KEY_SEAT_STATUS_MGQ (0x11111100)
#define KEY_SEAT_FREED_MGQ (0x11111011)
#define KEY_CLOCK_IN_MGQ (0x11111010)
#define KEY_WORKERS_SEAT_SEM (0x22222222)
#define KEY_USERS_SEAT_SEM (0x22222220)
#define KEY_SYNC_START_SEM (0x22222202)
#define KEY_SYNC_CHILDREN_START_SEM (0x22222200)
#define KEY_TICKETS_BUCKET_SEM (0x22222240)
#define KEY_SEATS_SHM (0x33333333)
#define KEY_CONFIG_SHM (0x33333330)
#define KEY_TICKETS_BUCKET_SHM (0x33333303)

//Macro
#define EXCLUSIVE_CREATE_FLAG (0666|IPC_CREAT|IPC_EXCL)
#define ENDEDDAY (SIGUSR1)
#define SECS_FOR_A_DAY (480 * config_shm_ptr->N_NANO_SECS / 1000000000)
#define NSECS_FOR_A_DAY ((480 * config_shm_ptr->N_NANO_SECS) % 1000000000)
#define LINE_BUFFER_SIZE (1024)
#define P_BREAK (2) //probabilità 1/P_BREAK



//TypeDefs
typedef enum {
    PACCHI,
    LETTERE,
    BANCOPOSTA,
    BOLLETTINI,
    PROD_FINANZIARI,
    OROLOGI,
    NUM_SERVIZI  // Usata per conoscere il numero totale di servizi tramite il suo indice
} ServiceType;

typedef struct {
    ServiceType service;
    int average_time;
} Service;

typedef struct {
    int ticket_index;                 // ID univoco del ticket
    int user_id;                   // ID dell'utente
    int service_type;             // Tipo di servizio richiesto (es. 0 = anagrafe, 1 = tributi, ecc.)
    double actual_deliver_time;
    int is_done; //old ver

    struct timespec request_time; // Quando l'utente ha richiesto il servizio
    struct timespec end_time;     // Quando il servizio è stato completato
    double time_taken;

    int day_number;               // Giorno della simulazione (1, 2, ..., SIM_DURATION)

    int seat_index;                  // ID dello sportello dove è stato servito (se servito)
    int operator_id;              // ID dell’operatore che ha servito (se servito)

}Ticket;

typedef struct {
    long mtype;// 1= ticket creato,2= richiesta
    pid_t requiring_user;
    ServiceType service_type;
    int ticket_index;
    struct timespec request_time;
}Ticket_request_message;


typedef struct{
    long mtype;// 1 erogato,2 richiesta
    int ticket_index;
}Ticket_tbe_message;

typedef struct {
    long mtype;
    pid_t worker;
} Break_message;

typedef struct {
    long mtype;
    int seat_index;
}Freed_seat_message;

typedef struct{
    long mtype;
    ServiceType service_type;
    pid_t worker_pid;
}Clock_in_message;


// Struttura completa dello sportello
typedef struct {
    ServiceType service_type; //servizio assegnato per la giornata
    int         worker_sem_id;       //id del semaforo che protegge l'accesso
    int         has_operator;        // 0 = seat libero, 1 = operatore presente
} Seat;


typedef struct{
    int  NOF_WORKERS;
    int NOF_USERS;
    int NOF_WORKER_SEATS;
    int SIM_DURATION;
    double P_SERV_MIN;
    double P_SERV_MAX;
    unsigned long  N_NANO_SECS;
    int NOF_PAUSE;
    int EXPLODE_THRESHOLD;
    int current_day;
}Config;

typedef struct {
    pid_t *seen_users_sim;
    int seen_users_sim_counter;
    int *total_ticket_served_per_day;
    int total_ticket_unserved;
    double *user_waiting_time_sum_per_day;
    double *service_serving_time_sum_per_day;
}Analytics_data;

#endif //COMMON_H

#ifndef COMMON_H
#define COMMON_H

//Librerie
#define _GNU_SOURCE
#include <semaphore.h>
#include <sys/types.h>
#include <time.h>

//IPC keys
#define KEY_TICKET_MGQ (0x11111111)
#define KEY_SEAT_STATUS_MGQ (0x11111110)
#define KEY_WORKERS_SEAT_SEM (0x22222222)
#define KEY_USERS_SEAT_SEM (0x22222220)
#define KEY_SYNC_START_SEM (0x22222200)
#define KEY_SYNC_CHILDREN_START_SEM (0x22222000)
#define KEY_SEATS_SHM (0x33333333)
#define KEY_CONFIG_SHM (0x33333330)

//Macro
#define EXCLUSIVE_CREATE_FLAG (0666|IPC_CREAT|IPC_EXCL)
#define ENDEDDAY (SIGUSR1)
#define SECS_FOR_A_DAY (1440 * config_shm_ptr->N_NANO_SECS / 1000000000)
#define NSECS_FOR_A_DAY ((1440 * config_shm_ptr->N_NANO_SECS) % 1000000000)
#define LINE_BUFFER_SIZE 1024
//TODO: #define P_BREAK


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

//todo: TIcket è stato cambiato rispetto all progetto x gennaio, andreanno cambiate un po' di robe nell'utilizzo
//todo: il cambiamento principale sta nel fatto che prima il ticket veniva usato dallo user, adesso viene mandato direttamente al worker
typedef struct {
    int ticket_id;                 // ID univoco del ticket
    int user_id;                   // ID dell'utente
    int service_type;             // Tipo di servizio richiesto (es. 0 = anagrafe, 1 = tributi, ecc.)

    struct timespec request_time; // Quando l'utente ha richiesto il servizio
    struct timespec start_time;   // Quando l’operatore ha iniziato a servire l’utente
    struct timespec end_time;     // Quando il servizio è stato completato

    int served;                   // 1 = servito, 0 = non servito
    int day_number;               // Giorno della simulazione (1, 2, ..., SIM_DURATION)

    int desk_id;                  // ID dello sportello dove è stato servito (se servito)
    int operator_id;              // ID dell’operatore che ha servito (se servito)

}Ticket;

typedef struct {
    long mtype;//0= richiesta, 1= ticket creato
    pid_t requiring_user;
    ServiceType service_type;
    Ticket ticket;
}Ticket_request_message;

//todo: ticket_emanation_message, Sembuf,


typedef struct {
    ServiceType service_type;
    int worker_sem_id;
    int user_sem_id;
    //ho rimosso ticket_emanation_msg_id perché l'operatore adesso ha già il ticket
}Seat;

typedef struct{
    int  NOF_WORKERS;
    int NOF_USERS;
    int NOF_WORKER_SEATS;
    int SIM_DURATION;
    double P_SERV_MIN;
    double P_SERV_MAX;
    int N_NANO_SECS;
    int NOF_PAUSE;
}Config;

//Analytic_tools non dovrebbe più essere necessario, forse va trovato un modo per contare le analytic: 12,13,14,15
//MA, se vogliamo contare un operatore attivo se ha erogato almeno un ticket basta aggiungere un attibuto nel ticket che dice quale operatore ha erogato quel ticket e 12, 13 e 15 vengono risolti in quel modo
//

#endif //COMMON_H

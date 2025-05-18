#ifndef COMMON_H
#define COMMON_H

//Librerie
#define _GNU_SOURCE
#include <semaphore.h>
#include <sys/types.h>
#include <time.h>

//IPC keys
#define KEY_TICKET_REQUEST_MGQ (0x11111111)
#define KEY_TICKETS_TBE_MGQ (0x11111101)
#define KEY_SEAT_STATUS_MGQ (0x11111100)
#define KEY_WORKERS_SEAT_SEM (0x22222222)
#define KEY_USERS_SEAT_SEM (0x22222220)
#define KEY_SYNC_START_SEM (0x22222202)
#define KEY_SYNC_CHILDREN_START_SEM (0x22222200)
#define KEY_SEATS_SHM (0x33333333)
#define KEY_CONFIG_SHM (0x33333330)
#define KEY_TICKETS_BUCKET_SHM (0x33333303)

//Macro
#define EXCLUSIVE_CREATE_FLAG (0666|IPC_CREAT|IPC_EXCL)
#define ENDEDDAY (SIGUSR1)
#define SECS_FOR_A_DAY (480 * config_shm_ptr->N_NANO_SECS / 1000000)//todo: per oea è gestito in micro ssecondi perchè altrimenti la iornata è troppo corta per qualche motivo
#define NSECS_FOR_A_DAY ((480 * config_shm_ptr->N_NANO_SECS) % 1000000)
#define LINE_BUFFER_SIZE (1024)
#define P_BREAK (10) //probabilità 1/10 todo: così è gestita in modo brutto, sistemare o nascondere questa macro



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

//todo: TIcket potrebbe venire stato cambiato rispetto all progetto x gennaio, andreanno cambiate un po' di robe nell'utilizzo se decidiamo di cambiarlo
//todo: il cambiamento principale starebbe nel fatto che prima il ticket veniva usato dallo user, adesso viene mandato direttamente al worker
typedef struct {
    int ticket_index;                 // ID univoco del ticket
    int user_id;                   // ID dell'utente
    int service_type;             // Tipo di servizio richiesto (es. 0 = anagrafe, 1 = tributi, ecc.)
    int actual_time;//old ver.
    int seat_index;//old ver.
    int is_done; //old ver

    struct timespec request_time; // Quando l'utente ha richiesto il servizio
    // struct timespec start_time;// Quando l’operatore ha iniziato a servire l’utente
    struct timespec end_time;     // Quando il servizio è stato completato

    int served;                   // 1 = servito, 0 = non servito
    int day_number;               // Giorno della simulazione (1, 2, ..., SIM_DURATION)

    int des_id;                  // ID dello sportello dove è stato servito (se servito)
    int operator_id;              // ID dell’operatore che ha servito (se servito)

}Ticket;
#define NO_TICKET_ATTRIBUTES 12

typedef struct {
    long mtype;// 1= ticket creato,2= richiesta
    pid_t requiring_user;
    ServiceType service_type;
    int ticket_index;
}Ticket_request_message;


typedef struct{
    long mtype;// 1 erogato,2 richiesta
    int ticket_index;
}Ticket_tbe_message;
//todo (se serve): Sembuf,



// Stato dello sportello 
typedef enum {
    SEAT_FREE     = 0,  //nessun operatore assegnato 
    SEAT_OCCUPIED = 1,  //operatore al lavoro
    SEAT_PAUSE    = 2   //operatore in pausa su questo sportello
} SeatState;

// Struttura completa dello sportello
typedef struct {
    ServiceType service_type; //servizio assegnato per la giornata
    pid_t       worker_pid;   //PID dell’operatore che lo occupa (0 se libero)
    SeatState   state;        //libero occupato pausa
    int         sem_id;       //id del semaforo che protegge l'accesso
} Seat;


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


#endif //COMMON_H

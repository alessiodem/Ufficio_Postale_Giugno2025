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
#define KEY_
#define KEY_SEATS_SHM (0x33333333)
#define KEY_CONFIG_SHM (0x33333330)
#define KEY_TICKETS_BUCKET_SHM (0x33333303)

//Macro
#define EXCLUSIVE_CREATE_FLAG (0666|IPC_CREAT|IPC_EXCL)
#define ENDEDDAY (SIGUSR1)
#define SECS_FOR_A_DAY (480 * config_shm_ptr->N_NANO_SECS / 1000000)//todo: per oea è gestito in micro ssecondi perchè altrimenti la iornata è troppo corta per qualche motivo
#define NSECS_FOR_A_DAY ((480 * config_shm_ptr->N_NANO_SECS) % 1000000)
#define LINE_BUFFER_SIZE (1024)
#define P_BREAK (1) //probabilità 1/10 todo: così è gestita in modo brutto, sistemare o nascondere questa macro
#define CONFIG_MAX_SEATS    64
#define CONFIG_MAX_WORKERS  64



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
    struct timespec actual_time;     // durata effettiva del servizio
    int is_done; //old ver

    struct timespec request_time; // Quando l'utente ha richiesto il servizio
    struct timespec end_time;     // Quando il servizio è stato completato
    struct timespec time_taken;      // tempo complessivo (end_time - request_time)

    int day_number;               // Giorno della simulazione (1, 2, ..., SIM_DURATION)

    int seat_index;                  // ID dello sportello dove è stato servito (se servito)
    int operator_id;              // ID dell’operatore che ha servito (se servito)

}Ticket;
#define NO_TICKET_ATTRIBUTES 12

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
//todo (se serve): Sembuf,




// Struttura completa dello sportello
typedef struct {
    ServiceType service_type; //servizio assegnato per la giornata
    int         worker_sem_id;       //id del semaforo che protegge l'accesso
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
#define CONFIG_QTY 8

//Analytic_tools non dovrebbe più essere necessario, forse va trovato un modo per contare le analytic: 12,13,14,15
//MA, se vogliamo contare un operatore attivo se ha erogato almeno un ticket basta aggiungere un attibuto nel ticket che dice quale operatore ha erogato quel ticket e 12, 13 e 15 vengono risolti in quel modo


#endif //COMMON_H

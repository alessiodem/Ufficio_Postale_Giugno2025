#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include "common.h"
#include "../lib/sem_handling.h"
#include "../lib/utils.h"
#include <sched.h>

//todo: da spostare
#ifndef KEY_BREAK_MGQ
#define KEY_BREAK_MGQ 0x11111110
#endif

typedef struct { long mtype; pid_t worker; } BreakMsg;
int break_mgq_id = -1;

int children_ready_sync_sem_id;
int children_go_sync_sem_id;
sigjmp_buf jump_buffer;
Seat *seats_shm_ptr;
Config *config_shm_ptr;
Ticket *tickets_bucket_shm_ptr;
int ticket_request_msg_id;
int tickets_tbe_mgq_id;//tbe= to be erogated
int current_seat_index;
int aviable_breaks;
int in_break = 0;
int day_passed=0;

ServiceType service_type;

//FUNZIONI DI SETUP
void handle_sig(int sig) {
    if (sig == ENDEDDAY) {
        printf("[DEBUG] Utente %d: Ricevuto segnale di fine giornata\n", getpid());
        day_passed++;
        if (current_seat_index >= 0) {
            semaphore_increment(seats_shm_ptr[current_seat_index].worker_sem_id);
            current_seat_index = -1;
        }

        siglongjmp(jump_buffer, 1);

    }else if (sig== SIGTERM) {
        printf("[DEBUG] Utente %d: Ricevuto SIGTERM, termino.\n", getpid());
        shmdt(config_shm_ptr);
        shmdt(seats_shm_ptr);
        shmdt(tickets_bucket_shm_ptr);
        exit(0);
    }
}
void setup_sigaction(){
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Errore sigaction SIGTERM");
        exit(EXIT_FAILURE);
    }

    if (sigaction(ENDEDDAY, &sa, NULL) == -1) {
        perror("Errore sigaction ENDEDDAY");
        exit(EXIT_FAILURE);
    }
}
void setup_ipcs() {
    printf("[DEBUG] Utente %d: Inizializzazione IPC\n", getpid());

    if ((children_ready_sync_sem_id = semget(KEY_SYNC_START_SEM, 1, 0666)) == -1) {
        perror("Errore semget KEY_SYNC_START_SEM");
        exit(EXIT_FAILURE);
    }

    if ((children_go_sync_sem_id = semget(KEY_SYNC_CHILDREN_START_SEM, 1, 0666)) == -1) {
        perror("Errore semget KEY_SYNC_CHILDREN_START_SEM");
        exit(EXIT_FAILURE);
    }

    int config_shm_id = shmget(KEY_CONFIG_SHM, sizeof(Config), 0666);
    if (config_shm_id == -1) {
        perror("Errore nella creazione della memoria condivisa per la configurazione");
        exit(EXIT_FAILURE);
    }
    config_shm_ptr = shmat(config_shm_id, NULL, 0);
    if (config_shm_ptr == (void *)-1) {
        perror("Errore nella memoria condivisa per la configurazione");
        exit(EXIT_FAILURE);
    }
    if ((ticket_request_msg_id = msgget(KEY_TICKET_REQUEST_MGQ, 0666)) == -1) {
        perror("Errore msgget KEY_TICKET_REQUEST_MGQ");
        exit(EXIT_FAILURE);
    }
    tickets_tbe_mgq_id = msgget(KEY_TICKETS_TBE_MGQ, 0666);
    if (tickets_tbe_mgq_id == -1) {
        perror("[ERROR] msgget() per ticket_tbe_mgq_id fallito");
        exit(EXIT_FAILURE);
    }
//inizializzare nel manager
    //l worker cerca di collegarsi alla message queue per le pause
    break_mgq_id = msgget(KEY_BREAK_MGQ, 0666);
    if (break_mgq_id == -1) {
        perror("[ERROR] msgget() break_mgq_id fallito");
        //la simulazione continua, ma le pause non verranno conteggiate
    }
    int seats_shm_id = shmget(KEY_SEATS_SHM, sizeof(Seat) * config_shm_ptr->NOF_WORKER_SEATS, 0666);
    if (seats_shm_id == -1) {
        perror("[ERROR] shmget() per seats_shm fallito");
        exit(EXIT_FAILURE);
    }
    seats_shm_ptr = shmat(seats_shm_id, NULL, 0);
    if (seats_shm_ptr == (void *)-1) {
        perror("[ERROR] shmat() per seats_shm fallito");
        exit(EXIT_FAILURE);
    }
    int tickets_bucket_id = shmget(KEY_TICKETS_BUCKET_SHM, sizeof(Ticket) * config_shm_ptr->NOF_USERS*config_shm_ptr->SIM_DURATION, 0666);
    if (tickets_bucket_id == -1) {
        perror("[ERROR] shmget() per seats_shm fallito");
        exit(EXIT_FAILURE);
    }
    tickets_bucket_shm_ptr = shmat(tickets_bucket_id, NULL, 0);
    if (tickets_bucket_shm_ptr == (void *)-1) {
        perror("[ERROR] shmat() per seats_shm fallito");
        exit(EXIT_FAILURE);
    }

    printf("[DEBUG] Utente %d: IPC inizializzati con successo\n", getpid());
}
//FUNZIONI DI DEBUG
#include <time.h>
void print_ticket(Ticket ticket) {
    printf("\n========== TICKET INFO ==========\n");
    printf("🆔 Ticket Index      : %d\n", ticket.ticket_index);
    printf("👤 User ID           : %d\n", ticket.user_id);
    printf("🔧 Service Type      : %d\n", ticket.service_type);
    printf("📅 Day Number        : %d\n", ticket.day_number);

    printf("⏰ Request Time      : %ld.%09ld\n", ticket.request_time.tv_sec, ticket.request_time.tv_nsec);
    printf("⏱️  End Time          : %ld.%09ld\n", ticket.end_time.tv_sec, ticket.end_time.tv_nsec);
    printf("⏱️️️⏱️⏱️  Time taken          : %f\n", ticket.time_taken);
//todo: se riusciamno gestire in modo migliore  tempi (dargli il tempo in secondi e nanosecondi)
    if (ticket.end_time.tv_sec != 0 || ticket.end_time.tv_nsec != 0) {
        printf("🏢 Desk index           : %d\n", ticket.seat_index);
        printf("👨‍💼 Operator ID      : %d\n", ticket.operator_id);
    }

    // Campi old version (se ancora rilevanti per debug)
    printf("🕒  Actual Time   : %d\n", ticket.actual_time);
    printf("📍  Seat Index    : %d\n", ticket.seat_index);
    printf("✔️  Is_done       : %d\n", ticket.is_done);

    printf("==================================\n");
}


//FUNZIONI DI FLOW PRINCIPALE
void set_ready() {
    printf("[DEBUG] Operatore %d: Sono pronto per la nuova giornata\n", getpid());
    semaphore_increment(children_ready_sync_sem_id);
    semaphore_do(children_go_sync_sem_id, 0);
    printf("[DEBUG] Operatore %d: Sto iniziando una nuova giornata\n", getpid());
}
void go_on_break() {

    printf("[DEBUG] Operatore %d: Vado in pausa. Pause rimanenti: %d\n", getpid(), aviable_breaks);

    //Notifica al direttore la pausa, se la coda è disponibile
    if (break_mgq_id != -1) {
        BreakMsg bm = { .mtype = 1, .worker = getpid() };
        if (msgsnd(break_mgq_id, &bm, sizeof(pid_t), IPC_NOWAIT) == -1)//todo: testare il funzionamento di IPC_NOWAIT in casi estremi
            perror("[WARN] msgsnd break_mgq");
    }

    //Libera lo sportello occupato, rendendolo disponibile
    if (current_seat_index >= 0) {
        semaphore_increment(seats_shm_ptr[current_seat_index].worker_sem_id);
        current_seat_index = -1;
    }

    //Attende la fine della giornata (ENDEDDAY) o un SIGTERM
    pause();
}
int main () {
    setup_sigaction();
    setup_ipcs();
    available_breaks = config_shm_ptr->NOF_PAUSE;
    service_type = get_random_service_type();
    current_seat_index = -1;
    sigsetjmp(jump_buffer, 1);

    set_ready();

    while (1) {//questo endless loop serve per consentirgli di continuare a cercare un seat libero, è inefficente todo: se vogliamo farlo bene va sostituito con un sistema di segnali quando un seat si libera

        for ( int i=0 ; i<config_shm_ptr->NOF_WORKER_SEATS; i++) {
            if (seats_shm_ptr[i].service_type == service_type && semaphore_do_not_wait(seats_shm_ptr[i].worker_sem_id, -1) == 0) {
                printf("[DEBUG] Operatore %d: Trovato posto libero %d\n", getpid(), i);
                current_seat_index = i;

                //EROGAZIONE SERVIZIO
                while (1){
                    printf("[DEBUG] Operatore %d: In attesa di ticket da erogare del mio tipo di servizio \n", getpid());

                    Ticket_tbe_message ttbemsg;
                    msgrcv(tickets_tbe_mgq_id, &ttbemsg,sizeof(ttbemsg)-sizeof(long),service_type+1,0);

                    //todo: rivedere bene lo storico dei commit su questo codice
                    printf("[DEBUG] Operatore %d: Inizio servizio, durata: %d\n", getpid(), tickets_bucket_shm_ptr[ttbemsg.ticket_index].actual_time);
                    sleep(tickets_bucket_shm_ptr[ttbemsg.ticket_index].actual_time);
                    clock_gettime(CLOCK_MONOTONIC,&tickets_bucket_shm_ptr[ttbemsg.ticket_index].end_time);
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].is_done = 1;
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].time_taken =tickets_bucket_shm_ptr[ttbemsg.ticket_index].end_time.tv_sec - tickets_bucket_shm_ptr[ttbemsg.ticket_index].request_time.tv_sec+tickets_bucket_shm_ptr[ttbemsg.ticket_index].end_time.tv_nsec - tickets_bucket_shm_ptr[ttbemsg.ticket_index].request_time.tv_nsec / 1e9 ;
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].operator_id=getpid();
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].day_number=day_passed;
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].seat_index=i;

                    printf("[DEBUG] Operatore %d: Servizio completato\n", getpid());
                    print_ticket(tickets_bucket_shm_ptr[ttbemsg.ticket_index]);

                    //DECIDE SE ANDARE IN PAUSA
                    if (available_breaks > 0) {

                        //if ( P_BREAK > 0 && rand() % P_BREAK == 0 ) {
                        if ( P_BREAK > 0 ) {
                            --available_breaks;
                            printf("[DEBUG] Operatore %d: Vado in pausa. Pause rimanenti: %d\n", getpid(), available_breaks);
                            go_on_break();
                        }

                    }
                    printf("[DEBUG] Operatore %d: NON vado in pausa\n", getpid());
                }
            }
        }
        //printf("[Operatore] non ho trovato uno sportello disponibile, aspetto\n");
        sched_yield(); // cede la CPU ad altri processi pronti se ha ciclato fino all'ultimo seat e non ha trovato dove sedersi (non è al 100% efficiente e sensato ma dovrebbe funzionare)
    }
    printf("[DEBUG] Operatore %d: Processo terminato in modo inaspettato\n", getpid());
    return 0;

}
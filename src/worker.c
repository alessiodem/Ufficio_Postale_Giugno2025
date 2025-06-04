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
int break_mgq_id = -1;
int children_ready_sync_sem_id;
int children_go_sync_sem_id;
sigjmp_buf jump_buffer;
Seat *seats_shm_ptr;
Config *config_shm_ptr;
Ticket *tickets_bucket_shm_ptr;
int seat_freed_mgq_id;
int ticket_request_msg_id;
int tickets_tbe_mgq_id;//tbe= to be erogated
int tickets_bucket_sem_id;
int current_seat_index;
int available_breaks;
int in_break = 0;
int day_passed=0;

ServiceType service_type;

//FUNZIONI DI SETUP
void handle_sig(int sig) {
    if (sig == ENDEDDAY) {
        //printf("[DEBUG] Utente %d: Ricevuto segnale di fine giornata\n", getpid());
        day_passed++;
        if (current_seat_index >= 0) {
            semaphore_increment(seats_shm_ptr[current_seat_index].worker_sem_id);
            current_seat_index = -1;
        }

        siglongjmp(jump_buffer, 1);

    }else if (sig== SIGTERM) {
        //printf("[DEBUG] Utente %d: Ricevuto SIGTERM, termino.\n", getpid());
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
    //printf("[DEBUG] Utente %d: Inizializzazione IPC\n", getpid());

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
    break_mgq_id = msgget(KEY_BREAK_MGQ, 0666);
    if (break_mgq_id == -1) {
        perror("[WARN] msgget() break_mgq_id fallito");
    }
    seat_freed_mgq_id = msgget(KEY_SEAT_FREED_MGQ, 0666);
    if (seat_freed_mgq_id == -1) {
        perror("[WARN] msgget() seat_freed_mgq_id fallito");
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
    tickets_bucket_sem_id = semget(KEY_TICKETS_BUCKET_SEM, 1, 0666);
    if (tickets_bucket_sem_id == -1) {
        perror("[ERROR] semget() per tickets_bucket_sem_id fallito");
        exit(EXIT_FAILURE);
    }

    //printf("[DEBUG] Utente %d: IPC inizializzati con successo\n", getpid());
}
//FUNZIONI DI DEBUG
#include <time.h>
void print_ticket(Ticket ticket) {
    printf("\n========== TICKET INFO ==========\n");
    printf("-- Indice Ticket                                : %d\n", ticket.ticket_index);
    printf("-- Tipo Di Servizio                             : %d\n", ticket.service_type);
    printf("-- Giornata                                     : %d\n", ticket.day_number);

    if (ticket.end_time.tv_sec != 0 && ticket.end_time.tv_nsec != 0) {
        printf("\n-- Indice Sportello                             : %d\n", ticket.seat_index);
        printf("-- ID Operatore                                 : %d\n", ticket.operator_id);
    }
    printf("-- ID Utente                                    : %d\n", ticket.user_id);

    printf("\n-- Tempo Erogazione Del Servizio                : %f\n", ticket.actual_deliver_time);
    printf("-- Tempo Reale Per L'Erogazione Del Servizio    : %f secondi\n", ticket.actual_deliver_time*config_shm_ptr->N_NANO_SECS/1000000000);
    printf("-- Tempo Impiegato Dall'Utente                  : %f\n", ticket.time_taken);
    printf("\n\n--  Is Done                                     : %s\n", ticket.is_done? "YES" : "NO");

    printf("==================================\n");
}


//FUNZIONI DI FLOW PRINCIPALE
void set_ready() {
    //printf("[DEBUG] Operatore %d: Sono pronto per la nuova giornata\n", getpid());
    semaphore_increment(children_ready_sync_sem_id);
    semaphore_do(children_go_sync_sem_id, 0);
    //printf("[DEBUG] Operatore %d: Sto iniziando una nuova giornata\n", getpid());
}
void go_on_break() {

    printf("[DEBUG] Operatore %d: Vado in pausa. Pause rimanenti: %d\n", getpid(), available_breaks);

    seats_shm_ptr[current_seat_index].has_operator = 0;
    semaphore_increment(seats_shm_ptr[current_seat_index].worker_sem_id);

    if (break_mgq_id != -1) {
        Break_message bm = { .mtype = 1, .worker = getpid() };
        if (msgsnd(break_mgq_id, &bm, sizeof(pid_t), IPC_NOWAIT) == -1)
            perror("[WARN] msgsnd break_mgq");
    }
    if (seat_freed_mgq_id != -1) {
        Freed_seat_message fsm = { .mtype = service_type+1, .seat_index = current_seat_index };
        if (msgsnd(break_mgq_id, &fsm, sizeof(fsm)- sizeof(fsm.mtype), IPC_NOWAIT) == -1)
            perror("[WARN] msgsnd seat_freed_mgq");
    }

    if (current_seat_index >= 0) {
        current_seat_index = -1;
        pause(); // aspetta la fine della giornata ENDEDDAY o SIGTERM
    }
}
int main () {
    srand(time(NULL)*getpid());
    setup_sigaction();
    setup_ipcs();
    available_breaks = config_shm_ptr->NOF_PAUSE;
    service_type = get_random_service_type();
    printf("[DEBUG] Operatore %d: Posso erogare il servizio: %d \n", getpid(),service_type);
    current_seat_index = -1;
    int seat_finder_index;
    sigsetjmp(jump_buffer, 1);
    seat_finder_index=0;
    set_ready();

    while (1) {
        //questo endless loop serve per consentirgli di continuare a cercare un seat libero, è inefficente todo: è molto inefficiente, se vogliamo farlo bene va sostituito con un sistema di segnali quando un seat si libera

        while ( seat_finder_index<config_shm_ptr->NOF_WORKER_SEATS) {
            if (seats_shm_ptr[seat_finder_index].service_type == service_type && semaphore_do_not_wait(seats_shm_ptr[seat_finder_index].worker_sem_id, -1) == 0) {
                printf("[DEBUG] Operatore %d: Trovato posto libero %d\n", getpid(), seat_finder_index);
                current_seat_index = seat_finder_index;
                seats_shm_ptr[seat_finder_index].has_operator = 1;

                //EROGAZIONE SERVIZIO
                while (1){
                    printf("[DEBUG] Operatore %d: In attesa di ticket da erogare del mio tipo di servizio \n", getpid());

                    Ticket_tbe_message ttbemsg;
                    msgrcv(tickets_tbe_mgq_id, &ttbemsg,sizeof(ttbemsg)-sizeof(long),service_type+1,0);

                    printf("[DEBUG] Operatore %d: Inizio servizio, durata: %f\n", getpid(), tickets_bucket_shm_ptr[ttbemsg.ticket_index].actual_deliver_time);
                    semaphore_decrement(tickets_bucket_sem_id);
                    struct timespec erogation_time = {
                        .tv_sec = (tickets_bucket_shm_ptr[ttbemsg.ticket_index].actual_deliver_time * config_shm_ptr->N_NANO_SECS) / 1000000000,
                        .tv_nsec = (int)(tickets_bucket_shm_ptr[ttbemsg.ticket_index].actual_deliver_time * config_shm_ptr->N_NANO_SECS) % 1000000000
                    };
                    semaphore_increment(tickets_bucket_sem_id);

                    nanosleep(&erogation_time,NULL);

                    struct timespec end_ts;
                    clock_gettime(CLOCK_REALTIME, &end_ts);

                    //Sezione critica: aggiornamento del ticket
                    semaphore_decrement(tickets_bucket_sem_id);
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].end_time = end_ts;

                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].time_taken =
                        (double)(end_ts.tv_sec - tickets_bucket_shm_ptr[ttbemsg.ticket_index].request_time.tv_sec) +
                        (double)(end_ts.tv_nsec - tickets_bucket_shm_ptr[ttbemsg.ticket_index].request_time.tv_nsec) / 1e9;

                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].operator_id = getpid();
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].day_number = day_passed;
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].seat_index = seat_finder_index;
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].is_done = 1;
                    semaphore_increment(tickets_bucket_sem_id);

                    printf("[DEBUG] Operatore %d: Servizio completato\n", getpid());
                    semaphore_decrement(tickets_bucket_sem_id);
                    Ticket ticket_tobeprint=tickets_bucket_shm_ptr[ttbemsg.ticket_index]; //ho scomposto il salvataggio e la print per risparmiare tempo passato in sezione critica
                    semaphore_increment(tickets_bucket_sem_id);
                    print_ticket(ticket_tobeprint);

                    //DECIDE SE ANDARE IN PAUSA
                    if (available_breaks > 0) {

                        if (P_BREAK<=1 || rand() % P_BREAK == 0 ) {
                            available_breaks--;
                            go_on_break();
                        }

                    }
                    printf("[DEBUG] Operatore %d: NON vado in pausa\n", getpid());
                }
            }
            seat_finder_index++;
        }
        //printf("[Operatore] non ho trovato uno sportello disponibile, aspetto\n");
        Freed_seat_message sfm;
        msgrcv(seat_freed_mgq_id,&sfm,sizeof(sfm), service_type+1,0);
        seat_finder_index=sfm.mtype-1;

    }
    printf("[DEBUG] Operatore %d: Processo terminato in modo inaspettato\n", getpid());
    return -1;

}
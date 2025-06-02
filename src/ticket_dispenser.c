#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/shm.h>
#include <time.h>          /* struct timespec */

#include "common.h"
#include "../lib/sem_handling.h"
#include "../lib/utils.h"

//VARIABILI GLOBALI
int children_ready_sync_sem_id;
int children_go_sync_sem_id;
sigjmp_buf jump_buffer;

int ticket_request_mgq_id;
int tickets_tbe_mgq_id;//tbe= to be erogated
int tickets_bucket_sem_id;

int ticket_index = 0;
int seat_finder_index=0;
Ticket *tickets_bucket_shm_ptr;
Seat *seats_shm_ptr;
Config *config_shm_ptr;

//SETUP FUNCTIONS
//todo: to test
void handle_sig(int sig) {
    if (sig == ENDEDDAY) {
        printf("[DEBUG] Ticket Dispenser: Ricevuto segnale di fine giornata\n");
        // pulire risorse

        // se serve terminare in modo pulito le risorse posso farlo qui

        //segnala al manager che la giornata è conclusa
        semaphore_increment(children_ready_sync_sem_id);

       siglongjmp(jump_buffer, 1);
    }
    else if (sig == SIGTERM) {
        //todo: potrebbe esserci bisogno id altro(come fflush(stdout), non mi sebra serva ma se qualcosa non funziona potrebbe essere per la sua assenza
        shmdt(config_shm_ptr);
        shmdt(seats_shm_ptr);
        shmdt(tickets_bucket_shm_ptr);
        printf("[DEBUG] Utente %d: Ricevuto SIGTERM, termino.\n", getpid());
        exit(0);
    }
}
void initSigAction() {
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);  // Nessun segnale bloccato durante l'esecuzione dell'handler
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
    printf("[DEBUG] Inizializzazione IPC\n");
    children_ready_sync_sem_id = semget(KEY_SYNC_START_SEM, 1, 0666);
    if (children_ready_sync_sem_id == -1) {
        perror("[ERROR] semget() per children_ready_sync_sem_id fallito");
        exit(EXIT_FAILURE);
    }

    children_go_sync_sem_id = semget(KEY_SYNC_CHILDREN_START_SEM, 1, 0666);
    if (children_go_sync_sem_id == -1) {
        perror("[ERROR] semget() per children_go_sync_sem_id fallito");
        exit(EXIT_FAILURE);
    }
    ticket_request_mgq_id = msgget(KEY_TICKET_REQUEST_MGQ, 0666);
    if (ticket_request_mgq_id == -1) {
        perror("[ERROR] msgget() per ticket_msg_id fallito");
        exit(EXIT_FAILURE);
    }
    tickets_tbe_mgq_id = msgget(KEY_TICKETS_TBE_MGQ, 0666);
    if (tickets_tbe_mgq_id == -1) {
        perror("[ERROR] msgget() per ticket_tbe_mgq_id fallito");
        exit(EXIT_FAILURE);
    }
    //todo: controllare se config_shm_id e  seats_shm_id sono necessari (scusa per il todo studido, sono sotanco)
    int config_shm_id = shmget(KEY_CONFIG_SHM, sizeof(Config), 0666);
    if (config_shm_id == -1) {
        perror("[ERROR] shmget() per config_shm fallito");
        exit(EXIT_FAILURE);
    }
    config_shm_ptr = shmat(config_shm_id, NULL, 0);
    if (config_shm_ptr == (void *)-1) {
        perror("[ERROR] shmat() per config_shm fallito");
        exit(EXIT_FAILURE);
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
        perror("[ERROR] shmget() per tickets_bucket_shm fallito");
        exit(EXIT_FAILURE);
    }
    tickets_bucket_shm_ptr = shmat(tickets_bucket_id, NULL, 0);
    if (tickets_bucket_shm_ptr == (void *)-1) {
        perror("[ERROR] shmat() per tickets_bucket_shm fallito");
        exit(EXIT_FAILURE);
    }
    tickets_bucket_sem_id = semget(KEY_TICKETS_BUCKET_SEM, 1, 0666);
    if (tickets_bucket_sem_id == -1) {
        perror("[ERROR] semget() per tickets_bucket_sem_id fallito");
        exit(EXIT_FAILURE);
    }
}

//FUNZIONI DI FLOW PRINCIPALE
void set_ready() {
    printf("[DEBUG] Utente %d: Sono pronto per la nuova giornata\n", getpid());
    semaphore_increment(children_ready_sync_sem_id);
    semaphore_do(children_go_sync_sem_id, 0);
    printf("[DEBUG] Utente %d: Sto iniziando una nuova giornata\n", getpid());
}
double generate_random_time(int average_time) {
    printf("[DEBUG] Ticket Dispenser: Generazione tempo casuale. Media: %d\n", average_time);
    double max_variation = average_time / 2;
    double variation = (rand() % (int)(2 * max_variation + 1)) - max_variation;
    double actual_time = average_time + variation;
    printf("[DEBUG] Ticket Dispenser: Tempo generato: %f\n", actual_time);
    return actual_time;
}

//todo: potremmo trovare un modo migliore rispetto a questo
int find_a_seat_index(ServiceType service_type) {
    printf("[DEBUG] Ticket Dispenser: Ricerca posto per servizio tipo %d\n", service_type);
    int attempts= 0;
    while (attempts<config_shm_ptr->NOF_WORKER_SEATS) {
        seat_finder_index = seat_finder_index % config_shm_ptr->NOF_WORKER_SEATS;
        if (seats_shm_ptr[seat_finder_index].service_type == service_type) {
            printf("[DEBUG] Ticket Dispenser: Trovato posto all'indice %d\n", seat_finder_index);
            seat_finder_index++;
            return seat_finder_index-1;
        }
        seat_finder_index++;
        attempts++;
        printf("[DEBUG] Ticket Dispenser: Spostamento all'indice %d\n", seat_finder_index);
    }
    // non è possibile che non lo trovi perché viene fatto prima un controllo da parte dell'utente
    perror("[TD_ERROR] Terminazione inaspettata, non è stato trovato un posto per il servizio dal ticket_dispenser: controllo precedente fallito");
    exit(EXIT_FAILURE);
}


Ticket generate_ticket(ServiceType service_type, int ticket_number, pid_t requiring_user_pid, struct timespec request_time) {
    printf("[DEBUG] Ticket Dispenser: Generazione ticket %d per servizio tipo %d\n", ticket_number, service_type);

    int average_time = get_average_time(service_type);

    /* converti il tempo medio generato (in secondi) in struct timespec */
    double actual_ts = generate_random_time(average_time);

    Ticket ticket = {
        .ticket_index = ticket_number,
        .service_type = service_type,
        .actual_time  = actual_ts,
        .is_done      = 0,
        .user_id      = requiring_user_pid,
        .request_time = request_time
    };

    printf("[DEBUG] Ticket Dispenser: Ticket generato - Numero: %d, Tempo reale: %f\n",
           ticket.ticket_index, (ticket.actual_time*config_shm_ptr->N_NANO_SECS)/1000000000);
    return ticket;
}

//MAIN

int main(int argc, char *argv[]) {
    srand(time(NULL));
    initSigAction();
    printf("[DEBUG] Ticket Dispenser: Avvio processo\n");
    setup_ipcs();
    sigsetjmp(jump_buffer, 1);
    Ticket_request_message tmsg;

    set_ready();
    while (1) {
        printf("[DEBUG] Ticket Dispenser: In attesa di richiesta ticket\n");
        msgrcv(ticket_request_mgq_id, &tmsg, sizeof(tmsg) - sizeof(long), 2, 0);

        printf("[DEBUG] Ticket Dispenser: Ricevuta richiesta da utente %d per servizio tipo %d\n", tmsg.requiring_user, tmsg.service_type);
        //todo: il codice qui sarebbe più indicato sotto ma il generate ticket ma spostato all'interno di ttbemsg e conseguenti modifiche
        Ticket ticket = generate_ticket(tmsg.service_type, ticket_index, tmsg.requiring_user,tmsg.request_time);
        tmsg.mtype = ticket.user_id;
        //Scrittura protetta sul bucket dei ticket
        semaphore_decrement(tickets_bucket_sem_id);
        tickets_bucket_shm_ptr[ticket.ticket_index] = ticket;
        semaphore_increment(tickets_bucket_sem_id);
        ticket_index++;

        Ticket_tbe_message ttbemsg;
        ttbemsg.ticket_index=ticket.ticket_index;
        ttbemsg.mtype=ticket.service_type+1;//+1 perché m_type non può essere 0 ed esistre un service_type=0
        printf("[DEBUG] Ticket Dispenser: Invio ticket %d alla coda di tickets da erogare \n",ticket.ticket_index);
        if (msgsnd(tickets_tbe_mgq_id, &ttbemsg, sizeof(ttbemsg) - sizeof(long), 0)==-1) {
            perror("[TD_ERROR] invio ticket da erogare fallito");
        }

        printf("[DEBUG] Ticket Dispenser: Invio ticket %d all'utente %d\n", ticket.ticket_index, tmsg.requiring_user);
        if (msgsnd(ticket_request_mgq_id, &tmsg, sizeof(tmsg) - sizeof(long), 0)==-1) {
            perror("[TD_ERROR] invio ticket all'utente fallito");
        }
        printf("[DEBUG] Ticket Dispenser: Ticket inviato con successo\n");
    }

    printf("[DEBUG] Ticket Dispenser %d: Processo terminato in modo inaspettato\n", getpid());
    return 0;
}
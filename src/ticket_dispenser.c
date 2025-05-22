#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/shm.h>

#include "common.h"
#include "../lib/sem_handling.h"
#include "../lib/utils.h"

//VARIABILI GLOBALI
int children_ready_sync_sem_id;
int children_go_sync_sem_id;
sigjmp_buf jump_buffer;

int ticket_request_mgq_id;
int tickets_tbe_mgq_id;//tbe= to be erogated

int ticket_index = 0;
int seat_finder_index=0;
Ticket *tickets_bucket_shm_ptr;
Seat *seats_shm_ptr;
Config *config_shm_ptr;

//SETUP FUNCTIONS
void handle_sig(int sig) {
    if (sig == ENDEDDAY) {
        printf("[DEBUG] Ticket Dispenser: Ricevuto segnale di fine giornata\n");

        siglongjmp(jump_buffer, 1);  // Salta all'inizio del ciclo
    }
    else if (sig == SIGTERM) {
        shmdt(config_shm_ptr);
        shmdt(seats_shm_ptr);
        shmdt(tickets_bucket_shm_ptr);
        printf("[DEBUG] Utente %d: Ricevuto SIGTERM, termino.\n", getpid());
        exit(EXIT_SUCCESS);
    }
}
void initSigAction() {
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
}

//FUNZIONI DI FLOW PRINCIPALE
void set_ready() {
    printf("[DEBUG] Utente %d: Sono pronto per la nuova giornata\n", getpid());
    semaphore_increment(children_ready_sync_sem_id);
    semaphore_do(children_go_sync_sem_id, 0);
    printf("[DEBUG] Utente %d: Sto iniziando una nuova giornata\n", getpid());
}
int generate_random_time(int average_time) {
    printf("[DEBUG] Ticket Dispenser: Generazione tempo casuale. Media: %d\n", average_time);
    int max_variation = average_time / 2;
    int variation = (rand() % (2 * max_variation + 1)) - max_variation;
    int actual_time = average_time + variation;
    int result = actual_time > 0 ? actual_time : 1;
    printf("[DEBUG] Ticket Dispenser: Tempo generato: %d\n", result);
    return result;
}

//todo: potremmo trovare un modo migliore rispetto a questo (testare se funziona bene,basta che funziona si può lasciare, dubito guarderanno questa parte di codice ed anche se lo facessero ha senso come gestione, è solo un po' inusuale (credo(spero(plz god be gentile))))
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
    Ticket ticket = {
        .ticket_index = ticket_number,
        .service_type = service_type,
        .actual_time = generate_random_time(average_time),//todo:  queste funzioni potrebbero dare il problema di eccessiva roba nello stack di chiamate, capire se effettivamente è un problema
        .is_done = 0,
        .user_id =requiring_user_pid,
        .request_time =request_time,
        .end_time = {0,0}
    };

    printf("[DEBUG] Ticket Dispenser: Ticket generato - Numero: %d, Tempo: %d\n",
           ticket.ticket_index, ticket.actual_time);
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

        Ticket ticket = generate_ticket(tmsg.service_type, ticket_index, tmsg.requiring_user,tmsg.request_time);
        tmsg.mtype = ticket.user_id;
        tickets_bucket_shm_ptr[ticket.ticket_index]=ticket;
        ticket_index++;

        Ticket_tbe_message ttbemsg;
        ttbemsg.ticket_index=ticket.ticket_index;
        ttbemsg.mtype=ticket.service_type+1;//+1 perché m_type non può essere 0 ed esiste un service_type==0
        printf("[DEBUG] Ticket Dispenser: Invio ticket %d alla coda di tickets da erogare \n",ticket.ticket_index);
        if (msgsnd(tickets_tbe_mgq_id, &ttbemsg, sizeof(ttbemsg) - sizeof(long), 0)==-1) {
            perror("[TD_ERROR] invio ticket da erogare fallito");
        }

        printf("[DEBUG] Ticket Dispenser: Invio ticket %d all'utente %d \n", ticket.ticket_index, tmsg.requiring_user);
        if (msgsnd(ticket_request_mgq_id, &tmsg, sizeof(tmsg) - sizeof(long), 0)==-1) {
            perror("[TD_ERROR] invio ticket all'utente fallito");
        }
        printf("[DEBUG] Ticket Dispenser: Ticket inviato con successo\n");
    }

    printf("[DEBUG] Ticket Dispenser %d: Processo terminato in modo inaspettato\n", getpid());
    return 0;
}
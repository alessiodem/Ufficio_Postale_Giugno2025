#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <unistd.h>
#include <bits/signum-generic.h>
#include <sys/shm.h>

#include "common.h"
#include "sem_handling.h"
#include "utils.h"

//VARIABILI GLOBALI
int children_ready_sync_sem_id;
int children_go_sync_sem_id;
sigjmp_buf jump_buffer;

int ticket_request_mgq_id;
int ticket_emanation_mgq_id;
Ticket_request_message tmsg;
int ticket_index = 0;
//todo: testare se la linea sotto è corretta (sono stanco scusa del todo stupido)
int seat_finder_index=0;
Seat *seats_shm_ptr;
Config *config_shm_ptr;

//SETUP FUNCTIONS
//todo: to test
void handle_sig(int sig) {
    if (sig == ENDEDDAY) {
        printf("[DEBUG] Ticket Dispenser: Ricevuto segnale di fine giornata\n");
        // pulire risorse

        // se serve terminare in modo pulito le risorse posso farlo qui

        // rimettersi in ready
        siglongjmp(jump_buffer, 1);  // Salta all'inizio del ciclo
    }
    else if (sig == SIGTERM) {
        //todo: potrebbe esserci bisogno id altro(come fflush(stdout), da vedere bene
        printf("[DEBUG] Utente %d: Ricevuto SIGTERM, termino.\n", getpid());
        exit(0);
    }
}
void initSigAction() {
    struct sigaction term_action;
    term_action.sa_handler = handle_sig;
    sigemptyset(&term_action.sa_mask);
    term_action.sa_flags = 0;
    sigaction(SIGTERM, &term_action, NULL);
    //todo: capire se posso unire sigaction in uno solo
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);    // Blocca solo SIGALRM durante l'esecuzione dell'handler

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
    ticket_emanation_mgq_id = msgget(KEY_TICKET_EMANATION_MGQ, 0666);
    if (ticket_request_mgq_id == -1) {
        perror("[ERROR] msgget() per ticket_msg_emanation_id fallito");
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
}

//FUNZIONI DI FLOW PRINCIPALE
int generate_random_time(int average_time) {
    printf("[DEBUG] Ticket Dispenser: Generazione tempo casuale. Media: %d\n", average_time);
    int max_variation = average_time / 2;
    int variation = (rand() % (2 * max_variation + 1)) - max_variation;
    int actual_time = average_time + variation;
    int result = actual_time > 0 ? actual_time : 1;
    printf("[DEBUG] Ticket Dispenser: Tempo generato: %d\n", result);
    return result;
    // todo: controllare bene che il tempo da aspettare come int funzioni bene
}
//todo: trovare un modo migliore rispetto a questo
int find_a_seat_index(ServiceType service_type) {
    printf("[DEBUG] Ticket Dispenser: Ricerca posto per servizio tipo %d\n", service_type);
    while (1) {
        if (seats_shm_ptr[seat_finder_index].service_type == service_type) {
            printf("[DEBUG] Ticket Dispenser: Trovato posto all'indice %d\n", seat_finder_index);
            seat_finder_index++;
            return seat_finder_index-1;
        }
        seat_finder_index++;
        seat_finder_index = seat_finder_index % config_shm_ptr->NOF_WORKER_SEATS;
        printf("[DEBUG] Ticket Dispenser: Spostamento all'indice %d\n", seat_finder_index);
    }
    // non è possibile che non lo trovi perché viene fatto prima un controllo
}


Ticket generate_ticket(ServiceType service_type, int ticket_number) {
    printf("[DEBUG] Ticket Dispenser: Generazione ticket %d per servizio tipo %d\n", ticket_number, service_type);

    int average_time = get_average_time(service_type);
    Ticket ticket = {
        .ticket_id = ticket_number,
        .service_type = service_type,
        .actual_time = generate_random_time(average_time),//todo: non è un todo ma ricordare che è gestito come fossero secondi reali (quindi da un int), quando il servizio deve essere erogato bisogna tradurre i secondi reali in secondi della simulazione
        .seat_index = find_a_seat_index(service_type),//todo:  queste funzioni potrebbero dare il problema di eccessiva roba nello stack di chiamate, capire se effettivamente è un problema
        .is_done = 0
    };

    printf("[DEBUG] Ticket Dispenser: Ticket generato - Numero: %d, Tempo: %d\n",
           ticket.ticket_id, ticket.actual_time);
    return ticket;
}

//MAIN

int main(int argc, char *argv[]) {
    initSigAction();
    printf("[DEBUG] Ticket Dispenser: Avvio processo\n");
    setup_ipcs();

    sigsetjmp(jump_buffer, 1);

    printf("[DEBUG] Ticket Dispenser: Attesa sincronizzazione processi\n");
    semaphore_increment(children_ready_sync_sem_id);
    semaphore_do(children_go_sync_sem_id, 0);
    printf("[DEBUG] Ticket Dispenser: Sincronizzazione completata, inizio ciclo di lavoro\n");

    while (1) {
        printf("[DEBUG] Ticket Dispenser: In attesa di richiesta ticket\n");
        if (msgrcv(ticket_request_mgq_id, &tmsg, sizeof(tmsg) - sizeof(long), 0, IPC_NOWAIT) == -1) {
            if (errno == ENOMSG) {
                usleep(100000);  // Attendi 100ms prima di riprovare
                continue;
            } else {
                perror("[ERROR] msgrcv fallito");
                break;
            }
        }
        printf("[DEBUG] Ticket Dispenser: Ricevuta richiesta da utente %d per servizio tipo %d\n", tmsg.requiring_user, tmsg.service_type);

        tmsg.ticket = generate_ticket(tmsg.service_type, ticket_index);
        tmsg.mtype = 1;
        ticket_index++;

        printf("[DEBUG] Ticket Dispenser: Invio ticket %d all'utente %d\n",
              tmsg.ticket.ticket_id, tmsg.requiring_user);
        msgsnd(ticket_request_mgq_id, &tmsg, sizeof(tmsg) - sizeof(long), 0);
        printf("[DEBUG] Ticket Dispenser: Ticket inviato con successo\n");
    }
    return 0;
}
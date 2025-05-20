#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sched.h>
#include <sys/shm.h>

#include "common.h"
#include "../lib/sem_handling.h"
#include "../lib/utils.h"

//VARIABILI GLOBALI
int children_ready_sync_sem_id;
int children_go_sync_sem_id;
sigjmp_buf jump_buffer;
Seat *seats_shm_ptr;
Config *config_shm_ptr;
Ticket *tickets_bucket_shm_ptr;
int ticket_request_msg_id;


//FUNZIONI DI SETUP DELLA SIMULAZIONE
void handle_sig(int sig) {
    if (sig == ENDEDDAY) {
        printf("[DEBUG] Utente %d: Ricevuto segnale di fine giornata\n", getpid());


        // pulire risorse
        // se serve terminare in modo pulito le risorse posso farlo qui
        // rimettersi in ready
        siglongjmp(jump_buffer, 1); // Salta all'inizio del ciclo
    }else if (sig== SIGTERM) {
        printf("[DEBUG] Utente %d: Ricevuto SIGTERM, termino.\n", getpid());
        shmdt(config_shm_ptr);
        shmdt(seats_shm_ptr);
        shmdt(tickets_bucket_shm_ptr);
        //fflush(stdout); todo: capire se questa cosa serve tramite dei test
        exit(0);
    }
}
void setup_sigaction(){
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
    int seats_shm_id = shmget(KEY_SEATS_SHM, sizeof(Seat) * config_shm_ptr->NOF_WORKER_SEATS, 0666);
    if (seats_shm_id == -1) {
        perror("Errore nella memoria condivisa per i posti");
        exit(EXIT_FAILURE);
    }
    seats_shm_ptr = shmat(seats_shm_id, NULL, 0);
    if (seats_shm_ptr == (void *)-1) {
        perror("Errore nel collegamento alla memoria condivisa dei posti");
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


//FUNZIONI DI FLOW PRINCIPALE
void set_ready() {
    printf("[DEBUG] Utente %d: Sono pronto per la nuova giornata\n", getpid());
    semaphore_increment(children_ready_sync_sem_id);
    semaphore_do(children_go_sync_sem_id, 0);
    printf("[DEBUG] Utente %d: Sto iniziando una nuova giornata\n", getpid());
}

int decide_if_go() {
    printf("[DEBUG] Utente %d: Decido se andare oggi\n", getpid());

    // Calcolo della probabilità casuale tra P_SERV_MIN e P_SERV_MAX
    double range = config_shm_ptr->P_SERV_MAX - config_shm_ptr->P_SERV_MIN;
    double p_serv = config_shm_ptr->P_SERV_MIN + ((double)rand() / RAND_MAX) * range;

    // Generazione della decisione basata su P_SERV
    int decision = ((double)rand() / RAND_MAX <= p_serv);

    // Stampa del risultato
    printf("[DEBUG] Utente %d: Decisione: %s\n", getpid(), decision ? "vado" : "resto a casa");

    return decision;
}
struct timespec generate_random_go_to_post_office_time() {
    struct timespec ts;
    ts.tv_sec = rand() % SECS_FOR_A_DAY;
    ts.tv_nsec = rand() % 1000000000;
    return ts;
}
int check_for_service_availability(ServiceType service_type) {
    printf("[DEBUG] Utente %d: Controllo disponibilità servizio tipo %d\n", getpid(), service_type);
    for (int i = 0; i < config_shm_ptr->NOF_WORKER_SEATS; i++) {
        if (seats_shm_ptr[i].service_type == service_type) {
            return 1;
        }
    }
    return 0;
}
// go_home aspetta l'inizio della prossima giornata
void go_home() {
    printf("[DEBUG] Utente %d: Tornato a casa\n", getpid());
    pause(); // aspetta la fine della giornata ENDDAY o della simulazione
}

//tutto da testare
int main(int argc, char *argv[]) {
    setup_sigaction();
    srand(getpid());
    setup_ipcs();
    sigsetjmp(jump_buffer, 1);

    set_ready();

    if (decide_if_go()) {
        struct timespec time_to_wait_before_going_to_post_office=generate_random_go_to_post_office_time();
        printf("[DEBUG] Utente %d: aspettero %d,%d secondi prima di andare all'ufficio postale\n",getpid(), time_to_wait_before_going_to_post_office.tv_sec, time_to_wait_before_going_to_post_office.tv_nsec);
        nanosleep(&time_to_wait_before_going_to_post_office,NULL);
        printf("[DEBUG] Utente %d: vado all'ufficio postale\n", getpid());
        ServiceType service_type = get_random_service_type();
        printf("[DEBUG] Utente %d: Ho scelto il servizio tipo %d\n", getpid(), service_type);
//todo: il processo utente non parte subito ma stabilisce un orario (sleeppa per un tempo casuale)
        if (check_for_service_availability(service_type)) {
            printf("[DEBUG] Utente %d: Servizio disponibile, calcolo tempo di attesa\n", getpid());

        ///RICHIEDE IL TICKET
        //TODO: rivedere la '''funzione''' sotto con la nuova versione di Ticket quando definiremo i Ticket
        // get_ticket(ServiceType service_type)
            printf("[DEBUG] Utente %d: Richiedo ticket per servizio tipo %d\n", getpid(), service_type);
            Ticket_request_message trm;
            trm.mtype = 2;
            trm.requiring_user = getpid();
            trm.service_type = service_type;
            clock_gettime(CLOCK_MONOTONIC,&trm.request_time);
            if (msgsnd(ticket_request_msg_id, &trm, sizeof(trm)-sizeof(trm.mtype), 0)==-1) {
                perror("Errore nell'invio della richiesta di ticket");
                exit(EXIT_FAILURE);
            }

            msgrcv(ticket_request_msg_id, &trm, sizeof(trm)-sizeof(trm.mtype), getpid(), 0);
            Ticket ticket = tickets_bucket_shm_ptr[trm.ticket_index];

            while (ticket.is_done==0)
                sched_yield(); // cede la CPU ad altri processi pronti
            //TODO: SOSTITUIRE IL WHILE SOPRA CON QUALCOSA DI PIÙ EFFICENTE
            printf("------- Utente %d: Servizio completato-------\n", getpid());

            go_home();
        } else {
            printf("[DEBUG] Utente %d: Servizio non disponibile\n", getpid());
            go_home();
        }
    } else {
        printf("[DEBUG] Utente %d: Oggi resto a casa\n", getpid());
        go_home();
    }


    printf("[DEBUG] Utente %d: Processo terminato in modo inaspettato\n", getpid());
    return 0;
}
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bits/signum-generic.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include "common.h"
#include "sem_handling.h"
#include "utils.h"

int children_ready_sync_sem_id;
int children_go_sync_sem_id;
sigjmp_buf jump_buffer;
Seat *seats_shm_ptr;
Config *config_shm_ptr;
Ticket *tickets_bucket_shm_ptr;
int ticket_request_msg_id;
int ticket_emanation_mgq_id;//todo: rimuovere ticket_emanation_mgq e tutto ciò che lo riguarda se ticket_tbe_mgq funziona
int tickets_tbe_mgq_id;//tbe= to be erogated
Seat current_seat;
int aviable_breaks;
int in_break = 0;

ServiceType service_type;

//FUNZIONI DI SETUP
void handle_sig(int sig) {
    if (sig == ENDEDDAY) {
        printf("[DEBUG] Utente %d: Ricevuto segnale di fine giornata\n", getpid());


        // pulire risorse
        // se serve terminare in modo pulito le risorse posso farlo qui
        // rimettersi in ready
        siglongjmp(jump_buffer, 1); // Salta all'inizio del ciclo
    }else if (sig== SIGTERM) {
        printf("[DEBUG] Utente %d: Ricevuto SIGTERM, termino.\n", getpid());
        fflush(stdout);
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


//FUNZIONI DI FLOW PRINCIPALE
void set_ready() {
    printf("[DEBUG] Operatore %d: Sono pronto per la nuova giornata\n", getpid());
    semaphore_increment(children_ready_sync_sem_id);
    semaphore_do(children_go_sync_sem_id, 0);
    printf("[DEBUG] Operatore %d: Sto iniziando una nuova giornata\n", getpid());
}
void go_on_break() {
    printf("[DEBUG] Operatore %d: Vado in pausa\n", getpid());
    pause(); // aspetta la fine della giornata ENDDAY o della simulazione
}

int main () {
    setup_sigaction();
    setup_ipcs();
    aviable_breaks = config_shm_ptr->NOF_PAUSE;
    service_type = get_random_service_type();
    sigsetjmp(jump_buffer, 1);

    set_ready();

        for (int i=0; i<config_shm_ptr->NOF_WORKER_SEATS; i++) {
            if (seats_shm_ptr[i].service_type == service_type && semaphore_do_not_wait(seats_shm_ptr[i].worker_sem_id, -1) == 0/*todo: assicurarsi che questa seconda parte dill'if funzioni davvero*/) {
                printf("[DEBUG] Worker %d: Trovato posto libero %d\n", getpid(), i);
                current_seat = seats_shm_ptr[i];

                //EROGAZIONE SERVIZIO
                while (1){
                    printf("[DEBUG] Worker %d: In attesa di ticket da erogare del mio tipo di servizio \n", getpid());


                    printf("[DEBUG] Worker %d: Cliente arrivato, attendo ticket\n", getpid());
                    Ticket_tbe_message ttbemsg;
                    msgrcv(tickets_tbe_mgq_id,&ttbemsg,sizeof(ttbemsg)-sizeof(long),service_type+1,0);

                    printf("[DEBUG] Worker %d: Inizio servizio, durata: %d\n", getpid(), tickets_bucket_shm_ptr[ttbemsg.ticket_index].actual_time);
                    sleep(tickets_bucket_shm_ptr[ttbemsg.ticket_index].actual_time);
                    clock_gettime(CLOCK_MONOTONIC,&tickets_bucket_shm_ptr[ttbemsg.ticket_index].end_time);
                    tickets_bucket_shm_ptr[ttbemsg.ticket_index].is_done = 1;

                    printf("[DEBUG] Worker %d: Servizio completato\n", getpid());

                    //DECIDE SE ANDARE IN PAUSA
                    if (aviable_breaks > 0) {
                        in_break = rand() % P_BREAK == 0;

                        if (in_break == 1) {
                            aviable_breaks--;
                            printf("[DEBUG] Worker %d: Vado in pausa. Pause rimanenti: %d\n", getpid(), aviable_breaks);
                            go_on_break();
                        }
                        else {
                            printf("[DEBUG] Worker %d: NON vado in pausa\n", getpid());
                        }
                    }

                    printf("[DEBUG] Worker %d: Processo terminato in modo inaspettato\n", getpid());
                    return 0;
                }
            }
            if (i==config_shm_ptr->NOF_WORKER_SEATS)
                i=0;
        }

}
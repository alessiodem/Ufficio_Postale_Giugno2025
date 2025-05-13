#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/shm.h>

#include "common.h"
#include "sem_handling.h"
#include "utils.h"


//VARIABILI GLOBALI
int children_ready_sync_sem_id;
int children_go_sync_sem_id;
sigjmp_buf jump_buffer;
Seat *seats_shm_ptr;
Config *config_shm_ptr;
int ticket_request_msg_id;
int ticket_emanation_msg_id;

#define MSG_SIZE (sizeof(req) - sizeof(long))

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
    if ((ticket_emanation_msg_id = msgget(KEY_TICKET_EMANATION_MGQ, 0666)) == -1) {
        perror("Errore msgget KEY_TICKET_EMANATION_MGQ");
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
    double P_SERV = config_shm_ptr->P_SERV_MIN + ((double)rand() / RAND_MAX) * range;

    // Generazione della decisione basata su P_SERV
    int decision = ((double)rand() / RAND_MAX <= P_SERV);

    // Stampa del risultato
    printf("[DEBUG] Utente %d: Decisione: %s\n", getpid(), decision ? "vado" : "resto a casa");

    return decision;
}
int check_for_service_aviability(ServiceType service_type) {
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

    /*int jump_result =*/ sigsetjmp(jump_buffer, 1);
    //non  ricordo del perché ho messo queste righe di codice ma sembrerebbe poter avere un senso(potrebbe benissimo essere un delirio delle 4 del mattino e basta ) dovrebbe poter essere cacciato ma in caso servisse è qui
    // if (jump_result != 0) {
    //     // Salto eseguito da siglongjmp() dopo ENDEDDAY
    //     set_ready();
    //     printf("[DEBUG] Utente %d: Inizio nuova giornata\n", getpid());
    // }


    set_ready();

    if (decide_if_go()) {
        ServiceType service_type = get_random_service_type();
        printf("[DEBUG] Utente %d: Ho scelto il servizio tipo %d\n", getpid(), service_type);

        if (check_for_service_aviability(service_type)) {
            printf("[DEBUG] Utente %d: Servizio disponibile, calcolo tempo di attesa\n", getpid());

        ///RICHIEDE IL TICKET
        //TODO: rivedere la '''funzione''' sotto con la nuova versione di Ticket quando definiremo i Ticket
        // get_ticket(ServiceType service_type)
            printf("[DEBUG] Utente %d: Richiedo ticket per servizio tipo %d\n", getpid(), service_type);
            Ticket_request_message req;
            req.mtype = 0;
            req.requiring_user = getpid();
            req.service_type = service_type;
            msgsnd(ticket_request_msg_id, &req, MSG_SIZE, 0);

            while (req.ticket.is_done==0)//todo: ottimizzare questa attesa (si potrebbe passare il comando al prossimo processo all'interno del while)

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
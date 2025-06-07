#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include <sys/msg.h>


#include "common.h"
#include "../lib/sem_handling.h"
#include "../lib/utils.h"

//VARIABILI GLOBALI
struct timespec daily_woking_time;
int no_children = 0;
pid_t *child_pids = NULL;
int days_passed;

//IPCs
Config *config_shm_ptr;
Seat *seats_shm_ptr;
Ticket *tickets_bucket_shm_ptr;
int children_ready_sync_sem_id;
int children_go_sync_sem_id;
int ticket_request_msg_id;
int tickets_tbe_mgq_id;//tbe= to be erogated
int seat_freed_mgq_id;
int break_mgq_id = -1;
int clock_in_mgq_id = -1;
int tickets_bucket_sem_id;

//PROTOTIPI FUNZIONI
void term_children();
void free_memory();
void setup_analytics();
void free_temp_analytics();
void free_analytics();

//FUNZIONI AUSILIARIE
// Funzione per aggiungere un PID alla lista dei figli creati
void add_child_pid(pid_t child_pid) {
    //printf("[DEBUG] Aggiunta del PID %d alla lista dei figli...\n", child_pid);
    pid_t *temp = realloc(child_pids, (no_children + 1) * sizeof(pid_t));
    if (temp == NULL) {
        perror("Allocazione memoria per PIDs fallita");
        exit(EXIT_FAILURE);
    }
    child_pids = temp;
    child_pids[no_children] = child_pid;
    no_children++;
}

void fork_and_execute(const char *file_path, char *const argv[]) {
    //printf("[DEBUG] Forking processo per: %s...\n", file_path);
    pid_t child_pid = fork();

    switch (child_pid) {
        case -1:
            perror("Fork fallito");
            exit(EXIT_FAILURE);

        case 0:
            //printf("[DEBUG] Processo figlio avviato: %s\n", file_path);
            if (execve(file_path, argv, NULL) == -1) {
                perror("Execve fallito");
                exit(EXIT_FAILURE);
            }
            break;

        default:
            //printf("[DEBUG] Processo padre: figlio %d avviato\n", child_pid);
            add_child_pid(child_pid);
            break;
    }
}

//FUNZIONI DEBUG
void debug__print__todays__seats__service(){
    printf("====================\n");
    for (int i =0;  i<config_shm_ptr->NOF_WORKER_SEATS;i++)
        printf("[DEBUG]Lo sportello %d puo' erogare il servizio %d\n", i,seats_shm_ptr[i].service_type);
    printf("====================\n");
}
void debug__print__configs(){
    if (config_shm_ptr == NULL) {
        fprintf(stderr, "Errore: puntatore Config nullo.\n");
        return;
    }
    printf("====================\n");
    printf("Configurazione:\n");
    printf("NOF_WORKERS: %d\n", config_shm_ptr->NOF_WORKERS);
    printf("NOF_USERS: %d\n", config_shm_ptr->NOF_USERS);
    printf("NOF_WORKER_SEATS: %d\n", config_shm_ptr->NOF_WORKER_SEATS);
    printf("SIM_DURATION: %d\n", config_shm_ptr->SIM_DURATION);
    printf("P_SERV_MIN: %.2f\n", config_shm_ptr->P_SERV_MIN);
    printf("P_SERV_MAX: %.2f\n", config_shm_ptr->P_SERV_MAX);
    printf("N_NANO_SECS: %ld\n", config_shm_ptr->N_NANO_SECS);
    printf("NOF_PAUSE: %d\n", config_shm_ptr->NOF_PAUSE);
    printf("====================\n");

};

//FUNZIONI DI SETUP DELLA SIMULAZIONE
void setup_ipcs() {

    //l'inizializzazione di config_shm_ptr è nella funzione setup_config() perché sevre inizializzarla prima di cricare la configurazione

    children_ready_sync_sem_id = semget(KEY_SYNC_START_SEM, 1, EXCLUSIVE_CREATE_FLAG);
    if (children_ready_sync_sem_id == -1) {
        perror("Errore nella creazione del semaforo di sincronizzazione per i figli");
        exit(EXIT_FAILURE);
    }
    if (semctl(children_ready_sync_sem_id, 0, SETVAL, 0) == -1) {
        perror("Errore nel settaggio iniziale del semaforo children_ready_sync_sem_id");
        exit(EXIT_FAILURE);
    }

    children_go_sync_sem_id = semget(KEY_SYNC_CHILDREN_START_SEM, 1, EXCLUSIVE_CREATE_FLAG);
    if (children_go_sync_sem_id == -1) {
        perror("Errore nella creazione del semaforo di partenza per i figli");
        exit(EXIT_FAILURE);
    }
    if (semctl(children_go_sync_sem_id, 0, SETVAL, 1) == -1) {
        perror("Errore nel settaggio iniziale del semaforo children_go_sync_sem_id");
        exit(EXIT_FAILURE);
    }

    int seats_shm_id = shmget(KEY_SEATS_SHM, sizeof(Seat) * config_shm_ptr->NOF_WORKER_SEATS, EXCLUSIVE_CREATE_FLAG);
    if (seats_shm_id == -1) {
        perror("Errore nella creazione della memoria condivisa per i posti");
        exit(EXIT_FAILURE);
    }
    seats_shm_ptr = shmat(seats_shm_id, NULL, 0);
    if (seats_shm_ptr == (void *)-1) {
        perror("[ERROR] shmat() per seats_shm fallito per seats_shm_ptr");
        exit(EXIT_FAILURE);
    }
    int tickets_bucket_shm_id = shmget(KEY_TICKETS_BUCKET_SHM, sizeof(Ticket) * config_shm_ptr->NOF_USERS*config_shm_ptr->SIM_DURATION, EXCLUSIVE_CREATE_FLAG);
    if (tickets_bucket_shm_id == -1) {
        perror("Errore nella creazione della memoria condivisa per il cesto di ticket");
        exit(EXIT_FAILURE);
    }
    tickets_bucket_shm_ptr = shmat(tickets_bucket_shm_id, NULL, 0);
    if (tickets_bucket_shm_ptr == (void *)-1) {
        perror("[ERROR] shmat() per tickets_bucket_shm fallito ");
        exit(EXIT_FAILURE);
    }
    ticket_request_msg_id = msgget(KEY_TICKET_REQUEST_MGQ, EXCLUSIVE_CREATE_FLAG);
    if (ticket_request_msg_id == -1) {
        perror("Errore nella creazione della message queue per i ticket");
        exit(EXIT_FAILURE);
    }
    tickets_tbe_mgq_id = msgget(KEY_TICKETS_TBE_MGQ, EXCLUSIVE_CREATE_FLAG);
    if (tickets_tbe_mgq_id == -1) {
        perror("Errore nella creazione della message queue per i ticket da erogare");
        exit(EXIT_FAILURE);
    }
    seat_freed_mgq_id = msgget(KEY_SEAT_FREED_MGQ, EXCLUSIVE_CREATE_FLAG);
    if (seat_freed_mgq_id == -1) {
        perror("Errore nella creazione della message queue per i seat liberati");
        exit(EXIT_FAILURE);
    }
    break_mgq_id = msgget(KEY_BREAK_MGQ, EXCLUSIVE_CREATE_FLAG);
    if (break_mgq_id == -1) {
        perror("Errore nella creazione della message queue le pause effettuate");
        exit(EXIT_FAILURE);
    }
    clock_in_mgq_id = msgget(KEY_CLOCK_IN_MGQ, EXCLUSIVE_CREATE_FLAG);
    if (clock_in_mgq_id == -1) {
        perror("Errore nella creazione della message queue per il check in degli operatori");
        exit(EXIT_FAILURE);
    }

    //Semaforo globale per proteggere tickets_bucket_shm
    tickets_bucket_sem_id = semget(KEY_TICKETS_BUCKET_SEM, 1, EXCLUSIVE_CREATE_FLAG);
    if (tickets_bucket_sem_id == -1) {
        perror("Errore nella creazione del semaforo per il cesto di ticket");
        exit(EXIT_FAILURE);
    }
    if (semctl(tickets_bucket_sem_id, 0, SETVAL, 1) == -1) {
        perror("Errore nel settaggio iniziale del semaforo tickets_bucket_sem_id");
        exit(EXIT_FAILURE);
    }
}

void setup_config(){

    int config_shm_id = shmget(KEY_CONFIG_SHM, sizeof(Config), EXCLUSIVE_CREATE_FLAG);
    if (config_shm_id == -1) {
        perror("Errore nella creazione della memoria condivisa per la configurazione");
        exit(EXIT_FAILURE);
    }
    config_shm_ptr = shmat(config_shm_id, NULL, 0);
    if (config_shm_ptr == (void *) -1) {
        perror("Errore nell'aggancio della memoria condivisa per la configurazione");
        exit(EXIT_FAILURE);
    }
}

void load_config(FILE *config_file) {
    int read_lines = 0;

    char *line = malloc(LINE_BUFFER_SIZE * sizeof(char));
    if (line == NULL) {
        perror("Errore allocazione buffer");
        fclose(config_file);
        exit(EXIT_FAILURE);
    }

    while (fgets(line, LINE_BUFFER_SIZE, config_file) != NULL) {
        char *name = strtok(line, " ");
        char *value = strtok(NULL, " \n"); // rimuove eventuale newline finale

        if (name == NULL || value == NULL) {
            printf("Errore nella lettura config 1\n");
            fclose(config_file);
            free(line);
            exit(EXIT_FAILURE);
        }

        int value_int = strtol(value, NULL, 10);
        double value_double = strtod(value, NULL);
        unsigned long value_ulong = strtoul(value, NULL, 10);

        if ((value_int < 0 && strcmp(name, "N_NANO_SECS") != 0) || value_double < 0) {
            printf("Errore nella lettura config 1 (valori negativi)\n");
            fclose(config_file);
            free(line);
            exit(EXIT_FAILURE);
        }

        if (strcmp(name, "NOF_WORKERS") == 0) config_shm_ptr->NOF_WORKERS = value_int;
        else if (strcmp(name, "NOF_USERS") == 0) config_shm_ptr->NOF_USERS = value_int;
        else if (strcmp(name, "NOF_WORKER_SEATS") == 0) config_shm_ptr->NOF_WORKER_SEATS = value_int;
        else if (strcmp(name, "SIM_DURATION") == 0) config_shm_ptr->SIM_DURATION = value_int;
        else if (strcmp(name, "P_SERV_MIN") == 0) config_shm_ptr->P_SERV_MIN = value_double;
        else if (strcmp(name, "P_SERV_MAX") == 0) config_shm_ptr->P_SERV_MAX = value_double;
        else if (strcmp(name, "N_NANO_SECS") == 0) config_shm_ptr->N_NANO_SECS = value_ulong;
        else if (strcmp(name, "NOF_PAUSE") == 0) config_shm_ptr->NOF_PAUSE = value_int;
        else if (strcmp(name, "EXPLODE_THRESHOLD") == 0) config_shm_ptr->EXPLODE_THRESHOLD = value_int;
        else {
            printf("Errore nella lettura config 2 (parametro sconosciuto)\n");
            fclose(config_file);
            free(line);
            exit(EXIT_FAILURE);
        }

        read_lines++;
    }

    free(line);

    if (config_shm_ptr->P_SERV_MAX > 1 ||
        config_shm_ptr->P_SERV_MIN > config_shm_ptr->P_SERV_MAX ||
        read_lines < 9) {
        fclose(config_file);
        printf("Errore nella lettura config 3 (parametri inconsistenti)\n");
        exit(EXIT_FAILURE);
    }
    config_shm_ptr->current_day = 0;
}

void compute_daytime(){
    daily_woking_time.tv_sec=SECS_FOR_A_DAY;
    daily_woking_time.tv_nsec=NSECS_FOR_A_DAY;
}
void create_seats() {
    //printf("[DEBUG] Creazione posti...\n");
    for (int i = 0; i < config_shm_ptr->NOF_WORKER_SEATS; i++) {
        seats_shm_ptr[i].worker_sem_id= create_semaphore_and_setval(IPC_PRIVATE, 1, 0666 | IPC_CREAT, 1);
        seats_shm_ptr[i].has_operator  = 0;
    }
    //printf("[DEBUG] Sportelli creati.\n");
}

void create_workers() {
    //printf("[DEBUG] Creazione workers...\n");

    char *child_argv[2];
    child_argv[0] = "worker";
    child_argv[1] = NULL;

    for (int worker = 0; worker < config_shm_ptr->NOF_WORKERS; worker++)
        fork_and_execute("./build/worker", child_argv);
    //printf("[DEBUG] Workers creati.\n");
}
void create_users() {
    //printf("[DEBUG] Creazione utenti...\n");

    char *child_argv[2];
    child_argv[0] = "user";
    child_argv[1] = NULL;

    for (int user = 0; user < config_shm_ptr->NOF_USERS; user++)
        fork_and_execute("./build/user", child_argv);
    //printf("[DEBUG] Utenti creati.\n");
}
void create_ticket_dispenser(){
    //printf("[DEBUG] Creazione Ticket_dispenser...\n");

    char *child_argv[2];
    child_argv[0] = "ticket_dispenser";
    child_argv[1] = NULL;

    fork_and_execute("./build/ticket_dispenser", child_argv);
    //printf("[DEBUG] Ticket dispenser creato.\n");
}
void handle_sig(int sig) {
    if (sig == SIGINT) {
        //printf("[DEBUG] Direttore %d: Ricevuto SIGINT, termino.\n", getpid());
        free_memory();
        exit(0);
    }
}
void setup_sigaction() {

    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa,NULL)==-1) {
        perror("Errore sigaction SIGINT");
        exit(EXIT_FAILURE);
    }
}

void setup_simulation(){
    srand(time(NULL)*getpid());
    compute_daytime();
    setup_sigaction();
    setup_ipcs();
    printf("[DEBUG] Durata di un giorno: %ld secondi e %ld nanosecondi\n", daily_woking_time.tv_sec, daily_woking_time.tv_nsec);
    create_seats();
    create_workers();
    create_users();
    create_ticket_dispenser();
    setup_analytics();
}
void randomize_seats_service(){
    for (int i = 0; i < config_shm_ptr->NOF_WORKER_SEATS; i++) {
        seats_shm_ptr[i].service_type = get_random_service_type();
    }
}


//FUNZIONI DI FLOW PRINCIPALE
void wait_to_all_children_be_ready(){

    printf("[DEBUG] Direttore: aspetto che i figli siano pronti. \n");
    semaphore_do(children_ready_sync_sem_id, -no_children);

    printf("[DEBUG] Direttore: SET!. \n");

    semaphore_decrement(children_go_sync_sem_id);
    printf("[DEBUG] Direttore: GO!!! \n\n");
}

void notify_day_ended(){
    for (int i =0; i<no_children; i++){
        kill(child_pids[i], ENDEDDAY);
        //printf("[DEBUG] Direttore: Notifico il figlio %d che la giornata è terminata\n",child_pids[i]);
    }
}


void print_end_simulation_output(char* end_cause, int day_passed) {
    printf("\n========================================\n");
    printf("          FINE SIMULAZIONE\n");
    printf("========================================\n");
    printf("📅 Ultimo giorno simulato : %d\n", day_passed);
    printf("🛑 Causa terminazione     : %s\n", end_cause);
    printf("----------------------------------------\n");
    printf("📊 Statistiche finali:\n");
    //analytics_print(days_passed);
    printf("========================================\n\n");
}
void check_explode_threshold() {
    int users_waiting=0;
    for (int i = 0;i<config_shm_ptr->NOF_USERS && tickets_bucket_shm_ptr[i].end_time.tv_nsec==0 && tickets_bucket_shm_ptr[i].end_time.tv_sec==0;i++) {//può non essere gestita la mutua esclusione perché durante l'esecuzione di questa line gli altri processi attendono al ready-go
        users_waiting++;
        if (users_waiting> config_shm_ptr->EXPLODE_THRESHOLD) {
            term_children();
            free_memory();
            print_end_simulation_output("EXPLODE THRESHOLD",days_passed-1);
            //analytics_finalize();
            exit(EXIT_FAILURE);
        }
    }
}

//FUNZIONI DI PULIZIA
void reset_resources(){
    // Reset dei semafori di sincronizzazione
    if (semctl(children_ready_sync_sem_id, 0, SETVAL, 0) == -1) {
        perror("[ERRORE] Errore nel reset del semaforo children_ready_sync_sem_id");
    }

    if (semctl(children_go_sync_sem_id, 0, SETVAL, 1) == -1) {
        perror("[ERRORE] Errore nel reset del semaforo children_go_sync_sem_id");
    }
    Ticket_request_message trm;
    // Pulizia dei messaggi presenti nelle message queue
    while (msgrcv(ticket_request_msg_id, &trm, sizeof(trm)-sizeof(trm.mtype), 0, IPC_NOWAIT) != -1)
        if (errno != ENOMSG) {
            perror("[ERRORE] Errore nello svuotamento della message queue ticket_request_msg_id");
        }
    Ticket_tbe_message ttbemsg;
    while (msgrcv(tickets_tbe_mgq_id, &ttbemsg, sizeof(ttbemsg)-sizeof(ttbemsg.mtype), 0, IPC_NOWAIT) != -1)
        if (errno != ENOMSG) {
            perror("[ERRORE] Errore nello svuotamento della message queue tickets_tbe_mgq_id");
        }
    Freed_seat_message fsm;
    while (msgrcv(seat_freed_mgq_id, &fsm, sizeof(fsm)-sizeof(fsm.mtype), 0, IPC_NOWAIT) != -1)
        if (errno != ENOMSG) {
            perror("[ERRORE] Errore nello svuotamento della message queue seat_freed_mgq_id");
        }
    Clock_in_message cim;
    while (msgrcv(clock_in_mgq_id, &cim, sizeof(cim)-sizeof(cim.mtype), 0, IPC_NOWAIT) != -1)
        if (errno != ENOMSG) {
            perror("[ERRORE] Errore nello svuotamento della message queue clock_in_mgq_id");
        }

    if (semctl(tickets_bucket_sem_id, 0, SETVAL, 1) == -1) {
        perror("Errore nel settaggio iniziale del semaforo tickets_bucket_sem");
        exit(EXIT_FAILURE);
    }

    //i semafori dei seats vengono resettati dai workers quando ricevono ENDDAY

    //printf("[DEBUG] Direttore: risorse pulite");
}
void free_memory() {
    free_analytics();

    // Libera memoria allocata dinamicamente
    free(child_pids);

    // Rimozione semafori di sincronizzazione
    semctl(children_ready_sync_sem_id, 0, IPC_RMID);
    semctl(children_go_sync_sem_id, 0, IPC_RMID);
    semctl(tickets_bucket_sem_id, 0, IPC_RMID);

    // Rimozione delle message queue
    msgctl(ticket_request_msg_id, IPC_RMID, NULL);
    msgctl(tickets_tbe_mgq_id, IPC_RMID, NULL);
    msgctl(break_mgq_id, IPC_RMID, NULL);
    msgctl(seat_freed_mgq_id, IPC_RMID, NULL);
    msgctl(clock_in_mgq_id, IPC_RMID, NULL);

    // Rimozione delle memorie condivise
    int config_shm_id = shmget(KEY_CONFIG_SHM, sizeof(Config), 0666);
    if (config_shm_id != -1) {
        shmctl(config_shm_id, IPC_RMID, NULL);
    }

    int seats_shm_id = shmget(KEY_SEATS_SHM, sizeof(Seat) * config_shm_ptr->NOF_WORKER_SEATS, 0666);
    if (seats_shm_id != -1) {
        shmctl(seats_shm_id, IPC_RMID, NULL);
    }

    int tickets_bucket_shm_id = shmget(KEY_TICKETS_BUCKET_SHM, sizeof(Ticket)*config_shm_ptr->SIM_DURATION*config_shm_ptr->NOF_USERS, 0666);
    if (tickets_bucket_shm_id != -1) {
        shmctl(tickets_bucket_shm_id, IPC_RMID, NULL);
    }

    // Rimozione dei semafori degli sportelli
    for (int i = 0; i < config_shm_ptr->NOF_WORKER_SEATS; i++) {
        semctl(seats_shm_ptr[i].worker_sem_id, 0, IPC_RMID);
    }

    // Distacco dalle memorie condivise
    shmdt(config_shm_ptr);
    shmdt(seats_shm_ptr);

    //printf("[DEBUG] Risorse IPC deallocate correttamente\n");
}


void term_children() {
    printf("[DEBUG] Terminazione della simulazione: terminazione di tutti i processi figli...\n");
    for (int i = 0; i < no_children; i++) {
        if (kill(child_pids[i], SIGTERM) == -1) {
            perror("Errore durante la terminazione del processo figlio");
        } else {
            // printf("[DEBUG] Segnale di terminazione inviato al processo figlio %d\n", child_pids[i]);
        }
    }
}

//STATISTICHE
pid_t *seen_users_sim;
int seen_users_sim_counter=0;
int *total_ticket_served_per_day;
int total_ticket_unserved;
double *user_waiting_time_sum_per_day;
double *service_serving_time_sum_per_day;

pid_t *seen_operators_pids_today;
int seen_operators_today_counter;
pid_t *seen_operators_pids_sim;
int seen_operators_sim_counter=0;

int operators_pauses_sim=0;

int *op_per_service;
int *seats_per_service;

void setup_temp_analytics(){


    seen_operators_pids_today=calloc(config_shm_ptr->NOF_WORKERS, sizeof(pid_t));
    if (seen_operators_pids_today == NULL) {
        perror("calloc seen_operators_sim" );
        exit(EXIT_FAILURE);
    }
    seats_per_service=calloc(config_shm_ptr->NOF_WORKER_SEATS, sizeof(int));
    if (seats_per_service == NULL) {
        perror("calloc seats_per_service" );
        exit(EXIT_FAILURE);
    }

}
void setup_analytics() {
    seen_users_sim=calloc(config_shm_ptr->NOF_USERS, sizeof(pid_t));
    if (seen_users_sim == NULL) {
        perror("calloc seen_users_sim" );
        exit(EXIT_FAILURE);
    }
    seen_operators_pids_sim=calloc(config_shm_ptr->NOF_USERS, sizeof(pid_t));
    if (seen_operators_pids_sim == NULL) {
        perror("calloc seen_operators_pids_sim" );
        exit(EXIT_FAILURE);
    }
    total_ticket_served_per_day=calloc(config_shm_ptr->SIM_DURATION, sizeof(int));
    if (total_ticket_served_per_day == NULL) {
        perror("calloc total_ticket_served_per_day" );
        exit(EXIT_FAILURE);
    }
    user_waiting_time_sum_per_day=calloc(config_shm_ptr->SIM_DURATION, sizeof(double));
    if (user_waiting_time_sum_per_day == NULL) {
        perror("calloc user_waiting_time_sum_per_day" );
        exit(EXIT_FAILURE);
    }
    service_serving_time_sum_per_day=calloc(config_shm_ptr->SIM_DURATION, sizeof(int));
    if (service_serving_time_sum_per_day == NULL) {
        perror("calloc service_serving_time_sum_per_day" );
        exit(EXIT_FAILURE);
    }
    op_per_service=calloc(config_shm_ptr->NOF_WORKERS, sizeof(int));
    if (op_per_service == NULL) {
        perror("calloc op_per_service" );
        exit(EXIT_FAILURE);
    }


    setup_temp_analytics();
    //15
    Clock_in_message cim;
    for (int i =0; i<config_shm_ptr->NOF_WORKER_SEATS;i++){
        if (msgrcv(clock_in_mgq_id, &cim, sizeof(cim)-sizeof(cim.mtype), 0, 0) != -1)
            op_per_service[cim.service_type]++;

    }
}

void compute_analytics_wrapper(Ticket bucket[], int service) {
    total_ticket_unserved=0;
    seen_operators_today_counter=0;

    int today= config_shm_ptr->current_day+1;
    for (int i =0; i< config_shm_ptr->NOF_USERS*config_shm_ptr->SIM_DURATION;i++) {
        Ticket current_ticket= bucket[i];
        if (current_ticket.day_number==config_shm_ptr->current_day && current_ticket.request_time.tv_nsec!=0 && current_ticket.request_time.tv_sec!=0){

            if (current_ticket.end_time.tv_nsec == 0 && current_ticket.end_time.tv_sec == 0 && current_ticket.request_time.tv_nsec != 0 &&current_ticket.request_time.tv_sec != 0  ) {
                //4
                total_ticket_unserved++;
            }else {
                //1 e 2
                int seen=0;
                for (int j =0; !seen && j<config_shm_ptr->NOF_USERS; j++) {
                    if (seen_users_sim[j]==current_ticket.user_id)
                        seen=1;
                }
                if (!seen) {
                    seen_users_sim[seen_users_sim_counter]=current_ticket.user_id;
                    seen_users_sim_counter++;

                }else
                    seen=0;
                //12 e 13
                   //controlla prima se c'è nel generale della simulazione, se non c'è aggiunge il pid ad entrambe le raccolte altrimenti controlla se oggi nonsi era ancora visto
                seen=0;
                for (int j =0; !seen && j<config_shm_ptr->NOF_WORKERS; j++) {
                    if (seen_operators_pids_sim[j]==current_ticket.operator_id)
                        seen=1;
                }
                if (!seen) {
                    seen_operators_pids_sim[seen_operators_sim_counter]=current_ticket.operator_id;
                    seen_operators_pids_today[seen_operators_today_counter]=current_ticket.operator_id;
                    seen_operators_sim_counter++;
                    seen_operators_today_counter++;
                }else{
                    seen=0;
                    for (int j =0; !seen && j<config_shm_ptr->NOF_WORKERS; j++) {
                        if (seen_operators_pids_today[j]==current_ticket.operator_id)
                            seen=1;
                    }
                    if (!seen) {
                        seen_operators_pids_today[seen_operators_today_counter]=current_ticket.operator_id;
                        seen_operators_today_counter++;

                    }else
                        seen=0;
                }

                //3 e 5 e supporto ad altre
                total_ticket_served_per_day[current_ticket.day_number]++;

                //7 e 8
                user_waiting_time_sum_per_day[current_ticket.day_number]+=current_ticket.time_taken - (current_ticket.actual_deliver_time*((double)config_shm_ptr->N_NANO_SECS)/1000000000);

                //9 e 10
                service_serving_time_sum_per_day[current_ticket.day_number]+=current_ticket.actual_deliver_time*(double)config_shm_ptr->N_NANO_SECS/1000000000;

            }
        }
    }
    double user_waiting_time_sum=0;
    for (int i=0; i< config_shm_ptr->SIM_DURATION;i++) {
        user_waiting_time_sum+=user_waiting_time_sum_per_day[i];
    }
    int total_ticket_served=0;
    for (int i=0; i< config_shm_ptr->SIM_DURATION;i++) {
        total_ticket_served+=total_ticket_served_per_day[i];
    }
    double service_serving_time_sum=0;
    for (int i=0; i< config_shm_ptr->SIM_DURATION;i++) {
        service_serving_time_sum+=service_serving_time_sum_per_day[i];
    }
    if (service==NUM_SERVIZI+1) {
        //14
        Break_message bm;

        while (msgrcv(break_mgq_id, &bm, sizeof(bm)-sizeof(bm.mtype), 0, IPC_NOWAIT) != -1)
            operators_pauses_sim++;

    }


    printf("\n============ GIORNO %d ============\n", today);

    printf("\nUtenti/Servizi\n");
    printf("1. Utenti serviti ‑ tot simulazione       : %d\n", seen_users_sim_counter);
    printf("2. Utenti serviti ‑ media/giorno          : %.2f\n", today       ? (double) seen_users_sim_counter /today : 0.0);
    printf("3. Servizi erogati ‑ tot simulazione      : %d\n", total_ticket_served);
    printf("4. Servizi NON erogati ‑ tot simulazione  : %d\n", total_ticket_unserved);
    printf("5. Servizi erogati ‑ media/giorno         : %.2f\n", today      ? (double) total_ticket_served / today: 0.0);
    printf("6. Servizi NON erogati ‑ media/giorno     : %.2f\n", today       ? (double) total_ticket_unserved / today : 0.0);

    printf("\nTempi medi\n");
    printf("7. Tempo medio attesa utenti ‑ simulazione       : %f s\n", total_ticket_served       ? user_waiting_time_sum / total_ticket_served : 0.0);
    printf("8. Tempo medio attesa utenti ‑ giornata          : %f s\n", total_ticket_served_per_day[today-1]       ? user_waiting_time_sum_per_day[today-1] / total_ticket_served_per_day[today-1] : 0.0);
    printf("9. Tempo medio erogazione servizi ‑ simulazione   : %f s\n", total_ticket_served       ? service_serving_time_sum / total_ticket_served : 0.0);
    printf("10. Tempo medio erogazione servizi ‑ giornata      : %f s\n", total_ticket_served_per_day[today-1]       ? service_serving_time_sum_per_day[today-1] / total_ticket_served_per_day[today-1] : 0.0);

    //solo statistiche generali
    if (service==NUM_SERVIZI+1) {
       printf("\n12. Operatori attivi ‑ giornata            : %f\n", (double)seen_operators_today_counter/today);
       printf("13. Operatori attivi ‑ simulazione         : %d\n", seen_operators_today_counter);
        printf("14a. Pause ‑ media/giorno                   : %f\n", today ? (double)operators_pauses_sim/today : 0.0);
        printf("14b. Pause ‑ tot simulazione                : %d\n", operators_pauses_sim);

        printf("\n ---  15.  --- Rapporto operatori / sportello dello stesso servizio(giornata) ---\n");
        /*//questo è quello che ci è sembrato essere richiesto dalla traccia ma essendo poco chiaro potrebbe non essere ciò che era richiesto
        for (int seat = 0; seat < config_shm_ptr->NOF_WORKER_SEATS; seat++)
            printf(" Sportello %2d erogante servizio %d:  %d sportelli dello stesso servizio / %d operatori abilitati ad erogare il servizio %d\n", seat,seats_shm_ptr[seat].service_type, seats_per_service[seats_shm_ptr[seat].service_type],op_per_service[seats_shm_ptr[seat].service_type],seats_shm_ptr[seat].service_type);
    */
        //la print sotto stampa invece quello che secondo noi era l'idea originare di questa analitica
        for (int st = 0; st < NUM_SERVIZI; st++)
            printf(" Servizio  %d:  %d sportelli dello stesso servizio / %d operatori abilitati ad erogare il servizio %d\n", st, seats_per_service[st],op_per_service[st],st);

    }
}
void compute_analytics() {

    for (int i =0; i<config_shm_ptr->NOF_WORKER_SEATS;i++){
        seats_per_service[seats_shm_ptr[i].service_type]++;
    }

    Ticket bucket_per_service[NUM_SERVIZI+2][config_shm_ptr->NOF_USERS*config_shm_ptr->SIM_DURATION];

    for (int i =0; i< config_shm_ptr->NOF_USERS*config_shm_ptr->SIM_DURATION;i++) {
        bucket_per_service[NUM_SERVIZI+1][i]=tickets_bucket_shm_ptr[i]; //array contenente tutti i ticket
        bucket_per_service[tickets_bucket_shm_ptr[i].service_type][i]=tickets_bucket_shm_ptr[i];//rimarranno dei buchi ma saranno controllati dopo con meno sforzo rispetto a riempirli
    }
    printf("\n\n========= STATISTICHE ‑ GENERALI  =========\n");
    compute_analytics_wrapper(bucket_per_service[NUM_SERVIZI+1],NUM_SERVIZI+1);
    for (int i=0; i<NUM_SERVIZI; i++) {
    printf("\n\n========= STATISTICHE ‑ SERVIZIO %d =========\n",i);
        free_temp_analytics();
        setup_temp_analytics();
        compute_analytics_wrapper(bucket_per_service[i],i);
    }

}
void free_temp_analytics() {
    free(seen_operators_pids_today);
    free(seats_per_service);
}
void free_analytics() {
free_temp_analytics();
    free(total_ticket_served_per_day);
    free(user_waiting_time_sum_per_day);
    free(service_serving_time_sum_per_day);
    free(op_per_service);
    free(seen_operators_pids_sim);
    free(seen_users_sim);
}

int main (int argc, char *argv[]){
    //sezione: lettura argomenti
    setup_config();

    if (argc != 2) {
        fprintf(stderr, "[USAGE] %s <percorso_file_config>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    FILE *config_file = fopen(argv[1], "r");
    if (config_file == NULL) {
        perror("Failed to open config file");
        exit(EXIT_FAILURE);
    }
    load_config(config_file);

    printf("%d", getpid());
    setup_simulation();
    printf("[DEBUG] La simulazione sta per essere avviata.\n");
    debug__print__configs();
    int days_passed;
    for (days_passed = 0; days_passed < config_shm_ptr->SIM_DURATION; days_passed++) {

        randomize_seats_service();
        //debug__print__todays__seats__service();

        wait_to_all_children_be_ready();

        printf("\n==============================\n==============================\n\n Giorno %d iniziato.\n \n==============================\n==============================\n", days_passed);

        nanosleep(&daily_woking_time, NULL);

        config_shm_ptr->current_day =days_passed;
        compute_analytics();

        reset_resources();
        notify_day_ended();

        struct timespec flush_delay = { .tv_sec = 0, .tv_nsec = 50 * 1000000 };
        nanosleep(&flush_delay, NULL);

        printf("\n==============================\n==============================\n\n Giorno %d terminato.\n \n==============================\n==============================\n", days_passed);


        check_explode_threshold();


    }

    printf("[DEBUG] Simulazione terminata.\n");

    //analytics_finalize();
    term_children();
    free_memory();

    print_end_simulation_output("timeout",days_passed-1);


    return 0;
}
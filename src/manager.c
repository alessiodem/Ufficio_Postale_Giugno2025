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
#include "../lib/analytics.h"

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
int break_mgq_id = -1;
int tickets_bucket_sem_id;

//PROTOTIPI FUNZIONI
void term_children();
void free_memory();

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
//TODO: questo non funziona senza dei permessi particolari e mi rompo il cazzo a trovare come averli, decidere se eliminare questa funzione
void debug__print__process__life() {
    pid_t pid = getpid();
    char command[256];

    // Use xterm to run strace on the current PID
    snprintf(command, sizeof(command),
             "xterm -hold -e 'strace -p %d' &", pid);

    int ret = system(command);
    if (ret != 0) {
        perror("Failed to launch xterm");
    }

}

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
    break_mgq_id = msgget(KEY_BREAK_MGQ, EXCLUSIVE_CREATE_FLAG);
    if (break_mgq_id == -1) {
        perror("Errore nella creazione della message queue le pause effettuate");
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
}


//todo: definire una volta per tutte la durata delle giornate
void compute_daytime(){
    daily_woking_time.tv_sec=SECS_FOR_A_DAY;
    daily_woking_time.tv_nsec=NSECS_FOR_A_DAY;
}
void create_seats() {
    //printf("[DEBUG] Creazione posti...\n");
    for (int i = 0; i < config_shm_ptr->NOF_WORKER_SEATS; i++) {
        seats_shm_ptr[i].service_type = get_random_service_type();// todo: questariga si pootrebbe eliminare ed inizializzare i service_type  con randomixe_service_type ad inizio del ciclo del main
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

void setup_simulation(){
    srand(time(NULL)*getpid());
    compute_daytime();
    setup_ipcs();
    //todo: risolvere il problema sotto
    //Il modulo analytics ha bisogno della message‑queue pause prontaprima che i worker vengano creati, quindi lo inizializziamo ora.
    analytics_init();
    printf("[DEBUG] Durata di un giorno: %ld secondi e %ld nanosecondi\n", daily_woking_time.tv_sec, daily_woking_time.tv_nsec);
    create_seats();
    create_workers();
    create_users();
    create_ticket_dispenser();
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
    for (int i = 0;i<config_shm_ptr->NOF_USERS && tickets_bucket_shm_ptr[i].end_time.tv_nsec==0 && tickets_bucket_shm_ptr[i].end_time.tv_sec==0;i++) {
        users_waiting++;
        if (users_waiting> config_shm_ptr->EXPLODE_THRESHOLD) {
            term_children();
            free_memory();
            print_end_simulation_output("EXPLODE THRESHOLD",days_passed-1);
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
    while (msgrcv(ticket_request_msg_id, &trm, sizeof(trm)-sizeof(trm.mtype), 0, IPC_NOWAIT) != -1);
    if (errno != ENOMSG) {
        perror("[ERRORE] Errore nello svuotamento della message queue ticket_request_msg_id");
    }
    Ticket_tbe_message ttbemsg; //todo: testare
    while (msgrcv(tickets_tbe_mgq_id, &ttbemsg, sizeof(ttbemsg)-sizeof(ttbemsg.mtype), 0, IPC_NOWAIT) != -1);
    if (errno != ENOMSG) {
        perror("[ERRORE] Errore nello svuotamento della message queue tickets_tbe_mgq_id");
    }

    //i semafori dei seats vengono resettati dai workers quando ricevono ENDDAY

    //printf("[DEBUG] Direttore: risorse pulite");
}
void free_memory() {

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
    //todo: capire se bisognas prima liminare i messaggi dalla queue

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


int main (int argc, char *argv[]){
    //print_process_life();// se non funziona commenta questa riga

    //Alessandro ha aggiunto un metodo per pulire le ipc in questa riga, non so se vada effettivamente aggiunto perché il programma dovrebbe fare questo tipo di puliziadopo la fine della simulazione ma prima della fine dell'esecuzione del amager in modo da non lasciare risorse allocate quando non servono
    //è però possibile che la simulazione non venga terminata correttamente quindi forse ha senso inserirla
    //todo: discutere sui commenti sopra

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
        debug__print__todays__seats__service();

        wait_to_all_children_be_ready();
        printf("[DEBUG] Giorno %d iniziato.\n", days_passed);
        nanosleep(&daily_woking_time, NULL);

        reset_resources();
        notify_day_ended();

        analytics_compute(days_passed);
        analytics_print(days_passed);

        check_explode_threshold();
        randomize_seats_service();



        //read_and_print_analytics(); //todo:implementare dopo che abbiamo la gestione delle erogazioni

        printf("\n==============================\n==============================\n\n [DEBUG] Giorno %d terminato.\n \n==============================\n==============================\n", days_passed);
    }

    printf("[DEBUG] Simulazione terminata.\n");

    analytics_finalize();
    term_children();
    free_memory();

    print_end_simulation_output("NESSUN ERRORE",days_passed-1);


    return 0;
}
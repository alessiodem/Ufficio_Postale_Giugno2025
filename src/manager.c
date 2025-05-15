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
#include "sem_handling.h"
#include "utils.h"

//VARIABILI GLOBALI
struct timespec ts;
int no_children = 0;
pid_t *child_pids = NULL;

//IPCs
Config *config_shm_ptr;
Seat *seats_shm_ptr;
Ticket *tickets_bucket_shm_ptr;
int children_ready_sync_sem_id;
int children_go_sync_sem_id;
int ticket_request_msg_id;
int tickets_tbe_mgq_id;//tbe= to be erogated

//PROTOTIPI FUNZIONI

//FUNZIONI  AUSILIARIE
// Funzione per aggiungere un PID alla lista dinamica
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
void __debug__print_todays_seats_service(){
    for (int i =0;  i<config_shm_ptr->NOF_WORKER_SEATS;i++)
        printf("[DEBUG]Lo sportello %d puo' erogare il servizio %d\n", i,seats_shm_ptr[i].service_type);
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
        perror("[ERROR] shmat() per seats_shm fallito");
        exit(EXIT_FAILURE);
    }
    int tickets_bucket_shm_id = shmget(KEY_TICKETS_BUCKET_SHM, sizeof(Ticket) * config_shm_ptr->NOF_USERS*config_shm_ptr->SIM_DURATION, EXCLUSIVE_CREATE_FLAG);
    if (tickets_bucket_shm_id == -1) {
        perror("Errore nella creazione della memoria condivisa per i posti");
        exit(EXIT_FAILURE);
    }
    tickets_bucket_shm_ptr = shmat(tickets_bucket_shm_id, NULL, 0);
    if (tickets_bucket_shm_ptr == (void *)-1) {
        perror("[ERROR] shmat() per seats_shm fallito");
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
    int incorrect_config = 0;
    int read_lines = 0;

    char *line = malloc(LINE_BUFFER_SIZE * sizeof(char));
    //TEST_CALL_PTR_RETURN(line)

    while (fgets(line, LINE_BUFFER_SIZE, config_file) != NULL) {
        char *name = strtok(line, " ");
        char *value = strtok(NULL, " ");
        int value_int = strtoul(value, NULL, 10);
        double value_double = strtod(value, NULL);
        if ( value_int < 0 || value_double<0 || name == NULL || value == NULL) {
            incorrect_config = 1;
            fclose(config_file);
            exit(EXIT_FAILURE);
        }
        if (strcmp(name, "NOF_WORKERS") == 0) config_shm_ptr->NOF_WORKERS = value_int;
        else if (strcmp(name, "NOF_USERS") == 0) config_shm_ptr->NOF_USERS = value_int;
        else if (strcmp(name, "NOF_WORKER_SEATS") == 0) config_shm_ptr->NOF_WORKER_SEATS = value_int;
        else if (strcmp(name, "SIM_DURATION") == 0) config_shm_ptr->SIM_DURATION = value_int;
        else if (strcmp(name, "P_SERV_MIN") == 0) config_shm_ptr->P_SERV_MIN = value_double;
        else if (strcmp(name, "P_SERV_MAX") == 0) config_shm_ptr->P_SERV_MAX = value_double;
        else if (strcmp(name, "N_NANO_SECS") == 0) config_shm_ptr->N_NANO_SECS = value_int;
        else if (strcmp(name, "NOF_PAUSE") == 0) config_shm_ptr->NOF_PAUSE = value_int;

        else {
            incorrect_config = 1;
            break;
        }
        read_lines++;
    }
    free(line);
    if (incorrect_config == 1 ||config_shm_ptr->P_SERV_MAX>1||config_shm_ptr->P_SERV_MIN>config_shm_ptr->P_SERV_MAX||read_lines < 8) { //non può essere <0 per i controlli sopra
        fclose(config_file);
        printf("Errore nella lettura config");
        exit(EXIT_FAILURE);
    }
}

//todo: test
void compute_daytime(){
    int secs_for_a_day = SECS_FOR_A_DAY;
    int nsecs_for_a_day = NSECS_FOR_A_DAY;
    ts.tv_sec=secs_for_a_day;
    ts.tv_nsec=nsecs_for_a_day;
}
void create_seats() {
    //printf("[DEBUG] Creazione posti...\n");
    for (int i = 0; i < config_shm_ptr->NOF_WORKER_SEATS; i++) {
        seats_shm_ptr[i].service_type = get_random_service_type();
        seats_shm_ptr[i].worker_sem_id = create_semaphore_and_setval(IPC_PRIVATE,1,0666|IPC_CREAT,1);//todo: valutare la possibilità di gestire i semafori come array di semafori (viene fatto nativamente cambiando nsems)
    }
    printf("[DEBUG] Sportelli creati.\n");
}

void create_workers() {
    //printf("[DEBUG] Creazione workers...\n");

    char child_arg_index[16];
    char *child_argv[2];
    child_argv[0] = "worker";
    child_argv[1] = NULL;

    for (int worker = 0; worker < config_shm_ptr->NOF_WORKERS; worker++)
        fork_and_execute("./worker", child_argv);
    printf("[DEBUG] Workers creati.\n");
}
void create_users() {
    //printf("[DEBUG] Creazione utenti...\n");

    char child_arg_index[16];
    char *child_argv[2];
    child_argv[0] = "user";
    child_argv[1] = NULL;

    for (int user = 0; user < config_shm_ptr->NOF_USERS; user++)
        fork_and_execute("./user", child_argv);
    printf("[DEBUG] Utenti creati.\n");
}
void create_ticket_dispenser(){
    //printf("[DEBUG] Creazione Ticket_dispenser...\n");

    char *child_argv[2];
    child_argv[0] = "ticket_dispenser";
    child_argv[1] = NULL;

    fork_and_execute("./ticket_dispenser", child_argv);
    printf("[DEBUG] Ticket dispenser creato.\n");
}

void setup_simulation(){
    srand(time(NULL));
    compute_daytime();
    setup_ipcs();
    printf("[DEBUG] Durata di un giorno: %ld secondi e %ld nanosecondi\n", ts.tv_sec, ts.tv_nsec);
    create_seats();
    create_workers();
    create_users();
    create_ticket_dispenser();
}
void randomize_seats_service(){
    for (int i = 0; i < config_shm_ptr->NOF_WORKER_SEATS; i++)
        seats_shm_ptr[i].service_type=get_random_service_type();
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
    for (int i =0; i<no_children; i++)
        kill(child_pids[i], ENDEDDAY);
    //todo: contare gli utenti  ancora in coda per l'explode threshld
    //potrebbe non essere il punto giusto per il todo sopra.
    // da implementare quando sappiamo come sono gestite le erogazioni
}

//FUNZIONI DI PULIZIA
void reset_resources(){
    //todo: pulirelr sltre risorse

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
    Ticket_tbe_message ttbemsg;
    while (msgrcv(tickets_tbe_mgq_id, &ttbemsg, sizeof(ttbemsg)-sizeof(ttbemsg.mtype), 0, IPC_NOWAIT) != -1);
    if (errno != ENOMSG) {
        perror("[ERRORE] Errore nello svuotamento della message queue tickets_tbe_mgq_id");
    }


}
void free_memory() {
    // Libera memoria allocata dinamicamente
    free(child_pids);

    // Distacco dalle memorie condivise
    shmdt(config_shm_ptr);
    shmdt(seats_shm_ptr);

    // Rimozione semafori di sincronizzazione
    semctl(children_ready_sync_sem_id, 0, IPC_RMID);
    semctl(children_go_sync_sem_id, 0, IPC_RMID);

    // Rimozione delle message queue
    msgctl(ticket_request_msg_id, IPC_RMID, NULL);
    msgctl(tickets_tbe_mgq_id, IPC_RMID, NULL);

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
    //todo: deallocare i semafori nei seats

    printf("[DEBUG] Risorse IPC deallocate correttamente\n");
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


    //Alessandro ha aggiunto un metodo per pulire le ipc in questa riga, non so se vada effettivamente aggiunto perché il programma dovrebbe fare questo tipo di puliziadopo la fine della simulazione ma prima della fine dell'esecuzione del amager in modo da non lasciare risorse allocate quando non servono
    //è però possibile che la simulazione non venga terminata correttamente quindi forse ha senso inserirla
    //todo: discutere sui commenti sopra
    //sezione: lettura argomenti
    //todo: TOTEST
    setup_config();
    if (argc != 2) {
        fprintf(stderr, "[USAGE] %s <percorso_file_config>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //todo: TOTEST
    FILE *config_file = fopen(argv[1], "r");
    if (config_file == NULL) {
        perror("Failed to open config file");
        exit(EXIT_FAILURE);
    }
    load_config(config_file);

    setup_simulation();


    printf("[DEBUG] La simulazione sta per essere avviata.\n");

    for (int days_passed = 0; days_passed < config_shm_ptr->SIM_DURATION; days_passed++) {
        __debug__print_todays_seats_service();
        //todo: TOTEST
        wait_to_all_children_be_ready();
        printf("[DEBUG] Giorno %d iniziato.\n", days_passed);
        nanosleep(&ts, NULL);
        //todo: TOTEST
        notify_day_ended();
        //todo: TOTEST
        randomize_seats_service();
        //todo: TOTEST
        reset_resources();
        //todo: aggiungere lettura analytics dove necessario
        //read_and_print_analytics(); //todo:impolementare dopo che abbiamo la gestione delle erogazioni
        printf("[DEBUG] Giorno %d terminato.\n", days_passed);
    }
        printf("[DEBUG] Simulazione terminata.\n");

    //todo: TOTEST
    term_children();
    //todo: TOTEST
    free_memory();


    return 0;
}

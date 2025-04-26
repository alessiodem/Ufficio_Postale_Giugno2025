#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "sem_handling.h"
#include "utils.h"

//VARIABILI GLOBALI
Config *config_shm_ptr;
struct timespec ts;
Seat *seats_shm_ptr;
int no_children = 0;
pid_t *child_pids = NULL;

//FUNZIONI  AUSILIARIE
//todo: LA FUNZIONE SOTTO potrebbe ESSERE SEMPLIFICATA ESSENDO CHE CONOSCIAMO APRIORI IL NUMERO DEI FIGLI TOTALI, unico prolema è che dobbiamo prma leggere le conf quindi non possiamo inizializzare l'array con la dimensione, si può  fare ma già così worka bene
//
// Funzione per aggiungere un PID alla lista dinamica
void add_child_pid(pid_t child_pid) {
    //printf("[DEBUG] Aggiunta del PID %d alla lista dei figli...\n", child_pid);
    pid_t *temp = realloc(child_pids, (no_children + 1) * sizeof(pid_t));
    if (temp == NULL) {
        perror("Allocazione memoria per PIDs fallita");
        exit(EXIT_FAILURE);
    }
    child_pids = temp;
    child_pids[no_children++] = child_pid;
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


//FUNZIONI DI SETUP DELLA SIMULAZIONE
void setup_ipcs() {
    //todo: aggiungere passo passo le inizializzazioni che servono

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
        if (value_int < 0||value_double<0) {
            incorrect_config = 1;
            break;
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
    }
}

//todo: test
void compute_daytime(){
    int secs_for_a_day = (1440 * config_shm_ptr->N_NANO_SECS) / 1000000000;
    int nsecs_for_a_day = (1440 * config_shm_ptr->N_NANO_SECS) % 1000000000;
    struct timespec ts;
    ts.tv_sec=secs_for_a_day;
    ts.tv_nsec=nsecs_for_a_day;
}
void create_seats() {
    //printf("[DEBUG] Creazione posti...\n");
    for (int i = 0; i < config_shm_ptr->NOF_WORKER_SEATS; i++) {
        seats_shm_ptr[i].service_type = get_random_service_type();
        seats_shm_ptr[i].worker_sem_id = create_semaphore_and_setval(IPC_PRIVATE,1,0666|IPC_CREAT,1);//todo: valutare la possibilità di gestire i semafori come array di semafori (viene fatto nativamente cambiando nsems)
        seats_shm_ptr[i].user_sem_id = create_semaphore_and_setval(IPC_PRIVATE,1,0666|IPC_CREAT,1);//todo: stessa cosa di worker_sem
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
//FUNZIONI SMANTELLAMENTO SIMULAZIONE
void free_memory() {

}

int main (int argc, char *argv[]){


    //Alessandro ha agigunto un metodo per pulire le ipc in questa riga, non so se vada effettivamente aggiunto perché il programma dovrebbe fare questo tipo di puliziadopo la fine della simulazione ma prima della fine dell'esecuzione del amager in modo da non lasciare risorse allocate quando non servono
    //è però possibile che la simulazione non venga terminata correttamente quindi forse ha senso inserirla
    //todo: discutere sui commenti sopra
    //sezione: lettura argomenti
    //todo: TOTEST
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <config_file_path>\n", argv[0]);//todo: cambiare messaggio di errore
        exit(EXIT_FAILURE);
    }

    //todo: TOTEST
    FILE *config_file = fopen(argv[1], "r");
    if (config_file == NULL) {
        perror("Failed to open config file");
        exit(EXIT_FAILURE);
    }
    load_config(config_file);

    printf("[DEBUG] Simulazione avviata.\n");

    //sezione: setup risorse e processi
    setup_simulation();


}
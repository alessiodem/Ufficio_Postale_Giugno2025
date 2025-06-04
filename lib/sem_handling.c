#include "sem_handling.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <errno.h>

// Funzione per creare un semaforo
int create_semaphore(key_t key, int num_sems, int sem_flags) {
    int sem_id = semget(key, num_sems, sem_flags);
    if (sem_id == -1) {
        perror("Error creating semaphore");
        return -1;
    }
    return sem_id;
}
int create_semaphore_and_setval(key_t key, int num_sems, int sem_flags, int val) {
    int sem_id = semget(key, num_sems, sem_flags);
    if (sem_id == -1) {
        perror("Error creating semaphore");
        return -1;
    }
    if (semctl(sem_id, 0, SETVAL, val) == -1) {
        perror("semctl SETVAL fallita");
        exit(EXIT_FAILURE);
    }
    return sem_id;
}
int create_timed_semaphore_and_setval(key_t key, int num_sems, int sem_flags, int val) {
    int sem_id = semget(key, num_sems, sem_flags);
    if (sem_id == -1) {
        perror("Error creating timed semaphore");
        return -1;
    }
    if (semctl(sem_id, 0, SETVAL, val) == -1) {
        perror("semctl SETVAL per timed semaphore fallita");
        exit(EXIT_FAILURE);
    }
    return sem_id;
}

// Funzione per ottenere l'ID di un semaforo esistente
int get_semaphore(key_t key, int num_sems) {
    int sem_id = semget(key, num_sems, 0);
    if (sem_id == -1) {
        perror("Error getting semaphore");
        return -1;
    }
    return sem_id;
}

// Funzione per eseguire la "wait" (P operation) su un semaforo
void semaphore_decrement(int sem_id) {
    //printf("[DEBUG] sto eseguendo decrement\n");
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = -1; // Decrementa il semaforo
    op.sem_flg = 0;

    if (semop(sem_id, &op, 1) == -1) {
        perror("Error during semaphore wait");
        exit(EXIT_FAILURE);
    }
}

// Funzione per eseguire la "signal" (V operation) su un semaforo
void semaphore_increment(int sem_id) {
    //printf("[DEBUG] sto eseguendo increment\n");
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = 1; // Incrementa il semaforo
    op.sem_flg = 0;

    if (semop(sem_id, &op, 1) == -1) {
        perror("Error during increment semaphore signal");
        exit(EXIT_FAILURE);
    }
}
int semaphore_do(int sem_id,int custom_operation) {
    //printf("[DEBUG] sto eseguendo do\n");
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = custom_operation; // Incrementa il semaforo
    op.sem_flg = 0;

    int semop_return=semop(sem_id, &op, 1);
    if (semop_return == -1) {
        perror("Error during do semaphore signal");
        exit(EXIT_FAILURE);
    }
    return semop_return;
}
int semaphore_do_not_wait(int sem_id,int custom_operation) {
    //printf("[DEBUG] sto eseguendo do not wait");

    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = custom_operation; 
    op.sem_flg = IPC_NOWAIT;

    int semop_return=semop(sem_id, &op, 1);
    if (semop_return == -1&& errno!=EAGAIN) {
        perror("Error during do not wait semaphore signal");
        exit(EXIT_FAILURE);
    }
    //printf("semop valore ritorno :%d/n", semop_return);
    return semop_return;
}

// Funzione per rimuovere un set di semafori
void remove_semaphore(int sem_id) {
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Error removing semaphore");
        exit(EXIT_FAILURE);
    }
}

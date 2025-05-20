#ifndef SEM_HANDLING_H
#define SEM_HANDLING_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

int create_semaphore(key_t key, int num_sems, int sem_flags);
int create_semaphore_and_setval(key_t key, int num_sems, int sem_flags, int val);
int create_timed_semaphore_and_setval(key_t key, int num_sems, int sem_flags, int val);
int get_semaphore(key_t key, int num_sems);
void semaphore_decrement(int sem_id);
void semaphore_increment(int sem_id);
int semaphore_do(int sem_id, int custom_operation);
int semaphore_do_not_wait(int sem_id, int custom_operation);
void remove_semaphore(int sem_id);
#endif // SEM_HANDLING_H

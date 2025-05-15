#include "utils.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

const Service services[] = {
    {PACCHI, 10},
    {LETTERE, 8},
    {BANCOPOSTA, 6},
    {BOLLETTINI, 8},
    {PROD_FINANZIARI, 20},
    {OROLOGI, 20}
};
int get_average_time(ServiceType st){
    return services[st].average_time;
}


Service get_random_service() {
    return services[rand() % NUM_SERVIZI];
}

ServiceType get_random_service_type() {
    return (ServiceType)(rand() % NUM_SERVIZI);
}
void print_ticket_debug( Ticket ticket) {
    

    printf("===== Ticket Debug Info =====\n");
    printf("Numero: %d\n", ticket.ticket_index);
    printf("Tipo di Servizio: %d\n", ticket.service_type);
    printf("Tempo Stimato: %d secondi\n", ticket.actual_time);
    printf("Indice Posto: %d\n", ticket.seat_index);
    printf("Stato: %s\n", ticket.is_done ? "Completato" : "In Attesa");
    printf("=============================\n");
}

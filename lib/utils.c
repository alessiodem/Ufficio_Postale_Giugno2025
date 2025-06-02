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

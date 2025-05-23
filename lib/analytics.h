#ifndef ANALYTICS_H
#define ANALYTICS_H

#include "../src/common.h"   // ho messo il percorso perche' solo il nome del file non gli piaceva
#include <stdbool.h>
#include <stddef.h>

//  Strutture dati pubbliche

// Statistiche elementari per una singola tipologia di servizio 
typedef struct {
    long   served;        //utenti serviti                  
    long   not_served;    //servizi non erogati              
    double tot_wait;      //somma tempi d’attesa  
    double tot_service;   //somma tempi d’erogazione     
} ServiceStats;

//Statistiche aggregate di una giornata
typedef struct {
    ServiceStats by_service[NUM_SERVIZI]; //breakdown per servizio

    long unique_operators;   //operatori che hanno lavorato (pid unici)
    long occupied_seats;     //sportelli occupati ≥ una volta 
    long pauses;             //pause effettuate dagli operatori
} DayStats;

//API del modulo analytics
   

//Inizializza le strutture interne
void analytics_init(void);

//Calcola le statistiche della giornata corrente e aggiorna
void analytics_compute(int current_day);

//Stampa su stdout il report della giornata appena conclusa
void analytics_print(int current_day);

//Libera eventuali risorse allocate dal modulo.
void analytics_finalize(void);

//Accesso read‑only ai dati raccolti
const DayStats *analytics_get_today(void);
const DayStats *analytics_get_total(void);

#endif /* ANALYTICS_H */
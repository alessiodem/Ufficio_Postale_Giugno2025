/*
Nome                Cosa rappresenta
today_stats         Le statistiche del giorno in corso
total_stats         Le statistiche accumulate su più giorni
seen_operator_sim   Un array che dice quali operatori sono stati visti durante tutta la simulazione
ops_per_seat        Quanti operatori hanno lavorato a ogni sportello (oggi)
seen_op_seat        Tabella per sapere se un certo operatore ha lavorato a un certo sportello (oggi)
break_mq_id         L’ID della message queue usata dagli operatori per richiedere pause


FUNZIONE                                COSA FA
analytics_init()                    	•	Azzera tutte le statistiche (today_stats, total_stats).
	                                    •	Alloca la memoria per seen_operator_sim, che tiene traccia di quali operatori hanno partecipato durante la simulazione.
	                                    •	Crea (o apre) una message queue, usata dagli operatori per chiedere pause.

analytics_compute(int current_day)      1.	Azzera le variabili temporanee del giorno (today_stats, ops_per_seat, ecc.).
	                                    2.	Alloca due array temporanei per:
	                                        •	Sapere quali operatori hanno lavorato oggi
	                                        •	Sapere quali sportelli sono stati usati oggi
	                                    3.	Scorre tutti i ticket della simulazione (cioè le richieste degli utenti), e per ognuno:
	                                        •	Se è del giorno current_day e valido:
	                                            •	Se è stato servito:
	                                            •	Calcola quanto ha aspettato e quanto è durato il servizio
	                                            •	Aggiorna le statistiche del giorno
	                                        •	Se non è stato servito, lo segna comunque
	                                        •	Segna quale operatore lo ha servito e a quale sportello
	                                    4.	Consuma tutti i messaggi di pausa (arrivati via coda), e li conta
	                                    5.	Somma le statistiche di oggi dentro quelle totali (giorno dopo giorno)
	                                    6.	Aggiorna anche delle statistiche assolute (es: quante persone servite in tutta la simulazione)
	                                    7.	Libera la memoria temporanea.

analytics_print(int current_day)        •	Stampa un report giornaliero, che include:
	                                        •	Quanti utenti sono stati serviti / non serviti per ogni tipo di servizio
	                                        •	Tempi medi di attesa e servizio
	                                        •	Quanti operatori hanno lavorato e a quali sportelli
	                                        •	Quante pause sono state fatte
	                                    •	Stampa anche un riepilogo di tutta la simulazione fino a quel giorno:
	                                        •	Medie giornaliere
	                                        •	Totali
	                                        •	Indicatori utili (es. percentuali di uso dei posti, pause, ecc.)
*/

#include "analytics.h"
#include "../src/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdbool.h>
#include "sem_handling.h"

//Dipendenze esterne (shared memory create altrove)
extern Ticket  *tickets_bucket_shm_ptr;   //definita in manager.c
extern Config  *config_shm_ptr;           //definita in manager.c
extern int tickets_bucket_sem_id;         //definito in manager.c

static int break_mq_id = -1;

//Stato interno al modulo
static DayStats total_stats;          //giornate totali
static DayStats today_stats;          //giornata corrente

//richieste dalla consegna
static long   days_completed          = 0;   //giornate già chiuse
static long   total_served_all        = 0;   //utenti/servizi serviti
static long   total_not_served_all    = 0;   //servizi non erogati
static double total_wait_all          = 0.0; //somma tempi d’attesa
static double total_service_all       = 0.0; //somma tempi erogazione

//matrice operatori-per-sportello per la giornata
static unsigned ops_per_seat[CONFIG_MAX_SEATS];
static bool     seen_op_seat[CONFIG_MAX_SEATS][CONFIG_MAX_WORKERS]; //chi ha lavorato dove

//operatori visti nella simulazione
static bool *seen_operator_sim = NULL;    //lunghezza NOF_WORKERS


//Trasforma un PID in un indice tra 0 e NOF_WORKERS-1.
static inline
size_t pid_to_index(pid_t p){
    return ((size_t)p) % (size_t)config_shm_ptr->NOF_WORKERS;  // la conversione di pid_t in size_t, quindi in un intero positivo serve perche' anche se raramente i pid possono essere negativi
}

//API
void
analytics_init(void){
    //azzero la memoria
    memset(&total_stats,  0, sizeof(total_stats));
    memset(&today_stats,  0, sizeof(today_stats));

    //array per gli operatori unici sulla simulazione
    seen_operator_sim = calloc(config_shm_ptr->NOF_WORKERS, sizeof(bool));
    if (!seen_operator_sim) {
        perror("calloc seen_operator_sim");
        exit(EXIT_FAILURE);
    }

    //Message queue per le pause
    break_mq_id = msgget(KEY_BREAK_MGQ, IPC_CREAT | 0666);
    if (break_mq_id == -1) {
        perror("msgget break_mq");
    }
}

void
analytics_compute(int current_day){
    //azzera le statistiche della giornata
    memset(&today_stats, 0, sizeof(today_stats));
    memset(ops_per_seat, 0, sizeof(ops_per_seat));
    memset(seen_op_seat, 0, sizeof(seen_op_seat));

    //Copia i valori dalla configurazione (Config) in variabili locali.
    size_t n_workers = (size_t)config_shm_ptr->NOF_WORKERS;
    size_t n_seats   = (size_t)config_shm_ptr->NOF_WORKER_SEATS;

    //Alloca due array temporanei:
    bool *seen_operator_today = calloc(n_workers, sizeof(bool));
    bool *seat_used_today     = calloc(n_seats,   sizeof(bool));    //quali sportelli sono stati usati oggi
    if (!seen_operator_today || !seat_used_today) {
        perror("calloc analytics_compute");
        exit(EXIT_FAILURE);
    }

    //Inizio sezione critica: snapshot dei ticket
    semaphore_decrement(tickets_bucket_sem_id);

    //scansiona il bucket dei ticket 
    size_t bucket_len = (size_t)config_shm_ptr->NOF_USERS *
                        (size_t)config_shm_ptr->SIM_DURATION;

    for (size_t i = 0; i < bucket_len; ++i) {
        Ticket *t = &tickets_bucket_shm_ptr[i];

        //Se il ticket non è mai stato usato, lo salta:
        if (t->request_time.tv_sec == 0 && t->request_time.tv_nsec == 0)
            continue;

        ServiceStats *day_sv  = &today_stats.by_service[t->service_type];   //prende il puntatore alla struttura delle statistiche del servizio usato in un singolo ticket (t), per il giorno corrente (today_stats), e lo memorizza in day_sv per lavorarci più facilmente.

        bool is_today = (t->day_number == current_day);

        //conta utente servito / non servito
        if (t->is_done) {       //vale true se il ticket è stato servito da un operatore.
            //tempo di attesa: total - erogazione
            double wait_time = t->time_taken - t->actual_time;
            double service_t = t->actual_time;

            if (is_today) {
                ++day_sv->served;
                day_sv->tot_wait    += wait_time;
                day_sv->tot_service += service_t;
            }
        } else {
            if (is_today)
                ++day_sv->not_served;
        }

        //operatori & sportelli
        if (t->operator_id > 0) {
            size_t idx = pid_to_index(t->operator_id);

                //Se questo operatore non era ancora stato visto a quel seat (sportello), lo segniamo e aumentiamo il numero di operatori su quel seat.
                        int seat = t->seat_index;
            if (seat >= 0 && seat < (int)n_seats) {
                if (!seen_op_seat[seat][idx]) {
                    seen_op_seat[seat][idx] = true;
                    ++ops_per_seat[seat];
                }
            }
                //Se questo operatore non è ancora stato conteggiato oggi, lo aggiungiamo.
            if (!seen_operator_today[idx]) {
                seen_operator_today[idx] = true;
                ++today_stats.unique_operators;
            }
                //Se è la prima volta che vediamo questo operatore in tutta la simulazione, aggiorniamo anche le statistiche totali.
            if (!seen_operator_sim[idx]) {
                seen_operator_sim[idx] = true;
                ++total_stats.unique_operators;
            }
        }

                //Tiene traccia di quanti sportelli sono stati effettivamente usati oggi.
        if (t->seat_index >= 0 && (size_t)t->seat_index < n_seats) {
            if (!seat_used_today[t->seat_index]) {
                seat_used_today[t->seat_index] = true;
                ++today_stats.occupied_seats;
            }
        }
    }

    //Fine sezione critica: rilascio lock
    semaphore_increment(tickets_bucket_sem_id);
    //consuma tutti i messaggi di pausa arrivati durante la giornata
    if (break_mq_id != -1) {
        Break_message bm;
        //leggi fino a svuotare
        while (msgrcv(break_mq_id, &bm, sizeof(pid_t), 0, IPC_NOWAIT) != -1)
            ++today_stats.pauses;

        if (errno != ENOMSG && errno != 0)
            perror("msgrcv break_mq");
    }
    total_stats.pauses += today_stats.pauses;

    //Accumula le statistiche del giorno in quelle totali
    for (int s = 0; s < NUM_SERVIZI; ++s) {
        ServiceStats *d = &today_stats.by_service[s];
        ServiceStats *t = &total_stats.by_service[s];

        t->served      += d->served;
        t->not_served  += d->not_served;
        t->tot_wait    += d->tot_wait;
        t->tot_service += d->tot_service;
    }

    // Aggiorna anche gli aggregati globali
    for (int s = 0; s < NUM_SERVIZI; ++s) {
        ServiceStats *d = &today_stats.by_service[s];
        total_served_all     += d->served;
        total_not_served_all += d->not_served;
        total_wait_all       += d->tot_wait;
        total_service_all    += d->tot_service;
    }
    ++days_completed;

    //ealloca la memoria degli array usati solo per oggi.
    free(seen_operator_today);
    free(seat_used_today);
}

static void __attribute__((unused))
print_service_line(const char *label, const ServiceStats *ss)
{
    printf("%-28s : %6ld | Non serviti: %4ld | "
           "Attesa media: %7.2f s | Erogazione media: %7.2f s\n",
           label,
           ss->served,
           ss->not_served,
           (ss->served ? ss->tot_wait    / ss->served   : 0.0),
           (ss->served ? ss->tot_service / ss->served   : 0.0));
}

void
analytics_print(int current_day)
{
    //1‑6
     
    double avg_served_day     = days_completed       ? (double)total_served_all     / days_completed : 0.0;
    double avg_not_served_day = days_completed       ? (double)total_not_served_all / days_completed : 0.0;
    double avg_wait_sim       = total_served_all     ?  total_wait_all    / total_served_all    : 0.0;
    double avg_service_sim    = total_served_all     ?  total_service_all / total_served_all    : 0.0;

    // metriche dei giorni gia finiti
    long   served_today   = 0;
    long   not_served_today = 0;
    double wait_today     = 0.0;
    double service_today  = 0.0;

    for (int s = 0; s < NUM_SERVIZI; ++s) {
        ServiceStats *d = &today_stats.by_service[s];
        served_today     += d->served;
        not_served_today += d->not_served;
        wait_today       += d->tot_wait;
        service_today    += d->tot_service;
    }

    double avg_wait_day    = served_today ? wait_today    / served_today : 0.0;
    double avg_service_day = served_today ? service_today / served_today : 0.0; 
    
    

    printf("\n========= STATISTICHE ‑ GIORNO %d =========\n", current_day + 1);

    //Utenti/Servizi
    printf("Utenti serviti ‑ tot simulazione       : %ld\n", total_served_all);
    printf("Utenti serviti ‑ media/giorno          : %.2f\n", avg_served_day);
    printf("Servizi erogati ‑ tot simulazione      : %ld\n", total_served_all);
    printf("Servizi NON erogati ‑ tot simulazione  : %ld\n", total_not_served_all);
    printf("Servizi NON erogati - giornata          : %ld\n", not_served_today);
    printf("Servizi erogati ‑ media/giorno         : %.2f\n", avg_served_day);
    printf("Servizi NON erogati ‑ media/giorno     : %.2f\n", avg_not_served_day);

    //Tempi medi
    printf("Tempo medio attesa ‑ simulazione       : %.2f s\n", avg_wait_sim);
    printf("Tempo medio attesa ‑ giornata          : %.2f s\n", avg_wait_day);
    printf("Tempo medio erogazione ‑ simulazione   : %.2f s\n", avg_service_sim);
    printf("Tempo medio erogazione ‑ giornata      : %.2f s\n", avg_service_day);

    //11

    printf("\n--- Breakdown per tipologia di servizio ---\n");
    for (int s = 0; s < NUM_SERVIZI; ++s) {
        ServiceStats *today = &today_stats.by_service[s];
        ServiceStats *tot   = &total_stats.by_service[s];

        double avg_w_today = today->served ? today->tot_wait    / today->served : 0.0;
        double avg_w_tot   = tot->served   ? tot->tot_wait      / tot->served   : 0.0;
        double avg_s_today = today->served ? today->tot_service / today->served : 0.0;
        double avg_s_tot   = tot->served   ? tot->tot_service   / tot->served   : 0.0;

        printf("Servizio %d:\n", s);
        printf("  Utenti serviti     (oggi / sim)      : %ld / %ld\n", today->served,     tot->served);
        printf("  Servizi non erogati (oggi / sim)     : %ld / %ld\n", today->not_served, tot->not_served);
        printf("  Tempo medio attesa  (oggi / sim)     : %.2f / %.2f s\n", avg_w_today, avg_w_tot);
        printf("  Tempo medio servizio(oggi / sim)     : %.2f / %.2f s\n", avg_s_today, avg_s_tot);
    }

    //12 13
    printf("\nOperatori attivi ‑ giornata            : %ld\n", today_stats.unique_operators);
    printf("Operatori attivi ‑ simulazione         : %ld\n", total_stats.unique_operators);

    //14
    double avg_pauses_day = days_completed ? (double)total_stats.pauses / days_completed : 0.0;
    printf("Pause ‑ media/giorno                   : %.2f\n", avg_pauses_day);
    printf("Pause ‑ tot simulazione                : %ld\n", total_stats.pauses);

    //15
    printf("\n--- Rapporto operatori / sportello (giornata) ---\n");
    for (int seat = 0; seat < config_shm_ptr->NOF_WORKER_SEATS; ++seat) {
        printf("Sportello %2d : %u operatori\n", seat, ops_per_seat[seat]);
    }
}


void analytics_finalize(void){
    free(seen_operator_sim);
    seen_operator_sim = NULL;
    //Non rimuoviamo la message‑queue: la può cancellare il manager alla fine
}

const DayStats *analytics_get_today(void){      //const DayStats *: restituisce un puntatore a una struttura DayStats che non può essere modificata da chi la riceve (const)
    return &today_stats;
}

const DayStats *analytics_get_total(void){
    return &total_stats;
}

//Registra un servizio non erogato (utente tornato a casa senza ticket)
void analytics_register_not_served(int s)
{
    ++today_stats.by_service[s].not_served;
    ++total_stats.by_service[s].not_served;
    ++total_not_served_all;
}

//Registra un servizio erogato, con tempi di attesa e di erogazione
void analytics_register_served(int service_type,
                               double service_time,
                               double wait_time);

void analytics_register_served(int s, double service_time, double wait_time)
{
    //Statistiche della giornata
    ++today_stats.by_service[s].served;
    today_stats.by_service[s].tot_wait    += wait_time;
    today_stats.by_service[s].tot_service += service_time;

    //Statistiche totali 
    ++total_stats.by_service[s].served;
    total_stats.by_service[s].tot_wait    += wait_time;
    total_stats.by_service[s].tot_service += service_time;

    ++total_served_all;
    total_wait_all    += wait_time;
    total_service_all += service_time;
}


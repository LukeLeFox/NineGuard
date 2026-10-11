#ifndef NGNETWATCH_H
#define NGNETWATCH_H

/*
 * Watchdog cambio rete (fase 2 full tunnel): logica pura, senza chiamate
 * di sistema, testata su host. NGNetConfig raccoglie i fatti (subnet
 * connesse, RTM_GET, router del servizio fisico da configd) e applica.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NG_WATCH_KEEP = 0,       /* bypass coerente con la rete attuale */
    NG_WATCH_ADD = 1,        /* aggiungere bypass via router */
    NG_WATCH_REPLACE = 2,    /* gateway cambiato o route sparita */
    NG_WATCH_REMOVE = 3,     /* endpoint ora on-link: bypass inutile */
    NG_WATCH_NO_NETWORK = 4  /* nessun router fisico raggiungibile */
};

typedef struct {
    int onlink;                /* endpoint in una subnet connessa non-utun */
    int have_router;           /* router fisico noto e on-link */
    uint32_t router;           /* host order */
    int recorded;              /* bypass nostro registrato */
    uint32_t recorded_gateway; /* host order */
    int kernel_matches;        /* RTM_GET: host route statica via recorded */
} ng_watch_input;

int ng_watch_decide(const ng_watch_input *input);

/* Fallimenti consecutivi (con router presente): 1 se durano da almeno
 * grace secondi. failing_since = 0 quando la situazione e' sana. */
int ng_watch_grace_expired(long *failing_since, int failing, long now,
                           long grace);

#ifdef __cplusplus
}
#endif

#endif

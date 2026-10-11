#ifndef NGNETCONFIG_H
#define NGNETCONFIG_H

#include <netinet/in.h>
#include <net/if.h>
#include "NGProfileUAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NG_NET_OK = 0,
    NG_NET_ERR_ARGS = -30,
    NG_NET_ERR_NO_INTERFACE = -31,
    NG_NET_ERR_ADDRESS = -32,
    NG_NET_ERR_ROUTE = -33,
    NG_NET_ERR_BUSY = -34,
    NG_NET_ERR_NO_LINK = -35,
    NG_NET_ERR_NO_GATEWAY = -36,
    NG_NET_ERR_BYPASS = -37,
    NG_NET_ERR_MTU = -38
};

typedef struct {
    unsigned routes;       /* route verso la utun (host + rete + coppia /1) */
    unsigned bypass;       /* host route endpoint via gateway originale */
    unsigned lan_skipped;  /* route contenute in una subnet connessa */
    int full;              /* coppia 0.0.0.0/1 + 128.0.0.0/1 applicata */
    unsigned mtu;          /* MTU della utun dopo NET_UP (0 = ignota) */
} ng_net_result;

/*
 * Applica, nell'ordine e con rollback completo a ogni errore:
 *  1. IPv4 /32 point-to-point su ifname;
 *  2. per ogni endpoint IPv4 coperto dalle route (sempre in full): host
 *     route endpoint/32 via il gateway attuale (RTM_GET prima di toccare
 *     le route); nessun bypass se l'endpoint e' on-link;
 *  0. MTU del profilo sulla utun (SIOCSIFMTU; wireguard-go la rilegge
 *     via RTM_IFINFO), ripristinata da ng_net_down;
 *  3. route verso ifname per gli AllowedIPs IPv4: /32 host, /1../31 rete,
 *     /0 come coppia 0.0.0.0/1 + 128.0.0.0/1.
 * La default route non viene mai toccata. Route contenute in una subnet
 * direttamente connessa (LAN) vengono saltate. Nessun DNS qui.
 */
int ng_net_up(const char *ifname, const ng_profile_plan *plan,
              ng_net_result *result);

/* Rimuove route, bypass e indirizzo applicati da ng_net_up. Idempotente. */
int ng_net_down(void);

/* All'avvio del daemon: rimuove i bypass registrati nel file di stato da
 * un'istanza precedente terminata senza NET_DOWN. */
void ng_net_recover(void);

int ng_net_is_up(void);
int ng_net_is_full(void);
unsigned ng_net_route_count(void);

typedef struct {
    unsigned replaced;  /* bypass aggiunti o spostati sul nuovo router */
    unsigned removed;   /* bypass tolti: endpoint ora on-link */
    unsigned waiting;   /* endpoint senza router fisico (rete assente) */
    unsigned failed;    /* RTM_ADD fallita: ritentata al giro successivo */
} ng_net_watch_result;

/* Endpoint IPv4 coperti dal tunnel, seguiti dal watchdog (0 se NET down). */
unsigned ng_net_watch_count(void);

/*
 * Watchdog cambio rete: per ogni endpoint coperto confronta subnet
 * connesse, router fisico attuale (da configd, ignorato se non on-link) e
 * host route di bypass nel kernel; sposta, aggiunge o toglie il bypass e
 * aggiorna net.state. NG_NET_OK, NG_NET_ERR_NO_GATEWAY (rete assente:
 * nulla toccato) o NG_NET_ERR_BYPASS (aggiunta fallita).
 */
int ng_net_watch(int have_router, struct in_addr router,
                 ng_net_watch_result *result);

/* RTM_GET: interfaccia e (se RTF_GATEWAY) gateway IPv4 per destination. */
int ng_route_lookup(struct in_addr destination, char ifname[IFNAMSIZ],
                    int *has_gateway, struct in_addr *gateway);

/* Come ng_route_lookup; static_host = 1 se la route trovata e' una host
 * route statica (es. il bypass dell'endpoint). */
int ng_route_lookup_flags(struct in_addr destination, char ifname[IFNAMSIZ],
                          int *has_gateway, struct in_addr *gateway,
                          int *static_host);

/* RTM_DELETE di una host route (solo helper di test). */
int ng_route_delete_host(struct in_addr destination);

#ifdef __cplusplus
}
#endif

#endif

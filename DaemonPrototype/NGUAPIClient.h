#ifndef NGUAPICLIENT_H
#define NGUAPICLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned peers;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    int64_t last_handshake_sec;   /* massimo tra i peer, 0 = mai */
    unsigned allowed_ips;
    unsigned endpoints;
    int has_private_key;
    unsigned listen_port;
} ng_uapi_stats;

/* Errore di trasporto/protocollo (socket assente, timeout, risposta
 * malformata). Distinto dai valori errno UAPI, che wireguard-go riporta
 * come negativi (es. -22 = EINVAL, -5 = EIO). */
#define NG_UAPI_ERR_TRANSPORT (-1000000)

/*
 * Invia un corpo completo "set=1\n...\n\n". Restituisce il valore errno
 * riportato da wireguard-go (0 = successo, negativo = errore UAPI) oppure
 * NG_UAPI_ERR_TRANSPORT. Il buffer di risposta viene azzerato.
 */
int ng_uapi_set(const char *socket_path, const char *body);

/*
 * Esegue "get=1" e riassume lo stato senza mai restituire chiavi.
 * La risposta grezza (che contiene la chiave privata in hex) viene
 * azzerata prima del ritorno. Restituisce errno UAPI o NG_UAPI_ERR_TRANSPORT.
 */
int ng_uapi_get_stats(const char *socket_path, ng_uapi_stats *stats);

/* Esposta per i test: analizza una risposta get=1 gia' letta. */
int ng_uapi_parse_get(const char *response, ng_uapi_stats *stats);

#ifdef __cplusplus
}
#endif

#endif

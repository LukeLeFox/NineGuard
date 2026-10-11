#ifndef NGPROFILEUAPI_H
#define NGPROFILEUAPI_H

/*
 * Conversione portabile (C puro) di un profilo WireGuard gia' validato
 * dal parser in testo UAPI "set=1" per wireguard-go 0.0.20200320.
 *
 * Il testo prodotto contiene chiavi private: il chiamante deve azzerarlo
 * (ng_secure_zero) e non deve mai stamparlo o registrarlo nei log.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NG_MAX_PEERS 32
#define NG_MAX_HOST_ROUTES 32
#define NG_IPV4_TEXT 16
#define NG_UAPI_CAPACITY 65536
#define NG_MAX_NET_ROUTES 32
#define NG_MAX_DNS 4
#define NG_DNS_DOMAIN_TEXT 64

enum {
    NG_PROFILE_OK = 0,
    NG_PROFILE_ERR_ARGS = -1,
    NG_PROFILE_ERR_KEY = -2,
    NG_PROFILE_ERR_PORT = -3,
    NG_PROFILE_ERR_KEEPALIVE = -4,
    NG_PROFILE_ERR_ALLOWED_IP = -5,
    NG_PROFILE_ERR_ENDPOINT = -6,
    NG_PROFILE_ERR_ADDRESS = -7,
    NG_PROFILE_ERR_SPACE = -8,
    NG_PROFILE_ERR_MTU = -9
};

#define NG_MTU_MIN 576
#define NG_MTU_MAX 1500

typedef struct {
    const char *public_key;     /* base64, obbligatoria */
    const char *preshared_key;  /* base64, opzionale */
    const char *endpoint;       /* host:porta o [v6]:porta, opzionale */
    const char *keepalive;      /* secondi, opzionale */
    const char *allowed_ips;    /* lista separata da virgole, obbligatoria */
} ng_peer_input;

typedef struct {
    const char *private_key;    /* base64, obbligatoria */
    const char *listen_port;    /* opzionale */
    const char *address;        /* lista separata da virgole, opzionale */
    const ng_peer_input *peers;
    unsigned peer_count;
    const char *dns;            /* campo DNS (IP e/o domini), opzionale */
    const char *mtu;            /* NG_MTU_MIN..NG_MTU_MAX, opzionale */
} ng_profile_input;

typedef struct {
    unsigned peer_count;
    char ipv4_address[NG_IPV4_TEXT];          /* primo IPv4 di Address */
    unsigned host_route_count;
    char host_routes[NG_MAX_HOST_ROUTES][NG_IPV4_TEXT];
    unsigned skipped_allowed_ips;             /* IPv6, endpoint/32, eccesso */
    unsigned endpoint_count;
    char endpoints_ipv4[NG_MAX_PEERS][NG_IPV4_TEXT];
    /* AllowedIPs IPv4 /1../31 (indirizzo di rete gia' mascherato). */
    unsigned net_route_count;
    char net_routes[NG_MAX_NET_ROUTES][NG_IPV4_TEXT];
    unsigned char net_prefixes[NG_MAX_NET_ROUTES];
    int full_ipv4;                            /* AllowedIPs 0.0.0.0/0 */
    /* DNS del profilo: server IPv4 e domini di ricerca (IPv6 ignorati). */
    unsigned dns_count;
    char dns_servers[NG_MAX_DNS][NG_IPV4_TEXT];
    unsigned search_count;
    char search_domains[NG_MAX_DNS][NG_DNS_DOMAIN_TEXT];
    unsigned mtu;                             /* 0 = default motore (1420) */
} ng_profile_plan;

void ng_secure_zero(void *buffer, size_t length);

/* 44 caratteri base64 -> 32 byte -> 64 caratteri hex minuscoli. */
int ng_b64key_to_hex(const char *b64, char hex[65]);

/*
 * Converte "host:porta" o "[v6]:porta" in un endpoint numerico accettato
 * da conn.parseEndpoint di wireguard-go. Gli hostname vengono risolti con
 * getaddrinfo preferendo IPv4. ipv4_out (opzionale) riceve l'IPv4 usato
 * oppure stringa vuota.
 */
int ng_resolve_endpoint(const char *text, char *out, size_t capacity,
                        char ipv4_out[NG_IPV4_TEXT]);

/*
 * Normalizza un CIDR IPv4/IPv6 (prefisso opzionale: /32 o /128).
 * family_out: 4 o 6.
 */
int ng_normalize_cidr(const char *text, char *out, size_t capacity,
                      int *family_out, unsigned *prefix_out);

/* Costruisce l'intero corpo "set=1\n...\n\n" in uapi (NG_UAPI_CAPACITY). */
int ng_profile_build_uapi(const ng_profile_input *input,
                          char *uapi, size_t capacity,
                          ng_profile_plan *plan);

const char *ng_profile_error_name(int code);

#ifdef __cplusplus
}
#endif

#endif

# ngtestpeer — peer WireGuard esterno controllato

Peer userspace per il gate "peer esterno controllato" di NineGuard 2.0, senza
installare `wg`/`wireguard-tools` e senza root sul PC.

- usa lo stesso `wireguard-go` dell'engine iOS (commit
  `52c834c4469f4f6e33c97c7294faffabfcbf114a`, archivio SHA-256 verificato);
- TUN a canali in memoria (`tun/tuntest`): nessuna interfaccia kernel, nessuna
  route, nessuna regola firewall, nessuna modifica di sistema;
- chiavi effimere generate a ogni avvio, mai stampate;
- risponde solo a ICMP echo e UDP echo (porta 7) verso `10.253.92.1`;
- scrive il profilo client (0600) nel file indicato e lo cancella all'uscita.

## Build (Go 1.14.15 isolato, già usato per l'engine)

```bash
chmod +x TestPeer/build-test-peer.sh
TestPeer/build-test-peer.sh
```

Usa l'archivio già scaricato in `/tmp/nineguard-wireguard-go-2020/source.tar.gz`
se presente (stesso SHA-256), altrimenti lo scarica. Al termine esegue un
`-selftest` locale: client in-process, handshake e echo UDP su loopback.

## Uso per il test con l'iPad

Sul PC (sostituire con l'IPv4 del PC sulla stessa rete dell'iPad):

```bash
TestPeer/build/ngtestpeer -endpoint-host <IP_PC> -listen-port 51830 \
    -client-conf /tmp/nineguard-testpeer/client.conf -duration 10m
```

(creare prima `/tmp/nineguard-testpeer` con `mkdir -m 0700`). La porta UDP
51830 deve essere raggiungibile dall'iPad: se il firewall del PC la blocca,
aprirla solo temporaneamente o scegliere un'altra porta.

In un secondo terminale:

```bash
TESTPEER_CONF=/tmp/nineguard-testpeer/client.conf \
    DaemonPrototype/tests/device-start-profile.sh peer
```

Con `-full` il profilo client usa `AllowedIPs = 0.0.0.0/0` e
`DNS = 10.253.92.1`; il peer risponde anche alle query DNS (record A fisso
10.253.92.1 per qualsiasi nome) e conta come `DROPPED` il traffico Internet
catturato dal tunnel. Usato dalle fasi `full` e `crash` dello script.

Atteso sull'iPad `UDP_ECHO=OK` e `HANDSHAKE_AGE` numerico; sul PC
`TESTPEER_STATS ... UDP_ECHO=1`. Interrompere `ngtestpeer` con Ctrl-C: il
profilo client viene rimosso.

## Verifica già eseguita (cloud, Linux x86_64, 11 ottobre 2026)

```text
SELFTEST_UDP_ECHO=true SERVER_HANDSHAKE=true SERVER_RX=228 SERVER_TX=172
SELFTEST=OK
```

e catena completa profilo generato → builder C di NineGuard → UAPI di
wireguard-go → echo UDP cifrato (`CLIENTDEV_ECHO= true`).

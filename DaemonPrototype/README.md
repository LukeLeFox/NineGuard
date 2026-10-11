# NineGuard daemon prototype

Prototipo ARMv7 verificato su iPad mini 1 con iOS 9.3.6. La libreria client
viene compilata anche nell'app; il demone conserva un `Makefile` separato ed è
incluso nel pacchetto rootful insieme al job launchd.

Funzioni attuali:

- socket Unix `root:mobile` con permessi `0660`;
- filtro kernel `getpeereid`: sono accettati solo UID/GID `mobile`;
- autenticazione reciproca HMAC-SHA256 con nonce casuale per connessione;
- segreto da 32 byte nel gruppo Keychain `dev.luke.nineguard.shared`;
- comandi `PING`, `STATUS`, `START`, `STOP`, `SHUTDOWN`;
- avvio di `wireguard-go -f utun0` con `posix_spawn`;
- attesa del socket UAPI e arresto sincrono del processo figlio;
- pulizia automatica di motore e socket di controllo su `SIGINT`/`SIGTERM`.

I percorsi definitivi sono centralizzati in `NGDaemonPaths.h`: socket sotto
`/var/run/nineguard`, daemon e motore sotto `/usr/libexec/nineguard`. La
directory del socket resta `root:wheel 0755`, mentre il socket è
`root:mobile 0660`; `mobile` può connettersi ma non sostituirlo. Il trasporto è
verificato sia con il client CLI sia dall'app reale sandboxata.

Compilazione diagnostica:

```bash
make -C DaemonPrototype clean signed
```

Output in `/tmp/nineguard-daemon-build`. Il client di prova deve creare il
segreto come utente `mobile` con `nineguardctl PROVISION`; `DELETE_SECRET` lo
rimuove. Quando il segreto non esiste, il demone resta vivo senza creare il
socket e attende che l'app lo generi; questo rende sicuro `RunAtLoad`.

Esito reale del test: `AUTHENTICATED_DAEMON_TEST_EXIT_CODE=0`.
Durante `START`, `utun0` era point-to-point, senza IPv4 e con zero route IPv4;
dopo `STOP` non risultavano interfacce `utun` né socket UAPI. `SHUTDOWN` ha
rimosso anche il socket di controllo. Un client che ha aperto la connessione
senza completare l'autenticazione è stato espulso dal timeout di tre secondi;
il client valido ha ricevuto `PONG` mentre quello bloccante era ancora vivo
(`IPC_STALL_TIMEOUT=OK`).

L'11 ottobre 2026 anche la build diagnostica dell'app, avviata realmente da
LaunchServices come utente `mobile`, ha completato `PING` verso il demone root.
Il log server conteneva un solo `IPC_AUTH=ACCEPTED` prima del successivo
`SHUTDOWN` inviato dal CLI. App, demone, socket e segreto di prova sono poi
stati arrestati o rimossi; nessuna interfaccia `utun` era presente.

Il job `dev.luke.nineguard.daemon` è stato caricato realmente con launchd.
L'app ha creato il segreto, autenticato `PING` e il demone ha avviato il motore
dalla posizione definitiva. Un diagnostico `getifaddrs` ha visto `utun0` senza
indirizzi IPv4; `STOP` ha rimosso sia interfaccia sia UAPI. I file installati
manualmente sono stati poi spostati in `/tmp` e il job scaricato.

## START_PROFILE, NET_UP, STATS (11 ottobre 2026, sessione Claude)

Implementati ma **non ancora provati sul dispositivo** (la sessione non aveva
accesso SSH all'iPad né alla toolchain Theos). Compilazione armv7/iOS 9
verificata con clang 18 + header iPhoneOS 9.3 (`-Wall -Wextra -Werror`, solo
oggetti: il link armv7 richiede ld64 di Theos). Logica portabile verificata
su host contro il vero parser UAPI di wireguard-go 0.0.20200320.

Nuovi comandi autenticati (stesso HMAC/nonce di prima):

| Comando | Effetto | Risposta di successo |
|---|---|---|
| `START_PROFILE <UUID>` | legge il profilo dal Keychain condiviso, lo rivalida con `NGWGConfigParser` come root, avvia l'engine e invia la configurazione UAPI; verifica con `get=1` | `START_PROFILE=OK PEERS=n ALLOWED=n ENDPOINTS=n HOST_ROUTES=n SKIPPED=n IPV4=YES` |
| `NET_UP` | IPv4 /32 point-to-point su `utun0` e **solo** host route IPv4 /32 degli AllowedIPs | `NET_UP=OK ROUTES=n SKIPPED=n` |
| `NET_DOWN` | rimuove host route e indirizzo (idempotente) | `NET_DOWN=OK` |
| `STATS` | riassunto UAPI senza chiavi | `STATS=OK PEERS=n HANDSHAKE_AGE=s/NEVER TX=n RX=n NET=UP/DOWN` |

`STATUS` ora riporta anche `PROFILE=YES/NO NET=UP/DOWN` e (0.3.1)
`NETWATCH=OK|WAITING|FAILING|STOPPED REBYPASS=<n>` del watchdog cambio rete
(vedi `FULL-TUNNEL-DNS-DESIGN.md`, "Fase 2"). `STOP` esegue prima
`NET_DOWN`. Se l'engine muore, lo stato rete/profilo viene azzerato.

Errori: `START_PROFILE=REFUSED_{BAD_UUID,NOT_FOUND,KEYCHAIN,PARSE,KEY,
ENDPOINT,ALLOWED_IP,ADDRESS,...}`, `ALREADY_RUNNING`, `ENGINE_FAILED`,
`UAPI_ERROR ERRNO=-22 ROLLBACK=OK` (errno UAPI negativi come in wireguard-go),
`UAPI_TRANSPORT`, `VERIFY_FAILED`; `NET_UP=FAILED_{ADDRESS,ROUTE,NO_INTERFACE,
NO_LINK,UNSAFE_PLAN} ROLLBACK=OK`.

Regole di sicurezza applicate dal codice:

- l'IPC trasporta solo l'UUID, mai testo o chiavi;
- il corpo UAPI con le chiavi in hex vive in un buffer da 64 KiB azzerato
  subito dopo l'invio; la risposta `get=1` viene azzerata dopo il parsing;
- endpoint hostname risolti dal daemon con `getaddrinfo` (preferenza IPv4),
  perché `wireguard-go` 2020 accetta solo IP numerici;
- `NET_UP` non crea mai default route né DNS: AllowedIPs non /32, IPv6 e
  quelli uguali all'IP dell'endpoint vengono contati in `SKIPPED` e non
  instradati (restano comunque nella tabella crittografica di wireguard-go);
- route rifiutate prima di toccare il kernel: 0.0.0.0, broadcast, 127/8,
  multicast, indirizzo locale;
- route con flag `RTF_UP|RTF_HOST|RTF_STATIC`, gateway = `sockaddr_dl` reale di
  `utun0` letto da `getifaddrs` (stessa forma verificata nel test route);
- rollback automatico di quanto applicato se un passo fallisce.

Limiti noti: `MTU` e `DNS` del profilo sono ignorati; l'IPv4 usato è il primo
di `Address`, sempre come /32 point-to-point (dst = sé stesso, come
`wg-quick` su macOS); `LOG_LEVEL=error` di wireguard-go può scrivere in
`/var/log/nineguard-engine.log` (0600 root) il valore di un endpoint rifiutato.

Nuovi file: `NGProfileUAPI.{h,c}` (conversione C pura), `NGUAPIClient.{h,c}`
(client UAPI set/get), `NGDaemonProfile.{h,m}` (Keychain + parser),
`NGNetConfig.{h,c}` (IPv4/route Darwin, ABI `rt_msghdr` locale con
`_Static_assert`), `ngprofiletool.m`, `ngudpprobe.c`, `nglistutun.c`,
`tests/device-start-profile.sh`, `tests/host/`.

Nota build: il Makefile aggiornato è in `Makefile.start-profile` e va copiato
su `Makefile` (gli strumenti della sessione non potevano scrivere `Makefile`).

Il client `NGIPCClientExchange` ora attende la risposta fino a 20 s (invio 3 s);
il server mantiene 3 s in entrambe le direzioni (anti-stallo invariato).

Helper di test (copiati dallo script in una directory `/tmp` nuova):

- `ngprofiletool synthetic|import <file>|remove <UUID>` (come `mobile`);
- `ngudpprobe <IPv4> <porta> [echo]`;
- `nglistutun` (getifaddrs: interfacce utun e IPv4).

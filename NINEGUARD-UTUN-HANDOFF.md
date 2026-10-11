# NineGuard — handoff test `utun` autonomo

Aggiornato: 11 ottobre 2026 (sessione 2: vedi "Sessione 2 (Claude)").

Questo file permette di continuare il lavoro in una nuova chat senza rileggere
la conversazione precedente. Non usare Git finché il lavoro non è concluso.

## Obiettivo e vincoli

NineGuard è un client WireGuard per iPad mini 1 (`iPad2,7`) con iOS 9.3.6,
jailbreak Carbon + iocaste. Le app extension di terze parti sono rotte dal
jailbreak (`invalid sandbox or container`), quindi `NetworkExtension` e
`NEPacketTunnelProvider` non sono una strada praticabile.

Nuova architettura prevista:

1. app NineGuard esistente per UI, QR e profili;
2. demone autonomo root, fuori da PluginKit;
3. motore WireGuard userspace collegato direttamente a un'interfaccia `utun`;
4. configurazione di IP, route e DNS solo in una fase successiva.

Vincoli operativi:

- directory: radice del repository NineGuard;
- Theos: `~/theos` o variabile `THEOS`;
- target invariato: `iphone:clang:9.2:9.0`, `ARCHS = armv7`;
- iPad: variabile `IPAD=root@<indirizzo-iPad>`;
- SSH legacy: `-o HostKeyAlgorithms=+ssh-rsa -o LogLevel=ERROR`;
- patch piccole, un passo per volta, output limitato;
- non modificare ancora app, NetworkExtension, profili, IP, route o DNS;
- niente Git fino alla fine.

## Risultato decisivo già ottenuto

Un eseguibile C ARMv7 ad hoc, avviato come root, ha aperto correttamente il
kernel control `com.apple.net.utun_control` e ha ottenuto `utun0`.

Output reale:

```text
NineGuard utun probe: euid=0
Controller trovato: id=6
SUCCESS: interfaccia=utun0 fd=3
Nessun IP, route o DNS applicato. Attesa 15 secondi...
Chiusura fd: l'interfaccia dovrebbe scomparire.
```

Il processo era ancora vivo dopo 2 secondi, ha mantenuto `fd=3` aperto per 15
secondi ed è terminato con status `0`. Questo dimostra che iocaste non impedisce
a un processo root autonomo di creare una `utun` senza NetworkExtension.

Il kernel reale del dispositivo è:

```text
Darwin 15.6.0
root:xnu-3248.61.1~1/RELEASE_ARM_S5L8942X
iPad2,7 arm P107AP
```

### Precisazione importante

`/sbin/ifconfig` non esiste sull'iPad. Il messaggio prodotto dallo script
esterno secondo cui `utun0` era stata rimossa dopo `close(fd)` era quindi un
falso positivo: il controllo falliva perché mancava il comando. La creazione
di `utun0` è invece provata direttamente dal `getsockopt(UTUN_OPT_IFNAME)`
riuscito. La scomparsa dopo `close(fd)` è attesa dall'implementazione XNU, ma
non è stata osservata con uno strumento esterno.

## File prodotti

```text
UtunProbe/utun_probe.c         sorgente, 124 righe
UtunProbe/utun_probe           Mach-O ARMv7 non firmato
UtunProbe/utun_probe.signed    copia firmata ad hoc con ldid
```

Hash SHA-256 verificati:

```text
11af2efacc68c8667190ce8b3186199da93e3075fe67deb7c2b300eafd578ea6  UtunProbe/utun_probe.c
c3dae88a00ca029319186caebaac4b0ae8ff1004ef5432557817273cc6dc4463  UtunProbe/utun_probe
3a01162d00b7b5c8d711d75299d8263c3ea2fdc98deb26a2298b3e6ba994fe94  UtunProbe/utun_probe.signed
```

Il binario firmato è stato copiato senza errori in:

```text
/tmp/nineguard-utun-probe/utun_probe
```

Checksum `cksum` locale e remoto coincidente:

```text
1693714258 51344
```

## Controlli del binario già superati

```text
Mach-O armv7 executable
flags: NOUNDEFS DYLDLINK TWOLEVEL PIE
LC_VERSION_MIN_IPHONEOS: 9.0
SDK: 9.2
dipendenza unica: /usr/lib/libSystem.B.dylib
LC_CODE_SIGNATURE presente dopo ldid -S
```

Simboli rilevanti importati: `socket`, `ioctl`, `connect`, `getsockopt`,
`close`, `sleep` e funzioni libc. Il probe non contiene chiamate per route,
indirizzi IP, DNS o comandi shell.

## ABI Darwin usata

Gli header privati `sys/kern_control.h` e `net/if_utun.h` non sono presenti
nell'SDK pubblico iPhoneOS 9.2. Il sorgente dichiara localmente soltanto l'ABI
necessaria, verificata nelle sorgenti Apple XNU:

- `SYSPROTO_CONTROL = 2`;
- `AF_SYS_CONTROL = 2`;
- `MAX_KCTL_NAME = 96`;
- `CTLIOCGINFO = _IOWR('N', 3, struct ctl_info)`;
- `UTUN_CONTROL_NAME = "com.apple.net.utun_control"`;
- `UTUN_OPT_IFNAME = 2`;
- `sizeof(struct ctl_info) = 100`;
- `sizeof(struct sockaddr_ctl) = 32`.

Riferimenti primari:

- <https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-3248.60.10/bsd/sys/kern_control.h>
- <https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-3248.60.10/bsd/sys/sys_domain.h>
- <https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-3248.60.10/bsd/net/if_utun.h>
- <https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-3248.60.10/bsd/net/if_utun.c>

## Compilazione riproducibile

```bash
"${THEOS:-$HOME/theos}/toolchain/linux/iphone/bin/clang" \
    -target armv7-apple-ios9.0 \
    -isysroot "${THEOS:-$HOME/theos}/sdks/iPhoneOS9.2.sdk" \
    -miphoneos-version-min=9.0 \
    -Os -Wall -Wextra -Werror -fPIE \
    -Wl,-pie -Wl,-dead_strip \
    UtunProbe/utun_probe.c \
    -o UtunProbe/utun_probe

cp -p UtunProbe/utun_probe UtunProbe/utun_probe.signed
"${THEOS:-$HOME/theos}/toolchain/linux/iphone/bin/ldid" \
    -S UtunProbe/utun_probe.signed
```

Non usare `-pie` direttamente: questa toolchain Clang lo segnala come argomento
inutilizzato e, con `-Werror`, blocca la build. Usare `-Wl,-pie`.

## Aggiornamento rete utun — 11 ottobre 2026

La sequenza prevista sotto è stata superata. Sul dispositivo reale sono ora
verificati anche motore WireGuard ARMv7, UAPI, IPv4, route e consegna di un
pacchetto dal kernel al file descriptor `utun`.

ABI `NET_RT_DUMP2` verificata con assert ARMv7 e dump reale:

```text
sizeof(rt_msghdr2)=92
offsetof(rtm_addrs)=12
sockaddr alignment=4 (ROUNDUP32)
ROUTE_PARSE=OK
ROUTE_DETAILS=OK
```

Con `192.0.2.10/32` e peer point-to-point `192.0.2.11`, il kernel crea una
sola route diretta. Una host route temporanea aggiuntiva ha prodotto:

```text
PRIMA:   192.0.2.11                         route utun=1
DURANTE: 192.0.2.11, 198.51.100.7           route utun=2
DOPO:    192.0.2.11                         route utun=1
ROLLBACK IPv4:                              route utun=0
```

La route `198.51.100.7/32` usa un gateway `sockaddr_dl` associato a `utun0`,
con flag `RTF_UP|RTF_HOST|RTF_STATIC` e senza `RTF_GATEWAY`. Non sono state
modificate Wi-Fi, default route o configurazione DNS.

Un datagramma UDP di tre byte inviato a `198.51.100.7:9` è stato osservato
direttamente sul fd utun:

```text
PACKET_FAMILY=2
PACKET_IP_VERSION=4
PACKET_PROTOCOL=17
PACKET_DST=198.51.100.7
PACKET_READ=OK
```

Tutti i rollback sono riusciti: host route, indirizzo IPv4 e interfaccia utun.
I sorgenti diagnostici restano temporaneamente in `/tmp/nineguard-netdiag`;
non integrarli ancora nell'app.

### Round-trip WireGuard cifrato superato

Due istanze indipendenti di `wireguard-go` 0.0.20200320 hanno creato `utun0`
e `utun1`, scambiato chiavi effimere via UAPI e completato l'handshake su
`127.0.0.1` (porte 51840 e 51841). Sono stati assegnati temporaneamente solo
`10.253.90.1/32` e `10.253.90.2/32`; client e server UDP erano vincolati alle
rispettive interfacce con `IP_BOUND_IF`.

La richiesta `NINEGUARD2` e la risposta `NINEGUARD2-ACK` sono arrivate
entrambe. I contatori UAPI letti immediatamente prima e dopo provano che il
traffico ha attraversato davvero i due motori cifrati, invece di essere
consegnato localmente dal kernel:

```text
A_TX_DELTA=80    B_RX_DELTA=80
B_TX_DELTA=80    A_RX_DELTA=80
ENCRYPTED_COUNTERS=OK
ENCRYPTED_ROUNDTRIP=OK
```

Il test si è chiuso con `PAIR_ROLLBACK=OK`, scomparsa di entrambi gli `utun`
verificata via `getifaddrs` dal diagnostico ARMv7, e rimozione di entrambi i
socket UAPI. Wi-Fi, route predefinita e DNS non sono stati modificati. I
helper restano solo in `/tmp/nineguard-netdiag` e `/tmp/nineguard-wg-test`.

### Lifecycle del demone root superato

Il prototipo ARMv7 `nineguardd` è stato eseguito come root e controllato via
socket Unix temporaneo `0600`. I comandi `PING`, `STATUS`, `START`, `STOP` e
`SHUTDOWN` hanno funzionato. `START` ha avviato `wireguard-go -f utun0` con
`posix_spawn`; il diagnostico nativo ha osservato una `utun0` point-to-point
senza IPv4 e con zero route IPv4. `STOP` ha rimosso interfaccia e socket UAPI,
mentre `SHUTDOWN` ha rimosso anche il socket di controllo:

```text
PONG
START=OK
ENGINE=RUNNING
ROUTES_IPV4_UTUN=0
STOP=OK
ENGINE_UTUN_ROLLBACK=OK
ENGINE_UAPI_ROLLBACK=OK
NINEGUARDD_CLEANUP=OK
CONTROL_SOCKET_ROLLBACK=OK
DAEMON_LIFECYCLE_TEST_EXIT_CODE=0
```

I sorgenti verificati sono conservati in `DaemonPrototype/`, hanno un Makefile
separato e non sono collegati al `Makefile` dell'app né installati con launchd.
Usano ancora solo percorsi sotto `/tmp/nineguard-wg-test`.

### IPC autenticato app-demone superato da client mobile e app reale

Sul kernel reale, `getpeereid`, `LOCAL_PEERPID` e `KERN_PROCARGS2` hanno
restituito correttamente UID, GID, PID e percorso dell'eseguibile per client
root e `mobile`. PID/percorso sono usati solo come dati diagnostici: non sono
stati scambiati per una prova crittografica dell'identità dell'app.

Un processo `mobile` e uno root, entrambi firmati con il gruppo Keychain
`dev.luke.nineguard.shared`, hanno condiviso correttamente un elemento di prova;
l'elemento è stato eliminato e la successiva lettura ha restituito
`errSecItemNotFound`.

Il protocollo ora applica:

- socket Unix `root:mobile` con permessi `0660`;
- accettazione dei soli peer con UID/GID `mobile`;
- segreto casuale da 32 byte nel Keychain, separato dai profili;
- nonce casuale nuovo per ogni connessione;
- HMAC-SHA256 distinto per richiesta e risposta;
- confronto MAC a tempo costante e timeout I/O di tre secondi.

Il client valido ha completato l'intero lifecycle del motore; un HMAC errato è
stato rifiutato. Un client `mobile` bloccante è stato espulso dal timeout e il
client valido ha ricevuto `PONG` mentre il primo processo era ancora vivo:

```text
AUTHENTICATED_IPC_PROBE=OK
AUTHENTICATED_DAEMON_LIFECYCLE=OK
AUTHENTICATED_DAEMON_TEST_EXIT_CODE=0
STALL_CLIENT_ALIVE_WHEN_PING_RETURNED=YES
IPC_STALL_TIMEOUT=OK
```

Il segreto di prova è stato cancellato. Il demone ora può partire senza
segreto, ma resta in attesa e non crea il socket finché l'app non lo genera.

La libreria `NGDaemonClient` è stata quindi collegata alla build dell'app. Una
build diagnostica, attivata soltanto con
`NINEGUARD_DAEMON_IPC_AUTOPROBE=1`, prepara il client e ritenta `PING` senza
riattivare NetworkExtension. LaunchServices ha confermato registrazione del
bundle e dello schema `nineguard://`, poi ha avviato davvero il binario
sandboxato come utente `mobile`:

```text
PROXY=FOUND
NINEGUARD_SCHEME_HANDLERS=1
APP_PROCESS=RUNNING
APP_IPC_AUTH=ACCEPTED
```

Prima di qualunque comando CLI, il log server conteneva esattamente un
`IPC_AUTH=ACCEPTED`: la connessione era quindi quella dell'app. Lo shutdown
autenticato ha prodotto `NINEGUARDD_CLEANUP=OK`; app, socket, segreto e demone
sono stati rimossi o arrestati e non risultavano interfacce `utun`.

### Engine riproducibile, percorsi definitivi e launchd verificati

`Engine/build-wireguard-go.sh` ricostruisce l'esatto commit upstream
`52c834c4469f4f6e33c97c7294faffabfcbf114a`, verificando l'archivio con
SHA-256 prima dell'estrazione. Usa Go 1.14.15, cgo, `GOARCH=arm`, `GOARM=7` e
target minimo iOS 9.0. Il risultato è stato eseguito sull'iPad e ha riportato
`wireguard-go v0.0.20200320`.

I percorsi ora sono centralizzati in `DaemonPrototype/NGDaemonPaths.h`:

```text
/var/run/nineguard/nineguardd.sock
/usr/libexec/nineguard/nineguardd
/usr/libexec/nineguard/wireguard-go
/var/run/wireguard/utun0.sock
```

La directory di controllo è `root:wheel 0755`, il socket `root:mobile 0660`.
L'app sandboxata ha autenticato `PING` anche sul percorso definitivo. Il job
launchd `dev.luke.nineguard.daemon` è stato poi caricato realmente senza
segreto: il processo è rimasto vivo, il socket assente; all'avvio dell'app il
segreto è stato creato, il socket è comparso e il server ha registrato una sola
autenticazione dell'app.

Dal job launchd il comando `START` ha avviato il motore nella posizione
definitiva. Un helper nativo `getifaddrs` ha osservato `utun0` e zero indirizzi
IPv4; `STOP` ha rimosso interfaccia e UAPI. Nessun IP, route o DNS è stato
applicato. Dopo il test il job è stato scaricato e i file installati a mano
sono stati spostati in `/tmp/nineguard-launchd-test-backup-v1`.

Un primo pacchetto rootful con app e daemon era stato costruito con successo,
ma non installato: l'app diagnostica attuale è già installata da IPA in un
container e una seconda copia sotto `/Applications` con lo stesso bundle ID
sarebbe ambigua. L'artefatto è stato spostato in
`/tmp/nineguard-full-package-not-install-v1.deb` e il `Makefile` principale è
tornato a costruire soltanto l'app.

Per evitare il conflitto è stato aggiunto `DaemonPackage/`, che produce il solo
companion privilegiato. Il pacchetto
`dev.luke.nineguard.daemon_0.1.0-1+debug_iphoneos-arm.deb` è stato installato
con `dpkg`; lo stato reale è `install ok installed`. L'app è rimasta nella sua
posizione containerizzata, mentre daemon, engine e launchd sono ora gestiti dal
package manager. Dopo l'installazione il job era in attesa senza socket;
l'avvio dell'app ha creato il segreto e prodotto esattamente un
`IPC_AUTH=ACCEPTED`.

### Build 13: bootstrap daemon e profilo locale sicuro

Il `PING` autenticato non è più protetto da un flag diagnostico: a ogni avvio
l'app prepara il segreto IPC, attende il companion launchd e mostra
`Daemon 2.0 autenticato` nel sottotitolo. La build 13 è installata come IPA e
il daemon ha registrato la relativa autenticazione. Il primo tentativo subito
dopo `installipa` è stato troppo precoce per LaunchServices; il secondo ha
avviato normalmente il processo, senza crash report.

`NGProfileStore` ora valida il testo QR, conserva l'intera configurazione nel
Keychain condiviso e salva nei defaults soltanto UUID e numero peer. Il gruppo
`dev.luke.nineguard.shared` è esplicito anche nelle query dei profili. Due
helper ARMv7 eseguiti come `mobile` hanno verificato:

```text
Keychain OK: 4 test superati
PROFILE_STORE=OK
```

Entrambi i test hanno rimosso gli elementi creati. La home salva ora davvero
un QR valido e rimuove il profilo locale; le azioni NetworkExtension principali
sono state tolte dalla UI. Il pulsante centrale interroga `STATUS` del daemon,
mentre la connessione resta esplicitamente disabilitata finché configurazione
UAPI, indirizzi e route non sono implementati end-to-end.

Restano due gate: implementare `START_PROFILE` con UAPI, indirizzi e route;
quindi provarlo verso un peer WireGuard esterno controllato con sola host
route. Il companion `.deb` è già compatibile con l'app IPA; l'eventuale
migrazione futura dell'app stessa a `/Applications` va pianificata senza
perdere i dati del container.

### Sessione 2 (Claude): START_PROFILE scritto, da provare sul device

L'11 ottobre 2026 una seconda sessione (senza SSH verso l'iPad né Theos) ha
implementato `START_PROFILE <UUID>` (Keychain → parser root → UAPI con
verifica `get=1`), `NET_UP`/`NET_DOWN` (IPv4 /32 + sole host route /32, mai
default route o DNS), `STATS`, gli helper `ngprofiletool`, `ngudpprobe`,
`nglistutun`, lo script `DaemonPrototype/tests/device-start-profile.sh` e il
peer userspace `TestPeer/ngtestpeer`. Verifiche solo fuori dal dispositivo:
compilazione armv7 con `-Werror`, unit test, integrazione con il vero parser
UAPI di wireguard-go 0.0.20200320 e catena completa con echo UDP cifrato.
Stato dettagliato e sequenza esatta dei gate 5–7 in
`NINEGUARD-2-CONTINUATION.md`.

## Sequenza storica ormai superata

Questa sezione descrive il piano originario, ora completato fino al round-trip
cifrato. Non usarla come stato corrente e non rifare questi test.

Procedere così, sempre un passo per volta:

1. preflight read-only delle toolchain Go e Rust installate su Penguin-Rig;
2. verificare quali versioni supportano ancora `darwin/arm` o
   `armv7-apple-ios` senza installare o cambiare nulla;
3. scegliere tra `wireguard-go`/bridge iOS e `boringtun` sulla base di una
   prova di compilazione minima, non di ipotesi;
4. compilare prima soltanto l'engine o una sua libreria C-callable;
5. collegarlo alla `utun` già provata;
6. solo dopo testare traffico pacchetti, IP, route e DNS in modo reversibile;
7. integrare infine il motore in un demone root e aggiungere IPC con l'app.

Nota storica: `main.m`, `Makefile`, entitlements e `Info.plist` sono stati poi
modificati in modo circoscritto per il probe IPC dell'app; `Tunnel/` non è
stato riattivato.

## Pulizia opzionale del solo test remoto

Non eseguire durante nuove prove. Quando non serve più:

```bash
ssh -o HostKeyAlgorithms=+ssh-rsa -o LogLevel=ERROR \
    "$IPAD" \
    'rm -rf /tmp/nineguard-utun-probe'
```

Il bersaglio è limitato alla directory temporanea indicata sopra.

## Prompt breve per una nuova chat

```text
Lavora nella radice del repository NineGuard. Leggi per intero
NINEGUARD-UTUN-HANDOFF.md e continua da “Aggiornamento rete utun”. Non rifare
i test ABI, IPv4, route, consegna UDP o round-trip WireGuard cifrato già
riusciti. Anche lifecycle root, IPC reciproco HMAC/Keychain, timeout anti-stallo
e `PING` dalla vera app sandboxata sono verificati: non rifarli. Anche engine
riproducibile, percorsi definitivi, pacchetto e launchd sono verificati.
`START_PROFILE`, `NET_UP` e `STATS` sono scritti (sessione 2) ma non ancora
provati: seguire "Prossimo passo esatto" in NINEGUARD-2-CONTINUATION.md. Il companion daemon `.deb` e la build 13 IPA sono già
installati; non installare un pacchetto che contiene una seconda copia
dell'app. Procedi con patch piccole, log limitato, target iOS 9.0 ARMv7 e
niente Git.
```

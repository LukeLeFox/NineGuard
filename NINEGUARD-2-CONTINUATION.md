# NineGuard 2.0 — stato completo per continuare

Aggiornato: 11 ottobre 2026 (seconda sessione, Claude/Cowork: START_PROFILE,
NET_UP, STATS e peer di test implementati, **non ancora provati sull'iPad**).
Questo file è autosufficiente e permette di
continuare senza la cronologia ChatGPT/Work. Leggere anche
`NINEGUARD-UTUN-HANDOFF.md` per i dettagli delle prove di rete già concluse.

## Regole operative

- directory: `/home/luke/Development/iOS-Legacy/NineGuard`;
- target invariato: `iphone:clang:9.2:9.0`, `ARCHS = armv7`;
- iPad mini 1 iOS 9.3.6: `root@10.10.99.104`;
- SSH: `-o HostKeyAlgorithms=+ssh-rsa -o LogLevel=ERROR`;
- patch piccole, output limitato, niente Git fino alla fine;
- non modificare Wi-Fi, default route o DNS;
- non reinstallare il pacchetto app+daemon sotto `/Applications`: l'app è già
  installata come IPA containerizzata;
- il companion daemon separato è invece installato e gestito da `dpkg`.

## Stato installato sul dispositivo

App:

```text
NineGuard 0.1.0 build 16 (Connetti/Disconnetti, full tunnel + DNS)
bundle: dev.luke.nineguard
installazione: IPA containerizzata (CFBundleVersion 16 verificato)
avvio da SSH: `uiopen nineguard://` (funziona solo a iPad sbloccato)
profilo attivo nell'app: quello EFFIMERO di ngtestpeer (chiavi ormai
inutili, peer fermato) — sostituirlo con un nuovo QR o "Rimuovi profilo"
```

Companion:

```text
Package: dev.luke.nineguard.daemon
Status: install ok installed
Version: 0.3.0-1+debug
launchd label: dev.luke.nineguard.daemon
daemon: /usr/libexec/nineguard/nineguardd
engine: /usr/libexec/nineguard/wireguard-go
socket: /var/run/nineguard/nineguardd.sock
```

Ultimo controllo reale:

```text
ENGINE=STOPPED
UTUN_INTERFACES=0
UTUN_IPV4_ADDRESSES=0
CONTROL_SOCKET=PRESENT
ENGINE_UAPI=ABSENT
APP_PROCESS=RUNNING
```

Questo è lo stato sicuro da cui ripartire: daemon attivo, motore fermo, nessuna
interfaccia o modifica di rete.

## Risultati già verificati — non ripeterli

1. ABI XNU/Darwin ARMv7 delle route:

```text
sizeof(rt_msghdr2)=92
offsetof(rtm_addrs)=12
sockaddr alignment=4
ROUTE_PARSE=OK
ROUTE_DETAILS=OK
```

2. Creazione `utun`, assegnazione IPv4 temporanea, host route temporanea,
   consegna UDP sul fd e rollback completo.
3. Round-trip WireGuard cifrato tra due istanze locali, handshake bidirezionale
   e contatori TX/RX coerenti (`80` byte in entrambe le direzioni).
4. Lifecycle motore root `START/STATUS/STOP/SHUTDOWN`, senza IP/route/DNS.
5. IPC app-daemon:
   - socket `root:mobile 0660` in directory `root:wheel 0755`;
   - filtro kernel `getpeereid` UID/GID `mobile`;
   - segreto casuale da 32 byte nel gruppo Keychain condiviso;
   - nonce nuovo per connessione;
   - HMAC-SHA256 richiesta/risposta e confronto costante;
   - timeout I/O tre secondi;
   - HMAC errato e client bloccante rifiutati.
6. Vera app sandboxata avviata da LaunchServices e autenticata dal daemon.
7. Percorso definitivo `/var/run/nineguard/nineguardd.sock` accessibile
   dall'app ma non sostituibile da `mobile`.
8. launchd: daemon avviato senza segreto resta in attesa senza socket; l'app
   crea il segreto, il socket compare e `PING` riesce.
9. Motore avviato dalla posizione definitiva: `utun0`, zero IPv4, UAPI
   presente; `STOP` elimina interfaccia e UAPI.
10. Keychain profili esplicito su `dev.luke.nineguard.shared`:

```text
Keychain OK: 4 test superati
PROFILE_STORE=OK
```

Gli elementi sintetici dei test sono stati rimossi.

## Architettura attuale

- app IPA: UI, scanner QR, parser, profili locali, client IPC;
- daemon root launchd: autenticazione, lifecycle del motore;
- `wireguard-go` ARMv7: crea `utun0` e UAPI;
- NetworkExtension rimane nel sorgente solo come codice legacy e non è il
  percorso operativo di NineGuard 2.0.

All'avvio normale la build 13 prepara il segreto, invia `PING` e mostra
`Daemon 2.0 autenticato`. Il pulsante centrale invia `STATUS`. Il pulsante di
connessione è intenzionalmente disabilitato: non deve essere riattivato prima
di completare configurazione UAPI, indirizzi e route.

## File principali aggiunti o modificati

```text
DaemonPrototype/NGIPCAuth.h
DaemonPrototype/NGIPCAuth.m
DaemonPrototype/NGDaemonPaths.h
DaemonPrototype/nineguardd.c
DaemonPrototype/nineguardctl.c
DaemonPrototype/NineGuardDaemon.entitlements
DaemonPrototype/dev.luke.nineguard.daemon.plist
DaemonPrototype/Makefile
DaemonPrototype/README.md
DaemonPackage/Makefile
DaemonPackage/control
DaemonPackage/layout/DEBIAN/postinst
DaemonPackage/layout/DEBIAN/prerm
DaemonPackage/README.md
Engine/clang-ios9
Engine/build-wireguard-go.sh
Engine/README.md
NGDaemonClient.h
NGDaemonClient.m
NGProfileStore.h
NGProfileStore.m
NGKeychainStore.m
main.m
Makefile
Resources/Info.plist
Tunnel/Resources/Info.plist
NineGuard.entitlements
```

## Build riproducibili

Engine:

```bash
make engine
```

Usa Go 1.14.15 isolato, commit upstream
`52c834c4469f4f6e33c97c7294faffabfcbf114a`, archivio SHA-256
`d2d6040070044e7f92a0d8d6d2a32a17642587f1c2959909acfdceb25f8b9a97`,
`GOOS=darwin GOARCH=arm GOARM=7 CGO_ENABLED=1`. `GOARM=7` è obbligatorio per
evitare il simbolo Darwin ARM `runtime.read_tls_fallback` indefinito.

App:

```bash
make clean
make -j2 stage
```

Companion daemon:

```bash
make -C DaemonPackage clean package
```

Artefatti attuali:

```text
packages/NineGuard_0.1.0-build13-daemon-ui-profile-store-armv7.ipa
SHA256 0dc5db3a3bf03640fa003de2ca9db4d682e0b235879d536f500211754eeed5ce

DaemonPackage/packages/dev.luke.nineguard.daemon_0.1.0-1+debug_iphoneos-arm.deb
SHA256 3dcda92b2914bb7eb90d02cdc664265267a8bb50ba47c9a26ec6a8ab7fd391e6

DaemonPackage/packages/dev.luke.nineguard.daemon_0.1.0-2+debug_iphoneos-arm.deb
SHA256 cf1a7242a017e87e922935adc2632e05b7d7bc6a216776de436ec5783005767f
```

Sul dispositivo è installato `0.1.0-1+debug`; `-2` è una ricostruzione
equivalente successiva. Il vecchio pacchetto monolitico app+daemon, da non
installare, è stato spostato in
`/tmp/nineguard-full-package-not-install-v1.deb`.

## Sicurezza profili

`NGProfileStore` valida nuovamente il testo con `NGWGConfigParser`, conserva
l'intera configurazione soltanto nel Keychain e scrive nei defaults soltanto
l'UUID attivo e il numero peer. Un nuovo QR valido aggiorna il profilo attivo;
la rimozione cancella l'elemento Keychain e i metadati.

Non stampare mai configurazioni, chiavi private, preshared key o endpoint
privati nei log o nella documentazione.

## Sessione 2 (Claude, 11 ottobre 2026): cosa è stato fatto

Vincolo della sessione: accesso solo ai file del PC (lettura/scrittura), **nessuna
shell sul PC, nessun SSH verso l'iPad, nessuna toolchain Theos**. Quindi tutto
ciò che segue è scritto e verificato fuori dal dispositivo; i gate 5–7 vanno
eseguiti da Luke con lo script indicato sotto.

Implementato nel daemon (dettagli in `DaemonPrototype/README.md`):

1. `START_PROFILE <UUID>`: solo l'UUID passa sull'IPC; il daemon legge il
   profilo dal gruppo Keychain `dev.luke.nineguard.shared`, lo rivalida con
   `NGWGConfigParser` come root, converte base64→hex, risolve gli endpoint
   (anche hostname, preferenza IPv4), avvia l'engine, invia `set=1` e
   verifica con `get=1` (numero peer e chiave privata presente). Errore UAPI
   → `STOP` automatico (`ROLLBACK=OK`).
2. `NET_UP` / `NET_DOWN`: IPv4 /32 point-to-point su `utun0` e sole host
   route IPv4 /32 degli AllowedIPs (mai default route, mai DNS; AllowedIPs
   più ampi, IPv6 e IP dell'endpoint → `SKIPPED`, non instradati).
3. `STATS`: peer, età handshake, TX/RX, stato rete; nessuna chiave.
4. `STATUS` riporta `PROFILE=` e `NET=`; `STOP` fa prima `NET_DOWN`.
5. Client IPC: timeout di ricezione 20 s solo lato client (START_PROFILE può
   durare >3 s); server invariato a 3 s.

App: `NGDaemonClient` accetta `STATS`, `NET_UP`, `NET_DOWN` e
`+startProfileID:completion:` (UUID validato). **UI non modificata**: il
pulsante Connessione resta disabilitato (gate 8).

Companion: `DaemonPackage/control` portato a `Version: 0.2.0`.

Nuovi strumenti:

- `DaemonPrototype/ngprofiletool` (mobile): `synthetic`, `import <file>`,
  `remove <UUID>`; stampa solo `PROFILE_ID=`.
- `DaemonPrototype/ngudpprobe`, `DaemonPrototype/nglistutun`.
- `DaemonPrototype/tests/device-start-profile.sh` (fasi `install`, `uapi`,
  `net`, `peer`, con cleanup automatico).
- `TestPeer/` (`ngtestpeer`): peer WireGuard **userspace** sul PC, stesso
  commit wireguard-go, TUN in memoria, nessun root/pacchetto/route; risponde a
  UDP echo (porta 7) e ICMP echo su `10.253.92.1`. È la soluzione "isolata e
  reversibile" per il gate 7.
- `DaemonPrototype/tests/host/`: test host (unit + integrazione UAPI reale).

Verifiche eseguite (nel cloud, fuori dall'iPad):

```text
armv7-apple-ios9.0, header iPhoneOS 9.3, -Wall -Wextra -Werror: 10/10 oggetti OK
simboli C esterni presenti negli stub .tbd dell'SDK
UNIT OK fails=0                          (ASan/UBSan)
UAPI_SET_ERRNO=0  GET PEERS=1 PRIV=1     (parser UAPI reale wireguard-go 2020)
BAD_SET_ERRNO=-22                        (errore UAPI riconosciuto)
A_AFTER HS_AGE=2 TX=212 RX=92 / HARNESS_DELIVERED= true
SELFTEST=OK                              (ngtestpeer)
catena conf ngtestpeer -> builder C -> UAPI -> CLIENTDEV_ECHO= true
```

Non verificato: link armv7 con ld64 Theos, Go 1.14.15 per `ngtestpeer`
(compilato con Go 1.24 evitando API successive a 1.14), e naturalmente tutto
il comportamento sul kernel iOS (Keychain da root per i profili, SIOCAIFADDR,
RTM_ADD con sockaddr_dl, risposta UDP sulla utun).

Backup dei file modificati: `_backup-pre-start-profile/` (versioni esatte
prima di questa sessione).

## Sessione 3 (Claude Code sul PC, 11 ottobre 2026): gate eseguiti

- Operazioni manuali fatte: `Makefile.start-profile` copiato su
  `DaemonPrototype/Makefile`, `chmod +x` dei tre script.
- `make -C DaemonPrototype clean signed`: OK al primo colpo (compile + link
  ld64 Theos armv7, firma ldid), nessuna correzione necessaria.
- Gate install: `device-start-profile.sh install` →
  `dev.luke.nineguard.daemon 0.2.0-1+debug install ok installed`,
  daemon in launchd, `/var/run/nineguard` presente. **Companion 0.2.0 installato.**
- Gate 5 (`uapi`): **OK** — `START_PROFILE=OK PEERS=1 ALLOWED=1 ENDPOINTS=1
  HOST_ROUTES=1 SKIPPED=0 IPV4=YES`, `STATUS ... PROFILE=YES NET=DOWN`,
  `STATS=OK PEERS=1 HANDSHAKE_AGE=NEVER TX=0 RX=0`, `UTUN_IPV4_ADDRESSES=0`,
  STOP/remove profilo OK, `DEVICE_PHASE_uapi=OK`. Keychain letto da root OK.
  Lo script ora fa `cd /tmp` prima di `su mobile` (toglie i warning getcwd).
- Gate 6 (`net`): **OK** — `NET_UP=OK ROUTES=1 SKIPPED=0`,
  `UTUN_IPV4_ADDRESSES=1`, `UDP_SEND=OK`, `STATS ... HANDSHAKE_AGE=NEVER
  TX=148 RX=0 NET=UP`, `NET_DOWN=OK` → `UTUN_IPV4_ADDRESSES=0`, STOP OK,
  `DEVICE_PHASE_net=OK`. SIOCAIFADDR e RTM_ADD funzionano sul kernel iOS 9.
  (Sull'iPad non c'è `netstat`; dopo STOP la utun viene distrutta.)
- `TestPeer/build-test-peer.sh`: OK con Go 1.14.15 locale, `SELFTEST=OK`.
- Gate 7 (`peer`, ngtestpeer sul PC 10.10.99.2 porta UDP 51830, ufw non ha
  bloccato): **OK** — `UDP_ECHO=OK`, `STATS=OK PEERS=1 HANDSHAKE_AGE=1
  TX=228 RX=172 NET=UP`, `DEVICE_PHASE_peer=OK`; lato PC
  `TESTPEER_STATS HANDSHAKE_AGE=5 TX=172 RX=228 UDP_ECHO=1 DROPPED=0`.
  Peer fermato con SIGINT, client.conf rimosso.
- **Gate 5, 6, 7 superati.**
- Punto 6 fatto (build 14): in `main.m` il pulsante tag 109 è ora
  "Connetti"/"Disconnetti". Tocco → `STATUS`; se `PROFILE=YES` → `STOP`
  (che fa anche NET_DOWN), altrimenti `START_PROFILE <UUID attivo>` →
  `NET_UP`; se NET_UP fallisce → `STOP` di rollback. Titolo/abilitazione
  aggiornati da `STATUS` dopo il PING iniziale, in `refreshProfileState` e
  dopo ogni azione; disabilitato senza profilo o daemon. Nessuna modifica a
  NGDaemonClient/daemon. Backup: `_backup-pre-build14/` (main.m, Info.plist).
  IPA `packages/NineGuard_0.1.0-build14-connect-button-armv7.ipa`
  SHA256 `b3a956833843a39445c071b4b519761dcef5788ef95aea99133046dc9fb66199`,
  stesso layout della build 13, installata.
- Script test: ora rimuove `/tmp/nineguard-sp-*` sull'iPad nel cleanup.
- Stato finale sessione 3: `ENGINE=STOPPED`, `UTUN_INTERFACES=0`, nessun
  crash report NineGuard.

- Gate 8 (UI, iPad sbloccato): build 14 avviata con `uiopen`,
  `NGDaemonIPCAutoprobeResult=OK`. Import QR del profilo ngtestpeer (QR PNG
  con `qrencode -o` in dir 0700, mostrato sul PC con `display`, poi
  cancellato): profilo salvato (prefs `NGActiveProfileID`, 1 peer) ma il
  pulsante restava disabilitato → **bug**: dopo il salvataggio dello scanner
  non veniva chiamato `refreshConnectionButton`. Corretto in `scanQR`
  (build 15, SHA256
  `02f32723bb2458933ca215ab0de0db516d2d0781f7fcf2b49c11cc0c9e6f11aa`,
  `packages/NineGuard_0.1.0-build15-connect-refresh-armv7.ipa`).
  Con build 15: Connetti → "Connesso. NET_UP=OK ROUTES=1 SKIPPED=0";
  `STATUS ENGINE=RUNNING PROFILE=YES NET=UP`, `UTUN_IPV4_ADDRESSES=1`;
  `ngudpprobe 10.253.92.1 7 echo` → `UDP_ECHO=OK`, `HANDSHAKE_AGE=1 TX=228
  RX=172`. Disconnetti → "Disconnesso. STOP=OK"; `ENGINE=STOPPED`,
  `UTUN_INTERFACES=0`; nessun crash report. **Gate 8 superato.**
  (Le prefs dell'app stanno in `/var/mobile/Library/Preferences/
  dev.luke.nineguard.plist`, binarie; sull'iPad non c'è `plutil`.)

- Full tunnel + DNS (richiesto da Luke): design e risultati in
  `DaemonPrototype/FULL-TUNNEL-DNS-DESIGN.md`. Companion **0.3.0** installato
  (route AllowedIPs IPv4 con /0 → 0/1+128/1, protezione LAN, bypass
  endpoint via RTM_GET, DNS via servizio configd temporaneo con
  OverridePrimary, SIGCHLD watchdog, socket UAPI orfano, `net.state`).
  Nuovi helper: `ngdnsprobe`, `ngroute`; `NGSCDynamicStore.h` (dichiarazioni
  locali, gli header SDK marcano SCDynamicStore unavailable).
  Fasi script nuove: `full`, `crash`, `bypass` (watchdog STOP 120 s sul
  device); `ngtestpeer -full` (AllowedIPs 0/0, DNS, risponde a query A).
  Gate F1, F2 (full/crash/bypass) **superati**; regressione `net` OK.
- App **build 16** installata
  (`packages/NineGuard_0.1.0-build16-full-tunnel-armv7.ipa`, SHA256
  `e563a2e042bd3de4eeebfc7936403879736a533e94a77069c95c0d5e8f4ab872`):
  "Rimuovi profilo" esegue prima `STOP`; messaggio di connessione leggibile
  (tipo di routing + DNS). Backup sorgenti in `_backup-pre-full/`.
- **F3 superato** (Luke, profilo reale, iPad su hotspot 4G): con il tunnel
  l'IP pubblico passa da WindTre all'IP di casa (server VPN). Icona Wi-Fi
  nascosta a tunnel attivo (utun0 primaria). Disconnetti → IP WindTre;
  rientro sul Wi-Fi di casa e verifica SSH: stato pulito (nessuna utun,
  route/DNS/primaria originali, nessuna chiave configd, nessun crash nuovo).

## Sessione 4 (Claude Code, 11 ottobre 2026): watchdog cambio rete

- Companion **0.3.1** installato via `device-start-profile.sh install`
  (deb SHA256 `917ab70c11e13e26469385802123a1559f2beb00707214baf3f683067c719a27`).
  App invariata (build 16). Backup sorgenti: `_backup-pre-netwatch/`.
- Nuovi file `DaemonPrototype/NGNetWatch.{h,c}` (logica pura); modifiche a
  `NGNetConfig`, `NGDNSConfig` (`ng_dns_physical_router`),
  `NGSCDynamicStore.h` (`SCDynamicStoreCopyKeyList`), `nineguardd.c`
  (`select` 5 s + `watch_network`), `ngroute.c` (`delete`, `gwcheck`),
  `Makefile`, test host (`unit.c` + `run-host-tests.sh`), script device
  (fasi `watch`, `netchange`). Dettagli in
  `DaemonPrototype/FULL-TUNNEL-DNS-DESIGN.md`, sezione "Fase 2".
- `make -C DaemonPrototype clean signed` pulita (-Werror);
  `tests/host/run-host-tests.sh` → `UNIT OK fails=0` (integrazione Go
  saltata: `tests/host/bin` non compilato, come prima).
- Device: `watch` **OK** (bypass cancellato → ripristinato entro 5 s,
  `NETWATCH=OK REBYPASS=1`); regressione `net`, `bypass`, `full`, `crash`
  **OK**; stato finale pulito.
- `netchange` con Luke (Wi-Fi casa → hotspot → casa, cambio manuale):
  **OK** — bypass spostato sul gateway dell'hotspot entro ~6 s e
  riportato su casa al rientro, `REBYPASS=3`, 1.1.1.1 sempre nella utun.
  Corretto nello script il controllo SSH (niente `BatchMode`).
- **Incidente**: per terminare il processo `sleep 600` del paracadute ho
  usato `kill <pid> $(ps -o ppid= -p <pid>)`; su iOS il PPID risultante ha
  colpito launchd → **riavvio dell'iPad** (12:24). Dopo il riavvio:
  jailbreak/SSH ok, `PONG`, `ENGINE=STOPPED`, nessuna utun, primaria en0,
  nessuna chiave NineGuard in configd, nessun crash report nuovo.
  Regola aggiunta in CLAUDE.md.

- Companion **0.3.2** (MTU del profilo applicata alla utun e ripristinata):
  deb SHA256 `2162f87079ae8973e1ec08e5eb589ab2bab6c4c058ff23d23ced628c525b1e73`;
  test host verdi; device `net`, `bypass`, `watch`, `full`, `crash` OK.
  Backup: `_backup-pre-mtu/`.

- App **build 17** installata
  (`packages/NineGuard_0.1.0-build17-stats-ui-armv7.ipa`, SHA256
  `7ae525c0f4c77e2f1bc58a1a3c0dfcd6e0158eaca2d86247a0315be2f2dd9cc2`,
  stesso layout della 16; backup `_backup-pre-build17/`): con
  `PROFILE=YES` un timer di 5 s (fermo in background) invia `STATUS` +
  `STATS` e mostra sotto il titolo "Handshake N s fa · ↓RX ↑TX" (più
  "rete assente" / "cambio rete..." da `NETWATCH`); se il tunnel si ferma
  fuori dall'app → "Tunnel non più attivo." (o messaggio dedicato con
  `NETWATCH=STOPPED`) e pulsante di nuovo "Connetti". Verificato con Luke
  (screenshot: "Handshake 36 s fa · ↓172 bytes ↑260 bytes"; STOP da SSH →
  messaggio e pulsante aggiornati). Installazione: IPA copiata in un
  /tmp nuovo + `installipa` (exit 0 questa volta).

### Da fare (sessione 5)

1. Fase 2 full tunnel **chiusa da Luke** (11 ott 2026): watchdog, MTU e
   STATS in UI fatti e verificati. Rinviati per scelta: icona Wi-Fi
   nascosta (estetico; richiederebbe di cambiare il meccanismo DNS con
   OverridePrimary che oggi funziona) e IPv6 blackhole (la rete di casa
   non ha IPv6: non verificabile). Riprenderli solo se Luke lo chiede.
3. Nota: "Rimuovi profilo" non ferma un tunnel attivo (il daemon ha già la
   config in memoria): valutare `STOP` prima della rimozione.
5. UI: stato connessione visibile (STATS periodico) e testo del pulsante
   durante START (oggi "Attendere...").

## Prossimo passo esatto (da eseguire sul PC)

Prima, due operazioni manuali: gli strumenti remoti della sessione non
possono scrivere file chiamati `Makefile` né impostare il bit di esecuzione.
Il nuovo Makefile del daemon è quindi in `DaemonPrototype/Makefile.start-profile`
(il vecchio è salvato in `_backup-pre-start-profile/DaemonPrototype/Makefile.orig`):

```bash
cd /home/luke/Development/iOS-Legacy/NineGuard
cp DaemonPrototype/Makefile.start-profile DaemonPrototype/Makefile
chmod +x DaemonPrototype/tests/device-start-profile.sh \
         DaemonPrototype/tests/host/run-host-tests.sh TestPeer/build-test-peer.sh
```

1. Build daemon + helper: `make -C DaemonPrototype clean signed`.
   Se fallisce, correggere qui (gli oggetti compilavano con header 9.3).
2. Aggiornare il companion (sostituisce solo daemon/engine/launchd, non
   l'app): `DaemonPrototype/tests/device-start-profile.sh install`.
   Atteso `dev.luke.nineguard.daemon 0.2.0-1+debug install ok installed`.
   Se `PING` poi fallisce, aprire l'app una volta (crea/legge il segreto).
3. Gate 5 (solo UAPI, nessun IP/route):
   `DaemonPrototype/tests/device-start-profile.sh uapi` →
   `START_PROFILE=OK PEERS=1 ...`, `STATS=OK PEERS=1 HANDSHAKE_AGE=NEVER`,
   `UTUN_IPV4_ADDRESSES=0`, `DEVICE_PHASE_uapi=OK`.
4. Gate 6 (IPv4 + host route sintetica 198.51.100.7):
   `DaemonPrototype/tests/device-start-profile.sh net` → `NET_UP=OK ROUTES=1`,
   `UTUN_IPV4_ADDRESSES=1`, `UDP_SEND=OK`, `NET_DOWN=OK`, `DEVICE_PHASE_net=OK`.
   (Il peer sintetico è TEST-NET 192.0.2.1: atteso TX>0 e handshake NEVER.)
5. Gate 7 (peer esterno controllato): `TestPeer/build-test-peer.sh`, poi
   avviare `TestPeer/build/ngtestpeer -endpoint-host <IP_PC> ...` (vedi
   `TestPeer/README.md`) e lanciare la fase `peer` con `TESTPEER_CONF`.
   Atteso `UDP_ECHO=OK`, handshake numerico, `DEVICE_PHASE_peer=OK`.
6. Solo dopo i tre gate: collegare il pulsante Connessione a
   `startProfileID:` + `NET_UP` (e `STOP` per disconnettere), build 14 IPA.
7. Dopo: full tunnel (route 0/1 + 128/1 con host route verso l'endpoint via
   gateway Wi-Fi) e DNS — ancora da progettare, con rollback obbligatorio.

Se un gate fallisce: lo script esegue comunque `NET_DOWN`, `STOP` e rimozione
del profilo. Guardare `/var/log/nineguardd.log` (righe `IPC_REPLY=`) e
`/var/log/nineguard-engine.log` (non pubblicarne il contenuto: può contenere
endpoint).

## Note operative importanti

- `installipa` stampa successo ma restituisce spesso exit code `1`; verificare
  sempre l'app realmente installata e il processo.
- subito dopo `installipa`, LaunchServices può richiedere alcuni secondi o un
  secondo open; la build 13 non ha prodotto crash report.
- sovrascrivere ripetutamente lo stesso Mach-O in `/tmp` può incontrare cache
  di firma/vnode su iOS 9 e terminare con `SIGKILL`; usare un nome remoto nuovo
  oppure installare tramite `dpkg`.
- `launchctl unload` può lasciare un socket stale; `prerm` e `postinst` del
  companion rimuovono soltanto il percorso esatto del socket.
- per ispezionare le interfacce usare un helper `getifaddrs`; su questo device
  non fare affidamento su `/sbin/ifconfig`.
- non usare Git finché il lavoro non è concluso e verificato.

## Prompt minimo per la prossima sessione

```text
Lavora in /home/luke/Development/iOS-Legacy/NineGuard. Leggi per intero
NINEGUARD-2-CONTINUATION.md (sezioni "Sessione 2" e "Prossimo passo esatto").
App build 15 IPA installata (pulsante Connetti/Disconnetti verificato con
ngtestpeer); companion 0.2.0-1+debug installato; gate install/uapi/net/peer
e gate 8 UI superati (sessione 3). Non rifare ABI, route, round-trip, IPC, launchd, Keychain né
i gate 5-7. Prossimo: "Da fare (sessione 4)". Nessuna default route/DNS,
patch piccole, target iOS 9 ARMv7, niente Git.
```

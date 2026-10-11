# NineGuard — Guida / Guide

**[Italiano](#italiano)** · **[English](#english)**

## Italiano

Come funziona NineGuard, come si compila, si installa e si usa.
Per i dettagli di sviluppo e la storia dei test vedi i documenti elencati
nel `README.md` (in italiano).

### 1. Cos'è

NineGuard è un client WireGuard per **iPad mini 1 con iOS 9.3.6
jailbroken** (verificato su `iPad2,7`, jailbreak Carbon + iocaste).

Su iOS 9 la strada normale per una VPN è NetworkExtension
(`NEPacketTunnelProvider`), ma con il jailbreak le app extension di terze
parti non partono (`invalid sandbox or container`). NineGuard quindi fa a
meno di NetworkExtension e si divide in tre pezzi:

| Pezzo | Dove sta | Cosa fa |
| --- | --- | --- |
| **App NineGuard** (IPA) | container dell'app, utente `mobile` | interfaccia, import QR, profili nel Keychain, comandi al daemon |
| **Daemon `nineguardd`** | `/usr/libexec/nineguard/`, root, avviato da launchd | avvia il motore, configura interfaccia, route e DNS, controlla la rete |
| **Motore `wireguard-go`** | `/usr/libexec/nineguard/`, figlio del daemon | il protocollo WireGuard vero e proprio, su `utun0` |

Daemon e motore sono installati da un pacchetto `.deb` separato (il
"companion", `dev.luke.nineguard.daemon`), gestito da `dpkg`. L'app resta
un'IPA normale: i due si aggiornano in modo indipendente.

### 2. Come funziona

#### Comunicazione app → daemon

- Il daemon ascolta su un socket Unix, `/var/run/nineguard/nineguardd.sock`
  (`root:mobile`, permessi `0660`), e accetta solo processi dell'utente
  `mobile`.
- Ogni connessione è autenticata in entrambe le direzioni (HMAC-SHA256 con
  nonce casuale) con un segreto di 32 byte nel gruppo Keychain condiviso
  `dev.luke.nineguard.shared`. **Il segreto lo crea l'app al primo avvio**:
  finché non esiste, il daemon resta in attesa senza aprire il socket.
- Comandi principali: `PING`, `STATUS`, `START_PROFILE <UUID>`, `NET_UP`,
  `NET_DOWN`, `STATS`, `STOP`. Sul socket passa solo l'UUID del profilo,
  mai la configurazione o le chiavi.

#### Profili

- Il profilo si importa da QR (formato standard `wg-quick`). L'app lo
  valida e salva **tutta la configurazione nel Keychain** condiviso; nelle
  preferenze restano solo l'UUID e il numero di peer.
- Con `START_PROFILE` il daemon (root) rilegge il profilo dal Keychain, lo
  rivalida, avvia `wireguard-go -f utun0` e gli passa la configurazione via
  UAPI. Gli endpoint con nome host vengono risolti dal daemon (preferenza
  IPv4).

#### Rete (`NET_UP`)

1. Assegna a `utun0` il primo IPv4 di `Address` come /32 point-to-point e,
   se c'è `MTU =` nel profilo (576–1500), imposta l'MTU della utun
   (default 1420).
2. Trasforma gli `AllowedIPs` IPv4 in route verso `utun0`. Le route che
   cadono dentro la rete locale (la LAN del Wi-Fi) vengono saltate, così la
   LAN resta raggiungibile.
3. **Full tunnel**: se il profilo ha `AllowedIPs = 0.0.0.0/0`, il daemon
   aggiunge `0.0.0.0/1` + `128.0.0.0/1` (come `wg-quick`): più specifiche
   della default route, che non viene mai toccata. Prima aggiunge una host
   route "di bypass" verso il server VPN attraverso il gateway Wi-Fi, così i
   pacchetti cifrati non rientrano nel tunnel.
4. **DNS** (solo in full tunnel e se il profilo ha `DNS =`): il daemon
   pubblica in configd un servizio temporaneo con i DNS della VPN e
   `OverridePrimary`. Le chiavi sono legate al processo del daemon: se il
   daemon muore, configd le rimuove da solo.

#### Cambio rete (watchdog)

Con il tunnel attivo, ogni 5 secondi il daemon confronta la route di bypass
con il router della rete Wi-Fi attuale (letto da configd):

- cambia Wi-Fi o gateway → sposta il bypass sul nuovo router;
- la route è sparita → la ricrea;
- nessuna rete → aspetta senza toccare niente (in full tunnel il traffico
  resta chiuso nel tunnel, nessuna fuga in chiaro);
- se il bypass non si riesce a ricreare per 30 secondi con una rete
  presente → ferma il tunnel (`STOP`) per sicurezza.

#### Ripristino

`NET_DOWN` e `STOP` rimuovono indirizzo, route, bypass, DNS e MTU. Se il
motore crasha, il daemon lo rileva subito e ripulisce. Se crasha il daemon,
launchd lo riavvia: le chiavi DNS spariscono da sole e i bypass rimasti
(registrati in `/var/run/nineguard/net.state`) vengono rimossi all'avvio.

### 3. Requisiti

**iPad**

- iPad (o iPhone) con iOS 9 a 32 bit (ARMv7), jailbroken. Testato solo su
  iPad mini 1 (`iPad2,7`) con iOS 9.3.6 e jailbreak untethered (iocaste).
- Pacchetti Cydia (versioni presenti sull'iPad di test):

  | Pacchetto | ID | Versione | Repo | Serve per |
  | --- | --- | --- | --- | --- |
  | OpenSSH | `openssh` | 6.7p1-13 | predefinito (saurik) | accesso root via SSH |
  | AppSync Unified | `ai.akemi.appsyncunified` | 116.2 | `https://cydia.akemi.ai/` | installare l'IPA firmata ad hoc |
  | IPA Installer Console | `com.autopear.installipa` | 3.4.1-1 | BigBoss (predefinito) | comando `installipa` |
  | UIKit Tools | `uikittools` | 1.1.12 | predefinito | `uiopen` (facoltativo) |

  `dpkg` è già presente con Cydia. Dopo aver installato OpenSSH **cambia
  subito le password di `root` e `mobile`** (il default di iOS è `alpine`):
  da SSH come root, `passwd` e poi `passwd mobile`.
- iPad e PC sulla stessa rete Wi-Fi (serve solo per installare).

**PC di build (Linux)**

- [Theos](https://theos.dev) in `~/theos` (o variabile `THEOS`) con
  toolchain iOS per Linux, SDK `iPhoneOS9.2.sdk` e `ldid`;
- Go **1.14.15** (variabile `NINEGUARD_GO_ROOT`, default
  `~/.local/opt/nineguard-go114/go`): serve per `wireguard-go` 2020 a 32 bit;
- `curl`, `tar`, `sha256sum`, `file`, `zip`, `python3`, `gcc` (test host).

Alcuni iPad vecchi accettano solo SSH con chiavi RSA legacy: in quel caso
aggiungi `-o HostKeyAlgorithms=+ssh-rsa` ai comandi `ssh`.

### 4. File pronti (Releases) oppure compilazione

Se non vuoi compilare, scarica dalla pagina **Releases** del repository
l'IPA (`NineGuard-build*-armv7.ipa`) e il companion
(`dev.luke.nineguard.daemon_*_iphoneos-arm.deb`), controlla i checksum con
`sha256sum -c SHA256SUMS` e passa al punto 5, usando quei file al posto di
quelli compilati.

Per compilare da sorgente:

Dalla radice del repository:

```bash
make engine                          # wireguard-go ARMv7 → Engine/build/
make -C DaemonPackage clean package  # daemon + engine → DaemonPackage/packages/*.deb
make clean && make -j2 stage         # app → .theos/_/Applications/NineGuard.app
DaemonPrototype/tests/host/run-host-tests.sh   # test sul PC (facoltativo)
```

`make engine` scarica solo il commit fissato di `wireguard-go`
(`v0.0.20200320`) e rifiuta un archivio con SHA-256 diverso.

Per ottenere l'IPA dall'app compilata:

```bash
rm -rf /tmp/ng-ipa && mkdir -p /tmp/ng-ipa/Payload
cp -a .theos/_/Applications/NineGuard.app /tmp/ng-ipa/Payload/
(cd /tmp/ng-ipa && zip -qr -X NineGuard.ipa Payload)
```

### 5. Installazione sull'iPad

Sostituisci `IPAD` con `root@<indirizzo dell'iPad>` (l'indirizzo è in
Impostazioni → Wi-Fi → (i) accanto alla rete). I file si copiano con
`ssh ... 'cat > file'` invece di `scp`: gli `scp` recenti usano SFTP, che il
vecchio OpenSSH dell'iPad potrebbe non avere.

I comandi usano i nomi dei file della Release; se hai compilato, usa
`DaemonPackage/packages/dev.luke.nineguard.daemon_*.deb` e
`/tmp/ng-ipa/NineGuard.ipa`.

1. **Companion (daemon + motore)**, prima dell'app:

   ```bash
   ssh IPAD 'cat > /tmp/ng.deb' < dev.luke.nineguard.daemon_0.3.2-1+debug_iphoneos-arm.deb
   ssh IPAD 'dpkg -i /tmp/ng.deb && rm /tmp/ng.deb'
   ```

   Il pacchetto installa `/usr/libexec/nineguard/{nineguardd,wireguard-go}`
   e `/Library/LaunchDaemons/dev.luke.nineguard.daemon.plist`, e carica il
   job launchd. Controllo:
   `dpkg-query -W dev.luke.nineguard.daemon` → `install ok installed`.

   In alternativa, lo script di test fa build, copia e `dpkg -i` in un
   colpo solo: `DaemonPrototype/tests/device-start-profile.sh install`
   (richiede la variabile `IPAD`, ad esempio
   `IPAD=root@<indirizzo-iPad> DaemonPrototype/tests/device-start-profile.sh install`).

2. **App**:

   ```bash
   NAME=/tmp/NineGuard-$(date +%s).ipa
   ssh IPAD "cat > $NAME" < NineGuard-build17-armv7.ipa
   ssh IPAD "installipa $NAME; rm -f $NAME"
   ```

   `installipa` può uscire con codice 1 anche se l'installazione è riuscita:
   controlla che l'icona compaia. Usa sempre un nome file nuovo in `/tmp`
   (iOS 9 tiene in cache le firme dei file sovrascritti).

3. **Primo avvio**: apri NineGuard dalla home. L'app crea il segreto
   condiviso, il daemon apre il socket e sotto il titolo compare
   **"Daemon 2.0 autenticato"**. Se non compare, chiudi e riapri l'app.

Al riavvio dell'iPad launchd fa ripartire il daemon da solo; il tunnel
invece non si riconnette in automatico.

### 6. Uso

- **Importa QR WireGuard**: inquadra il QR del profilo (quello generato da
  `wg-quick`/`qrencode` o dal pannello del tuo server). Il profilo diventa
  quello attivo.
- **Connetti / Disconnetti**: Connetti esegue `START_PROFILE` + `NET_UP`;
  il riquadro di stato dice se tutto il traffico IPv4 passa nel tunnel e
  quali DNS sono in uso. Se la rete non si configura, il tunnel viene
  fermato automaticamente.
- **Riga sotto il titolo** (ogni 5 s, solo a tunnel attivo): tempo
  dall'ultimo handshake e traffico ricevuto/inviato, più "rete assente" o
  "cambio rete..." quando il watchdog sta lavorando. Se il tunnel si ferma
  fuori dall'app, compare "Tunnel non più attivo." e il pulsante torna su
  Connetti.
- **Verifica IP pubblico**: mostra l'IP con cui esci su Internet; in full
  tunnel deve essere quello del server VPN.
- **Rimuovi profilo WireGuard 2.0**: ferma il tunnel e cancella il profilo
  dal Keychain.
- **Verifica daemon 2.0** e **Test Keychain**: diagnostica.

Il tunnel resta attivo anche chiudendo l'app: a gestirlo è il daemon.

### 7. Disinstallazione

1. Nell'app: **Disconnetti**, poi **Rimuovi profilo WireGuard 2.0**.
2. Companion: `ssh IPAD 'dpkg -r dev.luke.nineguard.daemon'` (scarica il
   job launchd e rimuove il socket).
3. App: tieni premuta l'icona e cancellala, come una normale app.
4. Facoltativo: `ssh IPAD 'rm -f /var/log/nineguardd.log /var/log/nineguard-engine.log'`.

### 8. Problemi comuni

| Sintomo | Cosa controllare |
| --- | --- |
| "Daemon 2.0 autenticato" non compare | `ssh IPAD 'dpkg-query -W dev.luke.nineguard.daemon; launchctl list \| grep nineguard; ls /var/run/nineguard'`; poi riapri l'app (crea il segreto). |
| Connetti → `REFUSED_...` | Il profilo non è valido per il daemon (`KEY`, `ENDPOINT`, `ALLOWED_IP`, `ADDRESS`, `MTU`, ...): reimporta il QR. |
| Connetti → `REFUSED_UAPI_BUSY` o `ENGINE_FAILED` | Un motore precedente è rimasto appeso: Disconnetti e riprova; in caso estremo `ssh IPAD 'launchctl unload /Library/LaunchDaemons/dev.luke.nineguard.daemon.plist; launchctl load /Library/LaunchDaemons/dev.luke.nineguard.daemon.plist'`. |
| Handshake sempre "in attesa" | Server irraggiungibile o chiavi errate: verifica endpoint e porta UDP dal lato server. |
| Dopo un problema la rete sembra strana | `STOP` ripristina tutto; se il daemon era morto, al riavvio ripulisce da solo. Verifica: `ssh IPAD "printf 'show State:/Network/Global/IPv4\nquit\n' \| scutil"` deve riportare `PrimaryInterface : en0`. |

Log sull'iPad: `/var/log/nineguardd.log` (comandi e risposte del daemon,
senza chiavi) e `/var/log/nineguard-engine.log` (errori del motore: può
contenere l'endpoint, non pubblicarlo).

### 9. Limiti noti

- **Icona Wi-Fi**: in full tunnel sparisce dalla barra di stato, perché il
  tunnel diventa l'interfaccia primaria (serve per i DNS). Il Wi-Fi resta
  connesso.
- **IPv6**: non instradato nel tunnel. Su una rete con IPv6 il traffico
  IPv6 uscirebbe in chiaro; finora testato solo su reti solo IPv4.
- **DNS** del profilo applicati solo in full tunnel.
- Un solo IPv4 di `Address` (il primo), sempre /32.
- Il watchdog controlla la rete ogni 5 s: subito dopo un cambio Wi-Fi il
  tunnel può restare fermo per qualche secondo.
- `wireguard-go` è la versione 2020 (`v0.0.20200320`), l'ultima che si
  compila con Go 1.14 per iOS 9 a 32 bit.
- Nel repository è presente anche una vecchia extension NetworkExtension
  (`Tunnel/`), inclusa nell'IPA ma non usata.

---

## English

[Italiano](#italiano) · **English**

How NineGuard works, and how to build, install and use it. The other
development documents in the repository (session logs, design notes) are
in Italian.

The app interface is in Italian: button names are quoted as they appear,
with a translation in parentheses.

### 1. What it is

NineGuard is a WireGuard client for **jailbroken iPad mini 1 running
iOS 9.3.6** (verified on `iPad2,7`, Carbon + iocaste jailbreak).

On iOS 9 the normal way to build a VPN is NetworkExtension
(`NEPacketTunnelProvider`), but under the jailbreak third-party app
extensions fail to start (`invalid sandbox or container`). NineGuard
therefore does without NetworkExtension and is split into three parts:

| Part | Where it lives | What it does |
| --- | --- | --- |
| **NineGuard app** (IPA) | app container, user `mobile` | UI, QR import, profiles in the Keychain, commands to the daemon |
| **`nineguardd` daemon** | `/usr/libexec/nineguard/`, root, started by launchd | starts the engine, configures interface, routes and DNS, watches the network |
| **`wireguard-go` engine** | `/usr/libexec/nineguard/`, child of the daemon | the actual WireGuard protocol, on `utun0` |

Daemon and engine are installed by a separate `.deb` package (the
"companion", `dev.luke.nineguard.daemon`) managed by `dpkg`. The app stays
a regular IPA: the two are updated independently.

### 2. How it works

#### App → daemon communication

- The daemon listens on a Unix socket, `/var/run/nineguard/nineguardd.sock`
  (`root:mobile`, mode `0660`), and only accepts processes running as
  `mobile`.
- Every connection is mutually authenticated (HMAC-SHA256 with a random
  nonce) using a 32-byte secret stored in the shared Keychain group
  `dev.luke.nineguard.shared`. **The app creates the secret on first
  launch**: until it exists, the daemon waits without opening the socket.
- Main commands: `PING`, `STATUS`, `START_PROFILE <UUID>`, `NET_UP`,
  `NET_DOWN`, `STATS`, `STOP`. Only the profile UUID crosses the socket,
  never the configuration or the keys.

#### Profiles

- Profiles are imported from a QR code (standard `wg-quick` format). The
  app validates the profile and stores **the whole configuration in the
  shared Keychain**; the preferences only keep the UUID and the number of
  peers.
- On `START_PROFILE` the daemon (root) reads the profile back from the
  Keychain, validates it again, starts `wireguard-go -f utun0` and sends it
  the configuration over UAPI. Endpoint hostnames are resolved by the
  daemon (IPv4 preferred).

#### Network (`NET_UP`)

1. Assigns the first IPv4 of `Address` to `utun0` as a point-to-point /32
   and, if the profile has `MTU =` (576–1500), sets the utun MTU
   (default 1420).
2. Turns the IPv4 `AllowedIPs` into routes to `utun0`. Routes that fall
   inside the local network (the Wi-Fi LAN) are skipped, so the LAN stays
   reachable.
3. **Full tunnel**: if the profile has `AllowedIPs = 0.0.0.0/0`, the
   daemon adds `0.0.0.0/1` + `128.0.0.0/1` (like `wg-quick`): more specific
   than the default route, which is never touched. Before that it adds a
   "bypass" host route to the VPN server through the Wi-Fi gateway, so the
   encrypted packets do not loop back into the tunnel.
4. **DNS** (full tunnel only, and only if the profile has `DNS =`): the
   daemon publishes a temporary service in configd with the VPN DNS servers
   and `OverridePrimary`. The keys are tied to the daemon process: if the
   daemon dies, configd removes them by itself.

#### Network changes (watchdog)

While the tunnel is up, every 5 seconds the daemon compares the bypass
route with the router of the current Wi-Fi network (read from configd):

- Wi-Fi or gateway changed → moves the bypass to the new router;
- the route disappeared → recreates it;
- no network → waits without touching anything (in full tunnel the
  traffic stays locked inside the tunnel, nothing leaks in the clear);
- if the bypass cannot be recreated for 30 seconds while a network is
  present → stops the tunnel (`STOP`) to stay safe.

#### Recovery

`NET_DOWN` and `STOP` remove address, routes, bypass, DNS and MTU. If the
engine crashes, the daemon notices immediately and cleans up. If the
daemon crashes, launchd restarts it: the DNS keys disappear by themselves
and leftover bypass routes (recorded in `/var/run/nineguard/net.state`) are
removed at startup.

### 3. Requirements

**iPad**

- iPad (or iPhone) on 32-bit (ARMv7) iOS 9, jailbroken. Only tested on an
  iPad mini 1 (`iPad2,7`) with iOS 9.3.6 and an untethered jailbreak
  (iocaste).
- Cydia packages (versions installed on the test iPad):

  | Package | ID | Version | Repo | Needed for |
  | --- | --- | --- | --- | --- |
  | OpenSSH | `openssh` | 6.7p1-13 | default (saurik) | root access over SSH |
  | AppSync Unified | `ai.akemi.appsyncunified` | 116.2 | `https://cydia.akemi.ai/` | installing the ad hoc signed IPA |
  | IPA Installer Console | `com.autopear.installipa` | 3.4.1-1 | BigBoss (default) | the `installipa` command |
  | UIKit Tools | `uikittools` | 1.1.12 | default | `uiopen` (optional) |

  `dpkg` already comes with Cydia. After installing OpenSSH, **change the
  `root` and `mobile` passwords right away** (the iOS default is `alpine`):
  over SSH as root, run `passwd` and then `passwd mobile`.
- iPad and PC on the same Wi-Fi network (only needed for installation).

**Build PC (Linux)**

- [Theos](https://theos.dev) in `~/theos` (or the `THEOS` variable) with
  the iOS toolchain for Linux, the `iPhoneOS9.2.sdk` SDK and `ldid`;
- Go **1.14.15** (`NINEGUARD_GO_ROOT` variable, default
  `~/.local/opt/nineguard-go114/go`): needed for 32-bit `wireguard-go`
  2020;
- `curl`, `tar`, `sha256sum`, `file`, `zip`, `python3`, `gcc` (host tests).

Some old iPads only accept SSH with legacy RSA host keys: in that case add
`-o HostKeyAlgorithms=+ssh-rsa` to the `ssh` commands.

### 4. Prebuilt files (Releases) or building

If you do not want to build, download from the repository **Releases**
page the IPA (`NineGuard-build*-armv7.ipa`) and the companion
(`dev.luke.nineguard.daemon_*_iphoneos-arm.deb`), verify the checksums with
`sha256sum -c SHA256SUMS` and go to step 5, using those files instead of
the built ones.

To build from source, from the repository root:

```bash
make engine                          # wireguard-go ARMv7 → Engine/build/
make -C DaemonPackage clean package  # daemon + engine → DaemonPackage/packages/*.deb
make clean && make -j2 stage         # app → .theos/_/Applications/NineGuard.app
DaemonPrototype/tests/host/run-host-tests.sh   # tests on the PC (optional)
```

`make engine` only downloads the pinned `wireguard-go` commit
(`v0.0.20200320`) and rejects an archive with a different SHA-256.

To package the built app as an IPA:

```bash
rm -rf /tmp/ng-ipa && mkdir -p /tmp/ng-ipa/Payload
cp -a .theos/_/Applications/NineGuard.app /tmp/ng-ipa/Payload/
(cd /tmp/ng-ipa && zip -qr -X NineGuard.ipa Payload)
```

### 5. Installing on the iPad

Replace `IPAD` with `root@<iPad address>` (the address is in Settings →
Wi-Fi → (i) next to the network). Files are copied with
`ssh ... 'cat > file'` instead of `scp`: recent `scp` versions use SFTP,
which the iPad's old OpenSSH may not provide.

The commands use the Release file names; if you built from source, use
`DaemonPackage/packages/dev.luke.nineguard.daemon_*.deb` and
`/tmp/ng-ipa/NineGuard.ipa`.

1. **Companion (daemon + engine)**, before the app:

   ```bash
   ssh IPAD 'cat > /tmp/ng.deb' < dev.luke.nineguard.daemon_0.3.2-1+debug_iphoneos-arm.deb
   ssh IPAD 'dpkg -i /tmp/ng.deb && rm /tmp/ng.deb'
   ```

   The package installs `/usr/libexec/nineguard/{nineguardd,wireguard-go}`
   and `/Library/LaunchDaemons/dev.luke.nineguard.daemon.plist`, and loads
   the launchd job. Check:
   `dpkg-query -W dev.luke.nineguard.daemon` → `install ok installed`.

   Alternatively, the test script builds, copies and runs `dpkg -i` in one
   go:
   `IPAD=root@<iPad address> DaemonPrototype/tests/device-start-profile.sh install`.

2. **App**:

   ```bash
   NAME=/tmp/NineGuard-$(date +%s).ipa
   ssh IPAD "cat > $NAME" < NineGuard-build17-armv7.ipa
   ssh IPAD "installipa $NAME; rm -f $NAME"
   ```

   `installipa` may exit with code 1 even when the installation succeeded:
   check that the icon appears. Always use a new file name in `/tmp`
   (iOS 9 caches the signatures of overwritten files).

3. **First launch**: open NineGuard from the home screen. The app creates
   the shared secret, the daemon opens its socket and **"Daemon 2.0
   autenticato"** (daemon authenticated) appears under the title. If it
   does not, close and reopen the app.

When the iPad reboots, launchd restarts the daemon by itself; the tunnel
does not reconnect automatically.

### 6. Usage

- **"Importa QR WireGuard"** (import WireGuard QR): scan the profile QR
  code (generated by `wg-quick`/`qrencode` or by your server's panel). The
  profile becomes the active one.
- **"Connetti" / "Disconnetti"** (connect / disconnect): Connect runs
  `START_PROFILE` + `NET_UP`; the status box tells whether all IPv4 traffic
  goes through the tunnel and which DNS servers are in use. If the network
  cannot be configured, the tunnel is stopped automatically.
- **Line under the title** (every 5 s, only while connected): time since
  the last handshake and received/sent traffic, plus "rete assente" (no
  network) or "cambio rete..." (network change) while the watchdog is at
  work. If the tunnel stops outside the app, "Tunnel non più attivo."
  (tunnel no longer active) appears and the button goes back to Connect.
- **"Verifica IP pubblico"** (check public IP): shows the IP you use on
  the Internet; in full tunnel it must be the VPN server's.
- **"Rimuovi profilo WireGuard 2.0"** (remove profile): stops the tunnel
  and deletes the profile from the Keychain.
- **"Verifica daemon 2.0"** and **"Test Keychain"**: diagnostics.

The tunnel stays up even if you close the app: the daemon manages it.

### 7. Uninstalling

1. In the app: **"Disconnetti"**, then **"Rimuovi profilo WireGuard 2.0"**.
2. Companion: `ssh IPAD 'dpkg -r dev.luke.nineguard.daemon'` (unloads the
   launchd job and removes the socket).
3. App: long-press the icon and delete it like any other app.
4. Optional: `ssh IPAD 'rm -f /var/log/nineguardd.log /var/log/nineguard-engine.log'`.

### 8. Troubleshooting

| Symptom | What to check |
| --- | --- |
| "Daemon 2.0 autenticato" does not appear | `ssh IPAD 'dpkg-query -W dev.luke.nineguard.daemon; launchctl list \| grep nineguard; ls /var/run/nineguard'`; then reopen the app (it creates the secret). |
| Connect → `REFUSED_...` | The daemon rejects the profile (`KEY`, `ENDPOINT`, `ALLOWED_IP`, `ADDRESS`, `MTU`, ...): import the QR again. |
| Connect → `REFUSED_UAPI_BUSY` or `ENGINE_FAILED` | A previous engine is stuck: disconnect and retry; as a last resort `ssh IPAD 'launchctl unload /Library/LaunchDaemons/dev.luke.nineguard.daemon.plist; launchctl load /Library/LaunchDaemons/dev.luke.nineguard.daemon.plist'`. |
| Handshake always "in attesa" (waiting) | Server unreachable or wrong keys: check the endpoint and the UDP port on the server side. |
| Networking looks odd after a problem | `STOP` restores everything; if the daemon had died, it cleans up by itself when it restarts. Check: `ssh IPAD "printf 'show State:/Network/Global/IPv4\nquit\n' \| scutil"` must report `PrimaryInterface : en0`. |

Logs on the iPad: `/var/log/nineguardd.log` (daemon commands and replies,
no keys) and `/var/log/nineguard-engine.log` (engine errors: may contain
the endpoint, do not publish it).

### 9. Known limitations

- **Wi-Fi icon**: in full tunnel it disappears from the status bar,
  because the tunnel becomes the primary interface (needed for DNS). Wi-Fi
  stays connected.
- **IPv6**: not routed through the tunnel. On a network with IPv6, IPv6
  traffic would leave in the clear; so far only tested on IPv4-only
  networks.
- Profile **DNS** is only applied in full tunnel.
- Only one IPv4 from `Address` (the first), always /32.
- The watchdog checks the network every 5 s: right after a Wi-Fi change
  the tunnel may stall for a few seconds.
- `wireguard-go` is the 2020 version (`v0.0.20200320`), the last one that
  builds with Go 1.14 for 32-bit iOS 9.
- The repository also contains an old NetworkExtension extension
  (`Tunnel/`), included in the IPA but unused.

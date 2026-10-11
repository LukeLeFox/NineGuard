# NineGuard — guida

Come funziona NineGuard, come si compila, si installa e si usa.
Per i dettagli di sviluppo e la storia dei test vedi i documenti elencati
nel `README.md`.

## 1. Cos'è

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

## 2. Come funziona

### Comunicazione app → daemon

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

### Profili

- Il profilo si importa da QR (formato standard `wg-quick`). L'app lo
  valida e salva **tutta la configurazione nel Keychain** condiviso; nelle
  preferenze restano solo l'UUID e il numero di peer.
- Con `START_PROFILE` il daemon (root) rilegge il profilo dal Keychain, lo
  rivalida, avvia `wireguard-go -f utun0` e gli passa la configurazione via
  UAPI. Gli endpoint con nome host vengono risolti dal daemon (preferenza
  IPv4).

### Rete (`NET_UP`)

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

### Cambio rete (watchdog)

Con il tunnel attivo, ogni 5 secondi il daemon confronta la route di bypass
con il router della rete Wi-Fi attuale (letto da configd):

- cambia Wi-Fi o gateway → sposta il bypass sul nuovo router;
- la route è sparita → la ricrea;
- nessuna rete → aspetta senza toccare niente (in full tunnel il traffico
  resta chiuso nel tunnel, nessuna fuga in chiaro);
- se il bypass non si riesce a ricreare per 30 secondi con una rete
  presente → ferma il tunnel (`STOP`) per sicurezza.

### Ripristino

`NET_DOWN` e `STOP` rimuovono indirizzo, route, bypass, DNS e MTU. Se il
motore crasha, il daemon lo rileva subito e ripulisce. Se crasha il daemon,
launchd lo riavvia: le chiavi DNS spariscono da sole e i bypass rimasti
(registrati in `/var/run/nineguard/net.state`) vengono rimossi all'avvio.

## 3. Requisiti

**iPad**

- iPad con iOS 9 a 32 bit (ARMv7), jailbroken, testato solo su iPad mini 1
  con iOS 9.3.6;
- OpenSSH (accesso root) e `dpkg`;
- un modo per installare IPA firmate ad hoc, ad esempio `installipa`
  (IPA Installer Console), con AppSync Unified se il jailbreak lo richiede.

**PC di build (Linux)**

- [Theos](https://theos.dev) in `~/theos` (o variabile `THEOS`) con
  toolchain iOS per Linux, SDK `iPhoneOS9.2.sdk` e `ldid`;
- Go **1.14.15** (variabile `NINEGUARD_GO_ROOT`, default
  `~/.local/opt/nineguard-go114/go`): serve per `wireguard-go` 2020 a 32 bit;
- `curl`, `tar`, `sha256sum`, `file`, `zip`, `python3`, `gcc` (test host).

Alcuni iPad vecchi accettano solo SSH con chiavi RSA legacy: in quel caso
aggiungi `-o HostKeyAlgorithms=+ssh-rsa` ai comandi `ssh`.

## 4. File pronti (Releases) oppure compilazione

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

## 5. Installazione sull'iPad

Sostituisci `IPAD` con `root@<indirizzo dell'iPad>`. I file si copiano con
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

## 6. Uso

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

## 7. Disinstallazione

1. Nell'app: **Disconnetti**, poi **Rimuovi profilo WireGuard 2.0**.
2. Companion: `ssh IPAD 'dpkg -r dev.luke.nineguard.daemon'` (scarica il
   job launchd e rimuove il socket).
3. App: tieni premuta l'icona e cancellala, come una normale app.
4. Facoltativo: `ssh IPAD 'rm -f /var/log/nineguardd.log /var/log/nineguard-engine.log'`.

## 8. Problemi comuni

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

## 9. Limiti noti

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

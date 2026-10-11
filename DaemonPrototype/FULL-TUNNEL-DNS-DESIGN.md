# NineGuard — design full tunnel + DNS (iOS 9, senza NetworkExtension)

Stato: design approvato da Luke l'11 ottobre 2026 ("progettiamo anche full + dns,
vorrei arrivare a fare un test vero con un profilo"). **Implementato nel
companion 0.3.0 e app build 16; gate F1 e F2 superati sul device** (vedi in
fondo). Meccanismo DNS scelto: A (servizio proprio, chiavi temporanee,
OverridePrimary) — funziona su iOS 9, il fallback B non è stato necessario.

## Obiettivo

Un profilo con `AllowedIPs = 0.0.0.0/0` e `DNS = ...` deve, dopo
Connetti, instradare tutto il traffico IPv4 nel tunnel e usare i DNS del
profilo, con ripristino completo a Disconnetti, a crash del motore e a
crash/riavvio del daemon. Comportamento allineato a wg-quick: il profilo
decide (AllowedIPs/DNS), l'app non ha un interruttore "full" separato.

## Situazione rilevata sul device (sola lettura, scutil)

```text
State:/Network/Global/IPv4  PrimaryInterface=en0  PrimaryService=<Wi-Fi>  Router=<gw LAN>
State:/Network/Global/DNS   ServerAddresses=[<dns LAN>]
State:/Network/Service/<Wi-Fi>/DNS  (scritto da IPConfiguration/DHCP)
State:/Network/Global/IPv6  assente (nessun IPv6 primario oggi)
plugin configd: IPMonitor, IPConfiguration, PPPController, ...
SDK 9.2: header SCDynamicStore presenti (SCDynamicStoreAddTemporaryValue ecc.)
```

## Routing (wg-quick style)

Calcolato in `NGProfileUAPI` (plan) e applicato in `NGNetConfig`:

1. IPv4 /32 point-to-point su utun0 (come oggi).
2. Ogni AllowedIP IPv4 diventa una route verso utun0 (gateway = sockaddr_dl
   della utun, come le host route già verificate):
   - /32 → host route (comportamento attuale);
   - /1../31 → route di rete con `RTA_NETMASK`;
   - /0 → **coppia 0.0.0.0/1 + 128.0.0.0/1** (più specifiche della default
     route, che non viene mai toccata né sostituita).
3. **Protezione LAN**: una route che coincide con, o è contenuta in, una
   subnet direttamente connessa di un'interfaccia non-utun (es. la /24 del
   Wi-Fi) viene saltata (`SKIPPED`). La /24 connessa resta più specifica delle
   /1, quindi **SSH dal PC sulla LAN continua a funzionare in full tunnel**.
4. **Bypass endpoint** (solo se c'è /0 o una route che contiene l'endpoint):
   prima di aggiungere le route, `RTM_GET` sull'IPv4 di ogni endpoint;
   - se la route trovata ha `RTF_GATEWAY` → host route statica endpoint/32
     via quel gateway IPv4 (es. il router Wi-Fi);
   - se l'endpoint è on-link (subnet connessa) → nessun bypass necessario.
5. Ordine: indirizzo → bypass → route; rollback in ordine inverso a ogni
   errore. `NET_DOWN` rimuove tutto, idempotente.
6. IPv6: AllowedIPs IPv6 restano `SKIPPED` (utun senza IPv6). Se in futuro
   compare `State:/Network/Global/IPv6`, fase 2: route blackhole ::/1 e
   8000::/1 verso utun per evitare leak.

## DNS

Solo se il profilo ha `DNS =` **e** il routing è full (/0). Server IPv4 (max
4); voci non-IP del campo DNS → `SearchDomains`. IPv6 ignorati.

Meccanismo preferito (A) — servizio proprio, chiavi **temporanee**:

```text
State:/Network/Service/dev.luke.nineguard/IPv4
  { InterfaceName=utun0, Addresses=[<ip tunnel>], Router=<ip tunnel>,
    OverridePrimary=1 }
State:/Network/Service/dev.luke.nineguard/DNS
  { ServerAddresses=[...], SearchDomains=[...] }
```

È lo schema usato da PPPController per le VPN di sistema: IPMonitor rende
primario il servizio e pubblica il suo DNS come resolver di default.
Con `SCDynamicStoreAddTemporaryValue` configd **rimuove le chiavi da solo se
il processo del daemon muore**: ripristino DNS anche su crash, senza file.

Fallback (B) — stile Tunnelblick: sovrascrivere
`State:/Network/Service/<primario>/DNS`, salvando il valore originale in
`/var/run/nineguard/dns.backup` (0600) e ripristinandolo a `NET_DOWN` e
all'avvio del daemon se il backup esiste. Svantaggi: DHCP può riscrivere la
chiave (serve watchdog), crash → DNS rotto fino al ripristino.

La scelta A/B si decide con i probe F1/F2 sotto.

## Robustezza

- Motore morto → utun distrutta → le route /1 spariscono da sole; il daemon
  (già oggi in `refresh_engine`) esegue `NET_DOWN` e rimuove DNS/bypass.
- Daemon morto → chiavi DNS temporanee rimosse da configd; la host route di
  bypass resta (innocua) ed è registrata in `/var/run/nineguard/net.state`,
  che il daemon pulisce all'avvio.
- Fase 2: watchdog cambio rete — implementato nel companion 0.3.1 (sotto).

## IPC e app

- `NET_UP` invariato come comando; risposta estesa:
  `NET_UP=OK ROUTES=<n> FULL=YES|NO BYPASS=<n> DNS=YES|NO|SKIPPED SKIPPED=<n>`.
- `STATS`/`STATUS` aggiungono `FULL=` e `DNS=`.
- App: nessun nuovo pulsante; Connetti già esegue START_PROFILE + NET_UP.
  "Verifica IP pubblico" serve da prova end-to-end con il profilo reale.

## Gate di verifica

- **F1** (impatto zero): `ngdnsprobe supplemental` — servizio temporaneo con
  solo DNS `SupplementalMatchDomains=[nineguard.test]`; verificare con
  `scutil --dns` che compaia il resolver e che sparisca a fine probe/kill.
  Verifica permessi di scrittura e cleanup automatico delle temporanee.
- **F2** (con ngtestpeer, `-full`): profilo AllowedIPs 0.0.0.0/0, DNS
  10.253.92.1; ngtestpeer risponde anche a DNS (A fisso 10.253.92.1 per
  qualsiasi nome). Atteso sull'iPad: `NET_UP=OK FULL=YES`, `ngresolve
  probe.nineguard.test` → 10.253.92.1 (resolver di sistema), probe UDP verso
  un IP Internet → contato come DROPPED da ngtestpeer (cattura full), SSH
  LAN vivo; `NET_DOWN` → resolver e route ripristinati. Test di crash:
  `kill -9` del motore e del daemon con verifica ripristino.
- **F3** (test vero): profilo reale di Luke via QR; Connetti; "Verifica IP
  pubblico" mostra l'IP del server VPN; navigazione Safari; Disconnetti
  ripristina IP pubblico originale.

## Risultati sul device (11 ottobre 2026)

- **F1**: `ngdnsprobe supplemental` → `DNSPROBE_ADD_TEMPORARY=OK`, resolver
  `nineguard.test` visibile in `scutil --dns`, DNS globale invariato; chiave
  rimossa da configd sia a uscita normale sia dopo `kill -9`.
- `RTM_GET` (`ngroute get`): Internet → `en0 GW=YES`, LAN → `en0 GW=NO`.
- Regressione gate 6 (`net`): invariato (`ROUTES=1 FULL=NO DNS=NO`).
- **F2 `full`** (ngtestpeer `-full`, endpoint on-link):
  `NET_UP=OK ROUTES=2 FULL=YES BYPASS=0 DNS=YES`; `1.1.1.1`/`8.8.8.8` →
  `utun0`, LAN → `en0` (SSH vivo); `PrimaryInterface : utun0`, DNS globale =
  DNS del tunnel; `ngroute resolve probe.nineguard.test` → `A=10.253.92.1`
  (resolver di sistema attraverso il tunnel); dopo `NET_DOWN` route e DNS
  originali, nessuna chiave NineGuard in configd.
- **F2 `crash`**: `kill -9` motore → entro 2 s route e DNS ripristinati
  (handler SIGCHLD), `ENGINE=STOPPED`. Bug trovato e corretto: socket UAPI
  orfano bloccava i riavvii (`REFUSED_UAPI_BUSY`) → `remove_stale_uapi()`.
  `kill -9` daemon → launchd uccide anche il motore, chiavi DNS temporanee
  rimosse, daemon riavviato (`PONG`, `ENGINE=STOPPED`).
- **F2 `bypass`** (profilo sintetico, endpoint TEST-NET oltre il gateway):
  `BYPASS=1`, endpoint → `en0 GW=YES STATIC_HOST=YES`, resto → `utun0`;
  `NET_DOWN` rimuove il bypass e `net.state`; dopo `kill -9` del daemon il
  bypass viene rimosso da `ng_net_recover()` al riavvio.

- **F3 (test vero, Luke)**: profilo reale importato via QR, iPad su hotspot
  4G (IP pubblico WindTre); Connetti → "Verifica IP pubblico" restituisce
  l'IP di casa (server VPN): full tunnel + DNS funzionanti end-to-end.
  Osservato: con il tunnel attivo l'icona Wi-Fi sparisce dalla barra di
  stato (utun0 diventa primaria via OverridePrimary; il Wi-Fi resta
  connesso). Disconnetti sull'hotspot → IP WindTre di nuovo. Rientro sul
  Wi-Fi di casa, verifica SSH: `ENGINE=STOPPED`, `UTUN_INTERFACES=0`,
  `1.1.1.1 → en0 GW=YES`, `PrimaryInterface : en0`, nessuna chiave
  NineGuard in configd, `net.state` assente, nessun wireguard-go, DNS di
  sistema funzionante, nessun nuovo crash report. **F3 superato.**

Limiti noti (fase 2): icona Wi-Fi nascosta con OverridePrimary (estetico); IPv6
non instradato (nessun IPv6 primario oggi); DNS applicato solo in full
tunnel. (MTU del profilo: gestita dalla 0.3.2, vedi sotto.)

## Fase 2 — watchdog cambio rete (companion 0.3.1, sessione 4)

Problema: con il tunnel attivo, cambiando Wi-Fi/gateway la host route di
bypass verso l'endpoint restava sul vecchio gateway (o spariva dal kernel),
mandando l'endpoint dentro la utun.

- `NGNetWatch.{h,c}`: decisione pura (testata su host) per endpoint:
  on-link → nessun bypass (REMOVE se c'era); router fisico assente →
  `NO_NETWORK` (non tocca nulla: in full tunnel resta tutto nel tunnel,
  nessun leak); bypass assente → ADD; gateway diverso o host route non più
  nel kernel → REPLACE; altrimenti KEEP. Più `ng_watch_grace_expired`.
- `NGNetConfig`: `ng_net_up` registra gli endpoint IPv4 coperti dalle route
  (anche quelli on-link, che su un'altra rete possono richiedere il bypass)
  e il gateway di ogni bypass; `ng_net_watch(router)` raccoglie i fatti
  (subnet connesse, `RTM_GET` sull'endpoint), applica DELETE/ADD e riscrive
  `net.state`. Un router fuori dalle subnet connesse (configd in
  transizione) vale come rete assente. Bypass preesistenti non nostri
  (EEXIST) restano ignorati.
- `ng_dns_physical_router` (`NGDNSConfig.c`): con OverridePrimary
  `Global/IPv4` riporta utun0, quindi scorre
  `State:/Network/Service/[^/]+/IPv4` (`SCDynamicStoreCopyKeyList`),
  ignora il servizio NineGuard e utun/ppp/ipsec, preferisce `en*`, legge
  `Router`. Verificato sul device a tunnel attivo: `ROUTER=YES MATCH=YES`.
- `nineguardd`: il loop usa `select` con timeout 5 s solo quando c'è almeno
  un endpoint da seguire (altrimenti resta bloccante come prima); ogni 5 s
  `watch_network()`. Se l'aggiunta del bypass fallisce con un router
  presente per 30 s → fallback sicuro `STOP` (NET_DOWN + DNS + motore) e
  `NETWATCH=STOPPED`. Senza rete fisica: `NETWATCH=WAITING` a tempo
  indeterminato (fail closed; lo STOP lascerebbe uscire il traffico in
  chiaro al rientro della rete).
- IPC: `STATUS` aggiunge `NETWATCH=OK|WAITING|FAILING|STOPPED REBYPASS=<n>`
  anche con `ENGINE=STOPPED` (evento leggibile dopo uno STOP del watchdog);
  azzerati a ogni `START_PROFILE=OK`. Log: righe `NETWATCH=...` senza IP.
- Helper `ngroute delete <ip>` (test) e `ngroute gwcheck <ip>`
  (`BYPASS=`, `MATCH=` con il router di configd, solo tag FNV a 16 bit dei
  gateway, mai IP).
- Script: fase `watch` (automatica: cancella il bypass come farebbe il
  kernel, atteso ripristino entro un tick, `REBYPASS=1`) e fase `netchange`
  (interattiva: Luke cambia Wi-Fi a mano; logger sul device; atteso bypass
  sul gateway della nuova rete con `MATCH=YES` e 1.1.1.1 → utun0, poi di
  nuovo sul gateway di casa, `REBYPASS>=3`, paracadute STOP a 600 s).

Risultati sul device (11 ottobre 2026, companion 0.3.1):
- `watch`: **OK** — `DELETE 192.0.2.1 OK` → `IF=utun0`; dopo 8 s
  `GWCHECK IF=en0 BYPASS=YES MATCH=YES`, `NETWATCH=OK REBYPASS=1`,
  `net.state` presente.
- Regressione `net`, `bypass`, `full` (endpoint on-link: `REBYPASS=0`),
  `crash`: **OK**. Stato finale pulito, nessun nuovo crash report.
- `netchange` (Luke, cambio Wi-Fi manuale casa → hotspot → casa):
  **OK**. Log del device (tag gateway casa `1565`, hotspot `6a56`): dopo il
  passaggio all'hotspot, al più 2 campioni (≤ 6 s) con `MATCH=NO`, poi
  bypass sul gateway dell'hotspot (`GWTAG=6a56 MATCH=YES`, `REBYPASS=2`);
  al rientro di nuovo su casa (`GWTAG=1565 MATCH=YES`, `REBYPASS=3`).
  1.1.1.1 sempre → utun0 (nessun leak), `NETWATCH=OK` per tutto il tempo
  (nessuno stato WAITING osservato: transizione rapida). Il primo giro
  dello script ha perso il rientro per un bug del controllo SSH
  (`BatchMode=yes` fa fallire l'autenticazione su questo iPad, corretto);
  i dati sono stati letti a mano dal log del device e la pulizia del trap
  è andata a buon fine (NET_DOWN, STOP, profilo rimosso).

Limiti: le route saltate per protezione LAN e le subnet connesse sono
calcolate a NET_UP (su una nuova rete la /24 connessa resta comunque più
specifica delle /1); poll ogni 5 s (fino a ~5 s di endpoint nel tunnel dopo
il cambio rete).

## Fase 2 — MTU del profilo (companion 0.3.2)

- `MTU =` del profilo → `plan.mtu` (576..1500, altrimenti
  `START_PROFILE=REFUSED_MTU`; assente = default del motore 1420). Non è
  una chiave UAPI.
- `NET_UP` imposta `SIOCSIFMTU` sulla utun prima dell'indirizzo e la
  ripristina in `ng_net_down` (anche nei rollback). wireguard-go 2020
  (`tun_darwin.go`) ascolta `RTM_IFINFO` e rilegge l'MTU (`EventMTUUpdate`);
  in ogni caso il kernel frammenta già a livello utun. Risposta
  `NET_UP=... MTU=<n>` (MTU effettiva letta con `SIOCGIFMTU`);
  `nglistutun` stampa `UTUN_INTERFACE=utun0 MTU=<n>`.
- Device: profilo sintetico con `MTU = 1280` (fasi `bypass`/`watch`) →
  `MTU=1280`, dopo `NET_DOWN` di nuovo 1420; `net`/`full`/`crash` senza MTU
  → 1420. Tutte **OK**.

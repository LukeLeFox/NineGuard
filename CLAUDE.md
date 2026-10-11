# NineGuard — istruzioni per Claude Code

Client WireGuard per iPad mini 1 (iOS 9.3.6, jailbreak), senza NetworkExtension:
app IPA + daemon root launchd + wireguard-go ARMv7 su `utun0`.

Prima di tutto leggi `NINEGUARD-2-CONTINUATION.md` (sezioni "Sessione 2" e
"Prossimo passo esatto"). Non rifare test già verificati (ABI, route,
round-trip, IPC, launchd, Keychain).

## Regole

- Target invariato `iphone:clang:9.2:9.0`, `ARCHS = armv7`; Theos in `~/theos` o `$THEOS`.
- iPad: usare la variabile `IPAD=root@<indirizzo-iPad>`; SSH con
  `-o HostKeyAlgorithms=+ssh-rsa -o LogLevel=ERROR`.
- Patch piccole, output limitato, niente Git finché il lavoro non è concluso.
- Mai default route, DNS o modifiche Wi-Fi senza che Luke lo chieda (full
  tunnel + DNS del profilo: chiesto l'11 ott 2026, sempre con ripristino).
- Mai stampare configurazioni, chiavi, PSK o endpoint privati.
- Non installare il pacchetto app+daemon: l'app è IPA containerizzata; si
  aggiorna solo il companion `DaemonPackage` via dpkg.
- Su iOS non c'è `ifconfig`: usa `nglistutun` (getifaddrs).
- Per binari copiati in `/tmp` sull'iPad usa sempre un nome/directory nuovi
  (cache firma/vnode di iOS 9 → SIGKILL).
- `installipa` può uscire con 1 anche se riesce: verifica sempre.
- Mai `kill` di PID calcolati (es. `ps -o ppid=`): su iOS ha colpito
  launchd e riavviato l'iPad. Solo PID registrati esplicitamente
  (`echo $!`), verificati con `ps -p` prima del kill.
- SSH non interattivo verso l'iPad: niente `-o BatchMode=yes` (fallisce
  l'autenticazione); usare `</dev/null`.

## Stato

Companion 0.3.2 (watchdog cambio rete, MTU) + app build 17 (STATS in UI)
installati
(sessione 4, 11 ott 2026).
Superati: gate install/uapi/net/peer, gate 8 UI (Connetti/Disconnetti),
full tunnel + DNS F1/F2 (`DaemonPrototype/FULL-TUNNEL-DNS-DESIGN.md`).
Luke ha chiesto e autorizzato full tunnel + DNS (default via coppia /1,
DNS via configd temporaneo): non è più vietato *in quel flusso*, ma ogni
test deve ripristinare. Test: `DaemonPrototype/tests/device-start-profile.sh
install|uapi|net|peer|full|crash|bypass|watch|netchange` (peer/full/crash: ngtestpeer, vedi
`TestPeer/README.md`, IP PC 10.10.99.2). Avvio app da SSH: `uiopen
nineguard://` (iPad sbloccato). F3 con profilo reale superato
(IP pubblico = server VPN). Verifica post-F3 pulita. Fase 2: watchdog cambio rete fatto e verificato
(`watch` e `netchange` con Luke OK); MTU del profilo fatta (0.3.2);
STATS in UI fatto (build 17). Fase 2 chiusa da Luke; icona Wi-Fi e
IPv6 rinviati (solo se Luke li chiede).
Aggiorna i file di handoff a ogni gate superato.

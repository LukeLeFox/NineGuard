# NineGuard privileged companion package

Questo sottoprogetto crea un pacchetto rootful separato dall'app. È la forma
corretta durante lo sviluppo: NineGuard resta installata come IPA nel proprio
container, mentre `dpkg` gestisce daemon, motore e launchd senza creare una
seconda app con lo stesso bundle ID.

Prima build dell'engine, poi package:

```bash
make engine
make -C DaemonPackage clean package
```

Il pacchetto contiene:

- `/usr/libexec/nineguard/nineguardd`;
- `/usr/libexec/nineguard/wireguard-go`;
- `/Library/LaunchDaemons/dev.luke.nineguard.daemon.plist`;
- la licenza upstream di `wireguard-go`.

`postinst` ricarica il job e rimuove soltanto l'eventuale socket stale;
`prerm` scarica il job e rimuove lo stesso socket. Il demone può partire prima
del primo provisioning: resta vivo senza creare il socket finché l'app non
genera il segreto condiviso nel Keychain.

Versione attuale: `0.3.2` (full tunnel + DNS, watchdog cambio rete, MTU
del profilo), installata e verificata sull'iPad l'11 ottobre 2026.
Installazione e disinstallazione: vedi `GUIDA.md` nella radice.

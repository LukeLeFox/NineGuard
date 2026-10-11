# NineGuard WireGuard engine

`build-wireguard-go.sh` ricostruisce il motore ARMv7 verificato sull'iPad mini
1 con iOS 9.3.6. Scarica esclusivamente il commit upstream fissato
`52c834c4469f4f6e33c97c7294faffabfcbf114a` (`v0.0.20200320`) e rifiuta un
archivio con SHA-256 diverso da quello registrato nello script.

Prerequisiti già presenti sulla workstation di sviluppo:

- Go 1.14.15 isolato in `~/.local/opt/nineguard-go114/go` (oppure
  `NINEGUARD_GO_ROOT`);
- Theos clang, SDK iPhoneOS 9.2 e `ldid` in `~/theos` (oppure `THEOS`);
- `curl`, `tar`, `sha256sum` e `file`.

Compilazione:

```bash
./Engine/build-wireguard-go.sh
```

Output:

```text
Engine/build/wireguard-go
Engine/build/COPYING.wireguard-go
```

La build usa `GOOS=darwin`, `GOARCH=arm`, `GOARM=7`, cgo e target minimo iOS
9.0. `GOARM=7` è necessario: senza di esso Go usa il fallback TLS ARM non
fornito dal runtime Darwin e il link fallisce con
`runtime.read_tls_fallback` indefinito.

Il binario prodotto è un eseguibile Mach-O ARMv7 PIE firmato ad hoc. Il test
read-only sul dispositivo ha restituito `wireguard-go v0.0.20200320` con
`--version` e non ha creato interfacce `utun`.

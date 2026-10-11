/*
 * ngdnsprobe — gate F1 del design full tunnel + DNS.
 *
 *   ngdnsprobe supplemental <secondi>
 *
 * Pubblica in configd, come valore TEMPORANEO legato a questo processo,
 * un servizio DNS supplementare valido solo per il dominio nineguard.test
 * (nessun effetto sul resolver di default), attende e termina. configd deve
 * rimuovere la chiave da solo all'uscita del processo. Richiede root.
 */
#include <CoreFoundation/CoreFoundation.h>
#include "NGSCDynamicStore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NG_PROBE_KEY "State:/Network/Service/dev.luke.nineguard.probe/DNS"

int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "supplemental") != 0) {
        fprintf(stderr, "uso: ngdnsprobe supplemental <secondi>\n");
        return 2;
    }
    int seconds = atoi(argv[2]);
    if (seconds < 1 || seconds > 600) {
        fprintf(stderr, "secondi 1..600\n");
        return 2;
    }

    SCDynamicStoreRef store = SCDynamicStoreCreate(
        NULL, CFSTR("dev.luke.nineguard.dnsprobe"), NULL, NULL);
    if (!store) {
        printf("DNSPROBE_STORE=FAILED ERR=%d\n", SCError());
        return 1;
    }

    CFStringRef server = CFSTR("10.253.92.1");
    CFStringRef domain = CFSTR("nineguard.test");
    CFArrayRef servers = CFArrayCreate(NULL, (const void **)&server, 1,
                                       &kCFTypeArrayCallBacks);
    CFArrayRef domains = CFArrayCreate(NULL, (const void **)&domain, 1,
                                       &kCFTypeArrayCallBacks);
    const void *keys[] = { CFSTR("ServerAddresses"),
                           CFSTR("SupplementalMatchDomains") };
    const void *values[] = { servers, domains };
    CFDictionaryRef dns = CFDictionaryCreate(
        NULL, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    Boolean added = SCDynamicStoreAddTemporaryValue(
        store, CFSTR(NG_PROBE_KEY), dns);
    printf("DNSPROBE_ADD_TEMPORARY=%s", added ? "OK" : "FAILED");
    if (!added)
        printf(" ERR=%d", SCError());
    printf("\n");
    fflush(stdout);

    if (added) {
        CFPropertyListRef back = SCDynamicStoreCopyValue(
            store, CFSTR(NG_PROBE_KEY));
        printf("DNSPROBE_READBACK=%s\n", back ? "OK" : "MISSING");
        if (back) CFRelease(back);
        fflush(stdout);
        sleep((unsigned)seconds);
    }

    CFRelease(dns);
    CFRelease(domains);
    CFRelease(servers);
    CFRelease(store);
    printf("DNSPROBE_EXIT=%s\n", added ? "OK" : "FAILED");
    return added ? 0 : 1;
}

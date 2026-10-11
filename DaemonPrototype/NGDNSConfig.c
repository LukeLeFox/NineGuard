#include "NGDNSConfig.h"
#include "NGSCDynamicStore.h"

#include <arpa/inet.h>
#include <string.h>

#define NG_DNS_SERVICE "State:/Network/Service/dev.luke.nineguard"

static SCDynamicStoreRef g_store;
static int g_applied;

int ng_dns_is_applied(void) {
    return g_applied;
}

static CFArrayRef string_array(const char (*items)[NG_IPV4_TEXT],
                               unsigned count) {
    CFMutableArrayRef array = CFArrayCreateMutable(NULL, 0,
                                                   &kCFTypeArrayCallBacks);
    for (unsigned i = 0; array && i < count; i++) {
        CFStringRef text = CFStringCreateWithCString(NULL, items[i],
                                                     kCFStringEncodingASCII);
        if (text) {
            CFArrayAppendValue(array, text);
            CFRelease(text);
        }
    }
    return array;
}

static CFArrayRef domain_array(const ng_profile_plan *plan) {
    CFMutableArrayRef array = CFArrayCreateMutable(NULL, 0,
                                                   &kCFTypeArrayCallBacks);
    for (unsigned i = 0; array && i < plan->search_count; i++) {
        CFStringRef text = CFStringCreateWithCString(
            NULL, plan->search_domains[i], kCFStringEncodingASCII);
        if (text) {
            CFArrayAppendValue(array, text);
            CFRelease(text);
        }
    }
    return array;
}

void ng_dns_remove(void) {
    if (g_store) {
        SCDynamicStoreRemoveValue(g_store, CFSTR(NG_DNS_SERVICE "/IPv4"));
        SCDynamicStoreRemoveValue(g_store, CFSTR(NG_DNS_SERVICE "/DNS"));
        CFRelease(g_store);
        g_store = NULL;
    }
    g_applied = 0;
}

int ng_dns_apply(const char *ifname, const char *ipv4_address,
                 const ng_profile_plan *plan) {
    if (!ifname || !ipv4_address || !*ipv4_address || !plan)
        return NG_DNS_ERR_ARGS;
    if (plan->dns_count == 0)
        return NG_DNS_SKIPPED;
    ng_dns_remove();

    g_store = SCDynamicStoreCreate(NULL, CFSTR("dev.luke.nineguard.daemon"),
                                   NULL, NULL);
    if (!g_store)
        return NG_DNS_ERR_STORE;

    char address_item[1][NG_IPV4_TEXT];
    strncpy(address_item[0], ipv4_address, NG_IPV4_TEXT - 1);
    address_item[0][NG_IPV4_TEXT - 1] = '\0';

    CFArrayRef servers = string_array(plan->dns_servers, plan->dns_count);
    CFArrayRef domains = domain_array(plan);
    CFArrayRef addresses = string_array(
        (const char (*)[NG_IPV4_TEXT])address_item, 1);
    CFStringRef interface = CFStringCreateWithCString(NULL, ifname,
                                                      kCFStringEncodingASCII);
    CFStringRef router = CFStringCreateWithCString(NULL, ipv4_address,
                                                   kCFStringEncodingASCII);
    int one = 1;
    CFNumberRef override = CFNumberCreate(NULL, kCFNumberIntType, &one);
    int result = NG_DNS_ERR_WRITE;

    if (servers && domains && addresses && interface && router && override) {
        const void *dns_keys[2] = { CFSTR("ServerAddresses"),
                                    CFSTR("SearchDomains") };
        const void *dns_values[2] = { servers, domains };
        CFDictionaryRef dns = CFDictionaryCreate(
            NULL, dns_keys, dns_values, plan->search_count ? 2 : 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

        const void *ip_keys[5] = { CFSTR("InterfaceName"), CFSTR("Addresses"),
                                   CFSTR("DestAddresses"), CFSTR("Router"),
                                   CFSTR("OverridePrimary") };
        const void *ip_values[5] = { interface, addresses, addresses, router,
                                     override };
        CFDictionaryRef ipv4 = CFDictionaryCreate(
            NULL, ip_keys, ip_values, 5, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

        if (dns && ipv4 &&
            SCDynamicStoreAddTemporaryValue(g_store,
                                            CFSTR(NG_DNS_SERVICE "/DNS"),
                                            dns) &&
            SCDynamicStoreAddTemporaryValue(g_store,
                                            CFSTR(NG_DNS_SERVICE "/IPv4"),
                                            ipv4))
            result = NG_DNS_OK;
        if (dns) CFRelease(dns);
        if (ipv4) CFRelease(ipv4);
    }
    if (servers) CFRelease(servers);
    if (domains) CFRelease(domains);
    if (addresses) CFRelease(addresses);
    if (interface) CFRelease(interface);
    if (router) CFRelease(router);
    if (override) CFRelease(override);

    if (result == NG_DNS_OK)
        g_applied = 1;
    else
        ng_dns_remove();
    return result;
}

static int copy_ascii(CFDictionaryRef dict, CFStringRef key, char *out,
                      size_t capacity) {
    CFTypeRef value = CFDictionaryGetValue(dict, key);
    if (value && CFGetTypeID(value) == CFArrayGetTypeID() &&
        CFArrayGetCount(value) > 0)
        value = CFArrayGetValueAtIndex(value, 0);
    return value && CFGetTypeID(value) == CFStringGetTypeID() &&
           CFStringGetCString(value, out, (CFIndex)capacity,
                              kCFStringEncodingASCII);
}

int ng_dns_physical_router(struct in_addr *router) {
    if (!router)
        return -1;
    memset(router, 0, sizeof(*router));
    SCDynamicStoreRef store = SCDynamicStoreCreate(
        NULL, CFSTR("dev.luke.nineguard.watch"), NULL, NULL);
    if (!store)
        return -1;
    CFArrayRef keys = SCDynamicStoreCopyKeyList(
        store, CFSTR("State:/Network/Service/[^/]+/IPv4"));
    int best = 0;
    for (CFIndex i = 0; keys && i < CFArrayGetCount(keys); i++) {
        CFStringRef key = CFArrayGetValueAtIndex(keys, i);
        if (CFEqual(key, CFSTR(NG_DNS_SERVICE "/IPv4")))
            continue;
        CFPropertyListRef value = SCDynamicStoreCopyValue(store, key);
        if (!value)
            continue;
        char name[16] = {0}, text[NG_IPV4_TEXT] = {0};
        struct in_addr address;
        if (CFGetTypeID(value) == CFDictionaryGetTypeID() &&
            copy_ascii(value, CFSTR("InterfaceName"), name, sizeof(name)) &&
            copy_ascii(value, CFSTR("Router"), text, sizeof(text)) &&
            strncmp(name, "utun", 4) && strncmp(name, "ppp", 3) &&
            strncmp(name, "ipsec", 5) &&
            inet_pton(AF_INET, text, &address) == 1 && address.s_addr) {
            int rank = strncmp(name, "en", 2) == 0 ? 2 : 1;
            if (rank > best) {
                best = rank;
                *router = address;
            }
        }
        CFRelease(value);
    }
    if (keys)
        CFRelease(keys);
    CFRelease(store);
    return best ? 0 : -1;
}

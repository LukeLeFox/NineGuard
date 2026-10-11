#ifndef NGSCDYNAMICSTORE_H
#define NGSCDYNAMICSTORE_H

/*
 * SCDynamicStore e' marcato "unavailable on iOS" negli header dell'SDK 9.x,
 * ma i simboli sono esportati da SystemConfiguration.framework (stub .tbd)
 * e usati da configd/scutil sul device. Dichiarazioni locali, ABI identico
 * a SCDynamicStore.h di macOS.
 */
#include <CoreFoundation/CoreFoundation.h>

typedef const struct __SCDynamicStore *SCDynamicStoreRef;

SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef allocator,
                                       CFStringRef name,
                                       void *callout, void *context);
CFPropertyListRef SCDynamicStoreCopyValue(SCDynamicStoreRef store,
                                          CFStringRef key);
CFArrayRef SCDynamicStoreCopyKeyList(SCDynamicStoreRef store,
                                     CFStringRef pattern);
Boolean SCDynamicStoreAddTemporaryValue(SCDynamicStoreRef store,
                                        CFStringRef key,
                                        CFPropertyListRef value);
Boolean SCDynamicStoreSetValue(SCDynamicStoreRef store, CFStringRef key,
                               CFPropertyListRef value);
Boolean SCDynamicStoreRemoveValue(SCDynamicStoreRef store, CFStringRef key);
int SCError(void);

#endif

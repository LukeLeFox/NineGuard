#ifndef NGDAEMONPROFILE_H
#define NGDAEMONPROFILE_H

#include "NGProfileUAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NG_LOAD_OK = 0,
    NG_LOAD_ERR_UUID = -20,
    NG_LOAD_ERR_NOT_FOUND = -21,
    NG_LOAD_ERR_KEYCHAIN = -22,
    NG_LOAD_ERR_PARSE = -23,
    NG_LOAD_ERR_MEMORY = -24
};

/* Formato UUID canonico 8-4-4-4-12 (hex). Nessun accesso al Keychain. */
int NGDaemonProfileIDValid(const char *profile_id);

/*
 * Legge la configurazione dal gruppo Keychain condiviso, la rivalida con
 * NGWGConfigParser nel processo root e produce il corpo UAPI.
 * *uapi_out e' allocato con NG_UAPI_CAPACITY byte: il chiamante deve
 * chiamare NGDaemonProfileFreeUAPI. Restituisce NG_LOAD_* oppure un
 * codice NG_PROFILE_ERR_*.
 */
int NGDaemonProfileLoad(const char *profile_id, char **uapi_out,
                        ng_profile_plan *plan);
void NGDaemonProfileFreeUAPI(char *uapi);

#ifdef __cplusplus
}
#endif

#endif

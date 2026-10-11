#include "NGNetWatch.h"

#include <stddef.h>

int ng_watch_decide(const ng_watch_input *input) {
    if (!input)
        return NG_WATCH_KEEP;
    if (input->onlink)
        return input->recorded ? NG_WATCH_REMOVE : NG_WATCH_KEEP;
    /* Senza rete fisica il full tunnel resta chiuso (nessun leak): si
     * aspetta la nuova rete senza toccare il bypass esistente. */
    if (!input->have_router || input->router == 0)
        return NG_WATCH_NO_NETWORK;
    if (!input->recorded)
        return NG_WATCH_ADD;
    if (input->kernel_matches && input->recorded_gateway == input->router)
        return NG_WATCH_KEEP;
    return NG_WATCH_REPLACE;
}

int ng_watch_grace_expired(long *failing_since, int failing, long now,
                           long grace) {
    if (!failing_since)
        return 0;
    if (!failing) {
        *failing_since = 0;
        return 0;
    }
    if (*failing_since == 0)
        *failing_since = now;
    return now - *failing_since >= grace;
}

/*
 * OpenBrowser: WebCore's network thread (AmigaNetworkSetup.c); openweb.fetch
 * opens bsdsocket.library and opentls.library itself.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OB_NETWORK_H
#define OB_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

/* On the main task, before WebCore loads anything from the network. */
int ob_network_open(void);
void ob_network_close(void);

/* On the network thread, at its start. */
void ob_network_thread_started(void);

#ifdef __cplusplus
}
#endif

#endif

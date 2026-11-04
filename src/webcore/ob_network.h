/*
 * GFOpenBrowser: WebCore's network thread (AmigaNetworkSetup.c); openweb.fetch
 * opens bsdsocket.library and gfopentls.library itself.
 * MIT, Copyright (c) 2026 AmiCradle Team.
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

/* A Shell variable (local Set, else ENV:) as a switch: dflt when it is not
 * set, 0 when it starts with '0', else 1. For OB_EARLYFETCH and OB_NETLOG. */
int ob_network_switch(const char *name, int dflt);

#ifdef __cplusplus
}
#endif

#endif

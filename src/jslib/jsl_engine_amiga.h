/*
 * javascript.library: the engine's public port on AmigaOS, shared by the
 * library (javascript_lib.c) and the engine (jsl_engine.c, jsl_os_exec.c).
 *
 * The library finds the port under Forbid, counts itself in ep_Users while
 * it calls ep_Table->NewTask, and counts itself out; the engine only quits
 * (idle, or on Ctrl-C) under Forbid with no users and no live script tasks,
 * so a table the library holds is never unloaded under it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef JSL_ENGINE_AMIGA_H
#define JSL_ENGINE_AMIGA_H

#include <exec/ports.h>
#include "jsl_engine.h"

struct JSEnginePort {
    struct MsgPort ep_Port;             /* public, JAVASCRIPT_ENGINE_PORT */
    ULONG ep_Version;                   /* JSL_TABLE_VERSION */
    CONST struct JSEngineTable *ep_Table;
    volatile LONG ep_Users;             /* library calls in progress */
};

/* Messages to the engine's port */
#define JSE_CALL 1                      /* run em_Call on the engine's process */

struct jsl_call;

struct JSEngineMsg {
    struct Message em_Message;
    ULONG em_Command;
    struct jsl_call *em_Call;
};

#endif

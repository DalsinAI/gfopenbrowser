/*
 * JavaScriptEngine: the program behind javascript.library. The library
 * starts it the first time a program needs it (LIBS:JavaScript/
 * JavaScriptEngine); it opens the public port JAVASCRIPT_ENGINE_PORT, a
 * struct JSEnginePort carrying the engine's table, and stays in memory so
 * the next program finds it ready.
 *
 * Its own process does two things: it starts and ends script tasks when
 * asked (JSE_CALL: every pthread is started and joined from here), and it
 * quits once no
 * script task has been alive for ENV:JavaScriptIdleQuit seconds (default
 * 60; 0 stays resident), or on Ctrl-C when none is alive.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "jsl_os.h"
#include "jsl_engine_amiga.h"
#include "jsimage.h"
#if OB_ICU_DATA_FILE
#include "ob_icudata.h"
#endif

static const char version[] __attribute__((used)) = "$VER: JavaScriptEngine 1.0 (22.10.2026) AmiCradle Team";

static struct JSEnginePort *enginePort;

/* From any task: the engine's process runs the call, the caller waits */
void jsl_os_on_engine(struct jsl_call *call)
{
    struct JSEngineMsg m;
    struct MsgPort *reply;
    if (FindTask(NULL) == enginePort->ep_Port.mp_SigTask) {
        call->fn(call->arg);
        return;
    }
    if (!(reply = CreateMsgPort())) {
        call->fn = NULL;
        return;
    }
    memset(&m, 0, sizeof m);
    m.em_Message.mn_Node.ln_Type = NT_MESSAGE;
    m.em_Message.mn_ReplyPort = reply;
    m.em_Message.mn_Length = sizeof m;
    m.em_Command = JSE_CALL;
    m.em_Call = call;
    PutMsg(&enginePort->ep_Port, &m.em_Message);
    WaitPort(reply);
    GetMsg(reply);
    DeleteMsgPort(reply);
}

static LONG idleSeconds(void)
{
    char value[16];
    if (GetVar((STRPTR)"JavaScriptIdleQuit", (STRPTR)value, sizeof value, GVF_GLOBAL_ONLY) > 0)
        return atol(value);
    return 60;
}

int main(void)
{
    struct MsgPort *timerPort;
    struct timerequest *timer;
    LONG idle = idleSeconds(), quiet = 0;
    int quit = 0;

    /* Loaded by a jsimage client (the browser's engine) for its code: run
     * the client, in this process, and nothing else */
    if (jsimage_client)
        return (int)jsimage_run_client();

    Forbid();
    if (FindPort((CONST_STRPTR)JAVASCRIPT_ENGINE_PORT)) {
        Permit();
        return 0;   /* already running */
    }
    Permit();

#if OB_ICU_DATA_FILE
    /* Built with the browser (src/webcore/CMakeLists.txt): ICU's data is
     * the file in LOCALE:Unicode/ or LIBS:JavaScript/Unicode/, read before
     * the first script task starts JavaScriptCore */
    if (!ob_icu_data_load())
        return 20;
#endif

    enginePort = AllocVec(sizeof *enginePort, MEMF_PUBLIC | MEMF_CLEAR);
    timerPort = CreateMsgPort();
    timer = timerPort ? (struct timerequest *)CreateIORequest(timerPort, sizeof *timer) : NULL;
    if (!enginePort || !timer || OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, &timer->tr_node, 0)) {
        if (timer)
            DeleteIORequest(&timer->tr_node);
        if (timerPort)
            DeleteMsgPort(timerPort);
        FreeVec(enginePort);
        return 20;
    }

    enginePort->ep_Port.mp_Node.ln_Name = (char *)JAVASCRIPT_ENGINE_PORT;
    enginePort->ep_Port.mp_Node.ln_Type = NT_MSGPORT;
    enginePort->ep_Port.mp_Flags = PA_SIGNAL;
    enginePort->ep_Port.mp_SigBit = AllocSignal(-1);
    enginePort->ep_Port.mp_SigTask = FindTask(NULL);
    {
        struct List *l = &enginePort->ep_Port.mp_MsgList;
        l->lh_Head = (struct Node *)&l->lh_Tail;
        l->lh_Tail = NULL;
        l->lh_TailPred = (struct Node *)&l->lh_Head;
    }
    enginePort->ep_Version = JSL_TABLE_VERSION;
    enginePort->ep_Table = jsl_engine_table();
    /* Check again as the port goes public: another engine may have started
     * while this one read ICU's data */
    Forbid();
    if (FindPort((CONST_STRPTR)JAVASCRIPT_ENGINE_PORT)) {
        Permit();
        FreeSignal(enginePort->ep_Port.mp_SigBit);
        CloseDevice(&timer->tr_node);
        DeleteIORequest(&timer->tr_node);
        DeleteMsgPort(timerPort);
        FreeVec(enginePort);
        return 0;   /* already running */
    }
    AddPort(&enginePort->ep_Port);
    Permit();

    timer->tr_node.io_Command = TR_ADDREQUEST;
    timer->tr_time.tv_secs = 1;
    timer->tr_time.tv_micro = 0;
    SendIO(&timer->tr_node);

    while (!quit) {
        ULONG got = Wait((1UL << enginePort->ep_Port.mp_SigBit) | (1UL << timerPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        struct JSEngineMsg *m;
        while ((m = (struct JSEngineMsg *)GetMsg(&enginePort->ep_Port))) {
            if (m->em_Command == JSE_CALL)
                m->em_Call->fn(m->em_Call->arg);
            ReplyMsg(&m->em_Message);
        }
        if (got & (1UL << timerPort->mp_SigBit)) {
            WaitIO(&timer->tr_node);
            quiet = jsl_engine_live_tasks() ? 0 : quiet + 1;
            timer->tr_time.tv_secs = 1;
            timer->tr_time.tv_micro = 0;
            SendIO(&timer->tr_node);
        }
        if ((got & SIGBREAKF_CTRL_C) || (idle > 0 && quiet >= idle)) {
            /* Only with no script task alive and no library call under way */
            Forbid();
            if (!jsl_engine_live_tasks() && !enginePort->ep_Users) {
                RemPort(&enginePort->ep_Port);
                quit = 1;
            }
            Permit();
        }
    }

    AbortIO(&timer->tr_node);
    WaitIO(&timer->tr_node);
    CloseDevice(&timer->tr_node);
    DeleteIORequest(&timer->tr_node);
    DeleteMsgPort(timerPort);
    {
        struct Message *m;
        while ((m = GetMsg(&enginePort->ep_Port)))
            ReplyMsg(m);
    }
    FreeSignal(enginePort->ep_Port.mp_SigBit);
    FreeVec(enginePort);
    return 0;
}

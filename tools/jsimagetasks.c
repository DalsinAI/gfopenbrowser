/* GFOpenBrowser (DalsinAI/gfopenbrowser). Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE. */
/* jsimagetasks: run beside a jsimage client (the browser's engine linked
 * against JavaScriptEngine, src/jslib/jsimage.h) that has stopped, to see
 * where every task is. For each running jsimage client it prints the
 * client's and the image's hunk, then every task in the system with its
 * state and signals and the stack words that point into either hunk, as
 * c+OFFSET (the client) or i+OFFSET (the image). The offsets are the
 * addresses `nm -n` gives for the unstripped client and JavaScriptEngine.
 *
 *   jsimagetasks [WORDS]     WORDS: stack words per task, 48 by default */
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/jslib/jsimage.h"

extern struct ExecBase *SysBase;

struct Range {
    ULONG start, end;
};
static struct Range client, image;
static char text[65536];
static int length;

static void put(const char *s)
{
    while (*s && length < (int)sizeof text - 1)
        text[length++] = *s++;
}

static void hex(ULONG value)
{
    char digits[12];
    int i = 0, n;
    do {
        n = value & 15;
        digits[i++] = n < 10 ? '0' + n : 'a' + n - 10;
        value >>= 4;
    } while (value);
    while (i && length < (int)sizeof text - 1)
        text[length++] = digits[--i];
}

/* A hunk's first byte and its end, from its segment list entry */
static struct Range hunk(BPTR segment)
{
    ULONG *s = BADDR(segment);
    struct Range r;
    r.start = (ULONG)(s + 1);
    r.end = (ULONG)s - 4 + s[-1];
    return r;
}

/* A CLI process whose command is a jsimage client: its second hunk is the
 * fixup table, and the table says where the image is */
static int findClient(struct Task *task)
{
    struct Process *process = (struct Process *)task;
    struct CommandLineInterface *cli;
    struct JSImageTable *t;
    BPTR code, next;
    ULONG at;
    if (task->tc_Node.ln_Type != NT_PROCESS || !process->pr_CLI)
        return 0;
    cli = BADDR(process->pr_CLI);
    if (!(code = cli->cli_Module) || !(next = *(BPTR *)BADDR(code)))
        return 0;
    t = (struct JSImageTable *)((UBYTE *)BADDR(next) + 4);
    if (t->jt_Magic != JSIMAGE_MAGIC || t->jt_Version != JSIMAGE_VERSION)
        return 0;
    client = hunk(code);
    /* jsimage_start: add = image - client - jt_Base */
    at = t->jt_Applied + client.start + t->jt_Base;
    image.start = at;
    image.end = at - 8 + ((ULONG *)at)[-2];
    put("JSIMAGE_CLIENT ");
    put(task->tc_Node.ln_Name ? task->tc_Node.ln_Name : "?");
    put(" task=");
    hex((ULONG)task);
    put(" client=");
    hex(client.start);
    put("-");
    hex(client.end);
    put(" image=");
    hex(image.start);
    put("-");
    hex(image.end);
    put(" fixups=");
    hex(t->jt_Count);
    put("\n");
    return 1;
}

static void dumpTask(struct Task *task, int most)
{
    ULONG *p = (ULONG *)task->tc_SPReg, *top = (ULONG *)task->tc_SPUpper;
    int n = 0;
    if (p < (ULONG *)task->tc_SPLower || p >= top)
        top = p + 65536 / sizeof(ULONG);
    put("JSIMAGE_TASK ");
    put(task->tc_Node.ln_Name ? task->tc_Node.ln_Name : "?");
    put(" task=");
    hex((ULONG)task);
    put(" state=");
    hex(task->tc_State);
    put(" sigwait=");
    hex(task->tc_SigWait);
    put(" sigrecvd=");
    hex(task->tc_SigRecvd);
    put(" pri=");
    hex((UBYTE)task->tc_Node.ln_Pri);
    put(" sp=");
    hex((ULONG)task->tc_SPReg);
    put(":");
    for (; p && p < top && n < most; p++) {
        if (*p >= client.start && *p < client.end) {
            put(" c+");
            hex(*p - client.start);
            n++;
        } else if (*p >= image.start && *p < image.end) {
            put(" i+");
            hex(*p - image.start);
            n++;
        }
    }
    put("\n");
}

int main(int argc, char **argv)
{
    struct Node *node;
    struct List *lists[2];
    int most = argc > 1 ? atoi(argv[1]) : 48, found = 0, l;
    if (most <= 0)
        most = 48;
    lists[0] = &SysBase->TaskWait;
    lists[1] = &SysBase->TaskReady;
    /* Everything inside Forbid(): no task runs while its stack is read */
    Forbid();
    for (l = 0; l < 2; l++)
        for (node = lists[l]->lh_Head; node->ln_Succ; node = node->ln_Succ)
            if (!found)
                found = findClient((struct Task *)node);
    if (found)
        for (l = 0; l < 2; l++)
            for (node = lists[l]->lh_Head; node->ln_Succ; node = node->ln_Succ)
                if ((struct Task *)node != FindTask(NULL))
                    dumpTask((struct Task *)node, most);
    Permit();
    if (!found)
        put("JSIMAGE_CLIENT none running\n");
    text[length] = 0;
    fputs(text, stdout);
    return found ? 0 : 5;
}

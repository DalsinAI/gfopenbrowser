/*
 * obstamp: one line for tools/bench's logs: a label, the time and free
 * memory, so an AmigaDOS script can time a program and see what memory it
 * kept or gave back.
 *
 *   obstamp LABEL/A
 *   OBSTAMP <label> day=<n> ms=<n> any=<bytes> fast=<bytes> chip=<bytes> largest=<bytes>
 *
 * day and ms are DateStamp()'s days since 1978 and milliseconds since
 * midnight (in 20 ms steps: 1/50 s ticks). Memory is AvailMem(): all free
 * memory, Fast and Chip, and the largest free block.
 *
 * GFOpenBrowser. MIT, Copyright (c) 2026 The AmiCradle Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

static const char version[] __attribute__((used)) = "$VER: obstamp 1.0 (19.10.2026)";

int main(void)
{
    LONG args[1] = { 0 };
    struct RDArgs *rd = ReadArgs((CONST_STRPTR)"LABEL/A", args, NULL);
    struct DateStamp now;
    LONG values[7];

    if (!rd) {
        PrintFault(IoErr(), (CONST_STRPTR)"obstamp");
        return RETURN_FAIL;
    }
    DateStamp(&now);
    values[0] = args[0];
    values[1] = now.ds_Days;
    values[2] = now.ds_Minute * 60000 + now.ds_Tick * (1000 / TICKS_PER_SECOND);
    values[3] = (LONG)AvailMem(MEMF_ANY);
    values[4] = (LONG)AvailMem(MEMF_FAST);
    values[5] = (LONG)AvailMem(MEMF_CHIP);
    values[6] = (LONG)AvailMem(MEMF_ANY | MEMF_LARGEST);
    VPrintf((CONST_STRPTR)"OBSTAMP %s day=%ld ms=%ld any=%lu fast=%lu chip=%lu largest=%lu\n", values);
    FreeArgs(rd);
    return RETURN_OK;
}

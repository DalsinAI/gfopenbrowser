/* JSONTest: json.library on the Amiga.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 *   JSONTest                 the self-test: every call, "ok" or "FAIL" per line
 *   JSONTest FILE            parse FILE: nodes, block size, the first error
 *   JSONTest FILE BENCH      parse FILE 20 times, read it through the
 *                            streaming reader 20 times, write it back 20 times,
 *                            with the time of each (DateStamp ticks, 1/50 s)
 *
 * Built by build-library.sh with the 68040 soft-float link. The answers it
 * checks are the ones tests/jsonlib/run.sh checks the core for on the host
 * and under qemu-m68k; here they come through the library's registers. */
#ifdef JSON_HOSTED
#include "json_hosted.h"   /* tests/jsonlib: the core, on the host or under qemu-m68k */
#else
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/json.h>
#include <string.h>
#endif

/* a string for Printf: a ULONG on the Amiga */
#ifndef SARG
#define SARG(s) ((ULONG)(s))
#endif

struct Library *JSONBase;
static LONG fails, passes;

static void check(const char *what, BOOL ok)
{
    if (ok) passes++;
    else fails++;
    Printf("%s %s\n", SARG(ok ? "ok  " : "FAIL"), SARG(what));
}

static BOOL same(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

/* Doubles compared bit for bit (no floating point needed to check them:
 * the constants are the compiler's, the values the library's) */
static BOOL same_double(const DOUBLE *a, DOUBLE b)
{
    return memcmp(a, &b, sizeof b) == 0;
}

static const char sample[] =
    "{\"name\":\"Amiga\\u00e9\",\"cpu\":68040,\"mhz\":25.5,\"chips\":[\"Alice\",\"Lisa\",\"Paula\"],"
    "\"a/b\":{\"~x\":true},\"big\":12345678901,\"neg\":-0,\"none\":null,\"name\":\"last\"}";

static void self_test(void)
{
    struct JSONError err;
    struct JSONDoc *doc = JSONParse((CONST_STRPTR)sample, JSON_STRLEN, 0, &err);
    ULONG n, len;
    LONG l;
    DOUBLE d;
    char buf[64];
    check("parse", doc != NULL && err.je_Code == JSONERR_OK);
    if (!doc) return;
    check("check block", JSONCheckDoc(doc, doc->jd_Size) == JSONERR_OK);
    check("root is an object of 9", JSONType(doc, 0) == JSON_OBJECT && JSONCount(doc, 0) == 9);
    n = JSONFind(doc, 0, (CONST_STRPTR)"name", JSON_STRLEN);
    check("find keeps the last equal key", same((const char *)JSONGetString(doc, n, &len), "last") && len == 4);
    n = JSONIndex(doc, 0, 0);
    check("index 0 is the first key", same((const char *)JSONGetString(doc, n, NULL), "name"));
    check("UTF-8 to Latin-1", JSONGetLatin1(doc, n + 1, buf, sizeof buf) == 6 && (UBYTE)buf[5] == 0xE9);
    n = JSONFind(doc, 0, (CONST_STRPTR)"cpu", JSON_STRLEN);
    check("int", JSONType(doc, n) == JSON_INT && JSONGetLong(doc, n, &l) == 0 && l == 68040);
    n = JSONFind(doc, 0, (CONST_STRPTR)"mhz", JSON_STRLEN);
    check("double", JSONGetDouble(doc, n, &d) == 0 && same_double(&d, 25.5));
    check("not a LONG", JSONGetLong(doc, n, &l) == JSONERR_RANGE);
    n = JSONPointer(doc, 0, (CONST_STRPTR)"/chips/2");
    check("pointer /chips/2", same((const char *)JSONGetString(doc, n, NULL), "Paula"));
    n = JSONPointer(doc, 0, (CONST_STRPTR)"/a~1b/~0x");
    check("pointer escapes", JSONType(doc, n) == JSON_TRUE);
    check("pointer misses", JSONPointer(doc, 0, (CONST_STRPTR)"/chips/3") == JSON_NONE);
    n = JSONFind(doc, 0, (CONST_STRPTR)"big", JSON_STRLEN);
    check("big number", JSONType(doc, n) == JSON_NUMBER && JSONGetDouble(doc, n, &d) == 0 && same_double(&d, 12345678901.0));
    {
        /* the block is position independent: walk a copy */
        struct JSONDoc *copy = AllocVec(doc->jd_Size, MEMF_ANY);
        if (copy) {
            CopyMem(doc, copy, doc->jd_Size);
            n = JSONPointer(copy, 0, (CONST_STRPTR)"/chips/0");
            check("a copied block", JSONCheckDoc(copy, doc->jd_Size) == 0 &&
                                    same((const char *)JSONGetString(copy, n, NULL), "Alice"));
            /* without the library: the macros */
            {
                struct JSONNode *nv = JSON_NODES(copy);
                check("the macros", JSON_NTYPE(&nv[0]) == JSON_OBJECT && JSON_NSKIP(nv, 0) == copy->jd_Nodes &&
                                    same((const char *)JSON_NTEXT(copy, &nv[1]), "name"));
            }
            FreeVec(copy);
        }
    }
    {
        STRPTR s = JSONStringify(doc, JSONFind(doc, 0, (CONST_STRPTR)"chips", JSON_STRLEN), 0, &len);
        check("stringify", same((const char *)s, "[\"Alice\",\"Lisa\",\"Paula\"]") && len == 24);
        JSONFreeText(s);
    }
    JSONFreeDoc(doc);

    doc = JSONParse((CONST_STRPTR)"[1,", 3, 0, &err);
    check("error: end of text", !doc && err.je_Code == JSONERR_EOF && err.je_Offset == 3 && err.je_Column == 4);
    doc = JSONParse((CONST_STRPTR)"\n [1 2]", 7, 0, &err);
    check("error: line and column", !doc && err.je_Code == JSONERR_SYNTAX && err.je_Line == 2 && err.je_Column == 5);
    check("error string", same((const char *)JSONErrorString(JSONERR_SYNTAX), "unexpected character"));
    doc = JSONParse((CONST_STRPTR)"[1,/*x*/2,]", 11, JSONF_COMMENTS | JSONF_TRAILING, &err);
    check("comments and trailing commas", doc && JSONCount(doc, 0) == 2);
    JSONFreeDoc(doc);
    {
        static const char lat[] = "[\"Gr\xFC\xDF" "e\"]";
        doc = JSONParse((CONST_STRPTR)lat, strlen(lat), JSONF_LATIN1, &err);
        check("Latin-1 text", doc && same((const char *)JSONGetString(doc, 1, &len), "Gr\xC3\xBC\xC3\x9F" "e"));
        JSONFreeDoc(doc);
    }
    {
        ULONG size = JSONDocSize((CONST_STRPTR)"{\"a\":[1,2]}", 11, 0, &err);
        ULONG mem[24];
        doc = JSONParseInto((CONST_STRPTR)"{\"a\":[1,2]}", 11, 0, mem, sizeof mem, &err);
        check("parse into my memory", size == 32 + 5 * 8 + 4 && doc == (struct JSONDoc *)mem &&
                                      JSONPointer(doc, 0, (CONST_STRPTR)"/a/1") == 4);
        JSONFreeDoc(doc);   /* not the library's: does nothing */
    }

    check("text to LONG", JSONTextToLong((CONST_STRPTR)"1.5e1", JSON_STRLEN, &l) == 0 && l == 15);
    check("text to double", JSONTextToDouble((CONST_STRPTR)"0.1", JSON_STRLEN, &d) == 0 && same_double(&d, 0.1));
    d = 0.1 + 0.2;
    check("double to text", JSONDoubleToText(&d, (STRPTR)buf) == 19 && same(buf, "0.30000000000000004"));
    d = 1e21;
    JSONDoubleToText(&d, (STRPTR)buf);
    check("double to text, exponent", same(buf, "1e+21"));

    {
        struct JSONWriter *w = JSONNewWriter(0);
        CONST_STRPTR t;
        d = -2.5e-7;
        JSONWriteBegin(w, JSON_OBJECT);
        JSONWriteKey(w, (CONST_STRPTR)"s", JSON_STRLEN);
        JSONWriteString(w, (CONST_STRPTR)"tab\there \"q\"", JSON_STRLEN);
        JSONWriteKey(w, (CONST_STRPTR)"n", JSON_STRLEN);
        JSONWriteBegin(w, JSON_ARRAY);
        JSONWriteLong(w, -42);
        JSONWriteDouble(w, &d);
        JSONWriteBool(w, TRUE);
        JSONWriteNull(w);
        JSONWriteNumberText(w, (CONST_STRPTR)"1e400", JSON_STRLEN);
        JSONWriteEnd(w);
        JSONWriteEnd(w);
        t = JSONWriterText(w, &len);
        check("writer", same((const char *)t, "{\"s\":\"tab\\there \\\"q\\\"\",\"n\":[-42,-2.5e-7,true,null,1e400]}"));
        JSONFreeWriter(w);
        w = JSONNewWriter(JSONW_LATIN1 | JSONW_PRETTY);
        JSONWriteBegin(w, JSON_ARRAY);
        JSONWriteString(w, (CONST_STRPTR)"Gr\xFC\xDF" "e", JSON_STRLEN);
        JSONWriteEnd(w);
        check("writer: Latin-1 in, UTF-8 out, pretty", same((const char *)JSONWriterText(w, NULL),
                                                             "[\n  \"Gr\xC3\xBC\xC3\x9F" "e\"\n]"));
        JSONResetWriter(w);
        JSONWriteKey(w, (CONST_STRPTR)"x", 1);
        check("writer: a key outside an object", JSONWriterError(w) == JSONERR_STATE && !JSONWriterText(w, NULL));
        JSONFreeWriter(w);
    }
    {
        struct JSONReader *r = JSONNewReader(0);
        static const char in[] = "{\"k\": [12.5, \"v\\u00e9\", false]}";
        LONG ev, events[16], ne = 0;
        ULONG i;
        BOOL texts = TRUE;
        for (i = 0; i < sizeof in - 1; i++) {
            JSONReaderFeed(r, (APTR)(in + i), 1);   /* one byte at a time */
            while ((ev = JSONReaderNext(r)) > 0 && ne < 16) {
                const char *tx = (const char *)JSONReaderText(r, &len);
                if (ev == JSONEV_KEY) texts &= same(tx, "k");
                if (ev == JSONEV_NUMBER) texts &= same(tx, "12.5");
                if (ev == JSONEV_STRING) texts &= same(tx, "v\xC3\xA9");
                events[ne++] = ev;
            }
        }
        JSONReaderFeed(r, NULL, 0);
        events[ne++] = JSONReaderNext(r);
        check("reader, a byte at a time",
              ne == 9 && events[8] == JSONEV_DONE && events[0] == JSONEV_OBJECT && events[1] == JSONEV_KEY && events[2] == JSONEV_ARRAY &&
              events[3] == JSONEV_NUMBER && events[4] == JSONEV_STRING && events[5] == JSONEV_FALSE &&
              events[6] == JSONEV_ARRAY_END && events[7] == JSONEV_OBJECT_END && texts);
        check("reader, the end", JSONReaderNext(r) == JSONEV_DONE && JSONReaderDepth(r) == 0);
        JSONFreeReader(r);
        r = JSONNewReader(0);
        JSONReaderFeed(r, (APTR)"[1,]", 4);
        while ((ev = JSONReaderNext(r)) > 0)
            ;
        check("reader, an error", ev == JSONEV_ERROR && JSONReaderError(r, &err) == JSONERR_SYNTAX &&
                                  err.je_Offset == 3);
        JSONFreeReader(r);
    }
}

static UBYTE *load(CONST_STRPTR name, ULONG *len)
{
    BPTR fh = Open(name, MODE_OLDFILE);
    UBYTE *buf = NULL;
    LONG size;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size >= 0 && (buf = AllocVec(size + 1, MEMF_ANY)) != NULL) {
        if (Read(fh, buf, size) != size) {
            FreeVec(buf);
            buf = NULL;
        }
        *len = size;
    }
    Close(fh);
    return buf;
}

static LONG ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ds.ds_Minute * 60 * 50) + ds.ds_Tick;
}

static void file_test(CONST_STRPTR name, BOOL bench)
{
    ULONG len, i, out = 0;
    UBYTE *text = load(name, &len);
    struct JSONError err;
    struct JSONDoc *doc;
    LONG t0;
    if (!text) {
        Printf("cannot read %s\n", SARG(name));
        fails++;
        return;
    }
    doc = JSONParse(text, len, 0, &err);
    if (!doc) {
        Printf("%s: %s at line %lu column %lu (byte %lu)\n", SARG(name), SARG(JSONErrorString(err.je_Code)),
               err.je_Line, err.je_Column, err.je_Offset);
        fails++;
        FreeVec(text);
        return;
    }
    Printf("%s: %lu bytes, %lu nodes, block %lu bytes\n", SARG(name), len, doc->jd_Nodes, doc->jd_Size);
    passes++;
    if (bench) {
        t0 = ticks();
        for (i = 0; i < 20; i++) {
            struct JSONDoc *d = JSONParse(text, len, 0, &err);
            JSONFreeDoc(d);
        }
        Printf("parse x20: %ld ticks\n", ticks() - t0);
        t0 = ticks();
        for (i = 0; i < 20; i++) {
            struct JSONReader *r = JSONNewReader(0);
            JSONReaderFeed(r, text, len);
            while (JSONReaderNext(r) > 0)
                ;
            JSONReaderFeed(r, NULL, 0);
            JSONReaderNext(r);
            JSONFreeReader(r);
        }
        Printf("reader x20: %ld ticks\n", ticks() - t0);
        t0 = ticks();
        for (i = 0; i < 20; i++) {
            STRPTR s = JSONStringify(doc, 0, 0, &out);
            JSONFreeText(s);
        }
        Printf("stringify x20: %ld ticks (%lu bytes)\n", ticks() - t0, out);
    }
    JSONFreeDoc(doc);
    FreeVec(text);
}

int main(int argc, char **argv)
{
    if (!(JSONBase = OpenLibrary((CONST_STRPTR)JSONNAME, JSONVERSION))) {
        PutStr((CONST_STRPTR)"JSONTest: cannot open json.library 1\n");
        return RETURN_FAIL;
    }
    Printf("%s", SARG(JSONBase->lib_IdString));
    if (argc > 1) file_test((CONST_STRPTR)argv[1], argc > 2 && strcmp(argv[2], "BENCH") == 0);
    else self_test();
    Printf("%ld passed, %ld failed\n", passes, fails);
    CloseLibrary(JSONBase);
    return fails ? RETURN_WARN : RETURN_OK;
}

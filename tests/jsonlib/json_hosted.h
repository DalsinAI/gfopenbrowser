/* Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root). */
/* JSONTest on the host and under qemu-m68k: the Amiga types, the few Exec
 * and DOS calls it makes, and every json.library call mapped straight onto
 * the core (src/jsonlib/json_core.h). tests/jsonlib/run.sh builds it with
 * -DJSON_HOSTED, so the self-test's answers are checked before the lab. */
#ifndef JSON_HOSTED_H
#define JSON_HOSTED_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "json_core.h"

#define EXEC_TYPES_H
typedef unsigned int ULONG;
typedef int LONG;
typedef unsigned char UBYTE;
typedef short BOOL;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef void *APTR;
typedef double DOUBLE;
typedef long BPTR;
#define TRUE 1
#define FALSE 0
#define CONST const
#define MEMF_ANY 0
#define RETURN_OK 0
#define RETURN_WARN 5
#define RETURN_FAIL 20
#define MODE_OLDFILE 0
#define OFFSET_END 1
#define OFFSET_BEGINNING (-1)
#include <libraries/json.h>

struct Library {
    const char *lib_IdString;
};
struct DateStamp {
    long ds_Minute, ds_Tick;
};

static void *h_alloc(struct oj_env *e, oj_u32 n) { (void)e; return malloc(n ? n : 1); }
static void h_free(struct oj_env *e, void *p) { (void)e; free(p); }
static struct oj_env hosted_env = { h_alloc, h_free };
static struct Library hosted_lib = { "json.library 1.0 (hosted core)\n" };

#define AllocVec(n, f) malloc(n)
#define FreeVec(p) free(p)
#define CopyMem(s, d, n) memcpy(d, s, n)
#define OpenLibrary(name, v) ((void)(name), (void)(v), &hosted_lib)
#define CloseLibrary(b) ((void)(b))
#define PutStr(s) fputs((const char *)(s), stdout)
#define Printf(...) printf(__VA_ARGS__)
#define SARG(s) ((const char *)(s))
static BPTR Open(CONST_STRPTR name, long mode) { (void)mode; return (BPTR)fopen((const char *)name, "rb"); }
static void Close(BPTR fh) { fclose((FILE *)fh); }
static LONG Seek(BPTR fh, LONG pos, LONG mode)
{
    long old = ftell((FILE *)fh);
    fseek((FILE *)fh, pos, mode == OFFSET_END ? SEEK_END : SEEK_SET);
    return (LONG)old;
}
static LONG Read(BPTR fh, APTR buf, LONG n) { return (LONG)fread(buf, 1, (size_t)n, (FILE *)fh); }
static void DateStamp(struct DateStamp *ds)
{
    long t = (long)(clock() / (CLOCKS_PER_SEC / 50));
    ds->ds_Minute = t / 3000;
    ds->ds_Tick = t % 3000;
}

/* The core's doubles are two longs, most significant first (the 68k's own
 * order): on a little-endian host the halves of a C double swap over */
static int host_le(void) { const unsigned one = 1; return *(const unsigned char *)&one; }
static void host_swap(oj_u32 *w)
{
    oj_u32 t;
    if (!host_le()) return;
    t = w[0];
    w[0] = w[1];
    w[1] = t;
}
static oj_s32 h_get_double(const struct oj_doc *d, oj_u32 n, double *v)
{
    oj_s32 rc = oj_get_double(&hosted_env, d, n, (oj_u32 *)v);
    host_swap((oj_u32 *)v);
    return rc;
}
static oj_s32 h_text_to_double(const char *t, oj_u32 l, double *v)
{
    oj_s32 rc = oj_text_to_double(&hosted_env, t, l, (oj_u32 *)v);
    host_swap((oj_u32 *)v);
    return rc;
}
static oj_u32 h_double_to_text(const double *v, char *b)
{
    double c = *v;
    host_swap((oj_u32 *)&c);
    return oj_double_to_text(&hosted_env, (const oj_u32 *)&c, b);
}
static oj_s32 h_write_double(struct oj_writer *w, const double *v)
{
    double c = *v;
    host_swap((oj_u32 *)&c);
    return oj_write_double(w, (const oj_u32 *)&c);
}

#define D_(d) ((const struct oj_doc *)(d))
#define JSONDocSize(t, l, f, e) oj_doc_size((const oj_u8 *)(t), l, f, (struct oj_error *)(e))
#define JSONParseInto(t, l, f, m, s, e) \
    ((struct JSONDoc *)oj_parse_into((const oj_u8 *)(t), l, f, m, s, (struct oj_error *)(e)))
#define JSONParse(t, l, f, e) ((struct JSONDoc *)oj_parse(&hosted_env, (const oj_u8 *)(t), l, f, (struct oj_error *)(e)))
#define JSONFreeDoc(d) oj_free_doc(&hosted_env, (struct oj_doc *)(d))
#define JSONCheckDoc(d, s) oj_check_doc(D_(d), s)
#define JSONType(d, n) oj_type(D_(d), n)
#define JSONCount(d, n) oj_count(D_(d), n)
#define JSONSkip(d, n) oj_skip(D_(d), n)
#define JSONIndex(d, n, i) oj_index(D_(d), n, i)
#define JSONFind(d, n, k, l) oj_find(D_(d), n, (const char *)(k), l)
#define JSONPointer(d, n, p) oj_pointer(D_(d), n, (const char *)(p))
#define JSONGetString(d, n, l) ((CONST_STRPTR)oj_get_string(D_(d), n, l))
#define JSONGetLong(d, n, v) oj_get_long(D_(d), n, v)
#define JSONGetDouble(d, n, v) h_get_double(D_(d), n, v)
#define JSONGetLatin1(d, n, b, s) oj_get_latin1(D_(d), n, (char *)(b), s)
#define JSONTextToDouble(t, l, v) h_text_to_double((const char *)(t), l, v)
#define JSONTextToLong(t, l, v) oj_text_to_long((const char *)(t), l, v)
#define JSONDoubleToText(v, b) h_double_to_text(v, (char *)(b))
#define JSONErrorString(c) ((CONST_STRPTR)oj_error_string(c))
#define JSONNewWriter(f) ((struct JSONWriter *)oj_new_writer(&hosted_env, f))
#define W_(w) ((struct oj_writer *)(w))
#define JSONFreeWriter(w) oj_free_writer(W_(w))
#define JSONWriteBegin(w, t) oj_write_begin(W_(w), t)
#define JSONWriteEnd(w) oj_write_end(W_(w))
#define JSONWriteKey(w, k, l) oj_write_key(W_(w), (const char *)(k), l)
#define JSONWriteString(w, s, l) oj_write_string(W_(w), (const char *)(s), l)
#define JSONWriteLong(w, v) oj_write_long(W_(w), v)
#define JSONWriteDouble(w, v) h_write_double(W_(w), v)
#define JSONWriteBool(w, v) oj_write_bool(W_(w), v)
#define JSONWriteNull(w) oj_write_null(W_(w))
#define JSONWriteNumberText(w, t, l) oj_write_number_text(W_(w), (const char *)(t), l)
#define JSONWriteNode(w, d, n) oj_write_node(W_(w), D_(d), n)
#define JSONWriterText(w, l) ((CONST_STRPTR)oj_writer_text(W_(w), l))
#define JSONWriterError(w) oj_writer_error(W_(w))
#define JSONResetWriter(w) oj_reset_writer(W_(w))
#define JSONStringify(d, n, f, l) ((STRPTR)oj_stringify(&hosted_env, D_(d), n, f, l))
#define JSONFreeText(t) oj_free_text(&hosted_env, (char *)(t))
#define R_(r) ((struct oj_reader *)(r))
#define JSONNewReader(f) ((struct JSONReader *)oj_new_reader(&hosted_env, f))
#define JSONFreeReader(r) oj_free_reader(R_(r))
#define JSONReaderFeed(r, b, l) oj_reader_feed(R_(r), (const oj_u8 *)(b), l)
#define JSONReaderNext(r) oj_reader_next(R_(r))
#define JSONReaderText(r, l) ((CONST_STRPTR)oj_reader_text(R_(r), l))
#define JSONReaderDepth(r) oj_reader_depth(R_(r))
#define JSONReaderError(r, e) oj_reader_error(R_(r), (struct oj_error *)(e))
#endif

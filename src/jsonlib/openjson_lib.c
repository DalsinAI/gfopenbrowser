/* GFOpenJSON: json.library, JSON (RFC 8259) for native programs.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * The work is done in json_core.cpp (lean C++, no Amiga headers), tested
 * on the host and under qemu-m68k (tests/jsonlib/run.sh); this file is
 * the library around it: the RomTag, the vector table in
 * sfd/json_lib.sfd's order, each call taking its arguments in its
 * registers, and what the core asks of Exec (struct oj_env: AllocVec and
 * FreeVec).
 *
 * Multicore (amigachrome-guest libraries/OPEN_LIBRARIES.md, section 5):
 * SysBase is set once at init and only read after; every other byte of
 * state is in the caller's own document, writer or reader, so any task
 * may call any vector at once. A document is never changed by the
 * library once it is made: any number of tasks may read one together.
 */
#include <exec/types.h>
#include <dos/dos.h>   /* BPTR: NDK 3.2 has it here, not in exec/types.h */
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <libraries/json.h>

#include "json_core.h"

/* Arguments in registers: an amiga-gcc extension (the host-side compile check
 * in tests/jsonlib/run.sh defines REG as a plain parameter) */
#ifndef REG
#define REG(r, decl) register decl __asm(#r)
#endif

#define LIB_VERSION  1
#define LIB_REVISION 0

/* The block and the error are the core's, as libraries/json.h lays them out */
typedef char check_doc[sizeof(struct JSONDoc) == sizeof(struct oj_doc) ? 1 : -1];
typedef char check_node[sizeof(struct JSONNode) == sizeof(struct oj_node) ? 1 : -1];
typedef char check_error[sizeof(struct JSONError) == sizeof(struct oj_error) ? 1 : -1];
typedef char check_double[sizeof(DOUBLE) == 8 ? 1 : -1];

struct OJBase {
    struct Library lib;
    BPTR seglist;
};

/* Set once by lib_init, read-only after: no per-call state lives here. */
struct ExecBase *SysBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = JSONNAME;
static const char lib_id[] = "json.library 1.0 (22.10.2026) GFOpenJSON, AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: json.library 1.0 (22.10.2026) GFOpenJSON, AmiCradle Team";

/* ---- What the core asks of Exec ------------------------------------------ */

static void *env_alloc(struct oj_env *env, oj_u32 size)
{
    (void)env;
    return AllocVec(size ? size : 1, MEMF_ANY);
}

static void env_free(struct oj_env *env, void *mem)
{
    (void)env;
    if (mem) FreeVec(mem);
}

/* const: the library's only writable global is SysBase */
static const struct oj_env env = { env_alloc, env_free };
#define E ((struct oj_env *)&env)

/* ---- Library housekeeping -------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct OJBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct OJBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_expunge(REG(a6, struct OJBase *base));

static BPTR lib_close(REG(a6, struct OJBase *base))
{
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct OJBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void)
{
    return 0;
}

/* ---- The calls, in their registers (sfd/json_lib.sfd) ------------------------ */

#define DOC struct JSONDoc
#define CDOC const struct JSONDoc
#define W struct JSONWriter
#define R struct JSONReader
#define CORE_DOC(d) ((const struct oj_doc *)(d))

static ULONG L_JSONDocSize(REG(a0, CONST_STRPTR text), REG(d0, ULONG len), REG(d1, ULONG flags),
                           REG(a1, struct JSONError *err))
{ return oj_doc_size((const oj_u8 *)text, len, flags, (struct oj_error *)err); }
static DOC *L_JSONParseInto(REG(a0, CONST_STRPTR text), REG(d0, ULONG len), REG(d1, ULONG flags), REG(a1, APTR mem),
                            REG(d2, ULONG size), REG(a2, struct JSONError *err))
{ return (DOC *)oj_parse_into((const oj_u8 *)text, len, flags, mem, size, (struct oj_error *)err); }
static DOC *L_JSONParse(REG(a0, CONST_STRPTR text), REG(d0, ULONG len), REG(d1, ULONG flags),
                        REG(a1, struct JSONError *err))
{ return (DOC *)oj_parse(E, (const oj_u8 *)text, len, flags, (struct oj_error *)err); }
static void L_JSONFreeDoc(REG(a0, DOC *doc))
{ oj_free_doc(E, (struct oj_doc *)doc); }
static LONG L_JSONCheckDoc(REG(a0, CDOC *doc), REG(d0, ULONG size))
{ return oj_check_doc(CORE_DOC(doc), size); }
static ULONG L_JSONType(REG(a0, CDOC *doc), REG(d0, ULONG node))
{ return oj_type(CORE_DOC(doc), node); }
static ULONG L_JSONCount(REG(a0, CDOC *doc), REG(d0, ULONG node))
{ return oj_count(CORE_DOC(doc), node); }
static ULONG L_JSONSkip(REG(a0, CDOC *doc), REG(d0, ULONG node))
{ return oj_skip(CORE_DOC(doc), node); }
static ULONG L_JSONIndex(REG(a0, CDOC *doc), REG(d0, ULONG node), REG(d1, ULONG index))
{ return oj_index(CORE_DOC(doc), node, index); }
static ULONG L_JSONFind(REG(a0, CDOC *doc), REG(d0, ULONG node), REG(a1, CONST_STRPTR key), REG(d1, ULONG keylen))
{ return oj_find(CORE_DOC(doc), node, (const char *)key, keylen); }
static ULONG L_JSONPointer(REG(a0, CDOC *doc), REG(d0, ULONG node), REG(a1, CONST_STRPTR pointer))
{ return oj_pointer(CORE_DOC(doc), node, (const char *)pointer); }
static CONST_STRPTR L_JSONGetString(REG(a0, CDOC *doc), REG(d0, ULONG node), REG(a1, ULONG *len))
{ return (CONST_STRPTR)oj_get_string(CORE_DOC(doc), node, (oj_u32 *)len); }
static LONG L_JSONGetLong(REG(a0, CDOC *doc), REG(d0, ULONG node), REG(a1, LONG *value))
{ return oj_get_long(CORE_DOC(doc), node, (oj_s32 *)value); }
static LONG L_JSONGetDouble(REG(a0, CDOC *doc), REG(d0, ULONG node), REG(a1, DOUBLE *value))
{ return oj_get_double(E, CORE_DOC(doc), node, (oj_u32 *)value); }
static ULONG L_JSONGetLatin1(REG(a0, CDOC *doc), REG(d0, ULONG node), REG(a1, STRPTR buffer), REG(d1, ULONG size))
{ return oj_get_latin1(CORE_DOC(doc), node, (char *)buffer, size); }
static LONG L_JSONTextToDouble(REG(a0, CONST_STRPTR text), REG(d0, ULONG len), REG(a1, DOUBLE *value))
{ return oj_text_to_double(E, (const char *)text, len, (oj_u32 *)value); }
static LONG L_JSONTextToLong(REG(a0, CONST_STRPTR text), REG(d0, ULONG len), REG(a1, LONG *value))
{ return oj_text_to_long((const char *)text, len, (oj_s32 *)value); }
static ULONG L_JSONDoubleToText(REG(a0, const DOUBLE *value), REG(a1, STRPTR buffer))
{ return oj_double_to_text(E, (const oj_u32 *)value, (char *)buffer); }
static CONST_STRPTR L_JSONErrorString(REG(d0, LONG code))
{ return (CONST_STRPTR)oj_error_string(code); }
static W *L_JSONNewWriter(REG(d0, ULONG flags))
{ return (W *)oj_new_writer(E, flags); }
static void L_JSONFreeWriter(REG(a0, W *w))
{ oj_free_writer((struct oj_writer *)w); }
static LONG L_JSONWriteBegin(REG(a0, W *w), REG(d0, ULONG type))
{ return oj_write_begin((struct oj_writer *)w, type); }
static LONG L_JSONWriteEnd(REG(a0, W *w))
{ return oj_write_end((struct oj_writer *)w); }
static LONG L_JSONWriteKey(REG(a0, W *w), REG(a1, CONST_STRPTR key), REG(d0, ULONG len))
{ return oj_write_key((struct oj_writer *)w, (const char *)key, len); }
static LONG L_JSONWriteString(REG(a0, W *w), REG(a1, CONST_STRPTR string), REG(d0, ULONG len))
{ return oj_write_string((struct oj_writer *)w, (const char *)string, len); }
static LONG L_JSONWriteLong(REG(a0, W *w), REG(d0, LONG value))
{ return oj_write_long((struct oj_writer *)w, value); }
static LONG L_JSONWriteDouble(REG(a0, W *w), REG(a1, const DOUBLE *value))
{ return oj_write_double((struct oj_writer *)w, (const oj_u32 *)value); }
static LONG L_JSONWriteBool(REG(a0, W *w), REG(d0, LONG value))
{ return oj_write_bool((struct oj_writer *)w, value); }
static LONG L_JSONWriteNull(REG(a0, W *w))
{ return oj_write_null((struct oj_writer *)w); }
static LONG L_JSONWriteNumberText(REG(a0, W *w), REG(a1, CONST_STRPTR text), REG(d0, ULONG len))
{ return oj_write_number_text((struct oj_writer *)w, (const char *)text, len); }
static LONG L_JSONWriteNode(REG(a0, W *w), REG(a1, CDOC *doc), REG(d0, ULONG node))
{ return oj_write_node((struct oj_writer *)w, CORE_DOC(doc), node); }
static CONST_STRPTR L_JSONWriterText(REG(a0, W *w), REG(a1, ULONG *len))
{ return (CONST_STRPTR)oj_writer_text((struct oj_writer *)w, (oj_u32 *)len); }
static LONG L_JSONWriterError(REG(a0, W *w))
{ return oj_writer_error((struct oj_writer *)w); }
static void L_JSONResetWriter(REG(a0, W *w))
{ oj_reset_writer((struct oj_writer *)w); }
static STRPTR L_JSONStringify(REG(a0, CDOC *doc), REG(d0, ULONG node), REG(d1, ULONG flags), REG(a1, ULONG *len))
{ return (STRPTR)oj_stringify(E, CORE_DOC(doc), node, flags, (oj_u32 *)len); }
static void L_JSONFreeText(REG(a0, STRPTR text))
{ oj_free_text(E, (char *)text); }
static R *L_JSONNewReader(REG(d0, ULONG flags))
{ return (R *)oj_new_reader(E, flags); }
static void L_JSONFreeReader(REG(a0, R *r))
{ oj_free_reader((struct oj_reader *)r); }
static void L_JSONReaderFeed(REG(a0, R *r), REG(a1, APTR buffer), REG(d0, ULONG len))
{ oj_reader_feed((struct oj_reader *)r, (const oj_u8 *)buffer, len); }
static LONG L_JSONReaderNext(REG(a0, R *r))
{ return oj_reader_next((struct oj_reader *)r); }
static CONST_STRPTR L_JSONReaderText(REG(a0, R *r), REG(a1, ULONG *len))
{ return (CONST_STRPTR)oj_reader_text((struct oj_reader *)r, (oj_u32 *)len); }
static ULONG L_JSONReaderDepth(REG(a0, R *r))
{ return oj_reader_depth((struct oj_reader *)r); }
static LONG L_JSONReaderError(REG(a0, R *r), REG(a1, struct JSONError *err))
{ return oj_reader_error((struct oj_reader *)r, (struct oj_error *)err); }

/* sfd/json_lib.sfd's order, line for line (check_vectors.py) */
static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)L_JSONDocSize,            /*  -30 */
    (APTR)L_JSONParseInto,          /*  -36 */
    (APTR)L_JSONParse,              /*  -42 */
    (APTR)L_JSONFreeDoc,            /*  -48 */
    (APTR)L_JSONCheckDoc,           /*  -54 */
    (APTR)L_JSONType,               /*  -60 */
    (APTR)L_JSONCount,              /*  -66 */
    (APTR)L_JSONSkip,               /*  -72 */
    (APTR)L_JSONIndex,              /*  -78 */
    (APTR)L_JSONFind,               /*  -84 */
    (APTR)L_JSONPointer,            /*  -90 */
    (APTR)L_JSONGetString,          /*  -96 */
    (APTR)L_JSONGetLong,            /* -102 */
    (APTR)L_JSONGetDouble,          /* -108 */
    (APTR)L_JSONGetLatin1,          /* -114 */
    (APTR)L_JSONTextToDouble,       /* -120 */
    (APTR)L_JSONTextToLong,         /* -126 */
    (APTR)L_JSONDoubleToText,       /* -132 */
    (APTR)L_JSONErrorString,        /* -138 */
    (APTR)L_JSONNewWriter,          /* -144 */
    (APTR)L_JSONFreeWriter,         /* -150 */
    (APTR)L_JSONWriteBegin,         /* -156 */
    (APTR)L_JSONWriteEnd,           /* -162 */
    (APTR)L_JSONWriteKey,           /* -168 */
    (APTR)L_JSONWriteString,        /* -174 */
    (APTR)L_JSONWriteLong,          /* -180 */
    (APTR)L_JSONWriteDouble,        /* -186 */
    (APTR)L_JSONWriteBool,          /* -192 */
    (APTR)L_JSONWriteNull,          /* -198 */
    (APTR)L_JSONWriteNumberText,    /* -204 */
    (APTR)L_JSONWriteNode,          /* -210 */
    (APTR)L_JSONWriterText,         /* -216 */
    (APTR)L_JSONWriterError,        /* -222 */
    (APTR)L_JSONResetWriter,        /* -228 */
    (APTR)L_JSONStringify,          /* -234 */
    (APTR)L_JSONFreeText,           /* -240 */
    (APTR)L_JSONNewReader,          /* -246 */
    (APTR)L_JSONFreeReader,         /* -252 */
    (APTR)L_JSONReaderFeed,         /* -258 */
    (APTR)L_JSONReaderNext,         /* -264 */
    (APTR)L_JSONReaderText,         /* -270 */
    (APTR)L_JSONReaderDepth,        /* -276 */
    (APTR)L_JSONReaderError,        /* -282 */
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct OJBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};

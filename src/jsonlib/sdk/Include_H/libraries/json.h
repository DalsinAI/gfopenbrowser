#ifndef LIBRARIES_JSON_H
#define LIBRARIES_JSON_H
/*
 * libraries/json.h: json.library, GFOpenJSON.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * json.library parses JSON text (RFC 8259) into one flat block of memory,
 * a JSONDoc: a header, an array of 8-byte nodes in document order, and a
 * pool of the strings (unescaped UTF-8, each NUL-terminated) and number
 * texts. Everything in the block is an offset from its start, never a
 * pointer, so a JSONDoc can be copied with CopyMem, sent in a message,
 * written to a file and read back: it is the value itself. JSONCheckDoc
 * verifies a block that came from somewhere you do not trust.
 *
 * A node is a JSONNode index (ULONG); the root is node 0. A container's
 * children follow it directly; its jn_Data is the index just past its
 * last descendant, so a whole subtree is skipped in one step. An
 * object's children are key, value, key, value... (keys are strings).
 *
 * The block's format is part of the ABI: walk it with the macros below
 * without calling the library at all, or through the library's calls.
 */

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#define JSONNAME "json.library"
#define JSONVERSION 1

/* ---- The block --------------------------------------------------------- */

#define JSON_MAGIC 0x4A534E31   /* 'JSN1' */

struct JSONDoc {
    ULONG jd_Magic;         /* JSON_MAGIC */
    ULONG jd_Size;          /* bytes in the whole block, header included */
    ULONG jd_Nodes;         /* number of nodes (at least 1) */
    ULONG jd_NodeOffset;    /* from the block's start to node 0 */
    ULONG jd_PoolOffset;    /* from the block's start to the pool */
    ULONG jd_PoolSize;      /* bytes in the pool */
    ULONG jd_Flags;         /* JSONDF_... */
    ULONG jd_Reserved;      /* 0 */
};

/* jd_Flags */
#define JSONDF_ALLOCATED 1  /* JSONParse allocated it: free it with JSONFreeDoc */

struct JSONNode {
    ULONG jn_Head;          /* type in bits 31-28, count or pool offset in 27-0 */
    ULONG jn_Data;          /* value, length or end index, by type */
};

/* Node types (JSONType, JSON_NTYPE). 0 is no node. */
#define JSON_NULL   1       /* Head: type only */
#define JSON_FALSE  2
#define JSON_TRUE   3
#define JSON_INT    4       /* an integer that fits a LONG: Data is the value */
#define JSON_NUMBER 5       /* any other number: Head offset of its text in the pool, Data its length */
#define JSON_STRING 6       /* Head offset of its UTF-8 in the pool, Data its length in bytes */
#define JSON_ARRAY  7       /* Head its count of elements, Data the index after its subtree */
#define JSON_OBJECT 8       /* Head its count of members (key + value pairs), Data as an array */

#define JSON_NONE   0xFFFFFFFFUL    /* "no node" from the lookup calls */

/* Walking a block without the library */
#define JSON_NODES(doc)   ((struct JSONNode *)((UBYTE *)(doc) + (doc)->jd_NodeOffset))
#define JSON_POOL(doc)    ((UBYTE *)(doc) + (doc)->jd_PoolOffset)
#define JSON_NTYPE(n)     ((n)->jn_Head >> 28)
#define JSON_NFIELD(n)    ((n)->jn_Head & 0x0FFFFFFFUL)    /* count, or pool offset */
#define JSON_NTEXT(doc, n) ((STRPTR)JSON_POOL(doc) + JSON_NFIELD(n))
#define JSON_NSKIP(nodes, i) (JSON_NTYPE(&(nodes)[i]) >= JSON_ARRAY ? (nodes)[i].jn_Data : (i) + 1)

/* Writers and readers are the library's own: only pointers to them */
struct JSONWriter;
struct JSONReader;

/* ---- Errors -------------------------------------------------------------- */

struct JSONError {
    LONG  je_Code;          /* JSONERR_... */
    ULONG je_Offset;        /* byte offset of the problem in the input */
    ULONG je_Line;          /* 1-based line */
    ULONG je_Column;        /* 1-based column, in bytes */
};

#define JSONERR_OK          0
#define JSONERR_NOMEM       1   /* out of memory */
#define JSONERR_SYNTAX      2   /* a character that cannot be there */
#define JSONERR_EOF         3   /* the text ended inside a value */
#define JSONERR_CONTROL     4   /* a control character inside a string */
#define JSONERR_ESCAPE      5   /* a bad \ escape */
#define JSONERR_UTF8        6   /* not UTF-8 */
#define JSONERR_NUMBER      7   /* a malformed number */
#define JSONERR_DEPTH       8   /* nested deeper than JSON_MAXDEPTH */
#define JSONERR_TOOBIG      9   /* more than 2^28 - 1 nodes, pool bytes or members */
#define JSONERR_TRAILING    10  /* more after the value */
#define JSONERR_BUFFER      11  /* JSONParseInto's block is too small */
#define JSONERR_BADDOC      12  /* JSONCheckDoc: not a sound JSONDoc */
#define JSONERR_STATE       13  /* a writer call out of order */
#define JSONERR_TYPE        14  /* the node is not of the type asked for */
#define JSONERR_RANGE       15  /* the number does not fit */

#define JSON_MAXDEPTH 1024

/* ---- Flags ---------------------------------------------------------------- */

/* JSONParse, JSONDocSize, JSONParseInto, JSONNewReader */
#define JSONF_COMMENTS  (1UL << 0)  /* allow // and slash-star comments */
#define JSONF_TRAILING  (1UL << 1)  /* allow a comma before ] and } */
#define JSONF_LATIN1    (1UL << 2)  /* the text is ISO-8859-1, not UTF-8 (strings come out UTF-8) */
#define JSONF_MULTI     (1UL << 3)  /* reader: one value after another (JSON Lines) */

/* JSONNewWriter, JSONStringify */
#define JSONW_PRETTY    (1UL << 0)  /* new lines, two-space indent */
#define JSONW_TABS      (1UL << 1)  /* new lines, tab indent */
#define JSONW_ASCII     (1UL << 2)  /* escape every non-ASCII character as \uXXXX */
#define JSONW_LATIN1    (1UL << 3)  /* strings given to the writer are ISO-8859-1 */
#define JSONW_MULTI     (1UL << 4)  /* several top-level values, one per line */

/* Length argument: the string is NUL-terminated */
#define JSON_STRLEN 0xFFFFFFFFUL

/* ---- Streaming reader events (JSONReaderNext) ------------------------------ */

#define JSONEV_ERROR        (-1)    /* JSONReaderError says why; stays an error */
#define JSONEV_MORE         0       /* feed more text (JSONReaderFeed) */
#define JSONEV_OBJECT       1       /* { */
#define JSONEV_OBJECT_END   2       /* } */
#define JSONEV_ARRAY        3       /* [ */
#define JSONEV_ARRAY_END    4       /* ] */
#define JSONEV_KEY          5       /* JSONReaderText: the key */
#define JSONEV_STRING       6       /* JSONReaderText: the string */
#define JSONEV_NUMBER       7       /* JSONReaderText: the number's text */
#define JSONEV_TRUE         8
#define JSONEV_FALSE        9
#define JSONEV_NULL         10
#define JSONEV_DONE         11      /* the text ended (a JSONReaderFeed of 0 bytes)
                                       cleanly after its value (JSONF_MULTI: after
                                       any number of values) */

#endif /* LIBRARIES_JSON_H */

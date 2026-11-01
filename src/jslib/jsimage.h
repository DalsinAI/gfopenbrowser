/*
 * jsimage: a program that takes JavaScriptCore, WTF and bmalloc from
 * JavaScriptEngine's code instead of linking its own copy
 * (scripts/jsimage.py says how it is linked).
 *
 * The client's first code is jsimage_entry (jsimage_client.c): it loads
 * JavaScriptEngine, checks that it is the build the client was linked
 * against, makes the client's references to it point at where it was
 * loaded, and runs it. JavaScriptEngine's main (jsl_engine.c) sees
 * jsimage_client set and runs the client's own start-up in the same
 * process (jsimage_host.c), so both C run-times are set up, and every
 * global, the allocator and the C library's state the two share are the
 * image's.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef JSIMAGE_H
#define JSIMAGE_H

/* The fixup table: the client's second hunk, written by jsimage.py */
struct JSImageTable {
    unsigned long jt_Magic;         /* JSIMAGE_MAGIC */
    unsigned long jt_Version;       /* JSIMAGE_VERSION */
    unsigned long jt_ID;            /* the image's jsimage_id */
    unsigned long jt_Base;          /* the client's __jsimage_base */
    long jt_IDOffset;               /* jsimage_id's offset in the image */
    long jt_ClientOffset;           /* jsimage_client's offset in the image */
    unsigned long jt_Applied;       /* what the fixups have added so far */
    unsigned long jt_Count;
    unsigned long jt_Offset[1];     /* jt_Count offsets in the client's hunk */
};
#define JSIMAGE_MAGIC   0x4A53494DUL    /* 'JSIM' */
#define JSIMAGE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/* In the image: the client's start-up to run (set by the client's
 * jsimage_entry before it runs the image), and the image's id (written
 * into the program by jsimage.py after the link) */
extern void *jsimage_client;
extern unsigned long jsimage_id;

/* In the image's main: run the client, with the image's command line */
long jsimage_run_client(void);

#ifdef __cplusplus
}
#endif

#endif

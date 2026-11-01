/* GFOpenJSON's core: json.library's work in lean C++, no Amiga headers.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Our own code throughout, written from RFC 8259 (JSON), RFC 6901 (JSON
 * Pointer), RFC 3629 (UTF-8), IEEE 754 and the ECMAScript Number-to-String
 * rules; no third-party JSON or number-conversion code.
 *
 * This header is C as well as C++: openjson_lib.c (the library shell) and
 * the tests include it. The block it builds is the one include/libraries/
 * json.h documents (struct JSONDoc, struct JSONNode); the numbers here are
 * that header's. Memory comes only through struct oj_env. The core uses
 * no floating point at all: numbers are converted to and from IEEE 754
 * doubles with integer arithmetic, so the library needs no maths library
 * and no FPU, and its answers are exact (correctly rounded) on every CPU.
 */
#ifndef JSON_CORE_H
#define JSON_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int oj_s32;
typedef unsigned int oj_u32;
typedef unsigned char oj_u8;

struct oj_env {
    void *(*alloc)(struct oj_env *env, oj_u32 size);   /* need not clear; NULL on failure */
    void (*free)(struct oj_env *env, void *mem);
};

/* struct JSONDoc */
struct oj_doc {
    oj_u32 magic, size, nodes, node_off, pool_off, pool_size, flags, reserved;
};
/* struct JSONNode */
struct oj_node {
    oj_u32 head, data;
};
/* struct JSONError */
struct oj_error {
    oj_s32 code;
    oj_u32 offset, line, column;
};

#define OJ_MAGIC 0x4A534E31u
#define OJ_DF_ALLOCATED 1u
#define OJ_NONE 0xFFFFFFFFu
#define OJ_STRLEN 0xFFFFFFFFu
#define OJ_MAXDEPTH 1024
#define OJ_FIELD_MAX 0x0FFFFFFFu

enum { OJ_T_NULL = 1, OJ_T_FALSE, OJ_T_TRUE, OJ_T_INT, OJ_T_NUMBER, OJ_T_STRING, OJ_T_ARRAY, OJ_T_OBJECT };

enum {
    OJ_OK = 0, OJ_NOMEM, OJ_SYNTAX, OJ_EOF, OJ_CONTROL, OJ_ESCAPE, OJ_UTF8, OJ_NUMBER, OJ_DEPTH,
    OJ_TOOBIG, OJ_TRAILING, OJ_BUFFER, OJ_BADDOC, OJ_STATE, OJ_TYPE, OJ_RANGE, OJ_ERRORS
};

#define OJ_F_COMMENTS 1u
#define OJ_F_TRAILING 2u
#define OJ_F_LATIN1   4u
#define OJ_F_MULTI    8u

#define OJ_W_PRETTY 1u
#define OJ_W_TABS   2u
#define OJ_W_ASCII  4u
#define OJ_W_LATIN1 8u
#define OJ_W_MULTI  16u

enum {
    OJ_EV_ERROR = -1, OJ_EV_MORE = 0, OJ_EV_OBJECT, OJ_EV_OBJECT_END, OJ_EV_ARRAY, OJ_EV_ARRAY_END,
    OJ_EV_KEY, OJ_EV_STRING, OJ_EV_NUMBER, OJ_EV_TRUE, OJ_EV_FALSE, OJ_EV_NULL, OJ_EV_DONE
};

/* Parsing into a block (len OJ_STRLEN: the text is NUL-terminated) */
oj_u32 oj_doc_size(const oj_u8 *text, oj_u32 len, oj_u32 flags, struct oj_error *err);
struct oj_doc *oj_parse_into(const oj_u8 *text, oj_u32 len, oj_u32 flags, void *mem, oj_u32 size,
                             struct oj_error *err);
struct oj_doc *oj_parse(struct oj_env *env, const oj_u8 *text, oj_u32 len, oj_u32 flags,
                        struct oj_error *err);
void oj_free_doc(struct oj_env *env, struct oj_doc *doc);
oj_s32 oj_check_doc(const struct oj_doc *doc, oj_u32 size);

/* Reading a block */
oj_u32 oj_type(const struct oj_doc *doc, oj_u32 node);
oj_u32 oj_count(const struct oj_doc *doc, oj_u32 node);
oj_u32 oj_skip(const struct oj_doc *doc, oj_u32 node);
oj_u32 oj_index(const struct oj_doc *doc, oj_u32 node, oj_u32 i);
oj_u32 oj_find(const struct oj_doc *doc, oj_u32 node, const char *key, oj_u32 keylen);
oj_u32 oj_pointer(const struct oj_doc *doc, oj_u32 node, const char *ptr);
const char *oj_get_string(const struct oj_doc *doc, oj_u32 node, oj_u32 *len);
oj_s32 oj_get_long(const struct oj_doc *doc, oj_u32 node, oj_s32 *out);
oj_s32 oj_get_double(struct oj_env *env, const struct oj_doc *doc, oj_u32 node, oj_u32 out[2]);
oj_u32 oj_get_latin1(const struct oj_doc *doc, oj_u32 node, char *buf, oj_u32 size);

/* Numbers. A double is two longs, most significant first (the 68k's own
 * order). Both calls take exactly RFC 8259's number grammar. */
oj_s32 oj_text_to_double(struct oj_env *env, const char *text, oj_u32 len, oj_u32 out[2]);
oj_s32 oj_text_to_long(const char *text, oj_u32 len, oj_s32 *out);
/* The shortest text that reads back as the same double (ECMAScript's
 * Number::toString form); NaN and the infinities give "null". buf needs
 * 26 bytes; returns the length, buf NUL-terminated (0: out of memory).
 * Numbers that need the bignums (a few hundred bytes) get them from env. */
oj_u32 oj_double_to_text(struct oj_env *env, const oj_u32 val[2], char *buf);

const char *oj_error_string(oj_s32 code);

/* Writer */
struct oj_writer;
struct oj_writer *oj_new_writer(struct oj_env *env, oj_u32 flags);
void oj_free_writer(struct oj_writer *w);
oj_s32 oj_write_begin(struct oj_writer *w, oj_u32 type);
oj_s32 oj_write_end(struct oj_writer *w);
oj_s32 oj_write_key(struct oj_writer *w, const char *key, oj_u32 len);
oj_s32 oj_write_string(struct oj_writer *w, const char *str, oj_u32 len);
oj_s32 oj_write_long(struct oj_writer *w, oj_s32 val);
oj_s32 oj_write_double(struct oj_writer *w, const oj_u32 val[2]);
oj_s32 oj_write_bool(struct oj_writer *w, oj_s32 val);
oj_s32 oj_write_null(struct oj_writer *w);
oj_s32 oj_write_number_text(struct oj_writer *w, const char *text, oj_u32 len);
oj_s32 oj_write_node(struct oj_writer *w, const struct oj_doc *doc, oj_u32 node);
const char *oj_writer_text(struct oj_writer *w, oj_u32 *len);
oj_s32 oj_writer_error(struct oj_writer *w);
void oj_reset_writer(struct oj_writer *w);
char *oj_stringify(struct oj_env *env, const struct oj_doc *doc, oj_u32 node, oj_u32 flags, oj_u32 *len);
void oj_free_text(struct oj_env *env, char *text);

/* Streaming reader */
struct oj_reader;
struct oj_reader *oj_new_reader(struct oj_env *env, oj_u32 flags);
void oj_free_reader(struct oj_reader *r);
void oj_reader_feed(struct oj_reader *r, const oj_u8 *buf, oj_u32 len);
oj_s32 oj_reader_next(struct oj_reader *r);
const char *oj_reader_text(struct oj_reader *r, oj_u32 *len);
oj_u32 oj_reader_depth(struct oj_reader *r);
oj_s32 oj_reader_error(struct oj_reader *r, struct oj_error *err);

#ifdef __cplusplus
}
#endif
#endif

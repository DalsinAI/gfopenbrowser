/* Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root). */
/* json.library's core (src/jsonlib/json_core.cpp) driven from a script,
 * on the host and under qemu-m68k; tests/jsonlib/json_ref.py writes the
 * script and checks the answers against Python's json module and Node's
 * Number formatting.
 *   json_test script.txt
 * Each script line is one case:
 *   parse FLAGS FILE      parse the file; "ok TEXT" (the document written
 *                         back compact by the writer) or "err CODE OFF LINE COL"
 *   reader FLAGS SEED FILE  the same file through the streaming reader, fed
 *                         in pieces cut by SEED; answers as parse
 *   num TEXT              text to double: "num BITS LONG" (LONG: the value
 *                         or "-" if not an exact LONG)
 *   fmt BITS              double to text
 * Every parse also checks the block with oj_check_doc, copies it to fresh
 * memory and walks the copy, and compares oj_parse_into with oj_parse. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json_core.h"

static unsigned long allocs;
static void *h_alloc(struct oj_env *e, oj_u32 n)
{
    (void)e;
    allocs++;
    return malloc(n ? n : 1);
}
static void h_free(struct oj_env *e, void *p)
{
    (void)e;
    allocs--;
    free(p);
}
static struct oj_env env = { h_alloc, h_free };

static unsigned long long rng;
static unsigned rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned)(rng >> 16);
}

static unsigned char *load(const char *path, unsigned *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) n = 0;
    fclose(f);
    *len = (unsigned)n;
    return b;
}

/* "ok TEXT"; the writer's new lines between JSONW_MULTI values become
 * spaces (compact text has no other new lines or spaces outside strings) */
static void put_text(const char *s, oj_u32 n)
{
    oj_u32 i;
    fputs("ok ", stdout);
    for (i = 0; i < n; i++)
        putchar(s[i] == '\n' ? ' ' : s[i]);
    putchar('\n');
}

/* Walk every node through the calls, checking they agree with the block */
static int walk(const struct oj_doc *doc)
{
    oj_u32 i, n = doc->nodes;
    for (i = 0; i < n; i++) {
        oj_u32 t = oj_type(doc, i);
        if (t >= 7) {
            oj_u32 c = oj_count(doc, i), k, j = i + 1;
            for (k = 0; k < c; k++) {
                if (oj_index(doc, i, k) != j) return 0;
                if (t == 8) {
                    oj_u32 kl;
                    const char *key = oj_get_string(doc, j, &kl);
                    oj_u32 v = oj_find(doc, i, key, kl);
                    /* the last equal key wins */
                    if (v == 0xFFFFFFFFu || oj_type(doc, v - 1) != 6) return 0;
                    j = oj_skip(doc, j + 1);
                } else {
                    j = oj_skip(doc, j);
                }
            }
            if (j != oj_skip(doc, i)) return 0;
            if (oj_index(doc, i, c) != 0xFFFFFFFFu) return 0;
        }
    }
    return oj_type(doc, n) == 0;
}

static void do_parse(oj_u32 flags, const char *path)
{
    unsigned len;
    unsigned char *text = load(path, &len);
    struct oj_error err;
    struct oj_doc *doc;
    oj_u32 size;
    if (!text) {
        printf("missing %s\n", path);
        return;
    }
    size = oj_doc_size(text, len, flags, &err);
    doc = oj_parse(&env, text, len, flags, &err);
    if (!doc) {
        if (size) printf("bad: size %u but no doc\n", size);
        printf("err %d %u %u %u\n", err.code, err.offset, err.line, err.column);
        free(text);
        return;
    }
    if (doc->size != size) printf("bad: size %u, doc %u\n", size, doc->size);
    if (oj_check_doc(doc, doc->size)) printf("bad: check_doc\n");
    {
        /* the block is position independent: a copy is the same document */
        struct oj_doc *copy = malloc(doc->size + 8);
        void *into = malloc(size);
        struct oj_doc *d2;
        memcpy(copy, doc, doc->size);
        if (oj_check_doc(copy, doc->size)) printf("bad: check_doc copy\n");
        if (!walk(copy)) printf("bad: walk\n");
        d2 = oj_parse_into(text, len, flags, into, size, &err);
        if (!d2 || memcmp((char *)d2 + 8, (char *)doc + 8, 16) ||
            memcmp((char *)d2 + 32, (char *)doc + 32, doc->size - 32))
            printf("bad: parse_into differs\n");
        if (oj_parse_into(text, len, flags, into, size - 1, &err) || err.code != 11)
            printf("bad: parse_into short\n");
        {
            oj_u32 n;
            char *s = oj_stringify(&env, copy, 0, 0, &n);
            if (!s) printf("bad: stringify\n");
            else {
                /* pretty, tabbed and ASCII-only forms read back as the same document */
                static const oj_u32 forms[3] = { OJ_W_PRETTY | OJ_W_ASCII, OJ_W_TABS, OJ_W_ASCII };
                int f;
                for (f = 0; f < 3; f++) {
                    oj_u32 m, k, m2;
                    char *alt = oj_stringify(&env, copy, 0, forms[f], &m), *back;
                    struct oj_doc *d3 = alt ? oj_parse(&env, (const oj_u8 *)alt, m, 0, &err) : NULL;
                    back = d3 ? oj_stringify(&env, d3, 0, 0, &m2) : NULL;
                    if (!back || m2 != n || memcmp(back, s, n)) printf("bad: form %u reads back differently\n", forms[f]);
                    if (alt && (forms[f] & OJ_W_ASCII))
                        for (k = 0; k < m; k++)
                            if ((unsigned char)alt[k] >= 0x80) {
                                printf("bad: non-ASCII in ASCII form\n");
                                break;
                            }
                    oj_free_text(&env, back);
                    oj_free_doc(&env, d3);
                    oj_free_text(&env, alt);
                }
                put_text(s, n);
                oj_free_text(&env, s);
            }
        }
        {
            /* damaged blocks: oj_check_doc refuses them, or they are safe
             * to walk and write (the sanitizers watch) */
            int k;
            for (k = 0; k < 24; k++) {
                memcpy(copy, doc, doc->size);
                ((unsigned char *)copy)[rnd() % doc->size] ^= (unsigned char)(1u << (rnd() % 8));
                if (k & 1) ((unsigned char *)copy)[8 + rnd() % (doc->size - 8)] = (unsigned char)rnd();
                if (oj_check_doc(copy, doc->size) == 0) {
                    oj_u32 m;
                    char *t = oj_stringify(&env, copy, 0, OJ_W_ASCII, &m);
                    walk(copy);
                    oj_free_text(&env, t);
                }
            }
        }
        free(into);
        free(copy);
    }
    oj_free_doc(&env, doc);
    free(text);
}

/* Events to the writer: the same compact text oj_stringify gives */
static void do_reader(oj_u32 flags, unsigned seed, const char *path)
{
    unsigned len, pos = 0;
    unsigned char *text = load(path, &len);
    struct oj_reader *r = oj_new_reader(&env, flags);
    struct oj_writer *w = oj_new_writer(&env, OJ_W_MULTI);
    int fed_end = 0;
    rng = 0x9E3779B97F4A7C15ull ^ seed;
    if (!text || !r || !w) {
        printf("missing %s\n", path);
        return;
    }
    for (;;) {
        oj_s32 ev = oj_reader_next(r);
        oj_u32 n;
        const char *t;
        if (ev == OJ_EV_MORE) {
            unsigned k, left = len - pos;
            if (!left) {
                if (fed_end) {
                    printf("bad: MORE after the end\n");
                    break;
                }
                oj_reader_feed(r, NULL, 0);
                fed_end = 1;
                continue;
            }
            k = seed == 0 ? left : 1 + rnd() % (rnd() % 4 ? 3 : 64);
            if (k > left) k = left;
            oj_reader_feed(r, text + pos, k);
            pos += k;
            continue;
        }
        if (ev == OJ_EV_ERROR) {
            struct oj_error e;
            oj_reader_error(r, &e);
            printf("err %d %u %u %u\n", e.code, e.offset, e.line, e.column);
            break;
        }
        if (ev == OJ_EV_DONE) {
            if (oj_reader_next(r) != OJ_EV_DONE) printf("bad: not DONE again\n");
            t = oj_writer_text(w, &n);
            if (!t) printf("ok\n");     /* JSONF_MULTI with no value */
            else put_text(t, n);
            break;
        }
        t = oj_reader_text(r, &n);
        switch (ev) {
        case OJ_EV_OBJECT: oj_write_begin(w, OJ_T_OBJECT); break;
        case OJ_EV_ARRAY: oj_write_begin(w, OJ_T_ARRAY); break;
        case OJ_EV_OBJECT_END:
        case OJ_EV_ARRAY_END: oj_write_end(w); break;
        case OJ_EV_KEY: oj_write_key(w, t, n); break;
        case OJ_EV_STRING: oj_write_string(w, t, n); break;
        case OJ_EV_NUMBER: {
            oj_s32 v;
            /* integers the parser keeps as INT come out as the writer's LONG */
            if (oj_text_to_long(t, n, &v) == 0 && !strchr(t, '.') && !strchr(t, 'e') && !strchr(t, 'E') &&
                strcmp(t, "-0"))
                oj_write_long(w, v);
            else
                oj_write_number_text(w, t, n);
            break;
        }
        case OJ_EV_TRUE: oj_write_bool(w, 1); break;
        case OJ_EV_FALSE: oj_write_bool(w, 0); break;
        case OJ_EV_NULL: oj_write_null(w); break;
        default: printf("bad: event %d\n", ev);
        }
        if (oj_writer_error(w)) {
            printf("bad: writer %d at event %d\n", oj_writer_error(w), ev);
            break;
        }
    }
    oj_free_writer(w);
    oj_free_reader(r);
    free(text);
}

static void do_num(const char *t)
{
    oj_u32 d[2];
    oj_s32 v;
    oj_s32 rc = oj_text_to_double(&env, t, 0xFFFFFFFFu, d);
    if (rc) {
        printf("num err %d\n", rc);
        return;
    }
    printf("num %08x%08x ", d[0], d[1]);
    if (oj_text_to_long(t, 0xFFFFFFFFu, &v) == 0) printf("%d\n", v);
    else printf("-\n");
}

static void do_fmt(const char *hex)
{
    oj_u32 d[2];
    char buf[32];
    unsigned long long b = strtoull(hex, NULL, 16);
    struct oj_writer *w = oj_new_writer(&env, 0);
    oj_u32 n;
    const char *t;
    d[0] = (oj_u32)(b >> 32);
    d[1] = (oj_u32)b;
    n = oj_double_to_text(&env, d, buf);
    oj_write_double(w, d);
    t = oj_writer_text(w, NULL);
    if (!t || strcmp(t, buf) || strlen(buf) != n) printf("bad: writer %s\n", t ? t : "(none)");
    printf("fmt %s\n", buf);
    oj_free_writer(w);
}

int main(int argc, char **argv)
{
    char line[4096];
    FILE *f;
    if (argc < 2 || !(f = fopen(argv[1], "r"))) {
        fprintf(stderr, "usage: json_test script\n");
        return 2;
    }
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        unsigned flags, seed;
        char path[2048];
        if (nl) *nl = 0;
        if (sscanf(line, "parse %u %2047s", &flags, path) == 2) do_parse(flags, path);
        else if (sscanf(line, "reader %u %u %2047s", &flags, &seed, path) == 3) do_reader(flags, seed, path);
        else if (!strncmp(line, "num ", 4)) do_num(line + 4);
        else if (!strncmp(line, "fmt ", 4)) do_fmt(line + 4);
        else if (line[0]) printf("bad: script line %s\n", line);
    }
    fclose(f);
    if (allocs) printf("bad: %lu allocations left\n", allocs);
    return 0;
}

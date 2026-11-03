/* A copy of DalsinAI/gfopentata's src/tata_ads.c (Tata 0.6.0, aeba9f0): change it there first. */
/* Tata's ad and tracker rules (tata_ads.h): a hash of host names, each with
 * what its lines say (block it, block some of its paths, pass it, allow its
 * pages), looked up for a name and each name it is under. Plain C, no Amiga
 * calls: GFOpenBrowser builds a copy of this file.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <string.h>
#include "tata_ads.h"

enum { BLOCK = 1, BLOCK_PATHS = 2, PASS = 4, ALLOW = 8 };

struct ads_path {
    struct ads_path *next;
    char path[1];               /* from its "/", as long as it is */
};

struct ads_entry {
    char *host;                 /* 0: a free slot */
    unsigned long hash;
    unsigned char kinds;
    struct ads_path *paths;
};

struct ads_chunk {
    struct ads_chunk *next;
    unsigned long used, size;
    char data[1];
};

struct tata_ads {
    tata_ads_alloc alloc;
    tata_ads_release release;
    struct ads_entry *table;
    unsigned long slots, used;  /* slots: a power of two */
    unsigned long blocks, passes, allows;
    struct ads_chunk *chunks;   /* the names and paths */
};

#define CHUNK 8192UL

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

/* FNV-1a over n bytes, without case */
static unsigned long hash_of(const char *s, unsigned long n)
{
    unsigned long h = 2166136261UL;
    while (n--) {
        h ^= (unsigned char)lower(*s++);
        h *= 16777619UL;
        h &= 0xffffffffUL;
    }
    return h;
}

/* the n bytes at a are the whole of b, without case */
static int same(const char *a, const char *b, unsigned long n)
{
    while (n--) {
        if (!*b || lower(*a++) != lower(*b++)) return 0;
    }
    return *b == 0;
}

static char *keep(tata_ads *ads, const char *s, unsigned long n)
{
    struct ads_chunk *c = ads->chunks;
    char *r;
    unsigned long i;
    if (!c || c->used + n + 1 > c->size) {
        unsigned long size = n + 1 > CHUNK ? n + 1 : CHUNK;
        if (!(c = ads->alloc(sizeof *c + size))) return 0;
        c->size = size;
        c->next = ads->chunks;
        ads->chunks = c;
    }
    r = c->data + c->used;
    for (i = 0; i < n; i++) r[i] = lower(s[i]);
    r[n] = 0;
    c->used += n + 1;
    return r;
}

static struct ads_entry *find(const tata_ads *ads, const char *host, unsigned long n, unsigned long h)
{
    unsigned long i;
    if (!ads->slots) return 0;
    for (i = h & (ads->slots - 1); ads->table[i].host; i = (i + 1) & (ads->slots - 1))
        if (ads->table[i].hash == h && same(host, ads->table[i].host, n)) return &ads->table[i];
    return 0;
}

static int grow(tata_ads *ads)
{
    unsigned long slots = ads->slots ? ads->slots * 2 : 1024, i, j;
    struct ads_entry *t = ads->alloc(slots * sizeof *t);
    if (!t) return 0;
    for (i = 0; i < ads->slots; i++) {
        if (!ads->table[i].host) continue;
        for (j = ads->table[i].hash & (slots - 1); t[j].host; j = (j + 1) & (slots - 1)) {
        }
        t[j] = ads->table[i];
    }
    if (ads->table) ads->release(ads->table);
    ads->table = t;
    ads->slots = slots;
    return 1;
}

static struct ads_entry *entry(tata_ads *ads, const char *host, unsigned long n)
{
    unsigned long h = hash_of(host, n), i;
    struct ads_entry *e = find(ads, host, n, h);
    if (e) return e;
    if ((ads->used + 1) * 2 > ads->slots && !grow(ads)) return 0;
    for (i = h & (ads->slots - 1); ads->table[i].host; i = (i + 1) & (ads->slots - 1)) {
    }
    e = &ads->table[i];
    if (!(e->host = keep(ads, host, n))) return 0;
    e->hash = h;
    ads->used++;
    return e;
}

tata_ads *tata_ads_new(tata_ads_alloc alloc, tata_ads_release release)
{
    tata_ads *ads = alloc(sizeof *ads);
    if (!ads) return 0;
    ads->alloc = alloc;
    ads->release = release;
    if (!grow(ads)) {
        release(ads);
        return 0;
    }
    return ads;
}

void tata_ads_free(tata_ads *ads)
{
    struct ads_chunk *c, *next;
    if (!ads) return;
    for (c = ads->chunks; c; c = next) {
        next = c->next;
        ads->release(c);
    }
    if (ads->table) ads->release(ads->table);
    ads->release(ads);
}

void tata_ads_counts(const tata_ads *ads, unsigned long *block, unsigned long *pass, unsigned long *allow)
{
    if (block) *block = ads ? ads->blocks : 0;
    if (pass) *pass = ads ? ads->passes : 0;
    if (allow) *allow = ads ? ads->allows : 0;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
}

/* One line: its verb (BLOCK, PASS, ALLOW, 0 for none), its host and the
 * path, if any (pointers into line, with lengths). Returns 0 or why not. */
static const char *parse(const char *line, unsigned long len, int *verb, const char **host, unsigned long *hn, const char **path,
                         unsigned long *pn)
{
    const char *p = line, *end = line + len, *w;
    unsigned long wn;
    *verb = 0;
    *path = 0;
    *pn = 0;
    while (p < end && is_space(*p)) p++;
    if (p == end || *p == '#') return 0;
    for (w = p; p < end && !is_space(*p); p++) {
    }
    wn = (unsigned long)(p - w);
    if (wn == 5 && same(w, "block", 5)) *verb = BLOCK;
    else if (wn == 4 && same(w, "pass", 4)) *verb = PASS;
    else if (wn == 5 && same(w, "allow", 5)) *verb = ALLOW;
    else return "A rule starts with block, pass or allow.";
    while (p < end && is_space(*p)) p++;
    for (*host = p; p < end && host_char(*p); p++) {
    }
    *hn = (unsigned long)(p - *host);
    while (*hn && (*host)[*hn - 1] == '.') (*hn)--;
    if (!*hn || **host == '.') return "A rule names a host, such as doubleclick.net.";
    if (p < end && *p == '/') {
        if (*verb != BLOCK) return "Only a block rule can have a path.";
        for (*path = p; p < end && !is_space(*p); p++) {
        }
        *pn = (unsigned long)(p - *path);
    }
    while (p < end && is_space(*p)) p++;
    if (p < end && *p != '#') return "A rule is a word and a host, with nothing after them.";
    return 0;
}

const char *tata_ads_check(const char *line)
{
    int verb;
    const char *host, *path, *why;
    unsigned long hn, pn, len = (unsigned long)strlen(line);
    if (len > TATA_ADS_MAX_LINE) return "The rule is too long.";
    while (len && (line[len - 1] == '\n' || is_space(line[len - 1]))) len--;
    if ((why = parse(line, len, &verb, &host, &hn, &path, &pn))) return why;
    return verb ? 0 : "The line has no rule.";
}

long tata_ads_add_text(tata_ads *ads, const char *text, long len)
{
    const char *p = text, *end = text + (len < 0 ? 0 : len);
    long added = 0;
    while (p < end) {
        const char *nl = memchr(p, '\n', (unsigned long)(end - p)), *host, *path;
        unsigned long n = nl ? (unsigned long)(nl - p) : (unsigned long)(end - p), hn, pn;
        int verb;
        struct ads_entry *e;
        if (n <= TATA_ADS_MAX_LINE && !parse(p, n, &verb, &host, &hn, &path, &pn) && verb) {
            if (!(e = entry(ads, host, hn))) return -1;
            if (verb == BLOCK && path && pn > 1) {
                struct ads_path *ap = ads->alloc(sizeof *ap + pn);
                if (!ap) return -1;
                memcpy(ap->path, path, pn);
                ap->path[pn] = 0;
                ap->next = e->paths;
                e->paths = ap;
                if (!(e->kinds & BLOCK_PATHS)) e->kinds |= BLOCK_PATHS;
                ads->blocks++;
            } else {
                if (!(e->kinds & verb)) {
                    if (verb == BLOCK) ads->blocks++;
                    else if (verb == PASS) ads->passes++;
                    else ads->allows++;
                }
                e->kinds |= (unsigned char)verb;
            }
            added++;
        }
        p += n + 1;
    }
    return added;
}

/* The entry for host or the nearest name it is under that has kind. */
static const struct ads_entry *under(const tata_ads *ads, const char *host, int kind)
{
    const char *h = host;
    while (h && *h) {
        unsigned long n = (unsigned long)strlen(h);
        const struct ads_entry *e = find(ads, h, n, hash_of(h, n));
        if (e && (e->kinds & kind)) return e;
        h = strchr(h, '.');
        if (h) h++;
    }
    return 0;
}

int tata_ads_allowed(const tata_ads *ads, const char *site)
{
    return ads && site && *site && under(ads, site, ALLOW) != 0;
}

int tata_ads_blocked(const tata_ads *ads, const char *host, const char *path, const char *site)
{
    const char *h;
    if (!ads || !host || !*host) return 0;
    if (site && *site && under(ads, site, ALLOW)) return 0;
    if (under(ads, host, PASS)) return 0;
    if (under(ads, host, BLOCK)) return 1;
    if (!path || !*path) path = "/";
    for (h = host; h && *h;) {
        unsigned long n = (unsigned long)strlen(h);
        const struct ads_entry *e = find(ads, h, n, hash_of(h, n));
        if (e && (e->kinds & BLOCK_PATHS)) {
            const struct ads_path *ap;
            for (ap = e->paths; ap; ap = ap->next)
                if (!strncmp(path, ap->path, strlen(ap->path))) return 1;
        }
        h = strchr(h, '.');
        if (h) h++;
    }
    return 0;
}

long tata_ads_edit(const char *text, long len, const char *verb, const char *target, char *out, long size)
{
    const char *p = text, *end = text + (len < 0 ? 0 : len), *thost, *tpath;
    unsigned long thn, tpn;
    long o = 0;
    int tverb;
    char rule[TATA_ADS_MAX_LINE + 8];
    if (strlen(target) > TATA_ADS_MAX_LINE - 8) return -1;
    strcpy(rule, verb ? verb : "block");
    strcat(rule, " ");
    strcat(rule, target);
    if (parse(rule, (unsigned long)strlen(rule), &tverb, &thost, &thn, &tpath, &tpn) || !tverb) return -1;
    while (p < end) {
        const char *nl = memchr(p, '\n', (unsigned long)(end - p)), *host, *path;
        unsigned long n = nl ? (unsigned long)(nl - p) : (unsigned long)(end - p), hn, pn, i;
        int lverb, drop = 0;
        /* a line about the same host and path, whatever its verb, goes */
        if (!parse(p, n, &lverb, &host, &hn, &path, &pn) && lverb && hn == thn && pn == tpn && (!pn || !strncmp(path, tpath, pn))) {
            drop = 1;
            for (i = 0; i < hn; i++)
                if (lower(host[i]) != lower(thost[i])) drop = 0;
        }
        if (!drop) {
            if (o + (long)n + 1 > size) return -1;
            memcpy(out + o, p, n);
            o += (long)n;
            out[o++] = '\n';
        }
        p += n + 1;
    }
    if (verb) {
        long rn = (long)strlen(rule);
        if (o + rn + 1 > size) return -1;
        memcpy(out + o, rule, (unsigned long)rn);
        o += rn;
        out[o++] = '\n';
    }
    return o;
}

/*
 * OpenBrowser: the engine's HTTP disk cache (see ob_httpcache.h).
 *
 * The folder holds one file per entry, named by the 64-bit FNV-1a hash of
 * its address in 16 hexadecimal digits:
 *
 *   OBHC1
 *   <address>
 *   <stored> <fresh until>      (seconds since 1970, ten digits each)
 *   <ETag, or an empty line>
 *   <Last-Modified, or an empty line>
 *   <number of headers>
 *   <Name: value>...
 *   <empty line>
 *   <the body>
 *
 * and "index": the size and the last use of every entry, written when the
 * cache closes and removed when it opens. Without it (the first start, or
 * after a crash) the folder is read once instead. Entries are written as
 * "<name>.<n>.tmp" and renamed when complete; a .tmp file found at the start
 * is what a crash left, and goes.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "ob_httpcache.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ENTRY_MAGIC "OBHC1"
#define INDEX_MAGIC "OBHCI1"
#define INDEX_NAME "index"
#define MAX_LINE 65536                      /* an address or a header */
#define MAX_HEADERS 256
#define MAX_LIFETIME (365L * 24 * 60 * 60)

struct item {
    unsigned long hi, lo;                   /* the address's hash */
    unsigned long size;                     /* bytes on the disk */
    long used;                              /* last use, seconds since 1970 */
};

struct ob_httpcache {
    char *dir;
    unsigned long maxBytes, maxEntry, bytes;
    struct item *items;
    unsigned count, capacity;
    unsigned long serial;
};

struct ob_httpcache_entry {
    FILE *file;
    char *path;
    long timesAt;                           /* where the times line starts */
    long stored, freshUntil;
    char *etag, *lastModified;
    char **headers;
    unsigned headerCount;
    unsigned long bodySize, bodyLeft;
};

struct ob_httpcache_writer {
    FILE *file;
    unsigned long hi, lo;
    char *temp;
    unsigned long size;
    unsigned long maxSize;
    int failed;
};

/* --- small helpers ---------------------------------------------------------- */

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int isSpace(int c)
{
    return c == ' ' || c == '\t';
}

static int isDigit(int c)
{
    return c >= '0' && c <= '9';
}

/* The n bytes at s are name (lower case), ignoring ASCII case. */
static int sameToken(const char *s, size_t n, const char *name)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!name[i] || lower((unsigned char)s[i]) != name[i])
            return 0;
    return name[n] == 0;
}

static char *copyString(const char *s, size_t n)
{
    char *copy = malloc(n + 1);
    if (copy) {
        memcpy(copy, s, n);
        copy[n] = 0;
    }
    return copy;
}

static void hashURL(const char *url, unsigned long *hi, unsigned long *lo)
{
    unsigned long long h = 0xcbf29ce484222325ULL;
    while (*url) {
        h ^= (unsigned char)*url++;
        h *= 0x100000001b3ULL;
    }
    *hi = (unsigned long)(h >> 32) & 0xffffffffUL;
    *lo = (unsigned long)h & 0xffffffffUL;
}

static void entryName(unsigned long hi, unsigned long lo, char name[17])
{
    static const char digits[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 8; i++) {
        name[i] = digits[(hi >> (28 - 4 * i)) & 15];
        name[8 + i] = digits[(lo >> (28 - 4 * i)) & 15];
    }
    name[16] = 0;
}

/* name is an entry's: 16 lower-case hexadecimal digits. */
static int parseEntryName(const char *name, unsigned long *hi, unsigned long *lo)
{
    int i;
    *hi = *lo = 0;
    for (i = 0; i < 16; i++) {
        int c = (unsigned char)name[i], v;
        if (isDigit(c))
            v = c - '0';
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else
            return 0;
        if (i < 8)
            *hi = (*hi << 4) | (unsigned long)v;
        else
            *lo = (*lo << 4) | (unsigned long)v;
    }
    return name[16] == 0;
}

/* dir and name joined: no separator after a volume's colon or a slash. */
static char *joinPath(const char *dir, const char *name)
{
    size_t d = strlen(dir), n = strlen(name);
    int slash = d && dir[d - 1] != ':' && dir[d - 1] != '/';
    char *path = malloc(d + slash + n + 1);
    if (path) {
        memcpy(path, dir, d);
        if (slash)
            path[d] = '/';
        memcpy(path + d + slash, name, n + 1);
    }
    return path;
}

static char *itemPath(const ob_httpcache *cache, unsigned long hi, unsigned long lo)
{
    char name[17];
    entryName(hi, lo, name);
    return joinPath(cache->dir, name);
}

/* One line without its end (\n, or \r\n), in *line (grown as needed). 0 at
 * the end of the file, or for a line longer than MAX_LINE. */
static int readLine(FILE *f, char **line, size_t *capacity)
{
    size_t n = 0;
    int c;
    while ((c = getc(f)) != EOF && c != '\n') {
        if (n + 1 >= *capacity) {
            size_t more = *capacity ? *capacity * 2 : 256;
            char *grown;
            if (more > MAX_LINE + 1)
                return 0;
            grown = realloc(*line, more);
            if (!grown)
                return 0;
            *line = grown;
            *capacity = more;
        }
        (*line)[n++] = (char)c;
    }
    if (c == EOF && !n)
        return 0;
    if (!*line && !(*line = malloc(*capacity = 256)))
        return 0;
    if (n && (*line)[n - 1] == '\r')
        n--;
    (*line)[n] = 0;
    return 1;
}

/* --- the index ---------------------------------------------------------------- */

static struct item *findItem(ob_httpcache *cache, unsigned long hi, unsigned long lo)
{
    unsigned i;
    for (i = 0; i < cache->count; i++)
        if (cache->items[i].hi == hi && cache->items[i].lo == lo)
            return &cache->items[i];
    return NULL;
}

static struct item *addItem(ob_httpcache *cache, unsigned long hi, unsigned long lo)
{
    struct item *item = findItem(cache, hi, lo);
    if (item)
        return item;
    if (cache->count == cache->capacity) {
        unsigned more = cache->capacity ? cache->capacity * 2 : 64;
        struct item *grown = realloc(cache->items, more * sizeof *grown);
        if (!grown)
            return NULL;
        cache->items = grown;
        cache->capacity = more;
    }
    item = &cache->items[cache->count++];
    item->hi = hi;
    item->lo = lo;
    item->size = 0;
    item->used = 0;
    return item;
}

static void dropItem(ob_httpcache *cache, struct item *item)
{
    cache->bytes -= item->size;
    *item = cache->items[--cache->count];
}

/* Removes the item's file and forgets it. */
static void evictItem(ob_httpcache *cache, struct item *item)
{
    char *path = itemPath(cache, item->hi, item->lo);
    if (path) {
        remove(path);
        free(path);
    }
    dropItem(cache, item);
}

/* Over the limit: the entries used longest ago go, all but keep. */
static void evict(ob_httpcache *cache, const struct item *keep)
{
    unsigned long keepHi = keep ? keep->hi : 0, keepLo = keep ? keep->lo : 0;
    while (cache->bytes > cache->maxBytes) {
        struct item *oldest = NULL;
        unsigned i;
        for (i = 0; i < cache->count; i++) {
            struct item *item = &cache->items[i];
            if (keep && item->hi == keepHi && item->lo == keepLo)
                continue;
            if (!oldest || item->used < oldest->used)
                oldest = item;
        }
        if (!oldest)
            break;
        evictItem(cache, oldest);
    }
}

static int readIndex(ob_httpcache *cache, const char *path)
{
    FILE *f = fopen(path, "rb");
    char line[64];
    int ok = 0;
    if (!f)
        return 0;
    if (fgets(line, sizeof line, f) && !strcmp(line, INDEX_MAGIC "\n")) {
        ok = 1;
        while (fgets(line, sizeof line, f)) {
            unsigned long hi, lo, size;
            long used;
            char name[17];
            struct item *item;
            if (sscanf(line, "%16s %lu %ld", name, &size, &used) != 3 || !parseEntryName(name, &hi, &lo)) {
                ok = 0;
                break;
            }
            if (!(item = addItem(cache, hi, lo))) {
                ok = 0;
                break;
            }
            cache->bytes -= item->size;
            item->size = size;
            item->used = used;
            cache->bytes += size;
        }
    }
    fclose(f);
    if (!ok) {
        cache->count = 0;
        cache->bytes = 0;
    }
    return ok;
}

static void writeIndex(ob_httpcache *cache)
{
    char *path = joinPath(cache->dir, INDEX_NAME), *temp = joinPath(cache->dir, INDEX_NAME ".tmp");
    FILE *f = path && temp ? fopen(temp, "wb") : NULL;
    if (f) {
        unsigned i;
        int ok = fputs(INDEX_MAGIC "\n", f) >= 0;
        for (i = 0; ok && i < cache->count; i++) {
            char name[17];
            entryName(cache->items[i].hi, cache->items[i].lo, name);
            ok = fprintf(f, "%s %lu %ld\n", name, cache->items[i].size, cache->items[i].used) > 0;
        }
        if (fclose(f))
            ok = 0;
        /* AmigaDOS renames onto no existing file: remove it first. */
        if (ok && rename(temp, path)) {
            remove(path);
            ok = !rename(temp, path);
        }
        if (!ok)
            remove(temp);
    }
    free(path);
    free(temp);
}

/* A temporary file the cache wrote: "index.tmp", or an entry's
 * "<16 hex digits>.<serial>.tmp". Nothing else in the folder is touched. */
static int isCacheTemp(const char *name, size_t length)
{
    char entry[17];
    unsigned long hi, lo;
    size_t i;
    if (!strcmp(name, INDEX_NAME ".tmp"))
        return 1;
    if (length < 16 + 1 + 1 + 4 || length > 16 + 1 + 5 + 4 || name[16] != '.' || strcmp(name + length - 4, ".tmp"))
        return 0;
    for (i = 17; i < length - 4; i++)
        if (!isDigit((unsigned char)name[i]))
            return 0;
    memcpy(entry, name, 16);
    entry[16] = 0;
    return parseEntryName(entry, &hi, &lo);
}

/* No index: every entry's size and date from the folder itself. */
static void readFolder(ob_httpcache *cache)
{
    DIR *d = opendir(cache->dir);
    struct dirent *e;
    char **leftovers = NULL;
    unsigned leftoverCount = 0, i;
    if (!d)
        return;
    while ((e = readdir(d))) {
        const char *name = e->d_name;
        size_t length = strlen(name);
        unsigned long hi, lo;
        if (isCacheTemp(name, length)) {
            /* removed after the listing: AmigaDOS lists badly while a folder changes */
            char **grown = realloc(leftovers, (leftoverCount + 1) * sizeof *grown);
            if (grown) {
                leftovers = grown;
                if ((leftovers[leftoverCount] = joinPath(cache->dir, name)))
                    leftoverCount++;
            }
        } else if (parseEntryName(name, &hi, &lo)) {
            char *path = joinPath(cache->dir, name);
            struct stat st;
            struct item *item;
            if (path && !stat(path, &st) && (item = addItem(cache, hi, lo))) {
                cache->bytes -= item->size;
                item->size = (unsigned long)st.st_size;
                item->used = (long)st.st_mtime;
                cache->bytes += item->size;
            }
            free(path);
        }
    }
    closedir(d);
    for (i = 0; i < leftoverCount; i++) {
        remove(leftovers[i]);
        free(leftovers[i]);
    }
    free(leftovers);
}

/* --- opening and closing ---------------------------------------------------- */

ob_httpcache *ob_httpcache_open(const char *dir, unsigned long maxBytes)
{
    ob_httpcache *cache;
    char *index;
    struct stat st;
    if (!dir || !*dir || !maxBytes)
        return NULL;
    if (stat(dir, &st)) {
        mkdir(dir, 0755);
        if (stat(dir, &st))
            return NULL;
    }
    if (!S_ISDIR(st.st_mode))
        return NULL;
    cache = calloc(1, sizeof *cache);
    if (!cache || !(cache->dir = copyString(dir, strlen(dir)))) {
        free(cache);
        return NULL;
    }
    cache->maxBytes = maxBytes;
    cache->maxEntry = maxBytes / 8;
    index = joinPath(dir, INDEX_NAME);
    if (index && readIndex(cache, index))
        remove(index);              /* written again at close; missing means a crash */
    else
        readFolder(cache);
    free(index);
    evict(cache, NULL);
    return cache;
}

ob_httpcache *ob_httpcache_open_from_env(const char *defaultDir)
{
    const char *value = getenv("OB_HTTP_CACHE");
    unsigned long megabytes = OB_HTTPCACHE_DEFAULT_MB;
    char *dir;
    ob_httpcache *cache;
    /* each value is used before the next getenv(), which may reuse its buffer */
    if (value && !strcmp(value, "0"))
        return NULL;
    value = getenv("OB_HTTP_CACHE_DIR");
    if (!value || !*value)
        value = defaultDir;
    if (!value || !(dir = copyString(value, strlen(value))))
        return NULL;
    value = getenv("OB_HTTP_CACHE_MB");
    if (value && *value) {
        char *end;
        unsigned long n = strtoul(value, &end, 10);
        if (!*end && n)
            megabytes = n < 4095 ? n : 4095;
    }
    cache = ob_httpcache_open(dir, megabytes * 1024UL * 1024UL);
    free(dir);
    return cache;
}

void ob_httpcache_close(ob_httpcache *cache)
{
    if (!cache)
        return;
    writeIndex(cache);
    free(cache->items);
    free(cache->dir);
    free(cache);
}

unsigned long ob_httpcache_bytes(const ob_httpcache *cache)
{
    return cache ? cache->bytes : 0;
}

unsigned ob_httpcache_count(const ob_httpcache *cache)
{
    return cache ? cache->count : 0;
}

/* --- what HTTP allows ------------------------------------------------------------ */

/* Looks for a directive (a Cache-Control or Pragma value: "a, b=1, c="x"").
 * With number, its value as seconds: -1 if it has none or it is not a
 * number. */
static int findDirective(const char *value, const char *name, long *number)
{
    if (!value)
        return 0;
    while (*value) {
        const char *start, *argument = NULL;
        size_t length, argumentLength = 0;
        while (isSpace((unsigned char)*value) || *value == ',')
            value++;
        start = value;
        while (*value && *value != ',' && *value != '=' && !isSpace((unsigned char)*value))
            value++;
        length = (size_t)(value - start);
        while (isSpace((unsigned char)*value))
            value++;
        if (*value == '=') {
            value++;
            while (isSpace((unsigned char)*value))
                value++;
            if (*value == '"') {
                argument = ++value;
                while (*value && *value != '"')
                    value++;
                argumentLength = (size_t)(value - argument);
                if (*value)
                    value++;
            } else {
                argument = value;
                while (*value && *value != ',' && !isSpace((unsigned char)*value))
                    value++;
                argumentLength = (size_t)(value - argument);
            }
        }
        while (*value && *value != ',')
            value++;
        if (length && sameToken(start, length, name)) {
            if (number) {
                long n = 0;
                size_t j;
                *number = -1;
                if (argument && argumentLength) {
                    for (j = 0; j < argumentLength && isDigit((unsigned char)argument[j]); j++)
                        n = n > MAX_LIFETIME ? n : n * 10 + (argument[j] - '0');
                    if (j == argumentLength)
                        *number = n;
                }
            }
            return 1;
        }
    }
    return 0;
}

/* Vary names no request header but Accept-Encoding (openweb.fetch sends the
 * same one each time and decodes the body). */
static int varyIsHarmless(const char *vary)
{
    while (*vary) {
        const char *start;
        while (isSpace((unsigned char)*vary) || *vary == ',')
            vary++;
        start = vary;
        while (*vary && *vary != ',' && !isSpace((unsigned char)*vary))
            vary++;
        if (vary > start && !sameToken(start, (size_t)(vary - start), "accept-encoding"))
            return 0;
    }
    return 1;
}

int ob_httpcache_request_policy(const struct ob_httpcache_request *request)
{
    long maxAge;
    if (!request->method || strcmp(request->method, "GET"))
        return OB_HTTPCACHE_BYPASS;
    if (request->hasAuthorization || request->hasConditional)
        return OB_HTTPCACHE_BYPASS;
    if (findDirective(request->cacheControl, "no-store", NULL))
        return OB_HTTPCACHE_BYPASS;
    if (findDirective(request->cacheControl, "no-cache", NULL)
        || (findDirective(request->cacheControl, "max-age", &maxAge) && maxAge == 0)
        || findDirective(request->pragma, "no-cache", NULL))
        return OB_HTTPCACHE_REVALIDATE;
    return OB_HTTPCACHE_USE;
}

long ob_httpcache_request_max_age(const struct ob_httpcache_request *request)
{
    long maxAge;
    if (findDirective(request->cacheControl, "max-age", &maxAge) && maxAge >= 0)
        return maxAge;
    return -1;
}

long ob_httpcache_lifetime(const struct ob_httpcache_response *response, time_t now)
{
    int validator = (response->etag && *response->etag) || (response->lastModified && *response->lastModified);
    const char *cacheControl = response->cacheControl && *response->cacheControl ? response->cacheControl : NULL;
    long lifetime, maxAge;
    if (response->status != 200)
        return -1;
    if (response->vary && !varyIsHarmless(response->vary))
        return -1;
    if (findDirective(cacheControl, "no-store", NULL))
        return -1;
    if (findDirective(cacheControl, "no-cache", NULL))
        lifetime = 0;
    else if (findDirective(cacheControl, "max-age", &maxAge))
        lifetime = maxAge > 0 ? maxAge : 0;     /* s-maxage is for shared caches */
    else if (!cacheControl && findDirective(response->pragma, "no-cache", NULL))
        lifetime = 0;
    else if (response->expires) {
        long expires = ob_httpcache_parse_date(response->expires);
        long date = response->date ? ob_httpcache_parse_date(response->date) : -1;
        /* an Expires that is not a date ("0", "-1") means already expired */
        lifetime = expires < 0 ? 0 : expires - (date >= 0 ? date : (long)now);
    } else
        lifetime = 0;                           /* no heuristic freshness */
    if (lifetime > 0 && response->age) {
        char *end;
        long age = strtol(response->age, &end, 10);
        if (end != response->age && age > 0)
            lifetime = age < lifetime ? lifetime - age : 0;
    }
    if (lifetime < 0)
        lifetime = 0;
    if (lifetime > MAX_LIFETIME)
        lifetime = MAX_LIFETIME;
    if (!lifetime && !validator)
        return -1;                              /* it could never be used */
    return lifetime;
}

static const char *parseNumber(const char *s, int minDigits, int maxDigits, long *value)
{
    int n = 0;
    *value = 0;
    while (n < maxDigits && isDigit((unsigned char)*s)) {
        *value = *value * 10 + (*s++ - '0');
        n++;
    }
    return n >= minDigits ? s : NULL;
}

static const char *parseMonth(const char *s, long *month)
{
    static const char months[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    int i;
    for (i = 0; i < 12; i++)
        if (lower((unsigned char)s[0]) == months[3 * i] && lower((unsigned char)s[1]) == months[3 * i + 1]
            && lower((unsigned char)s[2]) == months[3 * i + 2]) {
            *month = i + 1;
            return s + 3;
        }
    return NULL;
}

static const char *parseClock(const char *s, long *seconds)
{
    long h, m, sec;
    if (!(s = parseNumber(s, 2, 2, &h)) || *s++ != ':' || !(s = parseNumber(s, 2, 2, &m)) || *s++ != ':'
        || !(s = parseNumber(s, 2, 2, &sec)) || h > 23 || m > 59 || sec > 60)
        return NULL;
    *seconds = h * 3600 + m * 60 + sec;
    return s;
}

/* Days from 1 January 1970 to the date (proleptic Gregorian). */
static long daysFromCivil(long y, long m, long d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

long ob_httpcache_parse_date(const char *s)
{
    long day, month, year, seconds, days;
    if (!s)
        return -1;
    while (isSpace((unsigned char)*s))
        s++;
    while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))
        s++;                                    /* the weekday */
    if (*s == ',')
        s++;
    while (isSpace((unsigned char)*s))
        s++;
    if (isDigit((unsigned char)*s)) {
        /* "06 Nov 1994 08:49:37 GMT" or "06-Nov-94 08:49:37 GMT" */
        char separator;
        if (!(s = parseNumber(s, 1, 2, &day)))
            return -1;
        separator = *s;
        if ((separator != ' ' && separator != '-') || !(s = parseMonth(s + 1, &month)) || *s++ != separator)
            return -1;
        if (!(s = parseNumber(s, 2, 4, &year)) || *s++ != ' ' || !(s = parseClock(s, &seconds)))
            return -1;
        if (year < 100)
            year += year < 70 ? 2000 : 1900;
    } else {
        /* asctime: "Nov  6 08:49:37 1994" */
        if (!(s = parseMonth(s, &month)))
            return -1;
        while (isSpace((unsigned char)*s))
            s++;
        if (!(s = parseNumber(s, 1, 2, &day)) || *s++ != ' ' || !(s = parseClock(s, &seconds)) || *s++ != ' '
            || !(s = parseNumber(s, 4, 4, &year)))
            return -1;
    }
    while (isSpace((unsigned char)*s))
        s++;
    if (*s && strcmp(s, "GMT") && strcmp(s, "UTC"))
        return -1;
    if (day < 1 || day > 31 || year < 1970 || year > 2105)
        return -1;
    days = daysFromCivil(year, month, day);
    if (days > 0x7fffffffL / 86400 - 1)
        return -1;
    return days * 86400 + seconds;
}

/* --- reading entries ------------------------------------------------------------- */

void ob_httpcache_entry_free(ob_httpcache_entry *entry)
{
    unsigned i;
    if (!entry)
        return;
    if (entry->file)
        fclose(entry->file);
    for (i = 0; i < entry->headerCount; i++)
        free(entry->headers[i]);
    free(entry->headers);
    free(entry->etag);
    free(entry->lastModified);
    free(entry->path);
    free(entry);
}

/* Reads an entry's file up to its body; 0 if it is not url's, or damaged. */
static int readEntry(ob_httpcache_entry *entry, const char *url)
{
    char *line = NULL, *end;
    size_t capacity = 0;
    unsigned long count, i;
    long bodyAt, fileSize;
    int ok = 0;
    FILE *f = entry->file;
    if (!readLine(f, &line, &capacity) || strcmp(line, ENTRY_MAGIC))
        goto done;
    if (!readLine(f, &line, &capacity) || strcmp(line, url))
        goto done;
    entry->timesAt = ftell(f);
    if (!readLine(f, &line, &capacity) || sscanf(line, "%ld %ld", &entry->stored, &entry->freshUntil) != 2)
        goto done;
    if (!readLine(f, &line, &capacity) || !(entry->etag = copyString(line, strlen(line))))
        goto done;
    if (!readLine(f, &line, &capacity) || !(entry->lastModified = copyString(line, strlen(line))))
        goto done;
    if (!readLine(f, &line, &capacity))
        goto done;
    count = strtoul(line, &end, 10);
    if (*end || count > MAX_HEADERS || (count && !(entry->headers = calloc(count, sizeof *entry->headers))))
        goto done;
    for (i = 0; i < count; i++) {
        if (!readLine(f, &line, &capacity) || !(entry->headers[i] = copyString(line, strlen(line))))
            goto done;
        entry->headerCount++;
    }
    if (!readLine(f, &line, &capacity) || *line)
        goto done;
    bodyAt = ftell(f);
    if (bodyAt < 0 || fseek(f, 0, SEEK_END) || (fileSize = ftell(f)) < bodyAt || fseek(f, bodyAt, SEEK_SET))
        goto done;
    entry->bodySize = entry->bodyLeft = (unsigned long)(fileSize - bodyAt);
    ok = 1;
done:
    free(line);
    return ok;
}

ob_httpcache_entry *ob_httpcache_lookup(ob_httpcache *cache, const char *url, time_t now)
{
    unsigned long hi, lo;
    struct item *item;
    ob_httpcache_entry *entry;
    if (!cache || !url)
        return NULL;
    hashURL(url, &hi, &lo);
    if (!(item = findItem(cache, hi, lo)))
        return NULL;
    entry = calloc(1, sizeof *entry);
    if (!entry || !(entry->path = itemPath(cache, hi, lo))) {
        free(entry);
        return NULL;
    }
    entry->file = fopen(entry->path, "rb");
    if (!entry->file) {
        dropItem(cache, item);                  /* removed behind our back */
        ob_httpcache_entry_free(entry);
        return NULL;
    }
    if (!readEntry(entry, url)) {
        /* another address with the same hash, or a damaged file: either
         * way the next store for this name replaces it */
        ob_httpcache_entry_free(entry);
        return NULL;
    }
    item->used = (long)now;
    return entry;
}

int ob_httpcache_entry_fresh(const ob_httpcache_entry *entry, time_t now)
{
    return (long)now < entry->freshUntil;
}

long ob_httpcache_entry_age(const ob_httpcache_entry *entry, time_t now)
{
    return (long)now > entry->stored ? (long)now - entry->stored : 0;
}

const char *ob_httpcache_entry_etag(const ob_httpcache_entry *entry)
{
    return entry->etag;
}

const char *ob_httpcache_entry_last_modified(const ob_httpcache_entry *entry)
{
    return entry->lastModified;
}

unsigned ob_httpcache_entry_header_count(const ob_httpcache_entry *entry)
{
    return entry->headerCount;
}

const char *ob_httpcache_entry_header(const ob_httpcache_entry *entry, unsigned i)
{
    return i < entry->headerCount ? entry->headers[i] : NULL;
}

unsigned long ob_httpcache_entry_body_size(const ob_httpcache_entry *entry)
{
    return entry->bodySize;
}

long ob_httpcache_entry_read(ob_httpcache_entry *entry, void *buffer, unsigned long size)
{
    size_t n;
    if (size > entry->bodyLeft)
        size = entry->bodyLeft;
    if (!size)
        return 0;
    n = fread(buffer, 1, size, entry->file);
    if (n != size)
        return -1;
    entry->bodyLeft -= n;
    return (long)n;
}

int ob_httpcache_entry_refresh(ob_httpcache *cache, ob_httpcache_entry *entry, long lifetime, time_t now)
{
    FILE *f;
    int ok;
    (void)cache;
    if (lifetime < 0)
        lifetime = 0;
    f = fopen(entry->path, "r+b");
    if (!f)
        return 0;
    /* the times line has a fixed width, so it is rewritten in place */
    ok = !fseek(f, entry->timesAt, SEEK_SET) && fprintf(f, "%010ld %010ld", (long)now, (long)now + lifetime) == 21;
    if (fclose(f))
        ok = 0;
    if (ok) {
        entry->stored = (long)now;
        entry->freshUntil = (long)now + lifetime;
    }
    return ok;
}

/* --- storing ------------------------------------------------------------------------- */

static int hasLineBreak(const char *s)
{
    return s && (strchr(s, '\n') || strchr(s, '\r'));
}

ob_httpcache_writer *ob_httpcache_store_begin(ob_httpcache *cache, const char *url, long lifetime, time_t now,
    const char *etag, const char *lastModified, const char *const *headers, unsigned headerCount)
{
    ob_httpcache_writer *writer;
    char name[40];
    unsigned i;
    int ok;
    if (!cache || !url || !*url || lifetime < 0 || headerCount > MAX_HEADERS || strlen(url) > MAX_LINE)
        return NULL;
    if (hasLineBreak(url) || hasLineBreak(etag) || hasLineBreak(lastModified))
        return NULL;
    for (i = 0; i < headerCount; i++)
        if (!headers[i] || hasLineBreak(headers[i]) || strlen(headers[i]) > MAX_LINE)
            return NULL;
    writer = calloc(1, sizeof *writer);
    if (!writer)
        return NULL;
    hashURL(url, &writer->hi, &writer->lo);
    entryName(writer->hi, writer->lo, name);
    sprintf(name + 16, ".%lu.tmp", ++cache->serial % 100000UL);
    writer->temp = joinPath(cache->dir, name);
    writer->maxSize = cache->maxEntry;
    writer->file = writer->temp ? fopen(writer->temp, "wb") : NULL;
    if (!writer->file) {
        free(writer->temp);
        free(writer);
        return NULL;
    }
    ok = fprintf(writer->file, ENTRY_MAGIC "\n%s\n%010ld %010ld\n%s\n%s\n%u\n", url, (long)now, (long)now + lifetime,
        etag ? etag : "", lastModified ? lastModified : "", headerCount) > 0;
    for (i = 0; ok && i < headerCount; i++)
        ok = fprintf(writer->file, "%s\n", headers[i]) > 0;
    ok = ok && fputc('\n', writer->file) != EOF;
    if (!ok) {
        ob_httpcache_store_abort(cache, writer);
        return NULL;
    }
    writer->size = (unsigned long)ftell(writer->file);
    if (writer->size > writer->maxSize) {
        /* the headers alone are over the entry limit */
        ob_httpcache_store_abort(cache, writer);
        return NULL;
    }
    return writer;
}

int ob_httpcache_store_write(ob_httpcache_writer *writer, const void *data, unsigned long size)
{
    if (!writer || writer->failed)
        return 0;
    if (size > writer->maxSize || writer->size > writer->maxSize - size
        || fwrite(data, 1, size, writer->file) != size) {
        writer->failed = 1;
        return 0;
    }
    writer->size += size;
    return 1;
}

void ob_httpcache_store_abort(ob_httpcache *cache, ob_httpcache_writer *writer)
{
    (void)cache;
    if (!writer)
        return;
    if (writer->file)
        fclose(writer->file);
    if (writer->temp) {
        remove(writer->temp);
        free(writer->temp);
    }
    free(writer);
}

int ob_httpcache_store_commit(ob_httpcache *cache, ob_httpcache_writer *writer, time_t now)
{
    char *path;
    struct item *item;
    int ok;
    if (!writer)
        return 0;
    if (!cache) {
        ob_httpcache_store_abort(cache, writer);
        return 0;
    }
    ok = !writer->failed;
    if (fclose(writer->file))
        ok = 0;
    writer->file = NULL;
    path = ok ? itemPath(cache, writer->hi, writer->lo) : NULL;
    if (path && rename(writer->temp, path)) {
        /* AmigaDOS renames onto no existing file */
        remove(path);
        if (rename(writer->temp, path))
            ok = 0;
    }
    if (!path)
        ok = 0;
    free(path);
    if (!ok) {
        ob_httpcache_store_abort(cache, writer);
        return 0;
    }
    if ((item = addItem(cache, writer->hi, writer->lo))) {
        cache->bytes -= item->size;
        item->size = writer->size;
        item->used = (long)now;
        cache->bytes += item->size;
        evict(cache, item);
    }
    free(writer->temp);
    free(writer);
    return 1;
}

void ob_httpcache_remove(ob_httpcache *cache, const char *url)
{
    unsigned long hi, lo;
    struct item *item;
    if (!cache || !url)
        return;
    hashURL(url, &hi, &lo);
    if ((item = findItem(cache, hi, lo)))
        evictItem(cache, item);
}

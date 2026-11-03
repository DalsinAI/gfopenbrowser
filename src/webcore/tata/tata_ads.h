/* A copy of DalsinAI/gfopentata's src/tata_ads.h (Tata 0.6.0, aeba9f0): change it there first. */
/* Tata's ad and tracker rules (0.6.0): which requests a web browser
 * leaves out. Plain C with no Amiga calls, so GFOpenBrowser builds the same
 * file (a copy in its src/webcore/tata/) and a rule means the same in both.
 *
 * The rules are S:Tata.ads (the list Tata's package installs, rebuilt from
 * public lists by tools/tata_ads_update.py) and ENVARC:Tata/Ads (the user's,
 * copied to ENV:Tata/Ads), one a line:
 *
 *   block host        requests to host, and to any name under it
 *   block host/path   only those whose path starts with /path
 *   pass host         never block requests to host (or under it): a line
 *                     of the list the user doesn't want
 *   allow site        block nothing on the pages of site (or under it):
 *                     the browser's "Don't block on this site"
 *   # a comment
 *
 * Names are matched without case; "www." is not special. A pass line
 * beats any block line, so the user's file can take a host off the list.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef TATA_ADS_H
#define TATA_ADS_H

#define TATA_ADS_LIST "S:Tata.ads"
#define TATA_ADS      "ENVARC:Tata/Ads"
#define TATA_ADS_ENV  "ENV:Tata/Ads"

#define TATA_ADS_MAX_LINE 256   /* longer lines are skipped */

typedef struct tata_ads tata_ads;
typedef void *(*tata_ads_alloc)(unsigned long size);   /* zeroed memory, or 0 */
typedef void (*tata_ads_release)(void *block);

/* An empty set of rules. 0 when there is no memory. */
tata_ads *tata_ads_new(tata_ads_alloc alloc, tata_ads_release release);
/* The rules in text (len bytes) added to the set: the list's, then the
 * user's. Lines that don't parse are skipped. Returns the rules added, or
 * -1 when there is no memory. */
long tata_ads_add_text(tata_ads *ads, const char *text, long len);
void tata_ads_free(tata_ads *ads);
/* How many rules of each kind the set has: block (with paths, a rule a
 * path), pass, allow. Any pointer may be 0. */
void tata_ads_counts(const tata_ads *ads, unsigned long *block, unsigned long *pass, unsigned long *allow);

/* Whether a request to host, for path (from its "/", 0 for "/"), made by
 * a page of site (the page's host, 0 when unknown), is to be left out. */
int tata_ads_blocked(const tata_ads *ads, const char *host, const char *path, const char *site);

/* Whether an allow line covers site (the site or a name it is under):
 * nothing is blocked on its pages. */
int tata_ads_allowed(const tata_ads *ads, const char *site);

/* Whether line is a rule: 0 if it is, else why not (a sentence). */
const char *tata_ads_check(const char *line);
/* The text with every line for the same host and path (whatever its verb)
 * taken out and, when verb is not 0, the rule "verb target" put at the end
 * (target: host or host/path): into out (size bytes). Returns its length,
 * or -1 when it doesn't fit or the rule is bad. */
long tata_ads_edit(const char *text, long len, const char *verb, const char *target, char *out, long size);

#endif

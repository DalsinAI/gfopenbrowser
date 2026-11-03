/*
 * GFOpenBrowser: Tata's ad and tracker rules (ob_tata.c), which leave out
 * the requests they block. Tata (DalsinAI/gfopentata) keeps them: its list,
 * S:Tata.ads, and the user's rules, ENV:Tata/Ads (else ENVARC:Tata/Ads),
 * which Tata's window edits. Without Tata the browser's own copy of the
 * list, PROGDIR:Tata.ads, stands in for S:Tata.ads. The files are read
 * again when their dates change (looked at once a second at most). The
 * rules themselves are tata/tata_ads.h, a copy of Tata's.
 *
 * Not thread-safe: the engine uses it on the main task only.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_TATA_H
#define OB_TATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Whether a request to host (path from its "/", or 0), made for a page of
 * site (its host, or 0), is to be left out. A page's own site is never
 * blocked. */
int ob_tata_blocked(const char *host, const char *path, const char *site);
/* Whether a request to host (path, site as above) is to wait until the
 * page has painted and gone idle or a form is used (a defer rule; asked
 * after ob_tata_blocked()). A page's own site is never held back. */
int ob_tata_deferred(const char *host, const char *path, const char *site);
/* Whether nothing is blocked on site's pages (an allow rule covers it). */
int ob_tata_site_allowed(const char *site);
/* "Don't block on this site": an allow rule for site put in the user's
 * rules (allow 1), or taken out (allow 0), in ENVARC: and ENV:. Returns 1
 * when written. */
int ob_tata_allow_site(const char *site, int allow);

#ifdef __cplusplus
}
#endif

#endif

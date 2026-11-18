/*
 * GFOpenBrowser: the Amiga clipboard (ob_clipboard.c). Text copied in a page
 * goes to clipboard.device, unit 0, as IFF FTXT, so any program can paste
 * it; text another program copied can be pasted in a page.
 *
 * The clip holds the text twice: a CHRS chunk in Latin-1, which every
 * Amiga program reads (a character Latin-1 does not have becomes "?"), and
 * a UTF8 chunk with all of it, which programs that know it prefer. Reading
 * takes the UTF8 chunk when there is one, else every CHRS chunk in turn.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_CLIPBOARD_H
#define OB_CLIPBOARD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The clip for n bytes of UTF-8 text, written into out when it fits in
 * size. Returns the clip's size in bytes, whether it fitted or not. Bytes
 * that are not UTF-8 are taken as Latin-1. */
size_t obclip_ftxt_build(const char *utf8, size_t n, unsigned char *out, size_t size);

/* The text of a clip of n bytes as UTF-8 in out (not ended with a 0; cut
 * to size at a whole character). Returns its length in bytes, or -1 when
 * the clip is not IFF FTXT (a picture, a sound, damaged). */
long obclip_ftxt_text(const unsigned char *iff, size_t n, char *out, size_t size);

/* On the Amiga: WebCore's pasteboard reads and writes the clipboard from
 * now on (WebKit patch 0121). Call once, at start. */
void obclip_install(void);

#ifdef __cplusplus
}
#endif

#endif

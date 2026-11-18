/*
 * GFOpenBrowser: SHA-1, SHA-256, SHA-384 and SHA-512 from
 * gfopencrypto.library (see ob_digest.c). WebKit's CryptoDigestAmiga.cpp
 * declares the same four functions itself; keep the two in step.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_DIGEST_H
#define OB_DIGEST_H

#ifdef __cplusplus
extern "C" {
#endif

#define OB_DIGEST_SHA1   1
#define OB_DIGEST_SHA256 2
#define OB_DIGEST_SHA384 3
#define OB_DIGEST_SHA512 4

/* NULL: no such kind, no memory, or no gfopencrypto.library 2.2 or newer. */
void *ob_digest_new(int kind);
void ob_digest_update(void *digest, const void *data, unsigned long length);
/* Writes the hash (20, 32, 48 or 64 bytes) and gives its length, or -1.
 * A digest gives one hash; after that it can only be freed. */
int ob_digest_final(void *digest, unsigned char *out);
void ob_digest_free(void *digest);

#ifdef __cplusplus
}
#endif

#endif

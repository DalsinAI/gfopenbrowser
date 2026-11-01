/* keyprobe [core]: what TLS key work costs on this Amiga, and whether AmiSSL
 * will call into a provider that lives in our own program (the hook a key
 * offload service would use).
 *   1. times X25519 and P-256 key exchange, P-256 ECDSA sign and verify, and
 *      RSA-2048 verify, with AmiSSL's own code;
 *   2. registers a provider "obkey" with OSSL_PROVIDER_add_builtin, offering
 *      one digest, OBTEST, then fetches and runs that digest through AmiSSL;
 *   3. with "core", also calls one of the core functions AmiSSL hands the
 *      provider (AmiSSL is built base-relative on 68k, so this may crash).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/provider.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include "ob_network.h"
#include "keyprobe-rsa.h"

static struct timeval start;

static long since(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - start.tv_sec) * 1000L + (now.tv_usec - start.tv_usec) / 1000L;
}

static EVP_PKEY *keygen(const char *type, const char *curve)
{
    EVP_PKEY *key = NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, type, NULL);
    if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0
        || (curve && EVP_PKEY_CTX_set_group_name(ctx, curve) <= 0)
        || EVP_PKEY_keygen(ctx, &key) <= 0)
        key = NULL;
    EVP_PKEY_CTX_free(ctx);
    return key;
}

static void timeExchange(const char *name, const char *type, const char *curve)
{
    unsigned char secret[128];
    size_t length = sizeof secret;
    EVP_PKEY *ours, *theirs;
    EVP_PKEY_CTX *ctx;
    long t0 = since(), t1;
    ours = keygen(type, curve);
    t1 = since();
    theirs = keygen(type, curve);
    if (!ours || !theirs) {
        printf("%s: keygen failed\n", name);
        return;
    }
    t0 = t1 - t0;
    t1 = since();
    ctx = EVP_PKEY_CTX_new(ours, NULL);
    if (EVP_PKEY_derive_init(ctx) <= 0 || EVP_PKEY_derive_set_peer(ctx, theirs) <= 0
        || EVP_PKEY_derive(ctx, secret, &length) <= 0)
        printf("%s: derive failed\n", name);
    else
        printf("%s: keygen %ld ms, derive %ld ms (%lu byte secret)\n", name, t0, since() - t1, (unsigned long)length);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(ours);
    EVP_PKEY_free(theirs);
}

static void timeEcdsa(void)
{
    static const unsigned char message[] = "GFOpenBrowser key offload probe";
    unsigned char signature[128];
    size_t length = sizeof signature;
    EVP_PKEY *key = keygen("EC", "P-256");
    EVP_MD_CTX *md = EVP_MD_CTX_new();
    long t0;
    if (!key) {
        printf("ECDSA P-256: keygen failed\n");
        return;
    }
    t0 = since();
    if (EVP_DigestSignInit_ex(md, NULL, "SHA256", NULL, NULL, key, NULL) <= 0
        || EVP_DigestSign(md, signature, &length, message, sizeof message - 1) <= 0) {
        printf("ECDSA P-256: sign failed\n");
        return;
    }
    printf("ECDSA P-256: sign %ld ms\n", since() - t0);
    EVP_MD_CTX_reset(md);
    t0 = since();
    if (EVP_DigestVerifyInit_ex(md, NULL, "SHA256", NULL, NULL, key, NULL) <= 0
        || EVP_DigestVerify(md, signature, length, message, sizeof message - 1) != 1)
        printf("ECDSA P-256: verify failed\n");
    else
        printf("ECDSA P-256: verify %ld ms\n", since() - t0);
    EVP_MD_CTX_free(md);
    EVP_PKEY_free(key);
}

static void timeRsa(void)
{
    BIO *bio = BIO_new_mem_buf(rsaPub, -1);
    EVP_PKEY *key = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    EVP_MD_CTX *md = EVP_MD_CTX_new();
    long t0 = since();
    if (!key || EVP_DigestVerifyInit_ex(md, NULL, "SHA256", NULL, NULL, key, NULL) <= 0
        || EVP_DigestVerify(md, rsaSig, rsaSigLen, (const unsigned char *)rsaMsg, strlen(rsaMsg)) != 1)
        printf("RSA-2048: verify failed\n");
    else
        printf("RSA-2048: verify %ld ms\n", since() - t0);
    EVP_MD_CTX_free(md);
    EVP_PKEY_free(key);
    BIO_free(bio);
}

/* The provider: one digest whose output is a running byte sum, so the probe
 * can tell that our code, not AmiSSL's, produced it. */

static int callCore;
static const OSSL_DISPATCH *coreTable;
static int calls;

struct obtest { unsigned char sum; };

static void *obtestNew(void *provctx)
{
    (void)provctx;
    calls++;
    return calloc(1, sizeof(struct obtest));
}

static void obtestFree(void *ctx) { free(ctx); }

static int obtestInit(void *ctx, const OSSL_PARAM params[])
{
    (void)params;
    ((struct obtest *)ctx)->sum = 0;
    calls++;
    return 1;
}

static int obtestUpdate(void *ctx, const unsigned char *in, size_t length)
{
    while (length--)
        ((struct obtest *)ctx)->sum += *in++;
    calls++;
    return 1;
}

static int obtestFinal(void *ctx, unsigned char *out, size_t *length, size_t size)
{
    if (size < 4)
        return 0;
    out[0] = 'O';
    out[1] = 'B';
    out[2] = 'K';
    out[3] = ((struct obtest *)ctx)->sum;
    *length = 4;
    calls++;
    return 1;
}

static int obtestGetParams(OSSL_PARAM params[])
{
    OSSL_PARAM *p;
    if ((p = OSSL_PARAM_locate(params, OSSL_DIGEST_PARAM_SIZE)) && !OSSL_PARAM_set_size_t(p, 4))
        return 0;
    if ((p = OSSL_PARAM_locate(params, OSSL_DIGEST_PARAM_BLOCK_SIZE)) && !OSSL_PARAM_set_size_t(p, 1))
        return 0;
    calls++;
    return 1;
}

static const OSSL_DISPATCH obtestFunctions[] = {
    { OSSL_FUNC_DIGEST_NEWCTX, (void (*)(void))obtestNew },
    { OSSL_FUNC_DIGEST_FREECTX, (void (*)(void))obtestFree },
    { OSSL_FUNC_DIGEST_INIT, (void (*)(void))obtestInit },
    { OSSL_FUNC_DIGEST_UPDATE, (void (*)(void))obtestUpdate },
    { OSSL_FUNC_DIGEST_FINAL, (void (*)(void))obtestFinal },
    { OSSL_FUNC_DIGEST_GET_PARAMS, (void (*)(void))obtestGetParams },
    { 0, NULL }
};

static const OSSL_ALGORITHM obkeyDigests[] = {
    { "OBTEST", "provider=obkey", obtestFunctions, "GFOpenBrowser probe digest" },
    { NULL, NULL, NULL, NULL }
};

static const OSSL_ALGORITHM *obkeyQuery(void *provctx, int operation, int *noCache)
{
    (void)provctx;
    *noCache = 0;
    calls++;
    return operation == OSSL_OP_DIGEST ? obkeyDigests : NULL;
}

static void obkeyTeardown(void *provctx) { (void)provctx; }

static const OSSL_DISPATCH obkeyFunctions[] = {
    { OSSL_FUNC_PROVIDER_QUERY_OPERATION, (void (*)(void))obkeyQuery },
    { OSSL_FUNC_PROVIDER_TEARDOWN, (void (*)(void))obkeyTeardown },
    { 0, NULL }
};

static int obkeyInit(const OSSL_CORE_HANDLE *handle, const OSSL_DISPATCH *in, const OSSL_DISPATCH **out, void **provctx)
{
    (void)handle;
    coreTable = in;
    *out = obkeyFunctions;
    *provctx = (void *)1;
    calls++;
    printf("provider init called\n");
    if (callCore) {
        for (; in && in->function_id; in++) {
            if (in->function_id == OSSL_FUNC_CORE_GET_LIBCTX) {
                OSSL_FUNC_core_get_libctx_fn *getLibctx = OSSL_FUNC_core_get_libctx(in);
                printf("calling core get_libctx...\n");
                fflush(stdout);
                printf("core get_libctx returned %p\n", (void *)getLibctx(handle));
            }
        }
    }
    return 1;
}

static void testProvider(void)
{
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_MD *md;
    OSSL_PROVIDER *ours, *base;
    long t0 = since();
    if (!OSSL_PROVIDER_add_builtin(NULL, "obkey", obkeyInit)) {
        printf("add_builtin failed\n");
        return;
    }
    base = OSSL_PROVIDER_load(NULL, "default");
    ours = OSSL_PROVIDER_load(NULL, "obkey");
    printf("load: default %s, obkey %s\n", base ? "ok" : "FAILED", ours ? "ok" : "FAILED");
    if (!ours)
        return;
    md = EVP_MD_fetch(NULL, "OBTEST", NULL);
    if (!md) {
        printf("fetch OBTEST failed\n");
        return;
    }
    if (!EVP_Digest("abc", 3, out, &length, md, NULL))
        printf("digest failed\n");
    else
        printf("digest: %u bytes %c%c%c %02x (want OBK 26), %d provider calls, %ld ms\n", length, out[0], out[1], out[2], out[3], calls, since() - t0);
    EVP_MD_free(md);
}

int main(int argc, char **argv)
{
    callCore = argc > 1 && !strcmp(argv[1], "core");
    gettimeofday(&start, NULL);
    if (!ob_network_open()) {
        printf("AmiSSL not opened\n");
        return 20;
    }
    printf("%s\n", OpenSSL_version(OPENSSL_VERSION));
    timeExchange("X25519", "X25519", NULL);
    timeExchange("ECDH P-256", "EC", "P-256");
    timeEcdsa();
    timeRsa();
    testProvider();
    ob_network_close();
    printf("done\n");
    return 0;
}

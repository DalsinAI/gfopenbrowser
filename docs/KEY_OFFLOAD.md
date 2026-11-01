# TLS key work at pace: design

Status: design, 5 October 2026. Nothing here is built yet beyond the probe.

Note, 19 October 2026: the engine no longer uses AmiSSL (curl does TLS through
GFOpenTLS, WebKit patch 0022), so the AmiSSL provider path below is out of the
browser's build. The measurements still stand.

## What we want

Each new HTTPS connection makes the Amiga do several seconds of public-key
maths before the first byte of the page arrives. We keep TLS on the Amiga,
end to end, and send only that maths to something faster when one is
around: the emulator itself, a PiStorm's ARM, or a Cradle on the LAN. With
nothing around, AmiSSL does it as today, so the browser still runs on a bare
68040.

## What we measured

`tests/keyprobe` on the bench (Instance-24, AC090, AmiSSL 5.27, OpenSSL
3.6.2):

| Operation | Time |
| --- | --- |
| X25519 key generation | 2,340 ms (includes first-use setup) |
| X25519 shared secret | 1,160 ms |
| P-256 key generation | 1,560 ms |
| P-256 shared secret | 1,420 ms |
| ECDSA P-256 sign | 2,200 ms |
| ECDSA P-256 verify | 1,140 ms |
| RSA-2048 verify | 560 ms |

A TLS 1.3 handshake does one key generation, one shared secret, the
certificate chain (usually two or three verifies) and one more verify for
the server's handshake signature: about **5 to 6 seconds** per new server.
A page from four servers pays that four times. Over the bridge or the LAN
each operation is a few milliseconds.

## Can AmiSSL hand the maths to us? Yes

- AmiSSL 5.27 exports OpenSSL 3's provider API from `amisslext.library`:
  `OSSL_PROVIDER_add_builtin`, `OSSL_PROVIDER_load`,
  `EVP_set_default_properties`, `OSSL_PARAM_*`.
- It cannot load a provider from a file (no dynamic modules), so the
  provider lives inside our program and is registered with `add_builtin`.
- The probe registers a provider `obkey` with one made-up digest, fetches
  it through AmiSSL and runs it. AmiSSL called our code seven times and
  returned our result. So the hook works on the 68k library.
- **One rule:** AmiSSL's 68k build is base-relative (`-resident32`). The
  table of core functions it gives a provider at start points inside the
  library, and calling those directly from our code would run them without
  the library's data register. Our provider never calls them; it calls
  OpenSSL only through the normal library vectors, which set it up.
- Also available, if needed: `SSL_CTX_set_cert_verify_callback` (replace the
  whole chain check with one call of ours). curl gives us the `SSL_CTX`
  through `CURLOPT_SSL_CTX_FUNCTION`.

## The Amiga side: one provider, three paths

The provider `obkey` offers:

- **Signature verify** for RSA (PKCS#1 and PSS) and ECDSA P-256/P-384:
  certificate chains and the handshake signature.
- **Key exchange** for X25519 and P-256: key generation and shared secret,
  advertised to the TLS layer as groups.

Each operation goes to the first path that is up, and otherwise to AmiSSL's
own code (the default provider), so a failure costs only the time it would
have taken anyway.

| Path | Where | Trust |
| --- | --- | --- |
| Internal: bridge | AmiCradle's bridge board, service `opentls.key/1` | The host is the machine; trusted |
| Internal: Emu68 | PiStorm's ARM through an Emu68 call | The machine itself; trusted |
| Nursery | A Cradle on the LAN, found by mDNS, paired once | Trusted only after pairing |
| Local | AmiSSL on the 68k | Always there |

**What a helper learns.** Verifying uses only public data, but the helper
says yes or no, so it must be one we trust. The key exchange gives the
helper that connection's session secret. Internal paths are the machine
itself, so both are fine there. For the nursery we offer two settings:
verify only (the default), or verify and exchange.

**Message.** One small request and answer, the same on every path:
operation, algorithm, public key, data, signature (or our public value);
answer: yes/no, or the shared secret. A few hundred bytes, so it fits
today's bridge registers too: the Amiga stops for a millisecond, which is
acceptable until the rings from the bridge plan land.

## One API, and the AutoConfig card

We expose an API for the work, not a mechanism: a program asks a named
service (`opentls.key/1` here, `media.decode/1` for the media work) and does
not know where it runs. On the Amiga that API is one Exec device behind an
AutoConfig card, as in the bridge plan:

- Where the Amiga is emulated, the card is virtual: AmiCradle's bridge
  board on an x86 PC, and the same card presented by Emu68 on a PiStorm's
  Pi. The work then runs natively on that machine's own CPU.
- On a real Amiga without one, it can be a physical card that does the work
  itself, or passes it to a nursery on the LAN.
- With no card, the same library does the work on the 68k.

Media decoding follows the same path, with the codec libraries'
68k code as the fallback.

## The nursery: where Cradle lives on the LAN

A nursery is any Cradle that offers services to Amigas on the network: a
desktop Cradle, an appliance, or a small box that does nothing else.

- **Finding it.** It advertises `_amigachrome._tcp` as the appliance design
  already plans, with a TXT key `svc=` listing what it offers, for example
  `svc=opentls.key/1,openweb.fetch/1`, and `fp=` its pairing fingerprint.
  The Amiga sends one mDNS query to 224.0.0.251 and reads the answers. ACNet
  already allows exactly this group and nothing else.
- **Seeing what is there.** A small Amiga command, `Nursery`, lists every
  nursery found, its services, and whether it is paired. GFOpenBrowser uses
  the same code in the background at start-up and never waits for it.
- **Pairing.** Once per Amiga and nursery: the nursery shows a short code,
  the user types it on the Amiga, and both keep a shared 32-byte key in
  `ENVARC:`. After that, each session uses that key with ChaCha20-Poly1305,
  which is cheap symmetric work, so connecting to the nursery does not cost
  a handshake of its own. Discovery only reads; nothing is used until
  paired.
- **AmiCradle's rule stays.** A guest cannot reach its own PC, so inside
  AmiCradle the internal bridge path is used, and the nursery is for real
  Amigas and PiStorms, or a Cradle on another machine.

## Order of work (each a draft PR, on Dale's word)

1. **`Nursery` command and the `svc=` record:** list what is on the LAN.
   The Cradle side belongs to the bridge and appliance thread.
2. **The `obkey` provider with only the local path:** TLS goes through it
   and nothing gets faster yet; proves it with curl on real sites.
3. **Internal path:** `opentls.key/1` on AmiCradle's bridge, with the
   bridge thread. Measure a page from several servers.
4. **Nursery path with pairing,** for real Amigas.
5. **Emu68 path,** when the bridge thread has an Emu68 call.

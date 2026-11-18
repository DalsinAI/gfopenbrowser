#!/bin/sh
# The engine has a blob registry: WebCore's own BlobRegistryImpl, behind
# src/webcore/AmigaPlatformStrategies.cpp (18 November 2026). Before that
# the registry was an empty class: a file a page got from its file button
# was 0 bytes long, FileReader failed, and no blob: address loaded.
#
# WebCore is not built on this machine, so this reads the source: every one
# of BlobRegistry's calls must reach the real registry, and the registry
# must be handed out (ResourceHandle's blob: loader asks for it). That
# files and blobs then work is the lab's check (tests/pages/blob-registry.html).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
F="$ROOT/src/webcore/AmigaPlatformStrategies.cpp"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

ok '! grep -q "EmptyBlobRegistry" "$F"' "no empty registry is left"
ok 'grep -q "^class AmigaBlobRegistry final : public BlobRegistry {" "$F"' "the registry class is there"
ok 'grep -q "^    BlobRegistryImpl m_registry;" "$F"' "it holds WebCore's BlobRegistryImpl"
ok 'grep -q "BlobRegistryImpl\* blobRegistryImpl() final { return &m_registry; }" "$F"' "it hands the registry out, for the blob: loader"
ok 'grep -q "static NeverDestroyed<AmigaBlobRegistry> blobRegistry;" "$F"' "the strategies give that registry"
BODY=$(sed -n '/^class AmigaBlobRegistry final/,/^};/p' "$F")
for call in registerInternalFileBlobURL registerInternalBlobURL registerBlobURL registerInternalBlobURLOptionallyFileBacked \
        registerInternalBlobURLForSlice unregisterBlobURL registerBlobURLHandle unregisterBlobURLHandle blobType blobSize \
        writeBlobsToTemporaryFilesForIndexedDB; do
    ok 'echo "$BODY" | grep -q "m_registry\.$call("' "$call reaches the registry"
done
# nothing in the class answers by itself
ok '! echo "$BODY" | grep -q "final { }\|return 0;\|emptyString()\|completionHandler({ })"' "no call is answered with nothing"
ok '[ -f "$ROOT/tests/pages/blob-registry.html" ] && grep -q "createObjectURL" "$ROOT/tests/pages/blob-registry.html"' "the lab page is in tests/pages"

echo "test_blob_registry: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

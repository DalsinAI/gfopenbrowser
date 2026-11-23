#!/bin/sh
# Builds nothing: a kitchen job on this script leaves the kitchen's checkout of this
# repository at the ref asked for, for a bigguy task that builds from that checkout.
cd "$(dirname "$0")/../.." && echo "kitchen checkout: $(git rev-parse HEAD 2>/dev/null || echo unknown)"

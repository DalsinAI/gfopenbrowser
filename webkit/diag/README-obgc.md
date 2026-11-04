# OBGC diagnostic (reddit worker lost Structure)

Not for main builds. Apply on top of the normal patch stack (after 0104):

    cd <webkit tree> && patch -p1 < webkit/diag/obgc-stale-structure.patch

Touches Structure.cpp and LLIntSlowPaths.cpp only, so an incremental rebuild.
Logs up to 40 `OBGC_*` lines through dataLog (OBJSCLOG file):

- SELFPREV / DEADPREV: a Structure transition from a freed or self Structure.
- STALE_GET / STALE_PUT: a get_by_id / put_by_id slow path saw a base cell or
  Structure that is not live.
- STALE_PUTCACHE: the put_by_id cache holds a dead old/new StructureID.

`fl 1` means the cell sits in the block being allocated from; there a fresh
cell reads as not live, so treat that line as a lead only.

## obgc-nosuspend.patch (apply after the one above, MachineStackMarker.cpp only)

AmigaOS 3 cannot suspend a thread (0001 makes Thread::suspend fail), and
MachineThreads::tryCopyOtherThreadStacks then skips that thread without a
word: its stack and registers are not scanned. That happens whenever the heap's
collector thread, not the mutator, conducts a stop-the-world phase (the mutator
had released heap access). Logs up to 40 `OBGC_NOSUSPEND` lines. If one comes
before a SELFPREV/DEADPREV in the same worker, that is the lost root.

## obgc-putbase.patch (after both above; Structure.cpp and LLIntSlowPaths.cpp)

G7 (20 runs): 0 NOSUSPEND, 3 SELFPREV in worker VMs, no STALE_PUT before
them, so the base and its Structure were live when put_by_id's slow path
began and the Structure died in a GC inside the put. This patch remembers each
VM's current put_by_id base and, on SELFPREV, logs `OBGC_PREV_BASE`: a is the
base (live / fl), b the base's StructureID, c the GC version (newly allocated
version) at slow path entry, d now. Base dead too means a lost root (stack or
frame scan); base live with its Structure dead means marking. It also drops the
DEADPREV false positives (cells in the block being allocated from) and raises
the cap to 60 lines.

## obgc-pervm.patch (after all three above)

G8 printed no worker line because the one global cap was used up: the page VM
printed 51 STALE_PUT + 9 STALE_GET false positives (cells in the block being
allocated from) before any worker reached a report. This gives each VM its
own cap of 30 lines and drops not-live cells in a free-listed block from every
check (STALE_GET, STALE_PUT, STALE_PUTCACHE, DEADPREV).

## obgc-baseinfo.patch (after all four above; LLIntSlowPaths.cpp only)

G9: worker put_by_id at bytecode 2667 finds a live base whose Structure is
already dead (no GC during the put), then the transition reuses that cell
(SELFPREV). So the base kept a Structure the last GC did not mark. This logs
up to 6 `OBGC_BASEINFO` lines on a stale base: base JSType, cellState, whether
the heap counts the base marked, the dead Structure's class name / type /
dictionary flag (read as left), and the function, script URL, first line and
bytecode offset, to name the object and the code.

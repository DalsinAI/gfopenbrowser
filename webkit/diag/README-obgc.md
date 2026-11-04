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

# The os32-gcc16 stove

The compiler GFOpenBrowser builds with: bebbo's amiga-gcc framework
(https://franke.ms/git/bebbo/amiga-gcc) with gcc branch `amiga16.2`
(GCC 16.2.0b).

- `gcc-patches/`: our changes to GCC and newlib headers. These are GPL
  (GCC's licence), not MIT. See `../docs/PORT_NOTES.md` for what each does.
- `compat/` and `include/`: our additions to libnix: the allocator (small
  blocks in size classes, large ones and aligned allocation over libnix's
  malloc), long double maths, `fenv.h`, `uchar.h` and small shims. These are MIT.
- `STOVE-NOTES.txt`: how the stove was built, and its flags.

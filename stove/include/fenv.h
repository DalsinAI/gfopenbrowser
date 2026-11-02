/* GFOpenBrowser (DalsinAI/gfopenbrowser). Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE. */
/* C99 <fenv.h> for AmigaOS 3.x with a 68881/68882/68040/68060 FPU.
 * AmiCradle os32-gcc16: libnix has no fenv. Header-only, using the
 * FPCR (rounding, trap enables) and FPSR (accrued exceptions) directly. */
#ifndef _AMICRADLE_FENV_H
#define _AMICRADLE_FENV_H

typedef unsigned int fexcept_t;
typedef struct { unsigned int __control_register; unsigned int __status_register; } fenv_t;

/* FPSR accrued-exception byte */
#define FE_INEXACT   0x08
#define FE_DIVBYZERO 0x10
#define FE_UNDERFLOW 0x20
#define FE_OVERFLOW  0x40
#define FE_INVALID   0x80
#define FE_ALL_EXCEPT (FE_INEXACT | FE_DIVBYZERO | FE_UNDERFLOW | FE_OVERFLOW | FE_INVALID)

/* FPCR rounding-mode bits */
#define FE_TONEAREST  0x00
#define FE_TOWARDZERO 0x10
#define FE_DOWNWARD   0x20
#define FE_UPWARD     0x30

#define FE_DFL_ENV ((const fenv_t*)-1)

static __inline__ unsigned int __ac_get_fpcr(void) { unsigned int v; __asm__ __volatile__("fmove%.l %!,%0" : "=dm"(v)); return v; }
static __inline__ void __ac_set_fpcr(unsigned int v) { __asm__ __volatile__("fmove%.l %0,%!" : : "dm"(v)); }
static __inline__ unsigned int __ac_get_fpsr(void) { unsigned int v; __asm__ __volatile__("fmove%.l %/fpsr,%0" : "=dm"(v)); return v; }
static __inline__ void __ac_set_fpsr(unsigned int v) { __asm__ __volatile__("fmove%.l %0,%/fpsr" : : "dm"(v)); }

static __inline__ int feclearexcept(int e) { __ac_set_fpsr(__ac_get_fpsr() & ~(unsigned int)(e & FE_ALL_EXCEPT)); return 0; }
static __inline__ int fegetexceptflag(fexcept_t* f, int e) { *f = __ac_get_fpsr() & (unsigned int)(e & FE_ALL_EXCEPT); return 0; }
static __inline__ int feraiseexcept(int e) { __ac_set_fpsr(__ac_get_fpsr() | (unsigned int)(e & FE_ALL_EXCEPT)); return 0; }
static __inline__ int fesetexceptflag(const fexcept_t* f, int e) { unsigned int m = (unsigned int)(e & FE_ALL_EXCEPT); __ac_set_fpsr((__ac_get_fpsr() & ~m) | (*f & m)); return 0; }
static __inline__ int fetestexcept(int e) { return (int)(__ac_get_fpsr() & (unsigned int)(e & FE_ALL_EXCEPT)); }
static __inline__ int fegetround(void) { return (int)(__ac_get_fpcr() & 0x30); }
static __inline__ int fesetround(int r) { if (r & ~0x30) return -1; __ac_set_fpcr((__ac_get_fpcr() & ~0x30u) | (unsigned int)r); return 0; }
static __inline__ int fegetenv(fenv_t* env) { env->__control_register = __ac_get_fpcr(); env->__status_register = __ac_get_fpsr(); return 0; }
static __inline__ int feholdexcept(fenv_t* env) { fegetenv(env); __ac_set_fpsr(env->__status_register & ~(unsigned int)FE_ALL_EXCEPT); __ac_set_fpcr(env->__control_register & ~0xff00u); return 0; }
static __inline__ int fesetenv(const fenv_t* env)
{
    if (env == FE_DFL_ENV) { __ac_set_fpcr(0); __ac_set_fpsr(0); return 0; }
    __ac_set_fpcr(env->__control_register); __ac_set_fpsr(env->__status_register); return 0;
}
static __inline__ int feupdateenv(const fenv_t* env) { unsigned int raised = __ac_get_fpsr() & FE_ALL_EXCEPT; fesetenv(env); feraiseexcept((int)raised); return 0; }

#endif

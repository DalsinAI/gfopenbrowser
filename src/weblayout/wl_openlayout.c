#ifdef WL_USE_OPENLAYOUT_LIBRARY
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/gfopenlayout.h>

#ifndef WL_LIBRARY
#error "gfopenlayout.library is opened by weblayout.library only (WL_LIBRARY)"
#endif
/* weblayout.library opens gfopenlayout.library in its LibOpen */
int wl_openlayout_open(void)
{
    return OpenLayoutBase != NULL;
}

void wl_openlayout_close(void)
{
}
#endif

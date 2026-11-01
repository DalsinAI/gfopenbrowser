#ifdef WL_USE_OPENLAYOUT_LIBRARY
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/openlayout.h>

#ifdef WL_LIBRARY
/* weblayout.library opens gfopenlayout.library in its LibOpen */
int wl_openlayout_open(void)
{
    return OpenLayoutBase != NULL;
}

void wl_openlayout_close(void)
{
}
#else
struct Library *OpenLayoutBase = NULL;

int wl_openlayout_open(void)
{
    if (OpenLayoutBase) return 1;
    OpenLayoutBase = OpenLibrary((CONST_STRPTR)OPENLAYOUTLIB_NAME,
                                 OPENLAYOUTLIB_VERSION);
    return OpenLayoutBase != NULL;
}

void wl_openlayout_close(void)
{
    if (OpenLayoutBase) {
        CloseLibrary(OpenLayoutBase);
        OpenLayoutBase = NULL;
    }
}
#endif /* WL_LIBRARY */
#endif

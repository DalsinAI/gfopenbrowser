/*
 * weblayout: page pictures through picture.datatype.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "wl_image_dt.h"

#include <exec/libraries.h>
#include <intuition/screens.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/datatypes.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <string.h>

#ifdef WL_LIBRARY
/* weblayout.library opens datatypes.library 44 in its LibOpen when there is
 * one; without it pictures are drawn as their boxes */
int wl_dt_open(void)
{
    return DataTypesBase != NULL;
}

void wl_dt_close(void)
{
}
#else
struct Library *DataTypesBase = NULL;

int wl_dt_open(void)
{
    if (DataTypesBase) return 1;
    DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 44);
    return DataTypesBase != NULL;
}

void wl_dt_close(void)
{
    if (DataTypesBase) {
        CloseLibrary(DataTypesBase);
        DataTypesBase = NULL;
    }
}
#endif

void wl_dt_image_free(wl_dt_image *image)
{
    if (!image) return;
    WaitBlit();
    if (image->object) DisposeDTObject((Object *)image->object);
    memset(image, 0, sizeof(*image));
}

int wl_dt_image_load(wl_dt_image *image, const char *path,
                     struct Screen *screen, UWORD width, UWORD height)
{
    Object *object;
    struct BitMapHeader *header = NULL;
    struct BitMap *bitmap = NULL;
    PLANEPTR mask = NULL;

    if (!image || !path || !*path || !screen || !wl_dt_open()) return 0;
    wl_dt_image_free(image);

    object = NewDTObject((CONST_STRPTR)path,
                         DTA_SourceType, DTST_FILE,
                         DTA_GroupID, GID_PICTURE,
                         PDTA_Remap, TRUE,
                         PDTA_Screen, screen,
                         PDTA_FreeSourceBitMap, TRUE,
                         PDTA_UseFriendBitMap, TRUE,
                         TAG_DONE);
    if (!object) return 0;

    GetDTAttrs(object, PDTA_BitMapHeader, (ULONG)&header, TAG_DONE);
    if (header) {
        image->source_width = header->bmh_Width;
        image->source_height = header->bmh_Height;
    }

    if (width && height && DataTypesBase->lib_Version >= 45) {
        /* Scaling is optional. Some perfectly valid picture sub-datatypes do
         * not implement PDTM_SCALE; failure here must not turn a decodable
         * image into a broken image. */
        (void)DoDTMethod(object, NULL, NULL, PDTM_SCALE,
                         (ULONG)width, (ULONG)height, 0UL);
    }

    if (!DoDTMethod(object, NULL, NULL, DTM_PROCLAYOUT, NULL, TRUE)) {
        DisposeDTObject(object);
        return 0;
    }

    GetDTAttrs(object,
               PDTA_BitMapHeader, (ULONG)&header,
               PDTA_DestBitMap, (ULONG)&bitmap,
               PDTA_MaskPlane, (ULONG)&mask,
               TAG_DONE);
    if (!bitmap)
        GetDTAttrs(object, PDTA_BitMap, (ULONG)&bitmap, TAG_DONE);
    if (!bitmap || !header) {
        DisposeDTObject(object);
        return 0;
    }

    image->object = object;
    image->bitmap = bitmap;
    image->mask = mask;
    image->width = header->bmh_Width;
    image->height = header->bmh_Height;
    if (!image->source_width) image->source_width = image->width;
    if (!image->source_height) image->source_height = image->height;
    return 1;
}

int wl_dt_image_draw(const wl_dt_image *image, struct RastPort *rp,
                     WORD x, WORD y, UWORD width, UWORD height)
{
    UWORD w, h;
    if (!image || !image->bitmap || !rp) return 0;
    w = width && width < image->width ? width : image->width;
    h = height && height < image->height ? height : image->height;
    if (!w || !h) return 0;

    if (image->mask) {
        BltMaskBitMapRastPort(image->bitmap, 0, 0, rp,
                              x, y, w, h, 0xc0, image->mask);
    } else {
        BltBitMapRastPort(image->bitmap, 0, 0, rp,
                          x, y, w, h, 0xc0);
    }
    return 1;
}

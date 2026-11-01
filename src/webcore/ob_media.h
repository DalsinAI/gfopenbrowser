/*
 * GFOpenBrowser: page pictures through media.decode/1 (ob_media.c). Open after
 * the network; 1 when a board or a paired Cradle offers it.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OB_MEDIA_H
#define OB_MEDIA_H

#ifdef __cplusplus
extern "C" {
#endif

int ob_media_open(void);
void ob_media_close(void);

#ifdef __cplusplus
}
#endif

#endif

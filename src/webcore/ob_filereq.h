/*
 * GFOpenBrowser: the file requester for a page's file button
 * (<input type="file">): asl.library's, over the browser's window
 * (ob_filereq.c). The page gets the files' full AmigaDOS names.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_FILEREQ_H
#define OB_FILEREQ_H

#ifdef __cplusplus
extern "C" {
#endif

struct Window;

/* Adds the full name of file in drawer to list as one more line: the
 * drawer as it is when it is empty or ends in ":" or "/", else with a "/"
 * after it. drawer and file are Latin-1 (AmigaDOS); list is UTF-8, lines
 * parted by "\n", in size bytes with its ending 0. Returns 1, or 0 with
 * list unchanged when the name does not fit or file is empty. */
int ob_filereq_add(char *list, int size, const char *drawer, const char *file);

/* The requester (many: any number of files may be picked), over parent,
 * which sleeps meanwhile. Fills result as ob_filereq_add() does and
 * returns how many files are in it; 0 for Cancel or without asl.library.
 * The drawer shown is the one last picked from. */
int ob_filereq_choose(struct Window *parent, int many, char *result, int size);

#ifdef __cplusplus
}
#endif

#endif

/*
 * playlist.h - the .wav files in the root folder of the USB stick.
 * Sorted by name (case-insensitive) so both boards agree on the indexes.
 * Only the player task changes the list.
 */
#ifndef APP_PLAYLIST_H
#define APP_PLAYLIST_H

#include "galaxy_link.h"

int  Playlist_Scan(const char *root);    /* returns the number of tracks */
void Playlist_Clear(void);
int  Playlist_Count(void);
const char *Playlist_Name(int index);    /* NULL when out of range */
int  Playlist_Find(const char *name);    /* index or -1 */

/* pure helpers (tested on the PC) */
int  Playlist_IsWavName(const char *name);
void Playlist_SortNames(char names[][GX_NAME_MAX + 1], int count);

#endif

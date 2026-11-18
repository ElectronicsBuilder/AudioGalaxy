/*
 * playlist.c - the .wav files in the root folder of the USB stick. See playlist.h.
 */
#include "playlist.h"
#include <ctype.h>
#include <string.h>

#ifndef PLAYLIST_HOST_TEST
#include "ff.h"
#endif

static char names[GX_PLAYLIST_MAX][GX_NAME_MAX + 1];
static int count;

static int P_CompareNoCase(const char *a, const char *b)
{
  while (*a && *b)
  {
    int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
    if (d != 0) return d;
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int Playlist_IsWavName(const char *name)
{
  size_t n = strlen(name);
  if (n < 5 || name[0] == '.') return 0;          /* "._x.wav" are macOS metadata files */
  return P_CompareNoCase(name + n - 4, ".wav") == 0;
}

void Playlist_SortNames(char list[][GX_NAME_MAX + 1], int n)
{
  char tmp[GX_NAME_MAX + 1];
  for (int i = 1; i < n; i++)                     /* insertion sort, <= 100 names */
  {
    memcpy(tmp, list[i], sizeof tmp);
    int j = i - 1;
    while (j >= 0 && P_CompareNoCase(list[j], tmp) > 0)
    {
      memcpy(list[j + 1], list[j], sizeof tmp);
      j--;
    }
    memcpy(list[j + 1], tmp, sizeof tmp);
  }
}

void Playlist_Clear(void)
{
  count = 0;
}

int Playlist_Count(void)
{
  return count;
}

const char *Playlist_Name(int index)
{
  return (index >= 0 && index < count) ? names[index] : NULL;
}

int Playlist_Find(const char *name)
{
  for (int i = 0; i < count; i++)
  {
    if (P_CompareNoCase(names[i], name) == 0) return i;
  }
  return -1;
}

#ifndef PLAYLIST_HOST_TEST
int Playlist_Scan(const char *root)
{
  DIR dir;
  FILINFO fno;
  count = 0;
  if (f_opendir(&dir, root) != FR_OK) return 0;
  while (count < GX_PLAYLIST_MAX && f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0')
  {
    if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) continue;
    if (!Playlist_IsWavName(fno.fname)) continue;
    if (strlen(fno.fname) > GX_NAME_MAX) continue;   /* cannot be sent / reopened */
    strcpy(names[count++], fno.fname);
  }
  f_closedir(&dir);
  Playlist_SortNames(names, count);
  return count;
}
#endif

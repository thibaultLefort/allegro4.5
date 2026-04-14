/*         ______   ___    ___ 
 *        /\  _  \ /\_ \  /\_ \ 
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___ 
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      MacOS X common quartz gfx driver functions.
 *
 *      By Angelo Mottola, based on similar QNX code by Eric Botcazou.
 *      Patched for modern macOS: QuickDraw-backed bitmaps replaced with
 *      plain memory-backed storage.
 *
 *      See readme.txt for copyright information.
 */


#include "allegro.h"
#include "allegro/internal/aintern.h"
#include "allegro/platform/aintosx.h"

#ifndef ALLEGRO_MACOSX
   #error something is wrong with the makefile
#endif



/* setup_direct_shifts
 *  Setups direct color shifts.
 */
void setup_direct_shifts(void)
{
   _rgb_r_shift_15 = 10;
   _rgb_g_shift_15 = 5;
   _rgb_b_shift_15 = 0;

   _rgb_r_shift_16 = 11;
   _rgb_g_shift_16 = 5;
   _rgb_b_shift_16 = 0;

   _rgb_r_shift_24 = 16;
   _rgb_g_shift_24 = 8;
   _rgb_b_shift_24 = 0;

   _rgb_a_shift_32 = 24;
   _rgb_r_shift_32 = 16; 
   _rgb_g_shift_32 = 8; 
   _rgb_b_shift_32 = 0;
}



/* osx_qz_write_line: line switcher for pseudo-video bitmaps. */
unsigned long osx_qz_write_line(BITMAP *bmp, int line)
{
   if (!(bmp->id & BMP_ID_LOCKED))
      bmp->id |= BMP_ID_LOCKED | BMP_ID_AUTOLOCK;
   return (unsigned long)(bmp->line[line]);
}



/* osx_qz_unwrite_line: line updater for pseudo-video bitmaps. */
void osx_qz_unwrite_line(BITMAP *bmp)
{
   if (bmp->id & BMP_ID_AUTOLOCK) {
      bmp->id &= ~(BMP_ID_LOCKED | BMP_ID_AUTOLOCK);
   }
}



/* osx_qz_acquire: bitmap locking for pseudo-video bitmaps. */
void osx_qz_acquire(BITMAP *bmp)
{
   bmp->id |= BMP_ID_LOCKED;
}



/* osx_qz_release: bitmap unlocking for pseudo-video bitmaps. */
void osx_qz_release(BITMAP *bmp)
{
   bmp->id &= ~BMP_ID_LOCKED;
}



/* osx_qz_created_sub_bitmap: preserve ownership info for sub-bitmaps. */
void osx_qz_created_sub_bitmap(BITMAP *bmp, BITMAP *parent)
{
   bmp->extra = parent->extra;
}



/* _make_osx_bitmap: create a memory-backed bitmap that still behaves like
 * a driver-managed bitmap to the rest of the Mac backend.
 */
static BITMAP *_make_osx_bitmap(int width, int height, int bitmap_id)
{
   BITMAP *bmp;
   GFX_VTABLE *vtable;
   unsigned char *addr;
   int pitch;
   int i, size;
   int color_depth = screen ? bitmap_color_depth(screen) : _color_depth;
   int padding = (color_depth == 24) ? 1 : 0;

   vtable = _get_vtable(color_depth);
   if (!vtable)
      return NULL;

   pitch = width * BYTES_PER_PIXEL(color_depth);
   addr = _AL_MALLOC_ATOMIC(height * pitch + padding);
   if (!addr)
      return NULL;

   size = sizeof(BITMAP) + sizeof(char *) * height;
   bmp = (BITMAP *)_AL_MALLOC(size);
   if (!bmp) {
      _AL_FREE(addr);
      return NULL;
   }

   bmp->w = bmp->cr = width;
   bmp->h = bmp->cb = height;
   bmp->clip = TRUE;
   bmp->cl = bmp->ct = 0;
   bmp->vtable = vtable;
   bmp->write_bank = bmp->read_bank = osx_qz_write_line;
   bmp->dat = addr;
   bmp->id = bitmap_id;
   bmp->extra = NULL;
   bmp->x_ofs = 0;
   bmp->y_ofs = 0;
   bmp->seg = (bitmap_id & BMP_ID_VIDEO) ? _video_ds() : _default_ds();

   bmp->line[0] = addr;
   for (i = 1; i < height; i++)
      bmp->line[i] = bmp->line[i - 1] + pitch;
   return bmp;
}



/* osx_qz_create_video_bitmap: allocate a driver-managed memory bitmap. */
BITMAP *osx_qz_create_video_bitmap(int width, int height)
{
   if ((gfx_driver->w == width) && (gfx_driver->h == height) && (!osx_screen_used)) {
      osx_screen_used = TRUE;
      return screen;
   }
   return _make_osx_bitmap(width, height, BMP_ID_VIDEO);
}



/* osx_qz_create_system_bitmap: allocate a driver-managed system bitmap. */
BITMAP *osx_qz_create_system_bitmap(int width, int height)
{
   return _make_osx_bitmap(width, height, BMP_ID_SYSTEM);
}



/* osx_qz_destroy_video_bitmap: free a driver-managed memory bitmap. */
void osx_qz_destroy_video_bitmap(BITMAP *bmp)
{
   if (bmp) {
      if (bmp == screen) {
         osx_screen_used = FALSE;
         return;
      }
      if (bmp->dat)
         _AL_FREE(bmp->dat);
      _AL_FREE(bmp);
   }
}



/* osx_qz_blit_to_self: memory-backed replacement for the old QuickDraw path. */
void osx_qz_blit_to_self(BITMAP *source, BITMAP *dest, int source_x, int source_y, int dest_x, int dest_y, int width, int height)
{
   int bytes_per_pixel = BYTES_PER_PIXEL(bitmap_color_depth(source));
   int row_size = width * bytes_per_pixel;
   int y, step, start, end;

   if ((source == dest) && (dest_y > source_y)) {
      start = height - 1;
      end = -1;
      step = -1;
   }
   else {
      start = 0;
      end = height;
      step = 1;
   }

   for (y = start; y != end; y += step) {
      memmove(dest->line[dest_y + y] + dest_x * bytes_per_pixel,
              source->line[source_y + y] + source_x * bytes_per_pixel,
              row_size);
   }
}

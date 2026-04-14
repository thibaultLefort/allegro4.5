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
 *      MacOS X quartz fullscreen gfx driver — STUBBED.
 *      QuickDraw/Carbon/QuickTime APIs removed on modern macOS.
 *      Fullscreen mode is not available on this platform.
 *
 *      See readme.txt for copyright information.
 */

#include "allegro.h"
#include "allegro/internal/aintern.h"
#include "allegro/platform/aintosx.h"

#ifndef ALLEGRO_MACOSX
   #error something is wrong with the makefile
#endif


/* Global vars still referenced from main.m and aintosx.h */
void* osx_palette = NULL;      /* CGDirectPaletteRef removed */
int   osx_palette_dirty = FALSE;
int   osx_screen_used = FALSE;


static BITMAP *osx_qz_full_init(int w, int h, int v_w, int v_h, int color_depth)
{
   (void)w; (void)h; (void)v_w; (void)v_h; (void)color_depth;
   ustrzcpy(allegro_error, ALLEGRO_ERROR_SIZE,
      get_config_text("Fullscreen not supported on modern macOS"));
   return NULL;
}

static void osx_qz_full_exit(BITMAP *bmp) { (void)bmp; }
static void osx_qz_full_vsync(void) {}
static void osx_qz_full_set_palette(AL_CONST struct RGB *p, int from, int to, int vsync)
{
   (void)p; (void)from; (void)to; (void)vsync;
}
static int osx_qz_show_video_bitmap(BITMAP *bmp) { (void)bmp; return -1; }
static GFX_MODE_LIST *osx_qz_fetch_mode_list(void) { return NULL; }


/* Fade stubs (QuickDraw gamma APIs removed) */
void osx_init_fade_system(void) {}
void osx_fade_screen(int fade_in, double seconds) { (void)fade_in; (void)seconds; }


GFX_DRIVER gfx_quartz_full =
{
   GFX_QUARTZ_FULLSCREEN,
   empty_string,
   empty_string,
   "Quartz fullscreen (not supported)",
   osx_qz_full_init,
   osx_qz_full_exit,
   NULL, osx_qz_full_vsync, osx_qz_full_set_palette,
   NULL, NULL, NULL,
   osx_qz_create_video_bitmap,
   osx_qz_destroy_video_bitmap,
   osx_qz_show_video_bitmap,
   NULL,
   osx_qz_create_system_bitmap,
   osx_qz_destroy_video_bitmap,
   osx_mouse_set_sprite,
   osx_mouse_show,
   osx_mouse_hide,
   osx_mouse_move,
   NULL, NULL, NULL, NULL,
   osx_qz_fetch_mode_list,
   0, 0,
   TRUE,
   0, 0, 0, 0,
   FALSE
};

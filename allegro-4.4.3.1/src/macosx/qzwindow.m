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
 *      MacOS X quartz windowed gfx driver
 *
 *      By Angelo Mottola.
 *      Patched for modern macOS (arm64): replaced NSQuickDrawView/QuickDraw
 *      rendering with CoreGraphics pixel buffer rendering.
 *
 *      See readme.txt for copyright information.
 */


#include "allegro.h"
#include "allegro/internal/aintern.h"
#include "allegro/platform/aintosx.h"

#ifndef ALLEGRO_MACOSX
   #error something is wrong with the makefile
#endif

#import <CoreGraphics/CoreGraphics.h>


static BITMAP *osx_qz_window_init(int, int, int, int, int);
static void osx_qz_window_exit(BITMAP *);
static BITMAP *private_osx_qz_window_init(int, int, int, int, int);
static void private_osx_qz_window_exit(BITMAP *);
static void osx_qz_window_vsync(void);
static void osx_qz_window_set_palette(AL_CONST struct RGB *, int, int, int);
static void osx_signal_vsync(void);
static BITMAP* osx_create_video_bitmap(int w, int h);
static int osx_show_video_bitmap(BITMAP*);
static void osx_destroy_video_bitmap(BITMAP*);
static BITMAP* create_video_page(unsigned char*);

static pthread_mutex_t vsync_mutex;
static pthread_cond_t vsync_cond;
static int lock_nesting = 0;
static AllegroWindowDelegate *window_delegate = NULL;
static char driver_desc[256];
static int requested_color_depth;
static COLORCONV_BLITTER_FUNC *colorconv_blitter = NULL;
static AllegroView *qd_view = NULL;
static int desktop_depth;
static BITMAP* pseudo_screen = NULL;
static BITMAP* first_page = NULL;
static unsigned char *pseudo_screen_addr = NULL;
static int pseudo_screen_pitch;
static int pseudo_screen_depth;
static char *dirty_lines = NULL;
static GFX_VTABLE _special_vtable;
static GFX_VTABLE _unspecial_vtable;
static BITMAP* current_video_page = NULL;

/* CoreGraphics pixel buffer for modern rendering */
static unsigned char *cg_pixel_buffer = NULL;
static int cg_pixel_buffer_pitch = 0;

typedef struct OSX_WINDOW_INIT_REQUEST
{
   int w;
   int h;
   int v_w;
   int v_h;
   int color_depth;
   BITMAP *result;
} OSX_WINDOW_INIT_REQUEST;

void* osx_window_mutex;


@interface AllegroWindowThreadProxy : NSObject
+ (void)createWindowOnMainThread:(NSValue *)value;
+ (void)destroyWindowOnMainThread:(id)unused;
@end


GFX_DRIVER gfx_quartz_window =
{
   GFX_QUARTZ_WINDOW,
   empty_string,
   empty_string,
   "Quartz window",
   osx_qz_window_init,
   osx_qz_window_exit,
   NULL,
   osx_qz_window_vsync,
   osx_qz_window_set_palette,
   NULL, NULL, NULL,
   osx_create_video_bitmap,
   osx_destroy_video_bitmap,
   osx_show_video_bitmap,
   NULL, NULL, NULL,
   osx_mouse_set_sprite,
   osx_mouse_show,
   osx_mouse_hide,
   osx_mouse_move,
   NULL, NULL, NULL, NULL, NULL,
   0, 0,
   TRUE,
   0, 0, 0, 0,
   TRUE
};



/* prepare_window_for_animation:
 *  Stub: QuickDraw removed. Just marks all lines dirty so they get
 *  redrawn by osx_update_dirty_lines() on the next tick.
 */
static void prepare_window_for_animation(int refresh_view)
{
   (void)refresh_view;
   _unix_lock_mutex(osx_window_mutex);
   if (dirty_lines)
      memset(dirty_lines, 1, gfx_quartz_window.h);
   _unix_unlock_mutex(osx_window_mutex);
}


@implementation AllegroWindow

- (void)display
{
   [super display];
   if (desktop_depth == 32)
      prepare_window_for_animation(TRUE);
}

- (void)miniaturize: (id)sender
{
   if (desktop_depth == 32)
      prepare_window_for_animation(FALSE);
   [super miniaturize: sender];
}

@end


@implementation AllegroWindowThreadProxy

+ (void)createWindowOnMainThread:(NSValue *)value
{
   OSX_WINDOW_INIT_REQUEST *req = [value pointerValue];
   req->result = private_osx_qz_window_init(req->w, req->h, req->v_w,
      req->v_h, req->color_depth);
}

+ (void)destroyWindowOnMainThread:(id)unused
{
   (void)unused;
   private_osx_qz_window_exit(NULL);
}

@end



@implementation AllegroWindowDelegate

- (BOOL)windowShouldClose: (id)sender
{
   if (osx_window_close_hook) {
      osx_window_close_hook();
      return NO;   /* hook handles it; window stays open */
   }
   /* No hook — allow the window to close normally.
    * windowWillClose: will fire next and call exit(0). */
   return YES;
}

- (void)windowWillClose:(NSNotification *)notification
{
   /* Fired unconditionally whenever the window closes (red button, Cmd+W,
    * allegro_exit(), etc.).  If no hook is registered, exit cleanly here.
    *
    * Use _exit() instead of exit(): exit() runs atexit handlers, one of which
    * calls allegro_exit() → [osx_window close] → re-enters windowWillClose:
    * → exit() again inside an atexit handler, which is undefined behavior
    * (typically a hang or crash). _exit() terminates immediately without
    * running atexit handlers, breaking the re-entrancy. */
   if (!osx_window_close_hook)
      _exit(0);
}

- (void)windowDidDeminiaturize: (NSNotification *)aNotification
{
   _unix_lock_mutex(osx_window_mutex);
   memset(dirty_lines, 1, gfx_quartz_window.h);
   _unix_unlock_mutex(osx_window_mutex);
}

- (void)windowDidBecomeKey:(NSNotification *)notification
{
   _unix_lock_mutex(osx_skip_events_processing_mutex);
   osx_skip_events_processing = FALSE;
   _unix_unlock_mutex(osx_skip_events_processing_mutex);
}

- (void)windowDidResignKey:(NSNotification *)notification
{
   _unix_lock_mutex(osx_skip_events_processing_mutex);
   osx_skip_events_processing = TRUE;
   _unix_unlock_mutex(osx_skip_events_processing_mutex);
}

@end



@implementation AllegroView

/* resetCursorRects: restore the Allegro cursor rect. */
- (void)resetCursorRects
{
   [super resetCursorRects];
   [self addCursorRect: [self bounds] cursor: osx_cursor];
   [osx_cursor setOnMouseEntered: YES];
}

/* drawRect: render the CoreGraphics pixel buffer to the view. */
- (void)drawRect:(NSRect)dirtyRect
{
   (void)dirtyRect;
   if (!cg_pixel_buffer) return;
   int w = gfx_quartz_window.w;
   int h = gfx_quartz_window.h;
   CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
   CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
   CGDataProviderRef dp = CGDataProviderCreateWithData(
      NULL, cg_pixel_buffer, (size_t)h * cg_pixel_buffer_pitch, NULL);
   CGImageRef img = CGImageCreate(
      w, h, 8, 32, cg_pixel_buffer_pitch, cs,
      kCGBitmapByteOrder32Host | kCGImageAlphaNoneSkipFirst,
      dp, NULL, false, kCGRenderingIntentDefault);
   /* CGContextDrawImage renders correctly on layer-backed ARM64 macOS views.
    * The AppKit CTM already has a negative y-scale (d < 0) that maps the
    * bottom-left origin to the top-left of the screen, so no extra flip
    * is needed — applying one would double-flip the image upside down. */
   CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
   CGImageRelease(img);
   CGDataProviderRelease(dp);
   CGColorSpaceRelease(cs);
}

@end



/* osx_qz_acquire_win: bitmap locking for windowed mode. */
static void osx_qz_acquire_win(BITMAP *bmp)
{
   _unix_lock_mutex(osx_window_mutex);
   if (lock_nesting++ == 0)
      bmp->id |= BMP_ID_LOCKED;
}

/* osx_qz_release_win: bitmap unlocking for windowed mode. */
static void osx_qz_release_win(BITMAP *bmp)
{
   ASSERT(lock_nesting > 0);
   if (--lock_nesting == 0)
      bmp->id &= ~BMP_ID_LOCKED;
   _unix_unlock_mutex(osx_window_mutex);
}



/* osx_qz_write_line_win: line switcher for windowed mode. */
static unsigned long osx_qz_write_line_win(BITMAP *bmp, int line)
{
   if (!(bmp->id & BMP_ID_LOCKED)) {
      osx_qz_acquire_win(bmp);
      /* bmp->extra / QuickDraw port locking removed (no QuickDraw on modern macOS) */
      bmp->id |= BMP_ID_AUTOLOCK;
   }
   dirty_lines[line + bmp->y_ofs] = 1;
   return (unsigned long)(bmp->line[line]);
}



/* osx_qz_unwrite_line_win: line updater for windowed mode. */
static void osx_qz_unwrite_line_win(BITMAP *bmp)
{
   if (bmp->id & BMP_ID_AUTOLOCK) {
      osx_qz_release_win(bmp);
      /* QuickDraw UnlockPortBits removed */
      bmp->id &= ~BMP_ID_AUTOLOCK;
   }
}



/* osx_update_dirty_lines:
 *  Dirty lines updater — called from the main app thread.
 *  Uses CoreGraphics instead of the removed QuickDraw APIs.
 */
void osx_update_dirty_lines(void)
{
   struct GRAPHICS_RECT src_gfx_rect, dest_gfx_rect;
   int top, bottom;

   if (![osx_window isVisible])
      return;

   _unix_lock_mutex(osx_window_mutex);

   /* Skip if no dirty lines */
   for (top = 0; (top < gfx_quartz_window.h) && (!dirty_lines[top]); top++)
      ;
   if (top >= gfx_quartz_window.h) {
      _unix_unlock_mutex(osx_window_mutex);
      osx_signal_vsync();
      return;
   }

   if (cg_pixel_buffer && (colorconv_blitter || (osx_setup_colorconv_blitter() == 0))) {
      top = 0;
      while (top < gfx_quartz_window.h) {
         while ((top < gfx_quartz_window.h) && (!dirty_lines[top]))
            top++;
         if (top >= gfx_quartz_window.h)
            break;
         bottom = top;
         while ((bottom < gfx_quartz_window.h) && dirty_lines[bottom]) {
            dirty_lines[bottom] = 0;
            bottom++;
         }

         src_gfx_rect.width  = gfx_quartz_window.w;
         src_gfx_rect.height = bottom - top;
         src_gfx_rect.pitch  = pseudo_screen_pitch;
         src_gfx_rect.data   = pseudo_screen_addr + top * pseudo_screen_pitch;

         dest_gfx_rect.pitch = cg_pixel_buffer_pitch;
         dest_gfx_rect.data  = cg_pixel_buffer + top * cg_pixel_buffer_pitch;

         colorconv_blitter(&src_gfx_rect, &dest_gfx_rect);
         top = bottom;
      }
   } else {
      memset(dirty_lines, 0, gfx_quartz_window.h);
   }

   _unix_unlock_mutex(osx_window_mutex);

   [qd_view setNeedsDisplay:YES];

   osx_signal_vsync();
}



/* osx_setup_colorconv_blitter:
 *  Sets up the color conversion blitter.
 *  QuickDraw qdPort path removed; desktop depth always read from CG.
 */
int osx_setup_colorconv_blitter()
{
   CFDictionaryRef mode;
   int dd;

   mode = CGDisplayCurrentMode(kCGDirectMainDisplay);
   CFNumberGetValue(CFDictionaryGetValue(mode, kCGDisplayBitsPerPixel),
      kCFNumberSInt32Type, &desktop_depth);
   dd = desktop_depth;
   if (dd == 16) dd = 15;

   _unix_lock_mutex(osx_window_mutex);
   if (colorconv_blitter)
      _release_colorconv_blitter(colorconv_blitter);
   colorconv_blitter = _get_colorconv_blitter(requested_color_depth, dd);
   if (colorconv_blitter)
      _set_colorconv_palette(_current_palette, 0, 255);
   memset(dirty_lines, 1, gfx_quartz_window.h);
   _unix_unlock_mutex(osx_window_mutex);

   return (colorconv_blitter ? 0 : -1);
}



/* osx_qz_window_init:
 *  Initializes windowed gfx mode.
 */
static BITMAP *private_osx_qz_window_init(int w, int h, int v_w, int v_h, int color_depth)
{
   CFDictionaryRef mode;
   NSRect rect = NSMakeRect(0, 0, w, h);
   int refresh_rate;
   char tmp1[128], tmp2[128];

   pthread_cond_init(&vsync_cond, NULL);
   pthread_mutex_init(&vsync_mutex, NULL);
   osx_window_mutex = _unix_create_mutex();
   lock_nesting = 0;

   if (1
#ifdef ALLEGRO_COLOR8
      && (color_depth != 8)
#endif
#ifdef ALLEGRO_COLOR16
      && (color_depth != 15)
      && (color_depth != 16)
#endif
#ifdef ALLEGRO_COLOR24
      && (color_depth != 24)
#endif
#ifdef ALLEGRO_COLOR32
      && (color_depth != 32)
#endif
      ) {
      ustrzcpy(allegro_error, ALLEGRO_ERROR_SIZE, get_config_text("Unsupported color depth"));
      return NULL;
   }

   if ((w == 0) && (h == 0)) {
      w = 320;
      h = 200;
   }

   if (v_w < w) v_w = w;
   if (v_h < h) v_h = h;

   if ((v_w != w) || (v_h != h)) {
      ustrzcpy(allegro_error, ALLEGRO_ERROR_SIZE, get_config_text("Resolution not supported"));
      return NULL;
   }

   osx_window = [[AllegroWindow alloc] initWithContentRect: rect
      styleMask: NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask
      backing: NSBackingStoreBuffered
      defer: NO];

   window_delegate = [[[AllegroWindowDelegate alloc] init] autorelease];
   [osx_window setDelegate: window_delegate];
   [osx_window setOneShot: YES];
   [osx_window setAcceptsMouseMovedEvents: YES];
   [osx_window setViewsNeedDisplay: NO];
   [osx_window setReleasedWhenClosed: YES];
   [osx_window useOptimizedDrawing: YES];
   [osx_window center];

   qd_view = [[AllegroView alloc] initWithFrame: rect];
   if (!qd_view) {
      ustrzcpy(allegro_error, ALLEGRO_ERROR_SIZE, get_config_text("Not enough memory"));
      return NULL;
   }
   [osx_window setContentView: qd_view];

   set_window_title(osx_window_title);
   [osx_window makeKeyAndOrderFront: nil];

   dirty_lines = calloc(h + 1, sizeof(char));
   memset(dirty_lines, 1, h + 1);

   setup_direct_shifts();

   gfx_quartz_window.w = w;
   gfx_quartz_window.h = h;
   gfx_quartz_window.vid_mem = w * h * BYTES_PER_PIXEL(color_depth);

   requested_color_depth = color_depth;
   colorconv_blitter = NULL;
   mode = CGDisplayCurrentMode(kCGDirectMainDisplay);
   CFNumberGetValue(CFDictionaryGetValue(mode, kCGDisplayBitsPerPixel),
      kCFNumberSInt32Type, &desktop_depth);
   CFNumberGetValue(CFDictionaryGetValue(mode, kCGDisplayRefreshRate),
      kCFNumberSInt32Type, &refresh_rate);
   _set_current_refresh_rate(refresh_rate);

   pseudo_screen_pitch = w * BYTES_PER_PIXEL(color_depth);
   pseudo_screen_addr = _AL_MALLOC(h * pseudo_screen_pitch);
   pseudo_screen = _make_bitmap(w, h, (unsigned long) pseudo_screen_addr,
      &gfx_quartz_window, color_depth, pseudo_screen_pitch);
   if (!pseudo_screen)
      return NULL;
   current_video_page = pseudo_screen;
   first_page = NULL;

   memcpy(&_special_vtable, &_screen_vtable, sizeof(GFX_VTABLE));
   _special_vtable.acquire = osx_qz_acquire_win;
   _special_vtable.release = osx_qz_release_win;
   _special_vtable.unwrite_bank = osx_qz_unwrite_line_win;
   memcpy(&_unspecial_vtable, _get_vtable(color_depth), sizeof(GFX_VTABLE));
   pseudo_screen->read_bank  = osx_qz_write_line_win;
   pseudo_screen->write_bank = osx_qz_write_line_win;
   pseudo_screen->vtable = &_special_vtable;

   /* Allocate the CoreGraphics pixel buffer (always 32bpp BGRA) */
   cg_pixel_buffer_pitch = w * 4;
   cg_pixel_buffer = calloc(h, cg_pixel_buffer_pitch);
   if (!cg_pixel_buffer) {
      ustrzcpy(allegro_error, ALLEGRO_ERROR_SIZE, get_config_text("Not enough memory"));
      return NULL;
   }

   uszprintf(driver_desc, sizeof(driver_desc),
      uconvert_ascii("Cocoa window using CoreGraphics, %d bpp %s", tmp1),
      color_depth,
      uconvert_ascii(color_depth == desktop_depth ? "in matching" : "in fast emulation", tmp2));
   gfx_quartz_window.desc = driver_desc;

   osx_mouse_tracking_rect = [qd_view addTrackingRect: rect
      owner: NSApp
      userData: nil
      assumeInside: YES];

   osx_keyboard_focused(FALSE, 0);
   clear_keybuf();
   osx_gfx_mode = OSX_GFX_WINDOW;
   osx_skip_mouse_move = TRUE;
   osx_window_first_expose = TRUE;

   return pseudo_screen;
}

static BITMAP *osx_qz_window_init(int w, int h, int v_w, int v_h, int color_depth)
{
   BITMAP *bmp = NULL;
   OSX_WINDOW_INIT_REQUEST req;

   _unix_lock_mutex(osx_skip_events_processing_mutex);
   osx_skip_events_processing = TRUE;
   _unix_unlock_mutex(osx_skip_events_processing_mutex);

   if ([NSThread isMainThread]) {
      bmp = private_osx_qz_window_init(w, h, v_w, v_h, color_depth);
   }
   else {
      req.w = w;
      req.h = h;
      req.v_w = v_w;
      req.v_h = v_h;
      req.color_depth = color_depth;
      req.result = NULL;
      [AllegroWindowThreadProxy performSelectorOnMainThread:
         @selector(createWindowOnMainThread:)
         withObject: [NSValue valueWithPointer: &req]
         waitUntilDone: YES];
      bmp = req.result;
   }

   _unix_lock_mutex(osx_skip_events_processing_mutex);
   osx_skip_events_processing = FALSE;
   _unix_unlock_mutex(osx_skip_events_processing_mutex);

   if (!bmp)
      osx_qz_window_exit(bmp);
   return bmp;
}

static void private_osx_qz_window_exit(BITMAP *bmp)
{
   (void)bmp;

   /* Free CoreGraphics pixel buffer */
   if (cg_pixel_buffer) {
      free(cg_pixel_buffer);
      cg_pixel_buffer = NULL;
      cg_pixel_buffer_pitch = 0;
   }

   if (osx_window) {
      [osx_window close];
      osx_window = NULL;
   }

   if (pseudo_screen_addr) {
      free(pseudo_screen_addr);
      pseudo_screen_addr = NULL;
   }

   if (dirty_lines) {
      free(dirty_lines);
      dirty_lines = NULL;
   }

   if (colorconv_blitter) {
      _release_colorconv_blitter(colorconv_blitter);
      colorconv_blitter = NULL;
   }

   _unix_destroy_mutex(osx_window_mutex);
   pthread_cond_destroy(&vsync_cond);
   pthread_mutex_destroy(&vsync_mutex);

   osx_mouse_tracking_rect = -1;
   osx_gfx_mode = OSX_GFX_NONE;
}

static void osx_qz_window_exit(BITMAP *bmp)
{
   (void)bmp;

   _unix_lock_mutex(osx_skip_events_processing_mutex);
   osx_skip_events_processing = TRUE;
   _unix_unlock_mutex(osx_skip_events_processing_mutex);

   if ([NSThread isMainThread]) {
      private_osx_qz_window_exit(NULL);
   }
   else {
      [AllegroWindowThreadProxy performSelectorOnMainThread:
         @selector(destroyWindowOnMainThread:)
         withObject: nil
         waitUntilDone: YES];
   }
}



/* osx_qz_window_vsync: vertical sync for windowed mode. */
static void osx_qz_window_vsync(void)
{
   if (lock_nesting == 0) {
      pthread_mutex_trylock(&vsync_mutex);
      pthread_cond_wait(&vsync_cond, &vsync_mutex);
      pthread_mutex_unlock(&vsync_mutex);
   } else {
      ASSERT(0);
   }
}



/* osx_qz_window_set_palette: sets palette for quartz window. */
static void osx_qz_window_set_palette(AL_CONST struct RGB *p, int from, int to, int vsync)
{
   if (vsync)
      osx_qz_window_vsync();
   _unix_lock_mutex(osx_window_mutex);
   _set_colorconv_palette(p, from, to);
   memset(dirty_lines, 1, gfx_quartz_window.h);
   _unix_unlock_mutex(osx_window_mutex);
}

void osx_signal_vsync(void)
{
   pthread_mutex_lock(&vsync_mutex);
   pthread_cond_broadcast(&vsync_cond);
   pthread_mutex_unlock(&vsync_mutex);
}

static BITMAP* osx_create_video_bitmap(int w, int h)
{
   if ((w == gfx_quartz_window.w) && (h == gfx_quartz_window.h)) {
      if (first_page == NULL) {
         first_page = create_video_page(pseudo_screen_addr);
         return first_page;
      } else {
         return create_video_page(NULL);
      }
   } else {
      return create_bitmap(w, h);
   }
}

static BITMAP* create_video_page(unsigned char* addr)
{
   BITMAP* page;
   int i;
   int w = gfx_quartz_window.w, h = gfx_quartz_window.h;

   page = (BITMAP*) _AL_MALLOC(sizeof(BITMAP) + sizeof(char*) * h);
   if (!page) {
      ustrzcpy(allegro_error, ALLEGRO_ERROR_SIZE, get_config_text("Not enough memory"));
      return NULL;
   }
   if (addr == NULL) {
      addr = _AL_MALLOC(gfx_quartz_window.vid_mem);
      page->dat = addr;
      page->vtable = &_unspecial_vtable;
      page->write_bank = page->read_bank = _stub_bank_switch;
   } else {
      page->dat = NULL;
      page->vtable = &_special_vtable;
      page->write_bank = page->read_bank = osx_qz_write_line_win;
   }
   if (!addr) {
      ustrzcpy(allegro_error, ALLEGRO_ERROR_SIZE, get_config_text("Not enough memory"));
      return NULL;
   }
   page->w = page->cr = w;
   page->h = page->cb = h;
   page->clip = TRUE;
   page->cl = page->ct = 0;
   page->id = BMP_ID_VIDEO;
   page->extra = NULL;
   page->x_ofs = 0;
   page->y_ofs = 0;
   page->seg = _video_ds();
   for (i = 0; i < h; ++i) {
      page->line[i] = addr;
      addr += pseudo_screen_pitch;
   }
   return page;
}

static int osx_show_video_bitmap(BITMAP* vb)
{
   if (vb->vtable == &_special_vtable)
      return 0;
   if (vb->vtable == &_unspecial_vtable) {
      _unix_lock_mutex(osx_window_mutex);
      if ((current_video_page == pseudo_screen) && (first_page != NULL)) {
         first_page->vtable = &_unspecial_vtable;
         first_page->write_bank = first_page->read_bank = _stub_bank_switch;
      } else if ((current_video_page == first_page) && (first_page != NULL)) {
         pseudo_screen->vtable = &_unspecial_vtable;
         pseudo_screen->write_bank = pseudo_screen->read_bank = _stub_bank_switch;
      }
      if (current_video_page != NULL) {
         current_video_page->vtable = &_unspecial_vtable;
         current_video_page->write_bank = current_video_page->read_bank = _stub_bank_switch;
      }
      if ((vb == pseudo_screen) && (first_page != NULL)) {
         first_page->vtable = &_special_vtable;
         first_page->write_bank = first_page->read_bank = osx_qz_write_line_win;
      } else if (vb == first_page) {
         pseudo_screen->vtable = &_special_vtable;
         pseudo_screen->write_bank = pseudo_screen->read_bank = osx_qz_write_line_win;
      }
      pseudo_screen_addr = vb->line[0];
      vb->vtable = &_special_vtable;
      vb->write_bank = vb->read_bank = osx_qz_write_line_win;
      current_video_page = vb;
      memset(dirty_lines, 1, gfx_quartz_window.h);
      _unix_unlock_mutex(osx_window_mutex);
      return 0;
   }
   return -1;
}

static void osx_destroy_video_bitmap(BITMAP* bm)
{
   if (bm == pseudo_screen)
      return;
   if (bm == first_page)
      first_page = NULL;
   if (bm->vtable == &_special_vtable) {
      osx_show_video_bitmap(pseudo_screen);
      free(bm->dat);
      free(bm);
   } else if (bm->vtable == &_unspecial_vtable) {
      free(bm->dat);
      free(bm);
   }
}

/* Local variables:       */
/* c-basic-offset: 3      */
/* indent-tabs-mode: nil  */
/* End:                   */

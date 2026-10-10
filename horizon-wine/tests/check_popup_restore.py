#!/usr/bin/env python3
"""Run the NX driver's actual popup blits against a host framebuffer."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/win32u/winnx_drv.c').read_text()
if '--baseline' in sys.argv:
    source = subprocess.check_output(['git', '-C', str(root), 'show', 'HEAD:dlls/win32u/winnx_drv.c'], text=True)
core = source[source.index('struct wine_nx_surface\n'):source.index('/**********************************************************************')]
position = source[source.index('void wine_nx_drv_WindowPosChanged('):]
fixture = r'''
#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef int BOOL, LONG;
typedef unsigned int UINT, DWORD;
typedef uintptr_t HWND, ULONG_PTR;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { struct { int biWidth, biHeight; } bmiHeader; DWORD bmiColors[256]; } BITMAPINFO;
#define TRUE 1
#define FALSE 0
#define FIELD_OFFSET(t,f) offsetof(t,f)
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p) - offsetof(t,f)))
#define WS_POPUP 0x80000000u
#define GWL_STYLE -16
#define GW_HWNDNEXT 2
#define GW_OWNER 4
#define GW_CHILD 5
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define GA_ROOT 2
#define SWP_HIDEWINDOW 0x80
#define RDW_INVALIDATE 1
#define RDW_ERASE 4
#define RDW_FRAME 0x400
#define RDW_ALLCHILDREN 0x80
#define RDW_UPDATENOW 0x100
struct window_rects { RECT window, client, visible; };
struct window_surface;
struct window_surface_funcs {
    UINT size;
    void (*set_clip)(struct window_surface *, const RECT *, UINT);
    BOOL (*flush)(struct window_surface *, const RECT *, const RECT *, const BITMAPINFO *, const void *, BOOL, const BITMAPINFO *, const void *);
    void (*destroy)(struct window_surface *);
};
struct window_surface {
    const struct window_surface_funcs *funcs;
    HWND hwnd;
    RECT rect, bounds;
    DWORD *pixels;
};
static DWORD framebuffer[320*300];
static int presents;
static struct window_surface *surfaces[5];
struct ratio { UINT num, den; };
/* The compositor's side of a window: where it shows and what it was given. */
struct wine_nx_layer { int x, y, w, h, updates, destroyed; };
static struct wine_nx_layer *restacked[8];
static int restacks, restacked_count;
static void wine_nx_layer_place(struct wine_nx_layer *l, int x, int y, int w, int h) { l->x = x; l->y = y; l->w = w; l->h = h; }
static void wine_nx_layer_update(struct wine_nx_layer *l, const void *bits, int stride, int left, int top, int right, int bottom) {
    assert(bits && stride > 0 && left < right && top < bottom); l->updates++;
}
static void wine_nx_layer_destroy(struct wine_nx_layer *l) { l->destroyed = 1; }
static void wine_nx_compositor_restack(struct wine_nx_layer **layers, int count) {
    assert(count <= 8); memcpy(restacked, layers, count * sizeof(*layers)); restacked_count = count; restacks++;
}
static void SetRectEmpty(RECT *r) { memset(r, 0, sizeof(*r)); }
static int IsRectEmpty(const RECT *r) { return r->left >= r->right || r->top >= r->bottom; }
static void OffsetRect(RECT *r, int x, int y) { r->left += x; r->right += x; r->top += y; r->bottom += y; }
static int intersect_rect(RECT *out, const RECT *a, const RECT *b) {
    RECT r = {a->left > b->left ? a->left : b->left, a->top > b->top ? a->top : b->top,
              a->right < b->right ? a->right : b->right, a->bottom < b->bottom ? a->bottom : b->bottom};
    *out = r; return !IsRectEmpty(out);
}
static void union_rect(RECT *out, const RECT *a, const RECT *b) {
    RECT r = {a->left < b->left ? a->left : b->left, a->top < b->top ? a->top : b->top,
              a->right > b->right ? a->right : b->right, a->bottom > b->bottom ? a->bottom : b->bottom}; *out = r;
}
static void wine_nx_runtime_trace(const char *s) {}
static int wine_nx_runtime_verbose;
static void *wine_nx_fb_lock(int *w, int *h, int *stride) { *w = *stride = 320; *h = 300; return framebuffer; }
static void wine_nx_fb_unlock(void) {}
static void wine_nx_fb_present(void) { presents++; }
static int wine_nx_pointer_poll(int *x, int *y, unsigned int *buttons) { *x = *y = 0; *buttons = 0; return 0; }
static void window_surface_lock(struct window_surface *s) {}
static void window_surface_unlock(struct window_surface *s) {}
static void window_surface_release(struct window_surface *s) {}
static void window_surface_flush(struct window_surface *s) { assert(!"unexpected fallback"); }
static void *window_surface_get_color(struct window_surface *s, BITMAPINFO *info) {
    info->bmiHeader.biWidth = s->rect.right; info->bmiHeader.biHeight = -s->rect.bottom; return s->pixels;
}
static struct window_surface *window_surface_get(HWND h) { return h < 5 ? surfaces[h] : NULL; }
static void get_win_monitor_dpi(HWND h, struct ratio *dpi) { dpi->num = dpi->den = 1; }
static struct window_surface *get_driver_window_surface(struct window_surface *s, struct ratio dpi) { return s; }
static DWORD get_window_long(HWND h, int offset) { return h > 1 ? WS_POPUP : 0; }
static HWND get_desktop_window(void) { return 100; }
/* Top-level windows from the top: 4, then 1. */
static HWND get_window_relative(HWND h, UINT rel) {
    if (rel == GW_CHILD) return h == 100 ? 4 : 0;
    if (rel == GW_HWNDNEXT) return h == 4 ? 1 : 0;
    return h > 1 ? 1 : 0;
}
static HWND NtUserWindowFromPoint(int x, int y) { return 1; }
static HWND get_active_window(void) { return 1; }
static HWND get_focus(void) { return 1; }
static HWND NtUserGetAncestor(HWND h, UINT type) { return h; }
static void NtUserRedrawWindow(HWND h, void *r, int hrgn, UINT flags) { assert(!"unexpected redraw fallback"); }
'''
tests = r'''
static struct wine_nx_surface *create(HWND h, int w, int height, DWORD color) {
    struct wine_nx_surface *s = calloc(1, sizeof(*s));
    s->surface.funcs = &wine_nx_surface_funcs; s->surface.hwnd = h;
    s->surface.rect = (RECT){0, 0, w, height};
    s->surface.pixels = malloc(w * height * sizeof(DWORD));
    for (int i = 0; i < w * height; ++i) s->surface.pixels[i] = color;
    s->initial_redraw_done = TRUE;
    surfaces[h] = &s->surface;
    return s;
}
static void position(HWND h, int x, int y, UINT flags) {
    struct window_surface *s = surfaces[h];
    struct window_rects rects = {.visible = {x, y, x+s->rect.right, y+s->rect.bottom}};
    wine_nx_drv_WindowPosChanged(h, 0, 1, flags, &rects, s);
}
int main(void) {
    create(1, 320, 300, 0x00112233);
    create(2, 126, 220, 0x00ff0000);
    create(3, 93, 40, 0x0000ff00);
    position(1, 0, 0, 0);
    position(2, 32, 42, 0);
    assert(framebuffer[100*320+50] == 0xff0000ff);
    int before = presents;
    position(2, 32, 42, SWP_HIDEWINDOW);
    assert(framebuffer[100*320+50] == 0xff332211);
    assert(presents > before);  /* the owner's flush publishes, and so does the hide */
    /* A pending paint for Edit must not bring it back after closing. */
    wine_nx_surface_present_full(surfaces[2]);
    assert(framebuffer[100*320+50] == 0xff332211);
    position(3, 62, 42, 0);
    assert(framebuffer[50*320+70] == 0xff00ff00);
    assert(framebuffer[100*320+50] == 0xff332211);
    position(3, 62, 42, SWP_HIDEWINDOW);
    wine_nx_surface_present_full(surfaces[3]);
    for (int i = 0; i < 320*300; ++i) assert(framebuffer[i] == 0xff332211);
    /* Showing the same cached surface again must re-enable its flushes. */
    position(2, 32, 42, 0);
    assert(framebuffer[100*320+50] == 0xff0000ff);
    /* With the compositor a surface feeds a layer instead: the window shows
     * where the layer is placed, the framebuffer is not drawn on, and hiding
     * it is taking the layer away - what was under it needs no repainting. */
    struct wine_nx_layer layer = {0};
    struct wine_nx_surface *top = create(4, 50, 40, 0x00abcdef);
    top->layer = &layer;
    DWORD under = framebuffer[60*320+210];
    position(4, 200, 50, 0);
    assert(layer.x == 200 && layer.y == 50 && layer.w == 50 && layer.h == 40 && layer.updates == 1);
    assert(framebuffer[60*320+210] == under);
    assert(restacks == 1 && restacked_count == 1 && restacked[0] == &layer);
    before = presents;
    position(4, 200, 50, SWP_HIDEWINDOW);
    assert(!layer.w && !layer.h && presents == before && framebuffer[60*320+210] == under);
    /* A layer that is gone is not placed or stacked again. */
    wine_nx_surface_funcs.destroy(&top->surface);
    assert(layer.destroyed && !top->layer);
    wine_nx_restack_layers();
    assert(restacks == 2 && !restacked_count);
    for (int i = 1; i < 5; ++i) { free(surfaces[i]->pixels); free(surfaces[i]); }
    while (wine_nx_surface_entries) {
        struct wine_nx_surface_entry *next = wine_nx_surface_entries->next;
        free(wine_nx_surface_entries); wine_nx_surface_entries = next;
    }
    puts("PASS: menu hide restores pixels and presents; late hidden flushes suppressed; next menu and reopen; "
         "a compositor layer is placed, updated, hidden and forgotten");
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-popup-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(fixture + core + position + tests)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-address', '-fsanitize=address,undefined', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

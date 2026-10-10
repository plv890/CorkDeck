#!/usr/bin/env python3
"""Run the NX driver's pointer event translation against scripted controller states."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/win32u/winnx_drv.c').read_text()
defines = source[source.index('/* Buttons reported by wine_nx_pointer_poll(). */'):source.index('struct wine_nx_surface\n')]
events = source[source.index('/* The button flags that take the delivered buttons'):source.index('/**********************************************************************\n *           wine_nx_drv_CreateWindow')]
fixture = r'''
#include <assert.h>
#include <stdio.h>
typedef int BOOL, INT;
typedef unsigned int UINT, DWORD;
typedef void *HWND, *HCURSOR;
typedef int NTSTATUS;
typedef unsigned int user_handle_t;
#define STATUS_PENDING 0x103
struct object_lock { int id; };
#define OBJECT_LOCK_INIT {0}
typedef struct { user_handle_t cursor; int cursor_count; } input_shm_t;
#define wine_server_ptr_handle(h) ((void *)(unsigned long)(h))
/* The input state the program's SetCursor and ShowCursor calls leave. */
static input_shm_t shared_input;
static NTSTATUS shared_input_status;
static NTSTATUS get_shared_input(unsigned int tid, struct object_lock *lock, const input_shm_t **input_shm) {
    assert(!tid);
    if (shared_input_status) return shared_input_status;
    if (!lock->id) { lock->id = 1; *input_shm = &shared_input; return STATUS_PENDING; }
    return 0;
}
static int cursor_shown = 1;
static void wine_nx_cursor_show(int visible) { cursor_shown = visible; }
typedef long LPARAM;
typedef void *HKL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef struct { int x, y; } POINT;
#define TRUE 1
#define FALSE 0
#define INPUT_MOUSE 0
#define INPUT_KEYBOARD 1
#define MOUSEEVENTF_MOVE 0x0001
#define MOUSEEVENTF_LEFTDOWN 0x0002
#define MOUSEEVENTF_LEFTUP 0x0004
#define MOUSEEVENTF_RIGHTDOWN 0x0008
#define MOUSEEVENTF_RIGHTUP 0x0010
#define MOUSEEVENTF_ABSOLUTE 0x8000
#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP 0x0002
#define MAPVK_VK_TO_VSC_EX 4
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
typedef struct { int dx, dy; DWORD mouseData, dwFlags, time; unsigned long dwExtraInfo; } MOUSEINPUT;
typedef struct { WORD wVk, wScan; DWORD dwFlags, time; unsigned long dwExtraInfo; } KEYBDINPUT;
typedef struct { DWORD type; MOUSEINPUT mi; KEYBDINPUT ki; } INPUT;
/* Where the server holds the cursor after the movement it was told. */
typedef struct { POINT cursor; } desktop_shm_t;
static desktop_shm_t shared_desktop;
static NTSTATUS get_shared_desktop(struct object_lock *lock, const desktop_shm_t **desktop_shm) {
    if (!lock->id) { lock->id = 1; *desktop_shm = &shared_desktop; return STATUS_PENDING; }
    return 0;
}
/* What the runtime's polls saw since the last take (the background thread's included). */
static struct { int moved, placed, stepped, x, y, dx, dy; unsigned int held, pressed, released; } state;
static INPUT sent[32];
static int sent_count, presents, polls, set_x = -1, set_y = -1, follows, follow_x, follow_y;
static int wine_nx_pointer_poll(int *x, int *y, unsigned int *buttons) {
    polls++; *x = state.x; *y = state.y; *buttons = state.held; return state.moved;
}
static int wine_nx_pointer_take(int *x, int *y, unsigned int *buttons, unsigned int *pressed, unsigned int *released) {
    *x = state.x; *y = state.y; *buttons = state.held; *pressed = state.pressed; *released = state.released;
    return state.moved;
}
static int wine_nx_pointer_take_placed(void) { return state.placed; }
static int wine_nx_pointer_take_motion(int *dx, int *dy) { *dx = state.dx; *dy = state.dy; return state.stepped; }
static void wine_nx_pointer_follow(int x, int y) { follows++; follow_x = x; follow_y = y; }
static void wine_nx_pointer_set_pos(int x, int y) { set_x = x; set_y = y; }
static void wine_nx_fb_present(void) { presents++; }
extern void wine_nx_runtime_trace(const char *line) __attribute__((weak));
static void nxdrv_trace(const char *fmt, int a, int b, int c, int d) {}
static void nxdrv_trace_hot(const char *fmt, int a, int b, int c, int d) {}
static HKL NtUserGetKeyboardLayout(DWORD thread) { return 0; }
/* An arrow key has an extended scan code. */
static UINT NtUserMapVirtualKeyEx(UINT code, UINT type, HKL layout) {
    assert(type == MAPVK_VK_TO_VSC_EX);
    return code == 0x26 ? 0xe048 : 0x1e;
}
static UINT NtUserSendHardwareInput(HWND hwnd, UINT flags, const INPUT *input, LPARAM lparam) {
    assert(!hwnd && !flags && !lparam && sent_count < 32);
    assert(input->type == INPUT_MOUSE || input->type == INPUT_KEYBOARD);
    sent[sent_count++] = *input; return TRUE;
}
'''
tests = r'''
unsigned int wine_nx_pad_key_state;
unsigned short wine_nx_pad_keys[WINE_NX_PAD_KEY_COUNT];
static int traces;
void wine_nx_runtime_trace(const char *line) { traces++; }
static int motions, keys;
/* Call ProcessEvents once with what the polls saw; return the flags of the
 * events sent at a place on the screen (second << 32 | first), or -1 for
 * none. Movement by an amount and keys are counted aside. */
static long long take(int moved, int x, int y, unsigned int held, unsigned int pressed, unsigned int released) {
    long long flags = 0;
    int before = sent_count, places = 0, i;
    BOOL ret;
    state.moved = moved; state.x = x; state.y = y;
    state.held = held; state.pressed = pressed; state.released = released;
    motions = keys = 0;
    ret = wine_nx_drv_ProcessEvents(0);
    for (i = before; i < sent_count; i++) {
        if (sent[i].type == INPUT_KEYBOARD) { keys++; continue; }
        if (!(sent[i].mi.dwFlags & MOUSEEVENTF_ABSOLUTE)) {
            /* between the two steps of the buttons */
            assert(sent[i].mi.dwFlags == MOUSEEVENTF_MOVE && state.stepped && !motions++);
            assert(sent[i].mi.dx == state.dx && sent[i].mi.dy == state.dy);
            assert(places <= 1);
            continue;
        }
        assert(sent[i].mi.dx == x && sent[i].mi.dy == y && places < 2);
        flags |= (long long)sent[i].mi.dwFlags << (32 * places++);
    }
    assert(motions == state.stepped);
    assert(ret == (moved || places || keys));
    state.placed = state.stepped = 0;
    return places ? flags : -1;
}
/* One poll between calls: the edges follow from the previous buttons. */
static unsigned int previous;
static long long poll(int moved, int x, int y, unsigned int buttons) {
    long long flags = take(moved, x, y, buttons, buttons & ~previous, previous & ~buttons);
    previous = buttons;
    return flags;
}
/* The stick moved the arrow by an amount. */
static long long step(int x, int y, int dx, int dy, unsigned int buttons) {
    state.stepped = 1; state.dx = dx; state.dy = dy;
    shared_desktop.cursor.x = x; shared_desktop.cursor.y = y;
    return poll(1, x, y, buttons);
}
/* A touch put the arrow at a place. */
static long long touch(int x, int y, unsigned int buttons) {
    state.placed = 1;
    shared_desktop.cursor.x = x; shared_desktop.cursor.y = y;
    return poll(1, x, y, buttons);
}
int main(void) {
    enum { L = WINE_NX_POINTER_LEFT, R = WINE_NX_POINTER_RIGHT };
    const long long ABS = MOUSEEVENTF_ABSOLUTE;
    assert(poll(0, 640, 360, 0) == -1);
    assert(presents == 2 && polls == 1);  /* presents before and after; polls even with the background thread */
    /* The stick tells an amount, not a place. */
    assert(step(700, 360, 60, 0, 0) == -1 && motions == 1);
    /* A clicks where the cursor is, without a spurious move. */
    assert(poll(0, 700, 360, L) == (ABS | MOUSEEVENTF_LEFTDOWN) && !motions);
    assert(poll(0, 700, 360, L) == -1);
    /* Holding A while the stick moves drags. */
    assert(step(720, 380, 20, 20, L) == -1 && motions == 1);
    assert(poll(0, 720, 380, L | R) == (ABS | MOUSEEVENTF_RIGHTDOWN));
    assert(poll(0, 720, 380, R) == (ABS | MOUSEEVENTF_LEFTUP));
    assert(poll(0, 720, 380, 0) == (ABS | MOUSEEVENTF_RIGHTUP));
    /* B alone is a right click. */
    assert(poll(0, 720, 380, R) == (ABS | MOUSEEVENTF_RIGHTDOWN));
    assert(poll(0, 720, 380, 0) == (ABS | MOUSEEVENTF_RIGHTUP));
    /* A touch elsewhere moves and presses in one event; lifting releases. */
    assert(touch(100, 50, L) == (ABS | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN) && !motions);
    assert(poll(0, 100, 50, 0) == (ABS | MOUSEEVENTF_LEFTUP));
    /* A tapped between two calls (seen only by the background thread) still clicks. */
    assert(take(0, 100, 50, 0, L, L) == (ABS | MOUSEEVENTF_LEFTDOWN | (ABS | MOUSEEVENTF_LEFTUP) << 32));
    /* ...also while the stick moves, and for B: down, the movement, up. */
    state.stepped = 1; state.dx = 10; state.dy = 0;
    shared_desktop.cursor.x = 110; shared_desktop.cursor.y = 50;
    assert(take(1, 110, 50, 0, R, R) == (ABS | MOUSEEVENTF_RIGHTDOWN | (ABS | MOUSEEVENTF_RIGHTUP) << 32));
    /* A held, released and pressed again between calls: up, then down. */
    assert(take(0, 110, 50, L, L, 0) == (ABS | MOUSEEVENTF_LEFTDOWN));
    assert(take(0, 110, 50, L, L, L) == (ABS | MOUSEEVENTF_LEFTUP | (ABS | MOUSEEVENTF_LEFTDOWN) << 32));
    /* Nothing new: nothing sent. */
    assert(take(0, 110, 50, L, 0, 0) == -1);
    /* Another thread took the release: the state still catches up. */
    assert(take(0, 110, 50, 0, 0, 0) == (ABS | MOUSEEVENTF_LEFTUP));
    /* The server keeps the cursor where it was (clipped, or held by a program
     * that took the mouse): the movement was told in full, and the arrow goes
     * back to where the cursor is. */
    assert(!follows && !traces);
    state.stepped = 1; state.dx = 40; state.dy = -5;
    assert(take(1, 150, 45, 0, 0, 0) == -1);
    assert(follows == 1 && follow_x == 110 && follow_y == 50 && traces == 1);
    /* The arrow follows the cursor the program set: shown until a program sets
     * one, hidden when it sets none over its own cursor or hides it. */
    assert(cursor_shown);
    shared_input.cursor = 0x10010;           /* a class cursor */
    take(0, 110, 50, 0, 0, 0);
    assert(cursor_shown);
    shared_input.cursor = 0;                 /* SetCursor(NULL), as OpenTTD over its own */
    take(0, 110, 50, 0, 0, 0);
    assert(!cursor_shown);
    shared_input.cursor = 0x10010;
    shared_input.cursor_count = -1;          /* ShowCursor(FALSE) */
    take(0, 110, 50, 0, 0, 0);
    assert(!cursor_shown);
    shared_input.cursor_count = 0;
    wine_nx_drv_SetCursor(0, (HCURSOR)0x10010);
    assert(cursor_shown);
    shared_input_status = (NTSTATUS)0xc0000034;  /* unreadable: leave it as it is */
    shared_input.cursor = 0;
    take(0, 110, 50, 0, 0, 0);
    assert(cursor_shown);
    shared_input_status = 0;
    shared_input.cursor = 0x10010;
    assert(wine_nx_drv_SetCursorPos(12, 34) && set_x == 12 && set_y == 34);
    /* A control mapped to a key presses and releases it, with the scan code
     * DirectInput reads; an arrow's is extended. Unmapped controls send nothing. */
    wine_nx_pad_keys[3] = 0x26;              /* VK_UP */
    wine_nx_pad_keys[5] = 0x41;
    wine_nx_pad_key_state = 1u << 3 | 1u << 5 | 1u << 7;
    assert(take(0, 110, 50, 0, 0, 0) == -1 && keys == 2);
    assert(sent[sent_count - 2].ki.wVk == 0x26 && sent[sent_count - 2].ki.wScan == 0x48);
    assert(sent[sent_count - 2].ki.dwFlags == KEYEVENTF_EXTENDEDKEY);
    assert(sent[sent_count - 1].ki.wVk == 0x41 && sent[sent_count - 1].ki.wScan == 0x1e && !sent[sent_count - 1].ki.dwFlags);
    assert(take(0, 110, 50, 0, 0, 0) == -1 && !keys);
    sent_count = 0;
    wine_nx_pad_key_state = 1u << 5;
    assert(take(0, 110, 50, 0, 0, 0) == -1 && keys == 1);
    assert(sent[0].ki.wVk == 0x26 && sent[0].ki.dwFlags == (KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY));
    /* Without the floating keyboard in the program nothing opens. */
    assert(!wine_nx_drv_ShowSoftwareKeyboard(0));
    puts("PASS: stick movement by an amount, A left and B right clicks, drag, touch press, clicks between calls, "
         "arrow kept at the cursor, cursor visibility, SetCursorPos and controls mapped to keys");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-pointer-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(fixture + defines + events + tests)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

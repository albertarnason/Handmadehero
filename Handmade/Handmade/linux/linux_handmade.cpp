/*
    Linux/Wayland platform layer for Handmade Hero
    Mirrors the win32 layer structure by Casey Muratori.

    Display backend replaced:
      X11 + XImage  ->  Wayland + wl_shm shared-memory buffers

    Everything else is unchanged from the X11 version:
      - ALSA for audio
      - /dev/input/js* for gamepads
      - mmap / munmap for memory
      - clock_gettime for timing
      - __builtin_ia32_rdtsc for cycle counting

    Build:
      g++ linux_handmade.cpp xdg-shell-client-protocol.c handmade.cpp -o handmade \
          -lwayland-client -lxkbcommon -lasound \
          -std=c++11 -O2

    Protocol XML -> C headers (run once, or use system-installed headers):
      wayland-scanner client-header \
          /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
          > xdg-shell-client-protocol.h
      wayland-scanner private-code \
          /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
          > xdg-shell-client-protocol.c

    TODO (mirrors win32 TODO):
      - Saved game locations
      - Handle to own executable (readlink /proc/self/exe)
      - Asset loading path
      - Threading (pthreads)
      - Raw keyboard / pointer (wl_seat, zwp_relative_pointer)
      - Fullscreen (xdg_toplevel_set_fullscreen)
      - Custom cursor (wl_cursor / hide cursor)
      - Aspect ratio correction in blit
      - Hardware acceleration (EGL + OpenGL ES via wl_egl_window)
      - International keyboards (xkb_keymap already in use below)
      - Gamepad hot-plug (inotify on /dev/input)
      - Sleep precision (clock_nanosleep)
      - Pointer confinement (zwp_pointer_constraints_v1)
*/

#include "handmade.h"
#include "handmade.cpp"

// Wayland core
#include <wayland-client.h>

// XDG shell — window management on Wayland.
// These headers are generated from xdg-shell.xml (see build instructions above).
#include "xdg-shell-client-protocol.h"

// Keyboard symbol decoding (replaces XLookupKeysym)
#include <xkbcommon/xkbcommon.h>

// ALSA (unchanged from X11 version)
#include <alsa/asoundlib.h>

// Joystick (unchanged from X11 version)
#include <linux/joystick.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Macros  (Casey style)
// ---------------------------------------------------------------------------
#define global_variable  static
#define internal         static
#define local_persist    static

#define ArrayCount(arr) (sizeof(arr) / sizeof((arr)[0]))
#define Kilobytes(n)    ((n)          * 1024LL)
#define Megabytes(n)    (Kilobytes(n) * 1024LL)
#define Gigabytes(n)    (Megabytes(n) * 1024LL)

// ---------------------------------------------------------------------------
//  WAYLAND OFFSCREEN BUFFER
//
//  X11 path:     XImage wrapping our mmap'd pixels, blitted with XPutImage.
//  Wayland path: wl_shm pool -> wl_buffer wrapping the same mmap'd pixels.
//
//  We create TWO wl_buffers (double-buffer) so the compositor can hold
//  one while we write the other.
//
//  The shared-memory file is created with memfd_create(). The compositor
//  maps it read-only; we map it read-write.  Zero extra copies.
// ---------------------------------------------------------------------------
struct wayland_shm_buffer
{
    wl_buffer *WlBuffer;
    void      *Memory;
    bool       InUse;   // true while compositor is scanning out this slot
};

struct linux_offscreen_buffer
{
    wayland_shm_buffer Slots[2];
    int                CurrentSlot;

    wl_shm_pool *ShmPool;
    int          ShmFd;
    void        *ShmBase;   // full mmap of the shm file (both slots)

    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
    int SlotBytes;          // Width * Height * BytesPerPixel per slot
};

// ---------------------------------------------------------------------------
// Sound  (ALSA, identical to X11 version)
// ---------------------------------------------------------------------------
struct linux_sound_output
{
    int               SamplesPerSecond;
    uint32            RunningSampleIndex;
    int               BytesPerSample;
    int               SecondaryBufferSize;
    int               LatencySampleCount;
    snd_pcm_t        *PCMHandle;
    snd_pcm_uframes_t PeriodSize;
};

// ---------------------------------------------------------------------------
// Joystick  (identical to X11 version)
// ---------------------------------------------------------------------------
#define MAX_CONTROLLERS 4
struct linux_joystick_state
{
    int    Fd;
    bool   IsAnalog;
    int16  AxisX;
    int16  AxisY;
    uint32 Buttons;
};

#define LINUX_BUTTON_A   (1u << 0)
#define LINUX_BUTTON_B   (1u << 1)
#define LINUX_BUTTON_X   (1u << 2)
#define LINUX_BUTTON_Y   (1u << 3)
#define LINUX_BUTTON_LB  (1u << 4)
#define LINUX_BUTTON_RB  (1u << 5)

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
global_variable bool                   GlobalRunning;
global_variable linux_offscreen_buffer GlobalBackBuffer;
global_variable linux_joystick_state   GlobalControllers[MAX_CONTROLLERS];

// Wayland protocol objects
global_variable wl_display    *GlobalWlDisplay;
global_variable wl_registry   *GlobalWlRegistry;
global_variable wl_compositor *GlobalWlCompositor;
global_variable wl_shm        *GlobalWlShm;
global_variable wl_seat       *GlobalWlSeat;
global_variable wl_keyboard   *GlobalWlKeyboard;
global_variable xdg_wm_base   *GlobalXdgWmBase;

global_variable wl_surface    *GlobalWlSurface;
global_variable xdg_surface   *GlobalXdgSurface;
global_variable xdg_toplevel  *GlobalXdgToplevel;

// xkb keyboard state
global_variable xkb_context   *GlobalXkbContext;
global_variable xkb_keymap    *GlobalXkbKeymap;
global_variable xkb_state     *GlobalXkbState;

// Pointer to current frame's keyboard controller (set each frame)
global_variable game_controller_input *GlobalKeyboardController;

// Window geometry
global_variable int GlobalWindowWidth  = 1280;
global_variable int GlobalWindowHeight = 720;

// ---------------------------------------------------------------------------
// Timing  (identical to X11 version)
// ---------------------------------------------------------------------------
#define LINUX_PERF_FREQ 1000000000LL

internal int64
LinuxGetWallClock()
{
    struct timespec Ts;
    clock_gettime(CLOCK_MONOTONIC, &Ts);
    return (int64)Ts.tv_sec * 1000000000LL + (int64)Ts.tv_nsec;
}

// ---------------------------------------------------------------------------
// Memory  (identical to X11 version)
// ---------------------------------------------------------------------------
internal void *
LinuxAlloc(size_t Size)
{
    void *Result = mmap(0, Size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(Result == MAP_FAILED)
    {
        fprintf(stderr, "LinuxAlloc: mmap failed (%s)\n", strerror(errno));
        return 0;
    }
    return Result;
}

internal void
LinuxFree(void *Ptr, size_t Size)
{
    if(Ptr) munmap(Ptr, Size);
}

// ---------------------------------------------------------------------------
// Shared-memory file  (replaces XCreateImage + mmap)
//
// memfd_create() gives us an anonymous RAM-backed file descriptor that can
// be passed to another process (the compositor) so it can mmap our pixels
// read-only.  This is the Wayland equivalent of the X shared-memory
// extension (MIT-SHM), but without needing an extension.
// ---------------------------------------------------------------------------
internal int
LinuxCreateShmFd(size_t Size)
{
    int Fd = memfd_create("handmade_shm", MFD_CLOEXEC);
    if(Fd < 0) { fprintf(stderr, "memfd_create: %s\n", strerror(errno)); return -1; }
    if(ftruncate(Fd, (off_t)Size) < 0)
    {
        fprintf(stderr, "ftruncate: %s\n", strerror(errno));
        close(Fd); return -1;
    }
    return Fd;
}

// ---------------------------------------------------------------------------
// wl_buffer release callback
//
// The compositor calls this when it has finished reading a slot so we may
// reuse it.  X11 had no equivalent — XPutImage was synchronous from the
// application's perspective.
// ---------------------------------------------------------------------------
internal void
WlBufferRelease(void *Data, wl_buffer *Buffer)
{
    wayland_shm_buffer *Slot = (wayland_shm_buffer *)Data;
    Slot->InUse = false;
}

global_variable const wl_buffer_listener GlobalWlBufferListener = {
    WlBufferRelease
};

// ---------------------------------------------------------------------------
// Create / destroy shm buffers
// Mirrors Win32ReSizeDIBSection / LinuxResizeDIBSection
// ---------------------------------------------------------------------------
internal void
WaylandCreateShmBuffers(linux_offscreen_buffer *Buf, int Width, int Height)
{
    Buf->Width         = Width;
    Buf->Height        = Height;
    Buf->BytesPerPixel = 4;
    Buf->Pitch         = Width * 4;
    Buf->SlotBytes     = Width * Height * 4;
    size_t TotalSize   = (size_t)Buf->SlotBytes * 2;

    Buf->ShmFd = LinuxCreateShmFd(TotalSize);
    if(Buf->ShmFd < 0) return;

    Buf->ShmBase = mmap(0, TotalSize, PROT_READ | PROT_WRITE, MAP_SHARED,
                        Buf->ShmFd, 0);
    if(Buf->ShmBase == MAP_FAILED)
    {
        fprintf(stderr, "WaylandCreateShmBuffers: mmap failed\n");
        close(Buf->ShmFd); return;
    }

    Buf->ShmPool = wl_shm_create_pool(GlobalWlShm, Buf->ShmFd, (int32_t)TotalSize);

    for(int I = 0; I < 2; ++I)
    {
        int32_t Offset = I * Buf->SlotBytes;
        Buf->Slots[I].Memory   = (uint8 *)Buf->ShmBase + Offset;
        Buf->Slots[I].InUse    = false;
        Buf->Slots[I].WlBuffer = wl_shm_pool_create_buffer(
            Buf->ShmPool, Offset, Width, Height, Buf->Pitch,
            WL_SHM_FORMAT_XRGB8888);  // same layout as win32 32-bpp BITMAPINFO
        wl_buffer_add_listener(Buf->Slots[I].WlBuffer,
                               &GlobalWlBufferListener, &Buf->Slots[I]);
    }
    Buf->CurrentSlot = 0;
}

internal void
WaylandDestroyShmBuffers(linux_offscreen_buffer *Buf)
{
    for(int I = 0; I < 2; ++I)
    {
        if(Buf->Slots[I].WlBuffer) { wl_buffer_destroy(Buf->Slots[I].WlBuffer); Buf->Slots[I].WlBuffer = 0; }
    }
    if(Buf->ShmPool)  { wl_shm_pool_destroy(Buf->ShmPool); Buf->ShmPool = 0; }
    if(Buf->ShmBase)  { munmap(Buf->ShmBase, (size_t)Buf->SlotBytes * 2); Buf->ShmBase = 0; }
    if(Buf->ShmFd >= 0) { close(Buf->ShmFd); Buf->ShmFd = -1; }
}

// ---------------------------------------------------------------------------
// Blit  (replaces Win32DisplayBufferInWindow / XPutImage)
//
// X11:     XPutImage — synchronous copy from our memory into the X server.
// Wayland: attach our wl_buffer to the surface, mark it damaged, commit.
//          The compositor then reads our pixels on its own schedule,
//          typically synced to the display's vblank via the presentation-time
//          protocol.  No data is copied — the compositor maps our memfd.
// ---------------------------------------------------------------------------
internal void
WaylandDisplayBufferInWindow(linux_offscreen_buffer *Buf)
{
    // Find a free slot
    int SlotIdx = Buf->CurrentSlot;
    if(Buf->Slots[SlotIdx].InUse)
    {
        SlotIdx = 1 - SlotIdx;
        if(Buf->Slots[SlotIdx].InUse) return; // both busy, drop frame
    }

    wayland_shm_buffer *Slot = &Buf->Slots[SlotIdx];
    Slot->InUse = true;

    // Game always writes into Slots[0].Memory; copy to the selected slot
    // if they differ.  For true zero-copy, point the game directly at the
    // active slot (requires knowing which slot is free before GameUpdateAndRender).
    if(SlotIdx != 0)
        memcpy(Slot->Memory, Buf->Slots[0].Memory, (size_t)Buf->SlotBytes);

    wl_surface_attach(GlobalWlSurface, Slot->WlBuffer, 0, 0);
    wl_surface_damage_buffer(GlobalWlSurface, 0, 0, Buf->Width, Buf->Height);
    wl_surface_commit(GlobalWlSurface);

    Buf->CurrentSlot = 1 - SlotIdx;
}

// ---------------------------------------------------------------------------
// XDG-shell ping / pong  (Wayland-specific liveness check)
//
// The compositor pings us periodically; we must pong or it may freeze our
// window.  X11 had no equivalent requirement.
// ---------------------------------------------------------------------------
internal void
XdgWmBasePing(void *Data, xdg_wm_base *Base, uint32_t Serial)
{
    xdg_wm_base_pong(Base, Serial);
}

global_variable const xdg_wm_base_listener GlobalXdgWmBaseListener = { XdgWmBasePing };

// ---------------------------------------------------------------------------
// XDG surface configure
//
// The compositor sends this before we are allowed to draw.  We must call
// ack_configure, then commit a buffer.  X11 had a ConfigureNotify event but
// required no acknowledgement.
// ---------------------------------------------------------------------------
internal void
XdgSurfaceConfigure(void *Data, xdg_surface *Surface, uint32_t Serial)
{
    xdg_surface_ack_configure(Surface, Serial);

    if(GlobalBackBuffer.Width  != GlobalWindowWidth ||
       GlobalBackBuffer.Height != GlobalWindowHeight)
    {
        WaylandDestroyShmBuffers(&GlobalBackBuffer);
        WaylandCreateShmBuffers(&GlobalBackBuffer, GlobalWindowWidth, GlobalWindowHeight);
    }

    wl_surface_commit(GlobalWlSurface);
}

global_variable const xdg_surface_listener GlobalXdgSurfaceListener = { XdgSurfaceConfigure };

// ---------------------------------------------------------------------------
// XDG toplevel configure / close
// ---------------------------------------------------------------------------
internal void
XdgToplevelConfigure(void *Data, xdg_toplevel *Toplevel,
                     int32_t Width, int32_t Height, wl_array *States)
{
    if(Width  > 0) GlobalWindowWidth  = Width;
    if(Height > 0) GlobalWindowHeight = Height;
}

internal void
XdgToplevelClose(void *Data, xdg_toplevel *Toplevel)
{
    GlobalRunning = false;
}

global_variable const xdg_toplevel_listener GlobalXdgToplevelListener = {
    XdgToplevelConfigure,
    XdgToplevelClose
};

// ---------------------------------------------------------------------------
// Keyboard input  (replaces X11 KeyPress/KeyRelease + XLookupKeysym)
//
// Wayland delivers the XKB keymap as a memfd from the compositor.
// We load it into xkbcommon and call xkb_state_key_get_one_sym() to decode
// key events — the same library GNOME, KDE and SDL use internally.
// ---------------------------------------------------------------------------
internal void
WlKeyboardKeymap(void *Data, wl_keyboard *Kbd,
                 uint32_t Format, int32_t Fd, uint32_t Size)
{
    if(Format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(Fd); return; }

    char *MapStr = (char *)mmap(0, Size, PROT_READ, MAP_SHARED, Fd, 0);
    close(Fd);
    if(MapStr == MAP_FAILED) return;

    if(GlobalXkbKeymap) xkb_keymap_unref(GlobalXkbKeymap);
    if(GlobalXkbState)  xkb_state_unref(GlobalXkbState);

    GlobalXkbKeymap = xkb_keymap_new_from_string(GlobalXkbContext, MapStr,
                                                  XKB_KEYMAP_FORMAT_TEXT_V1,
                                                  XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(MapStr, Size);
    GlobalXkbState = xkb_state_new(GlobalXkbKeymap);
}

internal void WlKeyboardEnter   (void*,wl_keyboard*,uint32_t,wl_surface*,wl_array*) {}
internal void WlKeyboardLeave   (void*,wl_keyboard*,uint32_t,wl_surface*)           {}
internal void WlKeyboardModifiers(void*,wl_keyboard*,uint32_t,
                                  uint32_t Dep,uint32_t Lat,uint32_t Lock,uint32_t Group)
{
    if(GlobalXkbState) xkb_state_update_mask(GlobalXkbState, Dep, Lat, Lock, 0, 0, Group);
}

// Mirrors LinuxProcessKeyEvent from the X11 version
internal void
WlKeyboardKey(void *Data, wl_keyboard *Kbd,
              uint32_t Serial, uint32_t Time, uint32_t Key, uint32_t State)
{
    if(!GlobalXkbState || !GlobalKeyboardController) return;

    // Wayland key codes are evdev scan codes; XKB expects evdev + 8.
    xkb_keysym_t Sym = xkb_state_key_get_one_sym(GlobalXkbState, Key + 8);
    bool IsDown = (State == WL_KEYBOARD_KEY_STATE_PRESSED);

    game_controller_input *K = GlobalKeyboardController;

    switch(Sym)
    {
        case XKB_KEY_w: case XKB_KEY_Up:
            K->Up.EndedDown = IsDown; K->Up.HalfTransitionCount++; break;
        case XKB_KEY_s: case XKB_KEY_Down:
            K->Down.EndedDown = IsDown; K->Down.HalfTransitionCount++; break;
        case XKB_KEY_a: case XKB_KEY_Left:
            K->Left.EndedDown = IsDown; K->Left.HalfTransitionCount++; break;
        case XKB_KEY_d: case XKB_KEY_Right:
            K->Right.EndedDown = IsDown; K->Right.HalfTransitionCount++; break;
        case XKB_KEY_q:
            K->LeftShoulder.EndedDown = IsDown; K->LeftShoulder.HalfTransitionCount++; break;
        case XKB_KEY_e:
            K->RightShoulder.EndedDown = IsDown; K->RightShoulder.HalfTransitionCount++; break;
        case XKB_KEY_space:
            K->Down.EndedDown = IsDown; K->Down.HalfTransitionCount++; break;
        case XKB_KEY_Escape: case XKB_KEY_F4:
            if(IsDown) GlobalRunning = false; break;
        default: break;
    }
}

internal void WlKeyboardRepeatInfo(void*,wl_keyboard*,int32_t,int32_t) {}

global_variable const wl_keyboard_listener GlobalWlKeyboardListener = {
    WlKeyboardKeymap,
    WlKeyboardEnter,
    WlKeyboardLeave,
    WlKeyboardKey,
    WlKeyboardModifiers,
    WlKeyboardRepeatInfo
};

// ---------------------------------------------------------------------------
// wl_seat  (logical input seat: keyboard + pointer + touch)
// ---------------------------------------------------------------------------
internal void
WlSeatCapabilities(void *Data, wl_seat *Seat, uint32_t Caps)
{
    if((Caps & WL_SEAT_CAPABILITY_KEYBOARD) && !GlobalWlKeyboard)
    {
        GlobalWlKeyboard = wl_seat_get_keyboard(Seat);
        wl_keyboard_add_listener(GlobalWlKeyboard, &GlobalWlKeyboardListener, 0);
    }
    else if(!(Caps & WL_SEAT_CAPABILITY_KEYBOARD) && GlobalWlKeyboard)
    {
        wl_keyboard_destroy(GlobalWlKeyboard);
        GlobalWlKeyboard = 0;
    }
}
internal void WlSeatName(void*,wl_seat*,const char*) {}

global_variable const wl_seat_listener GlobalWlSeatListener = {
    WlSeatCapabilities,
    WlSeatName
};

// ---------------------------------------------------------------------------
// Registry  (Wayland service discovery)
//
// X11: extensions discovered with XQueryExtension / XInternAtom.
// Wayland: compositor broadcasts a list of "globals" (protocol objects)
//          via the registry.  We bind the ones we need here.
// ---------------------------------------------------------------------------
internal void
WlRegistryGlobal(void *Data, wl_registry *Reg,
                 uint32_t Name, const char *Interface, uint32_t Version)
{
    if(strcmp(Interface, wl_compositor_interface.name) == 0)
        GlobalWlCompositor = (wl_compositor *)wl_registry_bind(Reg, Name, &wl_compositor_interface, 4);
    else if(strcmp(Interface, wl_shm_interface.name) == 0)
        GlobalWlShm = (wl_shm *)wl_registry_bind(Reg, Name, &wl_shm_interface, 1);
    else if(strcmp(Interface, wl_seat_interface.name) == 0)
    {
        GlobalWlSeat = (wl_seat *)wl_registry_bind(Reg, Name, &wl_seat_interface, 5);
        wl_seat_add_listener(GlobalWlSeat, &GlobalWlSeatListener, 0);
    }
    else if(strcmp(Interface, xdg_wm_base_interface.name) == 0)
    {
        GlobalXdgWmBase = (xdg_wm_base *)wl_registry_bind(Reg, Name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(GlobalXdgWmBase, &GlobalXdgWmBaseListener, 0);
    }
}

internal void WlRegistryGlobalRemove(void*,wl_registry*,uint32_t) {}

global_variable const wl_registry_listener GlobalWlRegistryListener = {
    WlRegistryGlobal,
    WlRegistryGlobalRemove
};

// ---------------------------------------------------------------------------
// ALSA sound  (unchanged from X11 version)
// ---------------------------------------------------------------------------
internal void
LinuxInitSound(linux_sound_output *S)
{
    int Err = snd_pcm_open(&S->PCMHandle, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if(Err < 0) { fprintf(stderr,"snd_pcm_open: %s\n",snd_strerror(Err)); return; }
    snd_pcm_hw_params_t *HW; snd_pcm_hw_params_alloca(&HW);
    snd_pcm_hw_params_any(S->PCMHandle, HW);
    snd_pcm_hw_params_set_access(S->PCMHandle, HW, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(S->PCMHandle, HW, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(S->PCMHandle, HW, 2);
    unsigned int Rate = (unsigned int)S->SamplesPerSecond;
    snd_pcm_hw_params_set_rate_near(S->PCMHandle, HW, &Rate, 0);
    snd_pcm_uframes_t Period = (snd_pcm_uframes_t)S->LatencySampleCount;
    snd_pcm_hw_params_set_period_size_near(S->PCMHandle, HW, &Period, 0);
    S->PeriodSize = Period;
    snd_pcm_uframes_t Buf = Period * 4;
    snd_pcm_hw_params_set_buffer_size_near(S->PCMHandle, HW, &Buf);
    snd_pcm_hw_params(S->PCMHandle, HW);
    snd_pcm_prepare(S->PCMHandle);
}

internal void
LinuxFillSoundBuffer(linux_sound_output *S, game_sound_output_buffer *Src)
{
    if(!S->PCMHandle) return;
    int16 *D = Src->Samples; int Left = Src->SampleCount;
    while(Left > 0)
    {
        snd_pcm_sframes_t W = snd_pcm_writei(S->PCMHandle, D, (snd_pcm_uframes_t)Left);
        if(W == -EAGAIN) break;
        if(W < 0) { snd_pcm_recover(S->PCMHandle,(int)W,1); break; }
        D += W*2; Left -= (int)W; S->RunningSampleIndex += (uint32)W;
    }
}

internal int
LinuxGetAvailableSoundFrames(linux_sound_output *S)
{
    if(!S->PCMHandle) return 0;
    snd_pcm_sframes_t A = snd_pcm_avail_update(S->PCMHandle);
    if(A < 0) { snd_pcm_recover(S->PCMHandle,(int)A,1); return 0; }
    return (int)A;
}

// ---------------------------------------------------------------------------
// Joystick  (unchanged from X11 version)
// ---------------------------------------------------------------------------
internal void LinuxOpenJoysticks()
{
    for(int I=0;I<MAX_CONTROLLERS;++I)
    {
        GlobalControllers[I].Fd=-1;
        char P[64]; snprintf(P,sizeof(P),"/dev/input/js%d",I);
        int Fd=open(P,O_RDONLY|O_NONBLOCK);
        if(Fd>=0){GlobalControllers[I].Fd=Fd;fprintf(stdout,"Opened %s\n",P);}
    }
}
internal void LinuxCloseJoysticks()
{
    for(int I=0;I<MAX_CONTROLLERS;++I)
        if(GlobalControllers[I].Fd>=0){close(GlobalControllers[I].Fd);GlobalControllers[I].Fd=-1;}
}
internal void LinuxPollJoysticks()
{
    for(int I=0;I<MAX_CONTROLLERS;++I)
    {
        linux_joystick_state *JS=&GlobalControllers[I];
        if(JS->Fd<0)continue;
        struct js_event Ev;
        while(read(JS->Fd,&Ev,sizeof(Ev))==sizeof(Ev))
        {
            Ev.type&=~JS_EVENT_INIT;
            if(Ev.type==JS_EVENT_AXIS){if(Ev.number==0)JS->AxisX=Ev.value;if(Ev.number==1)JS->AxisY=Ev.value;}
            else if(Ev.type==JS_EVENT_BUTTON){if(Ev.value)JS->Buttons|=(1u<<Ev.number);else JS->Buttons&=~(1u<<Ev.number);}
        }
    }
}
internal void LinuxProcessDigitalButton(uint32 S,game_button_state *O,game_button_state *N,uint32 Bit)
{N->EndedDown=((S&Bit)==Bit);N->HalfTransitionCount=(O->EndedDown!=N->EndedDown)?1:0;}

// ---------------------------------------------------------------------------
// Entry point  (replaces WinMain)
// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    // -----------------------------------------------------------------------
    // Connect to Wayland compositor
    // X11: XOpenDisplay()
    // Wayland: wl_display_connect() — connects to $WAYLAND_DISPLAY socket.
    // -----------------------------------------------------------------------
    GlobalWlDisplay = wl_display_connect(0);
    if(!GlobalWlDisplay) { fprintf(stderr,"Cannot connect to Wayland compositor\n"); return 1; }

    // -----------------------------------------------------------------------
    // Registry roundtrip — enumerate compositor capabilities
    // X11: globals were implicit.  Wayland: we must ask.
    // -----------------------------------------------------------------------
    GlobalWlRegistry = wl_display_get_registry(GlobalWlDisplay);
    wl_registry_add_listener(GlobalWlRegistry, &GlobalWlRegistryListener, 0);
    wl_display_roundtrip(GlobalWlDisplay);  // compositor sends all globals
    wl_display_roundtrip(GlobalWlDisplay);  // seat / keyboard callbacks fire

    if(!GlobalWlCompositor || !GlobalWlShm || !GlobalXdgWmBase)
    { fprintf(stderr,"Compositor missing required globals\n"); return 1; }

    // -----------------------------------------------------------------------
    // xkbcommon
    // -----------------------------------------------------------------------
    GlobalXkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    // -----------------------------------------------------------------------
    // Create window
    //
    // X11: XCreateSimpleWindow -> immediately visible.
    // Wayland:
    //   1. wl_compositor_create_surface  -> canvas with no role
    //   2. xdg_wm_base_get_xdg_surface  -> assigns the xdg_surface role
    //   3. xdg_surface_get_toplevel      -> makes it a normal desktop window
    //   4. wl_surface_commit with no buffer -> triggers configure event
    //   5. XdgSurfaceConfigure fires, we ack and commit a real buffer
    //      -> window appears
    // -----------------------------------------------------------------------
    GlobalWlSurface   = wl_compositor_create_surface(GlobalWlCompositor);
    GlobalXdgSurface  = xdg_wm_base_get_xdg_surface(GlobalXdgWmBase, GlobalWlSurface);
    xdg_surface_add_listener(GlobalXdgSurface, &GlobalXdgSurfaceListener, 0);
    GlobalXdgToplevel = xdg_surface_get_toplevel(GlobalXdgSurface);
    xdg_toplevel_add_listener(GlobalXdgToplevel, &GlobalXdgToplevelListener, 0);
    xdg_toplevel_set_title(GlobalXdgToplevel, "Handmade Hero");
    xdg_toplevel_set_app_id(GlobalXdgToplevel, "handmade_hero");

    wl_surface_commit(GlobalWlSurface);
    wl_display_roundtrip(GlobalWlDisplay);  // XdgSurfaceConfigure fires here

    if(!GlobalBackBuffer.ShmPool)
        WaylandCreateShmBuffers(&GlobalBackBuffer, GlobalWindowWidth, GlobalWindowHeight);

    // -----------------------------------------------------------------------
    // Sound
    // -----------------------------------------------------------------------
    linux_sound_output SoundOutput = {};
    SoundOutput.SamplesPerSecond    = 48000;
    SoundOutput.BytesPerSample      = sizeof(int16) * 2;
    SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;
    SoundOutput.LatencySampleCount  = SoundOutput.SamplesPerSecond / 15;
    LinuxInitSound(&SoundOutput);
    int16 *Samples = (int16 *)LinuxAlloc(SoundOutput.SecondaryBufferSize);

    // -----------------------------------------------------------------------
    // Joysticks / Game memory / Input buffers
    // -----------------------------------------------------------------------
    LinuxOpenJoysticks();

    game_memory GameMemory = {};
    GameMemory.PermanentStorageSize = Megabytes(64);
    GameMemory.PermanentStorage     = LinuxAlloc(GameMemory.PermanentStorageSize);

    if(!Samples || !GameMemory.PermanentStorage)
    { fprintf(stderr,"Failed to allocate game memory\n"); return 1; }

    game_input Input[2]  = {};
    game_input *NewInput = &Input[0];
    game_input *OldInput = &Input[1];

    int64  LastCounter    = LinuxGetWallClock();
    uint64 LastCycleCount = __builtin_ia32_rdtsc();

    // -----------------------------------------------------------------------
    // Main loop
    // -----------------------------------------------------------------------
    GlobalRunning = true;
    while(GlobalRunning)
    {
        // Carry button state from last frame
        game_controller_input *KeyboardController = &NewInput->Controllers[0];
        for(int I = 0; I < ArrayCount(KeyboardController->Buttons); ++I)
            NewInput->Controllers[0].Buttons[I].EndedDown =
                OldInput->Controllers[0].Buttons[I].EndedDown;

        // -- Dispatch Wayland events  (mirrors PeekMessage loop) --
        // wl_display_dispatch_pending processes events already queued
        // client-side without a blocking read — equivalent to PeekMessage.
        GlobalKeyboardController = KeyboardController;
        wl_display_dispatch_pending(GlobalWlDisplay);
        wl_display_flush(GlobalWlDisplay);

        // -- Joysticks --
        LinuxPollJoysticks();
        int MaxCtrl = MAX_CONTROLLERS < ArrayCount(NewInput->Controllers)
                      ? MAX_CONTROLLERS : ArrayCount(NewInput->Controllers);

        for(int I = 0; I < MaxCtrl - 1; ++I)
        {
            linux_joystick_state  *JS  = &GlobalControllers[I];
            game_controller_input *Old = &OldInput->Controllers[I+1];
            game_controller_input *New = &NewInput->Controllers[I+1];
            if(JS->Fd < 0) continue;
            New->Analog = true;
            real32 X = (JS->AxisX<0)?(real32)JS->AxisX/32768.0f:(real32)JS->AxisX/32767.0f;
            real32 Y = (JS->AxisY<0)?(real32)JS->AxisY/32768.0f:(real32)JS->AxisY/32767.0f;
            Y = -Y;
            New->StartX=Old->EndX; New->StartY=Old->EndY;
            New->MinX=Old->MaxX=New->EndX=X;
            New->MinY=Old->MaxY=New->EndY=Y;
            LinuxProcessDigitalButton(JS->Buttons,&Old->Down,         &New->Down,         LINUX_BUTTON_A);
            LinuxProcessDigitalButton(JS->Buttons,&Old->Right,        &New->Right,        LINUX_BUTTON_B);
            LinuxProcessDigitalButton(JS->Buttons,&Old->Left,         &New->Left,         LINUX_BUTTON_X);
            LinuxProcessDigitalButton(JS->Buttons,&Old->Up,           &New->Up,           LINUX_BUTTON_Y);
            LinuxProcessDigitalButton(JS->Buttons,&Old->LeftShoulder, &New->LeftShoulder, LINUX_BUTTON_LB);
            LinuxProcessDigitalButton(JS->Buttons,&Old->RightShoulder,&New->RightShoulder,LINUX_BUTTON_RB);
        }

        // -- Sound --
        int AvailFrames = LinuxGetAvailableSoundFrames(&SoundOutput);
        game_sound_output_buffer SoundBuffer = {};
        SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
        SoundBuffer.SampleCount      = AvailFrames;
        SoundBuffer.Samples          = Samples;

        // -- Game update --
        // Game writes pixels into Slots[0].Memory
        game_offscreen_buffer Buffer = {};
        Buffer.Memory = GlobalBackBuffer.Slots[0].Memory;
        Buffer.Width  = GlobalBackBuffer.Width;
        Buffer.Height = GlobalBackBuffer.Height;
        Buffer.Pitch  = GlobalBackBuffer.Pitch;

        GameUpdateAndRender(&GameMemory, NewInput, &Buffer, &SoundBuffer);
        LinuxFillSoundBuffer(&SoundOutput, &SoundBuffer);

        // -- Blit --
        WaylandDisplayBufferInWindow(&GlobalBackBuffer);
        wl_display_flush(GlobalWlDisplay);

        // -- Timing --
        uint64 EndCycles  = __builtin_ia32_rdtsc();
        int64  EndCounter = LinuxGetWallClock();
        int64  NsElapsed  = EndCounter - LastCounter;
        real64 MSPerFrame = (real64)NsElapsed / 1000000.0;
        real64 FPS        = (real64)LINUX_PERF_FREQ / (real64)NsElapsed;
        real64 MCPF       = (real64)(EndCycles - LastCycleCount) / (1000.0 * 1000.0);
        char PB[256];
        snprintf(PB,sizeof(PB),"%.02f ms/f  %.02f fps  %.02f Mcycles/f\n",MSPerFrame,FPS,MCPF);
        write(STDOUT_FILENO, PB, strlen(PB));
        LastCounter    = EndCounter;
        LastCycleCount = EndCycles;

        // -- Swap input --
        game_input *Temp = NewInput; NewInput = OldInput; OldInput = Temp;
    }

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------
    LinuxCloseJoysticks();
    if(SoundOutput.PCMHandle){ snd_pcm_drain(SoundOutput.PCMHandle); snd_pcm_close(SoundOutput.PCMHandle); }
    WaylandDestroyShmBuffers(&GlobalBackBuffer);
    LinuxFree(Samples, SoundOutput.SecondaryBufferSize);
    LinuxFree(GameMemory.PermanentStorage, GameMemory.PermanentStorageSize);
    if(GlobalXkbState)    xkb_state_unref(GlobalXkbState);
    if(GlobalXkbKeymap)   xkb_keymap_unref(GlobalXkbKeymap);
    if(GlobalXkbContext)  xkb_context_unref(GlobalXkbContext);
    if(GlobalXdgToplevel) xdg_toplevel_destroy(GlobalXdgToplevel);
    if(GlobalXdgSurface)  xdg_surface_destroy(GlobalXdgSurface);
    if(GlobalWlSurface)   wl_surface_destroy(GlobalWlSurface);
    if(GlobalWlKeyboard)  wl_keyboard_destroy(GlobalWlKeyboard);
    if(GlobalWlSeat)      wl_seat_destroy(GlobalWlSeat);
    if(GlobalXdgWmBase)   xdg_wm_base_destroy(GlobalXdgWmBase);
    if(GlobalWlShm)       wl_shm_destroy(GlobalWlShm);
    if(GlobalWlCompositor)wl_compositor_destroy(GlobalWlCompositor);
    if(GlobalWlRegistry)  wl_registry_destroy(GlobalWlRegistry);
    wl_display_disconnect(GlobalWlDisplay);
    return 0;
}
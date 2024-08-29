/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

typedef uint8_t uint8;
typedef uint32_t uint32;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned char uchar;

#define BUTTONMASK (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         \
    (mask & ~(numlock_mask|LockMask) \
    & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define MOUSEMASK (BUTTONMASK|PointerMotionMask)
#define X_INTERN_ATOM(X) XInternAtom(display, X, False)
#define LENGTH(X) (int)(sizeof(X) / sizeof(*X))
#define TAGMASK   ((1 << (LENGTH(tags))) - 1)
#define PAUSE_MILIS_AS_NANOS(X) ((X)*1000*1000)

#define TAG_DISPLAY_SIZE 32
#define ALT_TAB_GRAB_TRIES 10
#define STATUS_BUFFER_SIZE 256
#define STATUS_MAX_BLOCKS 20

#if 0
#define DWM_DEBUG(...) do { \
    error(__func__, __VA_ARGS__); \
} while (0)
#else
#define DWM_DEBUG(...)
#endif

enum { BarBottom, BarTop };

enum { CursorNormal, CursorResize, CursorMove, CursorLast };
enum { SchemeNormal, SchemeInverse, SchemeSelected, SchemeUrgent };
enum { NetSupported, NetWMName, NetWMIcon, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetClientInfo, NetLast };
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast };
enum { ClickBarTags, ClickBarLayoutSymbol, ClickBarStatus, ClickBarTitle,
       ClickBottomBar, ClickClientWin, ClickRootWin, ClickLast };

typedef union {
    int i;
    uint ui;
    float f;
    const void *v;
} Arg;

typedef struct {
    uint click;
    uint mask;
    ulong button;
    void (*function)(const Arg *arg);
    const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
    char name[256];
    Client *next;
    Client *stack_next;
    Client *all_next;
    Monitor *monitor;
    Picture icon;
    float min_aspect, max_aspect;

    int x, y, w, h;
    int stored_fx, stored_fy, stored_fw, stored_fh;
    int old_x, old_y, old_w, old_h;
    int base_w, base_h;
    int increment_w, increment_h;
    int max_w, max_h, min_w, min_h;
    int border_pixels;
    int old_border_pixels;
    uint tags;

    uint icon_width, icon_height;

    bool hintsvalid;
    bool is_fixed, is_floating, is_urgent;
    Window window;
    bool never_focus, old_state;
    bool is_fullscreen, is_fake_fullscreen;
};

typedef struct {
    ulong mod;
    KeySym keysym;
    void (*function)(const Arg *);
    const Arg arg;
} Key;

typedef struct {
    const char *symbol;
    void (*function)(Monitor *);
} Layout;

typedef struct Pertag Pertag;
struct Monitor {
    char layout_symbol[16];
    const Layout *layout[2];

    Client *clients;
    Client *selected_client;
    Client *stack;
    Monitor *next;
    Pertag *pertag;

    uint tagset[2];

    float master_fact;
    int number_masters;
    int num;
    int top_bar_y;
    int bottom_bar_y;
    int mon_x, mon_y, mon_w, mon_h;
    int win_x, win_y, win_w, win_h;

    uint selected_tags;
    uint lay_i;

    bool show_top_bar;
    bool show_bottom_bar;
    Window top_bar_window;
    Window bottom_bar_window;
};

typedef struct {
    const char *class;
    const char *instance;
    const char *title;

    uint tags;
    uint switchtotag;
    int monitor;
    bool is_floating;
    bool is_fake_fullscreen;
} Rule;

typedef struct BlockSignal {
    int min_x;
    int max_x;
    int signal;
    int text_i;
} BlockSignal;

typedef struct StatusBar {
    char text[STATUS_BUFFER_SIZE];
    int pixels;
    int number_blocks;
    BlockSignal blocks_signal[STATUS_MAX_BLOCKS];
} StatusBar;

static StatusBar status_top = {0};
static StatusBar status_bottom = {0};
static int status_signal;

static void error(const char *, char *, ...);
static void user_alt_tab(const Arg *);
static void user_aspect_resize(const Arg *);
static void user_focus_monitor(const Arg *);
static void user_focus_stack(const Arg *);
static void user_focus_urgent(const Arg *);
static void user_increment_number_masters(const Arg *);
static void user_kill_client(const Arg *);
static void user_mouse_move(const Arg *);
static void user_mouse_resize(const Arg *);
static void user_promote_to_master(const Arg *);
static void user_quit_dwm(const Arg *);
static void user_set_layout(const Arg *);
static void user_set_master_fact(const Arg *);
static void user_signal_status_bar(const Arg *);
static void user_spawn(const Arg *);
static void user_tag(const Arg *);
static void user_tag_monitor(const Arg *);
static void user_toggle_floating(const Arg *);
static void user_toggle_fullscreen(const Arg *);
static void user_toggle_tag(const Arg *);
static void user_toggle_bar(const Arg *);
static void user_toggle_view(const Arg *);
static void user_view_tag(const Arg *);
static void user_window_view(const Arg *);

static void handler_others(XEvent *);
static void handler_button_press(XEvent *);
static void handler_client_message(XEvent *);
static void handler_configure_notify(XEvent *);
static void handler_configure_request(XEvent *);
static void handler_destroy_notify(XEvent *);
static void handler_enter_notify(XEvent *);
static void handler_expose(XEvent *);
static void handler_focus_in(XEvent *);
static void handler_key_press(XEvent *);
static void handler_mapping_notify(XEvent *);
static void handler_map_request(XEvent *);
static void handler_motion_notify(XEvent *);
static void handler_property_notify(XEvent *);
static void handler_unmap_notify(XEvent *);
static int handler_xerror(Display *, XErrorEvent *);
static int handler_xerror_dummy(Display *, XErrorEvent *);
static int handler_xerror_start(Display *, XErrorEvent *);

static Atom client_get_atom_property(Client *, Atom);
static Client *client_next_tiled(Client *);
static bool client_is_visible(Client *);
static bool client_send_event(Client *, Atom);
static int client_apply_size_hints(Client *, int *, int *, int *, int *, bool);
static int client_pixels_height(Client *);
static int client_pixels_width(Client *);
static void client_apply_rules(Client *);
static void client_attach(Client *);
static void client_attach_stack(Client *);
static void client_center(Client *);
static void client_configure(Client *);
static void client_detach(Client *);
static void client_detach_stack(Client *);
static void client_focus(Client *);
static void client_free_icon(Client *);
static void client_grab_buttons(Client *, bool);
static void client_new(Window, XWindowAttributes *);
static void client_pop(Client *);
static void client_resize(Client *, int, int, int, int, bool);
static void client_resize_apply(Client *, int, int, int, int);
static void client_send_monitor(Client *, Monitor *);
static void client_set_client_state(Client *, long);
static void client_set_client_tag_prop(Client *);
static void client_set_focus(Client *);
static void client_set_fullscreen(Client *, bool);
static void client_set_urgent(Client *, bool);
static void client_show_hide(Client *);
static void client_unfocus(Client *, bool);
static void client_unmanage(Client *, int);
static void client_update_icon(Client *);
static void client_update_size_hints(Client *);
static void client_update_title(Client *);
static void client_update_window_type(Client *);
static void client_update_wm_hints(Client *);

static void monitor_arrange(Monitor *);
static void monitor_arrange_monitor(Monitor *);
static void monitor_cleanup_monitor(Monitor *);
static void monitor_draw_bar(Monitor *);
static void monitor_layout_columns(Monitor *);
static void monitor_layout_grid(Monitor *);
static void monitor_layout_monocle(Monitor *);
static void monitor_layout_tile(Monitor *);
static void monitor_restack(Monitor *);
static void monitor_update_bar_position(Monitor *);
static void monitor_focus(Monitor *, bool);

static Monitor *create_monitor(void);
static Monitor *direction_to_monitor(int);
static Monitor *rectangle_to_monitor(int, int, int, int);
static Monitor *window_to_monitor(Window);
static Client *window_to_client(Window);

static void set_layout(const Layout *);
static int get_root_pointer(int *, int *);
static int get_text_pixels(char *);
static int get_text_property(Window, Atom, char *, uint);
static int update_geometry(void);
static long get_window_state(Window);
static void configure_bars_windows(void);
static void draw_bars(void);
static void draw_status_text(StatusBar *, int);
static void focus_direction(int);
static void focus_next(bool);
static void grab_keys(void);
static void scan_windows_once(void);
static void setup_once(void);
static void status_get_signal_number(BlockSignal *, int);
static void status_parse_text(StatusBar *);
static void toggle_bar(int);
static void update_numlock_mask(void);
static void status_update(void);
static void view_tag(uint);

static const char broken[] = "broken";

static int screen;
static int screen_width;
static int screen_height;

static uint bar_height;
static int text_padding;
static int (*xerrorxlib)(Display *, XErrorEvent *);
static uint numlock_mask = 0;

static void (*handlers[LASTEvent]) (XEvent *) = {
    [ButtonPress] = handler_button_press,
    [ButtonRelease] = NULL,
    [CirculateNotify] = handler_others,
    [CirculateRequest] = handler_others,
    [ClientMessage] = handler_client_message,
    [ColormapNotify] = handler_others,
    [ConfigureNotify] = handler_configure_notify,
    [ConfigureRequest] = handler_configure_request,
    [CreateNotify] = handler_others,
    [DestroyNotify] = handler_destroy_notify,
    [EnterNotify] = handler_enter_notify,
    [Expose] = handler_expose,
    [FocusIn] = handler_focus_in,
    [FocusOut] = handler_others,
    [GenericEvent] = handler_others,
    [GraphicsExpose] = handler_others,
    [GravityNotify] = handler_others,
    [KeyPress] = handler_key_press,
    [KeyRelease] = NULL,
    [KeymapNotify] = handler_others,
    [LeaveNotify] = handler_others,
    [MapNotify] = handler_others,
    [MapRequest] = handler_map_request,
    [MappingNotify] = handler_mapping_notify,
    [MotionNotify] = handler_motion_notify,
    [NoExpose] = handler_others,
    [PropertyNotify] = handler_property_notify,
    [ReparentNotify] = handler_others,
    [ResizeRequest] = handler_others,
    [SelectionClear] = handler_others,
    [SelectionNotify] = handler_others,
    [SelectionRequest] = handler_others,
    [UnmapNotify] = handler_unmap_notify,
    [VisibilityNotify] = handler_others,
};

static Atom wm_atoms[WMLast];
static Atom net_atoms[NetLast];
static Display *display;
static Visual *visual;
static Colormap color_map;
static Window root;
static Window wm_check_window;
static int depth;

static bool dwm_restart = false;
static bool dwm_running = true;

static Cur *cursor[CursorLast];
static Clr **scheme;
static Drw *drw;

static Monitor *monitors;
static Monitor *live_monitor;
static Client *all_clients = NULL;

#include "config.h"

struct Pertag {
    const Layout *layouts[LENGTH(tags) + 1][2];
    int number_masters[LENGTH(tags) + 1];
    float master_facts[LENGTH(tags) + 1];
    uint selected_layouts[LENGTH(tags) + 1];
    uint tag;
    uint old_tag;
    bool top_bars[LENGTH(tags) + 1];
    bool bottom_bars[LENGTH(tags) + 1];
};

static int tag_width[LENGTH(tags)];

/* compile-time check if all tags fit into an uint bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

void error(const char *function, char *format, ...) {
    int message_length;
    int header_length;
    va_list args;
    char message[256];
    char header[128];

    header_length = snprintf(header, sizeof (header) - 1,
                             "dwm: %s() ", function);

    va_start(args, format);
    message_length = vsnprintf(message, sizeof (message) - 1,
                               format, args);
    va_end(args);

    if (message_length < 0 || header_length < 0) {
        fprintf(stderr, "Error in vsnprintf()\n");
        exit(EXIT_FAILURE);
    }

    message[message_length] = '\0';
    header[header_length] = '\0';
    (void) write(STDERR_FILENO, header, (size_t) header_length);
    (void) write(STDERR_FILENO, message, (size_t) message_length);

    switch (fork()) {
    case -1:
        fprintf(stderr, "Error forking: %s\n", strerror(errno));
        break;
    case 0:
        execlp("dunstify", "dunstify", "-u", "critical", "-t", "2000",
                           header, message, NULL);
        fprintf(stderr, "Error trying to exec dunstify.\n");
        exit(EXIT_FAILURE);
    default:
        break;
    }
    return;
}

void
user_alt_tab(const Arg *) {
    static bool alt_tab_direction = false;
    Monitor *old_monitor = live_monitor;
    Client *client;
    bool grabbed = false;
    int grab_status = 1000;

    if (all_clients == NULL)
        return;

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        monitor_focus(monitor, false);
        view_tag((uint)~0);
        set_layout(&layouts[3]);
    }
    monitor_focus(old_monitor, false);
    client_focus(live_monitor->selected_client);
    focus_next(alt_tab_direction);

    for (int i = 0; i < ALT_TAB_GRAB_TRIES; i += 1) {
        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = PAUSE_MILIS_AS_NANOS(5);

        if (grab_status != GrabSuccess) {
            grab_status = XGrabKeyboard(display, root, True,
                                        GrabModeAsync, GrabModeAsync,
                                        CurrentTime);
        }
        if (grab_status == GrabSuccess) {
            grabbed = XGrabButton(display, AnyButton, AnyModifier, None, False,
                                  BUTTONMASK, GrabModeAsync, GrabModeAsync,
                                  None, None);
            break;
        }
        nanosleep(&pause, NULL);
    }

    client = live_monitor->selected_client;
    while (grabbed) {
        XEvent event;
        Monitor *monitor;

        XNextEvent(display, &event);
        switch (event.type) {
        case ConfigureRequest:
        case DestroyNotify:
        case Expose:
        case MapRequest:
            handlers[event.type](&event);
            break;
        case KeyPress:
            if (event.xkey.keycode == tabCycleKey)
                focus_next(alt_tab_direction);
            else if (event.xkey.keycode == key_j)
                focus_direction(0);
            else if (event.xkey.keycode == key_semicolon)
                focus_direction(1);
            else if (event.xkey.keycode == key_l)
                focus_direction(2);
            else if (event.xkey.keycode == key_k)
                focus_direction(3);
            client = live_monitor->selected_client;
            break;
        case KeyRelease:
            if (event.xkey.keycode == tabModKey) {
                XUngrabKeyboard(display, CurrentTime);
                XUngrabButton(display, AnyButton, AnyModifier, None);
                grabbed = false;
                alt_tab_direction = !alt_tab_direction;
                view_tag(client->tags);
            }
            break;
        case ButtonPress: {
            XButtonPressedEvent *button_event = &(event.xbutton);
            monitor = window_to_monitor(button_event->window);
            if (monitor && (monitor != live_monitor))
                monitor_focus(monitor, true);

            if ((client = window_to_client(button_event->window)))
                client_focus(client);
            XAllowEvents(display, AsyncBoth, CurrentTime);
            break;
        }
        case ButtonRelease:
            XUngrabKeyboard(display, CurrentTime);
            XUngrabButton(display, AnyButton, AnyModifier, None);
            grabbed = false;
            alt_tab_direction = !alt_tab_direction;
            view_tag(client->tags);
            break;
        default:
            break;
        }
    }
    return;
}

void
user_aspect_resize(const Arg *arg) {
    Monitor *monitor = live_monitor;
    Client *client = live_monitor->selected_client;
    float ratio;
    int w, h;
    int new_width, new_height;
    bool monitor_floating = !monitor->layout[monitor->lay_i]->function;

    if (!arg)
        return;
    if (client == NULL)
        return;

    if (!client->is_floating && !monitor_floating)
        return;

    ratio = (float)client->w / (float)client->h;
    h = arg->i;
    w = (int)(ratio*(float)h);

    new_width = client->w + w;
    new_height = client->h + h;

    XRaiseWindow(display, client->window);
    client_resize(client, client->x, client->y, new_width, new_height, true);
    return;
}

void
user_focus_monitor(const Arg *arg) {
    Monitor *monitor;

    if (!monitors->next)
        return;
    if ((monitor = direction_to_monitor(arg->i)) == live_monitor)
        return;

    monitor_focus(monitor, false);
    return;
}

void
user_focus_stack(const Arg *arg) {
    Client *client = NULL;

    if (!live_monitor->selected_client)
        return;
    if (live_monitor->selected_client->is_fullscreen && lockfullscreen)
        return;

    if (arg->i > 0) {
        for (client = live_monitor->selected_client->next;
             client && !client_is_visible(client);
             client = client->next);
        if (client == NULL) {
            for (client = live_monitor->clients;
                 client && !client_is_visible(client);
                 client = client->next);
        }
    } else {
        Client *client_aux;
        for (client_aux = live_monitor->clients;
             client_aux != live_monitor->selected_client;
             client_aux = client_aux->next) {
            if (client_is_visible(client_aux))
                client = client_aux;
        }
        if (client == NULL) {
            for (; client_aux; client_aux = client_aux->next) {
                if (client_is_visible(client_aux))
                    client = client_aux;
            }
        }
    }
    if (client) {
        client_focus(client);
        monitor_restack(live_monitor);
    }
    return;
}

void
user_focus_urgent(const Arg *) {
    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        Client *client;

        for (client = monitor->clients;
             client && !client->is_urgent;
             client = client->next);

        if (client) {
            int i = 0;
            client_unfocus(live_monitor->selected_client, false);
            live_monitor = monitor;

            while (i < LENGTH(tags) && !((1 << i) & client->tags))
                i += 1;
            if (i < LENGTH(tags)) {
                view_tag(1 << i);
                client_focus(client);
            }
        }
    }
    return;
}

void
user_increment_number_masters(const Arg *arg) {
    Monitor *monitor = live_monitor;
    Pertag *pertag = monitor->pertag;
    int number_slaves = -1;
    int number_masters;
    uint tag;

    for (Client *client = monitor->clients;
                 client;
                 client = client_next_tiled(client->next)) {
        number_slaves += 1;
    }

    number_masters = MIN(monitor->number_masters + arg->i, number_slaves + 1);
    number_masters = MAX(number_masters, 0);

    tag = monitor->pertag->tag;
    monitor->number_masters = pertag->number_masters[tag] = number_masters;

    monitor_arrange(monitor);
    return;
}

void
user_kill_client(const Arg *) {
    Client *selected = live_monitor->selected_client;
    if (!selected)
        return;

    if (!client_send_event(selected, wm_atoms[WMDelete])) {
        XGrabServer(display);
        XSetErrorHandler(handler_xerror_dummy);
        XSetCloseDownMode(display, DestroyAll);

        XKillClient(display, selected->window);
        XSync(display, False);

        XSetErrorHandler(handler_xerror);
        XUngrabServer(display);
    }
    return;
}

void
user_mouse_move(const Arg *) {
    Client *client;
    Monitor *monitor_aux;
    XEvent event;
    Time last_time = 0;
    int success;
    int x, y;
    int ocx, ocy;

    if (!(client = live_monitor->selected_client))
        return;

    if (client->is_fullscreen && !client->is_fake_fullscreen)
        return;

    monitor_restack(live_monitor);
    ocx = client->x;
    ocy = client->y;

    success = XGrabPointer(display, root, False,
                          MOUSEMASK, GrabModeAsync, GrabModeAsync,
                          None, cursor[CursorMove]->cursor, CurrentTime);
    if (success != GrabSuccess)
        return;

    if (!get_root_pointer(&x, &y))
        return;

    do {
        XMaskEvent(display,
                   MOUSEMASK|ExposureMask|SubstructureRedirectMask, &event);
        switch (event.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handlers[event.type](&event);
            break;
        case MotionNotify: {
            Monitor *monitor = live_monitor;
            bool is_floating = client->is_floating;
            int new_x = ocx + (event.xmotion.x - x);
            int new_y = ocy + (event.xmotion.y - y);
            int client_width = client_pixels_width(client);
            int client_height = client_pixels_height(client);
            int over_x[2] = {
                abs(monitor->win_x - new_x),
                abs(monitor->win_x + monitor->win_w - (new_x + client_width)),
            };
            int over_y[2] = {
                abs(monitor->win_y - new_y),
                abs(monitor->win_y + monitor->win_h - (new_y + client_height)),
            };

            if ((event.xmotion.time - last_time) <= (1000 / 60))
                continue;
            last_time = event.xmotion.time;

            if (over_x[0] < SNAP_PIXELS)
                new_x = monitor->win_x;
            else if (over_x[1] < SNAP_PIXELS)
                new_x = monitor->win_x + monitor->win_w - client_width;

            if (over_y[0] < SNAP_PIXELS)
                new_y = monitor->win_y;
            else if (over_y[1] < SNAP_PIXELS)
                new_y = monitor->win_y + monitor->win_h - client_height;

            if (!is_floating && monitor->layout[monitor->lay_i]->function) {
                bool moving_x = abs(new_x - client->x) > SNAP_PIXELS;
                bool moving_y = abs(new_y - client->y) > SNAP_PIXELS;
                if (moving_x || moving_y)
                    user_toggle_floating(NULL);
            }

            if (!monitor->layout[monitor->lay_i]->function || is_floating)
                client_resize(client, new_x, new_y, client->w, client->h, true);
            break;
        }
        default:
            break;
        }
    } while (event.type != ButtonRelease);

    XUngrabPointer(display, CurrentTime);

    monitor_aux = rectangle_to_monitor(client->x, client->y,
                                       client->w, client->h);
    if (monitor_aux != live_monitor) {
        client_send_monitor(client, monitor_aux);
        live_monitor = monitor_aux;
        client_focus(NULL);
    }
    return;
}

void
user_mouse_resize(const Arg *) {
    Client *client;
    Monitor *monitor;
    XEvent event;
    Time last_time = 0;
    int success;

    if (!(client = live_monitor->selected_client))
        return;

    if (client->is_fullscreen && !client->is_fake_fullscreen)
        return;

    monitor_restack(live_monitor);

    success = XGrabPointer(display, root, False,
                           MOUSEMASK, GrabModeAsync, GrabModeAsync,
                           None, cursor[CursorResize]->cursor, CurrentTime);
    if (success != GrabSuccess)
        return;

    XWarpPointer(display, None, client->window,
                 0, 0, 0, 0,
                 client->w + client->border_pixels - 1,
                 client->h + client->border_pixels - 1);
    do {
        XMaskEvent(display,
                   MOUSEMASK|ExposureMask|SubstructureRedirectMask, &event);
        switch (event.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handlers[event.type](&event);
            break;
        case MotionNotify: {
            bool monitor_floating;
            int new_w, new_h, new_x, new_y;
            bool over_x, under_x, over_y, under_y;

            if ((event.xmotion.time - last_time) <= (1000 / 60))
                continue;
            last_time = event.xmotion.time;

            event.xmotion.x += (-client->x - 2*client->border_pixels + 1);
            event.xmotion.y += (-client->y - 2*client->border_pixels + 1);

            new_w = MAX(event.xmotion.x, 1);
            new_h = MAX(event.xmotion.y, 1);

            monitor_floating = !(live_monitor->layout[live_monitor->lay_i]->function);
            if (!client->is_floating && !monitor_floating) {
                bool over_snap_x = abs(new_w - client->w) > SNAP_PIXELS;
                bool over_snap_y = abs(new_h - client->h) > SNAP_PIXELS;

                new_x = client->monitor->win_x + new_w;
                new_y = client->monitor->win_y + new_h;
                over_x = new_x >= live_monitor->win_x;
                under_x = new_x <= live_monitor->win_x + live_monitor->win_w;
                over_y = new_y >= live_monitor->win_y;
                under_y = new_y <= live_monitor->win_y + live_monitor->win_h;

                if (over_x && under_x && over_y && under_y
                    && (over_snap_x || over_snap_y)) {
                    user_toggle_floating(NULL);
                }
            }
            if (client->is_floating || monitor_floating)
                client_resize(client, client->x, client->y, new_w, new_h, true);
            break;
        }
        default:
            break;
        }
    } while (event.type != ButtonRelease);

    XWarpPointer(display, None, client->window,
                 0, 0, 0, 0,
                 client->w + client->border_pixels - 1,
                 client->h + client->border_pixels - 1);
    XUngrabPointer(display, CurrentTime);
    while (XCheckMaskEvent(display, EnterWindowMask, &event));

    monitor = rectangle_to_monitor(client->x, client->y, client->w, client->h);
    if (monitor != live_monitor) {
        client_send_monitor(client, monitor);
        live_monitor = monitor;
        client_focus(NULL);
    }
    return;
}

void
user_quit_dwm(const Arg *arg) {
    if (arg->i)
        dwm_restart = true;
    dwm_running = false;
    return;
}

void
user_set_layout(const Arg *arg) {
    set_layout(arg->v);
    return;
}

void
user_set_master_fact(const Arg *arg) {
    float factor;
    Pertag *pertag = live_monitor->pertag;

    if (!arg)
        return;
    if (!live_monitor->layout[live_monitor->lay_i]->function)
        return;

    if (arg->f < 1.0f)
        factor = arg->f + live_monitor->master_fact;
    else
        /* arg > 1.0 will set master_fact absolutely */
        factor = arg->f - 1.0f;

    if (factor < 0.05f || factor > 0.95f)
        return;

    live_monitor->master_fact = pertag->master_facts[pertag->tag] = factor;
    monitor_arrange(live_monitor);
    return;
}

void
user_signal_status_bar(const Arg *arg) {
    pid_t status_program_pid;
    union sigval signal_value;
    int pipefd[2];
    char buffer[32] = {0};

    if (!status_signal)
        return;
    signal_value.sival_int = arg->i | ((SIGRTMIN + status_signal) << 3);

    if (pipe(pipefd) < 0) {
        error(__func__, "Error creating pipe: %s\n", strerror(errno));
        status_program_pid = -1;
        return;
    }

    switch (fork()) {
    case -1:
        error(__func__, "Error forking: %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        status_program_pid = -1;
        return;
    case 0:
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("pidof", "pidof", "-s", STATUSBAR, NULL);
        error(__func__, "Error executing pidof.\n");
        exit(EXIT_FAILURE);
    default:
        close(pipefd[1]);
        break;
    }

    if (read(pipefd[0], buffer, sizeof (buffer) - 1) <= 0) {
        close(pipefd[0]);
        status_program_pid = -1;
        return;
    }
    close(pipefd[0]);

    status_program_pid = atoi(buffer);
    sigqueue(status_program_pid, SIGUSR1, signal_value);
    return;
}

void
user_tag(const Arg *arg) {
    Client *selected_client = live_monitor->selected_client;
    uint which_tag = arg->ui & TAGMASK;

    if (which_tag && selected_client) {
        selected_client->tags = which_tag;
        client_set_client_tag_prop(selected_client);
        client_focus(NULL);
        monitor_arrange(live_monitor);
    }
    return;
}

void
user_tag_monitor(const Arg *arg) {
    Monitor *monitor = direction_to_monitor(arg->i);
    Client *selected = live_monitor->selected_client;

    if (!selected || !monitors->next)
        return;

    if (selected->is_floating) {
        selected->x += monitor->mon_x - live_monitor->mon_x;
        selected->y += monitor->mon_y - live_monitor->mon_y;
    }

    client_send_monitor(selected, monitor);
    monitor_focus(monitor, false);
    user_toggle_floating(NULL);
    user_toggle_floating(NULL);
    return;
}

void
user_toggle_bar(const Arg *arg) {
    toggle_bar(arg->i);
    return;
}

void
user_toggle_floating(const Arg *) {
    Client *client = live_monitor->selected_client;

    if (client == NULL)
        return;

    if (client->is_fullscreen && !client->is_fake_fullscreen)
        return;

    client->is_floating = !client->is_floating || client->is_fixed;
    if (client->is_floating) {
        client_resize(client,
                      client->stored_fx, client->stored_fy,
                      client->stored_fw, client->stored_fh,
                      false);
    } else {
        client->stored_fx = client->x;
        client->stored_fy = client->y;
        client->stored_fw = client->w;
        client->stored_fh = client->h;
    }
    client_center(client);
    monitor_arrange(live_monitor);
    return;
}

void
user_toggle_fullscreen(const Arg *) {
    Client *client = live_monitor->selected_client;
    if (client)
        client_set_fullscreen(client, !client->is_fullscreen);
    return;
}

void
user_spawn(const Arg *arg) {
   struct sigaction signal_action;

   if (fork() == 0) {
       if (display)
           close(ConnectionNumber(display));
       setsid();

       sigemptyset(&signal_action.sa_mask);
       signal_action.sa_flags = 0;
       signal_action.sa_handler = SIG_DFL;
       sigaction(SIGCHLD, &signal_action, NULL);

       execvp(((char *const *)arg->v)[0], (char *const *)arg->v);
       error("user_spawn",
             "dwm: execvp '%s' failed:", ((char *const *)arg->v)[0]);
   }
   return;
}

void
user_toggle_tag(const Arg *arg) {
    uint newtags;

    if (!live_monitor->selected_client)
        return;

    newtags = live_monitor->selected_client->tags ^ (arg->ui & TAGMASK);
    if (newtags) {
        live_monitor->selected_client->tags = newtags;
        client_set_client_tag_prop(live_monitor->selected_client);
        client_focus(NULL);
        monitor_arrange(live_monitor);
    }
    return;
}

void
user_toggle_view(const Arg *arg) {
    Monitor *monitor = live_monitor;
    uint new_tags;
    uint tag;

    new_tags = monitor->tagset[monitor->selected_tags] ^ (arg->ui & TAGMASK);
    if (!new_tags)
        return;

    monitor->tagset[monitor->selected_tags] = new_tags;

    if (new_tags == (uint)~0) {
        monitor->pertag->old_tag = monitor->pertag->tag;
        monitor->pertag->tag = 0;
    }

    /* test if the user did not select the same tag */
    if (!(new_tags & 1 << (monitor->pertag->tag - 1))) {
        uint i = 0;
        monitor->pertag->old_tag = monitor->pertag->tag;
        while (!(new_tags & 1 << i))
            i += 1;
        monitor->pertag->tag = i + 1;
    }

    tag = monitor->pertag->tag;

    monitor->number_masters = monitor->pertag->number_masters[tag];
    monitor->master_fact = monitor->pertag->master_facts[tag];
    monitor->lay_i = monitor->pertag->selected_layouts[tag];
    monitor->layout[monitor->lay_i]
        = monitor->pertag->layouts[tag][monitor->lay_i];
    monitor->layout[monitor->lay_i^1]
        = monitor->pertag->layouts[tag][monitor->lay_i^1];

    if (monitor->show_top_bar != monitor->pertag->top_bars[tag])
        toggle_bar(BarTop);
    if (monitor->show_bottom_bar != monitor->pertag->bottom_bars[tag])
        toggle_bar(BarBottom);

    client_focus(NULL);
    monitor_arrange(monitor);
    return;
}

void
user_view_tag(const Arg *arg) {
    view_tag(arg->ui);
    return;
}

void
user_window_view(const Arg *) {
    Client *client = live_monitor->selected_client;
    view_tag(client->tags);
    return;
}

void
user_promote_to_master(const Arg *) {
    Client *client = live_monitor->selected_client;
    Monitor *monitor = live_monitor;
    bool monitor_floating = !monitor->layout[monitor->lay_i]->function;
    bool is_next_tiled = client == client_next_tiled(monitor->clients);

    if (client == NULL)
        return;
    if (monitor_floating || client->is_floating)
        return;
    if (is_next_tiled && !(client = client_next_tiled(client->next)))
        return;

    client_pop(client);
    return;
}

void
client_apply_rules(Client *client) {
    const char *class;
    const char *instance;
    XClassHint class_hint = { NULL, NULL };

    client->is_floating = false;
    client->tags = 0;
    XGetClassHint(display, client->window, &class_hint);
    class    = class_hint.res_class ? class_hint.res_class : broken;
    instance = class_hint.res_name  ? class_hint.res_name  : broken;

    for (int i = 0; i < LENGTH(rules); i += 1) {
        const Rule *rule = &rules[i];
        Monitor *monitor_aux;

        if ((!rule->title || strstr(client->name, rule->title))
            && (!rule->class || strstr(class, rule->class))
            && (!rule->instance || strstr(instance, rule->instance))) {
            client->is_floating = rule->is_floating;
            client->is_fake_fullscreen = rule->is_fake_fullscreen;
            client->tags |= rule->tags;

            if (rule->is_floating)
                client_center(client);

            for (monitor_aux = monitors;
                 monitor_aux && monitor_aux->num != rule->monitor;
                 monitor_aux = monitor_aux->next);
            if (monitor_aux)
                client->monitor = monitor_aux;

            if (rule->switchtotag)
                view_tag(rule->tags);
        }
    }
    if (class_hint.res_class)
        XFree(class_hint.res_class);
    if (class_hint.res_name)
        XFree(class_hint.res_name);

    if (client->tags & TAGMASK) {
        client->tags = client->tags & TAGMASK;
    } else {
        uint which_tags = client->monitor->selected_tags;
        client->tags = client->monitor->tagset[which_tags];
    }
    return;
}

int
client_apply_size_hints(Client *client,
                        int *x, int *y, int *w, int *h, bool interact) {
    Monitor *monitor = client->monitor;
    int success;

    *w = MAX(1, *w);
    *h = MAX(1, *h);

    if (interact) {
        if (*x > screen_width)
            *x = screen_width - client_pixels_width(client);
        if (*y > screen_height)
            *y = screen_height - client_pixels_height(client);
        if (*x + *w + 2*client->border_pixels < 0)
            *x = 0;
        if (*y + *h + 2*client->border_pixels < 0)
            *y = 0;
    } else {
        if (*x >= monitor->win_x + monitor->win_w)
            *x = monitor->win_x + monitor->win_w - client_pixels_width(client);
        if (*y >= monitor->win_y + monitor->win_h)
            *y = monitor->win_y + monitor->win_h - client_pixels_height(client);
        if (*x + *w + 2*client->border_pixels <= monitor->win_x)
            *x = monitor->win_x;
        if (*y + *h + 2*client->border_pixels <= monitor->win_y)
            *y = monitor->win_y;
    }

    if (*h < (int)bar_height)
        *h = (int)bar_height;
    if (*w < (int)bar_height)
        *w = (int)bar_height;

    if (resizehints
        || client->is_floating
        || !client->monitor->layout[client->monitor->lay_i]->function) {
        int base_is_min;

        if (!client->hintsvalid)
            client_update_size_hints(client);

        /* see last two sentences in ICCCM 4.1.2.3 */
        base_is_min = client->base_w == client->min_w
                      && client->base_h == client->min_h;
        if (!base_is_min) { /* temporarily remove base dimensions */
            *w -= client->base_w;
            *h -= client->base_h;
        }

        /* adjust for aspect limits */
        if (client->min_aspect > 0 && client->max_aspect > 0) {
            if (client->max_aspect < (float)*w / (float)*h)
                *w = *h*((int)(client->max_aspect + 0.5f));
            else if (client->min_aspect < (float)*h / (float)*w)
                *h = *w*((int)(client->min_aspect + 0.5f));
        }

        if (base_is_min) { /* increment calculation requires this */
            *w -= client->base_w;
            *h -= client->base_h;
        }

        /* adjust for increment value */
        if (client->increment_w)
            *w -= *w % client->increment_w;
        if (client->increment_h)
            *h -= *h % client->increment_h;

        /* restore base dimensions */
        *w = MAX(*w + client->base_w, client->min_w);
        *h = MAX(*h + client->base_h, client->min_h);
        if (client->max_w)
            *w = MIN(*w, client->max_w);
        if (client->max_h)
            *h = MIN(*h, client->max_h);
    }
    success = *x != client->x || *y != client->y
             || *w != client->w || *h != client->h;
    return success;
}

void
client_attach(Client *client) {
    client->next = client->monitor->clients;
    client->all_next = all_clients;
    client->monitor->clients = client;
    all_clients = client;
    return;
}

void
client_attach_stack(Client *client) {
    client->stack_next = client->monitor->stack;
    client->monitor->stack = client;
    return;
}

void
client_configure(Client *client) {
    XConfigureEvent configure_event;

    configure_event.type = ConfigureNotify;
    configure_event.display = display;
    configure_event.event = client->window;
    configure_event.window = client->window;
    configure_event.x = client->x;
    configure_event.y = client->y;
    configure_event.width = client->w;
    configure_event.height = client->h;
    configure_event.border_width = client->border_pixels;
    configure_event.above = None;
    configure_event.override_redirect = False;
    XSendEvent(display, client->window,
               False, StructureNotifyMask, (XEvent *)&configure_event);
    return;
}

void
client_detach(Client *client) {
    Client **clients;

    for (clients = &client->monitor->clients;
         *clients && *clients != client;
         clients = &(*clients)->next);
    *clients = client->next;

    for (clients = &all_clients;
         *clients && *clients != client;
         clients = &(*clients)->all_next);
    *clients = client->all_next;

    return;
}

void
client_detach_stack(Client *client) {
    Client **client_aux;

    for (client_aux = &client->monitor->stack;
         *client_aux && *client_aux != client;
         client_aux = &(*client_aux)->stack_next);
    *client_aux = client->stack_next;

    if (client == client->monitor->selected_client) {
        Client *t;
        for (t = client->monitor->stack;
             t && !client_is_visible(t);
             t = t->stack_next);
        client->monitor->selected_client = t;
    }
    return;
}

void
client_focus(Client *client) {
    Client *selected = live_monitor->selected_client;
    if (!client || !client_is_visible(client)) {
        for (client = live_monitor->stack;
             client && !client_is_visible(client);
             client = client->stack_next);
    }

    if (selected && selected != client)
        client_unfocus(selected, false);

    if (client) {
        if (client->monitor != live_monitor)
            live_monitor = client->monitor;
        if (client->is_urgent)
            client_set_urgent(client, false);
        client_detach_stack(client);
        client_attach_stack(client);
        client_grab_buttons(client, true);
        XSetWindowBorder(display, client->window,
                         scheme[SchemeSelected][ColBorder].pixel);
        client_set_focus(client);
    } else {
        XSetInputFocus(display, live_monitor->top_bar_window,
                       RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display, root, net_atoms[NetActiveWindow]);
    }

    live_monitor->selected_client = client;
    draw_bars();
    return;
}

Atom
client_get_atom_property(Client *client, Atom property) {
    int actual_format_return;
    ulong nitems_return;
    Atom actual_type_return;
    Atom atom = None;
    Atom *prop_return = NULL;
    int success;

    success = XGetWindowProperty(display, client->window, property,
                                0L, sizeof(atom), False, XA_ATOM,
                                &actual_type_return, &actual_format_return,
                                &nitems_return, &nitems_return,
                                (uchar **)&prop_return);
    if (success == Success && prop_return) {
        atom = *prop_return;
        XFree(prop_return);
    }
    return atom;
}

void
client_grab_buttons(Client *client, bool focused) {
    uint modifiers[] = { 0, LockMask, numlock_mask, numlock_mask|LockMask };

    update_numlock_mask();
    XUngrabButton(display, AnyButton, AnyModifier, client->window);
    if (!focused) {
        XGrabButton(display, AnyButton, AnyModifier, client->window, False,
                    BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
    }
    for (int i = 0; i < LENGTH(buttons); i += 1) {
        if (buttons[i].click != ClickClientWin)
            continue;

        for (int j = 0; j < LENGTH(modifiers); j += 1) {
            XGrabButton(display, (uint)buttons[i].button,
                        buttons[i].mask | modifiers[j],
                        client->window, False, BUTTONMASK,
                        GrabModeAsync, GrabModeSync, None, None);
        }
    }
    return;
}

void
client_center(Client *client) {
    Monitor *monitor = client->monitor;
    int client_width = client_pixels_width(client);
    int client_height = client_pixels_height(client);

    client->x = monitor->mon_x + (monitor->mon_w - client_width)/2;
    client->y = monitor->mon_y + (monitor->mon_h - client_height)/2;
    return;
}

void
client_new(Window window, XWindowAttributes *window_attributes) {
    Client *client;
    Client *trans_client = NULL;
    Window trans_window = None;
    XWindowChanges window_changes;
    int success;

    client = ecalloc(1, sizeof(*client));
    client->window = window;

    client->x = client->old_x = window_attributes->x;
    client->y = client->old_y = window_attributes->y;
    client->w = client->old_w = window_attributes->width;
    client->h = client->old_h = window_attributes->height;
    client->old_border_pixels = window_attributes->border_width;

    client_update_icon(client);
    client_update_title(client);

    success = XGetTransientForHint(display, window, &trans_window);
    if (success && (trans_client = window_to_client(trans_window))) {
        client->monitor = trans_client->monitor;
        client->tags = trans_client->tags;
    } else {
        client->monitor = live_monitor;
        client_apply_rules(client);
    }

    {
        Monitor *monitor = client->monitor;
        int client_width = client_pixels_width(client);
        int client_height = client_pixels_height(client);

        if (client->x + client_width > monitor->win_x + monitor->win_w)
            client->x = monitor->win_x + monitor->win_w - client_width;
        if (client->y + client_height > monitor->win_y + monitor->win_h)
            client->y = monitor->win_y + monitor->win_h - client_height;
    }
    client->x = MAX(client->x, client->monitor->win_x);
    client->y = MAX(client->y, client->monitor->win_y);
    client->border_pixels = border_pixels;

    window_changes.border_width = client->border_pixels;
    XConfigureWindow(display, window, CWBorderWidth, &window_changes);
    XSetWindowBorder(display, window, scheme[SchemeNormal][ColBorder].pixel);

    /* propagates border_pixels, if size doesn'trans_client change */
    client_configure(client);
    client_update_window_type(client);
    client_update_size_hints(client);
    client_update_wm_hints(client);
    {
        int actual_format_return;
        ulong *prop_return;
        ulong nitems_return;
        ulong bytes_after_return;
        Atom actual_type_return;

        success = XGetWindowProperty(display, client->window,
                                     net_atoms[NetClientInfo],
                                     0L, 2L, False, XA_CARDINAL,
                                     &actual_type_return, &actual_format_return,
                                     &nitems_return, &bytes_after_return,
                                     (uchar **)&prop_return);
        if (success == Success && nitems_return == 2) {
            client->tags = (uint)*prop_return;
            for (Monitor *mon = monitors; mon; mon = mon->next) {
                if (mon->num == (int)*(prop_return + 1)) {
                    client->monitor = mon;
                    break;
                }
            }
        }
        if (nitems_return > 0)
            XFree(prop_return);
    }
    client_set_client_tag_prop(client);

    client->stored_fx = client->x;
    client->stored_fy = client->y;
    client->stored_fw = client->w;
    client->stored_fh = client->h;

    client_center(client);

    XSelectInput(display, window,
                 EnterWindowMask
                 |FocusChangeMask
                 |PropertyChangeMask
                 |StructureNotifyMask);

    client_grab_buttons(client, false);

    if (!client->is_floating) {
        client->is_floating = trans_window != None || client->is_fixed;
        client->old_state = client->is_floating;
    }
    if (client->is_floating)
        XRaiseWindow(display, client->window);

    client_attach(client);
    client_attach_stack(client);

    XChangeProperty(display, root, net_atoms[NetClientList], XA_WINDOW,
                    32, PropModeAppend, (uchar *)&(client->window), 1);

    /* some windows require this */
    XMoveResizeWindow(display, client->window,
                      client->x + 2*screen_width, client->y,
                      (uint)client->w, (uint)client->h);
    client_set_client_state(client, NormalState);

    if (client->monitor == live_monitor)
        client_unfocus(live_monitor->selected_client, false);

    client->monitor->selected_client = client;
    monitor_arrange(client->monitor);
    XMapWindow(display, client->window);
    client_focus(NULL);
    return;
}

void
client_unmanage(Client *client, int destroyed) {
    Monitor *monitor = client->monitor;
    XWindowChanges window_changes;

    client_detach(client);
    client_detach_stack(client);
    client_free_icon(client);

    if (!destroyed) {
        window_changes.border_width = client->old_border_pixels;
        XGrabServer(display); /* avoid race conditions */
        XSetErrorHandler(handler_xerror_dummy);

        XSelectInput(display, client->window, NoEventMask);

        XConfigureWindow(display, client->window,
                         CWBorderWidth, &window_changes);
        XUngrabButton(display, AnyButton, AnyModifier, client->window);
        client_set_client_state(client, WithdrawnState);

        XSync(display, False);
        XSetErrorHandler(handler_xerror);
        XUngrabServer(display);
    }

    free(client);
    client_focus(NULL);

    XDeleteProperty(display, root, net_atoms[NetClientList]);
    for (Monitor *mon = monitors; mon; mon = mon->next) {
        for (Client *c = mon->clients; c; c = c->next) {
            XChangeProperty(display, root, net_atoms[NetClientList],
                            XA_WINDOW, 32, PropModeAppend,
                            (uchar *)&(c->window), 1);
        }
    }

    monitor_arrange(monitor);
    return;
}

Client *
client_next_tiled(Client *client) {
    while (true) {
        if (client == NULL)
            break;
        if (!client->is_floating && client_is_visible(client))
            break;

        client = client->next;
    }
    return client;
}

void
client_pop(Client *client) {
    client_detach(client);
    client_attach(client);
    client_focus(client);
    monitor_arrange(client->monitor);
    return;
}

void
client_resize(Client *client, int x, int y, int w, int h, bool interact) {
    if (client_apply_size_hints(client, &x, &y, &w, &h, interact))
        client_resize_apply(client, x, y, w, h);
    return;
}

void
client_resize_apply(Client *client, int x, int y, int w, int h) {
    XWindowChanges window_changes;
    uint n = 0;

    client->old_x = client->x;
    client->x = window_changes.x = x;
    client->old_y = client->y;
    client->y = window_changes.y = y;

    client->old_w = client->w;
    client->w = window_changes.width = w;
    client->old_h = client->h;
    client->h = window_changes.height = h;

    window_changes.border_width = client->border_pixels;

    for (Client *client_aux = client_next_tiled(live_monitor->clients);
                 client_aux;
                 client_aux = client_next_tiled(client_aux->next)) {
        n += 1;
    }

    if (!(client->is_floating)) {
        const Layout *layout = live_monitor->layout[live_monitor->lay_i];
        if (layout->function == monitor_layout_monocle || n == 1) {
            window_changes.border_width = 0;
            client->w = window_changes.width += client->border_pixels*2;
            client->h = window_changes.height += client->border_pixels*2;
        }
    }

    XConfigureWindow(display, client->window,
                     CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &window_changes);
    client_configure(client);
    XSync(display, False);
    return;
}

void
client_send_monitor(Client *client, Monitor *monitor) {
    if (client->monitor == monitor)
        return;

    client_unfocus(client, true);
    client_detach(client);
    client_detach_stack(client);

    client->monitor = monitor;
    client->tags = monitor->tagset[monitor->selected_tags];

    client_attach(client);
    client_attach_stack(client);
    client_set_client_tag_prop(client);
    client_focus(NULL);

    monitor_arrange(NULL);
    return;
}

void
client_set_client_state(Client *client, long state) {
    long data[] = { state, None };

    XChangeProperty(display, client->window,
                    wm_atoms[WMState], wm_atoms[WMState], 32,
                    PropModeReplace, (uchar *)data, 2);
    return;
}

bool
client_send_event(Client *client, Atom proto) {
    int n;
    Atom *protocols;
    bool exists = false;
    XEvent event;

    if (XGetWMProtocols(display, client->window, &protocols, &n)) {
        while (!exists && n--)
            exists = protocols[n] == proto;
        XFree(protocols);
    }
    if (exists) {
        event.type = ClientMessage;
        event.xclient.window = client->window;
        event.xclient.message_type = wm_atoms[WMProtocols];
        event.xclient.format = 32;
        event.xclient.data.l[0] = (long) proto;
        event.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, client->window, False, NoEventMask, &event);
    }
    return exists;
}

void
client_set_focus(Client *client) {
    if (!client->never_focus) {
        XSetInputFocus(display, client->window,
                       RevertToPointerRoot, CurrentTime);
        XChangeProperty(display, root, net_atoms[NetActiveWindow],
            XA_WINDOW, 32, PropModeReplace,
            (uchar *)&(client->window), 1);
    }
    client_send_event(client, wm_atoms[WMTakeFocus]);
    return;
}

void
client_set_fullscreen(Client *client, bool fullscreen) {
    if (fullscreen && !client->is_fullscreen) {
        XChangeProperty(display, client->window,
                        net_atoms[NetWMState], XA_ATOM, 32,
                        PropModeReplace,
                        (uchar*)&net_atoms[NetWMFullscreen], 1);
        client->is_fullscreen = true;
        if (client->is_fake_fullscreen) {
            client_resize_apply(client,
                                client->x, client->y, client->w, client->h);
            return;
        }
        client->old_state = client->is_floating;
        client->old_border_pixels = client->border_pixels;
        client->border_pixels = 0;
        client->is_floating = true;

        client_resize_apply(client,
                            client->monitor->mon_x, client->monitor->mon_y,
                            client->monitor->mon_w, client->monitor->mon_h);
        XRaiseWindow(display, client->window);
    } else if (!fullscreen && client->is_fullscreen) {
        XChangeProperty(display, client->window,
                        net_atoms[NetWMState], XA_ATOM, 32,
                        PropModeReplace, (uchar*)0, 0);
        client->is_fullscreen = false;
        if (client->is_fake_fullscreen) {
            client_resize_apply(client,
                                client->x, client->y, client->w, client->h);
            return;
        }
        client->is_floating = client->old_state;
        client->border_pixels = client->old_border_pixels;

        client->x = client->old_x;
        client->y = client->old_y;
        client->w = client->old_w;
        client->h = client->old_h;

        client_resize_apply(client, client->x, client->y, client->w, client->h);
        monitor_arrange(client->monitor);
    }
    return;
}

void
client_update_window_type(Client *client) {
    Atom state;
    Atom window_type;

    state = client_get_atom_property(client, net_atoms[NetWMState]);
    window_type = client_get_atom_property(client, net_atoms[NetWMWindowType]);

    if (state == net_atoms[NetWMFullscreen])
        client_set_fullscreen(client, true);
    if (window_type == net_atoms[NetWMWindowTypeDialog])
        client->is_floating = true;
    return;
}

void
client_update_wm_hints(Client *client) {
    XWMHints *wm_hints;
    bool urgent;

    if (!(wm_hints = XGetWMHints(display, client->window)))
        return;

    urgent = wm_hints->flags & XUrgencyHint;
    if (urgent && client == live_monitor->selected_client) {
        wm_hints->flags &= ~XUrgencyHint;
        XSetWMHints(display, client->window, wm_hints);
    } else {
        client->is_urgent = urgent;
        if (client->is_urgent) {
            XSetWindowBorder(display, client->window,
                             scheme[SchemeUrgent][ColBorder].pixel);
        }
    }

    if (wm_hints->flags & InputHint)
        client->never_focus = !wm_hints->input;
    else
        client->never_focus = false;

    XFree(wm_hints);
    return;
}

int
client_pixels_width(Client *client) {
    int width = client->w + 2*client->border_pixels;
    return width;
}

int
client_pixels_height(Client *client) {
    int height = client->h + 2*client->border_pixels;
    return height;
}

bool
client_is_visible(Client *client) {
    Monitor *monitor = client->monitor;
    uint monitor_tags = monitor->tagset[monitor->selected_tags];
    return client->tags & monitor_tags;
}

void
client_set_urgent(Client *client, bool urgent) {
    XWMHints *wm_hints;

    client->is_urgent = urgent;
    if (!(wm_hints = XGetWMHints(display, client->window)))
        return;

    if (urgent)
        wm_hints->flags = wm_hints->flags | XUrgencyHint;
    else
        wm_hints->flags = wm_hints->flags & ~XUrgencyHint;

    XSetWMHints(display, client->window, wm_hints);
    XFree(wm_hints);
    return;
}

void
client_show_hide(Client *client) {
    Monitor *mon;
    if (client == NULL)
        return;

    mon = client->monitor;

    if (client_is_visible(client)) {
        bool monitor_floating;

        if ((client->tags) && client->is_floating)
            client_center(client);

        /* show clients top down */
        XMoveWindow(display, client->window, client->x, client->y);

        monitor_floating = !mon->layout[mon->lay_i]->function;
        if ((monitor_floating || client->is_floating)
            && (!client->is_fullscreen || client->is_fake_fullscreen)) {
            client_resize(client,
                          client->x, client->y, client->w, client->h,
                          false);
        }
        client_show_hide(client->stack_next);
    } else {
        /* hide clients bottom up */
        client_show_hide(client->stack_next);
        XMoveWindow(display, client->window,
                    -2*client_pixels_width(client), client->y);
    }
    return;
}

void
client_set_client_tag_prop(Client *client) {
    long data[] = { (long) client->tags, (long) client->monitor->num };
    XChangeProperty(display, client->window, net_atoms[NetClientInfo],
                    XA_CARDINAL, 32, PropModeReplace, (uchar *)data, 2);
    return;
}

void
client_free_icon(Client *client) {
    if (client->icon) {
        XRenderFreePicture(display, client->icon);
        client->icon = None;
    }
    return;
}

void
client_unfocus(Client *client, bool set_focus) {
    if (client == NULL)
        return;

    client_grab_buttons(client, false);
    XSetWindowBorder(display, client->window,
                     scheme[SchemeNormal][ColBorder].pixel);

    if (set_focus) {
        XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display, root, net_atoms[NetActiveWindow]);
    }
    return;
}

void
client_update_size_hints(Client *client) {
    long supplied_return;
    bool has_maxes;
    bool mins_match_maxes;
    XSizeHints size_hints;
    int success;

    success = XGetWMNormalHints(display, client->window,
                               &size_hints, &supplied_return);
    if (!success) {
        /* size_hints is uninitialized,
         * ensure that size_hints.flags aren't used */
        size_hints.flags = PSize;
    }

    if (size_hints.flags & PBaseSize) {
        client->base_w = size_hints.base_width;
        client->base_h = size_hints.base_height;
    } else if (size_hints.flags & PMinSize) {
        client->base_w = size_hints.min_width;
        client->base_h = size_hints.min_height;
    } else {
        client->base_w = client->base_h = 0;
    }
    if (size_hints.flags & PResizeInc) {
        client->increment_w = size_hints.width_inc;
        client->increment_h = size_hints.height_inc;
    } else {
        client->increment_w = client->increment_h = 0;
    }
    if (size_hints.flags & PMaxSize) {
        client->max_w = size_hints.max_width;
        client->max_h = size_hints.max_height;
    } else {
        client->max_w = client->max_h = 0;
    }
    if (size_hints.flags & PMinSize) {
        client->min_w = size_hints.min_width;
        client->min_h = size_hints.min_height;
    } else if (size_hints.flags & PBaseSize) {
        client->min_w = size_hints.base_width;
        client->min_h = size_hints.base_height;
    } else {
        client->min_w = client->min_h = 0;
    }
    if (size_hints.flags & PAspect) {
        client->min_aspect = (float)size_hints.min_aspect.y
                             / (float)size_hints.min_aspect.x;
        client->max_aspect = (float)size_hints.max_aspect.x
                             / (float)size_hints.max_aspect.y;
    } else {
        client->max_aspect = client->min_aspect = 0.0;
    }

    has_maxes = client->max_w && client->max_h;
    mins_match_maxes = client->max_w == client->min_w
                       && client->max_h == client->min_h;
    client->is_fixed = has_maxes && mins_match_maxes;

    client->hintsvalid = true;
    return;
}

void
client_update_title(Client *client) {
    if (!get_text_property(client->window, net_atoms[NetWMName],
                           client->name, sizeof(client->name))) {
        get_text_property(client->window, XA_WM_NAME,
                          client->name, sizeof(client->name));
    }
    if (client->name[0] == '\0')
        strcpy(client->name, broken);
    return;
}

void
client_update_icon(Client *client) {
    Window window = client->window;
    Atom actual_type_return;
    int actual_format_return;
    ulong nitems_return;
    ulong bytes_after_return;
    ulong *prop_return = NULL;

    ulong *pixel_find = NULL;
    uint32 *pixel_find32;
    uint32 width_find, height_find;
    uint32 icon_width, icon_height;
    uint32 area_find = 0;
    uint *picture_width = &client->icon_width;
    uint *picture_height = &client->icon_height;
    int success;

    client_free_icon(client);
    success = XGetWindowProperty(display, window, net_atoms[NetWMIcon],
                                 0L, LONG_MAX, False, AnyPropertyType,
                                 &actual_type_return, &actual_format_return,
                                 &nitems_return, &bytes_after_return,
                                 (uchar **)&prop_return);
    if (success != Success)
        return;

    if (nitems_return == 0 || actual_format_return != 32) {
        XFree(prop_return);
        return;
    }

    do {
        ulong *pointer = prop_return;
        const ulong *end = prop_return + nitems_return;
        uint32 bstd = UINT32_MAX;
        uint32 d;

        while (pointer < (end - 1)) {
            uint32 max_dim;
            uint32 w = (uint32)*pointer++;
            uint32 h = (uint32)*pointer++;

            if (w >= 16384 || h >= 16384) {
                XFree(prop_return);
                return;
            }
            if ((area_find = w*h) > (end - pointer))
                break;

            max_dim = w > h ? w : h;
            if (max_dim >= ICONSIZE && (d = max_dim - ICONSIZE) < bstd) {
                bstd = d;
                pixel_find = pointer;
            }
            pointer += area_find;
        }

        if (pixel_find)
            break;

        pointer = prop_return;
        while (pointer < (end - 1)) {
            uint32 max_dim;
            uint32 w = (uint32)*pointer++;
            uint32 h = (uint32)*pointer++;

            if (w >= 16384 || h >= 16384) {
                XFree(prop_return);
                return;
            }
            if ((area_find = w*h) > (end - pointer))
                break;

            max_dim = w > h ? w : h;
            if ((d = ICONSIZE - max_dim) < bstd) {
                bstd = d;
                pixel_find = pointer;
            }
            pointer += area_find;
        }
    } while (false);

    if (!pixel_find) {
        XFree(prop_return);
        return;
    }

    width_find = (uint32)pixel_find[-2];
    height_find = (uint32)pixel_find[-1];
    if ((width_find == 0) || (height_find == 0)) {
        XFree(prop_return);
        return;
    }

    if (width_find <= height_find) {
        icon_height = ICONSIZE;
        icon_width = width_find*ICONSIZE / height_find;
        if (icon_width == 0)
            icon_width = 1;
    } else {
        icon_width = ICONSIZE;
        icon_height = height_find*ICONSIZE / width_find;
        if (icon_height == 0)
            icon_height = 1;
    }
    *picture_width = icon_width;
    *picture_height = icon_height;

    pixel_find32 = (uint32 *)pixel_find;
    for (uint32 i = 0; i < width_find*height_find; i += 1) {
        uint32 pixel = (uint32)pixel_find[i];
        uint8 a = pixel >> 24u;
        uint32 rb = (a*(pixel & 0xFF00FFu)) >> 8u;
        uint32 g = (a*(pixel & 0x00FF00u)) >> 8u;
        pixel_find32[i] = (rb & 0xFF00FFu) | (g & 0x00FF00u) | ((uint)a << 24u);
    }

    client->icon = drw_picture_create_resized(drw, (char *)pixel_find,
                                              width_find, height_find,
                                              icon_width, icon_height);
    XFree(prop_return);
    return;
}

void
monitor_arrange_monitor(Monitor *monitor) {
    strncpy(monitor->layout_symbol,
            monitor->layout[monitor->lay_i]->symbol,
            sizeof(monitor->layout_symbol));
    if (monitor->layout[monitor->lay_i]->function)
        monitor->layout[monitor->lay_i]->function(monitor);
    return;
}

void
monitor_focus(Monitor *monitor, bool set_focus) {
    client_unfocus(live_monitor->selected_client, set_focus);
    live_monitor = monitor;
    client_focus(NULL);
    return;
}

void
monitor_cleanup_monitor(Monitor *monitor) {
    if (monitor == monitors) {
        monitors = monitors->next;
    } else {
        Monitor *monitor_aux = monitors;
        while (monitor_aux && monitor_aux->next != monitor)
            monitor_aux = monitor_aux->next;
        monitor_aux->next = monitor->next;
    }
    XUnmapWindow(display, monitor->top_bar_window);

    XDestroyWindow(display, monitor->top_bar_window);
    XDestroyWindow(display, monitor->bottom_bar_window);

    free(monitor->pertag);
    free(monitor);
    return;
}

void
monitor_draw_bar(Monitor *monitor) {
    int x;
    int w;
    int text_pixels = 0;
    int urgent = 0;
    char tags_display[TAG_DISPLAY_SIZE];
    char *masters_names[LENGTH(tags)];
    Client *clients_with_icon[LENGTH(tags)] = {0};

    if (!monitor->show_top_bar)
        return;

    /* draw status first so it can be overdrawn by tags later */
    /* only drawn status on selected monitor */
    if (monitor == live_monitor) {
        drw_setscheme(drw, scheme[SchemeNormal]);

        draw_status_text(&status_top, monitor->win_w);
        text_pixels = status_top.pixels;
    }

    for (int i = 0; i < LENGTH(tags); i += 1) {
        masters_names[i] = NULL;
        clients_with_icon[i] = NULL;
    }

    for (Client *client = monitor->clients; client; client = client->next) {
        if (client->is_urgent)
            urgent |= client->tags;

        for (int i = 0; i < LENGTH(tags); i += 1) {
            if (client->icon && client->tags & (1 << i))
                clients_with_icon[i] = client;

            if (!masters_names[i] && client->tags & (1<<i)) {
                XClassHint class_hint = { NULL, NULL };
                XGetClassHint(display, client->window, &class_hint);
                masters_names[i] = class_hint.res_class;
            }
        }
    }

    x = 0;
    for (int i = 0; i < LENGTH(tags); i += 1) {
        Client *client_with_icon = clients_with_icon[i];
        char *master_name = masters_names[i];

        if (master_name) {
            if (client_with_icon) {
                snprintf(tags_display, sizeof(tags_display), "%s", tags[i]);
            } else {
                ulong n = strcspn(master_name, tag_label_delim);
                master_name[n] = '\0';
                snprintf(tags_display, sizeof(tags_display),
                         tag_label_format, tags[i], master_name);
            }
        } else {
            snprintf(tags_display, sizeof(tags_display),
                     tag_empty_format, tags[i]);
        }
        tag_width[i] = w = (int)get_text_pixels(tags_display);

        if (monitor->tagset[monitor->selected_tags] & 1 << i)
            drw_setscheme(drw, scheme[SchemeSelected]);
        else
            drw_setscheme(drw, scheme[SchemeNormal]);

        drw_text(drw, x, 0, (uint)w,
                 bar_height, (uint)text_padding/2,
                 tags_display, (int)urgent & 1 << i);
        x += w;
        if (client_with_icon) {
            drw_text(drw,
                     x, 0,
                     client_with_icon->icon_width + (uint)text_padding/2,
                     bar_height, 0, " ", urgent & 1 << i);
            drw_pic(drw,
                    x, (bar_height - client_with_icon->icon_height) / 2,
                    client_with_icon->icon_width, client_with_icon->icon_height,
                    client_with_icon->icon);
            x += client_with_icon->icon_width + (uint)text_padding/2;
            tag_width[i] += client_with_icon->icon_width + (uint)text_padding/2;
        }
    }
    w = (int)get_text_pixels(monitor->layout_symbol);
    drw_setscheme(drw, scheme[SchemeNormal]);
    x = drw_text(drw,
                 x, 0, (uint)w, bar_height, (uint)text_padding/2,
                 monitor->layout_symbol, 0);

    if ((w = monitor->win_w - text_pixels - x) > (int)bar_height) {
        int boxs = drw->fonts->h / 9;
        int boxw = drw->fonts->h / 6 + 2;

        if (monitor->selected_client) {
            int scheme_index;
            if (monitor == live_monitor)
                scheme_index = SchemeSelected;
            else
                scheme_index = SchemeNormal;
            drw_setscheme(drw, scheme[scheme_index]);

            drw_text(drw,
                     x, 0, (uint)w, bar_height,
                     (uint)text_padding/2, monitor->selected_client->name, 0);
            if (monitor->selected_client->is_floating)
                drw_rect(drw, x + boxs, boxs,
                         (uint)boxw, (uint)boxw,
                         monitor->selected_client->is_fixed, 0);
        } else {
            drw_setscheme(drw, scheme[SchemeNormal]);
            drw_rect(drw, x, 0, (uint)w, bar_height, 1, 1);
        }
    }
    drw_map(drw, monitor->top_bar_window,
            0, 0, (uint)monitor->win_w, bar_height);

    /* bottom bar */
    drw_setscheme(drw, scheme[SchemeNormal]);
    drw_rect(drw, 0, 0, (uint)monitor->win_w, bar_height, 1, 1);
    if (monitor == live_monitor)
        draw_status_text(&status_bottom, monitor->win_w);
    drw_map(drw, monitor->bottom_bar_window,
            0, 0, (uint)monitor->win_w, bar_height);
    return;
}

void
monitor_layout_columns(Monitor *monitor) {
    int i = 0;
    int number_tiled = 0;
    int x = 0;
    int y = 0;
    int mon_w;

    for (Client *client_aux = client_next_tiled(monitor->clients);
                 client_aux;
                 client_aux = client_next_tiled(client_aux->next)) {
        number_tiled += 1;
    }
    if (number_tiled == 0)
        return;
    if (number_tiled > 0) {
        snprintf(monitor->layout_symbol, sizeof(monitor->layout_symbol),
                 "|%d|", number_tiled);
    }

    if (number_tiled > monitor->number_masters) {
        if (monitor->number_masters != 0)
            mon_w = (int)((float)monitor->win_w*monitor->master_fact);
        else
            mon_w = 0;
    } else {
        mon_w = monitor->win_w;
    }

    for (Client *client = client_next_tiled(monitor->clients);
                 client;
                 client = client_next_tiled(client->next)) {
        int w;
        int h;
        if (i < monitor->number_masters) {
            w = (mon_w - x) / (MIN(number_tiled, monitor->number_masters) - i);
            client_resize(client,
                          x + monitor->win_x, monitor->win_y,
                          w - (2*client->border_pixels),
                          monitor->win_h - (2*client->border_pixels), false);
            x += client_pixels_width(client);
        } else {
            h = (monitor->win_h - y) / (number_tiled - i);
            client_resize(client,
                          x + monitor->win_x, monitor->win_y + y,
                          monitor->win_w - x - (2*client->border_pixels),
                          h - (2*client->border_pixels), false);
            y += client_pixels_height(client);
        }
        i += 1;
    }
    return;
}

void
monitor_layout_grid(Monitor *monitor) {
    int number_tiled = 0;
    int columns = 0;
    int rows;
    int col_i;
    int row_i;
    int column_width;
    int i = 0;

    for (Client *client = client_next_tiled(monitor->clients);
                 client;
                 client = client_next_tiled(client->next)) {
        number_tiled += 1;
    }
    if (number_tiled == 0)
        return;

    if (number_tiled > 0) {
        snprintf(monitor->layout_symbol, sizeof(monitor->layout_symbol),
                 "#%d#", number_tiled);
    }

    /* grid dimensions */
    while (columns*columns < number_tiled) {
        if (columns > (number_tiled / 2))
            break;
        columns += 1;
    }

    if (number_tiled == 5) {
        /* set layout against the general calculation: not 1:2:2, but 2:3 */
        columns = 2;
    }
    rows = number_tiled/columns;

    if (columns == 0)
        column_width = monitor->win_w;
    else
        column_width = monitor->win_w / columns;

    col_i = 0;
    row_i = 0;
    for (Client *client = client_next_tiled(monitor->clients);
                 client;
                 client = client_next_tiled(client->next)) {
        int client_height;
        int new_x;
        int new_y;
        int new_w;
        int new_h;

        if ((i/rows + 1) > (columns - number_tiled % columns))
            rows = number_tiled/columns + 1;

        client_height = monitor->win_h / rows;

        new_x = monitor->win_x + col_i*column_width;
        new_y = monitor->win_y + row_i*client_height;
        new_w = column_width - 2*client->border_pixels;
        new_h = client_height - 2*client->border_pixels;
        client_resize(client, new_x, new_y, new_w, new_h, false);

        row_i += 1;
        if (row_i >= rows) {
            row_i = 0;
            col_i += 1;
        }
        i += 1;
    }
    return;
}

void
monitor_layout_monocle(Monitor *monitor) {
    uint number_clients = 0;

    for (Client *client = monitor->clients; client; client = client->next) {
        if (client_is_visible(client))
            number_clients += 1;
    }

    if (number_clients > 0) {
        snprintf(monitor->layout_symbol, sizeof(monitor->layout_symbol),
                 "[%d]", number_clients);
    }

    for (Client *client = client_next_tiled(monitor->clients);
                 client;
                 client = client_next_tiled(client->next)) {
        int new_x = monitor->win_x;
        int new_y = monitor->win_y;
        int new_w = monitor->win_w - 2*client->border_pixels;
        int new_h = monitor->win_h - 2*client->border_pixels;
        client_resize(client, new_x, new_y, new_w, new_h, false);
    }
    return;
}

void
monitor_layout_tile(Monitor *monitor) {
    int number_tiled = 0;
    int i = 0;
    int mon_w = 0;
    int mon_y = 0;
    int tile_y = 0;

    for (Client *client_aux = client_next_tiled(monitor->clients);
                 client_aux;
                 client_aux = client_next_tiled(client_aux->next)) {
        number_tiled += 1;
    }
    if (number_tiled == 0)
        return;
    if (number_tiled > 0) {
        snprintf(monitor->layout_symbol, sizeof(monitor->layout_symbol),
                 "=%d|", number_tiled);
    }

    if (number_tiled > monitor->number_masters) {
        if (monitor->number_masters != 0)
            mon_w = (int)((float)monitor->win_w*monitor->master_fact);
        else
            mon_w = 0;
    } else {
        mon_w = monitor->win_w;
    }

    for (Client *client = client_next_tiled(monitor->clients);
                 client;
                 client = client_next_tiled(client->next)) {
        int h;
        int borders = 2*client->border_pixels;
        int min_number = MIN(number_tiled, monitor->number_masters);

        if (i < monitor->number_masters) {
            h = (monitor->win_h - mon_y) / (min_number - i);
            client_resize(client,
                          monitor->win_x, monitor->win_y + mon_y,
                          mon_w - borders, h - borders,
                          false);
            if (mon_y + client_pixels_height(client) < monitor->win_h)
                mon_y += client_pixels_height(client);
        } else {
            h = (monitor->win_h - tile_y) / (number_tiled - i);
            client_resize(client,
                          monitor->win_x + mon_w, monitor->win_y + tile_y,
                          monitor->win_w - mon_w - borders, h - borders,
                          false);
            if (tile_y + client_pixels_height(client) < monitor->win_h)
                tile_y += client_pixels_height(client);
        }
        i += 1;
    }
    return;
}

void
monitor_restack(Monitor *m) {
    XEvent event;

    monitor_draw_bar(m);
    if (!m->selected_client)
        return;

    if (m->selected_client->is_floating || !m->layout[m->lay_i]->function)
        XRaiseWindow(display, m->selected_client->window);

    if (m->layout[m->lay_i]->function) {
        XWindowChanges window_changes;
        window_changes.stack_mode = Below;
        window_changes.sibling = m->top_bar_window;

        for (Client *client = m->stack; client; client = client->stack_next) {
            if (!client->is_floating && client_is_visible(client)) {
                XConfigureWindow(display, client->window,
                                 CWSibling|CWStackMode, &window_changes);
                window_changes.sibling = client->window;
            }
        }
    }
    XSync(display, False);
    while (XCheckMaskEvent(display, EnterWindowMask, &event));
    return;
}

void
monitor_update_bar_position(Monitor *monitor) {
    monitor->win_y = monitor->mon_y;
    monitor->win_h = monitor->mon_h;

    if (monitor->show_top_bar) {
        monitor->win_h -= bar_height;
        monitor->top_bar_y = monitor->win_y;
        monitor->win_y = monitor->win_y + (int)bar_height;
    } else {
        monitor->top_bar_y = - (int)bar_height;
    }

    if (monitor->show_bottom_bar) {
        monitor->win_h -= bar_height;
        monitor->bottom_bar_y = monitor->win_y + monitor->win_h;
        monitor->win_y = monitor->win_y;
    } else {
        monitor->bottom_bar_y = - (int)bar_height;
    }
    return;
}

void
monitor_arrange(Monitor *monitor) {
    if (monitor) {
        client_show_hide(monitor->stack);
    } else {
        for (monitor = monitors; monitor; monitor = monitor->next)
            client_show_hide(monitor->stack);
    }
    if (monitor) {
        monitor_arrange_monitor(monitor);
        monitor_restack(monitor);
    } else {
        XEvent event;
        for (Monitor *monitor_aux = monitors;
                      monitor_aux;
                      monitor_aux = monitor_aux->next) {
            monitor_arrange_monitor(monitor_aux);
        }
        XSync(display, False);
        while (XCheckMaskEvent(display, EnterWindowMask, &event));
    }
    return;
}

Monitor *
create_monitor(void) {
    Monitor *monitor = ecalloc(1, sizeof(*monitor));
    Pertag *pertag = ecalloc(1, sizeof(*pertag));

    monitor->tagset[0] = monitor->tagset[1] = 1;
    monitor->master_fact = master_fact;
    monitor->number_masters = 1;
    monitor->show_top_bar = show_top_bar;
    monitor->show_bottom_bar = show_bottom_bar;

    monitor->layout[0] = &layouts[0];
    monitor->layout[1] = &layouts[1 % LENGTH(layouts)];
    strncpy(monitor->layout_symbol,
            layouts[0].symbol,
            sizeof(monitor->layout_symbol));

    pertag->tag = pertag->old_tag = 1;

    for (int i = 0; i <= LENGTH(tags); i += 1) {
        pertag->number_masters[i] = monitor->number_masters;
        pertag->master_facts[i] = monitor->master_fact;

        pertag->layouts[i][0] = monitor->layout[0];
        pertag->layouts[i][1] = monitor->layout[1];
        pertag->selected_layouts[i] = monitor->lay_i;

        pertag->top_bars[i] = monitor->show_top_bar;
        pertag->bottom_bars[i] = monitor->show_bottom_bar;
    }
    monitor->pertag = pertag;

    return monitor;
}

int
get_text_pixels(char *text) {
    int width = (int)(drw_fontset_getwidth(drw, text) + (uint)text_padding);
    return width;
}

int
get_root_pointer(int *x, int *y) {
    int di;
    uint dui;
    Window dummy;

    return XQueryPointer(display, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
get_window_state(Window window) {
    int actual_format_return;
    long result = -1;
    uchar *prop_return = NULL;
    ulong nitems_return;
    ulong bytes_after_return;
    Atom actual_type_return;
    int success;

    success = XGetWindowProperty(display, window, wm_atoms[WMState],
                                0L, 2L, False, wm_atoms[WMState],
                                &actual_type_return, &actual_format_return,
                                &nitems_return, &bytes_after_return,
                                (uchar **)&prop_return);
    if (success != Success)
        return -1;

    if (nitems_return != 0)
        result = *prop_return;
    XFree(prop_return);
    return result;
}

int
get_text_property(Window window, Atom atom, char *text, uint size) {
    char **list_return = NULL;
    int count_return;
    XTextProperty text_property;
    int success;

    if (!text || size == 0)
        return 0;
    text[0] = '\0';

    success = XGetTextProperty(display, window, &text_property, atom);
    if (!success || !text_property.nitems)
        return 0;

    if (text_property.encoding == XA_STRING) {
        strncpy(text, (char *)text_property.value, size - 1);
        text[size - 1] = '\0';
        XFree(text_property.value);
        return 1;
    }

    success = XmbTextPropertyToTextList(display, &text_property,
                                       &list_return, &count_return);
    if (success >= Success && count_return > 0 && *list_return) {
        strncpy(text, *list_return, size - 1);
        XFreeStringList(list_return);
    }

    text[size - 1] = '\0';
    XFree(text_property.value);
    return 1;
}

void
grab_keys(void) {
    uint modifiers[] = { 0, LockMask, numlock_mask, numlock_mask|LockMask };
    int first_keycode;
    int end;
    int skip;
    KeySym *key_sym;

    update_numlock_mask();

    XUngrabKey(display, AnyKey, AnyModifier, root);
    XDisplayKeycodes(display, &first_keycode, &end);

    key_sym = XGetKeyboardMapping(display,
                                  (uchar) first_keycode,
                                  (uchar) end - first_keycode + 1,
                                   &skip);
    if (!key_sym)
        return;

    for (int k = first_keycode; k <= end; k += 1) {
        for (int i = 0; i < LENGTH(keys); i += 1) {
            /* skip modifier codes, we do that ourselves */
            if (keys[i].keysym == key_sym[(k - first_keycode)*skip]) {
                for (int j = 0; j < LENGTH(modifiers); j += 1)
                    XGrabKey(display, k, (uint)keys[i].mod | modifiers[j],
                             root, True, GrabModeAsync, GrabModeAsync);
            }
        }
    }
    XFree(key_sym);
    return;
}

void
handler_others(XEvent *event) {
    switch (event->type) {
    case CirculateNotify:
        DWM_DEBUG("CirculateNotify");
        break;
    case CirculateRequest:
        DWM_DEBUG("CirculateRequest");
        break;
    case ColormapNotify:
        DWM_DEBUG("ColormapNotify");
        break;
    case CreateNotify:
        DWM_DEBUG("CreateNotify");
        break;
    case FocusOut:
        DWM_DEBUG("FocusOut");
        break;
    case GenericEvent:
        DWM_DEBUG("GenericEvent");
        break;
    case GraphicsExpose:
        DWM_DEBUG("GraphicsExpose");
        break;
    case GravityNotify:
        DWM_DEBUG("GravityNotify");
        break;
    case KeymapNotify:
        DWM_DEBUG("KeymapNotify");
        break;
    case LeaveNotify:
        DWM_DEBUG("LeaveNotify");
        break;
    case MapNotify:
        DWM_DEBUG("MapNotify");
        break;
    case NoExpose:
        DWM_DEBUG("NoExpose");
        break;
    case ReparentNotify:
        DWM_DEBUG("ReparentNotify");
        break;
    case ResizeRequest:
        DWM_DEBUG("ResizeRequest");
        break;
    case SelectionClear:
        DWM_DEBUG("SelectionClear");
        break;
    case SelectionNotify:
        DWM_DEBUG("SelectionNotify");
        break;
    case SelectionRequest:
        DWM_DEBUG("SelectionRequest");
        break;
    case VisibilityNotify:
        DWM_DEBUG("VisibilityNotify");
        break;
    default:
        DWM_DEBUG("unkown event type");
        break;
    }
    return;
}

void
handler_button_press(XEvent *event) {
    Arg arg = {0};
    Client *client;
    Monitor *monitor;
    XButtonPressedEvent *button_event = &event->xbutton;
    int button_x = button_event->x;
    uint click = ClickRootWin;

    /* focus monitor if necessary */
    monitor = window_to_monitor(button_event->window);
    if (monitor && monitor != live_monitor)
        monitor_focus(monitor, true);

    monitor = live_monitor;
    if (button_event->window == monitor->top_bar_window) {
        uint i = 0;
        int x = 0;

        do {
            x += tag_width[i];
        } while (button_x >= x && ++i < LENGTH(tags));

        if (i < LENGTH(tags)) {
            click = ClickBarTags;
            arg.ui = 1 << i;
        } else if (button_x < x + get_text_pixels(monitor->layout_symbol)) {
            click = ClickBarLayoutSymbol;
        } else if (button_x > monitor->win_w - status_top.pixels) {
            click = ClickBarStatus;
            status_get_signal_number(status_top.blocks_signal, button_x);
        } else {
            click = ClickBarTitle;
        }
    } else if (button_event->window == monitor->bottom_bar_window) {
        click = ClickBottomBar;
        status_get_signal_number(status_bottom.blocks_signal, button_x);
    } else if ((client = window_to_client(button_event->window))) {
        client_focus(client);
        monitor_restack(monitor);
        XAllowEvents(display, ReplayPointer, CurrentTime);
        click = ClickClientWin;
    }

    for (uint i = 0; i < LENGTH(buttons); i += 1) {
        if (click == buttons[i].click
            && buttons[i].function
            && (buttons[i].button == button_event->button)
            && CLEANMASK(buttons[i].mask) == CLEANMASK(button_event->state)) {
            const Arg *argument;

            if (click == ClickBarTags && buttons[i].arg.i == 0)
                argument = &arg;
            else
                argument = &buttons[i].arg;
            buttons[i].function(argument);
        }
    }
    return;
}

void draw_status_text(StatusBar *status_bar, int monitor_width) {
    int pixels = 0;
    int x0 = monitor_width - status_bar->pixels;

    for (int i = 0; i < status_bar->number_blocks; i += 1) {
        BlockSignal *block = &status_bar->blocks_signal[i];
        int text_pixels = block->max_x - block->min_x;

        block->max_x += x0;
        block->min_x += x0;

        if (text_pixels) {
            drw_text(drw,
                     x0 + pixels, 0, (uint)text_pixels, bar_height,
                     0, &(status_bar->text[block->text_i]), 0);
            pixels += text_pixels;
        }
    }
    return;
}

void
status_parse_text(StatusBar *status_bar) {
    BlockSignal *blocks = status_bar->blocks_signal;
    int i = 0;
    char *text = status_bar->text;
    char *status = status_bar->text;
    int total_pixels = 0;
    int text_pixels;
    char byte = *status;
    status += 1;

    for (text = status; *status; status += 1) {
        if ((uchar)(*status) < ' ') {
            blocks[i].signal = byte;
            byte = *status;
            *status = '\0';

            text_pixels = get_text_pixels(text) - text_padding;

            blocks[i].min_x = total_pixels;
            blocks[i].max_x = blocks[i].min_x + text_pixels;
            blocks[i].text_i = (int) (text - status_bar->text);

            total_pixels += text_pixels;
            i += 1;

            text = status + 1;
        }
    }
    blocks[i].signal = byte;

    text_pixels = (int)(get_text_pixels(text) - text_padding + 2);

    blocks[i].min_x = total_pixels;
    blocks[i].max_x = blocks[i].min_x + text_pixels;
    blocks[i].text_i = (int) (text - status_bar->text);

    total_pixels += text_pixels;
    status_bar->number_blocks = i + 1;
    status_bar->pixels = total_pixels;
    return;
}

void
status_get_signal_number(BlockSignal *blocks, int button_x) {
    status_signal = 0;

    for (int i = 0; i < STATUS_MAX_BLOCKS; i += 1) {
        if (blocks[i].min_x <= button_x && button_x <= blocks[i].max_x) {
            status_signal = blocks[i].signal;
            break;
        }
    }
    return;
}

void
handler_client_message(XEvent *event) {
    XClientMessageEvent *client_message_event = &event->xclient;
    Atom message_type = client_message_event->message_type;
    Client *client = window_to_client(client_message_event->window);

    if (client == NULL)
        return;

    if (message_type == net_atoms[NetWMState]) {
        ulong *data = (ulong *) client_message_event->data.l;

        if (data[1] == net_atoms[NetWMFullscreen]
            || data[2] == net_atoms[NetWMFullscreen]) {
            bool NET_WM_STATE_ADD = data[0] == 1;
            bool NET_WM_STATE_TOGGLE = data[0] == 2;
            bool fullscreen = NET_WM_STATE_ADD
                              || (NET_WM_STATE_TOGGLE
                                  && (!client->is_fullscreen
                                      || client->is_fake_fullscreen));
            client_set_fullscreen(client, fullscreen);
        }
    } else if (message_type == net_atoms[NetActiveWindow]) {
        if (client != live_monitor->selected_client && !client->is_urgent)
            client_set_urgent(client, true);
    }
    return;
}

void
handler_configure_request(XEvent *event) {
    Client *client;
    XConfigureRequestEvent *conf_request_event = &event->xconfigurerequest;
    XWindowChanges window_changes;
    bool mon_floating;

    mon_floating = !live_monitor->layout[live_monitor->lay_i]->function;

    if ((client = window_to_client(conf_request_event->window))) {
        if (conf_request_event->value_mask & CWBorderWidth) {
            client->border_pixels = conf_request_event->border_width;
            XSync(display, False);
            return;
        }

        if (client->is_floating || mon_floating) {
            Monitor *monitor;
            bool mask_xy;
            bool mask_hw;

            monitor = client->monitor;
            if (conf_request_event->value_mask & CWX) {
                client->old_x = client->x;
                client->x = monitor->mon_x + conf_request_event->x;
            }
            if (conf_request_event->value_mask & CWY) {
                client->old_y = client->y;
                client->y = monitor->mon_y + conf_request_event->y;
            }
            if (conf_request_event->value_mask & CWWidth) {
                client->old_w = client->w;
                client->w = conf_request_event->width;
            }
            if (conf_request_event->value_mask & CWHeight) {
                client->old_h = client->h;
                client->h = conf_request_event->height;
            }

            if (client->is_floating) {
                Monitor *m = monitor;
                int client_width = client_pixels_width(client);
                int client_height = client_pixels_height(client);

                if ((client->x + client->w) > (m->mon_x + m->mon_w))
                    client->x = m->mon_x + (m->mon_w / 2 - client_width / 2);
                if ((client->y + client->h) > (monitor->mon_y + monitor->mon_h))
                    client->y = m->mon_y + (m->mon_h / 2 - client_height / 2);
            }

            mask_xy = conf_request_event->value_mask & (CWX|CWY);
            mask_hw = conf_request_event->value_mask & (CWWidth|CWHeight);
            if (mask_xy && !mask_hw)
                client_configure(client);

            if (client_is_visible(client)) {
                XMoveResizeWindow(display, client->window,
                                  client->x, client->y,
                                  (uint)client->w, (uint)client->h);
            }
        } else {
            client_configure(client);
        }
    } else {
        window_changes.x = conf_request_event->x;
        window_changes.y = conf_request_event->y;
        window_changes.width = conf_request_event->width;
        window_changes.height = conf_request_event->height;
        window_changes.border_width = conf_request_event->border_width;
        window_changes.sibling = conf_request_event->above;
        window_changes.stack_mode = conf_request_event->detail;

        XConfigureWindow(display, conf_request_event->window,
                         (uint)conf_request_event->value_mask,
                         &window_changes);
    }
    XSync(display, False);
    return;
}

void
handler_configure_notify(XEvent *event) {
    XConfigureEvent *configure_event = &event->xconfigure;
    int dirty;

    if (configure_event->window != root)
        return;

    /* TODO: update_geometry handling sucks, needs to be simplified */
    dirty = screen_width != configure_event->width
            || screen_height != configure_event->height;
    screen_width = configure_event->width;
    screen_height = configure_event->height;

    if (update_geometry() || dirty) {
        drw_resize(drw, (uint)screen_width, bar_height);
        configure_bars_windows();
        for (Monitor *mon = monitors; mon; mon = mon->next) {
            for (Client *client = mon->clients; client; client = client->next) {
                if (client->is_fullscreen && !client->is_fake_fullscreen) {
                    client_resize_apply(client,
                                        mon->mon_x, mon->mon_y,
                                        mon->mon_w, mon->mon_h);
                }
            }
            XMoveResizeWindow(display, mon->top_bar_window,
                              mon->win_x, mon->top_bar_y,
                              (uint)mon->win_w, bar_height);
            XMoveResizeWindow(display, mon->bottom_bar_window,
                              mon->win_x, mon->bottom_bar_y,
                              (uint)mon->win_w, bar_height);
        }
        client_focus(NULL);
        monitor_arrange(NULL);
    }
    return;
}

void
handler_destroy_notify(XEvent *event) {
    Client *client;
    XDestroyWindowEvent *destroy_window_event = &event->xdestroywindow;

    if ((client = window_to_client(destroy_window_event->window)))
        client_unmanage(client, 1);
    return;
}

void
handler_enter_notify(XEvent *event) {
    Client *client;
    Monitor *monitor;
    XCrossingEvent *crossing_event = &event->xcrossing;
    bool is_root = crossing_event->window == root;
    bool notify_normal = crossing_event->mode == NotifyNormal;
    bool notify_inferior = crossing_event->detail == NotifyInferior;

    if (!is_root && (!notify_normal || notify_inferior))
        return;

    if ((client = window_to_client(crossing_event->window)))
        monitor = client->monitor;
    else
        monitor = window_to_monitor(crossing_event->window);

    if (monitor != live_monitor) {
        client_unfocus(live_monitor->selected_client, true);
        live_monitor = monitor;
    } else if (client == live_monitor->selected_client) {
        return;
    } else if (client == NULL) {
        return;
    }
    client_focus(client);
    return;
}

/* there are some broken focus acquiring clients needing extra handling */
void
handler_focus_in(XEvent *event) {
    XFocusChangeEvent *focus_change_event = &event->xfocus;

    if (!(live_monitor->selected_client))
        return;

    if (focus_change_event->window != live_monitor->selected_client->window)
        client_set_focus(live_monitor->selected_client);
    return;
}

void
handler_expose(XEvent *event) {
    Monitor *monitor;
    XExposeEvent *expose_event = &event->xexpose;

    if (expose_event->count != 0)
        return;
    if ((monitor = window_to_monitor(expose_event->window)))
        monitor_draw_bar(monitor);
    return;
}

void
handler_key_press(XEvent *event) {
    KeySym keysym;
    XKeyEvent *key_event = &event->xkey;
    keysym = XKeycodeToKeysym(display, (KeyCode)key_event->keycode, 0);

    for (uint i = 0; i < LENGTH(keys); i += 1) {
        if (keysym == keys[i].keysym
            && CLEANMASK(keys[i].mod) == CLEANMASK(key_event->state)
            && keys[i].function) {
            keys[i].function(&(keys[i].arg));
        }
    }
    return;
}

void
handler_mapping_notify(XEvent *event) {
    XMappingEvent *mapping_event = &event->xmapping;

    XRefreshKeyboardMapping(mapping_event);
    if (mapping_event->request == MappingKeyboard)
        grab_keys();
    return;
}

void
handler_map_request(XEvent *event) {
    static XWindowAttributes window_attributes;
    XMapRequestEvent *map_request_event = &event->xmaprequest;
    int success;

    success = XGetWindowAttributes(display,
                                  map_request_event->window,
                                  &window_attributes);
    if (!success)
        return;

    if (window_attributes.override_redirect)
        return;

    if (!window_to_client(map_request_event->window))
        client_new(map_request_event->window, &window_attributes);
    return;
}

void
handler_motion_notify(XEvent *event) {
    static Monitor *monitor_save = NULL;
    Monitor *m;
    XMotionEvent *motion_event = &event->xmotion;

    if (motion_event->window != root)
        return;

    m = rectangle_to_monitor(motion_event->x_root, motion_event->y_root, 1, 1);
    if (m != monitor_save && monitor_save)
        monitor_focus(m, true);

    monitor_save = m;
    return;
}

void
handler_property_notify(XEvent *event) {
    Client *client;
    Window trans;
    XPropertyEvent *property_event = &event->xproperty;

    if ((property_event->window == root)
        && (property_event->atom == XA_WM_NAME)) {
        status_update();
        monitor_draw_bar(live_monitor);
        return;
    }
    if (property_event->state == PropertyDelete)
        return;

    if (!(client = window_to_client(property_event->window)))
        return;

    switch (property_event->atom) {
    case XA_WM_TRANSIENT_FOR:
        if (!client->is_floating) {
            if (XGetTransientForHint(display, client->window, &trans)) {
                if (window_to_client(trans)) {
                    client->is_floating = true;
                    monitor_arrange(client->monitor);
                }
            }
        }
        break;
    case XA_WM_NORMAL_HINTS:
        client->hintsvalid = false;
        break;
    case XA_WM_HINTS:
        client_update_wm_hints(client);
        draw_bars();
        break;
    default:
        break;
    }

    if (property_event->atom == XA_WM_NAME
        || property_event->atom == net_atoms[NetWMName]) {
        client_update_title(client);
        if (client == client->monitor->selected_client)
            monitor_draw_bar(client->monitor);
    } else if (property_event->atom == net_atoms[NetWMIcon]) {
        client_update_icon(client);
        if (client == client->monitor->selected_client)
            monitor_draw_bar(client->monitor);
    }
    if (property_event->atom == net_atoms[NetWMWindowType])
        client_update_window_type(client);
    return;
}

void
handler_unmap_notify(XEvent *event) {
    Client *client;
    XUnmapEvent *unmap_event = &event->xunmap;

    if ((client = window_to_client(unmap_event->window))) {
        if (unmap_event->send_event) {
            client_set_client_state(client, WithdrawnState);
        } else {
            client_unmanage(client, 0);
        }
    }
    return;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handlers, which may call exit. */
int
handler_xerror(Display *error_display, XErrorEvent *error_event) {
    uchar error_code = error_event->error_code;
    uchar request_code = error_event->request_code;
    (void) error_display;

    if (error_code == BadWindow) {
        DWM_DEBUG("BadWindow");
        return 0;
    }

    switch (request_code) {
    case X_SetInputFocus:
        if (error_code == BadMatch) {
            DWM_DEBUG("X_SetInputFocus -> BadMatch");
            return 0;
        } else {
            DWM_DEBUG("X_SetInputFocus");
            goto default_handlers;
        }
    case X_PolyText8:
        if (error_code == BadDrawable) {
            DWM_DEBUG("X_PolyText8 -> BadDrawable");
            return 0;
        } else {
            DWM_DEBUG("X_PolyText8");
            goto default_handlers;
        }
    case X_PolyFillRectangle:
        if (error_code == BadDrawable) {
            DWM_DEBUG("X_PolyFillRectangle -> BadDrawable");
            return 0;
        } else {
            DWM_DEBUG("X_PolyFillRectangle");
            goto default_handlers;
        }
    case X_PolySegment:
        if (error_code == BadDrawable) {
            DWM_DEBUG("X_PolySegment -> BadDrawable");
            return 0;
        } else {
            DWM_DEBUG("X_PolySegment");
            goto default_handlers;
        }
    case X_ConfigureWindow:
        if (error_code == BadMatch) {
            DWM_DEBUG("X_ConfigureWindow -> BadMatch");
            return 0;
        } else {
            DWM_DEBUG("X_ConfigureWindow");
            goto default_handlers;
        }
    case X_GrabButton:
        if (error_code == BadAccess) {
            DWM_DEBUG("X_GrabButton -> BadAccess");
            return 0;
        } else {
            DWM_DEBUG("X_GrabButton");
            goto default_handlers;
        }
    case X_GrabKey:
        if (error_code == BadAccess) {
            DWM_DEBUG("X_GrabKey -> BadAccess");
            return 0;
        } else {
            goto default_handlers;
        }
    case X_CopyArea:
        if (error_code == BadDrawable) {
            DWM_DEBUG("X_CopyArea -> BadDrawable");
            return 0;
        } else {
            DWM_DEBUG("X_CopyArea");
            goto default_handlers;
        }
    default:
        DWM_DEBUG("Fatal error: request code=%d, error code=%d\n",
              request_code, error_code);
        goto default_handlers;
    }

default_handlers:
    return xerrorxlib(display, error_event); /* may call exit */
}

int
handler_xerror_dummy(Display *error_display, XErrorEvent *error_event) {
    (void) error_display;
    (void) error_event;
    return 0;
}

int
handler_xerror_start(Display *error_display, XErrorEvent *error_event) {
    (void) error_display;
    (void) error_event;
    error(__func__, "Error starting dwm: another window manager is running.\n");
    exit(EXIT_FAILURE);
}

#ifdef XINERAMA
static int
is_unique_geometry(XineramaScreenInfo *unique,
                   int n, XineramaScreenInfo *screen_info) {
    while (n--) {
        bool equal_x = unique[n].x_org == screen_info->x_org;
        bool equal_y = unique[n].y_org == screen_info->y_org;
        bool equal_w = unique[n].width == screen_info->width;
        bool equal_h = unique[n].height == screen_info->height;
        if (equal_x && equal_y && equal_w && equal_h)
            return 0;
    }
    return 1;
}
#endif /* XINERAMA */

Monitor *
rectangle_to_monitor(int x, int y, int w, int h) {
    Monitor *monitor = live_monitor;
    int a;
    int max_area = 0;

    for (Monitor *mon = monitors; mon; mon = mon->next) {
        int min_x = MIN(x + w, mon->win_x + mon->win_w);
        int min_y = MIN(y + h, mon->win_y + mon->win_h);

        int ax = MAX(0, min_x - MAX(x, mon->win_x));
        int ay = MAX(0, min_y - MAX(y, mon->win_y));

        if ((a = ax*ay) > max_area) {
            max_area = a;
            monitor = mon;
        }
    }
    return monitor;
}

Monitor *
direction_to_monitor(int direction) {
    Monitor *monitor = NULL;

    if (direction > 0) {
        if (!(monitor = live_monitor->next))
            monitor = monitors;
    } else if (live_monitor == monitors) {
        for (monitor = monitors;
             monitor->next;
             monitor = monitor->next);
    } else {
        for (monitor = monitors;
             monitor->next != live_monitor;
             monitor = monitor->next);
    }
    return monitor;
}

Client *
window_to_client(Window window) {
    for (Monitor *mon = monitors; mon; mon = mon->next) {
        for (Client *client = mon->clients; client; client = client->next) {
            if (client->window == window)
                return client;
        }
    }
    return NULL;
}

Monitor *
window_to_monitor(Window window) {
    Client *client;

    if (window == root) {
        int x;
        int y;
        if (get_root_pointer(&x, &y)) {
            Monitor *monitor = rectangle_to_monitor(x, y, 1, 1);
            return monitor;
        }
    }
    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        if (window == monitor->top_bar_window
            || window == monitor->bottom_bar_window) {
            return monitor;
        }
    }
    if ((client = window_to_client(window)))
        return client->monitor;

    return live_monitor;
}

void
focus_direction(int direction) {
    Client *selected = live_monitor->selected_client;
    Client *client = NULL;
    Client *client_aux;
    Client *next;
    uint best_score = UINT_MAX;

    if (!selected)
        return;

    next = selected->next;
    if (!next)
        next = selected->monitor->clients;
    for (client_aux = next; client_aux != selected; client_aux = next) {
        int client_score;
        int dist;

        next = client_aux->next;
        if (!next)
            next = selected->monitor->clients;

        if (!client_is_visible(client_aux) || client_aux->is_floating)
            continue;

        switch (direction) {
        case 0: // left (has preference -1)
            dist = selected->x - client_aux->x - client_aux->w;
            client_score = MIN(abs(dist), abs(dist + selected->monitor->win_w));
            client_score += abs(selected->y - client_aux->y) - 1;
            break;
        case 1: // right
            dist = client_aux->x - selected->x - selected->w;
            client_score = MIN(abs(dist), abs(dist + selected->monitor->win_w));
            client_score += abs(client_aux->y - selected->y);
            break;
        case 2: // up (has preference -1)
            dist = selected->y - client_aux->y - client_aux->h;
            client_score = MIN(abs(dist), abs(dist + selected->monitor->win_h));
            client_score += abs(selected->x - client_aux->x) - 1;
            break;
        default:
        case 3: // down
            dist = client_aux->y - selected->y - selected->h;
            client_score = MIN(abs(dist), abs(dist + selected->monitor->win_h));
            client_score += abs(client_aux->x - selected->x);
            break;
        }

        if ((uint)client_score < best_score) {
            best_score = (uint)client_score;
            client = client_aux;
        }
    }

    if (client && client != selected) {
        client_focus(client);
        monitor_restack(client->monitor);
    }
    return;
}

void
view_tag(uint arg_tags) {
    uint tmptag;
    uint tag;
    Monitor *monitor = live_monitor;

    if ((arg_tags & TAGMASK) == monitor->tagset[monitor->selected_tags])
        return;

    monitor->selected_tags ^= 1; /* toggle selected_client tagset */

    if (arg_tags & TAGMASK) {
        monitor->tagset[monitor->selected_tags] = arg_tags & TAGMASK;
        monitor->pertag->old_tag = monitor->pertag->tag;

        if (arg_tags == (uint)~0) {
            monitor->pertag->tag = 0;
        } else {
            uint i = 0;
            while (!(arg_tags & 1 << i))
                i += 1;
            monitor->pertag->tag = i + 1;
        }
    } else {
        tmptag = monitor->pertag->old_tag;
        monitor->pertag->old_tag = monitor->pertag->tag;
        monitor->pertag->tag = tmptag;
    }

    tag = monitor->pertag->tag;
    monitor->number_masters = monitor->pertag->number_masters[tag];
    monitor->master_fact = monitor->pertag->master_facts[tag];
    monitor->lay_i = monitor->pertag->selected_layouts[tag];
    monitor->layout[monitor->lay_i]
        = monitor->pertag->layouts[tag][monitor->lay_i];
    monitor->layout[monitor->lay_i^1]
        = monitor->pertag->layouts[tag][monitor->lay_i^1];

    if (monitor->show_top_bar != monitor->pertag->top_bars[tag])
        toggle_bar(BarTop);
    if (monitor->show_bottom_bar != monitor->pertag->bottom_bars[tag])
        toggle_bar(BarBottom);

    client_focus(NULL);
    monitor_arrange(monitor);
    return;
}

void
toggle_bar(int which) {
    Monitor *monitor = live_monitor;
    Pertag *pertag = monitor->pertag;
    Window bar_window;
    int bar_y;

    if (which == BarTop) {
        monitor->show_top_bar = !monitor->show_top_bar;
        pertag->top_bars[pertag->tag] = monitor->show_top_bar;

        monitor_update_bar_position(monitor);
        bar_window = monitor->top_bar_window;
        bar_y = monitor->top_bar_y;
    } else {
        monitor->show_bottom_bar = !monitor->show_bottom_bar;
        pertag->bottom_bars[pertag->tag] = monitor->show_bottom_bar;

        monitor_update_bar_position(monitor);
        bar_window = monitor->bottom_bar_window;
        bar_y = monitor->bottom_bar_y;
    }

    XMoveResizeWindow(display, bar_window,
                      monitor->win_x, bar_y,
                      (uint)monitor->win_w, bar_height);

    monitor_arrange(monitor);
    return;
}

void
set_layout(const Layout *layout) {
    Monitor *monitor = live_monitor;
    Pertag *pertag = monitor->pertag;

    if (!layout || layout != monitor->layout[monitor->lay_i]) {
        pertag->selected_layouts[pertag->tag] ^= 1;
        monitor->lay_i = pertag->selected_layouts[monitor->pertag->tag];
    }

    if (layout) {
        monitor->layout[monitor->lay_i] = layout;
        pertag->layouts[pertag->tag][monitor->lay_i] = layout;
    }

    strncpy(monitor->layout_symbol,
            monitor->layout[monitor->lay_i]->symbol,
            sizeof(monitor->layout_symbol));

    if (monitor->selected_client)
        monitor_arrange(monitor);
    else
        monitor_draw_bar(monitor);
    return;
}

void
focus_next(bool direction) {
    Monitor *monitor;
    Client *client;

    monitor = live_monitor;
    client = monitor->selected_client;
    while (client == NULL && monitor->next) {
        monitor = monitor->next;
        monitor_focus(monitor, true);
        client = monitor->selected_client;
    }
    if (client == NULL)
        return;

    if (direction) {
        if (client->all_next)
            client = client->all_next;
        else
            client = all_clients;
    } else {
        Client *last = client;
        if (last == all_clients)
            last = NULL;
        for (client = all_clients;
             client->all_next != last;
             client = client->all_next);
    }
    client_focus(client);
    return;
}

void
draw_bars(void) {
    for (Monitor *monitor = monitors; monitor; monitor = monitor->next)
        monitor_draw_bar(monitor);
    return;
}

void
scan_windows_once(void) {
    Window root_return;
    Window parent_return;
    Window *children_return = NULL;
    uint nchildren_return;
    XWindowAttributes window_attributes;
    int success;

    success = XQueryTree(display, root,
                         &root_return, &parent_return,
                         &children_return, &nchildren_return);
    if (!success)
        return;

    for (uint i = 0; i < nchildren_return; i += 1) {
        Window child = children_return[i];

        if (!XGetWindowAttributes(display, child, &window_attributes))
            continue;
        if (window_attributes.override_redirect)
            continue;
        if (XGetTransientForHint(display, child, &root_return))
            continue;

        if (window_attributes.map_state == IsViewable
            || get_window_state(child) == IconicState) {
            client_new(child, &window_attributes);
        }
    }

    /* now the transients */
    for (uint i = 0; i < nchildren_return; i += 1) {
        Window child = children_return[i];

        if (!XGetWindowAttributes(display, child, &window_attributes))
            continue;
        if (!XGetTransientForHint(display, child, &root_return))
            continue;

        if (window_attributes.map_state == IsViewable
            || get_window_state(child) == IconicState) {
            client_new(child, &window_attributes);
        }
    }

    if (children_return)
        XFree(children_return);
    return;
}

void
setup_once(void) {
    XVisualInfo *visual_infos;
    XVisualInfo vinfo_template;
    int nitems_return;
    long vinfo_mask = VisualScreenMask | VisualDepthMask | VisualClassMask;
    XSetWindowAttributes window_attributes;
    Atom UTF8STRING;
    struct sigaction signal_action;

    /* do not transform children into zombies when they terminate */
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
    signal_action.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &signal_action, NULL);

    /* clean up any zombies (inherited from .xinitrc etc) immediately */
    while (waitpid(-1, NULL, WNOHANG) > 0);

    /* init screen */
    screen = DefaultScreen(display);
    screen_width = DisplayWidth(display, screen);
    screen_height = DisplayHeight(display, screen);
    root = RootWindow(display, screen);

    vinfo_template.screen = screen;
    vinfo_template.depth = 32;
    vinfo_template.class = TrueColor;
    visual_infos = XGetVisualInfo(display,
                                  vinfo_mask, &vinfo_template, &nitems_return);

    visual = NULL;
    for (int i = 0; i < nitems_return; i += 1) {
        XRenderPictFormat *render_format;
        XVisualInfo visual_info = visual_infos[i];

        render_format = XRenderFindVisualFormat(display, visual_info.visual);
        if (render_format->type == PictTypeDirect
            && render_format->direct.alphaMask) {
            visual = visual_info.visual;
            depth = visual_info.depth;
            color_map = XCreateColormap(display, root, visual, AllocNone);
            break;
        }
    }

    XFree(visual_infos);

    if (!visual) {
        visual = DefaultVisual(display, screen);
        depth = DefaultDepth(display, screen);
        color_map = DefaultColormap(display, screen);
    }

    drw = drw_create(display, screen, root,
                     (uint)screen_width, (uint)screen_height,
                     visual, (uint)depth, color_map);
    if (!drw_fontset_create(drw, fonts, LENGTH(fonts))) {
        error(__func__, "Error loading fonts for dwm.\n");
        exit(EXIT_FAILURE);
    }
    text_padding = drw->fonts->h / 2;
    bar_height = drw->fonts->h + 2;
    update_geometry();

    /* init atoms */
    UTF8STRING = X_INTERN_ATOM("UTF8_STRING");
    wm_atoms[WMProtocols] = X_INTERN_ATOM("WM_PROTOCOLS");
    wm_atoms[WMDelete] = X_INTERN_ATOM("WM_DELETE_WINDOW");
    wm_atoms[WMState] = X_INTERN_ATOM("WM_STATE");
    wm_atoms[WMTakeFocus] = X_INTERN_ATOM("WM_TAKE_FOCUS");
    net_atoms[NetActiveWindow] = X_INTERN_ATOM("_NET_ACTIVE_WINDOW");
    net_atoms[NetSupported] = X_INTERN_ATOM("_NET_SUPPORTED");
    net_atoms[NetWMName] = X_INTERN_ATOM("_NET_WM_NAME");
    net_atoms[NetWMIcon] = X_INTERN_ATOM("_NET_WM_ICON");
    net_atoms[NetWMState] = X_INTERN_ATOM("_NET_WM_STATE");
    net_atoms[NetWMCheck] = X_INTERN_ATOM("_NET_SUPPORTING_WM_CHECK");
    net_atoms[NetWMFullscreen] = X_INTERN_ATOM("_NET_WM_STATE_FULLSCREEN");
    net_atoms[NetWMWindowType] = X_INTERN_ATOM("_NET_WM_WINDOW_TYPE");
    net_atoms[NetWMWindowTypeDialog] = X_INTERN_ATOM("_NET_WM_WINDOW_TYPE_DIALOG");
    net_atoms[NetClientList] = X_INTERN_ATOM("_NET_CLIENT_LIST");
    net_atoms[NetClientInfo] = X_INTERN_ATOM("_NET_CLIENT_INFO");

    /* init cursors */
    cursor[CursorNormal] = drw_cur_create(drw, XC_left_ptr);
    cursor[CursorResize] = drw_cur_create(drw, XC_sizing);
    cursor[CursorMove] = drw_cur_create(drw, XC_fleur);

    /* init appearance */
    scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
    for (int i = 0; i < LENGTH(colors); i += 1)
        scheme[i] = drw_scm_create(drw, colors[i], alphas[i], 3);

    /* init bars */
    configure_bars_windows();
    status_update();
    monitor_draw_bar(live_monitor);

    /* supporting window for NetWMCheck */
    wm_check_window = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(display, wm_check_window,
                    net_atoms[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (uchar *)&wm_check_window, 1);
    XChangeProperty(display, wm_check_window,
                    net_atoms[NetWMName], UTF8STRING, 8,
                    PropModeReplace, (uchar *)"dwm", 3);
    XChangeProperty(display, root,
                    net_atoms[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (uchar *)&wm_check_window, 1);

    /* EWMH support per view */
    XChangeProperty(display, root, net_atoms[NetSupported], XA_ATOM, 32,
        PropModeReplace, (uchar *)net_atoms, NetLast);
    XDeleteProperty(display, root, net_atoms[NetClientList]);
    XDeleteProperty(display, root, net_atoms[NetClientInfo]);

    /* select events */
    window_attributes.cursor = cursor[CursorNormal]->cursor;
    window_attributes.event_mask
        = SubstructureRedirectMask|SubstructureNotifyMask
          |ButtonPressMask|PointerMotionMask|EnterWindowMask
          |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;

    XChangeWindowAttributes(display, root,
                            CWEventMask|CWCursor, &window_attributes);
    XSelectInput(display, root, window_attributes.event_mask);
    grab_keys();
    client_focus(NULL);
    return;
}

void
configure_bars_windows(void) {
    XSetWindowAttributes window_attributes = {
        .override_redirect = True,
        .background_pixel = 0,
        .border_pixel = 0,
        .colormap = color_map,
        .event_mask = ButtonPressMask|ExposureMask
    };
    XClassHint class_hint = {"dwm", "dwm"};
    ulong value_mask = CWOverrideRedirect
                       | CWBackPixel | CWBorderPixel
                       | CWColormap |CWEventMask;

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        Window window;

        if (!monitor->top_bar_window) {
            window = XCreateWindow(display, root,
                                   monitor->win_x, monitor->top_bar_y,
                                   (uint)monitor->win_w, bar_height,
                                   0, depth, InputOutput, visual,
                                   value_mask, &window_attributes);
            monitor->top_bar_window = window;

            XDefineCursor(display,monitor->top_bar_window,
                          cursor[CursorNormal]->cursor);
            XMapRaised(display, monitor->top_bar_window);
            XSetClassHint(display, monitor->top_bar_window, &class_hint);
        }
        if (!monitor->bottom_bar_window) {
            window = XCreateWindow(display, root,
                                   monitor->win_x, monitor->bottom_bar_y,
                                   (uint)monitor->win_w, bar_height,
                                   0, depth, InputOutput, visual,
                                   value_mask, &window_attributes);
            monitor->bottom_bar_window = window;

            XDefineCursor(display, monitor->bottom_bar_window,
                          cursor[CursorNormal]->cursor);
            XMapRaised(display, monitor->bottom_bar_window);
            XSetClassHint(display, monitor->bottom_bar_window, &class_hint);
        }
    }
    return;
}

int
update_geometry(void) {
    bool dirty = false;

#ifdef XINERAMA
    if (XineramaIsActive(display)) {
        XineramaScreenInfo *screen_info;
        XineramaScreenInfo *unique = NULL;
        Client *client;
        Monitor *monitor;
        int i = 0;
        int j = 0;
        int number_monitors = 0;
        int number_unique;

        screen_info = XineramaQueryScreens(display, &number_unique);
        for (monitor = monitors; monitor; monitor = monitor->next)
            number_monitors += 1;

        /* only consider unique geometries as separate screens */
        unique = ecalloc((size_t) number_unique, sizeof(*unique));
        while (i < number_unique) {
            if (is_unique_geometry(unique, j, &screen_info[i])) {
                memcpy(&unique[j], &screen_info[i], sizeof(*unique));
                j += 1;
            }

            i += 1;
        }
        XFree(screen_info);
        number_unique = j;

        /* new monitors if number_unique > number_monitors */
        for (int k = number_monitors; k < number_unique; k += 1) {
            for (monitor = monitors;
                 monitor && monitor->next;
                 monitor = monitor->next);
            if (monitor)
                monitor->next = create_monitor();
            else
                monitors = create_monitor();
        }

        monitor = monitors;
        for (int k = 0; k < number_unique; k += 1) {
            bool unique_x = unique[k].x_org != monitor->mon_x;
            bool unique_y = unique[k].y_org != monitor->mon_y;
            bool unique_w = unique[k].width != monitor->mon_w;
            bool unique_h = unique[k].height != monitor->mon_h;

            if (k >= number_monitors
                || unique_x || unique_y || unique_w || unique_h) {
                dirty = true;
                monitor->num = k;
                monitor->mon_x = monitor->win_x = unique[k].x_org;
                monitor->mon_y = monitor->win_y = unique[k].y_org;
                monitor->mon_w = monitor->win_w = unique[k].width;
                monitor->mon_h = monitor->win_h = unique[k].height;
                monitor_update_bar_position(monitor);
            }

            if (!(monitor = monitor->next))
                break;
        }

        /* removed monitors if number_monitors > number_unique */
        for (int k = number_unique; k < number_monitors; k += 1) {
            for (monitor = monitors;
                 monitor && monitor->next;
                 monitor = monitor->next);

            while ((client = monitor->clients)) {
                dirty = true;
                monitor->clients = client->next;
                all_clients = client->all_next;
                client_detach_stack(client);
                client->monitor = monitors;
                client_attach(client);
                client_attach_stack(client);
            }

            if (monitor == live_monitor)
                live_monitor = monitors;
            monitor_cleanup_monitor(monitor);
        }
        free(unique);
    } else
#endif /* XINERAMA */
    { /* default monitor setup */
        if (!monitors)
            monitors = create_monitor();
        if (monitors->mon_w != screen_width
            || monitors->mon_h != screen_height) {
            dirty = true;
            monitors->mon_w = monitors->win_w = screen_width;
            monitors->mon_h = monitors->win_h = screen_height;
            monitor_update_bar_position(monitors);
        }
    }
    if (dirty) {
        live_monitor = monitors;
        live_monitor = window_to_monitor(root);
    }
    return dirty;
}

void
update_numlock_mask(void) {
    XModifierKeymap *modmap;

    numlock_mask = 0;
    modmap = XGetModifierMapping(display);
    for (int i = 0; i < 8; i += 1) {
        for (int j = 0; j < modmap->max_keypermod; j += 1) {
            KeyCode key_code = modmap->modifiermap[i*modmap->max_keypermod + j];
            if (key_code == XKeysymToKeycode(display, XK_Num_Lock))
                numlock_mask = (1 << i);
        }
    }
    XFreeModifiermap(modmap);
}

void
status_update(void) {
    char text[STATUS_BUFFER_SIZE*3];
    char *separator;

    if (!get_text_property(root, XA_WM_NAME, text, sizeof(text))) {
        strcpy(status_top.text, "dwm-"VERSION);
        status_top.pixels = get_text_pixels(status_top.text) - text_padding + 2;
        status_bottom.text[0] = '\0';
        return;
    }

    separator = strchr(text, status_separator);
    if (separator) {
        *separator = '\0';
        separator += 2;
        memcpy(status_bottom.text, separator, sizeof (status_bottom.text) - 1);
    } else {
        status_bottom.text[0] = '\0';
    }

    strncpy(status_top.text, text, sizeof(status_top.text) - 1);
    status_parse_text(&status_top);
    status_parse_text(&status_bottom);
    return;
}

int
main(int argc, char *argv[]) {
    if (argc == 2 && !strcmp("-v", argv[1])) {
        printf("dwm-"VERSION"\n");
        exit(EXIT_SUCCESS);
    } else if (argc != 1) {
        error(__func__, "usage: dwm [-v]");
        exit(EXIT_FAILURE);
    }

    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        error(__func__, "Warning: no locale support.\n");

    if (!(display = XOpenDisplay(NULL))) {
        error(__func__, "Error opening display.\n");
        exit(EXIT_FAILURE);
    }
    {
        xerrorxlib = XSetErrorHandler(handler_xerror_start);

        /* this causes an error if some other window manager is running */
        XSelectInput(display,
                     DefaultRootWindow(display),
                     SubstructureRedirectMask);

        XSync(display, False);
        XSetErrorHandler(handler_xerror);
        XSync(display, False);
    }

    setup_once();

#ifdef __OpenBSD__
    char *pledge_args = "stdio rpath proc exec";
    if (pledge(pledge_args, NULL) == -1) {
        error(__func__, "Error in pledge(%s)\n", pledge_args);
        exit(EXIT_FAILURE);
    }
#endif /* __OpenBSD__ */

    scan_windows_once();

    for (Monitor *m = monitors; m; m = m->next) {
        monitor_focus(m, false);

        view_tag(1 << 5);
        set_layout(&layouts[2]);

        toggle_bar(BarTop);
        toggle_bar(BarBottom);

        view_tag(1 << 1);
    }

    {
        XEvent event;
        XSync(display, False);

        while (dwm_running) {
            XNextEvent(display, &event);
            if (handlers[event.type])
                handlers[event.type](&event);
        }
    }

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        while (monitor->stack)
            client_unmanage(monitor->stack, 0);
    }

    XUngrabKey(display, AnyKey, AnyModifier, root);

    while (monitors)
        monitor_cleanup_monitor(monitors);

    if (dwm_restart) {
        error(__func__, "restarting...");
        execvp(argv[0], argv);
    }

    for (int i = 0; i < CursorLast; i += 1)
        drw_cur_free(drw, cursor[i]);
    for (int i = 0; i < LENGTH(colors); i += 1)
        free(scheme[i]);
    free(scheme);

    XDestroyWindow(display, wm_check_window);
    drw_free(drw);

    XSync(display, False);
    XSetInputFocus(display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(display, root, net_atoms[NetActiveWindow]);
    XCloseDisplay(display);

    exit(EXIT_SUCCESS);
}

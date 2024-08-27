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

/* macros */
#define BUTTONMASK (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         \
    (mask & ~(numlock_mask|LockMask) \
    & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define ISVISIBLE(C) ((C->tags & C->monitor->tagset[C->monitor->selected_tags]))
#define LENGTH(X) (int)(sizeof(X) / sizeof(*X))
#define MOUSEMASK (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)  ((X)->w + 2*(X)->border_pixels)
#define HEIGHT(X) ((X)->h + 2*(X)->border_pixels)
#define TAGMASK   ((1 << (LENGTH(tags))) - 1)
#define TEXT_PIXELS(X)  (drw_fontset_getwidth(drw, (X)) + lrpad)
#define PAUSE_MILIS_AS_NANOS(X) ((X) * 1000 * 1000)

#define OPAQUE 0xffU
#define TAG_DISPLAY_SIZE 32
#define GRAB_TRIES 10

#define X_INTERN_ATOM(X) XInternAtom(display, X, False)

/* enums */
enum { CursorNormal, CursorResize, CursorMove, CursorLast };
enum { SchemeNormal, SchemeInverse, SchemeSelected, SchemeUrgent }; /* color schemes */
enum { NetSupported, NetWMName, NetWMIcon, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType, /* EWMH atoms */
       NetWMWindowTypeDialog, NetClientList, NetClientInfo, NetLast };
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* atoms */
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
    void (*func)(const Arg *arg);
    const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
    char name[256];
    float min_a, max_a;

    int x, y, w, h;
    int stored_fx, stored_fy, stored_fw, stored_fh;
    int old_x, old_y, old_w, old_h;
    int base_w, base_h;
    int increment_w, increment_h;
    int max_w, max_h, min_w, min_h;
    int border_pixels;
    int old_border_pixels;
    uint tags;

    bool hintsvalid;
    bool is_fixed, is_floating, is_urgent;
    bool never_focus, old_state;
    bool is_fullscreen, is_fake_fullscreen;

    uint icon_width, icon_height;
    Picture icon;

    Client *next;
    Client *stack_next;
    Client *all_next;
    Monitor *monitor;
    Window window;
};

typedef struct {
    ulong mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

typedef struct {
    const char *symbol;
    void (*function)(Monitor *);
} Layout;

typedef struct Pertag Pertag;
struct Monitor {
    char layout_symbol[16];
    float master_fact;
    int number_masters;
    int num;
    int top_bar_y;
    int bottom_bar_y;
    int mon_x, mon_y, mon_w, mon_h;  /* screen size */
    int win_x, win_y, win_w, win_h;  /* window area */
    uint selected_tags;
    uint tagset[2];
    bool show_top_bar;
    bool show_bottom_bar;
    Client *clients;
    Client *selected_client;
    Client *stack;
    Monitor *next;
    Window top_bar_window;
    Window bottom_bar_window;
    const Layout *layout[2];
    uint lay_i;
    Pertag *pertag;
};

typedef struct {
    const char *class;
    const char *instance;
    const char *title;
    uint tags;
    uint switchtotag;
    int is_floating;
    int is_fake_fullscreen;
    int monitor;
} Rule;

/* function declarations */
static void user_alt_tab(const Arg *);
static void user_aspect_resize(const Arg *);
static void user_focus_direction(const Arg *);
static void user_focus_monitor(const Arg *);
static void user_focus_next(const Arg *);
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
static void user_toggle_bottom_bar(const Arg *);
static void user_toggle_floating(const Arg *);
static void user_toggle_fullscreen(const Arg *);
static void user_toggle_tag(const Arg *);
static void user_toggle_top_bar(const Arg *);
static void user_toggle_view(const Arg *);
static void user_view_tag(const Arg *);
static void user_window_view(const Arg *);

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

static void client_apply_rules(Client *);
static int client_apply_size_hints(Client *, int *, int *, int *, int *, int);
static void client_attach(Client *);
static void client_attach_stack(Client *);
static void client_configure(Client *);
static void client_detach(Client *);
static void client_detach_stack(Client *);
static void client_focus(Client *);
static Atom client_get_atom_property(Client *, Atom);
static void client_grab_buttons(Client *, int);
static Client *client_next_tiled(Client *);
static void client_pop(Client *);
static void client_resize(Client *, int, int, int, int, int);
static void client_resize_apply(Client *, int, int, int, int);
static bool client_send_event(Client *, Atom);
static void client_send_monitor(Client *, Monitor *);
static void client_set_client_state(Client *, long);
static void client_set_client_tag_prop(Client *);
static void client_set_focus(Client *);
static void client_set_fullscreen(Client *, int);
static void client_set_urgent(Client *, int);
static void client_show_hide(Client *);
static void client_free_icon(Client *);
static void client_unfocus(Client *, int client_set_focus);
static void client_unmanage(Client *, int);
static void client_update_size_hints(Client *);
static void client_update_title(Client *);
static void client_update_icon(Client *);
static void client_update_window_type(Client *);
static void client_update_wm_hints(Client *);

static void monitor_arrange(Monitor *);
static void monitor_arrange_monitor(Monitor *);
static void monitor_cleanup_monitor(Monitor *);
static void monitor_draw_bar(Monitor *);
static void monitor_layout_columns(Monitor *);
static void monitor_layout_gapless_grid(Monitor *);
static void monitor_layout_monocle(Monitor *);
static void monitor_layout_tile(Monitor *);
static void monitor_restack(Monitor *);
static void monitor_update_bar_position(Monitor *);

static Monitor *create_monitor(void);
static Monitor *direction_to_monitor(int);
static void dwm_debug(char *, ...);
static void draw_bars(void);
static int get_root_pointer(int *, int *);
static long get_window_state(Window);
static pid_t get_status_bar_pid(void);
static int get_text_property(Window, Atom, char *, uint);
static void grab_keys(void);

static void manage(Window, XWindowAttributes *);
static Monitor *rectangle_to_monitor(int, int, int, int);
static void scan_windows(void);
static void setup_once(void);
static int status_count_pixels(char *);
static void update_bars(void);
static void update_client_list(void);
static int update_geometry(void);
static void update_numlock_mask(void);
static void update_status(void);
static Client *window_to_client(Window);
static Monitor *window_to_monitor(Window);

/* variables */
static const char broken[] = "broken";
#define STATUS_TEXT_SIZE 256
static char top_status[STATUS_TEXT_SIZE];
static char bottom_status[STATUS_TEXT_SIZE];
static int status_text_pixels;
static int bottom_status_pixels;
static int status_signal;
static pid_t statuspid = -1;

static int screen;
static int screen_width;
static int screen_height;

static uint bar_height;  /* bar height */
static uint lrpad;      /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static uint numlock_mask = 0;

static void (*handler[LASTEvent]) (XEvent *) = {
    [ButtonPress] = handler_button_press,
    [ClientMessage] = handler_client_message,
    [ConfigureRequest] = handler_configure_request,
    [ConfigureNotify] = handler_configure_notify,
    [DestroyNotify] = handler_destroy_notify,
    [EnterNotify] = handler_enter_notify,
    [Expose] = handler_expose,
    [FocusIn] = handler_focus_in,
    [KeyPress] = handler_key_press,
    [MappingNotify] = handler_mapping_notify,
    [MapRequest] = handler_map_request,
    [MotionNotify] = handler_motion_notify,
    [PropertyNotify] = handler_property_notify,
    [UnmapNotify] = handler_unmap_notify
};

static Atom wmatom[WMLast], netatom[NetLast];
static Display *display;
static Visual *visual;
static Colormap cmap;
static Window root;
static Window wmcheckwin;
static int depth;

static bool dwm_restart = false;
static bool dwm_running = true;

static Cur *cursor[CursorLast];
static Clr **scheme;
static Drw *drw;

static Monitor *monitors;
static Monitor *current_monitor;
static Client *all_clients = NULL;
static int alt_tab_direction = 0;

/* configuration, allows nested code to access above variables */
#include "config.def.h"

struct Pertag {
    uint tag;
    uint old_tag;
    int number_masters[LENGTH(tags) + 1];
    float master_facts[LENGTH(tags) + 1];
    uint selected_layouts[LENGTH(tags) + 1];

    /* matrix of tags and layouts indexes */
    const Layout *layout_tags_indexes[LENGTH(tags) + 1][2];

    /* display bar for the current user_tag */
    bool top_bars[LENGTH(tags) + 1];
    bool bottom_bars[LENGTH(tags) + 1];
};

static int tag_width[LENGTH(tags)];

/* compile-time check if all tags fit into an uint bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

void
user_alt_tab(const Arg *) {
    bool grabbed = false;
    int grab_status = 1000;
    if (all_clients == NULL)
        return;

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next)
        user_view_tag(&(Arg){ .ui = (uint)~0 });
    user_focus_next(&(Arg){ .i = alt_tab_direction });

    for (int i = 0; i < GRAB_TRIES; i += 1) {
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

    while (grabbed) {
        XEvent event;
        Client *client;
        Monitor *monitor;

        XNextEvent(display, &event);
        switch (event.type) {
        case KeyPress:
            if (event.xkey.keycode == tabCycleKey)
                user_focus_next(&(Arg){ .i = alt_tab_direction });
            else if (event.xkey.keycode == key_j)
                user_focus_direction(&(Arg){ .i = 0 });
            else if (event.xkey.keycode == key_semicolon)
                user_focus_direction(&(Arg){ .i = 1 });
            else if (event.xkey.keycode == key_l)
                user_focus_direction(&(Arg){ .i = 2 });
            else if (event.xkey.keycode == key_k)
                user_focus_direction(&(Arg){ .i = 3 });
            break;
        case KeyRelease:
            if (event.xkey.keycode == tabModKey) {
                XUngrabKeyboard(display, CurrentTime);
                XUngrabButton(display, AnyButton, AnyModifier, None);
                grabbed = false;
                alt_tab_direction = !alt_tab_direction;
                user_window_view(0);
            }
            break;
        case ButtonPress: {
            XButtonPressedEvent *button_event = &(event.xbutton);
            monitor = window_to_monitor(button_event->window);
            if (monitor && (monitor != current_monitor)) {
                client_unfocus(current_monitor->selected_client, 1);
                current_monitor = monitor;
                client_focus(NULL);
            }
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
            user_window_view(0);
            break;
        default:
            break;
        }
    }
    return;
}

void
user_aspect_resize(const Arg *arg) {
    float ratio;
    int w;
    int h;
    int nw;
    int nh;
    Client *client = current_monitor->selected_client;

    if (!client || !arg)
        return;

    if (!client->is_floating
        && current_monitor->layout[current_monitor->lay_i]->function) {
        return;
    }

    ratio = (float)client->w / (float)client->h;
    h = arg->i;
    w = (int)(ratio*(float)h);

    nw = client->w + w;
    nh = client->h + h;

    XRaiseWindow(display, client->window);
    client_resize(client, client->x, client->y, nw, nh, True);
    return;
}

void
user_focus_direction(const Arg *arg) {
    Client *selected = current_monitor->selected_client;
    Client *client = NULL;
    Client *client_aux;
    Client *next;
    uint best_score = (uint)-1;

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

        if (!ISVISIBLE(client_aux) || client_aux->is_floating)
            continue;

        switch (arg->i) {
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
user_focus_monitor(const Arg *arg) {
    Monitor *monitor;

    if (!monitors->next)
        return;
    if ((monitor = direction_to_monitor(arg->i)) == current_monitor)
        return;

    client_unfocus(current_monitor->selected_client, 0);
    current_monitor = monitor;
    client_focus(NULL);
    return;
}

void
user_focus_next(const Arg *arg) {
    Monitor *m;
    Client *client;

    m = current_monitor;
    client = m->selected_client;
    while (client == NULL && m->next) {
        m = m->next;
        client_unfocus(current_monitor->selected_client, 1);
        current_monitor = m;
        client_focus(NULL);
        client = m->selected_client;
    }
    if (client == NULL)
        return;

    if (arg->i) {
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
user_focus_stack(const Arg *arg) {
    Client *client = NULL;

    if (!current_monitor->selected_client)
        return;
    if (current_monitor->selected_client->is_fullscreen && lockfullscreen)
        return;

    if (arg->i > 0) {
        for (client = current_monitor->selected_client->next;
             client && !ISVISIBLE(client);
             client = client->next);
        if (!client) {
            for (client = current_monitor->clients;
                 client && !ISVISIBLE(client);
                 client = client->next);
        }
    } else {
        Client *client_aux;
        for (client_aux = current_monitor->clients;
             client_aux != current_monitor->selected_client;
             client_aux = client_aux->next) {
            if (ISVISIBLE(client_aux))
                client = client_aux;
        }
        if (!client) {
            for (; client_aux; client_aux = client_aux->next) {
                if (ISVISIBLE(client_aux))
                    client = client_aux;
            }
        }
    }
    if (client) {
        client_focus(client);
        monitor_restack(current_monitor);
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
            client_unfocus(current_monitor->selected_client, 0);
            current_monitor = monitor;

            while (i < LENGTH(tags) && !((1 << i) & client->tags))
                i += 1;
            if (i < LENGTH(tags)) {
                const Arg a = {.ui = 1 << i};
                user_view_tag(&a);
                client_focus(client);
            }
        }
    }
    return;
}

void
user_increment_number_masters(const Arg *arg) {
    Monitor *monitor = current_monitor;
    int number_slaves = -1;
    int new_number_masters;
    uint tag;

    for (Client *client = monitor->clients;
                 client;
                 client = client_next_tiled(client->next)) {
        number_slaves += 1;
    }

    new_number_masters = MIN(monitor->number_masters + arg->i, number_slaves + 1);

    new_number_masters = MAX(new_number_masters, 0);
    tag = monitor->pertag->tag;
    monitor->number_masters = monitor->pertag->number_masters[tag] = new_number_masters;

    monitor_arrange(monitor);
    return;
}

void
user_kill_client(const Arg *) {
    Client *selected = current_monitor->selected_client;
    if (!selected)
        return;

    if (!client_send_event(selected, wmatom[WMDelete])) {
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
    int sucess;
    int x;
    int y;

    if (!(client = current_monitor->selected_client))
        return;

    /* no support moving fullscreen windows by mouse */
    if (client->is_fullscreen && !client->is_fake_fullscreen)
        return;

    monitor_restack(current_monitor);

    sucess = XGrabPointer(display, root, False,
                          MOUSEMASK, GrabModeAsync, GrabModeAsync,
                          None, cursor[CursorMove]->cursor, CurrentTime);
    if (sucess != GrabSuccess)
        return;

    if (!get_root_pointer(&x, &y))
        return;

    do {
        XMaskEvent(display, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &event);
        switch (event.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handler[event.type](&event);
            break;
        case MotionNotify: {
            Monitor *monitor = current_monitor;
            bool is_floating = client->is_floating;
            int nx = client->x + (event.xmotion.x - x);
            int ny = client->y + (event.xmotion.y - y);
            int over_x[2] = {
                abs(monitor->win_x - nx),
                abs((monitor->win_x + monitor->win_w) - (nx + WIDTH(client))),
            };
            int over_y[2] = {
                abs(monitor->win_y - ny),
                abs((monitor->win_y + monitor->win_h) - (ny + HEIGHT(client))),
            };

            if ((event.xmotion.time - last_time) <= (1000 / 60))
                continue;
            last_time = event.xmotion.time;

            if (over_x[0] < SNAP_PIXELS)
                nx = monitor->win_x;
            else if (over_x[1] < SNAP_PIXELS)
                nx = monitor->win_x + monitor->win_w - WIDTH(client);

            if (over_y[0] < SNAP_PIXELS)
                ny = monitor->win_y;
            else if (over_y[1] < SNAP_PIXELS)
                ny = monitor->win_y + monitor->win_h - HEIGHT(client);

            if (!is_floating && monitor->layout[monitor->lay_i]->function) {
                bool moving_x = abs(nx - client->x) > SNAP_PIXELS;
                bool moving_y = abs(ny - client->y) > SNAP_PIXELS;
                if (moving_x || moving_y)
                    user_toggle_floating(NULL);
            }

            if (!monitor->layout[monitor->lay_i]->function || is_floating)
                client_resize(client, nx, ny, client->w, client->h, 1);
            break;
        }
        default:
            break;
        }
    } while (event.type != ButtonRelease);

    XUngrabPointer(display, CurrentTime);

    monitor_aux = rectangle_to_monitor(client->x, client->y,
                                       client->w, client->h);
    if (monitor_aux != current_monitor) {
        client_send_monitor(client, monitor_aux);
        current_monitor = monitor_aux;
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
    int sucess;

    if (!(client = current_monitor->selected_client))
        return;

    /* no support resizing fullscreen windows by mouse */
    if (client->is_fullscreen && !client->is_fake_fullscreen)
        return;

    monitor_restack(current_monitor);

    sucess = XGrabPointer(display, root, False,
                          MOUSEMASK, GrabModeAsync, GrabModeAsync,
                          None, cursor[CursorResize]->cursor, CurrentTime);
    if (sucess != GrabSuccess)
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
            handler[event.type](&event);
            break;
        case MotionNotify: {
            bool monitor_floating;
            bool over_x;
            bool under_x;
            bool over_y;
            bool under_y;
            int nw;
            int nh;

            if ((event.xmotion.time - last_time) <= (1000 / 60))
                continue;
            last_time = event.xmotion.time;

            event.xmotion.x += (-client->x - 2*client->border_pixels + 1);
            event.xmotion.y += (-client->y - 2*client->border_pixels + 1);

            nw = MAX(event.xmotion.x, 1);
            nh = MAX(event.xmotion.y, 1);
            over_x = client->monitor->win_x + nw >= current_monitor->win_x;
            under_x =  client->monitor->win_x + nw <= current_monitor->win_x + current_monitor->win_w;
            over_y = client->monitor->win_y + nh >= current_monitor->win_y;
            under_y =  client->monitor->win_y + nh <= current_monitor->win_y + current_monitor->win_h;
            monitor_floating = !(current_monitor->layout[current_monitor->lay_i]->function);

            if (over_x && under_x && over_y && under_y) {
                bool over_snap_x = abs(nw - client->w) > SNAP_PIXELS;
                bool over_snap_y = abs(nh - client->h) > SNAP_PIXELS;
                if (!client->is_floating && !monitor_floating
                    && (over_snap_x || over_snap_y)) {
                    user_toggle_floating(NULL);
                }
            }
            if (client->is_floating || monitor_floating)
                client_resize(client, client->x, client->y, nw, nh, 1);
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
    if (monitor != current_monitor) {
        client_send_monitor(client, monitor);
        current_monitor = monitor;
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
    const Layout *layout = arg->v;
    Monitor *monitor = current_monitor;

    if (!arg || !arg->v || arg->v != monitor->layout[monitor->lay_i])
        monitor->lay_i = monitor->pertag->selected_layouts[monitor->pertag->tag] ^= 1;

    if (arg && arg->v)
        monitor->layout[monitor->lay_i]
            = monitor->pertag->layout_tags_indexes[monitor->pertag->tag][monitor->lay_i]
            = layout;

    strncpy(monitor->layout_symbol,
            monitor->layout[monitor->lay_i]->symbol,
            sizeof(monitor->layout_symbol));

    if (monitor->selected_client)
        monitor_arrange(monitor);
    else
        monitor_draw_bar(monitor);
    return;
}

/* arg > 1.0 will set master_fact absolutely */
void
user_set_master_fact(const Arg *arg) {
    float factor;
    uint tag = current_monitor->pertag->tag;

    if (!arg)
        return;
    if (!current_monitor->layout[current_monitor->lay_i]->function)
        return;

    if (arg->f < 1.0f)
        factor = arg->f + current_monitor->master_fact;
    else
        factor = arg->f - 1.0f;

    if (factor < 0.05f || factor > 0.95f)
        return;

    current_monitor->master_fact = current_monitor->pertag->master_facts[tag] = factor;
    monitor_arrange(current_monitor);
    return;
}

void
user_signal_status_bar(const Arg *arg) {
    union sigval signal_value;

    if (!status_signal)
        return;
    signal_value.sival_int = arg->i | ((SIGRTMIN+status_signal) << 3);

    if ((statuspid = get_status_bar_pid()) <= 0)
        return;

    sigqueue(statuspid, SIGUSR1, signal_value);
}

void
user_tag(const Arg *arg) {
    Client *selected_client = current_monitor->selected_client;
    uint which_tag = arg->ui & TAGMASK;

    if (which_tag && selected_client) {
        selected_client->tags = which_tag;
        client_set_client_tag_prop(selected_client);
        client_focus(NULL);
        monitor_arrange(current_monitor);
    }
}

void
user_tag_monitor(const Arg *arg) {
    Monitor *monitor = direction_to_monitor(arg->i);

    if (!current_monitor->selected_client || !monitors->next)
        return;

    if (current_monitor->selected_client->is_floating) {
        current_monitor->selected_client->x += monitor->mon_x - current_monitor->mon_x;
        current_monitor->selected_client->y += monitor->mon_y - current_monitor->mon_y;
    }

    client_send_monitor(current_monitor->selected_client, monitor);
    usleep(50);
    client_focus(NULL);
    usleep(50);
    user_focus_monitor(arg);
    user_toggle_floating(NULL);
    user_toggle_floating(NULL);
    return;
}

void
user_toggle_top_bar(const Arg *) {
    Monitor *monitor = current_monitor;

    monitor->show_top_bar
        = monitor->pertag->top_bars[monitor->pertag->tag]
        = !monitor->show_top_bar;

    monitor_update_bar_position(monitor);
    XMoveResizeWindow(display, monitor->top_bar_window,
                      monitor->win_x, monitor->top_bar_y,
                      (uint)monitor->win_w, bar_height);

    monitor_arrange(monitor);
    return;
}

void
user_toggle_bottom_bar(const Arg *) {
    Monitor *monitor = current_monitor;

    monitor->show_bottom_bar
        = monitor->pertag->bottom_bars[monitor->pertag->tag]
        = !monitor->show_bottom_bar;
    monitor_update_bar_position(monitor);
    XMoveResizeWindow(display, monitor->bottom_bar_window,
                      monitor->win_x, monitor->bottom_bar_y,
                      (uint)monitor->win_w, bar_height);
    monitor_arrange(monitor);
    return;
}

void
user_toggle_floating(const Arg *) {
    Monitor *monitor = current_monitor;
    Client *client = monitor->selected_client;

    if (!monitor->selected_client)
        return;

    /* no support for fullscreen windows */
    if (client->is_fullscreen && !client->is_fake_fullscreen)
        return;

    client->is_floating = !client->is_floating || client->is_fixed;
    if (client->is_floating) {
        client_resize(client,
                      client->stored_fx, client->stored_fy,
                      client->stored_fw, client->stored_fh,
                      False);
    } else {
        client->stored_fx = client->x;
        client->stored_fy = client->y;
        client->stored_fw = client->w;
        client->stored_fh = client->h;
    }

    client->x = client->monitor->mon_x + (client->monitor->mon_w - WIDTH(client)) / 2;
    client->y = client->monitor->mon_y + (client->monitor->mon_h - HEIGHT(client)) / 2;

    monitor_arrange(monitor);
    return;
}

void
user_toggle_fullscreen(const Arg *) {
    Client *client = current_monitor->selected_client;
    if (client)
        client_set_fullscreen(client, !client->is_fullscreen);
    return;
}

void
user_spawn(const Arg *arg) {
   struct sigaction sig_action;

   if (fork() == 0) {
       if (display)
           close(ConnectionNumber(display));
       setsid();

       sigemptyset(&sig_action.sa_mask);
       sig_action.sa_flags = 0;
       sig_action.sa_handler = SIG_DFL;
       sigaction(SIGCHLD, &sig_action, NULL);

       execvp(((char *const *)arg->v)[0], (char *const *)arg->v);
       die("dwm: execvp '%s' failed:", ((char *const *)arg->v)[0]);
   }
   return;
}

void
user_toggle_tag(const Arg *arg) {
    uint newtags;

    if (!current_monitor->selected_client)
        return;
    newtags = current_monitor->selected_client->tags ^ (arg->ui & TAGMASK);
    if (newtags) {
        current_monitor->selected_client->tags = newtags;
        client_set_client_tag_prop(current_monitor->selected_client);
        client_focus(NULL);
        monitor_arrange(current_monitor);
    }
    return;
}

void
user_toggle_view(const Arg *arg) {
    Monitor *monitor = current_monitor;
    uint new_tagset = monitor->tagset[monitor->selected_tags] ^ (arg->ui & TAGMASK);

    if (new_tagset) {
        uint tag;
        monitor->tagset[monitor->selected_tags] = new_tagset;

        if (new_tagset == (uint)~0) {
            monitor->pertag->old_tag = monitor->pertag->tag;
            monitor->pertag->tag = 0;
        }

        /* test if the user did not select the same user_tag */
        if (!(new_tagset & 1 << (monitor->pertag->tag - 1))) {
            uint i = 0;
            monitor->pertag->old_tag = monitor->pertag->tag;
            while (!(new_tagset & 1 << i))
                i += 1;
            monitor->pertag->tag = i + 1;
        }

        tag = monitor->pertag->tag;

        /* apply settings for this view */
        monitor->number_masters = monitor->pertag->number_masters[tag];
        monitor->master_fact = monitor->pertag->master_facts[tag];
        monitor->lay_i = monitor->pertag->selected_layouts[tag];
        monitor->layout[monitor->lay_i]
            = monitor->pertag->layout_tags_indexes[tag][monitor->lay_i];
        monitor->layout[monitor->lay_i^1]
            = monitor->pertag->layout_tags_indexes[tag][monitor->lay_i^1];

        if (monitor->show_top_bar != monitor->pertag->top_bars[tag])
            user_toggle_top_bar(NULL);
        if (monitor->show_bottom_bar != monitor->pertag->bottom_bars[tag])
            user_toggle_bottom_bar(NULL);

        client_focus(NULL);
        monitor_arrange(monitor);
    }
    return;
}

void
user_view_tag(const Arg *arg) {
    uint arg_tags = arg->ui;
    uint tmptag;
    uint tag;
    Monitor *monitor = current_monitor;

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
        = monitor->pertag->layout_tags_indexes[tag][monitor->lay_i];
    monitor->layout[monitor->lay_i^1]
        = monitor->pertag->layout_tags_indexes[tag][monitor->lay_i^1];

    if (monitor->show_top_bar != monitor->pertag->top_bars[tag])
        user_toggle_top_bar(NULL);
    if (monitor->show_bottom_bar != monitor->pertag->bottom_bars[tag])
        user_toggle_bottom_bar(NULL);

    client_focus(NULL);
    monitor_arrange(monitor);
    return;
}

/* Selects for the view of the focused window. The list of tags */
/* to be displayed is matched to the focused window user_tag list. */
void
user_window_view(const Arg *) {
    Window window;
    Window root_return;
    Window parent_return;
    Window *children_return;
    uint nchildren_return;
    int unused;
    Client *client;
    Arg view_arg;

    if (!XGetInputFocus(display, &window, &unused))
        return;

    while (XQueryTree(display, window,
                      &root_return, &parent_return, &children_return,
                      &nchildren_return) && parent_return != root_return) {
        window = parent_return;
    }

    if (!(client = window_to_client(window)))
        return;

    view_arg.ui = client->tags;
    user_view_tag(&view_arg);
    return;
}

void
user_promote_to_master(const Arg *) {
    Client *client = current_monitor->selected_client;
    Monitor *monitor = current_monitor;
    bool monitor_floating = !monitor->layout[monitor->lay_i]->function;
    bool is_next_tiled = client == client_next_tiled(monitor->clients);

    if (!client || monitor_floating || client->is_floating)
        return;
    if (is_next_tiled && !(client = client_next_tiled(client->next)))
        return;
    client_pop(client);
    return;
}

void
client_apply_rules(Client *client) {
    const char *class, *instance;
    XClassHint class_hint = { NULL, NULL };

    /* rule matching */
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

            if (rule->is_floating) {
                Monitor *monitor = client->monitor;
                client->x = monitor->win_x + monitor->win_w / 2 - WIDTH(client) / 2;
                client->y = monitor->win_y + monitor->win_h / 2 - HEIGHT(client) / 2;
            }

            for (monitor_aux = monitors;
                 monitor_aux && monitor_aux->num != rule->monitor;
                 monitor_aux = monitor_aux->next);
            if (monitor_aux)
                client->monitor = monitor_aux;

            if (rule->switchtotag) {
                Arg a = { .ui = rule->tags };
                user_view_tag(&a);
            }
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
client_apply_size_hints(Client *client, int *x, int *y, int *w, int *h, int interact) {
    int baseismin;
    Monitor *monitor = client->monitor;

    *w = MAX(1, *w);
    *h = MAX(1, *h);

    if (interact) {
        if (*x > screen_width)
            *x = screen_width - WIDTH(client);
        if (*y > screen_height)
            *y = screen_height - HEIGHT(client);
        if (*x + *w + 2*client->border_pixels < 0)
            *x = 0;
        if (*y + *h + 2*client->border_pixels < 0)
            *y = 0;
    } else {
        if (*x >= monitor->win_x + monitor->win_w)
            *x = monitor->win_x + monitor->win_w - WIDTH(client);
        if (*y >= monitor->win_y + monitor->win_h)
            *y = monitor->win_y + monitor->win_h - HEIGHT(client);
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
        if (!client->hintsvalid)
            client_update_size_hints(client);

        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = client->base_w == client->min_w && client->base_h == client->min_h;
        if (!baseismin) { /* temporarily remove base dimensions */
            *w -= client->base_w;
            *h -= client->base_h;
        }

        /* adjust for aspect limits */
        if (client->min_a > 0 && client->max_a > 0) {
            if (client->max_a < (float)*w / (float)*h)
                *w = *h*((int)(client->max_a + 0.5f));
            else if (client->min_a < (float)*h / (float) *w)
                *h = *w*((int)(client->min_a + 0.5f));
        }

        if (baseismin) { /* increment calculation requires this */
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
    return *x != client->x || *y != client->y || *w != client->w || *h != client->h;
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
    XDestroyWindow(display, monitor->bottom_bar_window);
    XDestroyWindow(display, monitor->top_bar_window);
    XDestroyWindow(display, monitor->bottom_bar_window);
    free(monitor);
    return;
}

void
client_configure(Client *client) {
    XConfigureEvent conf_event;

    conf_event.type = ConfigureNotify;
    conf_event.display = display;
    conf_event.event = client->window;
    conf_event.window = client->window;
    conf_event.x = client->x;
    conf_event.y = client->y;
    conf_event.width = client->w;
    conf_event.height = client->h;
    conf_event.border_width = client->border_pixels;
    conf_event.above = None;
    conf_event.override_redirect = False;
    XSendEvent(display, client->window,
               False, StructureNotifyMask, (XEvent *)&conf_event);
    return;
}

Monitor *
create_monitor(void) {
    Monitor *monitor = ecalloc(1, sizeof(*monitor));

    monitor->tagset[0] = monitor->tagset[1] = 1;
    monitor->master_fact = master_fact;
    monitor->number_masters = number_masters;
    monitor->show_top_bar = show_top_bar;
    monitor->show_bottom_bar = show_bottom_bar;
    monitor->layout[0] = &layouts[0];
    monitor->layout[1] = &layouts[1 % LENGTH(layouts)];
    strncpy(monitor->layout_symbol,
            layouts[0].symbol,
            sizeof(monitor->layout_symbol));
    monitor->pertag = ecalloc(1, sizeof(*(monitor->pertag)));
    monitor->pertag->tag = monitor->pertag->old_tag = 1;

    for (int i = 0; i <= LENGTH(tags); i += 1) {
        monitor->pertag->number_masters[i] = monitor->number_masters;
        monitor->pertag->master_facts[i] = monitor->master_fact;

        monitor->pertag->layout_tags_indexes[i][0] = monitor->layout[0];
        monitor->pertag->layout_tags_indexes[i][1] = monitor->layout[1];
        monitor->pertag->selected_layouts[i] = monitor->lay_i;

        monitor->pertag->top_bars[i] = monitor->show_top_bar;
        monitor->pertag->bottom_bars[i] = monitor->show_bottom_bar;
    }

    return monitor;
}

void dwm_debug(char *message, ...) {
    char buffer[256];
    char *argv[6] = {
        [0] = "dunstify",
        [1] = "-t",
        [2] = "900",
        [3] = "dwm",
        [4] = NULL,
        [5] = NULL,
    };

    va_list args;
    va_start(args, message);

    vsnprintf(buffer, sizeof(buffer), message, args);
    argv[4] = buffer;
    va_end(args);

    switch (fork()) {
    case 0:
        execvp(argv[0], argv);
        fprintf(stderr, "Error running %s\n", argv[0]);
        exit(EXIT_FAILURE);
    case -1:
        fprintf(stderr, "Error forking: %s\n", strerror(errno));
        break;
    default:
        break;
    }

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
             t && !ISVISIBLE(t);
             t = t->stack_next);
        client->monitor->selected_client = t;
    }
    return;
}

Monitor *
direction_to_monitor(int direction) {
    Monitor *monitor = NULL;

    if (direction > 0) {
        if (!(monitor = current_monitor->next))
            monitor = monitors;
    } else if (current_monitor == monitors) {
        for (monitor = monitors;
             monitor->next;
             monitor = monitor->next);
    } else {
        for (monitor = monitors;
             monitor->next != current_monitor;
             monitor = monitor->next);
    }
    return monitor;
}

void
monitor_draw_bar(Monitor *monitor) {
    int x;
    int w;
    int text_pixels = 0;
    int urgent = 0;
    char tags_display[TAG_DISPLAY_SIZE];
    char *masterclientontag[LENGTH(tags)];
    Client *icontagclient[LENGTH(tags)] = {0};

    if (!monitor->show_top_bar)
        return;

    /* draw status first so it can be overdrawn by tags later */
    if (monitor == current_monitor) { /* status is only drawn on selected monitor */
        char *text;
        char *s;
        char temp;
        drw_setscheme(drw, scheme[SchemeNormal]);

        x = 0;
        for (text = s = &top_status[0]; *s; s += 1) {
            if ((uchar)(*s) < ' ') {
                temp = *s;
                *s = '\0';

                text_pixels = (int)(TEXT_PIXELS(text) - lrpad);
                drw_text(drw, monitor->win_w - status_text_pixels + x, 0,
                         (uint)text_pixels, bar_height, 0, text, 0);
                x += text_pixels;

                *s = temp;
                text = s + 1;
            }
        }
        text_pixels = (int)(TEXT_PIXELS(text) - lrpad + 2);
        drw_text(drw,
                 monitor->win_w - status_text_pixels + x, 0,
                 (uint)text_pixels, bar_height, 0, text, 0);
        text_pixels = status_text_pixels;
    }

    for (int i = 0; i < LENGTH(tags); i += 1) {
        masterclientontag[i] = NULL;
        icontagclient[i] = NULL;
    }

    for (Client *client = monitor->clients; client; client = client->next) {
        if (client->is_urgent)
            urgent |= client->tags;

        for (int i = 0; i < LENGTH(tags); i += 1) {
            if (client->icon && client->tags & (1 << i))
                icontagclient[i] = client;
            if (!masterclientontag[i] && client->tags & (1<<i)) {
                XClassHint class_hint = { NULL, NULL };
                XGetClassHint(display, client->window, &class_hint);
                masterclientontag[i] = class_hint.res_class;
            }
        }
    }

    x = 0;
    for (int i = 0; i < LENGTH(tags); i += 1) {
        Client *client = icontagclient[i];
        uint which_scheme;

        if (masterclientontag[i]) {
            if (client) {
                snprintf(tags_display, sizeof(tags_display), "%s", tags[i]);
            } else {
                ulong n = strcspn(masterclientontag[i], tag_label_delim);
                masterclientontag[i][n] = '\0';
                snprintf(tags_display, sizeof(tags_display),
                         tag_label_format, tags[i], masterclientontag[i]);
            }
        } else {
            snprintf(tags_display, sizeof(tags_display), tag_empty_format, tags[i]);
        }
        tag_width[i] = w = (int)TEXT_PIXELS(tags_display);

        if (monitor->tagset[monitor->selected_tags] & 1 << i)
            which_scheme = SchemeSelected;
        else
            which_scheme = SchemeNormal;
        drw_setscheme(drw, scheme[which_scheme]);

        drw_text(drw, x, 0, (uint)w,
                 bar_height, lrpad / 2, tags_display, (int)urgent & 1 << i);
        x += w;
        if (client) {
            drw_text(drw, x, 0, client->icon_width + lrpad/2,
                     bar_height, 0, " ", urgent & 1 << i);
            drw_pic(drw,
                    x, (bar_height - client->icon_height) / 2,
                    client->icon_width, client->icon_height,
                    client->icon);
            x += client->icon_width + lrpad/2;
            tag_width[i] += client->icon_width + lrpad/2;
        }
    }
    w = (int)TEXT_PIXELS(monitor->layout_symbol);
    drw_setscheme(drw, scheme[SchemeNormal]);
    x = drw_text(drw, x, 0, (uint)w, bar_height, lrpad / 2, monitor->layout_symbol, 0);

    if ((w = monitor->win_w - text_pixels - x) > (int)bar_height) {
        int boxs = drw->fonts->h / 9;
        int boxw = drw->fonts->h / 6 + 2;

        if (monitor->selected_client) {
            int scheme_index;
            if (monitor == current_monitor)
                scheme_index = SchemeSelected;
            else
                scheme_index = SchemeNormal;
            drw_setscheme(drw, scheme[scheme_index]);

            drw_text(drw,
                     x, 0, (uint)w, bar_height,
                     lrpad / 2, monitor->selected_client->name, 0);
            if (monitor->selected_client->is_floating)
                drw_rect(drw, x + boxs, boxs,
                         (uint)boxw, (uint)boxw,
                         monitor->selected_client->is_fixed, 0);
        } else {
            drw_setscheme(drw, scheme[SchemeNormal]);
            drw_rect(drw, x, 0, (uint)w, bar_height, 1, 1);
        }
    }
    drw_map(drw, monitor->top_bar_window, 0, 0, (uint)monitor->win_w, bar_height);

    drw_setscheme(drw, scheme[SchemeNormal]);
    drw_rect(drw, 0, 0, (uint)monitor->win_w, bar_height, 1, 1);
    if (monitor == current_monitor) {
        char *text;
        char *s;

        x = 0;
        for (text = s = &bottom_status[0]; *s; s += 1) {
            char temp;

            if ((uchar)(*s) < ' ') {
                temp = *s;
                *s = '\0';

                text_pixels = (int)(TEXT_PIXELS(text) - lrpad);
                drw_text(drw,
                         monitor->win_w - bottom_status_pixels + x, 0,
                         (uint)text_pixels, bar_height, 0, text, 0);
                x += text_pixels;

                *s = temp;
                text = s + 1;
            }
        }
        text_pixels = (int)(TEXT_PIXELS(text) - lrpad + 2);
        drw_text(drw,
                 monitor->win_w - bottom_status_pixels + x, 0,
                 (uint)text_pixels, bar_height, 0, text, 0);
    }
    drw_map(drw, monitor->bottom_bar_window,
            0, 0, (uint)monitor->win_w, bar_height);
    return;
}

void
draw_bars(void) {
    for (Monitor *monitor = monitors; monitor; monitor = monitor->next)
        monitor_draw_bar(monitor);
    return;
}

void
client_focus(Client *client) {
    Client *selected = current_monitor->selected_client;
    if (!client || !ISVISIBLE(client)) {
        for (client = current_monitor->stack;
             client && !ISVISIBLE(client);
             client = client->stack_next);
    }

    if (selected && selected != client)
        client_unfocus(selected, 0);

    if (client) {
        if (client->monitor != current_monitor)
            current_monitor = client->monitor;
        if (client->is_urgent)
            client_set_urgent(client, 0);
        client_detach_stack(client);
        client_attach_stack(client);
        client_grab_buttons(client, 1);
        XSetWindowBorder(display, client->window,
                         scheme[SchemeSelected][ColBorder].pixel);
        client_set_focus(client);
    } else {
        XSetInputFocus(display, current_monitor->top_bar_window,
                       RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display, root, netatom[NetActiveWindow]);
    }

    current_monitor->selected_client = client;
    draw_bars();
    return;
}

void
monitor_layout_columns(Monitor *monitor) {
    int i = 0;
    int n = 0;
    int x = 0;
    int y = 0;
    int mon_w;

    for (Client *client_aux = client_next_tiled(monitor->clients);
                 client_aux;
                 client_aux = client_next_tiled(client_aux->next)) {
        n += 1;
    }
    if (n == 0)
        return;

    if (n > monitor->number_masters) {
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
            w = (mon_w - x) / (MIN(n, monitor->number_masters) - i);
            client_resize(client,
                   x + monitor->win_x, monitor->win_y,
                   w - (2*client->border_pixels),
                   monitor->win_h - (2*client->border_pixels), 0);
            x += WIDTH(client);
        } else {
            h = (monitor->win_h - y) / (n - i);
            client_resize(client,
                   x + monitor->win_x, monitor->win_y + y,
                   monitor->win_w - x - (2*client->border_pixels),
                   h - (2*client->border_pixels), 0);
            y += HEIGHT(client);
        }
        i += 1;
    }
    return;
}

void
monitor_layout_gapless_grid(Monitor *monitor) {
    int nclients = 0;
    int ncolumns = 0;
    int nrows;
    int col_i;
    int row_i;
    int column_width;
    int i = 0;

    for (Client *client = client_next_tiled(monitor->clients);
                 client;
                 client = client_next_tiled(client->next)) {
        nclients += 1;
    }
    if (nclients == 0)
        return;

    /* grid dimensions */
    while (ncolumns*ncolumns < nclients) {
        if (ncolumns > (nclients / 2))
            break;
        ncolumns += 1;
    }

    if (nclients == 5) {
        /* set layout against the general calculation: not 1:2:2, but 2:3 */
        ncolumns = 2;
    }
    nrows = nclients/ncolumns;

    if (ncolumns == 0)
        column_width = monitor->win_w;
    else
        column_width = monitor->win_w / ncolumns;

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

        if ((i/nrows + 1) > (ncolumns - nclients % ncolumns))
            nrows = nclients/ncolumns + 1;

        client_height = monitor->win_h / nrows;

        new_x = monitor->win_x + col_i*column_width;
        new_y = monitor->win_y + row_i*client_height;
        new_w = column_width - 2*client->border_pixels;
        new_h = client_height - 2*client->border_pixels;
        client_resize(client, new_x, new_y, new_w, new_h, False);

        row_i += 1;
        if (row_i >= nrows) {
            row_i = 0;
            col_i += 1;
        }
        i += 1;
    }
    return;
}

void
monitor_layout_monocle(Monitor *monitor) {
    uint n = 0;

    for (Client *client = monitor->clients; client; client = client->next) {
        if (ISVISIBLE(client))
            n += 1;
    }

    if (n > 0) /* override layout symbol */
        snprintf(monitor->layout_symbol, sizeof(monitor->layout_symbol), "[%d]", n);

    for (Client *client = client_next_tiled(monitor->clients);
                 client;
                 client = client_next_tiled(client->next)) {
        int new_x = monitor->win_x;
        int new_y = monitor->win_y;
        int new_w = monitor->win_w - 2*client->border_pixels;
        int new_h = monitor->win_h - 2*client->border_pixels;
        client_resize(client, new_x, new_y, new_w, new_h, 0);
    }
    return;
}

void
monitor_layout_tile(Monitor *m) {
    int n = 0;
    int i = 0;
    int mon_w = 0;
    int mon_y = 0;
    int tile_y = 0;

    for (Client *client_aux = client_next_tiled(m->clients);
                 client_aux;
                 client_aux = client_next_tiled(client_aux->next)) {
        n += 1;
    }
    if (n == 0)
        return;

    if (n > m->number_masters) {
        if (m->number_masters != 0)
            mon_w = (int)((float)m->win_w*m->master_fact);
        else
            mon_w = 0;
    } else {
        mon_w = m->win_w;
    }

    for (Client *client = client_next_tiled(m->clients);
                 client;
                 client = client_next_tiled(client->next)) {
        int h;
        if (i < m->number_masters) {
            h = (m->win_h - mon_y) / (MIN(n, m->number_masters) - i);
            client_resize(client,
                   m->win_x, m->win_y + mon_y,
                   mon_w - (2*client->border_pixels),
                   h - (2*client->border_pixels), 0);
            if (mon_y + HEIGHT(client) < m->win_h)
                mon_y += HEIGHT(client);
        } else {
            h = (m->win_h - tile_y) / (n - i);
            client_resize(client,
                   m->win_x + mon_w, m->win_y + tile_y,
                   m->win_w - mon_w - (2*client->border_pixels),
                   h - (2*client->border_pixels), 0);
            if (tile_y + HEIGHT(client) < m->win_h)
                tile_y += HEIGHT(client);
        }
        i += 1;
    }
    return;
}

Atom
client_get_atom_property(Client *client, Atom property) {
    int actual_format_return;
    ulong nitems_return;
    Atom actual_type_return;
    Atom atom = None;
    Atom *prop_return = NULL;
    int sucess;

    sucess = XGetWindowProperty(display, client->window, property,
                                0L, sizeof(atom), False, XA_ATOM,
                                &actual_type_return, &actual_format_return,
                                &nitems_return, &nitems_return,
                                (uchar **)&prop_return);
    if (sucess == Success && prop_return) {
        atom = *prop_return;
        XFree(prop_return);
    }
    return atom;
}

pid_t
get_status_bar_pid(void) {
    char buffer[32];
    char *string = buffer;
    char *client;
    long pid_long;
    FILE *fp;

    if (statuspid > 0) {
        snprintf(buffer, sizeof(buffer), "/proc/%u/cmdline", statuspid);
        if ((fp = fopen(buffer, "r"))) {
            fgets(buffer, sizeof(buffer), fp);
            while ((client = strchr(string, '/')))
                string = client + 1;
            fclose(fp);
            if (!strcmp(string, STATUSBAR))
                return statuspid;
        }
    }
    if (!(fp = popen("pidof -s "STATUSBAR, "r")))
        return -1;
    fgets(buffer, sizeof(buffer), fp);
    pclose(fp);
    pid_long = strtol(buffer, NULL, 10);
    return (pid_t) pid_long;
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
    int sucess;

    sucess = XGetWindowProperty(display, window, wmatom[WMState],
                                0L, 2L, False, wmatom[WMState],
                                &actual_type_return, &actual_format_return,
                                &nitems_return, &bytes_after_return,
                                (uchar **)&prop_return);
    if (sucess != Success)
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
    int sucess;

    if (!text || size == 0)
        return 0;
    text[0] = '\0';

    sucess = XGetTextProperty(display, window, &text_property, atom);
    if (!sucess || !text_property.nitems)
        return 0;

    if (text_property.encoding == XA_STRING) {
        strncpy(text, (char *)text_property.value, size - 1);
        text[size - 1] = '\0';
        XFree(text_property.value);
        return 1;
    }

    sucess = XmbTextPropertyToTextList(display, &text_property,
                                       &list_return, &count_return);
    if (sucess >= Success && count_return > 0 && *list_return) {
        strncpy(text, *list_return, size - 1);
        XFreeStringList(list_return);
    }

    text[size - 1] = '\0';
    XFree(text_property.value);
    return 1;
}

void
client_grab_buttons(Client *client, int focused) {
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
grab_keys(void) {
    uint modifiers[] = { 0, LockMask, numlock_mask, numlock_mask|LockMask };
    int start;
    int end;
    int skip;
    KeySym *key_sym;

    update_numlock_mask();

    XUngrabKey(display, AnyKey, AnyModifier, root);
    XDisplayKeycodes(display, &start, &end);

    key_sym = XGetKeyboardMapping(display,
                                  (uchar) start, (uchar) end - start + 1,
                                   &skip);
    if (!key_sym)
        return;

    for (int k = start; k <= end; k += 1) {
        for (int i = 0; i < LENGTH(keys); i += 1) {
            /* skip modifier codes, we do that ourselves */
            if (keys[i].keysym == key_sym[(k - start)*skip]) {
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
handler_button_press(XEvent *event) {
    uint click;
    Arg arg = {0};
    Client *client;
    Monitor *monitor;
    XButtonPressedEvent *button_event = &event->xbutton;
    int button_x = button_event->x;

    click = ClickRootWin;
    /* focus monitor if necessary */
    monitor = window_to_monitor(button_event->window);
    if (monitor && monitor != current_monitor) {
        client_unfocus(current_monitor->selected_client, 1);
        current_monitor = monitor;
        client_focus(NULL);
    }
    if (button_event->window == current_monitor->top_bar_window) {
        uint i = 0;
        uint x = 0;

        do {
            x += (uint)tag_width[i];
        } while ((uint)button_x >= x && ++i < LENGTH(tags));
        if (i < LENGTH(tags)) {
            click = ClickBarTags;
            arg.ui = 1 << i;
        } else if ((uint)button_x < x + TEXT_PIXELS(current_monitor->layout_symbol)) {
            click = ClickBarLayoutSymbol;
        } else if (button_x > current_monitor->win_w - status_text_pixels) {
            char *s;

            x = (uint)(current_monitor->win_w - status_text_pixels);
            click = ClickBarStatus;
            status_signal = 0;

            for (char *text = s = top_status; *s && (int)x <= button_x; s += 1) {
                if ((uchar)(*s) < ' ') {
                    char byte = *s;
                    *s = '\0';

                    x += TEXT_PIXELS(text) - lrpad;

                    *s = byte;
                    text = s + 1;
                    if ((int)x >= button_x)
                        break;
                    status_signal = byte;
                }
            }
        } else {
            click = ClickBarTitle;
        }
    } else if (button_event->window == current_monitor->bottom_bar_window) {
        int x = current_monitor->win_w - bottom_status_pixels;
        char *s = &bottom_status[0];

        click = ClickBottomBar;
        status_signal = 0;

        for (char *text = s; *s && x <= button_x; s += 1) {
            if ((uchar)(*s) < ' ') {
                char byte = *s;
                *s = '\0';

                x += TEXT_PIXELS(text) - lrpad;

                *s = byte;
                text = s + 1;
                if (x >= button_x)
                    break;
                status_signal = byte;
            }
        }
    } else if ((client = window_to_client(button_event->window))) {
        client_focus(client);
        monitor_restack(current_monitor);
        XAllowEvents(display, ReplayPointer, CurrentTime);
        click = ClickClientWin;
    }

    for (uint i = 0; i < LENGTH(buttons); i += 1) {
        if (click == buttons[i].click
            && buttons[i].func
            && (buttons[i].button == button_event->button)
            && CLEANMASK(buttons[i].mask) == CLEANMASK(button_event->state)) {
            const Arg *argument;

            if (click == ClickBarTags && buttons[i].arg.i == 0)
                argument = &arg;
            else
                argument = &buttons[i].arg;
            buttons[i].func(argument);
        }
    }
    return;
}

void
handler_client_message(XEvent *event) {
    XClientMessageEvent *client_message_event = &event->xclient;
    Client *client = window_to_client(client_message_event->window);

    if (!client)
        return;

    if (client_message_event->message_type == netatom[NetWMState]) {
        ulong *data = (ulong *) client_message_event->data.l;

        if (data[1] == netatom[NetWMFullscreen]
            || data[2] == netatom[NetWMFullscreen]) {
            bool NET_WM_STATE_ADD = data[0] == 1;
            bool NET_WM_STATE_TOGGLE = data[0] == 2;
            bool fullscreen = NET_WM_STATE_ADD
                              || (NET_WM_STATE_TOGGLE
                                  && (!client->is_fullscreen
                                      || client->is_fake_fullscreen));
            client_set_fullscreen(client, fullscreen);
        }
    } else if (client_message_event->message_type == netatom[NetActiveWindow]) {
        if (client != current_monitor->selected_client && !client->is_urgent)
            client_set_urgent(client, 1);
    }
    return;
}

void
handler_configure_request(XEvent *event) {
    Client *client;
    Monitor *monitor;
    XConfigureRequestEvent *conf_request_event = &event->xconfigurerequest;
    XWindowChanges window_changes;

    if ((client = window_to_client(conf_request_event->window))) {
        bool monitor_floating;
        if (conf_request_event->value_mask & CWBorderWidth) {
            client->border_pixels = conf_request_event->border_width;
            XSync(display, False);
            return;
        }

        monitor_floating = !current_monitor->layout[current_monitor->lay_i]->function;
        if (client->is_floating || monitor_floating) {
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
                if ((client->x + client->w) > (monitor->mon_x + monitor->mon_w))
                    client->x = monitor->mon_x + (monitor->mon_w / 2 - WIDTH(client) / 2);
                if ((client->y + client->h) > (monitor->mon_y + monitor->mon_h))
                    client->y = monitor->mon_y + (monitor->mon_h / 2 - HEIGHT(client) / 2);
            }

            mask_xy = conf_request_event->value_mask & (CWX|CWY);
            mask_hw = conf_request_event->value_mask & (CWWidth|CWHeight);
            if (mask_xy && !mask_hw)
                client_configure(client);

            if (ISVISIBLE(client)) {
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
        update_bars();
        for (Monitor *m = monitors; m; m = m->next) {
            for (Client *client = m->clients; client; client = client->next) {
                if (client->is_fullscreen && !client->is_fake_fullscreen) {
                    client_resize_apply(client,
                                        m->mon_x, m->mon_y,
                                        m->mon_w, m->mon_h);
                }
            }
            XMoveResizeWindow(display, m->top_bar_window,
                              m->win_x, m->top_bar_y, (uint)m->win_w, bar_height);
            XMoveResizeWindow(display, m->bottom_bar_window,
                              m->win_x, m->bottom_bar_y, (uint)m->win_w, bar_height);
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

    if (monitor != current_monitor) {
        client_unfocus(current_monitor->selected_client, 1);
        current_monitor = monitor;
    } else if (client == current_monitor->selected_client) {
        return;
    } else if (!client) {
        return;
    }
    client_focus(client);
    return;
}

/* there are some broken focus acquiring clients needing extra handling */
void
handler_focus_in(XEvent *event) {
    XFocusChangeEvent *focus_change_event = &event->xfocus;

    if (!(current_monitor->selected_client))
        return;

    if (focus_change_event->window != current_monitor->selected_client->window)
        client_set_focus(current_monitor->selected_client);
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
            && keys[i].func) {
            keys[i].func(&(keys[i].arg));
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
    int sucess;

    sucess = XGetWindowAttributes(display,
                                  map_request_event->window,
                                  &window_attributes);
    if (!sucess)
        return;

    if (window_attributes.override_redirect)
        return;

    if (!window_to_client(map_request_event->window))
        manage(map_request_event->window, &window_attributes);
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
    if (m != monitor_save && monitor_save) {
        client_unfocus(current_monitor->selected_client, 1);
        current_monitor = m;
        client_focus(NULL);
    }
    monitor_save = m;
    return;
}

void
handler_property_notify(XEvent *event) {
    Client *client;
    Window trans;
    XPropertyEvent *property_event = &event->xproperty;

    if ((property_event->window == root) && (property_event->atom == XA_WM_NAME)) {
        update_status();
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
        || property_event->atom == netatom[NetWMName]) {
        client_update_title(client);
        if (client == client->monitor->selected_client)
            monitor_draw_bar(client->monitor);
    } else if (property_event->atom == netatom[NetWMIcon]) {
        client_update_icon(client);
        if (client == client->monitor->selected_client)
            monitor_draw_bar(client->monitor);
    }
    if (property_event->atom == netatom[NetWMWindowType])
        client_update_window_type(client);
    return;
}

void
handler_unmap_notify(XEvent *event) {
    Client *client;
    XUnmapEvent *unmap_event = &event->xunmap;

    if ((client = window_to_client(unmap_event->window))) {
        if (unmap_event->send_event)
            client_set_client_state(client, WithdrawnState);
        else
            client_unmanage(client, 0);
    }
    return;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
handler_xerror(Display *d, XErrorEvent *ee) {
    (void) d;
    if (ee->error_code == BadWindow
    || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
    || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
    || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
    || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
    || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
    || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    return xerrorxlib(display, ee); /* may call exit */
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
    die("dwm: another window manager is already running");
    return -1;
}

#ifdef XINERAMA
static int
is_unique_geometry(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info) {
    while (n--) {
        if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
        && unique[n].width == info->width && unique[n].height == info->height)
            return 0;
    }
    return 1;
}
#endif /* XINERAMA */

void
manage(Window window, XWindowAttributes *window_attributes) {
    Client *client;
    Client *t = NULL;
    Window trans = None;
    XWindowChanges window_changes;

    client = ecalloc(1, sizeof(*client));
    client->window = window;

    client->x = client->old_x = window_attributes->x;
    client->y = client->old_y = window_attributes->y;
    client->w = client->old_w = window_attributes->width;
    client->h = client->old_h = window_attributes->height;
    client->old_border_pixels = window_attributes->border_width;

    client_update_icon(client);
    client_update_title(client);
    if (XGetTransientForHint(display, window, &trans) && (t = window_to_client(trans))) {
        client->monitor = t->monitor;
        client->tags = t->tags;
    } else {
        client->monitor = current_monitor;
        client_apply_rules(client);
    }

    if (client->x + WIDTH(client) > client->monitor->win_x + client->monitor->win_w)
        client->x = client->monitor->win_x + client->monitor->win_w - WIDTH(client);
    if (client->y + HEIGHT(client) > client->monitor->win_y + client->monitor->win_h)
        client->y = client->monitor->win_y + client->monitor->win_h - HEIGHT(client);
    client->x = MAX(client->x, client->monitor->win_x);
    client->y = MAX(client->y, client->monitor->win_y);
    client->border_pixels = border_pixels;

    window_changes.border_width = client->border_pixels;
    XConfigureWindow(display, window, CWBorderWidth, &window_changes);
    XSetWindowBorder(display, window, scheme[SchemeNormal][ColBorder].pixel);
    client_configure(client); /* propagates border_pixels, if size doesn't change */
    client_update_window_type(client);
    client_update_size_hints(client);
    client_update_wm_hints(client);
    {
        int actual_format_return;
        ulong *prop_return;
        ulong nitems_return;
        ulong bytes_after_return;
        Atom actual_type_return;
        int sucess;

        sucess = XGetWindowProperty(display, client->window, netatom[NetClientInfo],
                                    0L, 2L, False, XA_CARDINAL,
                                    &actual_type_return, &actual_format_return,
                                    &nitems_return, &bytes_after_return,
                                    (uchar **)&prop_return);
        if (sucess == Success && nitems_return == 2) {
            client->tags = (uint)*prop_return;
            for (Monitor *m = monitors; m; m = m->next) {
                if (m->num == (int)*(prop_return+1)) {
                    client->monitor = m;
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
    client->x = client->monitor->mon_x + (client->monitor->mon_w - WIDTH(client)) / 2;
    client->y = client->monitor->mon_y + (client->monitor->mon_h - HEIGHT(client)) / 2;

    XSelectInput(display, window,
                 EnterWindowMask
                 |FocusChangeMask
                 |PropertyChangeMask
                 |StructureNotifyMask);

    client_grab_buttons(client, 0);

    if (!client->is_floating)
        client->is_floating = client->old_state = trans != None || client->is_fixed;
    if (client->is_floating)
        XRaiseWindow(display, client->window);

    client_attach(client);
    client_attach_stack(client);

    XChangeProperty(display, root, netatom[NetClientList], XA_WINDOW,
                    32, PropModeAppend, (uchar *)&(client->window), 1);

    /* some windows require this */
    XMoveResizeWindow(display, client->window,
                      client->x + 2*screen_width, client->y,
                      (uint)client->w, (uint)client->h);
    client_set_client_state(client, NormalState);
    if (client->monitor == current_monitor)
        client_unfocus(current_monitor->selected_client, 0);
    client->monitor->selected_client = client;
    monitor_arrange(client->monitor);
    XMapWindow(display, client->window);
    client_focus(NULL);
    return;
}

Client *
client_next_tiled(Client *client) {
    while (true) {
        if (!client)
            break;
        if (!client->is_floating && ISVISIBLE(client))
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

Monitor *
rectangle_to_monitor(int x, int y, int w, int h) {
    Monitor *r = current_monitor;
    int a;
    int max_area = 0;

    for (Monitor *m = monitors; m; m = m->next) {
        int min_x = MIN(x + w, m->win_x + m->win_w);
        int min_y = MIN(y + h, m->win_y + m->win_h);

        int ax = MAX(0, min_x - MAX(x, m->win_x));
        int ay = MAX(0, min_y - MAX(y, m->win_y));

        if ((a = ax*ay) > max_area) {
            max_area = a;
            r = m;
        }
    }
    return r;
}

void
client_resize(Client *client, int x, int y, int w, int h, int interact) {
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

    for (Client *client_aux = client_next_tiled(current_monitor->clients);
                 client_aux;
                 client_aux = client_next_tiled(client_aux->next)) {
        n += 1;
    }

    if (!(client->is_floating)) {
        const Layout *layout = current_monitor->layout[current_monitor->lay_i];
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
            if (!client->is_floating && ISVISIBLE(client)) {
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
scan_windows(void) {
    Window root_return;
    Window parent_return;
    Window *children_return = NULL;
    uint nchildren_return;
    XWindowAttributes window_attributes;
    int sucess;

    sucess = XQueryTree(display, root,
                        &root_return, &parent_return,
                        &children_return, &nchildren_return);
    if (!sucess)
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
            manage(child, &window_attributes);
        }
    }
    for (uint i = 0; i < nchildren_return; i += 1) { /* now the transients */
        Window child = children_return[i];

        if (!XGetWindowAttributes(display, child, &window_attributes))
            continue;
        if (!XGetTransientForHint(display, child, &root_return))
            continue;

        if (window_attributes.map_state == IsViewable
            || get_window_state(child) == IconicState) {
            manage(child, &window_attributes);
        }
    }

    if (children_return)
        XFree(children_return);
    return;
}

void
client_send_monitor(Client *client, Monitor *m) {
    if (client->monitor == m)
        return;
    client_unfocus(client, 1);
    client_detach(client);
    client_detach_stack(client);
    client->monitor = m;
    client->tags = m->tagset[m->selected_tags]; /* assign tags of target monitor */
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

    XChangeProperty(display, client->window, wmatom[WMState], wmatom[WMState], 32,
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
        event.xclient.message_type = wmatom[WMProtocols];
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
        XSetInputFocus(display, client->window, RevertToPointerRoot, CurrentTime);
        XChangeProperty(display, root, netatom[NetActiveWindow],
            XA_WINDOW, 32, PropModeReplace,
            (uchar *)&(client->window), 1);
    }
    client_send_event(client, wmatom[WMTakeFocus]);
    return;
}

void
client_set_fullscreen(Client *client, int fullscreen) {
    if (fullscreen && !client->is_fullscreen) {
        XChangeProperty(display, client->window, netatom[NetWMState], XA_ATOM, 32,
            PropModeReplace, (uchar*)&netatom[NetWMFullscreen], 1);
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
        XChangeProperty(display, client->window, netatom[NetWMState], XA_ATOM, 32,
            PropModeReplace, (uchar*)0, 0);
        client->is_fullscreen = 0;
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
setup_once(void) {
    XRenderPictFormat *render_format;
    XVisualInfo *visual_infos;
    XVisualInfo vinfo_template;
    int nitems_return;
    long vinfo_mask = VisualScreenMask | VisualDepthMask | VisualClassMask;
    XSetWindowAttributes window_attributes;
    Atom UTF8STRING;
    struct sigaction sig_action;

    /* do not transform children into zombies when they terminate */
    sigemptyset(&sig_action.sa_mask);
    sig_action.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
    sig_action.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sig_action, NULL);

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
        render_format = XRenderFindVisualFormat(display, visual_infos[i].visual);
        if (render_format->type == PictTypeDirect
            && render_format->direct.alphaMask) {
            visual = visual_infos[i].visual;
            depth = visual_infos[i].depth;
            cmap = XCreateColormap(display, root, visual, AllocNone);
            break;
        }
    }

    XFree(visual_infos);

    if (!visual) {
        visual = DefaultVisual(display, screen);
        depth = DefaultDepth(display, screen);
        cmap = DefaultColormap(display, screen);
    }

    drw = drw_create(display, screen, root,
                     (uint)screen_width, (uint)screen_height,
                     visual, (uint)depth, cmap);
    if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
        die("no fonts could be loaded.");
    lrpad = drw->fonts->h / 2;
    bar_height = drw->fonts->h + 2;
    update_geometry();

    /* init atoms */
    UTF8STRING = X_INTERN_ATOM("UTF8_STRING");
    wmatom[WMProtocols] = X_INTERN_ATOM("WM_PROTOCOLS");
    wmatom[WMDelete] = X_INTERN_ATOM("WM_DELETE_WINDOW");
    wmatom[WMState] = X_INTERN_ATOM("WM_STATE");
    wmatom[WMTakeFocus] = X_INTERN_ATOM("WM_TAKE_FOCUS");
    netatom[NetActiveWindow] = X_INTERN_ATOM("_NET_ACTIVE_WINDOW");
    netatom[NetSupported] = X_INTERN_ATOM("_NET_SUPPORTED");
    netatom[NetWMName] = X_INTERN_ATOM("_NET_WM_NAME");
    netatom[NetWMIcon] = X_INTERN_ATOM("_NET_WM_ICON");
    netatom[NetWMState] = X_INTERN_ATOM("_NET_WM_STATE");
    netatom[NetWMCheck] = X_INTERN_ATOM("_NET_SUPPORTING_WM_CHECK");
    netatom[NetWMFullscreen] = X_INTERN_ATOM("_NET_WM_STATE_FULLSCREEN");
    netatom[NetWMWindowType] = X_INTERN_ATOM("_NET_WM_WINDOW_TYPE");
    netatom[NetWMWindowTypeDialog] = X_INTERN_ATOM("_NET_WM_WINDOW_TYPE_DIALOG");
    netatom[NetClientList] = X_INTERN_ATOM("_NET_CLIENT_LIST");
    netatom[NetClientInfo] = X_INTERN_ATOM("_NET_CLIENT_INFO");

    /* init cursors */
    cursor[CursorNormal] = drw_cur_create(drw, XC_left_ptr);
    cursor[CursorResize] = drw_cur_create(drw, XC_sizing);
    cursor[CursorMove] = drw_cur_create(drw, XC_fleur);

    /* init appearance */
    scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
    for (int i = 0; i < LENGTH(colors); i += 1)
        scheme[i] = drw_scm_create(drw, colors[i], alphas[i], 3);

    /* init bars */
    update_bars();
    update_status();

    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(display, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
        PropModeReplace, (uchar *)&wmcheckwin, 1);
    XChangeProperty(display, wmcheckwin, netatom[NetWMName], UTF8STRING, 8,
        PropModeReplace, (uchar *)"dwm", 3);
    XChangeProperty(display, root, netatom[NetWMCheck], XA_WINDOW, 32,
        PropModeReplace, (uchar *)&wmcheckwin, 1);

    /* EWMH support per view */
    XChangeProperty(display, root, netatom[NetSupported], XA_ATOM, 32,
        PropModeReplace, (uchar *)netatom, NetLast);
    XDeleteProperty(display, root, netatom[NetClientList]);
    XDeleteProperty(display, root, netatom[NetClientInfo]);

    /* select events */
    window_attributes.cursor = cursor[CursorNormal]->cursor;
    window_attributes.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
        |ButtonPressMask|PointerMotionMask|EnterWindowMask
        |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
    XChangeWindowAttributes(display, root, CWEventMask|CWCursor, &window_attributes);
    XSelectInput(display, root, window_attributes.event_mask);
    grab_keys();
    client_focus(NULL);
    return;
}

void
client_set_urgent(Client *client, int urgent) {
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
    Monitor *monitor;
    if (!client)
        return;

    monitor = client->monitor;

    if (ISVISIBLE(client)) {
        bool monitor_floating;

        if ((client->tags) && client->is_floating) {
            client->x = monitor->win_x;
            client->x += (monitor->win_w / 2 - WIDTH(client) / 2);

            client->y = monitor->win_y;
            client->y += (monitor->win_h / 2 - HEIGHT(client) / 2);
        }
        /* show clients top down */
        XMoveWindow(display, client->window, client->x, client->y);

        monitor_floating = !monitor->layout[monitor->lay_i]->function;
        if ((monitor_floating || client->is_floating)
            && (!client->is_fullscreen || client->is_fake_fullscreen)) {
            client_resize(client, client->x, client->y, client->w, client->h, 0);
        }
        client_show_hide(client->stack_next);
    } else {
        /* hide clients bottom up */
        client_show_hide(client->stack_next);
        XMoveWindow(display, client->window, -2*WIDTH(client), client->y);
    }
    return;
}

void
client_set_client_tag_prop(Client *client) {
    long data[] = { (long) client->tags, (long) client->monitor->num };
    XChangeProperty(display, client->window, netatom[NetClientInfo],
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
client_unfocus(Client *client, int client_set_focus) {
    if (!client)
        return;
    client_grab_buttons(client, 0);
    XSetWindowBorder(display, client->window, scheme[SchemeNormal][ColBorder].pixel);
    if (client_set_focus) {
        XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display, root, netatom[NetActiveWindow]);
    }
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

        /* restore border */
        XConfigureWindow(display, client->window, CWBorderWidth, &window_changes);
        XUngrabButton(display, AnyButton, AnyModifier, client->window);
        client_set_client_state(client, WithdrawnState);

        XSync(display, False);
        XSetErrorHandler(handler_xerror);
        XUngrabServer(display);
    }

    free(client);
    client_focus(NULL);
    update_client_list();
    monitor_arrange(monitor);
    return;
}

void
update_bars(void) {
    XSetWindowAttributes window_attributes = {
        .override_redirect = True,
        .background_pixel = 0,
        .border_pixel = 0,
        .colormap = cmap,
        .event_mask = ButtonPressMask|ExposureMask
    };
    XClassHint class_hint = {"dwm", "dwm"};
    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        Window window;
        ulong value_mask = CWOverrideRedirect
                           | CWBackPixel | CWBorderPixel
                           | CWColormap |CWEventMask;

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
update_client_list(void) {
    XDeleteProperty(display, root, netatom[NetClientList]);

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        for (Client *client = monitor->clients; client; client = client->next) {
            XChangeProperty(display, root, netatom[NetClientList],
                            XA_WINDOW, 32, PropModeAppend,
                            (uchar *)&(client->window), 1);
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
            if (is_unique_geometry(unique, (size_t) j, &screen_info[i]))
                memcpy(&unique[j++], &screen_info[i], sizeof(XineramaScreenInfo));

            i += 1;
        }
        XFree(screen_info);
        number_unique = j;

        /* new monitors if number_unique > number_monitors */
        for (int k = number_monitors; k < number_unique; k += 1) {
            for (monitor = monitors; monitor && monitor->next; monitor = monitor->next);
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

            if (monitor == current_monitor)
                current_monitor = monitors;
            monitor_cleanup_monitor(monitor);
        }
        free(unique);
    } else
#endif /* XINERAMA */
    { /* default monitor setup */
        if (!monitors)
            monitors = create_monitor();
        if (monitors->mon_w != screen_width || monitors->mon_h != screen_height) {
            dirty = true;
            monitors->mon_w = monitors->win_w = screen_width;
            monitors->mon_h = monitors->win_h = screen_height;
            monitor_update_bar_position(monitors);
        }
    }
    if (dirty) {
        current_monitor = monitors;
        current_monitor = window_to_monitor(root);
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
client_update_size_hints(Client *client) {
    long supplied_return;
    bool has_maxes;
    bool mins_match_maxes;
    XSizeHints size_hints;
    int sucess;

    sucess = XGetWMNormalHints(display, client->window,
                               &size_hints, &supplied_return);
    if (!sucess) {
        /* size_hints is uninitialized, ensure that size_hints.flags aren't used */
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
        client->min_a = (float)size_hints.min_aspect.y
                        / (float)size_hints.min_aspect.x;
        client->max_a = (float)size_hints.max_aspect.x
                        / (float)size_hints.max_aspect.y;
    } else {
        client->max_a = client->min_a = 0.0;
    }

    has_maxes = client->max_w && client->max_h;
    mins_match_maxes = client->max_w == client->min_w
                       && client->max_h == client->min_h;
    client->is_fixed = has_maxes && mins_match_maxes;

    client->hintsvalid = true;
    return;
}

int
status_count_pixels(char *text) {
    char *s;
    char *text2;
    int pixels = 0;
    for (text2 = s = text; *s; s += 1) {
        char byte;
        if ((uchar)(*s) < ' ') {
            byte = *s;
            *s = '\0';
            pixels += TEXT_PIXELS(text2) - lrpad;
            *s = byte;
            text2 = s + 1;
        }
    }
    pixels += TEXT_PIXELS(text2) - lrpad + 2;
    return pixels;
}

void
update_status(void) {
    char text[768];
    char *separator;

    if (!get_text_property(root, XA_WM_NAME, text, sizeof(text))) {
        strcpy(top_status, "dwm-"VERSION);
        status_text_pixels = (int)(TEXT_PIXELS(top_status) - lrpad + 2);
        bottom_status[0] = '\0';
        monitor_draw_bar(current_monitor);
        return;
    }

    separator = strchr(text, status_separator);
    if (separator) {
        *separator = '\0';
        separator += 1;
        strncpy(bottom_status, separator, sizeof(bottom_status) - 1);
    } else {
        bottom_status[0] = '\0';
    }

    strncpy(top_status, text, sizeof(top_status) - 1);
    status_text_pixels = status_count_pixels(top_status);
    bottom_status_pixels = status_count_pixels(bottom_status);
    monitor_draw_bar(current_monitor);
    return;
}

void
client_update_title(Client *client) {
    if (!get_text_property(client->window, netatom[NetWMName],
                           client->name, sizeof(client->name))) {
        get_text_property(client->window, XA_WM_NAME,
                          client->name, sizeof(client->name));
    }
    if (client->name[0] == '\0') /* hack to mark broken clients */
        strcpy(client->name, broken);
    return;
}

void
client_update_icon(Client *client) {
    Window window = client->window;
    uint *picture_width = &client->icon_width;
    uint *picture_height = &client->icon_height;
    int actual_format_return;
    ulong nitems_return;
    ulong bytes_after_return;
    ulong *prop_return = NULL;
    ulong *pixel_find = NULL;
    uint32 *pixel_find32;
    uint32 width_find;
    uint32 height_find;
    uint32 icon_width;
    uint32 icon_height;
    uint32 area_find = 0;
    Atom actual_type_return;
    int sucess;

    client_free_icon(client);
    sucess = XGetWindowProperty(display, window, netatom[NetWMIcon],
                                0L, LONG_MAX, False, AnyPropertyType,
                                &actual_type_return, &actual_format_return,
                                &nitems_return, &bytes_after_return,
                                (uchar **)&prop_return);
    if (sucess != Success)
        return;

    if (nitems_return == 0 || actual_format_return != 32) {
        XFree(prop_return);
        return;
    }

    do {
        ulong *i;
        const ulong *end = prop_return + nitems_return;
        uint32 bstd = UINT32_MAX;
        uint32 d;

        for (i = prop_return; i < (end - 1); i += area_find) {
            uint32 max_dim;
            uint32 w = (uint32)*i++;
            uint32 h = (uint32)*i++;
            if (w >= 16384 || h >= 16384) {
                XFree(prop_return);
                return;
            }
            if ((area_find = w*h) > (end - i))
                break;

            max_dim = w > h ? w : h;
            if (max_dim >= ICONSIZE && (d = max_dim - ICONSIZE) < bstd) {
                bstd = d;
                pixel_find = i;
            }
        }
        if (pixel_find)
            break;
        for (i = prop_return; i < (end - 1); i += area_find) {
            uint32 max_dim;
            uint32 w = (uint32)*i++;
            uint32 h = (uint32)*i++;
            if (w >= 16384 || h >= 16384) {
                XFree(prop_return);
                return;
            }
            if ((area_find = w*h) > (end - i))
                break;

            max_dim = w > h ? w : h;
            if ((d = ICONSIZE - max_dim) < bstd) {
                bstd = d;
                pixel_find = i;
            }
        }
    } while (false);

    if (!pixel_find) {
        XFree(prop_return);
        return;
    }

    width_find = (uint32) pixel_find[-2];
    height_find = (uint32) pixel_find[-1];
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
        uint32 pixel = (uint32) pixel_find[i];
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
client_update_window_type(Client *client) {
    Atom state = client_get_atom_property(client, netatom[NetWMState]);
    Atom window_type = client_get_atom_property(client, netatom[NetWMWindowType]);

    if (state == netatom[NetWMFullscreen])
        client_set_fullscreen(client, 1);
    if (window_type == netatom[NetWMWindowTypeDialog])
        client->is_floating = true;
    return;
}

void
client_update_wm_hints(Client *client) {
    XWMHints *wm_hints;

    if (!(wm_hints = XGetWMHints(display, client->window)))
        return;

    if (client == current_monitor->selected_client && wm_hints->flags & XUrgencyHint) {
        wm_hints->flags &= ~XUrgencyHint;
        XSetWMHints(display, client->window, wm_hints);
    } else {
        client->is_urgent = wm_hints->flags & XUrgencyHint;
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

Client *
window_to_client(Window window) {
    for (Monitor *m = monitors; m; m = m->next) {
        for (Client *client = m->clients; client; client = client->next) {
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

    return current_monitor;
}

int
main(int argc, char *argv[]) {
    if (argc == 2 && !strcmp("-v", argv[1]))
        die("dwm-"VERSION);
    else if (argc != 1)
        die("usage: dwm [-v]");

    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fputs("warning: no locale support\n", stderr);

    if (!(display = XOpenDisplay(NULL)))
        die("dwm: cannot open display");
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
    if (pledge("stdio rpath proc exec", NULL) == -1)
        die("pledge");
#endif /* __OpenBSD__ */

    scan_windows();
    {
        XEvent event;
        XSync(display, False);

        while (dwm_running && !XNextEvent(display, &event)) {
            if (handler[event.type])
                handler[event.type](&event);
        }
    }

    if (dwm_restart) {
        dwm_debug("restarting...");
        execvp(argv[0], argv);
    }

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        while (monitor->stack)
            client_unmanage(monitor->stack, 0);
    }

    XUngrabKey(display, AnyKey, AnyModifier, root);

    while (monitors)
        monitor_cleanup_monitor(monitors);
    for (int i = 0; i < CursorLast; i += 1)
        drw_cur_free(drw, cursor[i]);
    for (int i = 0; i < LENGTH(colors); i += 1)
        free(scheme[i]);
    free(scheme);

    XDestroyWindow(display, wmcheckwin);
    drw_free(drw);

    XSync(display, False);
    XSetInputFocus(display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(display, root, netatom[NetActiveWindow]);
    XCloseDisplay(display);

    exit(EXIT_SUCCESS);
}

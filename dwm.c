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

typedef uint32_t uint32;
typedef unsigned int uint;

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->monitor->tagset[C->monitor->seltags]))
#define LENGTH(X)               (int) (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define NUMTAGS					(LENGTH(tags) + LENGTH(scratchpads))
#define TAGMASK     			((1 << NUMTAGS) - 1)
#define SPTAG(i) 				((1 << LENGTH(tags)) << (i))
#define SPTAGMASK   			(((1 << LENGTH(scratchpads))-1) << LENGTH(tags))
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

#define OPAQUE                  0xffU
#define TAGWIDTH                32

/* enums */
enum { CursorNormal, CursorResize, CursorMove, CursorLast };
enum { SchemeNorm, SchemeInv, SchemeSel, SchemeUrg }; /* color schemes */
enum { NetSupported, NetWMName, NetWMIcon, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType, /* EWMH atoms */
       NetWMWindowTypeDialog, NetClientList, NetClientInfo, NetLast };
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* atoms */
enum { ClickTagBar, ClickLtSymbol, ClickStatusText, ClickWinTitle,
       ClickExtraBar, ClickClientWin, ClickRootWin, ClickLast };

typedef union {
	int i;
	uint ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	uint click;
	uint mask;
	unsigned long button;
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
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	uint tags;
	int isfixed, isfloating, isurgent;
	int neverfocus, oldstate, isfullscreen, isfakefullscreen;
	uint icon_width, icon_height;
	int unused;
	Picture icon;
	Client *next;
	Client *snext;
	Client *allnext;
	Monitor *monitor;
	Window win;
};

typedef struct {
	unsigned long mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

typedef struct Pertag Pertag;
struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int bar_y;               /* bar geometry */
	int extra_bar_y;              /* extra bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	uint seltags;
	uint layout_index;
	uint tagset[2];
	int showbar;
	int topbar;
	int extrabar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	Window extrabarwin;
	const Layout *layout[2];
	Pertag *pertag;
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	uint tags;
	uint switchtotag;
	int isfloating;
	int isfakefullscreen;
	int monitor;
	int unused;
} Rule;

/* function declarations */
static void alttab(const Arg *arg);
static void applyrules(Client *client);
static int applysizehints(Client *client, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *monitor);
static void arrangemon(Monitor *monitor);
static void aspectresize(const Arg *arg);
static void attach(Client *client);
static void attachstack(Client *client);
static void buttonpress(XEvent *e);
static void cleanup(void);
static void cleanupmon(Monitor *monitor);
static void clientmessage(XEvent *e);
static void col(Monitor *);
static void configure(Client *client);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void debug_dwm(char *message, ...);
static void destroynotify(XEvent *e);
static void detach(Client *client);
static void detachstack(Client *client);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *monitor);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *client);
static void focusdir(const Arg *arg);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusnext(const Arg *arg);
static void focusstack(const Arg *arg);
static void focusurgent(const Arg *arg);
static void gaplessgrid(Monitor *monitor);
static Atom getatomprop(Client *client, Atom prop);
static Picture geticonprop(Window w, uint *icon_width, uint *icon_height);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static pid_t getstatusbarpid(void);
static int gettextprop(Window w, Atom atom, char *text, uint size);
static void grabbuttons(Client *client, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *monitor);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *client);
static void pop(Client *client);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *client, int x, int y, int w, int h, int interact);
static void resizeclient(Client *client, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *monitor);
static void run(void);
static void scan(void);
static int sendevent(Client *client, Atom proto);
static void sendmon(Client *client, Monitor *monitor);
static void setclientstate(Client *client, long state);
static void setclienttagprop(Client *client);
static void setfocus(Client *client);
static void setfullscreen(Client *client, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *client, int urg);
static void showhide(Client *client);
static void sigstatusbar(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *monitor);
static void togglebar(const Arg *arg);
static void toggleextrabar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscr(const Arg *arg);
static void togglescratch(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void freeicon(Client *client);
static void unfocus(Client *client, int setfocus);
static void unmanage(Client *client, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *monitor);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *client);
static void updatestatus(void);
static void updatetitle(Client *client);
static void updateicon(Client *client);
static void updatewindowtype(Client *client);
static void updatewmhints(Client *client);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static void winview(const Arg* arg);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void xinitvisual(void);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static char extra_status[256];
static int statusw;
static int statussig;
static pid_t statuspid = -1;
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static uint numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int restart = 0;
static int running = 1;
static Cur *cursor[CursorLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *monitors, *selmon;
static Window root, wmcheckwin;

static int alt_tab_direction = 0;
static Client *allclients = NULL;
static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

/* configuration, allows nested code to access above variables */
#include "config.def.h"

struct Pertag {
	uint curtag, prevtag; /* current and previous tag */
	int nmasters[LENGTH(tags) + 1]; /* number of windows in master area */
	float mfacts[LENGTH(tags) + 1]; /* mfacts per tag */
	uint sellts[LENGTH(tags) + 1]; /* selected layouts */
	const Layout *ltidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
	int showbars[LENGTH(tags) + 1]; /* display bar for the current tag */
};

static uint tagw[LENGTH(tags)];

/* compile-time check if all tags fit into an uint bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */

void
alttab(const Arg *arg) {
	(void) arg;
	if (allclients == NULL)
		return;

	for (Monitor *monitor = monitors; monitor; monitor = monitor->next)
		view(&(Arg){ .ui = (uint) ~0 });
	focusnext(&(Arg){ .i = alt_tab_direction });

	int grabbed = 1;
	int grabbed_keyboard = 1000;
	for (int i = 0; i < 100; i += 1) {
		struct timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 1000000;

		if (grabbed_keyboard != GrabSuccess) {
			grabbed_keyboard = XGrabKeyboard(dpy, DefaultRootWindow(dpy), True,
											 GrabModeAsync, GrabModeAsync, CurrentTime);
		}
		if (grabbed_keyboard == GrabSuccess) {
			XGrabButton(dpy, AnyButton, AnyModifier, None, False,
						BUTTONMASK, GrabModeAsync, GrabModeAsync,
						None, None);
			break;
		}
		nanosleep(&ts, NULL);
		if (i == 100 - 1)
			grabbed = 0;
	}

	XEvent event;
	Client *client;
	Monitor *monitor;
	XButtonPressedEvent *button_event;

	while (grabbed) {
		XNextEvent(dpy, &event);
		switch (event.type) {
		case KeyPress:
			if (event.xkey.keycode == tabCycleKey)
				focusnext(&(Arg){ .i = alt_tab_direction });
			else if (event.xkey.keycode == key_j)
				focusdir(&(Arg){ .i = 0 });
			else if (event.xkey.keycode == key_semicolon)
				focusdir(&(Arg){ .i = 1 });
			else if (event.xkey.keycode == key_l)
				focusdir(&(Arg){ .i = 2 });
			else if (event.xkey.keycode == key_k)
				focusdir(&(Arg){ .i = 3 });
			break;
		case KeyRelease:
			if (event.xkey.keycode == tabModKey) {
				XUngrabKeyboard(dpy, CurrentTime);
				XUngrabButton(dpy, AnyButton, AnyModifier, None);
				grabbed = 0;
				alt_tab_direction = !alt_tab_direction;
				winview(0);
			}
			break;
	    case ButtonPress:
			button_event = &(event.xbutton);
			if ((monitor = wintomon(button_event->window)) && monitor != selmon) {
				unfocus(selmon->sel, 1);
				selmon = monitor;
				focus(NULL);
			}
			if ((client = wintoclient(button_event->window)))
				focus(client);
			XAllowEvents(dpy, AsyncBoth, CurrentTime);
			break;
		case ButtonRelease:
			XUngrabKeyboard(dpy, CurrentTime);
			XUngrabButton(dpy, AnyButton, AnyModifier, None);
			grabbed = 0;
			alt_tab_direction = !alt_tab_direction;
			winview(0);
			break;
		default:
			break;
		}
	}
	return;
}

void
applyrules(Client *client)
{
	const char *class, *instance;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	client->isfloating = 0;
	client->tags = 0;
	XGetClassHint(dpy, client->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (int i = 0; i < LENGTH(rules); i++) {
		const Rule *r = &rules[i];
		Monitor *monitor;

		if ((!r->title || strstr(client->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			client->isfloating = r->isfloating;
			client->isfakefullscreen = r->isfakefullscreen;
			client->tags |= r->tags;
			if ((r->tags & SPTAGMASK) && r->isfloating) {
				client->x = client->monitor->wx + (client->monitor->ww / 2 - WIDTH(client) / 2);
				client->y = client->monitor->wy + (client->monitor->wh / 2 - HEIGHT(client) / 2);
			}

			for (monitor = monitors; monitor && monitor->num != r->monitor; monitor = monitor->next);
			if (monitor)
				client->monitor = monitor;
			if (r->switchtotag) {
				Arg a = { .ui = r->tags };
				view(&a);
			}
		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	client->tags = client->tags & TAGMASK ? client->tags & TAGMASK : (client->monitor->tagset[client->monitor->seltags] & (uint) ~SPTAGMASK);
	return;
}

int
applysizehints(Client *client, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *monitor = client->monitor;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(client);
		if (*y > sh)
			*y = sh - HEIGHT(client);
		if (*x + *w + 2 * client->bw < 0)
			*x = 0;
		if (*y + *h + 2 * client->bw < 0)
			*y = 0;
	} else {
		if (*x >= monitor->wx + monitor->ww)
			*x = monitor->wx + monitor->ww - WIDTH(client);
		if (*y >= monitor->wy + monitor->wh)
			*y = monitor->wy + monitor->wh - HEIGHT(client);
		if (*x + *w + 2 * client->bw <= monitor->wx)
			*x = monitor->wx;
		if (*y + *h + 2 * client->bw <= monitor->wy)
			*y = monitor->wy;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;
	if (resizehints || client->isfloating || !client->monitor->layout[client->monitor->layout_index]->arrange) {
		if (!client->hintsvalid)
			updatesizehints(client);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = client->basew == client->minw && client->baseh == client->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= client->basew;
			*h -= client->baseh;
		}
		/* adjust for aspect limits */
		if (client->min_a > 0 && client->max_a > 0) {
			if (client->max_a < (float)*w / *h)
				*w = *h * client->max_a + 0.5;
			else if (client->min_a < (float)*h / *w)
				*h = *w * client->min_a + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= client->basew;
			*h -= client->baseh;
		}
		/* adjust for increment value */
		if (client->incw)
			*w -= *w % client->incw;
		if (client->inch)
			*h -= *h % client->inch;
		/* restore base dimensions */
		*w = MAX(*w + client->basew, client->minw);
		*h = MAX(*h + client->baseh, client->minh);
		if (client->maxw)
			*w = MIN(*w, client->maxw);
		if (client->maxh)
			*h = MIN(*h, client->maxh);
	}
	return *x != client->x || *y != client->y || *w != client->w || *h != client->h;
}

void
arrange(Monitor *monitor)
{
	XEvent ev;
	if (monitor) {
		showhide(monitor->stack);
	} else {
		for (monitor = monitors; monitor; monitor = monitor->next)
			showhide(monitor->stack);
	}
	if (monitor) {
		arrangemon(monitor);
		restack(monitor);
	} else {
		for (monitor = monitors; monitor; monitor = monitor->next)
			arrangemon(monitor);
		XSync(dpy, False);
		while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	}
	return;
}

void
arrangemon(Monitor *monitor)
{
	strncpy(monitor->ltsymbol, monitor->layout[monitor->layout_index]->symbol, sizeof monitor->ltsymbol);
	if (monitor->layout[monitor->layout_index]->arrange)
		monitor->layout[monitor->layout_index]->arrange(monitor);
	return;
}

void
aspectresize(const Arg *arg) {
	/* only floating windows can be moved */
	Client *client;
	client = selmon->sel;
	float ratio;
	int w, h,nw, nh;

	if (!client || !arg)
		return;
	if (selmon->layout[selmon->layout_index]->arrange && !client->isfloating)
		return;

	ratio = (float)client->w / (float)client->h;
	h = arg->i;
	w = (int)(ratio * h);

	nw = client->w + w;
	nh = client->h + h;

	XRaiseWindow(dpy, client->win);
	resize(client, client->x, client->y, nw, nh, True);
	return;
}

void
attach(Client *client)
{
	client->next = client->monitor->clients;
	client->allnext = allclients;
	client->monitor->clients = client;
	allclients = client;
	return;
}

void
attachstack(Client *client)
{
	client->snext = client->monitor->stack;
	client->monitor->stack = client;
	return;
}

void
buttonpress(XEvent *e)
{
	uint i, x, click;
	Arg arg = {0};
	Client *client;
	Monitor *monitor;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClickRootWin;
	/* focus monitor if necessary */
	if ((monitor = wintomon(ev->window)) && monitor != selmon) {
		unfocus(selmon->sel, 1);
		selmon = monitor;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		i = x = 0;
		do
			x += tagw[i];
		while (ev->x >= x && ++i < LENGTH(tags));
		if (i < LENGTH(tags)) {
			click = ClickTagBar;
			arg.ui = 1 << i;
		} else if (ev->x < x + TEXTW(selmon->ltsymbol)) {
			click = ClickLtSymbol;
		} else if (ev->x > selmon->ww - statusw) {
			char *s;
			x = selmon->ww - statusw;
			click = ClickStatusText;
			statussig = 0;
			for (char *text = s = stext; *s && x <= ev->x; s++) {
				if ((unsigned char)(*s) < ' ') {
					char ch = *s;
					*s = '\0';
					x += TEXTW(text) - lrpad;
					*s = ch;
					text = s + 1;
					if (x >= ev->x)
						break;
					statussig = ch;
				}
			}
		} else {
			click = ClickWinTitle;
		}
	} else if (ev->window == selmon->extrabarwin) {
		x = 0;
		click = ClickExtraBar;
		statussig = 0;
		char *s = &extra_status[0];
		debug_dwm("extextl = %s, x=%d\n", extra_status, x);
		sleep(1);
		for (char *text = s; *s && x <= ev->x; s++) {
			debug_dwm("s = %d, x = %d\n", *s, x);
			if ((unsigned char)(*s) < ' ') {
				char ch = *s;
				*s = '\0';
				x += TEXTW(text) - lrpad;
				*s = ch;
				text = s + 1;
				if (x >= ev->x)
					break;
				statussig = ch;
			    debug_dwm("final statussigs = %d\n", *s);
			}
		}
		debug_dwm("outsides = %d = %client\n", *s, *s);
	} else if ((client = wintoclient(ev->window))) {
		focus(client);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClickClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClickTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);

	return;
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	size_t i;

	view(&a);
	selmon->layout[selmon->layout_index] = &foo;
	for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
		while (monitor->stack)
			unmanage(monitor->stack, 0);
	}
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (monitors)
		cleanupmon(monitors);
	for (i = 0; i < CursorLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		free(scheme[i]);
	free(scheme);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	return;
}

void
cleanupmon(Monitor *monitor)
{
	Monitor *m;

	if (monitor == monitors) {
		monitors = monitors->next;
	} else {
		for (m = monitors; m && m->next != monitor; m = m->next);
		m->next = monitor->next;
	}
	XUnmapWindow(dpy, monitor->barwin);
	XDestroyWindow(dpy, monitor->extrabarwin);
	XDestroyWindow(dpy, monitor->barwin);
	XDestroyWindow(dpy, monitor->extrabarwin);
	free(monitor);
	return;
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *client = wintoclient(cme->window);

	if (!client)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(client, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				      || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */
                                      && (!client->isfullscreen || client->isfakefullscreen))));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (client != selmon->sel && !client->isurgent)
			seturgent(client, 1);
	}
	return;
}

void
configure(Client *client)
{
	XConfigureEvent conf_event;

	conf_event.type = ConfigureNotify;
	conf_event.display = dpy;
	conf_event.event = client->win;
	conf_event.window = client->win;
	conf_event.x = client->x;
	conf_event.y = client->y;
	conf_event.width = client->w;
	conf_event.height = client->h;
	conf_event.border_width = client->bw;
	conf_event.above = None;
	conf_event.override_redirect = False;
	XSendEvent(dpy, client->win,
			   False, StructureNotifyMask, (XEvent *)&conf_event);
	return;
}

void
configurenotify(XEvent *e)
{
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (Monitor *m = monitors; m; m = m->next) {
				for (Client *client = m->clients; client; client = client->next) {
					if (client->isfullscreen && !client->isfakefullscreen)
						resizeclient(client, m->mx, m->my, m->mw, m->mh);
				}
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->bar_y, m->ww, bh);
				XMoveResizeWindow(dpy, m->extrabarwin, m->wx, m->extra_bar_y, m->ww, bh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
	return;
}

void
configurerequest(XEvent *e)
{
	Client *client;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((client = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth) {
			client->bw = ev->border_width;
		} else if (client->isfloating || !selmon->layout[selmon->layout_index]->arrange) {
			m = client->monitor;
			if (ev->value_mask & CWX) {
				client->oldx = client->x;
				client->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				client->oldy = client->y;
				client->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				client->oldw = client->w;
				client->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				client->oldh = client->h;
				client->h = ev->height;
			}
			if ((client->x + client->w) > m->mx + m->mw && client->isfloating)
				client->x = m->mx + (m->mw / 2 - WIDTH(client) / 2);
			if ((client->y + client->h) > m->my + m->mh && client->isfloating)
				client->y = m->my + (m->mh / 2 - HEIGHT(client) / 2);
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(client);
			if (ISVISIBLE(client))
				XMoveResizeWindow(dpy, client->win, client->x, client->y, client->w, client->h);
		} else {
			configure(client);
		}
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
	return;
}

Monitor *
createmon(void)
{
	Monitor *m;
	uint i;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
	m->extrabar = extrabar;
	m->layout[0] = &layouts[0];
	m->layout[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	m->pertag = ecalloc(1, sizeof(Pertag));
	m->pertag->curtag = m->pertag->prevtag = 1;

	for (i = 0; i <= LENGTH(tags); i++) {
		m->pertag->nmasters[i] = m->nmaster;
		m->pertag->mfacts[i] = m->mfact;

		m->pertag->ltidxs[i][0] = m->layout[0];
		m->pertag->ltidxs[i][1] = m->layout[1];
		m->pertag->sellts[i] = m->layout_index;

		m->pertag->showbars[i] = m->showbar;
	}

	return m;
}

void debug_dwm(char *message, ...) {
	char buffer[256];
	char *argv[6] = {
		"dunstify",
		"-t",
		"3000",
		"dwm",
		NULL,
		NULL,
	};

	va_list args;
	va_start(args, message);
	
	vsnprintf(buffer, sizeof (buffer),
			  message, args);
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
destroynotify(XEvent *e)
{
	Client *client;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((client = wintoclient(ev->window)))
		unmanage(client, 1);
	return;
}

void
detach(Client *client)
{
	Client **tc;

	for (tc = &client->monitor->clients; *tc && *tc != client; tc = &(*tc)->next);
	*tc = client->next;
	for (tc = &allclients; *tc && *tc != client; tc = &(*tc)->allnext);
	*tc = client->allnext;
	return;
}

void
detachstack(Client *client)
{
	Client **tc, *t;

	for (tc = &client->monitor->stack; *tc && *tc != client; tc = &(*tc)->snext);
	*tc = client->snext;

	if (client == client->monitor->sel) {
		for (t = client->monitor->stack; t && !ISVISIBLE(t); t = t->snext);
		client->monitor->sel = t;
	}
	return;
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = monitors;
	} else if (selmon == monitors) {
		for (m = monitors; m->next; m = m->next);
	} else {
		for (m = monitors; m->next != selmon; m = m->next);
	}
	return m;
}

void
drawbar(Monitor *m)
{
	int x, w, tw = 0, etwl = 0, etwr = 0;
	int boxs = drw->fonts->h / 9;
	int boxw = drw->fonts->h / 6 + 2;
	uint i, occ = 0, urg = 0;
	char tagdisp[TAGWIDTH];
	char *masterclientontag[LENGTH(tags)];

	if (!m->showbar)
		return;

	/* draw status first so it can be overdrawn by tags later */
	if (m == selmon) { /* status is only drawn on selected monitor */
		char *text, *s, ch;
		drw_setscheme(drw, scheme[SchemeNorm]);

		x = 0;
		for (text = s = stext; *s; s++) {
			if ((unsigned char)(*s) < ' ') {
				ch = *s;
				*s = '\0';
				tw = TEXTW(text) - lrpad;
				drw_text(drw, m->ww - statusw + x, 0, tw, bh, 0, text, 0);
				x += tw;
				*s = ch;
				text = s + 1;
			}
		}
		tw = TEXTW(text) - lrpad + 2;
		drw_text(drw, m->ww - statusw + x, 0, tw, bh, 0, text, 0);
		tw = statusw;
	}

	Client *icontagclient[LENGTH(tags)] = {0};

	for (i = 0; i < LENGTH(tags); i++) {
		masterclientontag[i] = NULL;
		icontagclient[i] = NULL;
	}

	for (Client *client = m->clients; client; client = client->next) {
		occ |= client->tags;
		if (client->isurgent)
			urg |= client->tags;
		for (i = 0; i < LENGTH(tags); i++) {
			if (client->icon && client->tags & (1 << i))
				icontagclient[i] = client;
			if (!masterclientontag[i] && client->tags & (1<<i)) {
				XClassHint ch = { NULL, NULL };
				XGetClassHint(dpy, client->win, &ch);
				masterclientontag[i] = ch.res_class;
			}
		}
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) {
		Client *client = icontagclient[i];

		if (masterclientontag[i]) {
			if (client) {
				snprintf(tagdisp, TAGWIDTH, "%s", tags[i]);
			} else {
				masterclientontag[i][strcspn(masterclientontag[i], tag_label_delim)] = '\0';
				snprintf(tagdisp, TAGWIDTH, tag_label_format, tags[i], masterclientontag[i]);
			}
		} else {
			snprintf(tagdisp, TAGWIDTH, tag_empty_format, tags[i]);
		}
		tagw[i] = w = TEXTW(tagdisp);
		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, tagdisp, urg & 1 << i);
		x += w;
		if (client) {
			drw_text(drw, x, 0, client->icon_width + lrpad/2, bh, 0, " ", urg & 1 << i);
			drw_pic(drw, x, (bh - client->icon_height) / 2, client->icon_width, client->icon_height, client->icon);
			x += client->icon_width + lrpad/2;
			tagw[i] += client->icon_width + lrpad/2;
		}
	}
	w = TEXTW(m->ltsymbol);
	drw_setscheme(drw, scheme[SchemeNorm]);
	x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

	if ((w = m->ww - tw - x) > bh) {
		if (m->sel) {
			drw_setscheme(drw, scheme[m == selmon ? SchemeSel : SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, m->sel->name, 0);
			if (m->sel->isfloating)
				drw_rect(drw, x + boxs, boxs, boxw, boxw, m->sel->isfixed, 0);
		} else {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
		}
	}
	drw_map(drw, m->barwin, 0, 0, m->ww, bh);

	if (m == selmon) { /* extra status is only drawn on selected monitor */
		drw_setscheme(drw, scheme[SchemeNorm]);
		/* clear default bar draw buffer by drawing a blank rectangle */
		drw_rect(drw, 0, 0, m->ww, bh, 1, 1);
		etwl = TEXTW(extra_status);
		drw_text(drw, 0, 0, etwl, bh, 0, extra_status, 0);
		drw_map(drw, m->extrabarwin, 0, 0, m->ww, bh);
	}
	return;
}

void
drawbars(void)
{
	for (Monitor *m = monitors; m; m = m->next)
		drawbar(m);
	return;
}

void
enternotify(XEvent *e)
{
	Client *client;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	client = wintoclient(ev->window);
	m = client ? client->monitor : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!client || client == selmon->sel) {
		return;
	}
	focus(client);
	return;
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
	return;
}

void
focus(Client *client)
{
	if (!client || !ISVISIBLE(client))
		for (client = selmon->stack; client && !ISVISIBLE(client); client = client->snext);
	if (selmon->sel && selmon->sel != client)
		unfocus(selmon->sel, 0);
	if (client) {
		if (client->monitor != selmon)
			selmon = client->monitor;
		if (client->isurgent)
			seturgent(client, 0);
		detachstack(client);
		attachstack(client);
		grabbuttons(client, 1);
		XSetWindowBorder(dpy, client->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(client);
	} else {
		XSetInputFocus(dpy, selmon->barwin, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = client;
	drawbars();
	return;
}

void
focusdir(const Arg *arg)
{
	Client *s = selmon->sel, *f = NULL, *client, *next;

	if (!s)
		return;

	uint score = -1;
	uint client_score;
	int dist;
	int dirweight = 20;
	int isfloating = s->isfloating;

	next = s->next;
	if (!next)
		next = s->monitor->clients;
	for (client = next; client != s; client = next) {

		next = client->next;
		if (!next)
			next = s->monitor->clients;

		if (!ISVISIBLE(client) || client->isfloating != isfloating) // || HIDDEN(client)
			continue;

		switch (arg->i) {
		case 0: // left
			dist = s->x - client->x - client->w;
			client_score =
				dirweight * MIN(abs(dist), abs(dist + s->monitor->ww)) +
				abs(s->y - client->y);
			break;
		case 1: // right
			dist = client->x - s->x - s->w;
			client_score =
				dirweight * MIN(abs(dist), abs(dist + s->monitor->ww)) +
				abs(client->y - s->y);
			break;
		case 2: // up
			dist = s->y - client->y - client->h;
			client_score =
				dirweight * MIN(abs(dist), abs(dist + s->monitor->wh)) +
				abs(s->x - client->x);
			break;
		default:
		case 3: // down
			dist = client->y - s->y - s->h;
			client_score =
				dirweight * MIN(abs(dist), abs(dist + s->monitor->wh)) +
				abs(client->x - s->x);
			break;
		}

		if (((arg->i == 0 || arg->i == 2) && client_score <= score) || client_score < score) {
			score = client_score;
			f = client;
		}
	}

	if (f && f != s) {
		focus(f);
		restack(f->monitor);
	}
	return;
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
	return;
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!monitors->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
	return;
}

static void
focusnext(const Arg *arg) {
	Monitor *m;
	Client *client;

	m = selmon;
	client = m->sel;
	while (client == NULL && m->next) {
		m = m->next;
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
		client = m->sel;
	}
	if (client == NULL)
		return;

	if (arg->i) {
		if (client->allnext)
			client = client->allnext;
		else
			client = allclients;
	} else {
		Client *last = client;
		if (last == allclients)
			last = NULL;
		for (client = allclients; client->allnext != last; client = client->allnext);
	}
	focus(client);
	return;
}

void
focusstack(const Arg *arg)
{
	Client *client = NULL, *i;

	if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
		return;
	if (arg->i > 0) {
		for (client = selmon->sel->next; client && !ISVISIBLE(client); client = client->next);
		if (!client)
			for (client = selmon->clients; client && !ISVISIBLE(client); client = client->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next) {
			if (ISVISIBLE(i))
				client = i;
		}
		if (!client) {
			for (; i; i = i->next) {
				if (ISVISIBLE(i))
					client = i;
			}
		}
	}
	if (client) {
		focus(client);
		restack(selmon);
	}
	return;
}

static void
focusurgent(const Arg *arg) {
	(void) arg;
	for (Monitor *m = monitors; m; m = m->next) {
		Client *client;

		for (client = m->clients; client && !client->isurgent; client = client->next);
		if (client) {
			int i;
			unfocus(selmon->sel, 0);
			selmon = m;
			for (i = 0; i < LENGTH(tags) && !((1 << i) & client->tags); i++);
			if (i < LENGTH(tags)) {
				const Arg a = {.ui = 1 << i};
				view(&a);
				focus(client);
			}
		}
	}
	return;
}

void
gaplessgrid(Monitor *m) {
	uint n, cols, rows, cn, rn, i, cx, cy, cw, ch;
	Client *client;

	for (n = 0, client = nexttiled(m->clients); client; client = nexttiled(client->next), n++);
	if (n == 0)
		return;

	/* grid dimensions */
	for (cols = 0; cols <= n/2; cols++) {
		if (cols*cols >= n)
			break;
	}
	if (n == 5) /* set layout against the general calculation: not 1:2:2, but 2:3 */
		cols = 2;
	rows = n/cols;

	/* window geometries */
	cw = cols ? m->ww / cols : m->ww;
	cn = 0; /* current column number */
	rn = 0; /* current row number */
	for (i = 0, client = nexttiled(m->clients); client; i++, client = nexttiled(client->next)) {
		if (i/rows + 1 > cols - n%cols)
			rows = n/cols + 1;
		ch = rows ? m->wh / rows : m->wh;
		cx = m->wx + cn*cw;
		cy = m->wy + rn*ch;
		resize(client, cx, cy, cw - 2 * client->bw, ch - 2 * client->bw, False);
		rn++;
		if (rn >= rows) {
			rn = 0;
			cn++;
		}
	}
	return;
}

Atom
getatomprop(Client *client, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, client->win, prop, 0L, sizeof atom, False, XA_ATOM,
						   &da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

pid_t
getstatusbarpid(void)
{
	char buf[32], *str = buf, *client;
	FILE *fp;

	if (statuspid > 0) {
		snprintf(buf, sizeof(buf), "/proc/%u/cmdline", statuspid);
		if ((fp = fopen(buf, "r"))) {
			fgets(buf, sizeof(buf), fp);
			while ((client = strchr(str, '/')))
				str = client + 1;
			fclose(fp);
			if (!strcmp(str, STATUSBAR))
				return statuspid;
		}
	}
	if (!(fp = popen("pidof -s "STATUSBAR, "r")))
		return -1;
	fgets(buf, sizeof(buf), fp);
	pclose(fp);
	return strtol(buf, NULL, 10);
}

static uint32 prealpha(uint32 p) {
	uint8_t a = p >> 24u;
	uint32 rb = (a * (p & 0xFF00FFu)) >> 8u;
	uint32 g = (a * (p & 0x00FF00u)) >> 8u;
	return (rb & 0xFF00FFu) | (g & 0x00FF00u) | (a << 24u);
}

Picture
geticonprop(Window win, uint *picw, uint *pich)
{
	int format;
	unsigned long n, extra, *p = NULL;
	Atom real;

	if (XGetWindowProperty(dpy, win, netatom[NetWMIcon], 0L, LONG_MAX, False, AnyPropertyType, 
						   &real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return None; 
	if (n == 0 || format != 32) {
		XFree(p);
		return None;
	}

	unsigned long *bstp = NULL;
	uint32 w, h, sz;
	{
		unsigned long *i; const unsigned long *end = p + n;
		uint32 bstd = UINT32_MAX, d, m;
		for (i = p; i < end - 1; i += sz) {
			if ((w = *i++) >= 16384 || (h = *i++) >= 16384) {
				XFree(p);
				return None;
			}
			if ((sz = w * h) > end - i)
				break;
			if ((m = w > h ? w : h) >= ICONSIZE && (d = m - ICONSIZE) < bstd) {
				bstd = d;
				bstp = i;
			}
		}
		if (!bstp) {
			for (i = p; i < end - 1; i += sz) {
				if ((w = *i++) >= 16384 || (h = *i++) >= 16384) {
					XFree(p);
					return None;
				}
				if ((sz = w * h) > end - i)
					break;
				if ((d = ICONSIZE - (w > h ? w : h)) < bstd) {
					bstd = d;
					bstp = i;
				}
			}
		}
		if (!bstp) {
			XFree(p);
			return None;
		}
	}

	if ((w = *(bstp - 2)) == 0 || (h = *(bstp - 1)) == 0) {
		XFree(p);
		return None;
	}

	uint32 icon_width, icon_height;
	if (w <= h) {
		icon_height = ICONSIZE; icon_width = w * ICONSIZE / h;
		if (icon_width == 0)
			icon_width = 1;
	} else {
		icon_width = ICONSIZE; icon_height = h * ICONSIZE / w;
		if (icon_height == 0)
			icon_height = 1;
	}
	*picw = icon_width; *pich = icon_height;

	uint32 i, *bstp32 = (uint32 *)bstp;
	for (sz = w * h, i = 0; i < sz; ++i)
		bstp32[i] = prealpha(bstp[i]);

	Picture ret = drw_picture_create_resized(drw, (char *)bstp, w, h, icon_width, icon_height);
	XFree(p);

	return ret;
}

int
getrootptr(int *x, int *y)
{
	int di;
	uint dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, uint size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *client, int focused)
{
	uint i, j;
	uint modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

	updatenumlockmask();
	XUngrabButton(dpy, AnyButton, AnyModifier, client->win);
	if (!focused)
		XGrabButton(dpy, AnyButton, AnyModifier, client->win, False,
			        BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
	for (i = 0; i < LENGTH(buttons); i++) {
		if (buttons[i].click == ClickClientWin) {
			for (j = 0; j < LENGTH(modifiers); j++)
				XGrabButton(dpy, buttons[i].button,
							buttons[i].mask | modifiers[j],
							client->win, False, BUTTONMASK,
							GrabModeAsync, GrabModeSync, None, None);
		}
	}
	return;
}

void
grabkeys(void)
{
	uint i, j, k;
	uint modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
	int start, end, skip;
	KeySym *syms;

	updatenumlockmask();

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	XDisplayKeycodes(dpy, &start, &end);
	syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
	if (!syms)
		return;
	for (k = start; k <= end; k++) {
		for (i = 0; i < LENGTH(keys); i++) {
			/* skip modifier codes, we do that ourselves */
			if (keys[i].keysym == syms[(k - start) * skip]) {
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabKey(dpy, k, keys[i].mod | modifiers[j],
					    	 root, True, GrabModeAsync, GrabModeAsync);
			}
		}
	}
	XFree(syms);
	return;
}

void
incnmaster(const Arg *arg)
{
	int nslave = 0;
	Client *client = selmon->clients;

	for (client = nexttiled(client->next); client; client = nexttiled(client->next), nslave++);

	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag] = MAX(MIN(selmon->nmaster + arg->i, nslave + 1), 0);
	arrange(selmon);
	return;
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	uint i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++) {
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
	}
	return;
}

void
killclient(const Arg *arg)
{
	(void) arg;
	if (!selmon->sel)
		return;
	if (!sendevent(selmon->sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	return;
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *client, *t = NULL;
	Window trans = None;
	XWindowChanges wc;

	client = ecalloc(1, sizeof(Client));
	client->win = w;
	/* geometry */
	client->x = client->oldx = wa->x;
	client->y = client->oldy = wa->y;
	client->w = client->oldw = wa->width;
	client->h = client->oldh = wa->height;
	client->oldbw = wa->border_width;

	updateicon(client);
	updatetitle(client);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		client->monitor = t->monitor;
		client->tags = t->tags;
	} else {
		client->monitor = selmon;
		applyrules(client);
	}

	if (client->x + WIDTH(client) > client->monitor->wx + client->monitor->ww)
		client->x = client->monitor->wx + client->monitor->ww - WIDTH(client);
	if (client->y + HEIGHT(client) > client->monitor->wy + client->monitor->wh)
		client->y = client->monitor->wy + client->monitor->wh - HEIGHT(client);
	client->x = MAX(client->x, client->monitor->wx);
	client->y = MAX(client->y, client->monitor->wy);
	client->bw = borderpx;

	wc.border_width = client->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(client); /* propagates border_width, if size doesn't change */
	updatewindowtype(client);
	updatesizehints(client);
	updatewmhints(client);
	{
		int format;
		unsigned long *data, n, extra;
		Monitor *m;
		Atom atom;
		if (XGetWindowProperty(dpy, client->win, netatom[NetClientInfo], 0L, 2L, False, XA_CARDINAL,
				&atom, &format, &n, &extra, (unsigned char **)&data) == Success && n == 2) {
			client->tags = *data;
			for (m = monitors; m; m = m->next) {
				if (m->num == *(data+1)) {
					client->monitor = m;
					break;
				}
			}
		}
		if (n > 0)
			XFree(data);
	}
	setclienttagprop(client);

	client->stored_fx = client->x;
	client->stored_fy = client->y;
	client->stored_fw = client->w;
	client->stored_fh = client->h;
	client->x = client->monitor->mx + (client->monitor->mw - WIDTH(client)) / 2;
	client->y = client->monitor->my + (client->monitor->mh - HEIGHT(client)) / 2;
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(client, 0);
	if (!client->isfloating)
		client->isfloating = client->oldstate = trans != None || client->isfixed;
	if (client->isfloating)
		XRaiseWindow(dpy, client->win);
	attach(client);
	attachstack(client);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(client->win), 1);
	XMoveResizeWindow(dpy, client->win, client->x + 2 * sw, client->y, client->w, client->h); /* some windows require this */
	setclientstate(client, NormalState);
	if (client->monitor == selmon)
		unfocus(selmon->sel, 0);
	client->monitor->sel = client;
	arrange(client->monitor);
	XMapWindow(dpy, client->win);
	focus(NULL);
	return;
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
	return;
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
	return;
}

void
monocle(Monitor *m)
{
	uint n = 0;
	Client *client;

	for (client = m->clients; client; client = client->next) {
		if (ISVISIBLE(client))
			n++;
	}
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (client = nexttiled(m->clients); client; client = nexttiled(client->next))
		resize(client, m->wx, m->wy, m->ww - 2 * client->bw, m->wh - 2 * client->bw, 0);
	return;
}

void
motionnotify(XEvent *e)
{
	static Monitor *monitor = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != monitor && monitor) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	monitor = m;
	return;
}

void
movemouse(const Arg *arg)
{
	(void) arg;
	int x, y, ocx, ocy, nx, ny;
	Client *client;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(client = selmon->sel))
		return;
	if (client->isfullscreen && !client->isfakefullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = client->x;
	ocy = client->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CursorMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch (ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(client))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(client);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(client))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(client);
			if (!client->isfloating && selmon->layout[selmon->layout_index]->arrange
			&& (abs(nx - client->x) > snap || abs(ny - client->y) > snap))
				togglefloating(NULL);
			if (!selmon->layout[selmon->layout_index]->arrange || client->isfloating)
				resize(client, nx, ny, client->w, client->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(client->x, client->y, client->w, client->h)) != selmon) {
		sendmon(client, m);
		selmon = m;
		focus(NULL);
	}
	return;
}

Client *
nexttiled(Client *client)
{
	for (; client && (client->isfloating || !ISVISIBLE(client)); client = client->next);
	return client;
}

void
pop(Client *client)
{
	detach(client);
	attach(client);
	focus(client);
	arrange(client->monitor);
	return;
}

void
propertynotify(XEvent *e)
{
	Client *client;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME)) {
		updatestatus();
	} else if (ev->state == PropertyDelete) {
		return; /* ignore */
	} else if ((client = wintoclient(ev->window))) {
		switch (ev->atom) {
		default:
			break;
		case XA_WM_TRANSIENT_FOR:
			if (!client->isfloating && (XGetTransientForHint(dpy, client->win, &trans)) &&
				(client->isfloating = (wintoclient(trans)) != NULL))
				arrange(client->monitor);
			break;
		case XA_WM_NORMAL_HINTS:
			client->hintsvalid = 0;
			break;
		case XA_WM_HINTS:
			updatewmhints(client);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(client);
			if (client == client->monitor->sel)
				drawbar(client->monitor);
		}
		else if (ev->atom == netatom[NetWMIcon]) {
			updateicon(client);
			if (client == client->monitor->sel)
				drawbar(client->monitor);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(client);
	}
	return;
}

void
quit(const Arg *arg)
{
	if (arg->i)
		restart = 1;
	running = 0;
	return;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = monitors; m; m = m->next) {
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	}
	return r;
}

void
resize(Client *client, int x, int y, int w, int h, int interact)
{
	if (applysizehints(client, &x, &y, &w, &h, interact))
		resizeclient(client, x, y, w, h);
	return;
}

void
resizeclient(Client *client, int x, int y, int w, int h)
{
	XWindowChanges wc;
	uint n;
	Client *nbc;

	client->oldx = client->x; client->x = wc.x = x;
	client->oldy = client->y; client->y = wc.y = y;
	client->oldw = client->w; client->w = wc.width = w;
	client->oldh = client->h; client->h = wc.height = h;
	wc.border_width = client->bw;

	for (n = 0, nbc = nexttiled(selmon->clients); nbc; nbc = nexttiled(nbc->next), n++);

	if (!(client->isfloating) && selmon->layout[selmon->layout_index]->arrange) {
		if (selmon->layout[selmon->layout_index]->arrange == monocle || n == 1) {
			wc.border_width = 0;
			client->w = wc.width += client->bw * 2;
			client->h = wc.height += client->bw * 2;
		}
	}

	XConfigureWindow(dpy, client->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(client);
	XSync(dpy, False);
	return;
}

void
resizemouse(const Arg *arg)
{
	(void) arg;
	int ocx, ocy, nw, nh;
	Client *client;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(client = selmon->sel))
		return;
	if (client->isfullscreen && !client->isfakefullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = client->x;
	ocy = client->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
					 None, cursor[CursorResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, client->win, 0, 0, 0, 0, client->w + client->bw - 1, client->h + client->bw - 1);
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch (ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nw = MAX(ev.xmotion.x - ocx - 2 * client->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * client->bw + 1, 1);
			if (client->monitor->wx + nw >= selmon->wx && client->monitor->wx + nw <= selmon->wx + selmon->ww
			&& client->monitor->wy + nh >= selmon->wy && client->monitor->wy + nh <= selmon->wy + selmon->wh) {
				if (!client->isfloating && selmon->layout[selmon->layout_index]->arrange
				&& (abs(nw - client->w) > snap || abs(nh - client->h) > snap))
					togglefloating(NULL);
			}
			if (!selmon->layout[selmon->layout_index]->arrange || client->isfloating)
				resize(client, client->x, client->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, client->win, 0, 0, 0, 0, client->w + client->bw - 1, client->h + client->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(client->x, client->y, client->w, client->h)) != selmon) {
		sendmon(client, m);
		selmon = m;
		focus(NULL);
	}
	return;
}

void
restack(Monitor *m)
{
	Client *client;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;
	if (m->sel->isfloating || !m->layout[m->layout_index]->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if (m->layout[m->layout_index]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (client = m->stack; client; client = client->snext) {
			if (!client->isfloating && ISVISIBLE(client)) {
				XConfigureWindow(dpy, client->win, CWSibling|CWStackMode, &wc);
				wc.sibling = client->win;
			}
		}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	return;
}

void
run(void)
{
	XEvent ev;
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev)) {
		if (handler[ev.type])
			handler[ev.type](&ev);
	}
	return;
}

void
scan(void)
{
	uint i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
	return;
}

void
sendmon(Client *client, Monitor *m)
{
	if (client->monitor == m)
		return;
	unfocus(client, 1);
	detach(client);
	detachstack(client);
	client->monitor = m;
	client->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(client);
	attachstack(client);
	setclienttagprop(client);
	focus(NULL);
	arrange(NULL);
	return;
}

void
setclientstate(Client *client, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, client->win, wmatom[WMState], wmatom[WMState], 32,
		            PropModeReplace, (unsigned char *)data, 2);
	return;
}

int
sendevent(Client *client, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, client->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = client->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, client->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *client)
{
	if (!client->neverfocus) {
		XSetInputFocus(dpy, client->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(client->win), 1);
	}
	sendevent(client, wmatom[WMTakeFocus]);
	return;
}

void
setfullscreen(Client *client, int fullscreen)
{
	if (fullscreen && !client->isfullscreen) {
		XChangeProperty(dpy, client->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		client->isfullscreen = 1;
		if (client->isfakefullscreen) {
			resizeclient(client, client->x, client->y, client->w, client->h);
			return;
		}
		client->oldstate = client->isfloating;
		client->oldbw = client->bw;
		client->bw = 0;
		client->isfloating = 1;
		resizeclient(client, client->monitor->mx, client->monitor->my, client->monitor->mw, client->monitor->mh);
		XRaiseWindow(dpy, client->win);
	} else if (!fullscreen && client->isfullscreen) {
		XChangeProperty(dpy, client->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		client->isfullscreen = 0;
		if (client->isfakefullscreen) {
			resizeclient(client, client->x, client->y, client->w, client->h);
			return;
		}
		client->isfloating = client->oldstate;
		client->bw = client->oldbw;
		client->x = client->oldx;
		client->y = client->oldy;
		client->w = client->oldw;
		client->h = client->oldh;
		resizeclient(client, client->x, client->y, client->w, client->h);
		arrange(client->monitor);
	}
	return;
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->layout[selmon->layout_index])
		selmon->layout_index = selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
	if (arg && arg->v)
		selmon->layout[selmon->layout_index] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->layout_index] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->layout[selmon->layout_index]->symbol, sizeof selmon->ltsymbol);
	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
	return;
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->layout[selmon->layout_index]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
	arrange(selmon);
	return;
}

void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	xinitvisual();
	drw = drw_create(dpy, screen, root, sw, sh, visual, depth, cmap);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h / 2;
	bh = drw->fonts->h + 2;
	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMIcon] = XInternAtom(dpy, "_NET_WM_ICON", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetClientInfo] = XInternAtom(dpy, "_NET_CLIENT_INFO", False);
	/* init cursors */
	cursor[CursorNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CursorResize] = drw_cur_create(drw, XC_sizing);
	cursor[CursorMove] = drw_cur_create(drw, XC_fleur);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], alphas[i], 3);
	/* init bars */
	updatebars();
	updatestatus();
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	XDeleteProperty(dpy, root, netatom[NetClientInfo]);
	/* select events */
	wa.cursor = cursor[CursorNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
	for (Monitor *m = monitors; m; m = m->next) {
		unfocus(selmon->sel, 0);
		selmon = m;
		focus(NULL);
		Arg lay_monocle = {.v = &layouts[2]};
		Arg lay_grid = {.v = &layouts[3]};
		Arg tag8 = {.ui = 1 << 5};
		Arg tag1 = {.ui = 1 << 0};
		Arg tag0 = {.ui = ~0};
		view(&tag8);
		setlayout(&lay_monocle);
		togglebar(0);
		view(&tag0);
		setlayout(&lay_grid);
		view(&tag1);
	}
	return;
}

void
seturgent(Client *client, int urg)
{
	XWMHints *wmh;

	client->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, client->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, client->win, wmh);
	XFree(wmh);
	return;
}

void
showhide(Client *client)
{
	if (!client)
		return;
	if (ISVISIBLE(client)) {
		if ((client->tags & SPTAGMASK) && client->isfloating) {
			client->x = client->monitor->wx + (client->monitor->ww / 2 - WIDTH(client) / 2);
			client->y = client->monitor->wy + (client->monitor->wh / 2 - HEIGHT(client) / 2);
		}
		/* show clients top down */
		XMoveWindow(dpy, client->win, client->x, client->y);
		if ((!client->monitor->layout[client->monitor->layout_index]->arrange || client->isfloating)
                && (!client->isfullscreen || client->isfakefullscreen))
			resize(client, client->x, client->y, client->w, client->h, 0);
		showhide(client->snext);
	} else {
		/* hide clients bottom up */
		showhide(client->snext);
		XMoveWindow(dpy, client->win, WIDTH(client) * -2, client->y);
	}
	return;
}

void
sigstatusbar(const Arg *arg)
{
	union sigval sv;

	if (!statussig)
		return;
    sv.sival_int = arg->i | ((SIGRTMIN+statussig) << 3);

	if ((statuspid = getstatusbarpid()) <= 0)
		return;

	sigqueue(statuspid, SIGUSR1, sv);
}

void
setclienttagprop(Client *client)
{
	long data[] = { (long) client->tags, (long) client->monitor->num };
	XChangeProperty(dpy, client->win, netatom[NetClientInfo], XA_CARDINAL, 32,
			        PropModeReplace, (unsigned char *) data, 2);
	return;
}

void
tag(const Arg *arg)
{
	Client *client;
	if (selmon->sel && arg->ui & TAGMASK) {
		client = selmon->sel;
		selmon->sel->tags = arg->ui & TAGMASK;
		setclienttagprop(client);
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	if (!selmon->sel || !monitors->next)
		return;
	Monitor *monitor = dirtomon(arg->i);
	if (selmon->sel->isfloating) {
		selmon->sel->x += monitor->mx - selmon->mx;
		selmon->sel->y += monitor->my - selmon->my;
	}
	sendmon(selmon->sel, monitor);
	usleep(50);
	focus(NULL);
	usleep(50);
	focusmon(arg);
	togglefloating(NULL);
	togglefloating(NULL);
	return;
}

void
col(Monitor *m)
{
	uint i, n, h, w, x, y, mw;
	Client *client;

	for (n = 0, client = nexttiled(m->clients); client; client = nexttiled(client->next), n++);
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->ww * m->mfact : 0;
	else
		mw = m->ww;
	for (i = x = y = 0, client = nexttiled(m->clients); client; client = nexttiled(client->next), i++) {
		if (i < m->nmaster) {
			w = (mw - x) / (MIN(n, m->nmaster) - i);
			resize(client, x + m->wx, m->wy, w - (2 * client->bw), m->wh - (2 * client->bw), 0);
			x += WIDTH(client);
		} else {
			h = (m->wh - y) / (n - i);
			resize(client, x + m->wx, m->wy + y, m->ww - x - (2 * client->bw), h - (2 * client->bw), 0);
			y += HEIGHT(client);
		}
	}
	return;
}

void
tile(Monitor *m)
{
	uint i, n, h, mw, my, ty;
	Client *client;

	for (n = 0, client = nexttiled(m->clients); client; client = nexttiled(client->next), n++);
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->ww * m->mfact : 0;
	else
		mw = m->ww;
	for (i = my = ty = 0, client = nexttiled(m->clients); client; client = nexttiled(client->next), i++)
		if (i < m->nmaster) {
			h = (m->wh - my) / (MIN(n, m->nmaster) - i);
			resize(client, m->wx, m->wy + my, mw - (2*client->bw), h - (2*client->bw), 0);
			if (my + HEIGHT(client) < m->wh)
				my += HEIGHT(client);
		} else {
			h = (m->wh - ty) / (n - i);
			resize(client, m->wx + mw, m->wy + ty, m->ww - mw - (2*client->bw), h - (2*client->bw), 0);
			if (ty + HEIGHT(client) < m->wh)
				ty += HEIGHT(client);
		}
	return;
}

void
togglebar(const Arg *arg)
{
	(void) arg;
	selmon->showbar = selmon->pertag->showbars[selmon->pertag->curtag] = !selmon->showbar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->bar_y, selmon->ww, bh);
	arrange(selmon);
	return;
}

void
toggleextrabar(const Arg *arg)
{
	selmon->extrabar = !selmon->extrabar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->extrabarwin, selmon->wx, selmon->extra_bar_y, selmon->ww, bh);
	arrange(selmon);
}


void
togglefloating(const Arg *arg)
{
	(void) arg;
	if (!selmon->sel)
		return;
	if (selmon->sel->isfullscreen && !selmon->sel->isfakefullscreen) /* no support for fullscreen windows */
		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if (selmon->sel->isfloating) {
		resize(selmon->sel, selmon->sel->stored_fx, selmon->sel->stored_fy,
		       selmon->sel->stored_fw, selmon->sel->stored_fh, False);
	} else {
		/*save last known float dimensions*/
		selmon->sel->stored_fx = selmon->sel->x;
		selmon->sel->stored_fy = selmon->sel->y;
		selmon->sel->stored_fw = selmon->sel->w;
		selmon->sel->stored_fh = selmon->sel->h;
	}

    selmon->sel->x = selmon->sel->monitor->mx + (selmon->sel->monitor->mw - WIDTH(selmon->sel)) / 2;
    selmon->sel->y = selmon->sel->monitor->my + (selmon->sel->monitor->mh - HEIGHT(selmon->sel)) / 2;

	arrange(selmon);
}

void
togglefullscr(const Arg *arg)
{
	(void) arg;
	if (selmon->sel)
		setfullscreen(selmon->sel, !selmon->sel->isfullscreen);
	return;
}

void
spawn(const Arg *arg)
{
   struct sigaction sa;

   if (fork() == 0) {
       if (dpy)
           close(ConnectionNumber(dpy));
       setsid();

       sigemptyset(&sa.sa_mask);
       sa.sa_flags = 0;
       sa.sa_handler = SIG_DFL;
       sigaction(SIGCHLD, &sa, NULL);

       execvp(((char **)arg->v)[0], (char **)arg->v);
       die("dwm: execvp '%s' failed:", ((char **)arg->v)[0]);
   }
}

void
togglescratch(const Arg *arg)
{
	Client *client;
	uint found = 0;
	uint scratchtag = SPTAG(arg->ui);
	Arg sparg = {.v = scratchpads[arg->ui].cmd};

	for (client = selmon->clients; client && !(found = client->tags & scratchtag); client = client->next);
	if (found) {
		uint this_tag = client->tags & selmon->tagset[selmon->seltags];
		uint newtagset = selmon->tagset[selmon->seltags] ^ scratchtag;

		if (this_tag) {
			client->tags = scratchtag;
		} else {
			client->tags |= selmon->tagset[selmon->seltags];
		}
		if (newtagset) {
			selmon->tagset[selmon->seltags] = newtagset;
			focus(NULL);
			arrange(selmon);
		}
		if (ISVISIBLE(client)) {
			focus(client);
			restack(selmon);
		}
	} else {
		selmon->tagset[selmon->seltags] |= scratchtag;
		spawn(&sparg);
	}
}

void
toggletag(const Arg *arg)
{
	uint newtags;

	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		setclienttagprop(selmon->sel);
		focus(NULL);
		arrange(selmon);
	}
	return;
}

void
toggleview(const Arg *arg)
{
	uint newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
	int i;

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;

		if (newtagset == ~0) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			selmon->pertag->curtag = 0;
		}

		/* test if the user did not select the same tag */
		if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			for (i = 0; !(newtagset & 1 << i); i++) ;
			selmon->pertag->curtag = i + 1;
		}

		/* apply settings for this view */
		selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
		selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
		selmon->layout_index = selmon->pertag->sellts[selmon->pertag->curtag];
		selmon->layout[selmon->layout_index] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->layout_index];
		selmon->layout[selmon->layout_index^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->layout_index^1];

		if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
			togglebar(NULL);

		focus(NULL);
		arrange(selmon);
	}
	return;
}

void
freeicon(Client *client)
{
	if (client->icon) {
		XRenderFreePicture(dpy, client->icon);
		client->icon = None;
	}
	return;
}

void
unfocus(Client *client, int setfocus)
{
	if (!client)
		return;
	grabbuttons(client, 0);
	XSetWindowBorder(dpy, client->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	return;
}

void
unmanage(Client *client, int destroyed)
{
	Monitor *m = client->monitor;
	XWindowChanges wc;

	detach(client);
	detachstack(client);
	freeicon(client);
	if (!destroyed) {
		wc.border_width = client->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, client->win, NoEventMask);
		XConfigureWindow(dpy, client->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, client->win);
		setclientstate(client, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(client);
	focus(NULL);
	updateclientlist();
	arrange(m);
	return;
}

void
unmapnotify(XEvent *e)
{
	Client *client;
	XUnmapEvent *ev = &e->xunmap;

	if ((client = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(client, WithdrawnState);
		else
			unmanage(client, 0);
	}
	return;
}

void
updatebars(void)
{
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = 0,
		.border_pixel = 0,
		.colormap = cmap,
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"dwm", "dwm"};
	for (m = monitors; m; m = m->next) {
		if (!m->barwin) {
			m->barwin = XCreateWindow(dpy, root, m->wx, m->bar_y, m->ww, bh, 0, depth,
					InputOutput, visual,
					CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
			XDefineCursor(dpy, m->barwin, cursor[CursorNormal]->cursor);
			XMapRaised(dpy, m->barwin);
			XSetClassHint(dpy, m->barwin, &ch);
		}
		if (!m->extrabarwin) {
			m->extrabarwin = XCreateWindow(dpy, root, m->wx, m->extra_bar_y, m->ww, bh, 0, depth,
					InputOutput, visual,
					CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
			XDefineCursor(dpy, m->extrabarwin, cursor[CursorNormal]->cursor);
			XMapRaised(dpy, m->extrabarwin);
			XSetClassHint(dpy, m->extrabarwin, &ch);
		}
	}
	return;
}

void
updatebarpos(Monitor *m)
{
	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) {
		m->wh -= bh;
		m->bar_y = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	} else {
		m->bar_y = -bh;
	}
	if (m->extrabar) {
		m->wh -= bh;
		m->extra_bar_y = !m->topbar ? m->wy : m->wy + m->wh;
		m->wy = !m->topbar ? m->wy + bh : m->wy;
	} else
		m->extra_bar_y = -bh;
	return;
}

void
updateclientlist(void)
{
	Client *client;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = monitors; m; m = m->next) {
		for (client = m->clients; client; client = client->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
							XA_WINDOW, 32, PropModeAppend,
							(unsigned char *) &(client->win), 1);
	}
	return;
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *client;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = monitors; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = monitors; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				monitors = createmon();
		}
		for (i = 0, m = monitors; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh) {
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = monitors; m && m->next; m = m->next);
			while ((client = m->clients)) {
				dirty = 1;
				m->clients = client->next;
				allclients = client->allnext;
				detachstack(client);
				client->monitor = monitors;
				attach(client);
				attachstack(client);
			}
			if (m == selmon)
				selmon = monitors;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!monitors)
			monitors = createmon();
		if (monitors->mw != sw || monitors->mh != sh) {
			dirty = 1;
			monitors->mw = monitors->ww = sw;
			monitors->mh = monitors->wh = sh;
			updatebarpos(monitors);
		}
	}
	if (dirty) {
		selmon = monitors;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	uint i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++) {
		for (j = 0; j < modmap->max_keypermod; j++) {
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
		}
	}
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *client)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, client->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		client->basew = size.base_width;
		client->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		client->basew = size.min_width;
		client->baseh = size.min_height;
	} else
		client->basew = client->baseh = 0;
	if (size.flags & PResizeInc) {
		client->incw = size.width_inc;
		client->inch = size.height_inc;
	} else
		client->incw = client->inch = 0;
	if (size.flags & PMaxSize) {
		client->maxw = size.max_width;
		client->maxh = size.max_height;
	} else
		client->maxw = client->maxh = 0;
	if (size.flags & PMinSize) {
		client->minw = size.min_width;
		client->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		client->minw = size.base_width;
		client->minh = size.base_height;
	} else
		client->minw = client->minh = 0;
	if (size.flags & PAspect) {
		client->min_a = (float)size.min_aspect.y / size.min_aspect.x;
		client->max_a = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		client->max_a = client->min_a = 0.0;
	client->isfixed = (client->maxw && client->maxh && client->maxw == client->minw && client->maxh == client->minh);
	client->hintsvalid = 1;
	return;
}

void
updatestatus(void)
{
	char text[768];
	if (!gettextprop(root, XA_WM_NAME, text, sizeof(text))) {
		strcpy(stext, "dwm-"VERSION);
		statusw = TEXTW(stext) - lrpad + 2;
		extra_status[0] = '\0';
	} else {
		char *l = strchr(text, statussep);
		if (l) {
			*l = '\0'; l++;
			strncpy(extra_status, l, sizeof(extra_status) - 1);
		} else {
			extra_status[0] = '\0';
		}

		strncpy(stext, text, sizeof(stext) - 1);
		char *s;
		char *text2;
		statusw  = 0;
		for (text2 = s = stext; *s; s++) {
			char ch;
			if ((unsigned char)(*s) < ' ') {
				ch = *s;
				*s = '\0';
				statusw += TEXTW(text2) - lrpad;
				*s = ch;
				text2 = s + 1;
			}
		}
		statusw += TEXTW(text2) - lrpad + 2;

	}
	drawbar(selmon);
	return;
}

void
updatetitle(Client *client)
{
	if (!gettextprop(client->win, netatom[NetWMName], client->name, sizeof client->name))
		gettextprop(client->win, XA_WM_NAME, client->name, sizeof client->name);
	if (client->name[0] == '\0') /* hack to mark broken clients */
		strcpy(client->name, broken);
	return;
}

void
updateicon(Client *client)
{
	freeicon(client);
	client->icon = geticonprop(client->win, &client->icon_width, &client->icon_height);
	return;
}

void
updatewindowtype(Client *client)
{
	Atom state = getatomprop(client, netatom[NetWMState]);
	Atom wtype = getatomprop(client, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(client, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		client->isfloating = 1;
	return;
}

void
updatewmhints(Client *client)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, client->win))) {
		if (client == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, client->win, wmh);
		} else {
			client->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
			if (client->isurgent)
				XSetWindowBorder(dpy, client->win, scheme[SchemeUrg][ColBorder].pixel);
		}
		if (wmh->flags & InputHint)
			client->neverfocus = !wmh->input;
		else
			client->neverfocus = 0;
		XFree(wmh);
	}
	return;
}

void
view(const Arg *arg)
{
	int i;
	uint tmptag;

	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK) {
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
		selmon->pertag->prevtag = selmon->pertag->curtag;

		if (arg->ui == ~0) {
			selmon->pertag->curtag = 0;
		} else {
			for (i = 0; !(arg->ui & 1 << i); i++);
			selmon->pertag->curtag = i + 1;
		}
	} else {
		tmptag = selmon->pertag->prevtag;
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->pertag->curtag = tmptag;
	}

	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
	selmon->layout_index = selmon->pertag->sellts[selmon->pertag->curtag];
	selmon->layout[selmon->layout_index] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->layout_index];
	selmon->layout[selmon->layout_index^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->layout_index^1];

	if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
		togglebar(NULL);

	focus(NULL);
	arrange(selmon);
	return;
}

Client *
wintoclient(Window w)
{
	Client *client;
	Monitor *m;

	for (m = monitors; m; m = m->next) {
		for (client = m->clients; client; client = client->next) {
			if (client->win == w)
				return client;
		}
	}
	return NULL;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *client;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = monitors; m; m = m->next)
		if (w == m->barwin || w == m->extrabarwin)
			return m;
	if ((client = wintoclient(w)))
		return client->monitor;
	return selmon;
}

/* Selects for the view of the focused window. The list of tags */
/* to be displayed is matched to the focused window tag list. */
void
winview(const Arg* arg) {
	(void) arg;
	Window win, win_r, win_p, *win_c;
	unsigned nc;
	int unused;
	Client* client;
	Arg a;

	if (!XGetInputFocus(dpy, &win, &unused))
		return;
	while (XQueryTree(dpy, win, &win_r, &win_p, &win_c, &nc) && win_p != win_r)
		win = win_p;

	if (!(client = wintoclient(win)))
		return;

	a.ui = client->tags;
	view(&a);
	return;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *d, XErrorEvent *ee)
{
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
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *d, XErrorEvent *ee)
{
	(void) d;
	(void) ee;
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *d, XErrorEvent *ee)
{
	(void) d;
	(void) ee;
	die("dwm: another window manager is already running");
	return -1;
}

void
xinitvisual(void)
{
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	int nitems;
	int i;

	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

	infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
	visual = NULL;
	for (i = 0; i < nitems; i ++) {
		fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
			visual = infos[i].visual;
			depth = infos[i].depth;
			cmap = XCreateColormap(dpy, root, visual, AllocNone);
			useargb = 1;
			break;
		}
	}

	XFree(infos);

	if (!visual) {
		visual = DefaultVisual(dpy, screen);
		depth = DefaultDepth(dpy, screen);
		cmap = DefaultColormap(dpy, screen);
	}
	return;
}

void
zoom(const Arg *arg)
{
	(void) arg;
	Client *client = selmon->sel;

	if (!selmon->layout[selmon->layout_index]->arrange || !client || client->isfloating)
		return;
	if (client == nexttiled(selmon->clients) && !(client = nexttiled(client->next)))
		return;
	pop(client);
	return;
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc != 1)
		die("usage: dwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	{
		xerrorxlib = XSetErrorHandler(xerrorstart);
		/* this causes an error if some other window manager is running */
		XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XSync(dpy, False);
	}
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	run();
	if (restart) {
		debug_dwm("restarting...");
		execvp(argv[0], argv);
	}
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}

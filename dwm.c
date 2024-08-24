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
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
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
enum { CursorNormal, CursorResize, CursorMove, CursorLast }; /* cursor */
enum { SchemeNorm, SchemeInv, SchemeSel, SchemeUrg }; /* color schemes */
enum { NetSupported, NetWMName, NetWMIcon, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetClientInfo, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClickTagBar, ClickLtSymbol, ClickStatusText, ClickWinTitle,
       ClickExtraBar,
       ClickClientWin, ClickRootWin, ClickLast }; /* clicks */

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
	uint icw, ich;
	int unused;
	Picture icon;
	Client *next;
	Client *snext;
	Client *allnext;
	Monitor *mon;
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
	int by;               /* bar geometry */
	int eby;              /* extra bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	uint seltags;
	uint sellt;
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
	const Layout *lt[2];
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
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void aspectresize(const Arg *arg);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void col(Monitor *);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void debug_dwm(char *message, ...);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusdir(const Arg *arg);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusnext(const Arg *arg);
static void focusstack(const Arg *arg);
static void focusurgent(const Arg *arg);
static void gaplessgrid(Monitor *m);
static Atom getatomprop(Client *c, Atom prop);
static Picture geticonprop(Window w, uint *icw, uint *ich);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static pid_t getstatusbarpid(void);
static int gettextprop(Window w, Atom atom, char *text, uint size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static int sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setclienttagprop(Client *c);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void sigstatusbar(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglebar(const Arg *arg);
static void toggleextrabar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscr(const Arg *arg);
static void togglescratch(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void freeicon(Client *c);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updateicon(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
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
static Monitor *mons, *selmon;
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

	for (Monitor *m = mons; m; m = m->next)
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
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev;

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
			ev = &(event.xbutton);
			if ((m = wintomon(ev->window)) && m != selmon) {
				unfocus(selmon->sel, 1);
				selmon = m;
				focus(NULL);
			}
			if ((c = wintoclient(ev->window)))
				focus(c);
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
applyrules(Client *c)
{
	const char *class, *instance;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (int i = 0; i < LENGTH(rules); i++) {
		const Rule *r = &rules[i];
		Monitor *m;

		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isfloating = r->isfloating;
			c->isfakefullscreen = r->isfakefullscreen;
			c->tags |= r->tags;
			if ((r->tags & SPTAGMASK) && r->isfloating) {
				c->x = c->mon->wx + (c->mon->ww / 2 - WIDTH(c) / 2);
				c->y = c->mon->wy + (c->mon->wh / 2 - HEIGHT(c) / 2);
			}

			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
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
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : (c->mon->tagset[c->mon->seltags] & (uint) ~SPTAGMASK);
	return;
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;
	if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
		if (!c->hintsvalid)
			updatesizehints(c);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->min_a > 0 && c->max_a > 0) {
			if (c->max_a < (float)*w / *h)
				*w = *h * c->max_a + 0.5;
			else if (c->min_a < (float)*h / *w)
				*h = *w * c->min_a + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	XEvent ev;
	if (m) {
		showhide(m->stack);
	} else {
		for (m = mons; m; m = m->next)
			showhide(m->stack);
	}
	if (m) {
		arrangemon(m);
		restack(m);
	} else {
		for (m = mons; m; m = m->next)
			arrangemon(m);
		XSync(dpy, False);
		while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	}
	return;
}

void
arrangemon(Monitor *m)
{
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	return;
}

void
aspectresize(const Arg *arg) {
	/* only floating windows can be moved */
	Client *c;
	c = selmon->sel;
	float ratio;
	int w, h,nw, nh;

	if (!c || !arg)
		return;
	if (selmon->lt[selmon->sellt]->arrange && !c->isfloating)
		return;

	ratio = (float)c->w / (float)c->h;
	h = arg->i;
	w = (int)(ratio * h);

	nw = c->w + w;
	nh = c->h + h;

	XRaiseWindow(dpy, c->win);
	resize(c, c->x, c->y, nw, nh, True);
	return;
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->allnext = allclients;
	c->mon->clients = c;
	allclients = c;
	return;
}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
	return;
}

void
buttonpress(XEvent *e)
{
	uint i, x, click;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClickRootWin;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
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
		debug_dwm("outsides = %d = %c\n", *s, *s);
	} else if ((c = wintoclient(ev->window))) {
		focus(c);
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
	selmon->lt[selmon->sellt] = &foo;
	for (Monitor *m = mons; m; m = m->next) {
		while (m->stack)
			unmanage(m->stack, 0);
	}
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);
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
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons) {
		mons = mons->next;
	} else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->extrabarwin);
	XDestroyWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->extrabarwin);
	free(mon);
	return;
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				      || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */
                                      && (!c->isfullscreen || c->isfakefullscreen))));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1);
	}
	return;
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
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
			for (Monitor *m = mons; m; m = m->next) {
				for (Client *c = m->clients; c; c = c->next) {
					if (c->isfullscreen && !c->isfakefullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				}
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
				XMoveResizeWindow(dpy, m->extrabarwin, m->wx, m->eby, m->ww, bh);
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
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth) {
			c->bw = ev->border_width;
		} else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2);
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2);
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else {
			configure(c);
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
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	m->pertag = ecalloc(1, sizeof(Pertag));
	m->pertag->curtag = m->pertag->prevtag = 1;

	for (i = 0; i <= LENGTH(tags); i++) {
		m->pertag->nmasters[i] = m->nmaster;
		m->pertag->mfacts[i] = m->mfact;

		m->pertag->ltidxs[i][0] = m->lt[0];
		m->pertag->ltidxs[i][1] = m->lt[1];
		m->pertag->sellts[i] = m->sellt;

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
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
	return;
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
	for (tc = &allclients; *tc && *tc != c; tc = &(*tc)->allnext);
	*tc = c->allnext;
	return;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
	return;
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons) {
		for (m = mons; m->next; m = m->next);
	} else {
		for (m = mons; m->next != selmon; m = m->next);
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

	for (Client *c = m->clients; c; c = c->next) {
		occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
		for (i = 0; i < LENGTH(tags); i++) {
			if (c->icon && c->tags & (1 << i))
				icontagclient[i] = c;
			if (!masterclientontag[i] && c->tags & (1<<i)) {
				XClassHint ch = { NULL, NULL };
				XGetClassHint(dpy, c->win, &ch);
				masterclientontag[i] = ch.res_class;
			}
		}
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) {
		Client *c = icontagclient[i];

		if (masterclientontag[i]) {
			if (c) {
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
		if (c) {
			drw_text(drw, x, 0, c->icw + lrpad/2, bh, 0, " ", urg & 1 << i);
			drw_pic(drw, x, (bh - c->ich) / 2, c->icw, c->ich, c->icon);
			x += c->icw + lrpad/2;
			tagw[i] += c->icw + lrpad/2;
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
	for (Monitor *m = mons; m; m = m->next)
		drawbar(m);
	return;
}

void
enternotify(XEvent *e)
{
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!c || c == selmon->sel) {
		return;
	}
	focus(c);
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
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, selmon->barwin, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
	return;
}

void
focusdir(const Arg *arg)
{
	Client *s = selmon->sel, *f = NULL, *c, *next;

	if (!s)
		return;

	uint score = -1;
	uint client_score;
	int dist;
	int dirweight = 20;
	int isfloating = s->isfloating;

	next = s->next;
	if (!next)
		next = s->mon->clients;
	for (c = next; c != s; c = next) {

		next = c->next;
		if (!next)
			next = s->mon->clients;

		if (!ISVISIBLE(c) || c->isfloating != isfloating) // || HIDDEN(c)
			continue;

		switch (arg->i) {
		case 0: // left
			dist = s->x - c->x - c->w;
			client_score =
				dirweight * MIN(abs(dist), abs(dist + s->mon->ww)) +
				abs(s->y - c->y);
			break;
		case 1: // right
			dist = c->x - s->x - s->w;
			client_score =
				dirweight * MIN(abs(dist), abs(dist + s->mon->ww)) +
				abs(c->y - s->y);
			break;
		case 2: // up
			dist = s->y - c->y - c->h;
			client_score =
				dirweight * MIN(abs(dist), abs(dist + s->mon->wh)) +
				abs(s->x - c->x);
			break;
		default:
		case 3: // down
			dist = c->y - s->y - s->h;
			client_score =
				dirweight * MIN(abs(dist), abs(dist + s->mon->wh)) +
				abs(c->x - s->x);
			break;
		}

		if (((arg->i == 0 || arg->i == 2) && client_score <= score) || client_score < score) {
			score = client_score;
			f = c;
		}
	}

	if (f && f != s) {
		focus(f);
		restack(f->mon);
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

	if (!mons->next)
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
	Client *c;

	m = selmon;
	c = m->sel;
	while (c == NULL && m->next) {
		m = m->next;
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
		c = m->sel;
	}
	if (c == NULL)
		return;

	if (arg->i) {
		if (c->allnext)
			c = c->allnext;
		else
			c = allclients;
	} else {
		Client *last = c;
		if (last == allclients)
			last = NULL;
		for (c = allclients; c->allnext != last; c = c->allnext);
	}
	focus(c);
	return;
}

void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
		return;
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next) {
			if (ISVISIBLE(i))
				c = i;
		}
		if (!c) {
			for (; i; i = i->next) {
				if (ISVISIBLE(i))
					c = i;
			}
		}
	}
	if (c) {
		focus(c);
		restack(selmon);
	}
	return;
}

static void
focusurgent(const Arg *arg) {
	(void) arg;
	for (Monitor *m = mons; m; m = m->next) {
		Client *c;

		for (c = m->clients; c && !c->isurgent; c = c->next);
		if (c) {
			int i;
			unfocus(selmon->sel, 0);
			selmon = m;
			for (i = 0; i < LENGTH(tags) && !((1 << i) & c->tags); i++);
			if (i < LENGTH(tags)) {
				const Arg a = {.ui = 1 << i};
				view(&a);
				focus(c);
			}
		}
	}
	return;
}

void
gaplessgrid(Monitor *m) {
	uint n, cols, rows, cn, rn, i, cx, cy, cw, ch;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
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
	for (i = 0, c = nexttiled(m->clients); c; i++, c = nexttiled(c->next)) {
		if (i/rows + 1 > cols - n%cols)
			rows = n/cols + 1;
		ch = rows ? m->wh / rows : m->wh;
		cx = m->wx + cn*cw;
		cy = m->wy + rn*ch;
		resize(c, cx, cy, cw - 2 * c->bw, ch - 2 * c->bw, False);
		rn++;
		if (rn >= rows) {
			rn = 0;
			cn++;
		}
	}
	return;
}

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
						   &da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

pid_t
getstatusbarpid(void)
{
	char buf[32], *str = buf, *c;
	FILE *fp;

	if (statuspid > 0) {
		snprintf(buf, sizeof(buf), "/proc/%u/cmdline", statuspid);
		if ((fp = fopen(buf, "r"))) {
			fgets(buf, sizeof(buf), fp);
			while ((c = strchr(str, '/')))
				str = c + 1;
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

	uint32 icw, ich;
	if (w <= h) {
		ich = ICONSIZE; icw = w * ICONSIZE / h;
		if (icw == 0)
			icw = 1;
	} else {
		icw = ICONSIZE; ich = h * ICONSIZE / w;
		if (ich == 0)
			ich = 1;
	}
	*picw = icw; *pich = ich;

	uint32 i, *bstp32 = (uint32 *)bstp;
	for (sz = w * h, i = 0; i < sz; ++i)
		bstp32[i] = prealpha(bstp[i]);

	Picture ret = drw_picture_create_resized(drw, (char *)bstp, w, h, icw, ich);
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
grabbuttons(Client *c, int focused)
{
	uint i, j;
	uint modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

	updatenumlockmask();
	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if (!focused)
		XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
			        BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
	for (i = 0; i < LENGTH(buttons); i++) {
		if (buttons[i].click == ClickClientWin) {
			for (j = 0; j < LENGTH(modifiers); j++)
				XGrabButton(dpy, buttons[i].button,
							buttons[i].mask | modifiers[j],
							c->win, False, BUTTONMASK,
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
	Client *c = selmon->clients;

	for (c = nexttiled(c->next); c; c = nexttiled(c->next), nslave++);

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
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	updateicon(c);
	updatetitle(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else {
		c->mon = selmon;
		applyrules(c);
	}

	if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
		c->x = c->mon->wx + c->mon->ww - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
		c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->wx);
	c->y = MAX(c->y, c->mon->wy);
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	{
		int format;
		unsigned long *data, n, extra;
		Monitor *m;
		Atom atom;
		if (XGetWindowProperty(dpy, c->win, netatom[NetClientInfo], 0L, 2L, False, XA_CARDINAL,
				&atom, &format, &n, &extra, (unsigned char **)&data) == Success && n == 2) {
			c->tags = *data;
			for (m = mons; m; m = m->next) {
				if (m->num == *(data+1)) {
					c->mon = m;
					break;
				}
			}
		}
		if (n > 0)
			XFree(data);
	}
	setclienttagprop(c);

	c->stored_fx = c->x;
	c->stored_fy = c->y;
	c->stored_fw = c->w;
	c->stored_fh = c->h;
	c->x = c->mon->mx + (c->mon->mw - WIDTH(c)) / 2;
	c->y = c->mon->my + (c->mon->mh - HEIGHT(c)) / 2;
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if (c->isfloating)
		XRaiseWindow(dpy, c->win);
	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;
	arrange(c->mon);
	XMapWindow(dpy, c->win);
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
	Client *c;

	for (c = m->clients; c; c = c->next) {
		if (ISVISIBLE(c))
			n++;
	}
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
	return;
}

void
motionnotify(XEvent *e)
{
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	mon = m;
	return;
}

void
movemouse(const Arg *arg)
{
	(void) arg;
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen && !c->isfakefullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
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
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c);
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
	return;
}

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
	return;
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME)) {
		updatestatus();
	} else if (ev->state == PropertyDelete) {
		return; /* ignore */
	} else if ((c = wintoclient(ev->window))) {
		switch (ev->atom) {
		default:
			break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			c->hintsvalid = 0;
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		}
		else if (ev->atom == netatom[NetWMIcon]) {
			updateicon(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
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

	for (m = mons; m; m = m->next) {
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	}
	return r;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
	return;
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;
	uint n;
	Client *nbc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;

	for (n = 0, nbc = nexttiled(selmon->clients); nbc; nbc = nexttiled(nbc->next), n++);

	if (!(c->isfloating) && selmon->lt[selmon->sellt]->arrange) {
		if (selmon->lt[selmon->sellt]->arrange == monocle || n == 1) {
			wc.border_width = 0;
			c->w = wc.width += c->bw * 2;
			c->h = wc.height += c->bw * 2;
		}
	}

	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
	return;
}

void
resizemouse(const Arg *arg)
{
	(void) arg;
	int ocx, ocy, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen && !c->isfakefullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
					 None, cursor[CursorResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
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

			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh) {
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
	return;
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;
	if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if (m->lt[m->sellt]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext) {
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
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
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	setclienttagprop(c);
	focus(NULL);
	arrange(NULL);
	return;
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		            PropModeReplace, (unsigned char *)data, 2);
	return;
}

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
	}
	sendevent(c, wmatom[WMTakeFocus]);
	return;
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		if (c->isfakefullscreen) {
			resizeclient(c, c->x, c->y, c->w, c->h);
			return;
		}
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fullscreen && c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0;
		if (c->isfakefullscreen) {
			resizeclient(c, c->x, c->y, c->w, c->h);
			return;
		}
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(c->mon);
	}
	return;
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
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

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
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
	for (Monitor *m = mons; m; m = m->next) {
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
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
	return;
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		if ((c->tags & SPTAGMASK) && c->isfloating) {
			c->x = c->mon->wx + (c->mon->ww / 2 - WIDTH(c) / 2);
			c->y = c->mon->wy + (c->mon->wh / 2 - HEIGHT(c) / 2);
		}
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating)
                && (!c->isfullscreen || c->isfakefullscreen))
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
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
setclienttagprop(Client *c)
{
	long data[] = { (long) c->tags, (long) c->mon->num };
	XChangeProperty(dpy, c->win, netatom[NetClientInfo], XA_CARDINAL, 32,
			        PropModeReplace, (unsigned char *) data, 2);
	return;
}

void
tag(const Arg *arg)
{
	Client *c;
	if (selmon->sel && arg->ui & TAGMASK) {
		c = selmon->sel;
		selmon->sel->tags = arg->ui & TAGMASK;
		setclienttagprop(c);
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	if (!selmon->sel || !mons->next)
		return;
	Monitor *mon = dirtomon(arg->i);
	if (selmon->sel->isfloating) {
		selmon->sel->x += mon->mx - selmon->mx;
		selmon->sel->y += mon->my - selmon->my;
	}
	sendmon(selmon->sel, mon);
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
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->ww * m->mfact : 0;
	else
		mw = m->ww;
	for (i = x = y = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) {
		if (i < m->nmaster) {
			w = (mw - x) / (MIN(n, m->nmaster) - i);
			resize(c, x + m->wx, m->wy, w - (2 * c->bw), m->wh - (2 * c->bw), 0);
			x += WIDTH(c);
		} else {
			h = (m->wh - y) / (n - i);
			resize(c, x + m->wx, m->wy + y, m->ww - x - (2 * c->bw), h - (2 * c->bw), 0);
			y += HEIGHT(c);
		}
	}
	return;
}

void
tile(Monitor *m)
{
	uint i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->ww * m->mfact : 0;
	else
		mw = m->ww;
	for (i = my = ty = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			h = (m->wh - my) / (MIN(n, m->nmaster) - i);
			resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), 0);
			if (my + HEIGHT(c) < m->wh)
				my += HEIGHT(c);
		} else {
			h = (m->wh - ty) / (n - i);
			resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2*c->bw), h - (2*c->bw), 0);
			if (ty + HEIGHT(c) < m->wh)
				ty += HEIGHT(c);
		}
	return;
}

void
togglebar(const Arg *arg)
{
	(void) arg;
	selmon->showbar = selmon->pertag->showbars[selmon->pertag->curtag] = !selmon->showbar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh);
	arrange(selmon);
	return;
}

void
toggleextrabar(const Arg *arg)
{
	selmon->extrabar = !selmon->extrabar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->extrabarwin, selmon->wx, selmon->eby, selmon->ww, bh);
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

    selmon->sel->x = selmon->sel->mon->mx + (selmon->sel->mon->mw - WIDTH(selmon->sel)) / 2;
    selmon->sel->y = selmon->sel->mon->my + (selmon->sel->mon->mh - HEIGHT(selmon->sel)) / 2;

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
	Client *c;
	uint found = 0;
	uint scratchtag = SPTAG(arg->ui);
	Arg sparg = {.v = scratchpads[arg->ui].cmd};

	for (c = selmon->clients; c && !(found = c->tags & scratchtag); c = c->next);
	if (found) {
		uint this_tag = c->tags & selmon->tagset[selmon->seltags];
		uint newtagset = selmon->tagset[selmon->seltags] ^ scratchtag;

		if (this_tag) {
			c->tags = scratchtag;
		} else {
			c->tags |= selmon->tagset[selmon->seltags];
		}
		if (newtagset) {
			selmon->tagset[selmon->seltags] = newtagset;
			focus(NULL);
			arrange(selmon);
		}
		if (ISVISIBLE(c)) {
			focus(c);
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
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
		selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
		selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];

		if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
			togglebar(NULL);

		focus(NULL);
		arrange(selmon);
	}
	return;
}

void
freeicon(Client *c)
{
	if (c->icon) {
		XRenderFreePicture(dpy, c->icon);
		c->icon = None;
	}
	return;
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	return;
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	XWindowChanges wc;

	detach(c);
	detachstack(c);
	freeicon(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	updateclientlist();
	arrange(m);
	return;
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
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
	for (m = mons; m; m = m->next) {
		if (!m->barwin) {
			m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, depth,
					InputOutput, visual,
					CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
			XDefineCursor(dpy, m->barwin, cursor[CursorNormal]->cursor);
			XMapRaised(dpy, m->barwin);
			XSetClassHint(dpy, m->barwin, &ch);
		}
		if (!m->extrabarwin) {
			m->extrabarwin = XCreateWindow(dpy, root, m->wx, m->eby, m->ww, bh, 0, depth,
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
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	} else {
		m->by = -bh;
	}
	if (m->extrabar) {
		m->wh -= bh;
		m->eby = !m->topbar ? m->wy : m->wy + m->wh;
		m->wy = !m->topbar ? m->wy + bh : m->wy;
	} else
		m->eby = -bh;
	return;
}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
							XA_WINDOW, 32, PropModeAppend,
							(unsigned char *) &(c->win), 1);
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
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
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
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				allclients = c->allnext;
				detachstack(c);
				c->mon = mons;
				attach(c);
				attachstack(c);
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
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
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->min_a = (float)size.min_aspect.y / size.min_aspect.x;
		c->max_a = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->max_a = c->min_a = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
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
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
	return;
}

void
updateicon(Client *c)
{
	freeicon(c);
	c->icon = geticonprop(c->win, &c->icw, &c->ich);
	return;
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
	return;
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else {
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
			if (c->isurgent)
				XSetWindowBorder(dpy, c->win, scheme[SchemeUrg][ColBorder].pixel);
		}
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
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
	selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
	selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
	selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];

	if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
		togglebar(NULL);

	focus(NULL);
	arrange(selmon);
	return;
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->win == w)
				return c;
		}
	}
	return NULL;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin || w == m->extrabarwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
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
	Client* c;
	Arg a;

	if (!XGetInputFocus(dpy, &win, &unused))
		return;
	while (XQueryTree(dpy, win, &win_r, &win_p, &win_c, &nc) && win_p != win_r)
		win = win_p;

	if (!(c = wintoclient(win)))
		return;

	a.ui = c->tags;
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
	Client *c = selmon->sel;

	if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
		return;
	if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
		return;
	pop(c);
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

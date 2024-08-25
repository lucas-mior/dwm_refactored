/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int border_pixels  = 3;        /* border pixel of windows */
static const unsigned int tabModKey = 0x40;
static const unsigned int tabCycleKey = 0x17;
static const unsigned int key_j = 44;
static const unsigned int key_semicolon = 47;
static const unsigned int key_l = 46;
static const unsigned int key_k = 45;
static const unsigned int superKey = 133;
static const int snap      = 32;       /* snap pixel */
static const int showbar            = 1;        /* 0 means no standard bar */
static const int topbar             = 1;        /* 0 means standard bar at bottom */
static const int extrabar           = 1;        /* 0 means no extra bar */
static const char statussep         = '!';      /* separator between statuses */
#define ICONSIZE 18   /* icon size */
#define ICONSPACING 5 /* space between icon and title */
static const char *fonts[]          = {
	"LiberationSans:size=14",
	"Noto Color Emoji:size=11:antialias=true:autohint=true",
	"LiterationMono Nerd Font:size=11:style=Regular",
};
static const char *colors[][3]      = {
	/*               fg         bg         border  */
	[SchemeNorm] = { "#ffffff", "#000000",   "#000000" },
	[SchemeInv]  = { "#000000", "#004400", "#000000" },
	[SchemeSel]  = { "#ffffff", "#004400", "green"   },
	[SchemeUrg]  = { "red",     "red",     "red"     },
};
static const unsigned int alphas[][3] = {
	/*               fg      bg    border */
	[SchemeNorm] = { OPAQUE, 0xbb, 0x00   },
	[SchemeSel]  = { OPAQUE, 0xbb, OPAQUE },
	[SchemeUrg]  = { OPAQUE, 0xbb, OPAQUE },
};

typedef struct {
	const char *name;
	const void *cmd;
} Sp;

const char *spcmd1[] = {"st", "-n", "python", "-e", "python", NULL};
const char *spcmd2[] = {"st", "-n", "scratch"};
static Sp scratchpads[] = {
	/* name          cmd  */
	{"python",      spcmd1},
	{"scratch",     spcmd2},
};

/* tagging */
static const char *tags[] = { "F1", "F2", "F3", "1", "2", "3" };

static const char tag_label_format[] = "%s: %s";	/* format of a tag label */
static const char tag_empty_format[] = "%s";	/* format of an empty tag */
static const char tag_label_delim[] = ":-_\n";	/* format of an empty tag */

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     switchtotag  isfloating   isfakefullscreen monitor */
	{ "firefox",  NULL,       NULL,         1 << 0,     1,           0,           1,               -1 },
	{ "LibreWolf",NULL,       NULL,         1 << 0,     1,           0,           1,               -1 },
	{ "Brave",    NULL,       NULL,         1 << 0,     1,           0,           1,               -1 },
	{ "KiCad",    NULL,       NULL,         1 << 3,     1,           0,           0,               -1 },
	{ "OMEdit",   NULL,       NULL,         1 << 3,     1,           0,           0,               -1 },
	{ NULL,       NULL,       " - mpv",     1 << 4,     1,           0,           0,               -1 },
	{ NULL,       NULL,       "ncmpcpp",    1 << 5,     1,           0,           0,               -1 },
	/* { NULL,       NULL,       "python",     ~0,         0,           1,           0,               -1 }, */
	{ NULL,       NULL,       "csv_plotter.py",~0,         0,           1,           0,               -1 },
	{ NULL,       NULL,       "clip.sh",    0,          0,           1,           0,               -1 },
	{ NULL,       NULL,       "clip1.sh",   0,          0,           1,           0,               -1 },
	{ NULL,       NULL,       "arqs.zsh",   0,          0,           1,           0,               -1 },
	{ NULL, NULL, "Plant Identification Progress",   0, 0,           1,           0,               -1 },
	{ NULL,       "python",   NULL,   SPTAG(0),   0,           0,           0,               -1 },
	{ NULL,       "scratch",   NULL,   SPTAG(1),   0,           0,           0,               -1 },
};

/* layout(s) */
static const float master_fact     = 0.50; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 1;    /* 1 means respect size hints in tiled resizals */
static const int lockfullscreen = 0; /* 1 will force focus on the fullscreen window */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
	{ "###",      gapless_grid },
	{ "|||",      col },
};

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggle_view,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggle_tag,      {.ui = 1 << TAG} },

#define STATUSBAR "dwmblocks2"

static const Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY|ShiftMask,             XK_b,      toggle_bar,      {0} },
	{ MODKEY|ControlMask,             XK_b,    toggle_extra_bar, {0} },
	{ MODKEY,                       XK_k,      focus_stack,     {.i = +1 } },
	{ MODKEY,                       XK_l,      focus_stack,     {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_u,      focus_urgent,    {0} },
	{ MODKEY,                       XK_i,      inc_number_masters,     {.i = +1 } },
	{ MODKEY,                       XK_u,      inc_number_masters,     {.i = -1 } },
	{ MODKEY,                       XK_j,      set_master_fact,       {.f = -0.05} },
	{ MODKEY,                    XK_semicolon, set_master_fact,       {.f = +0.05} },
	{ MODKEY|ControlMask,           XK_j,      aspect_resize,   {.i = -25} },
	{ MODKEY|ControlMask,        XK_semicolon, aspect_resize,   {.i = +25} },
	{ MODKEY|ShiftMask,             XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_Tab,    view,           {0} },
	{ MODKEY,                       XK_q,      kill_client,     {0} },
	{ MODKEY,                       XK_t,      set_layout,      {.v = &layouts[0]} },
	{ MODKEY|ShiftMask,             XK_f,      set_layout,      {.v = &layouts[1]} },
	{ MODKEY,                       XK_m,      set_layout,      {.v = &layouts[2]} },
	{ MODKEY|ShiftMask,             XK_g,      set_layout,      {.v = &layouts[3]} },
	{ MODKEY|ShiftMask,             XK_c,      set_layout,      {.v = &layouts[4]} },
	{ MODKEY,                       XK_space,  toggle_floating, {0} },
	{ MODKEY,                   XK_apostrophe, view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,         XK_apostrophe, tag,            {.ui = ~0 } },
	{ MODKEY|ControlMask,           XK_k,      focus_monitor,       {.i = -1 } },
	{ MODKEY|ControlMask,           XK_l,      focus_monitor,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_k,      tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_l,      tagmon,         {.i = +1 } },
	{ MODKEY,            			XK_equal,  toggle_scratch,  {.ui = 0 } },
	{ MODKEY,            			XK_Return, toggle_scratch,  {.ui = 1 } },
	TAGKEYS(                        XK_F1,                     0)
	TAGKEYS(                        XK_F2,                     1)
	TAGKEYS(                        XK_F3,                     2)
	TAGKEYS(                        XK_1,                      3)
	TAGKEYS(                        XK_2,                      4)
	TAGKEYS(                        XK_3,                      5)
	{ MODKEY|ControlMask|ShiftMask, XK_q,      quit,           {.i = 0} },
	{ MODKEY|ControlMask|ShiftMask, XK_r,      quit,           {.i = 1} },
	{ MODKEY,                       XK_g,      winview,        {0}},
	{ Mod1Mask,                     XK_Tab,    alt_tab,         {0} },
	{ 0,                            XK_F11,    toggle_fullscreen,  {0}},
};

/* button definitions */
/* click can be ClickTagBar, ClickLayoutSymbol, ClickStatusText, ClickWinTitle, ClickClientWin, or ClickRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClickLayoutSymbol,          0,              Button1,        set_layout,      {0} },
	{ ClickLayoutSymbol,          0,              Button3,        set_layout,      {.v = &layouts[2]} },
	{ ClickWinTitle,          0,              Button2,        zoom,           {0} },
	{ ClickClientWin,         MODKEY,         Button1,        move_mouse,      {0} },
	{ ClickClientWin,         MODKEY,         Button2,        toggle_floating, {0} },
	{ ClickClientWin,         MODKEY,         Button3,        resize_mouse,    {0} },
	{ ClickStatusText,        0,              Button1,        signal_status_bar,   {.i = 1} },
	{ ClickStatusText,        0,              Button2,        signal_status_bar,   {.i = 2} },
	{ ClickStatusText,        0,              Button3,        signal_status_bar,   {.i = 3} },
	{ ClickStatusText,        0,              Button4,        signal_status_bar,   {.i = 4} },
	{ ClickStatusText,        0,              Button5,        signal_status_bar,   {.i = 5} },
	{ ClickStatusText,        ShiftMask,      Button1,        signal_status_bar,   {.i = 6} },
	{ ClickStatusText,        ControlMask,    Button1,        signal_status_bar,   {.i = 7} },
	{ ClickExtraBar,        0,              Button1,        signal_status_bar,   {.i = 1} },
	{ ClickExtraBar,        0,              Button2,        signal_status_bar,   {.i = 2} },
	{ ClickExtraBar,        0,              Button3,        signal_status_bar,   {.i = 3} },
	{ ClickExtraBar,        0,              Button4,        signal_status_bar,   {.i = 4} },
	{ ClickExtraBar,        0,              Button5,        signal_status_bar,   {.i = 5} },
	{ ClickExtraBar,        ShiftMask,      Button1,        signal_status_bar,   {.i = 6} },
	{ ClickExtraBar,        ControlMask,    Button1,        signal_status_bar,   {.i = 7} },
	{ ClickTagBar,            0,              Button1,        view,           {0} },
	{ ClickTagBar,            0,              Button3,        toggle_view,     {0} },
	{ ClickTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClickTagBar,            MODKEY,         Button3,        toggle_tag,      {0} },
};


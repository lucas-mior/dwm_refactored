/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx  = 3;        /* border pixel of windows */
static const unsigned int tabModKey = 0x40;
static const unsigned int tabCycleKey = 0x17;
static const unsigned int snap      = 32;       /* snap pixel */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
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

/* tagging */
static const char *tags[] = { "Esc", "F1", "F2", "F3", "F4", "1", "2", "3", "4" };

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
	{ "KiCad",    NULL,       NULL,         1 << 6,     1,           0,           0,               -1 },
	{ "OMEdit",   NULL,       NULL,         1 << 6,     1,           0,           0,               -1 },
	{ NULL,       NULL,       " - mpv",     1 << 7,     1,           0,           0,               -1 },
	{ NULL,       NULL,       "ncmpcpp",    1 << 8,     1,           0,           0,               -1 },
	{ NULL,       NULL,       "python",     ~0,         0,           1,           0,               -1 },
	{ NULL,       NULL,       "clip.sh",    0,          0,           1,           0,               -1 },
	{ NULL,       NULL,       "clip1.sh",   0,          0,           1,           0,               -1 },
	{ NULL,       NULL,       "arqs.zsh",   0,          0,           1,           0,               -1 },
	{ NULL,       NULL,       "Figure 1",   0,          0,           1,           0,               -1 },
};

/* layout(s) */
static const float mfact     = 0.50; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 1;    /* 1 means respect size hints in tiled resizals */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
	{ "###",      gaplessgrid },
	{ "|||",      col },
};

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

#define STATUSBAR "dwmblocks2"

static const Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY|ShiftMask,             XK_b,      togglebar,      {0} },
	{ MODKEY,                       XK_k,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_l,      focusstack,     {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_u,      focusurgent,    {0} },
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_u,      incnmaster,     {.i = -1 } },
	{ MODKEY,                       XK_j,      setmfact,       {.f = -0.05} },
	{ MODKEY,                    XK_semicolon, setmfact,       {.f = +0.05} },
	{ MODKEY|ControlMask,           XK_j,      aspectresize,   {.i = -25} },
	{ MODKEY|ControlMask,        XK_semicolon, aspectresize,   {.i = +25} },
	{ MODKEY|ShiftMask,             XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_Tab,    view,           {0} },
	{ MODKEY,                       XK_q,      killclient,     {0} },
	{ MODKEY,                       XK_t,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                       XK_f,      setlayout,      {.v = &layouts[1]} },
	{ MODKEY,                       XK_m,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY|ShiftMask,             XK_g,      setlayout,      {.v = &layouts[3]} },
	{ MODKEY|ShiftMask,             XK_c,      setlayout,      {.v = &layouts[4]} },
	{ MODKEY,                       XK_space,  togglefloating, {0} },
	{ MODKEY,                   XK_apostrophe, view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,         XK_apostrophe, tag,            {.ui = ~0 } },
	{ MODKEY|ControlMask,           XK_k,      focusmon,       {.i = -1 } },
	{ MODKEY|ControlMask,           XK_l,      focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_k,      tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_l,      tagmon,         {.i = +1 } },
	TAGKEYS(                        XK_Escape,                 0)
	TAGKEYS(                        XK_F1,                     1)
	TAGKEYS(                        XK_F2,                     2)
	TAGKEYS(                        XK_F3,                     3)
	TAGKEYS(                        XK_F4,                     4)
	TAGKEYS(                        XK_1,                      5)
	TAGKEYS(                        XK_2,                      6)
	TAGKEYS(                        XK_3,                      7)
	TAGKEYS(                        XK_4,                      8)
	{ MODKEY|ControlMask|ShiftMask, XK_q,      quit,           {.i = 0} },
	{ MODKEY|ControlMask|ShiftMask, XK_r,      quit,           {.i = 1} },
	{ MODKEY,                       XK_g,      winview,        {0}},
	{ Mod1Mask,                     XK_Tab,    alttab,         {0} },
	{ 0,                            XK_F11,    togglefullscr,  {0}},
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
	{ ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} },
	{ ClkWinTitle,          0,              Button2,        zoom,           {0} },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkStatusText,        0,              Button1,        sigstatusbar,   {.i = 1} },
	{ ClkStatusText,        0,              Button2,        sigstatusbar,   {.i = 2} },
	{ ClkStatusText,        0,              Button3,        sigstatusbar,   {.i = 3} },
	{ ClkStatusText,        0,              Button4,        sigstatusbar,   {.i = 4} },
	{ ClkStatusText,        0,              Button5,        sigstatusbar,   {.i = 5} },
	{ ClkStatusText,        ShiftMask,      Button1,        sigstatusbar,   {.i = 6} },
	{ ClkStatusText,        ControlMask,    Button1,        sigstatusbar,   {.i = 7} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};


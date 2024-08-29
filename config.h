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
static const int SNAP_PIXELS       = 32;
static const bool show_top_bar      = 1;   /* 0 means no top bar */
static const bool show_bottom_bar   = 1;   /* 0 means no bottom bar */
static const char status_separator = -1;  /* separator between statuses */
#define ICONSIZE 22   /* icon size */
#define ICONSPACING 5 /* space between icon and title */
static const char *fonts[]          = {
	"LiberationSans:size=16",
	"Noto Color Emoji:size=11:antialias=true:autohint=true",
	"LiterationMono Nerd Font:size=12:style=Regular",
};
static const char *colors[][3]      = {
	/*               fg         bg         border  */
	[SchemeNormal] = { "#ffffff", "#000000",   "#000000" },
	[SchemeInverse]  = { "#000000", "#004400", "#000000" },
	[SchemeSelected]  = { "#ffffff", "#004400", "green"   },
	[SchemeUrgent]  = { "red",     "red",     "red"     },
};
#define OPAQUE 0xffU
static const unsigned int alphas[][3] = {
	/*               fg      bg    border */
	[SchemeNormal] = { OPAQUE, 0xbb, 0x00   },
	[SchemeSelected]  = { OPAQUE, 0xbb, OPAQUE },
	[SchemeUrgent]  = { OPAQUE, 0xbb, OPAQUE },
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
	/* { NULL,       NULL,       "python",     (uint)~0,         0,           1,           0,               -1 }, */
	{ NULL,       NULL,       "csv_plotter.py",(uint)~0,         0,           1,           0,               -1 },
	{ NULL,       NULL,       "clip.sh",    0,          0,           1,           0,               -1 },
	{ NULL,       NULL,       "clip1.sh",   0,          0,           1,           0,               -1 },
	{ NULL,       NULL,       "arqs.zsh",   0,          0,           1,           0,               -1 },
	{ NULL, NULL, "Plant Identification Progress",   0, 0,           1,           0,               -1 },
};

/* layout(s) */
static const float master_fact  = 0.50;
static const bool resizehints    = true; /* 1 means respect size hints in tiled resizals */
static const bool lockfullscreen = false; /* 1 will force focus on the fullscreen window */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      monitor_layout_tile },
	{ "><>",      NULL },
	{ "[M]",      monitor_layout_monocle },
	{ "###",      monitor_layout_grid },
	{ "|||",      monitor_layout_columns },
};

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      user_view_tag,       {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      user_toggle_view,    {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      user_tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      user_toggle_tag,     {.ui = 1 << TAG} },

#define STATUSBAR "dwmblocks2"

static const Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY|ShiftMask,             XK_b,      user_toggle_bar,               {BarTop} },
	{ MODKEY|ControlMask,           XK_b,      user_toggle_bar,               {BarBottom} },
	{ MODKEY,                       XK_k,      user_focus_stack,              {.i = +1 } },
	{ MODKEY,                       XK_l,      user_focus_stack,              {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_u,      user_focus_urgent,             {0} },
	{ MODKEY,                       XK_i,      user_increment_number_masters, {.i = +1 } },
	{ MODKEY,                       XK_u,      user_increment_number_masters, {.i = -1 } },
	{ MODKEY,                       XK_j,      user_set_master_fact,          {.f = -0.05f} },
	{ MODKEY,                    XK_semicolon, user_set_master_fact,          {.f = +0.05f} },
	{ MODKEY|ControlMask,           XK_j,      user_aspect_resize,            {.i = -25} },
	{ MODKEY|ControlMask,        XK_semicolon, user_aspect_resize,            {.i = +25} },
	{ MODKEY|ShiftMask,             XK_Return, user_promote_to_master,        {0} },
	{ MODKEY,                       XK_Tab,    user_view_tag,                 {0} },
	{ MODKEY,                       XK_q,      user_kill_client,              {0} },
	{ MODKEY,                       XK_t,      user_set_layout,               {.v = &layouts[0]} },
	{ MODKEY|ShiftMask,             XK_f,      user_set_layout,               {.v = &layouts[1]} },
	{ MODKEY,                       XK_m,      user_set_layout,               {.v = &layouts[2]} },
	{ MODKEY|ShiftMask,             XK_g,      user_set_layout,               {.v = &layouts[3]} },
	{ MODKEY|ShiftMask,             XK_c,      user_set_layout,               {.v = &layouts[4]} },
	{ MODKEY,                       XK_space,  user_toggle_floating,          {0} },
	{ MODKEY,                   XK_apostrophe, user_view_tag,                 {.ui = (uint) ~0 } },
	{ MODKEY|ShiftMask,         XK_apostrophe, user_tag,                      {.ui = (uint) ~0 } },
	{ MODKEY|ControlMask,           XK_k,      user_focus_monitor,            {.i = -1 } },
	{ MODKEY|ControlMask,           XK_l,      user_focus_monitor,            {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_k,      user_tag_monitor,              {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_l,      user_tag_monitor,              {.i = +1 } },
	TAGKEYS(                        XK_F1,                         0)
	TAGKEYS(                        XK_F2,                         1)
	TAGKEYS(                        XK_F3,                         2)
	TAGKEYS(                        XK_1,                          3)
	TAGKEYS(                        XK_2,                          4)
	TAGKEYS(                        XK_3,                          5)
	{ MODKEY|ControlMask|ShiftMask, XK_q,      user_quit_dwm,                 {.i = 0} },
	{ MODKEY|ControlMask|ShiftMask, XK_r,      user_quit_dwm,                 {.i = 1} },
	{ MODKEY,                       XK_g,      user_window_view,              {0}},
	{ Mod1Mask,                     XK_Tab,    user_alt_tab,                  {0} },
	{ 0,                            XK_F11,    user_toggle_fullscreen,        {0}},
};

/* button definitions */
/* click can be ClickBarTags, ClickBarLayoutSymbol, ClickBarStatus, ClickBarTitle, ClickClientWin, or ClickRootWin */
static const Button buttons[] = {
	/* click                event mask     button   function                argument */
	{ ClickBarTags,         0,           Button1, user_view_tag,          {0} },
	{ ClickBarTags,         0,           Button3, user_toggle_view,       {0} },
	{ ClickBarTags,         MODKEY,      Button1, user_tag,               {0} },
	{ ClickBarTags,         MODKEY,      Button3, user_toggle_tag,        {0} },
	{ ClickBarLayoutSymbol, 0,           Button1, user_set_layout,        {0} },
	{ ClickBarLayoutSymbol, 0,           Button3, user_set_layout,        {.v = &layouts[2]} },
	{ ClickBarTitle,        0,           Button2, user_promote_to_master, {0} },
	{ ClickClientWin,       MODKEY,      Button1, user_mouse_move,        {0} },
	{ ClickClientWin,       MODKEY,      Button2, user_toggle_floating,   {0} },
	{ ClickClientWin,       MODKEY,      Button3, user_mouse_resize,      {0} },
	{ ClickBarStatus,       0,           Button1, user_signal_status_bar, {.i = 1} },
	{ ClickBarStatus,       0,           Button2, user_signal_status_bar, {.i = 2} },
	{ ClickBarStatus,       0,           Button3, user_signal_status_bar, {.i = 3} },
	{ ClickBarStatus,       0,           Button4, user_signal_status_bar, {.i = 4} },
	{ ClickBarStatus,       0,           Button5, user_signal_status_bar, {.i = 5} },
	{ ClickBarStatus,       ShiftMask,   Button1, user_signal_status_bar, {.i = 6} },
	{ ClickBarStatus,       ControlMask, Button1, user_signal_status_bar, {.i = 7} },
	{ ClickBottomBar,       0,           Button1, user_signal_status_bar, {.i = 1} },
	{ ClickBottomBar,       0,           Button2, user_signal_status_bar, {.i = 2} },
	{ ClickBottomBar,       0,           Button3, user_signal_status_bar, {.i = 3} },
	{ ClickBottomBar,       0,           Button4, user_signal_status_bar, {.i = 4} },
	{ ClickBottomBar,       0,           Button5, user_signal_status_bar, {.i = 5} },
	{ ClickBottomBar,       ShiftMask,   Button1, user_signal_status_bar, {.i = 6} },
	{ ClickBottomBar,       ControlMask, Button1, user_signal_status_bar, {.i = 7} },
};


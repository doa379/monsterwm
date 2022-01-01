/* see LICENSE for copyright and license */

#ifndef CONFIG_H
#define CONFIG_H

/** modifiers **/
#define MOD1            Mod1Mask    /* ALT key */
#define MOD4            Mod4Mask    /* Super/Windows key */
#define CTRL            ControlMask /* Control key */
#define SHIFT           ShiftMask   /* Shift key */

/** generic settings **/
#define MASTER_SIZE     0.55

#define ATTACH_ASIDE    True      /* False means new window is master */
#define FOLLOW_WINDOW   False     /* follow the window when moved to a different desktop */
#define FOLLOW_MOUSE    False     /* focus the window the mouse just entered */
#define CLICK_TO_FOCUS  True      /* focus an unfocused window when clicked  */
#define FOCUS_BUTTON    Button1   /* mouse button to be used along with CLICK_TO_FOCUS */
#define BORDER_WIDTH    2         /* window border width */
#define FOCUS           "#ff950e" /* focused window border color    */
#define UNFOCUS         "#444444" /* unfocused window border color  */
#define MINWSZ          50        /* minimum window size in pixels  */
#define DEFAULT_DESKTOP 0         /* the desktop to focus initially */
#define DESKTOPS        4         /* number of desktops - edit DESKTOPCHANGE keys to suit */
#define STATUSFILE      "/tmp/status"

/**
 * open applications to specified desktop with specified mode.
 * if desktop is negative, then current is assumed
 */
static const Rule rules[] = { \
    /*  class     mon desktop  follow */
    { "MPlayer",  0,   3,    True },
    { "Gimp",     0,   0,    False },
};

/* helper for spawning shell commands */
#define SHCMD(cmd) {.com = (const char*[]){ "/bin/sh", "-c", cmd, NULL }}

/**
 * custom commands
 * must always end with ', NULL };'
 */
static const char *termcmd[] = { "term",     NULL };
static const char *menucmd[] = { "dmenu_run", NULL };

#define DESKTOPCHANGE(K,N) \
    {  MOD4,             K,              change_desktop, {.i = N}}, \
    {  MOD4|ShiftMask,   K,              client_to_desktop, {.i = N}},

#define MONITORCHANGE(K,N) \
    {  MOD4,             K,              change_monitor, {.i = N}}, \
    {  MOD4|ShiftMask,   K,              client_to_monitor, {.i = N}},

/**
 * keyboard shortcuts
 */
static Key keys[] = {
    /* modifier          key            function           argument */
    {  MOD4,             XK_BackSpace,  focusurgent,       { NULL }},
    {  MOD4|SHIFT,       XK_c,          killclient,        { NULL }},
    {  MOD4,             XK_Tab,        next_win,          { NULL }},
    {  MOD4|SHIFT,       XK_Tab,        prev_win,          { NULL }},
    {  MOD4,             XK_space,      status,            { NULL }},
    {  MOD4,             XK_h,          resize_master,     { .i = -10 }}, /* decrease size in px */
    {  MOD4,             XK_l,          resize_master,     { .i = +10 }}, /* increase size in px */
    {  MOD4,             XK_j,          resize_stack,      { .i = -10 }}, /* shrink   size in px */
    {  MOD4,             XK_k,          resize_stack,      { .i = +10 }}, /* grow     size in px */
    {  MOD4,             XK_equal,      rotate,            { .i = -1 }},
    {  MOD4,             XK_minus,      rotate,            { .i = +1 }},
    {  MOD4,             XK_o,          move_down,         { NULL }},
    {  MOD4,             XK_p,          move_up,           { NULL }},
    {  MOD4,             XK_bracketleft, rotate_filled,    { .i = -1 }},
    {  MOD4,             XK_bracketright, rotate_filled,   { .i = +1 }},
    {  MOD4,             XK_grave,      last_desktop,      { NULL }},
    {  MOD4,             XK_Return,     swap_master,       { NULL }},
    {  MOD4,             XK_t,          setlayout,         { .i = TILE }},
    {  MOD4,             XK_m,          setlayout,         { .i = MONOCLE }},
    {  MOD4,             XK_b,          setlayout,         { .i = BSTACK }},
    {  MOD4,             XK_g,          setlayout,         { .i = GRID }},
    {  MOD4|SHIFT,       XK_r,          quit,              { .i = 0 }}, /* quit with exit value 0 */
    {  MOD4|SHIFT,       XK_q,          quit,              { .i = 1 }}, /* quit with exit value 1 */
    {  MOD4,             XK_Return,     spawn,             { .com = termcmd }},
    {  MOD4,             XK_Escape,     spawn,             { .com = menucmd }},
    {  MOD4|CTRL,        XK_j,          moveresize,        { .v = (int []){   0,  25,   0,   0 } }}, /* move down  */
    {  MOD4|CTRL,        XK_k,          moveresize,        { .v = (int []){   0, -25,   0,   0 } }}, /* move up    */
    {  MOD4|CTRL,        XK_l,          moveresize,        { .v = (int []){  25,   0,   0,   0 } }}, /* move right */
    {  MOD4|CTRL,        XK_h,          moveresize,        { .v = (int []){ -25,   0,   0,   0 } }}, /* move left  */
    {  MOD4|CTRL|SHIFT,  XK_j,          moveresize,        { .v = (int []){   0,   0,   0,  25 } }}, /* height grow   */
    {  MOD4|CTRL|SHIFT,  XK_k,          moveresize,        { .v = (int []){   0,   0,   0, -25 } }}, /* height shrink */
    {  MOD4|CTRL|SHIFT,  XK_l,          moveresize,        { .v = (int []){   0,   0,  25,   0 } }}, /* width grow    */
    {  MOD4|CTRL|SHIFT,  XK_h,          moveresize,        { .v = (int []){   0,   0, -25,   0 } }}, /* width shrink  */
       DESKTOPCHANGE(    XK_1,                             0)
       DESKTOPCHANGE(    XK_2,                             1)
       DESKTOPCHANGE(    XK_3,                             2)
       DESKTOPCHANGE(    XK_4,                             3)
       MONITORCHANGE(    XK_F1,                            0)
       MONITORCHANGE(    XK_F2,                            1)
};

/**
 * mouse shortcuts
 */
static Button buttons[] = {
    {  0,       Button2,     mousemotion,   {.i = MOVE}},
    {  CTRL,    Button3,     mousemotion,   {.i = RESIZE}},
    {  MOD4,    Button3,     spawn,         {.com = menucmd}},
};
#endif

/* vim: set expandtab ts=4 sts=4 sw=4 : */

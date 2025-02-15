/* see license for copyright and license */

#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include "dbus.h"

#define LENGTH(x)             (sizeof(x) / sizeof(*x))
#define CLEANMASK(mask)       (mask & ~(numlockmask | LockMask))
#define BUTTONMASK            ButtonPressMask | ButtonReleaseMask
#define ISIMM(c)              (c->isfixed || c->istrans)
#define ROOTMASK              SubstructureRedirectMask | ButtonPressMask | SubstructureNotifyMask | PropertyChangeMask
#define NOTIFY(body, urg, to) notify_send("mwm", body, urg, to)

enum { QUIT, RESTART };
enum { RESIZE, MOVE };
enum { MONOCLE, TILE, BSTACK, GRID, MODES };
enum { WM_PROTOCOLS, WM_DELETE_WINDOW, WM_COUNT };
enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_ACTIVE, 
        NET_WMNAME, NET_WTYPE, NET_NOTIF, NET_UTIL, NET_COUNT };

typedef union {
  const char **cmd;
  const int i;
  const void *v;
} Arg;

typedef struct {
  unsigned int mod;
  KeySym keysym;
  void (*func)(const Arg *);
  const Arg arg;
} Key;

typedef struct {
  unsigned int mask, button;
  void (*func)(const Arg *);
  const Arg arg;
} Button;

typedef struct {
  const char *class;
  const int monitor;
  const int desktop;
  const Bool follow;
} Rule;

static void change_desktop(const Arg *);
static void change_monitor(const Arg *);
static void client_to_desktop(const Arg *);
static void client_to_monitor(const Arg *);
static void focusurgent();
static void killclient();
static void last_desktop();
static void move_down();
static void move_up();
static void moveresize(const Arg *);
static void mousemotion(const Arg *);
static void next_win();
static void prev_win();
static void quit(const Arg *);
static void resize_master(const Arg *);
static void resize_stack(const Arg *);
static void rotate(const Arg *);
static void rotate_filled(const Arg *);
static void spawn(const Arg *);
static void swap_master();
static void status();
static void togglefixed();
static void setlayout(const Arg *);
static void setfloating();
static void to_client(const Arg *);

#include "config.h"

typedef struct Client {
  struct Client *next;
  Bool isurgn, ismono, isfull, istrans, isfixed;
  Window win;
  int x, y, w, h;
  char NAME[64];
} Client;

typedef struct {
  int mode, masz, sasz;
  Client *head, *curr, *prev;
} Desktop;

typedef struct Monitor {
  int x, y, h, w, currdeskidx, prevdeskidx;
  Desktop desktops[DESKTOPS];
} Monitor;

static Client *addwindow(Window, Desktop *);
static void buttonpress(XEvent *);
static void cleanup();
static void clientmessage(XEvent *);
static void configurerequest(XEvent *);
static void deletewindow(Window);
static void destroynotify(XEvent *);
static void enternotify(XEvent *);
static void focus(Client *, Desktop *, Monitor *);
static void focusin(XEvent *);
static unsigned long getcolor(const char *, const int);
static void grabbuttons(Client *);
static void grabkeys(void);
static void grid(int, int, int, int, const Desktop *);
static void keypress(XEvent *);
static void maprequest(XEvent *);
static void maprequest_window(Monitor *, Desktop *, Client *, Window, const XWindowAttributes *);
static void monocle(int, int, int, int, const Desktop *);
static Client *prevclient(Client *, Desktop *);
static void propertynotify(XEvent *);
static void removeclient(Client *, Desktop *, Monitor *);
static void run(void);
static void setfullscreen(Client *, Monitor *, Bool);
static void setup(void);
static void sigchld(int);
static void stack(int, int, int, int, const Desktop *);
static void unmapnotify(XEvent *);
static Bool wintoclient(Window, Client **, Desktop **, Monitor **);
static int xerror(Display *, XErrorEvent *);
static int xerrorstart(Display *, XErrorEvent *);
static void desktopinfo(const Monitor *);
static void clientinfo(const Monitor *);
static void coverfree(Client *, Desktop *, Monitor *);
static void covercenter(Client *, Monitor *);
static void clientname(Client *);
static void arrange(Desktop *, Monitor *, const int);
static void listclients(Desktop *);

static Bool running = True;
static int nmons, currmonidx, retval;
static unsigned int numlockmask, win_focus, win_unfocus, win_infocus;
static Display *dpy;
static Window root;
static Atom wmatoms[WM_COUNT], netatoms[NET_COUNT];
static Monitor *mons;

static void (*events[LASTEvent])(XEvent *) = {
  [KeyPress]         = keypress,     [EnterNotify]    = enternotify,
  [MapRequest]       = maprequest,   [ClientMessage]  = clientmessage,
  [ButtonPress]      = buttonpress,  [DestroyNotify]  = destroynotify,
  [UnmapNotify]      = unmapnotify,  [PropertyNotify] = propertynotify,
  [ConfigureRequest] = configurerequest, [FocusIn] = focusin,
};

static void (*layout[MODES])(int, int, int, int, const Desktop *) = {
  [TILE] = stack, [BSTACK] = stack, [GRID] = grid, [MONOCLE] = monocle,
};

/**
 * add the given window to the given desktop
 *
 * create a new client to hold the new window
 *
 * if there is no head at the given desktop
 * add the window as the head
 * otherwise if ATTACH_ASIDE is not set,
 * add the window as the last client
 * otherwise add the window as head
 */
Client *addwindow(Window w, Desktop *d) {
  Client *c = NULL, *t = prevclient(d->head, d);
  if (!(c = (Client *) calloc(1, sizeof *c)))
    err(EXIT_FAILURE, "cannot allocate client");

  if (!d->head)
    d->head = c;
  else if (!ATTACH_ASIDE) {
    c->next = d->head; 
    d->head = c;
  } else if (t) 
    t->next = c;
  else 
    d->head->next = c;

  XSelectInput(dpy, (c->win = w), PropertyChangeMask | FocusChangeMask | (FOLLOW_MOUSE ? EnterWindowMask : 0));
  return c;
}

/**
 * on the press of a key binding (see grabkeys)
 * call the appropriate handler
 */
void buttonpress(XEvent *e) {
  Monitor *m = NULL; Desktop *d = NULL; Client *c = NULL;
  Bool w = wintoclient(e->xbutton.window, &c, &d, &m);
  int cm = 0; 
  while (m != &mons[cm] && cm < nmons)
    ++cm;

  if (w && CLICK_TO_FOCUS && e->xbutton.button == FOCUS_BUTTON && (c != d->curr || cm != currmonidx)) {
    if (cm != currmonidx)
      change_monitor(&(Arg){ .i = cm });
    focus(c, d, m);
  }

  for (unsigned int i = 0; i < LENGTH(buttons); i++)
    if (CLEANMASK(buttons[i].mask) == CLEANMASK(e->xbutton.state) &&
          buttons[i].func && buttons[i].button == e->xbutton.button) {
      if (w && cm != currmonidx)
        change_monitor(&(Arg){ .i = cm });
      if (w && c != d->curr)
        focus(c, d, m);
      buttons[i].func(&(buttons[i].arg));
    }
}

/**
 * focus another desktop
 *
 * to avoid flickering (esp. monocle mode):
 * first map the new windows
 * first the current window and then all other
 * then unmap the old windows
 * first all others then the current
 */
void change_desktop(const Arg *arg) {
  Monitor *m = &mons[currmonidx];
  if (arg->i == m->currdeskidx + 1 || arg->i < 0 || arg->i > DESKTOPS)
    return;
  Desktop *d = &m->desktops[(m->prevdeskidx = m->currdeskidx)], *n = &m->desktops[(m->currdeskidx = arg->i - 1)];
  if (n->curr)
    XMapWindow(dpy, n->curr->win);
  for (Client *c = n->head; c; c = c->next)
    XMapWindow(dpy, c->win);
  XChangeWindowAttributes(dpy, root, CWEventMask, &(XSetWindowAttributes){ .do_not_propagate_mask = SubstructureNotifyMask });
  for (Client *c = d->head; c; c = c->next)
    if (c != d->curr)
      XUnmapWindow(dpy, c->win);
  if (d->curr)
    XUnmapWindow(dpy, d->curr->win);
  XChangeWindowAttributes(dpy, root, CWEventMask, &(XSetWindowAttributes){ .event_mask = ROOTMASK });
  if (n->head)
    focus(n->curr, n, m);
  desktopinfo(m);
}

/**
 * focus another monitor
 */
void change_monitor(const Arg *arg) {
  if (arg->i == currmonidx || arg->i < 0 || arg->i >= nmons)
    return;
  Monitor *m = &mons[currmonidx], *n = &mons[(currmonidx = arg->i)];
  focus(m->desktops[m->currdeskidx].curr, &m->desktops[m->currdeskidx], m);
  focus(n->desktops[n->currdeskidx].curr, &n->desktops[n->currdeskidx], n);
  desktopinfo(m);
}

void cleanup(void) {
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  if (retval == QUIT) {
    Window root_return, parent_return, *children;
    unsigned int nchildren;
    XQueryTree(dpy, root, &root_return, &parent_return, &children, &nchildren);
    for (unsigned int i = 0; i < nchildren; i++) 
      deletewindow(children[i]);
    
    if (children)
      XFree(children);
  } else if (retval == RESTART) {
      ;
  }

  XSync(dpy, False);
  free(mons);
}

/**
 * move the current focused client to another desktop
 *
 * add the current client as the last on the new desktop
 * then remove it from the current desktop
 */
void client_to_desktop(const Arg *arg) {
  Monitor *m = &mons[currmonidx];
  Desktop *d = &m->desktops[m->currdeskidx], *n = NULL;
  if (arg->i == m->currdeskidx + 1 || arg->i < 0 || arg->i > DESKTOPS || !d->curr)
    return;

  Client *c = d->curr, *p = prevclient(d->curr, d),
    *l = prevclient(m->desktops[arg->i].head, (n = &m->desktops[arg->i - 1]));
  /* unlink current client from current desktop */
  if (d->head == c || !p) 
    d->head = c->next;
  else 
    p->next = c->next;
  c->next = NULL;
  XChangeWindowAttributes(dpy, root, CWEventMask, &(XSetWindowAttributes){ .do_not_propagate_mask = SubstructureNotifyMask });
  if (XUnmapWindow(dpy, c->win))
    focus(d->prev, d, m);
  XChangeWindowAttributes(dpy, root, CWEventMask, &(XSetWindowAttributes){ .event_mask = ROOTMASK });
  /* link client to new desktop and make it the current */
  focus(l ? (l->next = c) : n->head ? (n->head->next = c) : (n->head = c), n, m);
  if (FOLLOW_WINDOW)
    change_desktop(arg);
}

/**
 * move the current focused client to another monitor
 *
 * add the current client as the last on the new monitor's current desktop
 * then remove it from the current monitor's current desktop
 *
 * removing the client means unlinking it and unmapping it.
 * add the client means linking it as the last client, and
 * mapping it. mapping must happen after the client has been
 * unmapped from the current monitor's current desktop.
 */
void client_to_monitor(const Arg *arg) {
  Monitor *cm = &mons[currmonidx], *nm = NULL;
  Desktop *cd = &cm->desktops[cm->currdeskidx], *nd = NULL;
  if (arg->i == currmonidx || arg->i < 0 || arg->i >= nmons || !cd->curr)
    return;

  nd = &mons[arg->i].desktops[(nm = &mons[arg->i])->currdeskidx];
  Client *c = cd->curr, *p = prevclient(c, cd), *l = prevclient(nd->head, nd);
  /* unlink current client from current monitor's current desktop */
  if (cd->head == c || !p)
    cd->head = c->next;
  else 
    p->next = c->next;
  c->next = NULL;
  focus(cd->prev, cd, cm);
  /* link to new monitor's current desktop */
  focus(l ? (l->next = c) : nd->head ? (nd->head->next = c) : (nd->head = c), nd, nm);
  change_monitor(arg);
  desktopinfo(nm);
}

/**
 * receive and process client messages
 *
 * check if window wants to change its state to fullscreen,
 * or if the window want to become active/focused
 *
 * to change the state of a mapped window, a client MUST
 * send a _NET_WM_STATE client message to the root window
 * message_type must be _NET_WM_STATE
 *   data.l[0] is the action to be taken
 *   data.l[1] is the property to alter three actions:
 *   - remove/unset _NET_WM_STATE_REMOVE=0
 *   - add/set _NET_WM_STATE_ADD=1,
 *   - toggle _NET_WM_STATE_TOGGLE=2
 *
 * to request to become active, a client should send a
 * message of _NET_ACTIVE_WINDOW type. when such a message
 * is received and a client holding that window exists,
 * the window becomes the current active focused window
 * on its desktop.
 */
void clientmessage(XEvent *e) {
  Monitor *m = NULL; Desktop *d = NULL; Client *c = NULL;
  if (!wintoclient(e->xclient.window, &c, &d, &m))
    return;

  if (e->xclient.message_type == netatoms[NET_WM_STATE] &&
        ((unsigned) e->xclient.data.l[1] == netatoms[NET_FULLSCREEN]
          || (unsigned) e->xclient.data.l[2] == netatoms[NET_FULLSCREEN])) {
    setfullscreen(c, m, (e->xclient.data.l[0] == 1 || (e->xclient.data.l[0] == 2 && !c->isfull)));
  } else if (e->xclient.message_type == netatoms[NET_ACTIVE])
      focus(c, d, m);
}

/**
 * configure a window's size, position, border width, and stacking order.
 *
 * windows usually have a prefered size (width, height) and position (x, y),
 * and sometimes borer with (border_width) and stacking order (above, detail).
 * a configure request attempts to reconfigure those properties for a window.
 *
 * we don't really care about those values, because a tiling wm will impose
 * its own values for those properties.
 * however the requested values must be set initially for some windows,
 * otherwise the window will misbehave or even crash (see gedit, geany, gvim).
 *
 * some windows depend on the number of columns and rows to set their
 * size, and not on pixels (terminals, consoles, some editors etc).
 * normally those clients when tiled and respecting the prefered size
 * will create gaps around them (window_hints).
 * however, clients are tiled to match the wm's prefered size,
 * not respecting those prefered values.
 *
 * some windows implement window manager functions themselves.
 * that is windows explicitly steal focus, or manage subwindows,
 * or move windows around w/o the window manager's help, etc..
 * to disallow this behavior, we 'tile()' the desktop to which
 * the window that sent the configure request belongs.
 */
void configurerequest(XEvent *e) {
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges wc = { ev->x, ev->y, ev->width, ev->height, ev->border_width, ev->above, ev->detail };
  if (XConfigureWindow(dpy, ev->window, ev->value_mask, &wc))
    XSync(dpy, False);
}

/**
 * clients receiving a WM_DELETE_WINDOW message should behave as if
 * the user selected "delete window" from a hypothetical menu and
 * also perform any confirmation dialog with the user.
 */
void deletewindow(Window w) {
  XEvent ev = { .type = ClientMessage };
  ev.xclient.window = w;
  ev.xclient.format = 32;
  ev.xclient.message_type = wmatoms[WM_PROTOCOLS];
  ev.xclient.data.l[0]    = wmatoms[WM_DELETE_WINDOW];
  ev.xclient.data.l[1]    = CurrentTime;
  XSendEvent(dpy, w, False, NoEventMask, &ev);
}

void destroynotify(XEvent *e) {
  Monitor *m = NULL; Desktop *d = NULL; Client *c = NULL;
  if (wintoclient(e->xdestroywindow.window, &c, &d, &m))
    removeclient(c, d, m);
}

/**
 * when the mouse enters a window's borders, that window,
 * if has set notifications of such events (EnterWindowMask)
 * will notify that the pointer entered its region
 * and will get focus if FOLLOW_MOUSE is set in the config.
 */
void enternotify(XEvent *e) {
  Monitor *m = NULL;
  Desktop *d = NULL;
  Client *c = NULL, *p = NULL;
  if (!FOLLOW_MOUSE || (e->xcrossing.mode != NotifyNormal && e->xcrossing.detail == NotifyInferior)
      || !wintoclient(e->xcrossing.window, &c, &d, &m) || e->xcrossing.window == d->curr->win)
    return;

  if (m != &mons[currmonidx]) 
    for (int cm = 0; cm < nmons; cm++)
      if (m == &mons[cm]) 
        change_monitor(&(Arg){ .i = cm });

  if ((p = d->prev))
    XChangeWindowAttributes(dpy, p->win, CWEventMask, &(XSetWindowAttributes){ .do_not_propagate_mask = EnterWindowMask });
  focus(c, d, m);
  if (p)
    XChangeWindowAttributes(dpy, p->win, CWEventMask, &(XSetWindowAttributes){ .event_mask = EnterWindowMask });
}

void focus(Client *c, Desktop *d, Monitor *m) {
  if (!d->head || !c) {
    XDeleteProperty(dpy, root, netatoms[NET_ACTIVE]);
    d->curr = d->prev = NULL;
    return;
  } else if (d->prev == c && d->curr != c->next)
    d->prev = prevclient((d->curr = c), d); 
  else if (d->curr != c) { 
    d->prev = d->curr; 
    d->curr = c;
  }
  
  for (c = d->head; c; c = c->next) {
    XSetWindowBorder(dpy, c->win, (c != d->curr) ? win_unfocus : (m == &mons[currmonidx]) ? win_focus : win_infocus);
    XSetWindowBorderWidth(dpy, c->win, c->isfull || c->ismono ? 0 : BORDER_WIDTH);
    if (CLICK_TO_FOCUS || c == d->curr) 
      grabbuttons(c);
  }
  
  XRaiseWindow(dpy, d->curr->win);
  XSetInputFocus(dpy, d->curr->win, RevertToPointerRoot, CurrentTime);
  XChangeProperty(dpy, root, netatoms[NET_ACTIVE], XA_WINDOW, 32, PropModeReplace, (unsigned char *) &d->curr->win, 1);
  XSync(dpy, False);
}

/**
 * dont give focus to any client except current.
 * some apps explicitly call XSetInputFocus (see
 * tabbed, chromium), resulting in loss of input
 * focuse (mouse/kbd) from the current focused
 * client.
 *
 * this gives focus back to the current selected
 * client, by the user, through the wm.
 */
void focusin(XEvent *e) {
  Monitor *m = &mons[currmonidx]; Desktop *d = &m->desktops[m->currdeskidx];
  if (d->curr && d->curr->win != e->xfocus.window)
    focus(d->curr, d, m);
}

/**
 * find and focus the first client that received an urgent hint
 * first look in the current desktop then on other desktops
 */
void focusurgent(void) {
  Monitor *m = &mons[currmonidx];
  Client *c = NULL;
  int d = -1;
  for (c = m->desktops[m->currdeskidx].head; c && !c->isurgn; c = c->next);
  while (!c && d < DESKTOPS - 1) 
    for (c = m->desktops[++d].head; c && !c->isurgn; c = c->next);

  if (c) { 
    if (d > -1)
      change_desktop(&(Arg){ .i = d });
    
    focus(c, &m->desktops[m->currdeskidx], m);
  }
}

/**
 * get a pixel with the requested color to
 * fill some window area (such as borders)
 */
unsigned long getcolor(const char* color, const int screen) {
  XColor c; Colormap map = DefaultColormap(dpy, screen);
  if (!XAllocNamedColor(dpy, map, color, &c, &c))
    err(EXIT_FAILURE, "cannot allocate color");
  return c.pixel;
}

/**
 * register button bindings to be notified of
 * when they occur.
 * the wm listens to those button bindings and
 * calls an appropriate handler when a binding
 * occurs (see buttonpress).
 */
void grabbuttons(Client *c) {
  Monitor *cm = &mons[currmonidx];
  unsigned int b, m, modifiers[] = { 0, LockMask, numlockmask, numlockmask | LockMask };

  for (m = 0; CLICK_TO_FOCUS && m < LENGTH(modifiers); m++)
    if (c != cm->desktops[cm->currdeskidx].curr) XGrabButton(dpy, FOCUS_BUTTON, modifiers[m],
        c->win, False, BUTTONMASK, GrabModeAsync, GrabModeAsync, None, None);
    else
      XUngrabButton(dpy, FOCUS_BUTTON, modifiers[m], c->win);

  for (b = 0, m = 0; b < LENGTH(buttons); b++, m = 0) 
    while (m < LENGTH(modifiers))
      XGrabButton(dpy, buttons[b].button, buttons[b].mask|modifiers[m++], c->win,
        False, BUTTONMASK, GrabModeAsync, GrabModeAsync, None, None);
}

/**
 * register key bindings to be notified of
 * when they occur.
 * the wm listens to those key bindings and
 * calls an appropriate handler when a binding
 * occurs (see keypressed).
 */
void grabkeys(void) {
  KeyCode code;
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  unsigned int k, m, modifiers[] = { 0, LockMask, numlockmask, numlockmask | LockMask };
  for (k = 0, m = 0; k < LENGTH(keys); k++, m = 0)
    while ((code = XKeysymToKeycode(dpy, keys[k].keysym)) && m < LENGTH(modifiers))
      XGrabKey(dpy, code, keys[k].mod|modifiers[m++], root, True, GrabModeAsync, GrabModeAsync);
}

/**
 * grid mode / grid layout
 * arrange windows in a grid aka fair
 */
void grid(int x, int y, int w, int h, const Desktop *d) {
  int n = 0, cols = 0, cn = 0, rn = 0, i = -1;
  for (Client *c = d->head; c; c = c->next) {
    if (!ISIMM(c)) 
      ++n;
    if (c->ismono && !ISIMM(c)) {
      XMoveResizeWindow(dpy, c->win, x, y, w - 2 * BORDER_WIDTH, h - 2 * BORDER_WIDTH);
      XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
      c->ismono = False;
    }
  }

  for (cols = 0; cols <= n / 2; cols++)
    if (cols * cols >= n)
      break; /* emulate square root */
  if (n == 0) 
    return; 
  else if (n == 5) 
    cols = 2;

  int rows = n / cols, ch = h - BORDER_WIDTH, cw = (w - BORDER_WIDTH) / (cols ? cols : 1);
  for (Client *c = d->head; c; c = c->next) {
    if (ISIMM(c))
      continue; 
    else 
      ++i;
    if (i / rows + 1 > cols - n%cols)
      rows = n / cols + 1;
    XMoveResizeWindow(dpy, c->win, c->x = x + cn * cw, c->y = y + rn * ch / rows, 
      c->w = cw - BORDER_WIDTH, c->h = ch / rows - BORDER_WIDTH);
    if (++rn >= rows) { 
      rn = 0; 
      cn++;
    }
  }
}

/**
 * on the press of a key binding (see grabkeys)
 * call the appropriate handler
 */
void keypress(XEvent *e) {
  KeySym keysym = XkbKeycodeToKeysym(dpy, e->xkey.keycode, 0, 0);
  for (unsigned int i = 0; i < LENGTH(keys); i++)
    if (keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(e->xkey.state))
      if (keys[i].func) 
        keys[i].func(&keys[i].arg);
}

/**
 * explicitly kill the current client - close the highlighted window
 * if the client accepts WM_DELETE_WINDOW requests send a delete message
 * otherwise forcefully kill and remove the client
 */
void killclient(void) {
  Monitor *m = &mons[currmonidx];
  Desktop *d = &m->desktops[m->currdeskidx];
  if (!d->curr)
    return;

  Atom *prot = NULL;
  int n = -1;
  if (XGetWMProtocols(dpy, d->curr->win, &prot, &n))
    while (--n >= 0 && prot[n] != wmatoms[WM_DELETE_WINDOW]);
  if (n < 0) { 
    XKillClient(dpy, d->curr->win);
    removeclient(d->curr, d, m);
  } else
      deletewindow(d->curr->win);
  
  if (prot)
    XFree(prot);
}

void last_desktop(void) {
  change_desktop(&(Arg){ .i = mons[currmonidx].prevdeskidx + 1 });
}

void maprequest(XEvent *e) {
  Window w = e->xmaprequest.window;
  XWindowAttributes wa = { 0 };
  Monitor *m = NULL;
  Desktop *d = NULL;
  Client *c = NULL;
  if (wintoclient(w, &c, &d, &m) || (XGetWindowAttributes(dpy, w, &wa) && wa.override_redirect))
    return;

  maprequest_window(m, d, c, w, &wa);
}

void maprequest_window(Monitor *m, Desktop *d, Client *c, Window w, const XWindowAttributes *wa) {
  XClassHint ch = { 0, 0 };
  Bool follow = False;
  int newmon = currmonidx, newdsk = mons[currmonidx].currdeskidx;
  if (XGetClassHint(dpy, w, &ch))
    for (unsigned int i = 0; i < LENGTH(rules); i++)
      if (strstr(ch.res_class, rules[i].class) || strstr(ch.res_name, rules[i].class)) {
        if (rules[i].monitor >= 0 && rules[i].monitor < nmons)
          newmon = rules[i].monitor;
        if (rules[i].desktop >= 0 && rules[i].desktop < DESKTOPS)
          newdsk = rules[i].desktop;
        follow = rules[i].follow;
        break;
      }

  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);

  c = addwindow(w, (d = &(m = &mons[newmon])->desktops[newdsk]));
  c->istrans = XGetTransientForHint(dpy, c->win, &w);
  c->w = wa->width;
  c->h = wa->height;
  
  if (m->currdeskidx == newdsk)
    XMapWindow(dpy, c->win);
  if (follow) { 
    change_monitor(&(Arg) { .i = newmon });
    change_desktop(&(Arg) { .i = newdsk });
  }

  int i;
  unsigned long l;
  unsigned char *state = NULL;
  Atom a;
  if (XGetWindowProperty(dpy, c->win, netatoms[NET_WM_STATE], 0L, sizeof a, False, XA_ATOM, &a, &i, &l, &l, &state) == Success && state)
    setfullscreen(c, m, (*(Atom *) state == netatoms[NET_FULLSCREEN]));

  Bool isnotif = False;
  if (XGetWindowProperty(dpy, c->win, netatoms[NET_WTYPE], 0L, sizeof a, False, XA_ATOM, &a, &i, &l, &l, &state) == Success && state)
    if (*(Atom *) state == netatoms[NET_NOTIF] || *(Atom *) state == netatoms[NET_UTIL])
      isnotif = True;

  if (state)
    XFree(state);

  coverfree(c, d, m);
  if (isnotif)
    covercenter(c, m);

  clientname(c);
  focus(c, d, m);
}

void mousemotion(const Arg *arg) {
  Monitor *m = &mons[currmonidx];
  Desktop *d = &m->desktops[m->currdeskidx];
  XWindowAttributes wa;
  XEvent ev;
  Client *c = d->curr;
  if (!c || !XGetWindowAttributes(dpy, c->win, &wa))
    return;
  else if (arg->i == RESIZE)
    XWarpPointer(dpy, c->win, c->win, 0, 0, 0, 0, --wa.width, --wa.height);
    
  int rx, ry, co, xw, yh;
  unsigned int v; Window w;
  if (!XQueryPointer(dpy, root, &w, &w, &rx, &ry, &co, &co, &v) || w != c->win)
    return;

  if (XGrabPointer(dpy, root, False, BUTTONMASK | PointerMotionMask, GrabModeAsync,
        GrabModeAsync, None, None, CurrentTime) != GrabSuccess)
    return;

  do {
    XMaskEvent(dpy, BUTTONMASK | PointerMotionMask | SubstructureRedirectMask, &ev);
    if (ev.type == MotionNotify) {
      xw = (arg->i == MOVE ? wa.x : wa.width)  + ev.xmotion.x - rx;
      yh = (arg->i == MOVE ? wa.y : wa.height) + ev.xmotion.y - ry;
      if (arg->i == RESIZE)
        XResizeWindow(dpy, c->win, xw > MINWSZ ? (c->w = xw) : (c->w = wa.width), 
            yh > MINWSZ ? (c->h = yh) : (c->h = wa.height));
      else if (arg->i == MOVE)
        XMoveWindow(dpy, c->win, c->x = xw, c->y = yh);
    } else if (ev.type == ConfigureRequest || ev.type == MapRequest)
        events[ev.type](&ev);
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);
}

void monocle(int x, int y, int w, int h, const Desktop *d) {
  Client *c = d->curr;
  if (!c || c->istrans || c->isfull)
    return;
  else if (!c->ismono) {
    XMoveResizeWindow(dpy, c->win, x, y, w, h);
    XSetWindowBorderWidth(dpy, c->win, 0);
    c->ismono = True;
  } else {
    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
    c->ismono = False;
  }
}
/**
 * swap positions of current and next from current clients
 */
void move_down(void) {
  Desktop *d = &mons[currmonidx].desktops[mons[currmonidx].currdeskidx];
  if (!d->curr || !d->head->next)
    return;
  /* p is previous, c is current, n is next, if current is head n is last */
  Client *p = prevclient(d->curr, d), *n = d->curr->next ? d->curr->next : d->head;
  /*
   * if c is head, swapping with n should update head to n
   * [c]->[n]->..  ==>  [n]->[c]->..
   *  ^head              ^head
   *
   * else there is a previous client and p->next should be what's after c
   * ..->[p]->[c]->[n]->..  ==>  ..->[p]->[n]->[c]->..
   */
  if (d->curr == d->head)
    d->head = n; 
  else 
    p->next = d->curr->next;
  /*
   * if c is the last client, c will be the current head
   * [n]->..->[p]->[c]->NULL  ==>  [c]->[n]->..->[p]->NULL
   *  ^head                         ^head
   * else c will take the place of n, so c-next will be n->next
   * ..->[p]->[c]->[n]->..  ==>  ..->[p]->[n]->[c]->..
   */
  d->curr->next = d->curr->next ? n->next : n;
  /*
   * if c was swapped with n then they now point to the same ->next. n->next should be c
   * ..->[p]->[c]->[n]->..  ==>  ..->[p]->[n]->..  ==>  ..->[p]->[n]->[c]->..
   *                                        [c]-^
   *
   * else c is the last client and n is head,
   * so c will be move to be head, no need to update n->next
   * [n]->..->[p]->[c]->NULL  ==>  [c]->[n]->..->[p]->NULL
   *  ^head                         ^head
   */
  if (d->curr->next == n->next)
    n->next = d->curr;
  else
    d->head = d->curr;
}

/**
 * swap positions of current and previous from current clients
 */
void move_up(void) {
  Desktop *d = &mons[currmonidx].desktops[mons[currmonidx].currdeskidx];
  if (!d->curr || !d->head->next)
    return;
  /* p is previous from current or last if current is head */
  Client *pp = NULL, *p = prevclient(d->curr, d);
  /* pp is previous from p, or null if current is head and thus p is last */
  if (p->next) 
    for (pp = d->head; pp && pp->next != p; pp = pp->next);
  /*
   * if p has a previous client then the next client should be current (current is c)
   * ..->[pp]->[p]->[c]->..  ==>  ..->[pp]->[c]->[p]->..
   *
   * if p doesn't have a previous client, then p might be head, so head must change to c
   * [p]->[c]->..  ==>  [c]->[p]->..
   *  ^head              ^head
   * if p is not head, then c is head (and p is last), so the new head is next of c
   * [c]->[n]->..->[p]->NULL  ==>  [n]->..->[p]->[c]->NULL
   *  ^head         ^last           ^head         ^last
   */
  if (pp) 
    pp->next = d->curr; 
  else 
    d->head = d->curr == d->head ? d->curr->next : d->curr;
  /*
   * next of p should be next of c
   * ..->[pp]->[p]->[c]->[n]->..  ==>  ..->[pp]->[c]->[p]->[n]->..
   * except if c was head (now c->next is head), so next of p should be c
   * [c]->[n]->..->[p]->NULL  ==>  [n]->..->[p]->[c]->NULL
   *  ^head         ^last           ^head         ^last
   */
  p->next = d->curr->next == d->head ? d->curr : d->curr->next;
  /*
   * next of c should be p
   * ..->[pp]->[p]->[c]->[n]->..  ==>  ..->[pp]->[c]->[p]->[n]->..
   * except if c was head (now c->next is head), so c is must be last
   * [c]->[n]->..->[p]->NULL  ==>  [n]->..->[p]->[c]->NULL
   *  ^head         ^last           ^head         ^last
   */
  d->curr->next = d->curr->next == d->head ? NULL : p;
}

/**
 * move and resize a window with the keyboard
 */
void moveresize(const Arg *arg) {
  Monitor *m = &mons[currmonidx]; Desktop *d = &m->desktops[m->currdeskidx];
  XWindowAttributes wa;
  Client *c = d->curr;
  if (!c || !XGetWindowAttributes(dpy, c->win, &wa))
    return;
  if (!c->istrans)
    focus(c, d, m); 
  XRaiseWindow(dpy, c->win);
  XMoveResizeWindow(dpy, c->win, c->x = wa.x + ((int *) arg->v)[0], c->y = wa.y + ((int *) arg->v)[1],
      c->w = wa.width + ((int *) arg->v)[2], c->h = wa.height + ((int *) arg->v)[3]);
}

void next_win(void) {
  Desktop *d = &mons[currmonidx].desktops[mons[currmonidx].currdeskidx];
  if (d->curr && d->head->next)
    focus(d->curr->next ? d->curr->next : d->head, d, &mons[currmonidx]);
  listclients(d);
}

Client *prevclient(Client *c, Desktop *d) {
  Client *p = NULL;
  if (c && d->head && d->head->next)
    for (p = d->head; p->next && p->next != c; p = p->next);
  return p;
}

void prev_win(void) {
  Desktop *d = &mons[currmonidx].desktops[mons[currmonidx].currdeskidx];
  if (d->curr && d->head->next)
    focus(prevclient(d->curr, d), d, &mons[currmonidx]);
  listclients(d);
}

/**
 * set unrgent hint for a window
 */
void propertynotify(XEvent *e) {
  Monitor *m = NULL; Desktop *d = NULL; Client *c = NULL;
  if (e->xproperty.atom != XA_WM_HINTS || !wintoclient(e->xproperty.window, &c, &d, &m))
    return;

  XWMHints *wmh = XGetWMHints(dpy, c->win);
  Desktop *cd = &mons[currmonidx].desktops[mons[currmonidx].currdeskidx];
  c->isurgn = (c != cd->curr && wmh && (wmh->flags & XUrgencyHint));
  if (wmh)
    XFree(wmh);

  clientinfo(m);
}

void quit(const Arg *arg) {
  retval = arg->i;
  running = False;
}

void removeclient(Client *c, Desktop *d, Monitor *m) {
  Client **p = NULL;
  for (p = &d->head; *p && (*p != c); p = &(*p)->next);
  if (!*p) 
    return;
  else 
    *p = c->next;
  if (c == d->prev && !(d->prev = prevclient(d->curr, d)))
    d->prev = d->head;
  if (c == d->curr || (d->head && !d->head->next))
    focus(d->prev, d, m);
  free(c);
}

void resize_master(const Arg *arg) {
  Monitor *m = &mons[currmonidx];
  Desktop *d = &m->desktops[m->currdeskidx];
  int msz = (d->mode == BSTACK ? m->h : m->w) * MASTER_SIZE + (d->masz += arg->i);
  if (msz >= MINWSZ && (d->mode == BSTACK ? m->h : m->w) - msz >= MINWSZ) 
    arrange(d, m, TILE);
  else
    d->masz -= arg->i; /* reset master area size */
}

void resize_stack(const Arg *arg) {
  mons[currmonidx].desktops[mons[currmonidx].currdeskidx].sasz += arg->i;
  arrange(&mons[currmonidx].desktops[mons[currmonidx].currdeskidx], &mons[currmonidx], TILE);
}

void rotate(const Arg *arg) {
  change_desktop(&(Arg){ .i = (DESKTOPS + mons[currmonidx].currdeskidx + arg->i) % DESKTOPS + 1 });
}

void rotate_filled(const Arg *arg) {
  Monitor *m = &mons[currmonidx];
  int n = arg->i;
  while (n < DESKTOPS && !m->desktops[(DESKTOPS + m->currdeskidx + n) % DESKTOPS].head)
    n += arg->i;
  change_desktop(&(Arg){ .i = (DESKTOPS + m->currdeskidx + n) % DESKTOPS + 1 });
}

/**
 * main event loop
 * on receival of an event call the appropriate handler
 */
void run(void) {
  XEvent ev;
  while (running && !XNextEvent(dpy, &ev)) 
    if (events[ev.type])
      events[ev.type](&ev);
}

void setfullscreen(Client *c, Monitor *m, Bool fullscrn) {
  if (fullscrn != c->isfull)
    XChangeProperty(dpy, c->win, netatoms[NET_WM_STATE], XA_ATOM, 32, PropModeReplace, 
      (unsigned char *) ((c->isfull = fullscrn) ? &netatoms[NET_FULLSCREEN] : 0), fullscrn);
  if (fullscrn)
    XMoveResizeWindow(dpy, c->win, m->x, m->y, m->w, m->h);
  else
    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);

  XSetWindowBorderWidth(dpy, c->win, c->isfull || c->ismono ? 0 : BORDER_WIDTH);
}

void setup(void) {
  sigchld(0);
  /* screen and root window */
  const int screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  /* initialize monitors and desktops */
  XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nmons);
  if (!nmons || !info)
    errx(EXIT_FAILURE, "Xinerama is not active");
  if (!(mons = calloc(nmons, sizeof(Monitor))))
    err(EXIT_FAILURE, "cannot allocate mons");

  for (int m = 0; m < nmons; m++) {
    mons[m] = (Monitor) {
      .x = info[m].x_org, 
      .y = info[m].y_org,
      .w = info[m].width, 
      .h = info[m].height
    };

    for (unsigned int d = 0; d < DESKTOPS; d++)
      mons[m].desktops[d] = (Desktop) { .mode = 0 };
  }

  XFree(info);
  /* get color for focused and unfocused client borders */
  win_focus = getcolor(FOCUS, screen);
  win_unfocus = getcolor(UNFOCUS, screen);
  win_infocus = getcolor(FOCUS, screen);
  /* set numlockmask */
  XModifierKeymap *modmap = XGetModifierMapping(dpy);
  for (int k = 0; k < 8; k++) 
    for (int j = 0; j < modmap->max_keypermod; j++)
      if (modmap->modifiermap[modmap->max_keypermod*k + j] == XKeysymToKeycode(dpy, XK_Num_Lock))
        numlockmask = (1 << k);
  
  XFreeModifiermap(modmap);
  /* set up atoms for dialog/notification windows */
  wmatoms[WM_PROTOCOLS]     = XInternAtom(dpy, "WM_PROTOCOLS",     False);
  wmatoms[WM_DELETE_WINDOW] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  netatoms[NET_SUPPORTED]   = XInternAtom(dpy, "_NET_SUPPORTED",   False);
  netatoms[NET_WM_STATE]    = XInternAtom(dpy, "_NET_WM_STATE",    False);
  netatoms[NET_ACTIVE]      = XInternAtom(dpy, "_NET_ACTIVE_WINDOW",       False);
  netatoms[NET_FULLSCREEN]  = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  netatoms[NET_WMNAME]      = XInternAtom(dpy, "_NET_WM_NAME", False);
  netatoms[NET_WTYPE]       = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", True);
  netatoms[NET_NOTIF]       = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", True);
  netatoms[NET_UTIL]        = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", True);
  /* propagate EWMH support */
  XChangeProperty(dpy, root, netatoms[NET_SUPPORTED], XA_ATOM, 32, PropModeReplace, (unsigned char *) netatoms, NET_COUNT);
  XSetErrorHandler(xerrorstart);
  /* set masks for reporting events handled by the wm */
  XSelectInput(dpy, root, ROOTMASK);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XSync(dpy, False);
  grabkeys();

  Window root_return, parent_return, *children;
  unsigned int nchildren;
  XQueryTree(dpy, root, &root_return, &parent_return, &children, &nchildren);
  for (unsigned int i = 0; i < nchildren; i++) {
    XWindowAttributes wa = { 0 };
    XGetWindowAttributes(dpy, children[i], &wa);
    if (wa.map_state == IsViewable)
      maprequest_window(NULL, NULL, NULL, children[i], &wa);
  }
  
  if (children)
    XFree(children);
}

void sigchld(__attribute__((unused)) int sig) {
  if (signal(SIGCHLD, sigchld) != SIG_ERR)
    while (0 < waitpid(-1, NULL, WNOHANG));
  else 
    err(EXIT_FAILURE, "cannot install SIGCHLD handler");
}

void spawn(const Arg *arg) {
  if (fork())
    return;
  if (dpy) 
    close(ConnectionNumber(dpy));
  setsid();
  execvp((char *) arg->cmd[0], (char **) arg->cmd);
  err(EXIT_SUCCESS, "execvp %s", (char *) arg->cmd[0]);
}

void stack(int x, int y, int w, int h, const Desktop *d) {
  Client *c = NULL, *t = NULL; Bool b = ( d->mode == BSTACK );
  int n = 0, p = 0, z = (b ? w : h), ma = (b ? h : w) * MASTER_SIZE + d->masz;
  /* count stack windows and grab first non-floating, non-fullscreen window */
  for (t = d->head; t; t = t->next)
    if (!ISIMM(t)) { 
      if (c)
        ++n; 
      else 
        c = t;
    
      if (t->ismono && !ISIMM(t)) {
        XMoveResizeWindow(dpy, c->win, x, y, w - 2 * BORDER_WIDTH, h - 2 * BORDER_WIDTH);
        XSetWindowBorderWidth(dpy, t->win, BORDER_WIDTH);
        t->ismono = False;
      }
    }
  /* if there is only one window (c && !n), it should cover the available screen space
   * if there is only one stack window, then we don't care about growth
   * if more than one stack windows (n > 1) adjustments may be needed.
   *
   *   - p is the num of pixels than remain when spliting the
   *       available width/height to the number of windows
   *   - z is each client's height/width
   *
   *      ----------  --.    ----------------------.
   *      |   |----| }--|--> sasz                  }--> first client will have
   *      |   | 1s |    |                          |    z+p+sasz height/width.
   *      | M |----|-.  }--> screen height (h)  ---'
   *      |   | 2s | }--|--> client height (z)    two stack clients on tile mode
   *      -----------' -'                         ::: ascii art by c00kiemon5ter
   *
   * what we do is, remove the sasz from the screen height/width and then
   * divide that space with the windows on the stack so all windows have
   * equal height/width: z = (z - sasz)/n
   *
   * sasz was left out (subtrackted), to later be added to the first client
   * height/width. before we do that, there will be cases when the num of
   * windows cannot be perfectly divided with the available screen height/width.
   * for example: 100px scr. height, and 3 stack windows: 100/3 = 33,3333..
   * so we get that remaining space and merge it to sasz: p = (z - sasz) % n + sasz
   *
   * in the end, we know each client's height/width (z), and how many pixels
   * should be added to the first stack client (p) so that it satisfies sasz,
   * and also, does not result in gaps created on the bottom of the screen.
   */
  if (c && !n)
    XMoveResizeWindow(dpy, c->win, x, y, w - 2 * BORDER_WIDTH, h - 2 * BORDER_WIDTH);
  if (!c || !n) 
    return;
  else if (n > 1) {
    p = (z - d->sasz) % n + d->sasz;
    z = (z - d->sasz) / n;
  }
  /* tile the first non-floating, non-fullscreen window to cover the master area */
  if (b)
    XMoveResizeWindow(dpy, c->win, c->x = x, c->y = y, c->w = w - 2 * BORDER_WIDTH, c->h = ma - BORDER_WIDTH);
  else
    XMoveResizeWindow(dpy, c->win, c->x = x, c->y = y, c->w = ma - BORDER_WIDTH, c->h = h - 2 * BORDER_WIDTH);
  /* tile the next non-floating, non-fullscreen (and first) stack window adding p */
  for (c = c->next; c && ISIMM(c); c = c->next);
  int cw = (b ? h : w) - 2 * BORDER_WIDTH - ma, ch = z - BORDER_WIDTH;
  if (b)
    XMoveResizeWindow(dpy, c->win, c->x = x, c->y = y += ma, c->h = ch - BORDER_WIDTH + p, c->w = cw);
  else
    XMoveResizeWindow(dpy, c->win, c->x = x += ma, c->y = y, c->w = cw, c->h = ch - BORDER_WIDTH + p);
  /* tile the rest of the non-floating, non-fullscreen stack windows */
  for (b ? (x += ch + p) : (y += ch + p), c = c->next; c; c = c->next) {
    if (ISIMM(c))
      continue;
    if (b) { 
      XMoveResizeWindow(dpy, c->win, c->x = x, c->y = y, c->h = ch, c->w = cw); 
      x += z;
    } else {
      XMoveResizeWindow(dpy, c->win, c->x = x, c->y = y, c->w = cw, c->h = ch);
      y += z;
    }
  }
}

void swap_master(void) {
  Monitor *m = &mons[currmonidx];
  Desktop *d = &m->desktops[mons[currmonidx].currdeskidx];
  if (!d->curr || !d->head->next)
    return;
  if (d->curr == d->head)
    move_down();
  else 
    while (d->curr != d->head)
      move_up();

  arrange(d, m, TILE);
  focus(d->head, d, &mons[currmonidx]);
}

/**
 * windows that request to unmap should lose their client
 * so invisible windows do not exist on screen
 */
void unmapnotify(XEvent *e) {
  Monitor *m = NULL; Desktop *d = NULL; Client *c = NULL;
  if (wintoclient(e->xunmap.window, &c, &d, &m))
    removeclient(c, d, m);
}

/**
 * find to which client and desktop the given window belongs to
 */
Bool wintoclient(Window w, Client **c, Desktop **d, Monitor **m) {
  for (int cm = 0; cm < nmons && !*c; cm++)
    for (int cd = 0; cd < DESKTOPS && !*c; cd++)
      for (*m = &mons[cm], *d = &(*m)->desktops[cd], *c = (*d)->head; *c && (*c)->win != w; *c = (*c)->next);
      
  return (*c != NULL);
}

/**
 * There's no way to check accesses to destroyed windows,
 * thus those cases are ignored (especially on UnmapNotify's).
 */
int xerror(__attribute__((unused)) Display *dpy, XErrorEvent *ee) {
  if ((ee->error_code == BadAccess   && (ee->request_code == X_GrabKey
          ||  ee->request_code == X_GrabButton))
      || (ee->error_code  == BadMatch    && (ee->request_code == X_SetInputFocus
          ||  ee->request_code == X_ConfigureWindow))
      || (ee->error_code  == BadDrawable && (ee->request_code == X_PolyFillRectangle
          || ee->request_code == X_CopyArea  ||  ee->request_code == X_PolySegment
          ||  ee->request_code == X_PolyText8))
      || ee->error_code   == BadWindow)
    return 0;
  err(EXIT_FAILURE, "xerror: request: %d code: %d", ee->request_code, ee->error_code);
}

/**
 * error handler function to display an appropriate error message
 * when the window manager initializes (see setup - XSetErrorHandler)
 */
int xerrorstart(__attribute__((unused)) Display *dpy, __attribute__((unused)) XErrorEvent *ee) {
  errx(EXIT_FAILURE, "xerror: another window manager is already running");
}

int main(int ARGC, char *ARGV[]) {
  if (ARGC == 2 && !strncmp(ARGV[1], "-v", 3))
    errx(EXIT_SUCCESS, "version %s", VERSION);
  else if (ARGC != 1) 
    errx(EXIT_FAILURE, "usage: man monsterwm");
  if (!(dpy = XOpenDisplay(NULL)))
    errx(EXIT_FAILURE, "cannot open display");
  setup();
  NOTIFY("WM init", 2, 1000);
  run();
  cleanup();
  NOTIFY("WM deinit", 2, 1000);
  XCloseDisplay(dpy);
  return retval;
}

void desktopinfo(const Monitor *m) {
  char STR[1024];
  snprintf(STR, sizeof STR - 1, "%d", m->currdeskidx + 1);
  NOTIFY(STR, 2, 500);
}

void clientinfo(const Monitor *m) {
  const Desktop *d = &m->desktops[m->currdeskidx];
  for (Client *c = d->head; c; c = c->next) {
    unsigned urg = c->isurgn ? 2 : 1;
    if (c == d->curr)
      NOTIFY(c->NAME, urg, 500);
  }
}

void status(void) {
  char STR[1024] = "status info";
  FILE *fp = fopen(STATUSFILE, "r");
  if (fp) {
    fgets(STR, sizeof STR - 1, fp);
    fclose(fp);
  }

  NOTIFY(STR, 1, 1000);
}

void togglefixed(void) {
  Monitor *m = &mons[currmonidx];
  Desktop *d = &m->desktops[m->currdeskidx];
  Client *c = d->curr;
  c->isfixed = !c->isfixed;
  char STR[1024];
  snprintf(STR, sizeof STR - 1, "%s %s", c->NAME, c->isfixed ? "immutable" : "mutable");
  NOTIFY(STR, 1, 1000);
}

void clientname(Client *c) {
  XTextProperty name;
  c->NAME[0] = '\0';
  if ((XGetTextProperty(dpy, c->win, &name, netatoms[NET_WMNAME]) ||
        XGetTextProperty(dpy, c->win, &name, XA_WM_NAME))
      && name.nitems) {
    char **list = NULL;
    int n;
    if (name.encoding == XA_STRING)
      strncpy(c->NAME, (char *) name.value, sizeof c->NAME - 1);
    else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(c->NAME, *list, sizeof c->NAME - 1);
			XFreeStringList(list);
		}

    c->NAME[sizeof c->NAME - 1] = '\0';
    XFree(name.value);
  }
}

void coverfree(Client *c, Desktop *d, Monitor *m) {
  int x = 0, y = 0, minh = 0, ww = c->w, wh = c->h;
  if (d->head->next) {
    Client *c = d->head;
    for (; c->next->next; c = c->next)
      minh = !minh || c->h < minh ? c->h : minh;

    x = c->w + c->x;
    if (x + ww > m->w) {
      x = 0;
      y = !minh || c->h < minh ? c->h : minh;
    }
    
    y += c->y;
    if (y + wh > m->h)
      x = y = 0;
  }

  c->x = x;
  c->y = y;
  XMoveWindow(dpy, c->win, c->x, c->y);
}

void covercenter(Client *c, Monitor *m) {
  c->x = m->w / 2 - c->w / 2;
  c->y = m->h / 2 - c->h / 2;
  XMoveWindow(dpy, c->win, c->x, c->y);
}

void setlayout(const Arg *arg) {
  Desktop *d = &mons[currmonidx].desktops[mons[currmonidx].currdeskidx];
  arrange(d, &mons[currmonidx], arg->i);
  focus(d->curr, d, &mons[currmonidx]);
}

void arrange(Desktop *d, Monitor *m, const int mode) {
  d->mode = mode;
  layout[mode](m->x, m->y, m->w, m->h, d);
}

void setfloating(void)
{
  Desktop *d = &mons[currmonidx].desktops[mons[currmonidx].currdeskidx];
  Client *c = d->curr;
  if (c && !c->istrans) {
    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
    c->ismono = False;
  }
}

void listclients(Desktop *d) {
  char STR[1024] = { 0 };
  unsigned n = 1;
  for (Client *c = d->head; c; c = c->next, n++) {
    char C[128];
    if (c->isfixed)
      snprintf(C, sizeof C - 1, "%d: %c[%s]\n", n, c == d->curr ? '*' : ' ', c->NAME);
    else
      snprintf(C, sizeof C - 1, "%d: %c%s\n", n, c == d->curr ? '*' : ' ', c->NAME);
    strcat(STR, C);
  }
  
  NOTIFY(STR, 1, 500);
}

void to_client(const Arg *arg) {
  Monitor *m = &mons[currmonidx];
  Desktop *d = &m->desktops[mons[currmonidx].currdeskidx];
  Client *c = d->head;
  int n = 1;
  for (; c && n != arg->i; c = c->next, n++);
  if (c) {
    focus(c, d, m);
    listclients(d);
  }
}

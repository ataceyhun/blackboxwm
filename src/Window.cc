// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Window.cc for Blackbox - an X11 Window manager
// Copyright (c) 2001 - 2004 Sean 'Shaleh' Perry <shaleh@debian.org>
// Copyright (c) 1997 - 2000, 2002 - 2004
//         Bradley T Hughes <bhughes at trolltech.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// make sure we get bt::textPropertyToString()
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "Window.hh"
#include "Screen.hh"
#include "WindowGroup.hh"
#include "Windowmenu.hh"
#include "Workspace.hh"
#include "blackbox.hh"

#include <Pen.hh>
#include <PixmapCache.hh>
#include <Unicode.hh>

#include <X11/Xatom.h>
#ifdef SHAPE
#  include <X11/extensions/shape.h>
#endif

#include <assert.h>


#if 0
static
void watch_decorations(const char *msg, WindowDecorationFlags flags) {
  fprintf(stderr, "Decorations: %s\n", msg);
  fprintf(stderr, "title   : %d\n",
          (flags & WindowDecorationTitlebar) != 0);
  fprintf(stderr, "handle  : %d\n",
          (flags & WindowDecorationHandle) != 0);
  fprintf(stderr, "grips   : %d\n",
          (flags & WindowDecorationGrip) != 0);
  fprintf(stderr, "border  : %d\n",
          (flags & WindowDecorationBorder) != 0);
  fprintf(stderr, "iconify : %d\n",
          (flags & WindowDecorationIconify) != 0);
  fprintf(stderr, "maximize: %d\n",
          (flags & WindowDecorationMaximize) != 0);
  fprintf(stderr, "close   : %d\n",
          (flags & WindowDecorationClose) != 0);
}
#endif


/*
 * Returns the appropriate WindowType based on the _NET_WM_WINDOW_TYPE
 */
static WindowType window_type_from_atom(const bt::Netwm &netwm, Atom atom) {
  if (atom == netwm.wmWindowTypeDialog())
    return WindowTypeDialog;
  if (atom == netwm.wmWindowTypeDesktop())
    return WindowTypeDesktop;
  if (atom == netwm.wmWindowTypeDock())
    return WindowTypeDock;
  if (atom == netwm.wmWindowTypeMenu())
    return WindowTypeMenu;
  if (atom == netwm.wmWindowTypeSplash())
    return WindowTypeSplash;
  if (atom == netwm.wmWindowTypeToolbar())
    return WindowTypeToolbar;
  if (atom == netwm.wmWindowTypeUtility())
    return WindowTypeUtility;
  return WindowTypeNormal;
}


/*
 * Determine the appropriate decorations and functions based on the
 * given properties and hints.
 */
static void update_decorations(WindowDecorationFlags &decorations,
                               WindowFunctionFlags &functions,
                               bool transient,
                               const EWMH &ewmh,
                               const MotifHints &motifhints,
                               const WMNormalHints &wmnormal,
                               const WMProtocols &wmprotocols) {
  decorations = AllWindowDecorations;
  functions   = AllWindowFunctions;

  // transients should be kept on the same workspace are their parents
  if (transient)
    functions &= ~WindowFunctionChangeWorkspace;

  // modify the window decorations/functions based on window type
  switch (ewmh.window_type) {
  case WindowTypeDialog:
    decorations &= ~(WindowDecorationIconify |
                     WindowDecorationMaximize);
    functions   &= ~(WindowFunctionShade |
                     WindowFunctionIconify |
                     WindowFunctionMaximize |
                     WindowFunctionChangeLayer |
                     WindowFunctionFullScreen);
    break;

  case WindowTypeDesktop:
  case WindowTypeDock:
  case WindowTypeSplash:
    decorations = NoWindowDecorations;
    functions   = NoWindowFunctions;
    break;

  case WindowTypeToolbar:
  case WindowTypeMenu:
    decorations &= ~(WindowDecorationHandle |
                     WindowDecorationGrip |
                     WindowDecorationBorder |
                     WindowDecorationIconify |
                     WindowDecorationMaximize);
    functions   &= ~(WindowFunctionResize |
                     WindowFunctionShade |
                     WindowFunctionIconify |
                     WindowFunctionMaximize |
                     WindowFunctionFullScreen);
    break;

  case WindowTypeUtility:
    decorations &= ~(WindowDecorationIconify |
                     WindowDecorationMaximize);
    functions   &= ~(WindowFunctionShade |
                     WindowFunctionIconify |
                     WindowFunctionMaximize);
    break;

  default:
    break;
  }

  // mask away stuff turned off by Motif hints
  decorations &= motifhints.decorations;
  functions   &= motifhints.functions;

  // disable shade if we do not have a titlebar
  if (!(decorations & WindowDecorationTitlebar))
    functions &= ~WindowFunctionShade;

  // disable grips and maximize if we have a fixed size window
  if ((wmnormal.flags & (PMinSize|PMaxSize)) == (PMinSize|PMaxSize)
      && wmnormal.max_width <= wmnormal.min_width
      && wmnormal.max_height <= wmnormal.min_height) {
    decorations &= ~(WindowDecorationMaximize |
                     WindowDecorationGrip);
    functions   &= ~(WindowFunctionResize |
                     WindowFunctionMaximize);
  }

  // cannot close if client doesn't understand WM_DELETE_WINDOW
  if (!wmprotocols.wm_delete_window) {
    decorations &= ~WindowDecorationClose;
    functions   &= ~WindowFunctionClose;
  }
}


/*
 * Add specified window to the appropriate window group, creating a
 * new group if necessary.
 */
static void update_window_group(Window window_group,
                                Blackbox *blackbox,
                                BlackboxWindow *win) {
  BWindowGroup *group = win->findWindowGroup();
  if (!group) {
    new BWindowGroup(blackbox, window_group);
    group = win->findWindowGroup();
    assert(group != 0);
  }
  group->addWindow(win);
}


/*
 * Initializes the class with default values/the window's set initial values.
 */
BlackboxWindow::BlackboxWindow(Blackbox *b, Window w, BScreen *s) {
  // fprintf(stderr, "BlackboxWindow size: %d bytes\n",
  //         sizeof(BlackboxWindow));

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::BlackboxWindow(): creating 0x%lx\n", w);
#endif // DEBUG

  /*
    set timer to zero... it is initialized properly later, so we check
    if timer is zero in the destructor, and assume that the window is not
    fully constructed if timer is zero...
  */
  timer = (bt::Timer*) 0;
  blackbox = b;
  client.window = w;
  _screen = s;
  lastButtonPressTime = 0;

  /*
    the server needs to be grabbed here to prevent client's from sending
    events while we are in the process of managing their window.
    We hold the grab until after we are done moving the window around.
  */

  blackbox->XGrabServer();

  // fetch client size and placement
  XWindowAttributes wattrib;
  if (! XGetWindowAttributes(blackbox->XDisplay(),
                             client.window, &wattrib) ||
      ! wattrib.screen || wattrib.override_redirect) {
#ifdef    DEBUG
    fprintf(stderr,
            "BlackboxWindow::BlackboxWindow(): XGetWindowAttributes failed\n");
#endif // DEBUG

    blackbox->XUngrabServer();
    delete this;
    return;
  }

  // set the eventmask early in the game so that we make sure we get
  // all the events we are interested in
  XSetWindowAttributes attrib_set;
  attrib_set.event_mask = PropertyChangeMask | StructureNotifyMask;
  attrib_set.do_not_propagate_mask = ButtonPressMask | ButtonReleaseMask |
                                     ButtonMotionMask;
  XChangeWindowAttributes(blackbox->XDisplay(), client.window,
                          CWEventMask|CWDontPropagate, &attrib_set);

  client.colormap = wattrib.colormap;
  window_number = bt::BSENTINEL;
  client.strut = 0;
  /*
    set the initial size and location of client window (relative to the
    _root window_). This position is the reference point used with the
    window's gravity to find the window's initial position.
  */
  client.rect.setRect(wattrib.x, wattrib.y, wattrib.width, wattrib.height);
  client.old_bw = wattrib.border_width;
  client.current_state = NormalState;

  frame.window = frame.plate = frame.title = frame.handle = None;
  frame.close_button = frame.iconify_button = frame.maximize_button = None;
  frame.right_grip = frame.left_grip = None;
  frame.utitle = frame.ftitle = frame.uhandle = frame.fhandle = None;
  frame.ulabel = frame.flabel = frame.ubutton = frame.fbutton = None;
  frame.pbutton = frame.ugrip = frame.fgrip = None;
  frame.style = _screen->resource().windowStyle();

  timer = new bt::Timer(blackbox, this);
  timer->setTimeout(blackbox->resource().autoRaiseDelay());

  client.title = readWMName();
  client.icon_title = readWMIconName();

  // get size, aspect, minimum/maximum size, ewmh and other hints set
  // by the client
  client.ewmh = readEWMH();
  client.motif = readMotifHints();
  client.wmhints = readWMHints();
  client.wmnormal = readWMNormalHints();
  client.wmprotocols = readWMProtocols();
  client.transient_for = readTransientInfo();

  if (isTransient()) {
    // add ourselves to our transient_for
    BlackboxWindow *win = findTransientFor();
    if (win) {
      win->addTransient(this);
      client.ewmh.workspace = win->workspace();
    }
  }

  if (client.wmhints.window_group != None)
    ::update_window_group(client.wmhints.window_group, blackbox, this);

  client.state.visible = false;
  client.state.iconic = false;
  client.state.moving = false;
  client.state.resizing = false;
  client.state.focused = false;

  switch (windowType()) {
  case WindowTypeDesktop:
    setLayer(StackingList::LayerDesktop);
    break;

  case WindowTypeDock:
    setLayer(StackingList::LayerAbove);
    break;
  default:
    break; // nothing to do here
  } // switch

  ::update_decorations(client.decorations,
                       client.functions,
                       isTransient(),
                       client.ewmh,
                       client.motif,
                       client.wmnormal,
                       client.wmprotocols);

  // sanity checks
  if (client.wmhints.initial_state == IconicState
      && !hasWindowFunction(WindowFunctionIconify))
    client.wmhints.initial_state = NormalState;
  if (isMaximized() && !hasWindowFunction(WindowFunctionMaximize))
    client.ewmh.maxv = client.ewmh.maxh = false;
  if (isFullScreen() && !hasWindowFunction(WindowFunctionFullScreen))
    client.ewmh.fullscreen = false;

  bt::Netwm::Strut strut;
  if (blackbox->netwm().readWMStrut(client.window, &strut)) {
    client.strut = new bt::Netwm::Strut;
    *client.strut = strut;
    _screen->addStrut(client.strut);
  }

  frame.window = createToplevelWindow();
  blackbox->insertEventHandler(frame.window, this);

  frame.plate = createChildWindow(frame.window, NoEventMask);
  blackbox->insertEventHandler(frame.plate, this);

  if (client.decorations & WindowDecorationTitlebar)
    createTitlebar();

  if (client.decorations & WindowDecorationHandle)
    createHandle();

  // apply the size and gravity hint to the frame
  upsize();
  applyGravity(frame.rect);

  associateClientWindow();

  blackbox->insertEventHandler(client.window, this);
  blackbox->insertWindow(client.window, this);
  blackbox->insertWindow(frame.plate, this);

  // preserve the window's initial state on first map, and its current
  // state across a restart
  if (!readState())
    client.current_state = client.wmhints.initial_state;

  if (client.state.iconic) {
    // prepare the window to be iconified
    client.current_state = IconicState;
    client.state.iconic = False;
  } else if (workspace() != bt::BSENTINEL &&
             workspace() != _screen->currentWorkspace()) {
    client.current_state = WithdrawnState;
  }

  configure(frame.rect);

  positionWindows();

  blackbox->XUngrabServer();

#ifdef SHAPE
  if (blackbox->hasShapeExtensions() && client.state.shaped)
    configureShape();
#endif // SHAPE

  // now that we know where to put the window and what it should look like
  // we apply the decorations
  decorate();

  grabButtons();

  XMapSubwindows(blackbox->XDisplay(), frame.window);

  client.premax = frame.rect;

  if (isShaded()) {
    client.ewmh.shaded = false;
    unsigned long save_state = client.current_state;
    setShaded(true);

    /*
      At this point in the life of a window, current_state should only be set
      to IconicState if the window was an *icon*, not if it was shaded.
    */
    if (save_state != IconicState)
      client.current_state = save_state;
  }

  if (!hasWindowFunction(WindowFunctionMaximize))
    client.ewmh.maxh = client.ewmh.maxv = false;

  if (isFullScreen()) {
    client.ewmh.fullscreen = false; // trick setFullScreen into working
    setFullScreen(true);
  } else if (isMaximized()) {
    remaximize();
  }
}


BlackboxWindow::~BlackboxWindow(void) {
#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::~BlackboxWindow: destroying 0x%lx\n",
          client.window);
#endif // DEBUG

  if (! timer) // window not managed...
    return;

  if (client.state.moving || client.state.resizing) {
    _screen->hideGeometry();
    XUngrabPointer(blackbox->XDisplay(), blackbox->XTime());
  }

  delete timer;

  if (client.strut) {
    _screen->removeStrut(client.strut);
    delete client.strut;
  }

  BWindowGroup *group = findWindowGroup();
  if (group)
    group->removeWindow(this);

  // remove ourselves from our transient_for
  if (isTransient()) {
    BlackboxWindow *win = findTransientFor();
    if (win)
      win->removeTransient(this);
    client.transient_for = 0;
  }

  if (frame.title)
    destroyTitlebar();

  if (frame.handle)
    destroyHandle();

  blackbox->removeEventHandler(frame.plate);
  blackbox->removeWindow(frame.plate);
  XDestroyWindow(blackbox->XDisplay(), frame.plate);

  if (frame.window) {
    blackbox->removeEventHandler(frame.window);
    XDestroyWindow(blackbox->XDisplay(), frame.window);
  }

  blackbox->removeEventHandler(client.window);
  blackbox->removeWindow(client.window);
}


/*
 * Creates a new top level window, with a given location, size, and border
 * width.
 * Returns: the newly created window
 */
Window BlackboxWindow::createToplevelWindow(void) {
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWColormap | CWOverrideRedirect | CWEventMask;

  attrib_create.colormap = _screen->screenInfo().colormap();
  attrib_create.override_redirect = True;
  attrib_create.event_mask = EnterWindowMask | LeaveWindowMask;

  return XCreateWindow(blackbox->XDisplay(),
                       _screen->screenInfo().rootWindow(), 0, 0, 1, 1, 0,
                       _screen->screenInfo().depth(), InputOutput,
                       _screen->screenInfo().visual(),
                       create_mask, &attrib_create);
}


/*
 * Creates a child window, and optionally associates a given cursor with
 * the new window.
 */
Window BlackboxWindow::createChildWindow(Window parent,
                                         unsigned long event_mask,
                                         Cursor cursor) {
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWEventMask;

  attrib_create.event_mask = event_mask;

  if (cursor) {
    create_mask |= CWCursor;
    attrib_create.cursor = cursor;
  }

  return XCreateWindow(blackbox->XDisplay(), parent, 0, 0, 1, 1, 0,
                       _screen->screenInfo().depth(), InputOutput,
                       _screen->screenInfo().visual(),
                       create_mask, &attrib_create);
}


/*
 * Reparents the client window into the newly created frame.
 *
 * Note: the server must be grabbed before calling this function.
 */
void BlackboxWindow::associateClientWindow(void) {
  XSetWindowBorderWidth(blackbox->XDisplay(), client.window, 0);
  XChangeSaveSet(blackbox->XDisplay(), client.window, SetModeInsert);

  XSelectInput(blackbox->XDisplay(), frame.plate,
               FocusChangeMask | SubstructureRedirectMask);

  unsigned long event_mask = PropertyChangeMask | StructureNotifyMask;
  XSelectInput(blackbox->XDisplay(), client.window,
               event_mask & ~StructureNotifyMask);
  XReparentWindow(blackbox->XDisplay(), client.window, frame.plate, 0, 0);
  XSelectInput(blackbox->XDisplay(), client.window, event_mask);

#ifdef    SHAPE
  if (blackbox->hasShapeExtensions()) {
    XShapeSelectInput(blackbox->XDisplay(), client.window,
                      ShapeNotifyMask);

    Bool shaped = False;
    int foo;
    unsigned int ufoo;

    XShapeQueryExtents(blackbox->XDisplay(), client.window, &shaped,
                       &foo, &foo, &ufoo, &ufoo, &foo, &foo, &foo,
                       &ufoo, &ufoo);
    client.state.shaped = shaped;
  }
#endif // SHAPE
}


void BlackboxWindow::decorate(void) {
  if (client.decorations & WindowDecorationTitlebar) {
    // render focused button texture
    frame.fbutton =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->focus.button,
                            frame.style->button_width,
                            frame.style->button_width,
                            frame.fbutton);

    // render unfocused button texture
    frame.ubutton =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->unfocus.button,
                            frame.style->button_width,
                            frame.style->button_width,
                            frame.ubutton);

    // render pressed button texture
    frame.pbutton =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->pressed,
                            frame.style->button_width,
                            frame.style->button_width,
                            frame.pbutton);

    // render focused titlebar texture
    frame.ftitle =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->focus.title,
                            frame.rect.width(),
                            frame.style->title_height,
                            frame.ftitle);

    // render unfocused titlebar texture
    frame.utitle =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->unfocus.title,
                            frame.rect.width(),
                            frame.style->title_height,
                            frame.utitle);

    // render focused label texture
    frame.flabel =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->focus.label,
                            frame.label_w,
                            frame.style->label_height,
                            frame.flabel);

    // render unfocused label texture
    frame.ulabel =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->unfocus.label,
                            frame.label_w,
                            frame.style->label_height,
                            frame.ulabel);
  }

  XSetWindowBorder(blackbox->XDisplay(), frame.plate,
                   frame.style->frame_border.pixel(_screen->screenNumber()));

  if (client.decorations & WindowDecorationHandle) {
    frame.fhandle =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->focus.handle,
                            frame.rect.width(),
                            frame.style->handle_height,
                            frame.fhandle);

    frame.uhandle =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->unfocus.handle,
                            frame.rect.width(),
                            frame.style->handle_height,
                            frame.uhandle);
  }

  if (client.decorations & WindowDecorationGrip) {
    frame.fgrip =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->focus.grip,
                            frame.style->grip_width,
                            frame.style->handle_height,
                            frame.fgrip);

    frame.ugrip =
      bt::PixmapCache::find(_screen->screenNumber(),
                            frame.style->unfocus.grip,
                            frame.style->grip_width,
                            frame.style->handle_height,
                            frame.ugrip);
  }
}


void BlackboxWindow::createHandle(void) {
  frame.handle = createChildWindow(frame.window,
                                   ButtonPressMask | ButtonReleaseMask |
                                   ButtonMotionMask | ExposureMask);
  blackbox->insertEventHandler(frame.handle, this);

  if (client.decorations & WindowDecorationGrip)
    createGrips();
}


void BlackboxWindow::destroyHandle(void) {
  if (frame.left_grip || frame.right_grip)
    destroyGrips();

  if (frame.fhandle) bt::PixmapCache::release(frame.fhandle);
  if (frame.uhandle) bt::PixmapCache::release(frame.uhandle);

  frame.fhandle = frame.uhandle = None;

  blackbox->removeEventHandler(frame.handle);
  XDestroyWindow(blackbox->XDisplay(), frame.handle);
  frame.handle = None;
}


void BlackboxWindow::createGrips(void) {
  frame.left_grip =
    createChildWindow(frame.handle,
                      ButtonPressMask | ButtonReleaseMask |
                      ButtonMotionMask | ExposureMask,
                      blackbox->resource().resizeBottomLeftCursor());
  blackbox->insertEventHandler(frame.left_grip, this);

  frame.right_grip =
    createChildWindow(frame.handle,
                      ButtonPressMask | ButtonReleaseMask |
                      ButtonMotionMask | ExposureMask,
                      blackbox->resource().resizeBottomRightCursor());
  blackbox->insertEventHandler(frame.right_grip, this);
}


void BlackboxWindow::destroyGrips(void) {
  if (frame.fgrip) bt::PixmapCache::release(frame.fgrip);
  if (frame.ugrip) bt::PixmapCache::release(frame.ugrip);

  frame.fgrip = frame.ugrip = None;

  blackbox->removeEventHandler(frame.left_grip);
  blackbox->removeEventHandler(frame.right_grip);

  XDestroyWindow(blackbox->XDisplay(), frame.left_grip);
  XDestroyWindow(blackbox->XDisplay(), frame.right_grip);
  frame.left_grip = frame.right_grip = None;
}


void BlackboxWindow::createTitlebar(void) {
  frame.title = createChildWindow(frame.window,
                                  ButtonPressMask | ButtonReleaseMask |
                                  ButtonMotionMask | ExposureMask);
  frame.label = createChildWindow(frame.title,
                                  ButtonPressMask | ButtonReleaseMask |
                                  ButtonMotionMask | ExposureMask);
  blackbox->insertEventHandler(frame.title, this);
  blackbox->insertEventHandler(frame.label, this);

  if (client.decorations & WindowDecorationIconify) createIconifyButton();
  if (client.decorations & WindowDecorationMaximize) createMaximizeButton();
  if (client.decorations & WindowDecorationClose) createCloseButton();
}


void BlackboxWindow::destroyTitlebar(void) {
  if (frame.close_button)
    destroyCloseButton();

  if (frame.iconify_button)
    destroyIconifyButton();

  if (frame.maximize_button)
    destroyMaximizeButton();

  if (frame.fbutton) bt::PixmapCache::release(frame.fbutton);
  if (frame.ubutton) bt::PixmapCache::release(frame.ubutton);
  if (frame.pbutton) bt::PixmapCache::release(frame.pbutton);
  if (frame.ftitle)  bt::PixmapCache::release(frame.ftitle);
  if (frame.utitle)  bt::PixmapCache::release(frame.utitle);
  if (frame.flabel)  bt::PixmapCache::release(frame.flabel);
  if (frame.ulabel)  bt::PixmapCache::release(frame.ulabel);

  frame.fbutton = frame.ubutton = frame.pbutton =
   frame.ftitle = frame.utitle =
   frame.flabel = frame.ulabel = None;

  blackbox->removeEventHandler(frame.title);
  blackbox->removeEventHandler(frame.label);

  XDestroyWindow(blackbox->XDisplay(), frame.label);
  XDestroyWindow(blackbox->XDisplay(), frame.title);
  frame.title = frame.label = None;
}


void BlackboxWindow::createCloseButton(void) {
  if (frame.title != None) {
    frame.close_button = createChildWindow(frame.title,
                                           ButtonPressMask |
                                           ButtonReleaseMask |
                                           ButtonMotionMask | ExposureMask);
    blackbox->insertEventHandler(frame.close_button, this);
  }
}


void BlackboxWindow::destroyCloseButton(void) {
  blackbox->removeEventHandler(frame.close_button);
  XDestroyWindow(blackbox->XDisplay(), frame.close_button);
  frame.close_button = None;
}


void BlackboxWindow::createIconifyButton(void) {
  if (frame.title != None) {
    frame.iconify_button = createChildWindow(frame.title,
                                             ButtonPressMask |
                                             ButtonReleaseMask |
                                             ButtonMotionMask | ExposureMask);
    blackbox->insertEventHandler(frame.iconify_button, this);
  }
}


void BlackboxWindow::destroyIconifyButton(void) {
  blackbox->removeEventHandler(frame.iconify_button);
  XDestroyWindow(blackbox->XDisplay(), frame.iconify_button);
  frame.iconify_button = None;
}


void BlackboxWindow::createMaximizeButton(void) {
  if (frame.title != None) {
    frame.maximize_button = createChildWindow(frame.title,
                                              ButtonPressMask |
                                              ButtonReleaseMask |
                                              ButtonMotionMask | ExposureMask);
    blackbox->insertEventHandler(frame.maximize_button, this);
  }
}


void BlackboxWindow::destroyMaximizeButton(void) {
  blackbox->removeEventHandler(frame.maximize_button);
  XDestroyWindow(blackbox->XDisplay(), frame.maximize_button);
  frame.maximize_button = None;
}


void BlackboxWindow::positionButtons(bool redecorate_label) {
  // we need to use signed ints here to detect windows that are too small
  const int extra = frame.style->title_margin == 0 ?
                    frame.style->focus.button.borderWidth() : 0,
               bw = frame.style->button_width + frame.style->title_margin
                    - extra,
               by = frame.style->title_margin +
                    frame.style->focus.title.borderWidth();
  int lx = by, lw = frame.rect.width() - by;

  if (client.decorations & WindowDecorationIconify) {
    if (frame.iconify_button == None) createIconifyButton();

    XMoveResizeWindow(blackbox->XDisplay(), frame.iconify_button, by, by,
                      frame.style->button_width, frame.style->button_width);
    XMapWindow(blackbox->XDisplay(), frame.iconify_button);

    lx += bw;
    lw -= bw;
  } else if (frame.iconify_button) {
    destroyIconifyButton();
  }

  int bx = frame.rect.width() - bw
           - frame.style->focus.title.borderWidth() - extra;

  if (client.decorations & WindowDecorationClose) {
    if (frame.close_button == None) createCloseButton();

    XMoveResizeWindow(blackbox->XDisplay(), frame.close_button, bx, by,
                      frame.style->button_width, frame.style->button_width);
    XMapWindow(blackbox->XDisplay(), frame.close_button);

    bx -= bw;
    lw -= bw;
  } else if (frame.close_button) {
    destroyCloseButton();
  }

  if (client.decorations & WindowDecorationMaximize) {
    if (frame.maximize_button == None) createMaximizeButton();

    XMoveResizeWindow(blackbox->XDisplay(), frame.maximize_button, bx, by,
                      frame.style->button_width, frame.style->button_width);
    XMapWindow(blackbox->XDisplay(), frame.maximize_button);

    bx -= bw;
    lw -= bw;
  } else if (frame.maximize_button) {
    destroyMaximizeButton();
  }

  if (lw > by) {
    frame.label_w = lw - by;
    XMoveResizeWindow(blackbox->XDisplay(), frame.label, lx, by,
                      frame.label_w, frame.style->label_height);
    XMapWindow(blackbox->XDisplay(), frame.label);

    if (redecorate_label) {
      frame.flabel =
        bt::PixmapCache::find(_screen->screenNumber(),
                              frame.style->focus.label,
                              frame.label_w, frame.style->label_height,
                              frame.flabel);
      frame.ulabel =
        bt::PixmapCache::find(_screen->screenNumber(),
                              frame.style->unfocus.label,
                              frame.label_w, frame.style->label_height,
                              frame.ulabel);
    }

    const bt::ustring ellided =
      bt::ellideText(client.title, frame.label_w, bt::toUnicode("..."),
                     _screen->screenNumber(), frame.style->font);

    if (ellided != client.visible_title) {
      client.visible_title = ellided;
      blackbox->netwm().setWMVisibleName(client.window, client.visible_title);
    }
  } else {
    XUnmapWindow(blackbox->XDisplay(), frame.label);
  }

  redrawLabel();
  redrawAllButtons();
}


void BlackboxWindow::reconfigure(void) {
  restoreGravity(client.rect);
  upsize();
  applyGravity(frame.rect);
  positionWindows();
  decorate();
  redrawWindowFrame();

  ungrabButtons();
  grabButtons();
}


void BlackboxWindow::grabButtons(void) {
  if (! _screen->resource().isSloppyFocus() ||
      _screen->resource().doClickRaise())
    // grab button 1 for changing focus/raising
    blackbox->grabButton(Button1, 0, frame.plate, True, ButtonPressMask,
                         GrabModeSync, GrabModeSync, frame.plate, None,
                         _screen->resource().allowScrollLock());

  if (hasWindowFunction(WindowFunctionMove))
    blackbox->grabButton(Button1, Mod1Mask, frame.window, True,
                         ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                         GrabModeAsync, frame.window,
                         blackbox->resource().moveCursor(),
                         _screen->resource().allowScrollLock());
  if (hasWindowFunction(WindowFunctionResize))
    blackbox->grabButton(Button3, Mod1Mask, frame.window, True,
                         ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                         GrabModeAsync, frame.window,
                         blackbox->resource().resizeBottomRightCursor(),
                         _screen->resource().allowScrollLock());
  // alt+middle lowers the window
  blackbox->grabButton(Button2, Mod1Mask, frame.window, True,
                       ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                       frame.window, None,
                       _screen->resource().allowScrollLock());
}


void BlackboxWindow::ungrabButtons(void) {
  blackbox->ungrabButton(Button1, 0, frame.plate);
  blackbox->ungrabButton(Button1, Mod1Mask, frame.window);
  blackbox->ungrabButton(Button2, Mod1Mask, frame.window);
  blackbox->ungrabButton(Button3, Mod1Mask, frame.window);
}


void BlackboxWindow::positionWindows(void) {
  const unsigned int bw = (hasWindowDecoration(WindowDecorationBorder)
                           ? frame.style->frame_border_width
                           : 0);

  XMoveResizeWindow(blackbox->XDisplay(), frame.window,
                    frame.rect.x(), frame.rect.y(),
                    frame.rect.width(), frame.rect.height());
  XMoveResizeWindow(blackbox->XDisplay(), frame.plate,
                    frame.margin.left - bw,
                    frame.margin.top - bw,
                    client.rect.width(), client.rect.height());
  XSetWindowBorderWidth(blackbox->XDisplay(), frame.plate, bw);
  XMoveResizeWindow(blackbox->XDisplay(), client.window,
                    0, 0, client.rect.width(), client.rect.height());
  // ensure client.rect contains the real location
  client.rect.setPos(frame.rect.left() + frame.margin.left,
                     frame.rect.top() + frame.margin.top);

  if (client.decorations & WindowDecorationTitlebar) {
    if (frame.title == None) createTitlebar();

    XMoveResizeWindow(blackbox->XDisplay(), frame.title,
                      0, 0, frame.rect.width(), frame.style->title_height);

    positionButtons();
    XMapSubwindows(blackbox->XDisplay(), frame.title);
    XMapWindow(blackbox->XDisplay(), frame.title);
  } else if (frame.title) {
    destroyTitlebar();
  }

  if (client.decorations & WindowDecorationHandle) {
    if (frame.handle == None) createHandle();

    // use client.rect here so the value is correct even if shaded
    XMoveResizeWindow(blackbox->XDisplay(), frame.handle,
                      0, client.rect.height() + frame.margin.top,
                      frame.rect.width(), frame.style->handle_height);

    if (client.decorations & WindowDecorationGrip) {
      if (frame.left_grip == None || frame.right_grip == None) createGrips();

      XMoveResizeWindow(blackbox->XDisplay(), frame.left_grip, 0, 0,
                        frame.style->grip_width, frame.style->handle_height);

      const int nx = frame.rect.width() - frame.style->grip_width;
      XMoveResizeWindow(blackbox->XDisplay(), frame.right_grip, nx, 0,
                        frame.style->grip_width, frame.style->handle_height);

      XMapSubwindows(blackbox->XDisplay(), frame.handle);
    } else {
      destroyGrips();
    }

    XMapWindow(blackbox->XDisplay(), frame.handle);
  } else if (frame.handle) {
    destroyHandle();
  }
}


bt::ustring BlackboxWindow::readWMName(void) {
  bt::ustring name;

  if (!blackbox->netwm().readWMName(client.window, name) || name.empty()) {
    XTextProperty text_prop;
    if (XGetWMName(blackbox->XDisplay(), client.window, &text_prop)) {
      name =
        bt::toUnicode(bt::textPropertyToString(blackbox->XDisplay(),
                                               text_prop));
      XFree((char *) text_prop.value);
    }
  }
  if (name.empty())
    name = bt::toUnicode("Unnamed");

  return name;
}


bt::ustring BlackboxWindow::readWMIconName(void) {
  bt::ustring name;

  if (!blackbox->netwm().readWMIconName(client.window, name) || name.empty()) {
    XTextProperty text_prop;
    if (XGetWMIconName(blackbox->XDisplay(), client.window, &text_prop)) {
      name =
        bt::toUnicode(bt::textPropertyToString(blackbox->XDisplay(),
                                               text_prop));
      XFree((char *) text_prop.value);
    }
  }
  if (name.empty())
    return bt::ustring();

  return name;
}


EWMH BlackboxWindow::readEWMH(void) {
  EWMH ewmh;
  ewmh.window_type  = WindowTypeNormal;
  ewmh.workspace    = 0; // initialized properly below
  ewmh.modal        = false;
  ewmh.maxv         = false;
  ewmh.maxh         = false;
  ewmh.shaded       = false;
  ewmh.skip_taskbar = false;
  ewmh.skip_pager   = false;
  ewmh.hidden       = false;
  ewmh.fullscreen   = false;
  ewmh.above        = false;
  ewmh.below        = false;

  // note: wm_name and wm_icon_name are read separately

  bool ret;
  const bt::Netwm& netwm = blackbox->netwm();

  bt::Netwm::AtomList atoms;
  ret = netwm.readWMWindowType(client.window, atoms);
  if (ret) {
    bt::Netwm::AtomList::iterator it = atoms.begin(), end = atoms.end();
    for (; it != end; ++it) {
      if (netwm.isSupportedWMWindowType(*it)) {
        ewmh.window_type = ::window_type_from_atom(netwm, *it);
        break;
      }
    }
  }

  atoms.clear();
  ret = netwm.readWMState(client.window, atoms);
  if (ret) {
    bt::Netwm::AtomList::iterator it = atoms.begin(), end = atoms.end();
    for (; it != end; ++it) {
      Atom state = *it;
      if (state == netwm.wmStateModal()) {
        ewmh.modal = true;
      } else if (state == netwm.wmStateMaximizedVert()) {
        ewmh.maxv = true;
      } else if (state == netwm.wmStateMaximizedHorz()) {
        ewmh.maxh = true;
      } else if (state == netwm.wmStateShaded()) {
        ewmh.shaded = true;
      } else if (state == netwm.wmStateSkipTaskbar()) {
        ewmh.skip_taskbar = true;
      } else if (state == netwm.wmStateSkipPager()) {
        ewmh.skip_pager = true;
      } else if (state == netwm.wmStateHidden()) {
        /*
          ignore _NET_WM_STATE_HIDDEN if present, the wm sets this
          state, not the application
         */
      } else if (state == netwm.wmStateFullscreen()) {
        ewmh.fullscreen = true;
      } else if (state == netwm.wmStateAbove()) {
        ewmh.above = true;
      } else if (state == netwm.wmStateBelow()) {
        ewmh.below = true;
      }
    }
  }

  switch (ewmh.window_type) {
  case WindowTypeDesktop:
  case WindowTypeDock:
    // these types should occupy all workspaces by default
    ewmh.workspace = bt::BSENTINEL;
    break;

  default:
    if (!netwm.readWMDesktop(client.window, ewmh.workspace))
      ewmh.workspace = _screen->currentWorkspace();
    break;
  } //switch

  return ewmh;
}


/*
 * Retrieve which Window Manager Protocols are supported by the client
 * window.
 */
WMProtocols BlackboxWindow::readWMProtocols(void) {
  WMProtocols protocols;
  protocols.wm_delete_window = false;
  protocols.wm_take_focus    = false;

  Atom *proto;
  int num_return = 0;

  if (XGetWMProtocols(blackbox->XDisplay(), client.window,
                      &proto, &num_return)) {
    for (int i = 0; i < num_return; ++i) {
      if (proto[i] == blackbox->wmDeleteWindowAtom()) {
        protocols.wm_delete_window = true;
      } else if (proto[i] == blackbox->wmTakeFocusAtom()) {
        protocols.wm_take_focus = true;
      }
    }
    XFree(proto);
  }

  return protocols;
}


/*
 * Returns the value of the WM_HINTS property.  If the property is not
 * set, a set of default values is returned instead.
 */
WMHints BlackboxWindow::readWMHints(void) {
  WMHints wmh;
  wmh.accept_focus = false;
  wmh.window_group = None;
  wmh.initial_state = NormalState;

  XWMHints *wmhint = XGetWMHints(blackbox->XDisplay(), client.window);
  if (!wmhint) return wmh;

  if (wmhint->flags & InputHint)
    wmh.accept_focus = (wmhint->input == True);
  if (wmhint->flags & StateHint)
    wmh.initial_state = wmhint->initial_state;
  if (wmhint->flags & WindowGroupHint
      && wmhint->window_group != _screen->screenInfo().rootWindow())
    wmh.window_group = wmhint->window_group;

  XFree(wmhint);

  return wmh;
}


/*
 * Returns the value of the WM_NORMAL_HINTS property.  If the property
 * is not set, a set of default values is returned instead.
 */
WMNormalHints BlackboxWindow::readWMNormalHints(void) {
  WMNormalHints wmnormal;
  wmnormal.flags = 0;
  wmnormal.min_width    = wmnormal.min_height   = 1u;
  wmnormal.width_inc    = wmnormal.height_inc   = 1u;
  wmnormal.min_aspect_x = wmnormal.min_aspect_y = 1u;
  wmnormal.max_aspect_x = wmnormal.max_aspect_y = 1u;
  wmnormal.base_width   = wmnormal.base_height  = 0u;
  wmnormal.win_gravity  = NorthWestGravity;

  /*
    use the full screen, not the strut modified size. otherwise when
    the availableArea changes max_width/height will be incorrect and
    lead to odd rendering bugs.
  */
  const bt::Rect &rect = _screen->screenInfo().rect();
  wmnormal.max_width = rect.width();
  wmnormal.max_height = rect.height();

  XSizeHints sizehint;
  long unused;
  if (! XGetWMNormalHints(blackbox->XDisplay(), client.window,
                          &sizehint, &unused))
    return wmnormal;

  wmnormal.flags = sizehint.flags;

  if (sizehint.flags & PMinSize) {
    if (sizehint.min_width > 0)
      wmnormal.min_width  = sizehint.min_width;
    if (sizehint.min_height > 0)
      wmnormal.min_height = sizehint.min_height;
  }

  if (sizehint.flags & PMaxSize) {
    if (sizehint.max_width > static_cast<signed>(wmnormal.min_width))
      wmnormal.max_width  = sizehint.max_width;
    else
      wmnormal.max_width  = wmnormal.min_width;

    if (sizehint.max_height > static_cast<signed>(wmnormal.min_height))
      wmnormal.max_height = sizehint.max_height;
    else
      wmnormal.max_height = wmnormal.min_height;
  }

  if (sizehint.flags & PResizeInc) {
    wmnormal.width_inc  = sizehint.width_inc;
    wmnormal.height_inc = sizehint.height_inc;
  }

  if (sizehint.flags & PAspect) {
    wmnormal.min_aspect_x = sizehint.min_aspect.x;
    wmnormal.min_aspect_y = sizehint.min_aspect.y;
    wmnormal.max_aspect_x = sizehint.max_aspect.x;
    wmnormal.max_aspect_y = sizehint.max_aspect.y;
  }

  if (sizehint.flags & PBaseSize) {
    wmnormal.base_width  = sizehint.base_width;
    wmnormal.base_height = sizehint.base_height;
  }

  if (sizehint.flags & PWinGravity)
    wmnormal.win_gravity = sizehint.win_gravity;

  return wmnormal;
}


/*
 * Returns the Motif hints for the class' contained window.
 */
MotifHints BlackboxWindow::readMotifHints(void) {
  MotifHints motif;
  motif.decorations = AllWindowDecorations;
  motif.functions   = AllWindowFunctions;

  /*
    this structure only contains 3 elements, even though the Motif 2.0
    structure contains 5, because we only use the first 3
  */
  struct PropMotifhints {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
  };
  static const unsigned int PROP_MWM_HINTS_ELEMENTS = 3u;
  enum { // MWM flags
    MWM_HINTS_FUNCTIONS   = 1<<0,
    MWM_HINTS_DECORATIONS = 1<<1
  };
  enum { // MWM functions
    MWM_FUNC_ALL      = 1<<0,
    MWM_FUNC_RESIZE   = 1<<1,
    MWM_FUNC_MOVE     = 1<<2,
    MWM_FUNC_MINIMIZE = 1<<3,
    MWM_FUNC_MAXIMIZE = 1<<4,
    MWM_FUNC_CLOSE    = 1<<5
  };
  enum { // MWM decorations
    MWM_DECOR_ALL      = 1<<0,
    MWM_DECOR_BORDER   = 1<<1,
    MWM_DECOR_RESIZEH  = 1<<2,
    MWM_DECOR_TITLE    = 1<<3,
    MWM_DECOR_MENU     = 1<<4,
    MWM_DECOR_MINIMIZE = 1<<5,
    MWM_DECOR_MAXIMIZE = 1<<6
  };

  Atom atom_return;
  PropMotifhints *prop = 0;
  int format;
  unsigned long num, len;
  int ret = XGetWindowProperty(blackbox->XDisplay(), client.window,
                               blackbox->motifWmHintsAtom(), 0,
                               PROP_MWM_HINTS_ELEMENTS, False,
                               blackbox->motifWmHintsAtom(), &atom_return,
                               &format, &num, &len,
                               (unsigned char **) &prop);

  if (ret != Success || !prop || num != PROP_MWM_HINTS_ELEMENTS) {
    if (prop) XFree(prop);
    return motif;
  }

  if (prop->flags & MWM_HINTS_FUNCTIONS) {
    if (prop->functions & MWM_FUNC_ALL) {
      motif.functions = AllWindowFunctions;
    } else {
      motif.functions = NoWindowFunctions;

      if (prop->functions & MWM_FUNC_RESIZE)
        motif.functions |= WindowFunctionResize;
      if (prop->functions & MWM_FUNC_MOVE)
        motif.functions |= WindowFunctionMove;
      if (prop->functions & MWM_FUNC_MINIMIZE)
        motif.functions |= WindowFunctionIconify;
      if (prop->functions & MWM_FUNC_MAXIMIZE)
        motif.functions |= WindowFunctionMaximize;
      if (prop->functions & MWM_FUNC_CLOSE)
        motif.functions |= WindowFunctionClose;
    }
  }

  if (prop->flags & MWM_HINTS_DECORATIONS) {
    if (prop->decorations & MWM_DECOR_ALL) {
      motif.decorations = AllWindowDecorations;
    } else {
      motif.decorations = NoWindowDecorations;

      if (prop->decorations & MWM_DECOR_BORDER)
        motif.decorations |= WindowDecorationBorder;
      if (prop->decorations & MWM_DECOR_RESIZEH) {
        motif.decorations |= (WindowDecorationHandle |
                              WindowDecorationGrip);
      }
      if (prop->decorations & MWM_DECOR_TITLE) {
        motif.decorations |= (WindowDecorationTitlebar |
                              WindowDecorationClose);
      }
      if (prop->decorations & MWM_DECOR_MINIMIZE)
        motif.decorations |= WindowDecorationIconify;
      if (prop->decorations & MWM_DECOR_MAXIMIZE)
        motif.decorations |= WindowDecorationMaximize;
    }
  }

  XFree(prop);

  return motif;
}


/*
 * Reads the value of the WM_TRANSIENT_FOR property and returns a
 * pointer to the transient parent for this window.  If the
 * WM_TRANSIENT_FOR is missing or invalid, this function returns 0.
 *
 * 'client.wmhints' should be properly updated before calling this
 * function.
 *
 * Note: a return value of ~0ul signifies a window that should be
 * transient but has no discernible parent.
 */
Window BlackboxWindow::readTransientInfo(void) {
  Window trans_for = None;

  if (!XGetTransientForHint(blackbox->XDisplay(), client.window, &trans_for)) {
    // WM_TRANSIENT_FOR hint not set
    return 0;
  }

  if (trans_for == client.window) {
    // wierd client... treat this window as a normal window
    return 0;
  }

  if (trans_for == None || trans_for == _screen->screenInfo().rootWindow()) {
    /*
      this is a violation of the ICCCM, yet the EWMH allows this as a
      way to signify a group transient.
    */
    trans_for = client.wmhints.window_group;
  }

  return trans_for;
}


/*
 * This function is responsible for updating both the client and the
 * frame rectangles.  According to the ICCCM a client message is not
 * sent for a resize, only a move.
 */
void BlackboxWindow::configure(int dx, int dy,
                               unsigned int dw, unsigned int dh) {
  bool send_event = ((frame.rect.x() != dx || frame.rect.y() != dy) &&
                     ! client.state.moving);

  if (dw != frame.rect.width() || dh != frame.rect.height()) {
    frame.rect.setRect(dx, dy, dw, dh);

    if (frame.rect.right() <= 0 || frame.rect.bottom() <= 0)
      frame.rect.setPos(0, 0);

    client.rect.setCoords(frame.rect.left() + frame.margin.left,
                          frame.rect.top() + frame.margin.top,
                          frame.rect.right() - frame.margin.right,
                          frame.rect.bottom() - frame.margin.bottom);

#ifdef SHAPE
    if (client.state.shaped)
      configureShape();
#endif // SHAPE

    positionWindows();
    decorate();
    redrawWindowFrame();
  } else {
    frame.rect.setPos(dx, dy);

    XMoveWindow(blackbox->XDisplay(), frame.window,
                frame.rect.x(), frame.rect.y());
    /*
      we may have been called just after an opaque window move, so
      even though the old coords match the new ones no ConfigureNotify
      has been sent yet.  There are likely other times when this will
      be relevant as well.
    */
    if (! client.state.moving) send_event = True;
  }

  if (send_event) {
    // if moving, the update and event will occur when the move finishes
    client.rect.setPos(frame.rect.left() + frame.margin.left,
                       frame.rect.top() + frame.margin.top);

    XEvent event;
    event.type = ConfigureNotify;

    event.xconfigure.display = blackbox->XDisplay();
    event.xconfigure.event = client.window;
    event.xconfigure.window = client.window;
    event.xconfigure.x = client.rect.x();
    event.xconfigure.y = client.rect.y();
    event.xconfigure.width = client.rect.width();
    event.xconfigure.height = client.rect.height();
    event.xconfigure.border_width = client.old_bw;
    event.xconfigure.above = frame.window;
    event.xconfigure.override_redirect = False;

    XSendEvent(blackbox->XDisplay(), client.window, False,
               StructureNotifyMask, &event);
  }
}


#ifdef SHAPE
void BlackboxWindow::configureShape(void) {
  XShapeCombineShape(blackbox->XDisplay(), frame.window, ShapeBounding,
                     frame.margin.left, frame.margin.top,
                     client.window, ShapeBounding, ShapeSet);

  int num = 0;
  XRectangle xrect[2];

  if (client.decorations & WindowDecorationTitlebar) {
    xrect[0].x = xrect[0].y = 0;
    xrect[0].width = frame.rect.width();
    xrect[0].height = frame.style->title_height;
    ++num;
  }

  if (client.decorations & WindowDecorationHandle) {
    xrect[1].x = 0;
    xrect[1].y = client.rect.height() + frame.margin.top;
    xrect[1].width = frame.rect.width();
    xrect[1].height = frame.style->handle_height;
    ++num;
  }

  XShapeCombineRectangles(blackbox->XDisplay(), frame.window,
                          ShapeBounding, 0, 0, xrect, num,
                          ShapeUnion, Unsorted);
}
#endif // SHAPE


void BlackboxWindow::addTransient(BlackboxWindow *win) {
  client.transientList.push_front(win);
}


void BlackboxWindow::removeTransient(BlackboxWindow *win) {
  client.transientList.remove(win);
}


BlackboxWindow *BlackboxWindow::findTransientFor(void) const {
  BlackboxWindow *win = 0;
  if (isTransient()) {
    win = blackbox->findWindow(client.transient_for);
    if (win && win->_screen != _screen)
      win = 0;
  }
  return win;
}


// walk up to either 1) a non-transient window 2) a group transient,
// watching out for a circular chain
BlackboxWindow *BlackboxWindow::findNonTransientParent(void) const {
  BlackboxWindow *w = const_cast<BlackboxWindow *>(this);

  BlackboxWindowList seen;
  seen.push_back(w);

  while (w->isTransient() && !w->isGroupTransient()) {
    BlackboxWindow * const tmp = w->findTransientFor();
    if (!tmp)
      break;
    w = tmp;

    if (std::find(seen.begin(), seen.end(), w) != seen.end()) {
      // circular transient chain
      break;
    }
    seen.push_back(w);
  }
  return w;
}


BWindowGroup *BlackboxWindow::findWindowGroup(void) const {
  BWindowGroup *group = 0;
  if (client.wmhints.window_group)
    group = blackbox->findWindowGroup(client.wmhints.window_group);
  return group;
}


void BlackboxWindow::setWorkspace(unsigned int new_workspace) {
  client.ewmh.workspace = new_workspace;
  blackbox->netwm().setWMDesktop(client.window, client.ewmh.workspace);
}


bool BlackboxWindow::setInputFocus(void) {
  if (!isVisible())
    return false;
  if (client.state.focused)
    return true;

  const bt::Rect &scr = _screen->screenInfo().rect();
  if (!frame.rect.intersects(scr)) {
    // client is outside the screen, move it to the center
    configure(scr.x() + (scr.width() - frame.rect.width()) / 2,
              scr.y() + (scr.height() - frame.rect.height()) / 2,
              frame.rect.width(), frame.rect.height());
  }

  /*
    pass focus to any modal transients, giving modal group transients
    higher priority
  */
  BWindowGroup *group = findWindowGroup();
  if (group && !group->transients().empty()) {
    BlackboxWindowList::const_iterator it = group->transients().begin(),
                                      end = group->transients().end();
    for (; it != end; ++it) {
      BlackboxWindow * const tmp = *it;
      if (!tmp->isVisible() || !tmp->isModal())
        continue;
      if (tmp == this) {
        // we are the newest modal group transient
        break;
      }
      if (isTransient()) {
        if (tmp == findNonTransientParent()) {
          // we are a transient of the modal group transient
          break;
        }
      }
      return tmp->setInputFocus();
    }
  }

  if (!client.transientList.empty()) {
    BlackboxWindowList::const_iterator it = client.transientList.begin(),
                                      end = client.transientList.end();
    for (; it != end; ++it) {
      BlackboxWindow * const tmp = *it;
      if (tmp->isVisible() && tmp->isModal())
        return tmp->setInputFocus();
    }
  }

  XSetInputFocus(blackbox->XDisplay(), client.window,
                 RevertToPointerRoot, blackbox->XTime());

  if (client.wmprotocols.wm_take_focus) {
    XEvent ce;
    ce.xclient.type = ClientMessage;
    ce.xclient.message_type = blackbox->wmProtocolsAtom();
    ce.xclient.display = blackbox->XDisplay();
    ce.xclient.window = client.window;
    ce.xclient.format = 32;
    ce.xclient.data.l[0] = blackbox->wmTakeFocusAtom();
    ce.xclient.data.l[1] = blackbox->XTime();
    ce.xclient.data.l[2] = 0l;
    ce.xclient.data.l[3] = 0l;
    ce.xclient.data.l[4] = 0l;
    XSendEvent(blackbox->XDisplay(), client.window, False, NoEventMask, &ce);
  }

  return true;
}


void BlackboxWindow::show(void) {
  if (client.state.visible)
    return;

  if (client.state.iconic)
    _screen->removeIcon(this);

  client.state.iconic = false;
  client.state.visible = true;
  setState(isShaded() ? IconicState : NormalState);

  XMapWindow(blackbox->XDisplay(), client.window);
  XMapSubwindows(blackbox->XDisplay(), frame.window);
  XMapWindow(blackbox->XDisplay(), frame.window);

  if (!client.transientList.empty()) {
    BlackboxWindowList::iterator it = client.transientList.begin(),
                                end = client.transientList.end();
    for (; it != end; ++it)
      (*it)->show();
  }

#ifdef DEBUG
  int real_x, real_y;
  Window child;
  XTranslateCoordinates(blackbox->XDisplay(), client.window,
                        _screen->screenInfo().rootWindow(),
                        0, 0, &real_x, &real_y, &child);
  fprintf(stderr, "%s -- assumed: (%d, %d), real: (%d, %d)\n", title().c_str(),
          client.rect.left(), client.rect.top(), real_x, real_y);
  assert(client.rect.left() == real_x && client.rect.top() == real_y);
#endif
}


void BlackboxWindow::hide(void) {
  if (!client.state.visible) return;

  client.state.visible = false;
  setState(client.state.iconic ? IconicState : client.current_state);

  XUnmapWindow(blackbox->XDisplay(), frame.window);

  /*
   * we don't want this XUnmapWindow call to generate an UnmapNotify
   * event, so we need to clear the event mask on client.window for a
   * split second.  HOWEVER, since X11 is asynchronous, the window
   * could be destroyed in that split second, leaving us with a ghost
   * window... so, we need to do this while the X server is grabbed
   */
  unsigned long event_mask = PropertyChangeMask | StructureNotifyMask;
  blackbox->XGrabServer();
  XSelectInput(blackbox->XDisplay(), client.window,
               event_mask & ~StructureNotifyMask);
  XUnmapWindow(blackbox->XDisplay(), client.window);
  XSelectInput(blackbox->XDisplay(), client.window, event_mask);
  blackbox->XUngrabServer();
}


void BlackboxWindow::close(void) {
  assert(hasWindowFunction(WindowFunctionClose));

  XEvent ce;
  ce.xclient.type = ClientMessage;
  ce.xclient.message_type = blackbox->wmProtocolsAtom();
  ce.xclient.display = blackbox->XDisplay();
  ce.xclient.window = client.window;
  ce.xclient.format = 32;
  ce.xclient.data.l[0] = blackbox->wmDeleteWindowAtom();
  ce.xclient.data.l[1] = blackbox->XTime();
  ce.xclient.data.l[2] = 0l;
  ce.xclient.data.l[3] = 0l;
  ce.xclient.data.l[4] = 0l;
  XSendEvent(blackbox->XDisplay(), client.window, False, NoEventMask, &ce);
}


void BlackboxWindow::activate(void) {
  if (workspace() != bt::BSENTINEL
      && workspace() != _screen->currentWorkspace())
    _screen->setCurrentWorkspace(workspace());
  if (client.state.iconic)
    show();
  if (client.ewmh.shaded)
    setShaded(false);
  if (setInputFocus())
    _screen->raiseWindow(this);
}


void BlackboxWindow::iconify(void) {
  assert(hasWindowFunction(WindowFunctionIconify));

  if (client.state.iconic) return;

  if (isTransient()) {
    BlackboxWindow *win = findTransientFor();
    if (win) {
      if (!win->isIconic())
        win->iconify();
    }
  }

  _screen->addIcon(this);

  client.state.iconic = true;
  hide();

  // iconify all transients first
  if (!client.transientList.empty()) {
    BlackboxWindowList::iterator it = client.transientList.begin(),
                                end = client.transientList.end();
    for (; it != end; ++it)
      (*it)->iconify();
  }
}


void BlackboxWindow::maximize(unsigned int button) {
  assert(hasWindowFunction(WindowFunctionMaximize));

  // any maximize operation always unshades
  client.ewmh.shaded = false;

  if (isMaximized()) {
    client.ewmh.maxh = client.ewmh.maxv = false;

    if (!isFullScreen()) {
      /*
        when a resize is begun, maximize(0) is called to clear any
        maximization flags currently set.  Otherwise it still thinks
        it is maximized.  so we do not need to call configure()
        because resizing will handle it
      */
      if (! client.state.resizing)
        configure(client.premax);

      redrawAllButtons(); // in case it is not called in configure()
    }

    setState(client.current_state);
    return;
  }

  switch (button) {
  case 1:
    client.ewmh.maxh = true;
    client.ewmh.maxv = true;
    break;

  case 2:
    client.ewmh.maxh = false;
    client.ewmh.maxv = true;
    break;

  case 3:
    client.ewmh.maxh = true;
    client.ewmh.maxv = false;
    break;

  default:
    assert(0);
    break;
  }

  if (!isFullScreen()) {
    frame.changing = _screen->availableArea();

    upsize();
    client.premax = frame.rect;

    if (!client.ewmh.maxh) {
      frame.changing.setX(client.premax.x());
      frame.changing.setWidth(client.premax.width());
    }
    if (!client.ewmh.maxv) {
      frame.changing.setY(client.premax.y());
      frame.changing.setHeight(client.premax.height());
    }

    constrain(TopLeft);

    frame.rect = bt::Rect(); // trick configure into working
    configure(frame.changing);
    redrawAllButtons(); // in case it is not called in configure()
  }

  setState(client.current_state);
}


// re-maximizes the window to take into account availableArea changes
void BlackboxWindow::remaximize(void) {
  if (isShaded()) return;

  unsigned int button = 0u;
  if (client.ewmh.maxv) {
    button = (client.ewmh.maxh) ? 1u : 2u;
  } else if (client.ewmh.maxh) {
    button = (client.ewmh.maxv) ? 1u : 3u;
  }

  // trick maximize() into working
  client.ewmh.maxh = client.ewmh.maxv = false;

  const bt::Rect tmp = client.premax;
  maximize(button);
  client.premax = tmp;
}


void BlackboxWindow::setShaded(bool shaded) {
  assert(hasWindowFunction(WindowFunctionShade));

  if (!!client.ewmh.shaded == !!shaded)
    return;

  client.ewmh.shaded = shaded;
  if (!isShaded()) {
    if (isMaximized()) {
      remaximize();
    } else {
      // set the frame rect to the normal size
      frame.rect.setHeight(client.rect.height() + frame.margin.top +
                           frame.margin.bottom);

      XResizeWindow(blackbox->XDisplay(), frame.window,
                    frame.rect.width(), frame.rect.height());
    }

    setState(NormalState);
  } else {
    // set the frame rect to the shaded size
    frame.rect.setHeight(frame.style->title_height);

    XResizeWindow(blackbox->XDisplay(), frame.window,
                  frame.rect.width(), frame.rect.height());

    setState(IconicState);
  }
}


void BlackboxWindow::setFullScreen(bool b) {
  assert(hasWindowFunction(WindowFunctionFullScreen));

  if (client.ewmh.fullscreen == b)
    return;

  bool refocus = isFocused();
  client.ewmh.fullscreen = b;
  if (isFullScreen()) {
    client.decorations = NoWindowDecorations;
    client.functions &= ~(WindowFunctionMove |
                          WindowFunctionResize |
                          WindowFunctionShade |
                          WindowFunctionChangeLayer);

    if (!isMaximized())
      client.premax = frame.rect;

    upsize();
    frame.rect = bt::Rect(); // trick configure() into working

    frame.changing = _screen->screenInfo().rect();
    constrain(TopLeft);
    configure(frame.changing);
    if (isVisible())
      _screen->changeLayer(this, StackingList::LayerFullScreen);
    setState(client.current_state);
  } else {
    ::update_decorations(client.decorations,
                         client.functions,
                         isTransient(),
                         client.ewmh,
                         client.motif,
                         client.wmnormal,
                         client.wmprotocols);

    if (client.decorations & WindowDecorationTitlebar)
      createTitlebar();
    if (client.decorations & WindowDecorationHandle)
      createHandle();

    upsize();
    frame.rect = bt::Rect(); // trick configure() into working

    if (!isMaximized()) {
      configure(client.premax);
      if (isVisible())
        _screen->changeLayer(this, StackingList::LayerNormal);
      setState(client.current_state);
    } else {
      if (isVisible())
        _screen->changeLayer(this, StackingList::LayerNormal);
      remaximize();
    }
  }

  ungrabButtons();
  grabButtons();

  if (refocus)
    (void) setInputFocus();
}


void BlackboxWindow::redrawWindowFrame(void) const {
  if (client.decorations & WindowDecorationTitlebar) {
    redrawTitle();
    redrawLabel();
    redrawAllButtons();
  }

  if (client.decorations & WindowDecorationHandle) {
    redrawHandle();

    if (client.decorations & WindowDecorationGrip)
      redrawGrips();
  }
}


void BlackboxWindow::setFocused(bool focused) {
  if (focused == client.state.focused)
    return;

  client.state.focused = isVisible() ? focused : false;

  if (isVisible()) {
    redrawWindowFrame();

    if (client.state.focused) {
      XInstallColormap(blackbox->XDisplay(), client.colormap);
    } else {
      if (client.ewmh.fullscreen && layer() != StackingList::LayerBelow)
        _screen->changeLayer(this, StackingList::LayerBelow);
    }
  }
}


void BlackboxWindow::setState(unsigned long new_state) {
  client.current_state = new_state;

  unsigned long state[2];
  state[0] = client.current_state;
  state[1] = None;
  XChangeProperty(blackbox->XDisplay(), client.window,
                  blackbox->wmStateAtom(), blackbox->wmStateAtom(), 32,
                  PropModeReplace, (unsigned char *) state, 2);

  const bt::Netwm& netwm = blackbox->netwm();

  // set _NET_WM_STATE
  bt::Netwm::AtomList atoms;
  if (isModal())
    atoms.push_back(netwm.wmStateModal());
  if (isShaded())
    atoms.push_back(netwm.wmStateShaded());
  if (isIconic())
    atoms.push_back(netwm.wmStateHidden());
  if (isFullScreen())
    atoms.push_back(netwm.wmStateFullscreen());
  if (client.ewmh.maxh)
    atoms.push_back(netwm.wmStateMaximizedHorz());
  if (client.ewmh.maxv)
    atoms.push_back(netwm.wmStateMaximizedVert());
  if (client.ewmh.skip_taskbar)
    atoms.push_back(netwm.wmStateSkipTaskbar());
  if (client.ewmh.skip_pager)
    atoms.push_back(netwm.wmStateSkipPager());

  switch (layer()) {
  case StackingList::LayerAbove:
    atoms.push_back(netwm.wmStateAbove());
    break;
  case StackingList::LayerBelow:
    atoms.push_back(netwm.wmStateBelow());
    break;
  default:
    break;
  }

  if (atoms.empty())
    netwm.removeProperty(client.window, netwm.wmState());
  else
    netwm.setWMState(client.window, atoms);

  // set _NET_WM_ALLOWED_ACTIONS
  atoms.clear();

  if (! client.state.iconic) {
    if (hasWindowFunction(WindowFunctionChangeWorkspace))
      atoms.push_back(netwm.wmActionChangeDesktop());

    if (hasWindowFunction(WindowFunctionIconify))
      atoms.push_back(netwm.wmActionMinimize());

    if (hasWindowFunction(WindowFunctionShade))
      atoms.push_back(netwm.wmActionShade());

    if (hasWindowFunction(WindowFunctionMove))
      atoms.push_back(netwm.wmActionMove());

    if (hasWindowFunction(WindowFunctionResize))
      atoms.push_back(netwm.wmActionResize());

    if (hasWindowFunction(WindowFunctionMaximize)) {
      atoms.push_back(netwm.wmActionMaximizeHorz());
      atoms.push_back(netwm.wmActionMaximizeVert());
    }

    atoms.push_back(netwm.wmActionFullscreen());
  }

  if (hasWindowFunction(WindowFunctionClose))
    atoms.push_back(netwm.wmActionClose());

  netwm.setWMAllowedActions(client.window, atoms);
}


bool BlackboxWindow::readState(void) {
  client.current_state = NormalState;

  Atom atom_return;
  bool ret = False;
  int foo;
  unsigned long *state, ulfoo, nitems;

  if ((XGetWindowProperty(blackbox->XDisplay(), client.window,
                          blackbox->wmStateAtom(),
                          0l, 2l, False, blackbox->wmStateAtom(),
                          &atom_return, &foo, &nitems, &ulfoo,
                          (unsigned char **) &state) != Success) ||
      (! state)) {
    return False;
  }

  if (nitems >= 1) {
    client.current_state = static_cast<unsigned long>(state[0]);
    ret = True;
  }

  XFree((void *) state);

  return ret;
}


/*
 *
 */
void BlackboxWindow::clearState(void) {
  XDeleteProperty(blackbox->XDisplay(), client.window,
                  blackbox->wmStateAtom());

  const bt::Netwm& netwm = blackbox->netwm();
  netwm.removeProperty(client.window, netwm.wmDesktop());
  netwm.removeProperty(client.window, netwm.wmState());
  netwm.removeProperty(client.window, netwm.wmAllowedActions());
  netwm.removeProperty(client.window, netwm.wmVisibleName());
  netwm.removeProperty(client.window, netwm.wmVisibleIconName());
}



/*
 * Positions the bt::Rect r according the the client window position and
 * window gravity.
 */
void BlackboxWindow::applyGravity(bt::Rect &r) {
  // apply horizontal window gravity
  switch (client.wmnormal.win_gravity) {
  default:
  case NorthWestGravity:
  case SouthWestGravity:
  case WestGravity:
    r.setX(client.rect.x());
    break;

  case NorthGravity:
  case SouthGravity:
  case CenterGravity:
    r.setX(client.rect.x() - (frame.margin.left + frame.margin.right) / 2);
    break;

  case NorthEastGravity:
  case SouthEastGravity:
  case EastGravity:
    r.setX(client.rect.x() - (frame.margin.left + frame.margin.right) + 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setX(client.rect.x() - frame.margin.left);
    break;
  }

  // apply vertical window gravity
  switch (client.wmnormal.win_gravity) {
  default:
  case NorthWestGravity:
  case NorthEastGravity:
  case NorthGravity:
    r.setY(client.rect.y());
    break;

  case CenterGravity:
  case EastGravity:
  case WestGravity:
    r.setY(client.rect.y() - ((frame.margin.top + frame.margin.bottom) / 2));
    break;

  case SouthWestGravity:
  case SouthEastGravity:
  case SouthGravity:
    r.setY(client.rect.y() - (frame.margin.bottom + frame.margin.top) + 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setY(client.rect.y() - frame.margin.top);
    break;
  }
}


/*
 * The reverse of the applyGravity function.
 *
 * Positions the bt::Rect r according to the frame window position and
 * window gravity.
 */
void BlackboxWindow::restoreGravity(bt::Rect &r) {
  // restore horizontal window gravity
  switch (client.wmnormal.win_gravity) {
  default:
  case NorthWestGravity:
  case SouthWestGravity:
  case WestGravity:
    r.setX(frame.rect.x());
    break;

  case NorthGravity:
  case SouthGravity:
  case CenterGravity:
    r.setX(frame.rect.x() + (frame.margin.left + frame.margin.right) / 2);
    break;

  case NorthEastGravity:
  case SouthEastGravity:
  case EastGravity:
    r.setX(frame.rect.x() + (frame.margin.left + frame.margin.right) - 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setX(frame.rect.x() + frame.margin.left);
    break;
  }

  // restore vertical window gravity
  switch (client.wmnormal.win_gravity) {
  default:
  case NorthWestGravity:
  case NorthEastGravity:
  case NorthGravity:
    r.setY(frame.rect.y());
    break;

  case CenterGravity:
  case EastGravity:
  case WestGravity:
    r.setY(frame.rect.y() + (frame.margin.top + frame.margin.bottom) / 2);
    break;

  case SouthWestGravity:
  case SouthEastGravity:
  case SouthGravity:
    r.setY(frame.rect.y() + (frame.margin.top + frame.margin.bottom) - 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setY(frame.rect.y() + frame.margin.top);
    break;
  }
}


void BlackboxWindow::redrawTitle(void) const {
  const bt::Rect u(0, 0, frame.rect.width(), frame.style->title_height);
  bt::drawTexture(_screen->screenNumber(),
                  (client.state.focused
                   ? frame.style->focus.title
                   : frame.style->unfocus.title),
                  frame.title, u, u,
                  (client.state.focused
                   ? frame.ftitle
                   : frame.utitle));
}


void BlackboxWindow::redrawLabel(void) const {
  bt::Rect u(0, 0, frame.label_w, frame.style->label_height);
  Pixmap p = (client.state.focused ? frame.flabel : frame.ulabel);
  if (p == ParentRelative) {
    const bt::Texture &texture =
      (isFocused() ? frame.style->focus.title : frame.style->unfocus.title);
    int offset = texture.borderWidth();
    if (client.decorations & WindowDecorationIconify)
      offset += frame.style->button_width + frame.style->title_margin;

    const bt::Rect t(-(frame.style->title_margin + offset),
                     -(frame.style->title_margin + texture.borderWidth()),
                     frame.rect.width(), frame.style->title_height);
    bt::drawTexture(_screen->screenNumber(), texture, frame.label, t, u,
                    (client.state.focused ? frame.ftitle : frame.utitle));
  } else {
    bt::drawTexture(_screen->screenNumber(),
                    (client.state.focused
                     ? frame.style->focus.label
                     : frame.style->unfocus.label),
                    frame.label, u, u, p);
  }

  const bt::Pen pen(_screen->screenNumber(),
                    ((client.state.focused)
                     ? frame.style->focus.text
                     : frame.style->unfocus.text));
  u.setCoords(u.left()  + frame.style->label_margin,
              u.top() + frame.style->label_margin,
              u.right() - frame.style->label_margin,
              u.bottom() - frame.style->label_margin);
  bt::drawText(frame.style->font, pen, frame.label, u,
               frame.style->alignment, client.visible_title);
}


void BlackboxWindow::redrawAllButtons(void) const {
  if (frame.iconify_button) redrawIconifyButton();
  if (frame.maximize_button) redrawMaximizeButton();
  if (frame.close_button) redrawCloseButton();
}


void BlackboxWindow::redrawIconifyButton(bool pressed) const {
  const bt::Rect u(0, 0, frame.style->button_width, frame.style->button_width);
  Pixmap p = (pressed ? frame.pbutton :
              (client.state.focused ? frame.fbutton : frame.ubutton));
  if (p == ParentRelative) {
    const bt::Texture &texture =
      (isFocused() ? frame.style->focus.title : frame.style->unfocus.title);
    const bt::Rect t(-(frame.style->title_margin + texture.borderWidth()),
                     -(frame.style->title_margin + texture.borderWidth()),
                     frame.rect.width(), frame.style->title_height);
    bt::drawTexture(_screen->screenNumber(), texture, frame.iconify_button,
                    t, u, (client.state.focused
                           ? frame.ftitle
                           : frame.utitle));
  } else {
    bt::drawTexture(_screen->screenNumber(),
                    (pressed ? frame.style->pressed :
                     (client.state.focused ? frame.style->focus.button :
                      frame.style->unfocus.button)),
                    frame.iconify_button, u, u, p);
  }

  const bt::Pen pen(_screen->screenNumber(),
                    (client.state.focused
                     ? frame.style->focus.foreground
                     : frame.style->unfocus.foreground));
  bt::drawBitmap(frame.style->iconify, pen, frame.iconify_button, u);
}


void BlackboxWindow::redrawMaximizeButton(bool pressed) const {
  const bt::Rect u(0, 0, frame.style->button_width, frame.style->button_width);
  Pixmap p = (pressed ? frame.pbutton :
              (client.state.focused ? frame.fbutton : frame.ubutton));
  if (p == ParentRelative) {
    const bt::Texture &texture =
      (isFocused() ? frame.style->focus.title : frame.style->unfocus.title);
    int button_w = frame.style->button_width
                   + frame.style->title_margin + texture.borderWidth();
    if (client.decorations & WindowDecorationClose)
      button_w *= 2;
    const bt::Rect t(-(frame.rect.width() - button_w),
                     -(frame.style->title_margin + texture.borderWidth()),
                     frame.rect.width(), frame.style->title_height);
    bt::drawTexture(_screen->screenNumber(), texture, frame.maximize_button,
                    t, u, (client.state.focused
                           ? frame.ftitle
                           : frame.utitle));
  } else {
    bt::drawTexture(_screen->screenNumber(),
                    (pressed ? frame.style->pressed :
                     (client.state.focused ? frame.style->focus.button :
                      frame.style->unfocus.button)),
                    frame.maximize_button, u, u, p);
  }

  const bt::Pen pen(_screen->screenNumber(),
                    (client.state.focused
                     ? frame.style->focus.foreground
                     : frame.style->unfocus.foreground));
  bt::drawBitmap(isMaximized() ? frame.style->restore : frame.style->maximize,
                 pen, frame.maximize_button, u);
}


void BlackboxWindow::redrawCloseButton(bool pressed) const {
  const bt::Rect u(0, 0, frame.style->button_width, frame.style->button_width);
  Pixmap p = (pressed ? frame.pbutton :
              (client.state.focused ? frame.fbutton : frame.ubutton));
  if (p == ParentRelative) {
    const bt::Texture &texture =
      (isFocused() ? frame.style->focus.title : frame.style->unfocus.title);
    const int button_w = frame.style->button_width +
                         frame.style->title_margin +
                         texture.borderWidth();
    const bt::Rect t(-(frame.rect.width() - button_w),
                     -(frame.style->title_margin + texture.borderWidth()),
                     frame.rect.width(), frame.style->title_height);
    bt::drawTexture(_screen->screenNumber(),texture, frame.close_button, t, u,
                    (client.state.focused ? frame.ftitle : frame.utitle));
  } else {
    bt::drawTexture(_screen->screenNumber(),
                    (pressed ? frame.style->pressed :
                     (client.state.focused ? frame.style->focus.button :
                      frame.style->unfocus.button)),
                    frame.close_button, u, u, p);
  }

  const bt::Pen pen(_screen->screenNumber(),
                    (client.state.focused
                     ? frame.style->focus.foreground
                     : frame.style->unfocus.foreground));
  bt::drawBitmap(frame.style->close, pen, frame.close_button, u);
}


void BlackboxWindow::redrawHandle(void) const {
  const bt::Rect u(0, 0, frame.rect.width(), frame.style->handle_height);
  bt::drawTexture(_screen->screenNumber(),
                  (client.state.focused ? frame.style->focus.handle :
                                          frame.style->unfocus.handle),
                  frame.handle, u, u,
                  (client.state.focused ? frame.fhandle : frame.uhandle));
}


void BlackboxWindow::redrawGrips(void) const {
  const bt::Rect u(0, 0, frame.style->grip_width, frame.style->handle_height);
  Pixmap p = (client.state.focused ? frame.fgrip : frame.ugrip);
  if (p == ParentRelative) {
    bt::Rect t(0, 0, frame.rect.width(), frame.style->handle_height);
    bt::drawTexture(_screen->screenNumber(),
                    (client.state.focused ? frame.style->focus.handle :
                                            frame.style->unfocus.handle),
                    frame.right_grip, t, u, p);

    t.setPos(-(frame.rect.width() - frame.style->grip_width), 0);
    bt::drawTexture(_screen->screenNumber(),
                    (client.state.focused ? frame.style->focus.handle :
                                            frame.style->unfocus.handle),
                    frame.right_grip, t, u, p);
  } else {
    bt::drawTexture(_screen->screenNumber(),
                    (client.state.focused ? frame.style->focus.grip :
                                            frame.style->unfocus.grip),
                    frame.left_grip, u, u, p);

    bt::drawTexture(_screen->screenNumber(),
                    (client.state.focused ? frame.style->focus.grip :
                                            frame.style->unfocus.grip),
                    frame.right_grip, u, u, p);
  }
}


void
BlackboxWindow::clientMessageEvent(const XClientMessageEvent * const event) {
  if (event->format != 32)
    return;

  const bt::Netwm& netwm = blackbox->netwm();

  if (event->message_type == blackbox->wmChangeStateAtom()) {
    if (event->data.l[0] == IconicState) {
      if (hasWindowFunction(WindowFunctionIconify))
        iconify();
    } else if (event->data.l[0] == NormalState) {
      activate();
    }
  } else if (event->message_type == netwm.activeWindow()) {
    activate();
  } else if (event->message_type == netwm.closeWindow()) {
    if (hasWindowFunction(WindowFunctionClose))
      close();
  } else if (event->message_type == netwm.moveresizeWindow()) {
    XConfigureRequestEvent request;
    request.window = event->window;
    request.x = event->data.l[1];
    request.y = event->data.l[2];
    request.width = event->data.l[3];
    request.height = event->data.l[4];
    request.value_mask = CWX | CWY | CWWidth | CWHeight;

    const int old_gravity = client.wmnormal.win_gravity;
    if (event->data.l[0] != 0)
      client.wmnormal.win_gravity = event->data.l[0];

    configureRequestEvent(&request);

    client.wmnormal.win_gravity = old_gravity;
  } else if (event->message_type == netwm.wmDesktop()) {
    if (hasWindowFunction(WindowFunctionChangeWorkspace)) {
      const unsigned int desktop = event->data.l[0];
      setWorkspace(desktop);

      if (isVisible() && workspace() != bt::BSENTINEL
          && workspace() != _screen->currentWorkspace()) {
        hide();
      } else if (!isVisible()
                 && (workspace() == bt::BSENTINEL
                     || workspace() == _screen->currentWorkspace())) {
        show();
      }
    }
  } else if (event->message_type == netwm.wmState()) {
    Atom action = event->data.l[0],
          first = event->data.l[1],
         second = event->data.l[2];

    if (first == netwm.wmStateModal() || second == netwm.wmStateModal()) {
      if ((action == netwm.wmStateAdd() ||
           (action == netwm.wmStateToggle() && ! client.ewmh.modal)) &&
          isTransient())
        client.ewmh.modal = true;
      else
        client.ewmh.modal = false;
    }

    if (hasWindowFunction(WindowFunctionMaximize)) {
      int max_horz = 0, max_vert = 0;

      if (first == netwm.wmStateMaximizedHorz() ||
          second == netwm.wmStateMaximizedHorz()) {
        max_horz = ((action == netwm.wmStateAdd()
                     || (action == netwm.wmStateToggle()
                         && !client.ewmh.maxh))
                    ? 1 : -1);
      }

      if (first == netwm.wmStateMaximizedVert() ||
          second == netwm.wmStateMaximizedVert()) {
        max_vert = ((action == netwm.wmStateAdd()
                     || (action == netwm.wmStateToggle()
                         && !client.ewmh.maxv))
                    ? 1 : -1);
      }

      if (max_horz != 0 || max_vert != 0) {
        if (isMaximized())
          maximize(0);
        unsigned int button = 0u;
        if (max_horz == 1 && max_vert != 1)
          button = 3u;
        else if (max_vert == 1 && max_horz != 1)
          button = 2u;
        else if (max_vert == 1 && max_horz == 1)
          button = 1u;
        if (button)
          maximize(button);
      }
    }

    if (hasWindowFunction(WindowFunctionShade)) {
      if (first == netwm.wmStateShaded() ||
          second == netwm.wmStateShaded()) {
        if (action == netwm.wmStateRemove())
          setShaded(false);
        else if (action == netwm.wmStateAdd())
          setShaded(true);
        else if (action == netwm.wmStateToggle())
          setShaded(!isShaded());
      }
    }

    if (first == netwm.wmStateSkipTaskbar()
        || second == netwm.wmStateSkipTaskbar()
        || first == netwm.wmStateSkipPager()
        || second == netwm.wmStateSkipPager()) {
      if (first == netwm.wmStateSkipTaskbar()
          || second == netwm.wmStateSkipTaskbar()) {
        client.ewmh.skip_taskbar = (action == netwm.wmStateAdd()
                                    || (action == netwm.wmStateToggle()
                                        && !client.ewmh.skip_taskbar));
      }
      if (first == netwm.wmStateSkipPager()
          || second == netwm.wmStateSkipPager()) {
        client.ewmh.skip_pager = (action == netwm.wmStateAdd()
                                  || (action == netwm.wmStateToggle()
                                      && !client.ewmh.skip_pager));
      }
      // we do nothing with skip_*, but others might... we should at
      // least make sure these are present in _NET_WM_STATE
      setState(client.current_state);
    }

    if (first == netwm.wmStateHidden() ||
        second == netwm.wmStateHidden()) {
      /*
        ignore _NET_WM_STATE_HIDDEN, the wm sets this state, not the
        application
      */
    }

    if (hasWindowFunction(WindowFunctionFullScreen)) {
      if (first == netwm.wmStateFullscreen() ||
          second == netwm.wmStateFullscreen()) {
        if (action == netwm.wmStateAdd() ||
            (action == netwm.wmStateToggle() &&
             ! client.ewmh.fullscreen)) {
          setFullScreen(true);
        } else if (action == netwm.wmStateToggle() ||
                   action == netwm.wmStateRemove()) {
          setFullScreen(false);
        }
      }
    }

    if (hasWindowFunction(WindowFunctionChangeLayer)) {
      if (first == netwm.wmStateAbove() ||
          second == netwm.wmStateAbove()) {
        if (action == netwm.wmStateAdd() ||
            (action == netwm.wmStateToggle() &&
             layer() != StackingList::LayerAbove)) {
          _screen->changeLayer(this, StackingList::LayerAbove);
        } else if (action == netwm.wmStateToggle() ||
                   action == netwm.wmStateRemove()) {
          _screen->changeLayer(this, StackingList::LayerNormal);
        }
      }

      if (first == netwm.wmStateBelow() ||
          second == netwm.wmStateBelow()) {
        if (action == netwm.wmStateAdd() ||
            (action == netwm.wmStateToggle() &&
             layer() != StackingList::LayerBelow)) {
          _screen->changeLayer(this, StackingList::LayerBelow);
        } else if (action == netwm.wmStateToggle() ||
                   action == netwm.wmStateRemove()) {
          _screen->changeLayer(this, StackingList::LayerNormal);
        }
      }
    }
  }
}


void BlackboxWindow::unmapNotifyEvent(const XUnmapEvent * const event) {
  if (event->window != client.window)
    return;

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::unmapNotifyEvent() for 0x%lx\n",
          client.window);
#endif // DEBUG

  _screen->releaseWindow(this);
}


void
BlackboxWindow::destroyNotifyEvent(const XDestroyWindowEvent * const event) {
  if (event->window != client.window)
    return;

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::destroyNotifyEvent() for 0x%lx\n",
          client.window);
#endif // DEBUG

  _screen->releaseWindow(this);
}


void BlackboxWindow::reparentNotifyEvent(const XReparentEvent * const event) {
  if (event->window != client.window || event->parent == frame.plate)
    return;

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::reparentNotifyEvent(): reparent 0x%lx to "
          "0x%lx.\n", client.window, event->parent);
#endif // DEBUG

  /*
    put the ReparentNotify event back into the queue so that
    BlackboxWindow::restore(void) can do the right thing
  */
  XEvent replay;
  replay.xreparent = *event;
  XPutBackEvent(blackbox->XDisplay(), &replay);

  _screen->releaseWindow(this);
}


void BlackboxWindow::propertyNotifyEvent(const XPropertyEvent * const event) {
#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::propertyNotifyEvent(): for 0x%lx\n",
          client.window);
#endif

  switch(event->atom) {
  case XA_WM_TRANSIENT_FOR: {
    if (isTransient()) {
      // remove ourselves from our transient_for
      BlackboxWindow *win = findTransientFor();
      if (win)
        win->removeTransient(this);
    }

    // determine if this is a transient window
    client.transient_for = readTransientInfo();
    if (isTransient()) {
      BlackboxWindow *win = findTransientFor();
      if (win) {
        // add ourselves to our new transient_for
        win->addTransient(this);

        if (workspace() != win->workspace())
          setWorkspace(win->workspace());

        if (isVisible() && workspace() != bt::BSENTINEL
            && workspace() != _screen->currentWorkspace()) {
          hide();
        } else if (!isVisible()
                   && (workspace() == bt::BSENTINEL
                       || workspace() == _screen->currentWorkspace())) {
          show();
        }
      }
    }

    ::update_decorations(client.decorations,
                         client.functions,
                         isTransient(),
                         client.ewmh,
                         client.motif,
                         client.wmnormal,
                         client.wmprotocols);

    reconfigure();

    break;
  }

  case XA_WM_HINTS: {
    // remove from current window group
    BWindowGroup *group = findWindowGroup();
    if (group)
      group->removeWindow(this);

    client.wmhints = readWMHints();

    if (client.wmhints.window_group != None)
      ::update_window_group(client.wmhints.window_group, blackbox, this);

    break;
  }

  case XA_WM_ICON_NAME: {
    client.icon_title = readWMIconName();
    if (client.state.iconic)
      _screen->propagateWindowName(this);
    break;
  }

  case XA_WM_NAME: {
    client.title = readWMName();

    client.visible_title =
      bt::ellideText(client.title, frame.label_w, bt::toUnicode("..."),
                     _screen->screenNumber(), frame.style->font);
    blackbox->netwm().setWMVisibleName(client.window, client.visible_title);

    if (client.decorations & WindowDecorationTitlebar)
      redrawLabel();

    _screen->propagateWindowName(this);

    break;
  }

  case XA_WM_NORMAL_HINTS: {
    client.wmnormal = readWMNormalHints();

    if ((client.wmnormal.flags & (PMinSize|PMaxSize)) == (PMinSize|PMaxSize)) {
      /*
        The window now can/cannot resize itself, so the buttons need
        to be regrabbed and the decorations updated.
      */
      ungrabButtons();

      ::update_decorations(client.decorations,
                           client.functions,
                           isTransient(),
                           client.ewmh,
                           client.motif,
                           client.wmnormal,
                           client.wmprotocols);

      // update frame.rect based on the new decorations
      upsize();

      grabButtons();
    }

    /*
      Update the current geometry by constraining it (the current
      geometry) based on the information from the property.
    */
    frame.changing = frame.rect;
    constrain(TopLeft);

    if (frame.rect != frame.changing)
      configure(frame.changing);

    break;
  }

  default: {
    if (event->atom == blackbox->wmProtocolsAtom()) {
      client.wmprotocols = readWMProtocols();

      if (client.wmprotocols.wm_delete_window
          && hasWindowDecoration(WindowDecorationTitlebar)) {
        client.decorations |= WindowDecorationClose;
        client.functions   |= WindowFunctionClose;

        if (!frame.close_button) {
          createCloseButton();
          positionButtons(True);
        }
      }
    } else if (event->atom == blackbox->motifWmHintsAtom()) {
      client.motif = readMotifHints();

      ::update_decorations(client.decorations,
                           client.functions,
                           isTransient(),
                           client.ewmh,
                           client.motif,
                           client.wmnormal,
                           client.wmprotocols);

      reconfigure();
    } else if (event->atom == blackbox->netwm().wmStrut()) {
      if (! client.strut) {
        client.strut = new bt::Netwm::Strut;
        _screen->addStrut(client.strut);
      }

      blackbox->netwm().readWMStrut(client.window, client.strut);
      if (client.strut->left || client.strut->right ||
          client.strut->top || client.strut->bottom) {
        _screen->updateStrut();
      } else {
        _screen->removeStrut(client.strut);
        delete client.strut;
      }
    }

    break;
  }
  } // switch
}


void BlackboxWindow::exposeEvent(const XExposeEvent * const event) {
#ifdef DEBUG
  fprintf(stderr, "BlackboxWindow::exposeEvent() for 0x%lx\n", client.window);
#endif

  if (frame.title == event->window)
    redrawTitle();
  else if (frame.label == event->window)
    redrawLabel();
  else if (frame.close_button == event->window)
    redrawCloseButton();
  else if (frame.maximize_button == event->window)
    redrawMaximizeButton();
  else if (frame.iconify_button == event->window)
    redrawIconifyButton();
  else if (frame.handle == event->window)
    redrawHandle();
  else if (frame.left_grip == event->window ||
           frame.right_grip == event->window)
    redrawGrips();
}


void BlackboxWindow::configureRequestEvent(const XConfigureRequestEvent *
                                           const event) {
  if (event->window != client.window || client.state.iconic)
    return;

  if (event->value_mask & CWBorderWidth)
    client.old_bw = event->border_width;

  if (event->value_mask & (CWX | CWY | CWWidth | CWHeight)) {
    bt::Rect req = frame.rect;

    if (event->value_mask & (CWX | CWY)) {
      restoreGravity(client.rect);
      if (event->value_mask & CWX)
        client.rect.setX(event->x);
      if (event->value_mask & CWY)
        client.rect.setY(event->y);
      applyGravity(req);
    }

    if (event->value_mask & (CWWidth | CWHeight)) {
      if (event->value_mask & CWWidth)
        req.setWidth(event->width + frame.margin.left + frame.margin.right);
      if (event->value_mask & CWHeight)
        req.setHeight(event->height + frame.margin.top + frame.margin.bottom);
    }

    configure(req);
  }

  if (event->value_mask & CWStackMode) {
    switch (event->detail) {
    case Below:
    case BottomIf:
      _screen->lowerWindow(this);
      break;

    case Above:
    case TopIf:
    default:
      _screen->raiseWindow(this);
      break;
    }
  }
}


void BlackboxWindow::buttonPressEvent(const XButtonEvent * const event) {
#ifdef DEBUG
  fprintf(stderr, "BlackboxWindow::buttonPressEvent() for 0x%lx\n",
          client.window);
#endif

  if (frame.maximize_button == event->window) {
    if (event->button < 4)
      redrawMaximizeButton(true);
  } else if (frame.iconify_button == event->window) {
    if (event->button == 1)
      redrawIconifyButton(true);
  } else if (frame.close_button == event->window) {
    if (event->button == 1)
      redrawCloseButton(true);
  } else {
    if (event->button == 1
        || (event->button == 3 && event->state == Mod1Mask)) {
      frame.grab_x = event->x_root - frame.rect.x();
      frame.grab_y = event->y_root - frame.rect.y();

      _screen->raiseWindow(this);

      if (! client.state.focused)
        (void) setInputFocus();
      else
        XInstallColormap(blackbox->XDisplay(), client.colormap);

      if (frame.plate == event->window) {
        XAllowEvents(blackbox->XDisplay(), ReplayPointer, event->time);
      } else if (frame.title == event->window
                 || frame.label == event->window
                 && hasWindowFunction(WindowFunctionShade)) {
        if ((event->time - lastButtonPressTime <=
             blackbox->resource().doubleClickInterval()) ||
            event->state == ControlMask) {
          lastButtonPressTime = 0;
          setShaded(!isShaded());
        } else {
          lastButtonPressTime = event->time;
        }
      }
    } else if (event->button == 2) {
      _screen->lowerWindow(this);
    } else if (event->button == 3) {
      const int extra = frame.style->frame_border_width;
      const bt::Rect rect(client.rect.x() - extra,
                          client.rect.y() - extra,
                          client.rect.width() + (extra * 2),
                          client.rect.height() + (extra * 2));

      Windowmenu *windowmenu = _screen->windowmenu(this);
      windowmenu->popup(event->x_root, event->y_root, rect);
    }
  }
}


void BlackboxWindow::buttonReleaseEvent(const XButtonEvent * const event) {
#ifdef DEBUG
  fprintf(stderr, "BlackboxWindow::buttonReleaseEvent() for 0x%lx\n",
          client.window);
#endif

  if (event->window == frame.maximize_button) {
    if (event->button < 4) {
      if (bt::within(event->x, event->y,
                     frame.style->button_width, frame.style->button_width)) {
        maximize(event->button);
        _screen->raiseWindow(this);
      } else {
        redrawMaximizeButton();
      }
    }
  } else if (event->window == frame.iconify_button) {
    if (event->button == 1) {
      if (bt::within(event->x, event->y,
                     frame.style->button_width, frame.style->button_width))
        iconify();
      else
        redrawIconifyButton();
    }
  } else if (event->window == frame.close_button) {
    if (event->button == 1) {
      if (bt::within(event->x, event->y,
                     frame.style->button_width, frame.style->button_width))
        close();
      redrawCloseButton();
    }
  } else if (client.state.moving) {
    client.state.moving = False;

    if (! _screen->resource().doOpaqueMove()) {
      /* when drawing the rubber band, we need to make sure we only
       * draw inside the frame... frame.changing_* contain the new
       * coords for the window, so we need to subtract 1 from
       * changing_w/changing_h every where we draw the rubber band
       * (for both moving and resizing)
       */
      bt::Pen pen(_screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
      const int bw = frame.style->frame_border_width, hw = bw / 2;
      pen.setGCFunction(GXxor);
      pen.setLineWidth(bw);
      pen.setSubWindowMode(IncludeInferiors);
      XDrawRectangle(blackbox->XDisplay(), _screen->screenInfo().rootWindow(),
                     pen.gc(),
                     frame.changing.x() + hw,
                     frame.changing.y() + hw,
                     frame.changing.width() - bw,
                     frame.changing.height() - bw);
      blackbox->XUngrabServer();

      configure(frame.changing.x(), frame.changing.y(),
                frame.changing.width(), frame.changing.height());
    } else {
      configure(frame.rect.x(), frame.rect.y(),
                frame.rect.width(), frame.rect.height());
    }
    _screen->hideGeometry();
    XUngrabPointer(blackbox->XDisplay(), blackbox->XTime());
  } else if (client.state.resizing) {
    bt::Pen pen(_screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
    const int bw = frame.style->frame_border_width, hw = bw / 2;
    pen.setGCFunction(GXxor);
    pen.setLineWidth(bw);
    pen.setSubWindowMode(IncludeInferiors);
    XDrawRectangle(blackbox->XDisplay(), _screen->screenInfo().rootWindow(),
                   pen.gc(),
                   frame.changing.x() + hw,
                   frame.changing.y() + hw,
                   frame.changing.width() - bw,
                   frame.changing.height() - bw);
    blackbox->XUngrabServer();

    _screen->hideGeometry();

    constrain((event->window == frame.left_grip) ? TopRight : TopLeft);

    // unset maximized state when resized after fully maximized
    if (isMaximized())
      maximize(0);
    client.state.resizing = False;
    configure(frame.changing.x(), frame.changing.y(),
              frame.changing.width(), frame.changing.height());

    XUngrabPointer(blackbox->XDisplay(), blackbox->XTime());
  } else if (event->window == frame.window) {
    if (event->button == 2 && event->state == Mod1Mask)
      XUngrabPointer(blackbox->XDisplay(), blackbox->XTime());
  }
}


static
void collisionAdjust(int* x, int* y, unsigned int width, unsigned int height,
                     const bt::Rect& rect, int snap_distance) {
  // window corners
  const int wleft = *x,
           wright = *x + width - 1,
             wtop = *y,
          wbottom = *y + height - 1,

            dleft = abs(wleft - rect.left()),
           dright = abs(wright - rect.right()),
             dtop = abs(wtop - rect.top()),
          dbottom = abs(wbottom - rect.bottom());

  // snap left?
  if (dleft < snap_distance && dleft <= dright)
    *x = rect.left();
  // snap right?
  else if (dright < snap_distance)
    *x = rect.right() - width + 1;

  // snap top?
  if (dtop < snap_distance && dtop <= dbottom)
    *y = rect.top();
  // snap bottom?
  else if (dbottom < snap_distance)
    *y = rect.bottom() - height + 1;
}


void BlackboxWindow::motionNotifyEvent(const XMotionEvent * const event) {
#ifdef DEBUG
  fprintf(stderr, "BlackboxWindow::motionNotifyEvent() for 0x%lx\n",
          client.window);
#endif

  if (hasWindowFunction(WindowFunctionMove) && ! client.state.resizing &&
      event->state & Button1Mask &&
      (frame.title == event->window || frame.label == event->window ||
       frame.handle == event->window || frame.window == event->window)) {
    if (! client.state.moving) {
      // begin a move
      XGrabPointer(blackbox->XDisplay(), event->window, False,
                   Button1MotionMask | ButtonReleaseMask,
                   GrabModeAsync, GrabModeAsync,
                   None, blackbox->resource().moveCursor(), blackbox->XTime());

      client.state.moving = True;

      if (! _screen->resource().doOpaqueMove()) {
        blackbox->XGrabServer();

        frame.changing = frame.rect;
        _screen->showGeometry(BScreen::Position, frame.changing);

        bt::Pen pen(_screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
        const int bw = frame.style->frame_border_width, hw = bw / 2;
        pen.setGCFunction(GXxor);
        pen.setLineWidth(bw);
        pen.setSubWindowMode(IncludeInferiors);
        XDrawRectangle(blackbox->XDisplay(), _screen->screenInfo().rootWindow(),
                       pen.gc(),
                       frame.changing.x() + hw,
                       frame.changing.y() + hw,
                       frame.changing.width() - bw,
                       frame.changing.height() - bw);
      }
    } else {
      // continue a move
      int dx = event->x_root - frame.grab_x, dy = event->y_root - frame.grab_y;

      const int snap_distance = _screen->resource().edgeSnapThreshold();

      if (snap_distance) {
        collisionAdjust(&dx, &dy, frame.rect.width(), frame.rect.height(),
                        _screen->availableArea(), snap_distance);
        if (! _screen->resource().doFullMax())
          collisionAdjust(&dx, &dy, frame.rect.width(), frame.rect.height(),
                          _screen->screenInfo().rect(), snap_distance);
      }

      if (_screen->resource().doOpaqueMove()) {
        configure(dx, dy, frame.rect.width(), frame.rect.height());
      } else {
        bt::Pen pen(_screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
        const int bw = frame.style->frame_border_width, hw = bw / 2;
        pen.setGCFunction(GXxor);
        pen.setLineWidth(bw);
        pen.setSubWindowMode(IncludeInferiors);
        XDrawRectangle(blackbox->XDisplay(), _screen->screenInfo().rootWindow(),
                       pen.gc(),
                       frame.changing.x() + hw,
                       frame.changing.y() + hw,
                       frame.changing.width() - bw,
                       frame.changing.height() - bw);

        frame.changing.setPos(dx, dy);

        XDrawRectangle(blackbox->XDisplay(), _screen->screenInfo().rootWindow(),
                       pen.gc(),
                       frame.changing.x() + hw,
                       frame.changing.y() + hw,
                       frame.changing.width() - bw,
                       frame.changing.height() - bw);
      }

      _screen->showGeometry(BScreen::Position, bt::Rect(dx, dy, 0, 0));
    }
  } else if (hasWindowFunction(WindowFunctionResize) &&
             (event->state & Button1Mask &&
              (event->window == frame.right_grip ||
               event->window == frame.left_grip)) ||
             (event->state & Button3Mask && event->state & Mod1Mask &&
              event->window == frame.window)) {
    bool left = (event->window == frame.left_grip);

    if (! client.state.resizing) {
      // begin a resize
      blackbox->XGrabServer();
      XGrabPointer(blackbox->XDisplay(), event->window, False,
                   ButtonMotionMask | ButtonReleaseMask,
                   GrabModeAsync, GrabModeAsync, None,
                   ((left) ? blackbox->resource().resizeBottomLeftCursor() :
                    blackbox->resource().resizeBottomRightCursor()),
                   blackbox->XTime());

      client.state.resizing = True;

      frame.grab_x = event->x;
      frame.grab_y = event->y;
      frame.changing = frame.rect;

      constrain(left ? TopRight : TopLeft);

      bt::Pen pen(_screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
      const int bw = frame.style->frame_border_width, hw = bw / 2;
      pen.setGCFunction(GXxor);
      pen.setLineWidth(bw);
      pen.setSubWindowMode(IncludeInferiors);
      XDrawRectangle(blackbox->XDisplay(), _screen->screenInfo().rootWindow(),
                     pen.gc(),
                     frame.changing.x() + hw,
                     frame.changing.y() + hw,
                     frame.changing.width() - bw,
                     frame.changing.height() - bw);

      showGeometry(frame.changing);
    } else {
      // continue a resize
      const bt::Rect curr = frame.changing;

      if (left) {
        int delta =
          std::min<signed>(event->x_root - frame.grab_x,
                           frame.rect.right() -
                           (frame.margin.left + frame.margin.right + 1));
        frame.changing.setCoords(delta, frame.rect.top(),
                                 frame.rect.right(), frame.rect.bottom());
      } else {
        int nw = std::max<signed>(event->x - frame.grab_x + frame.rect.width(),
                                  frame.margin.left + frame.margin.right + 1);
        frame.changing.setWidth(nw);
      }

      int nh = std::max<signed>(event->y - frame.grab_y + frame.rect.height(),
                                frame.margin.top + frame.margin.bottom + 1);
      frame.changing.setHeight(nh);

      constrain(left ? TopRight : TopLeft);

      if (curr != frame.changing) {
        bt::Pen pen(_screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
        const int bw = frame.style->frame_border_width, hw = bw / 2;
        pen.setGCFunction(GXxor);
        pen.setLineWidth(bw);
        pen.setSubWindowMode(IncludeInferiors);
        XDrawRectangle(blackbox->XDisplay(), _screen->screenInfo().rootWindow(),
                       pen.gc(),
                       curr.x() + hw,
                       curr.y() + hw,
                       curr.width() - bw,
                       curr.height() - bw);

        XDrawRectangle(blackbox->XDisplay(), _screen->screenInfo().rootWindow(),
                       pen.gc(),
                       frame.changing.x() + hw,
                       frame.changing.y() + hw,
                       frame.changing.width() - bw,
                       frame.changing.height() - bw);

        showGeometry(frame.changing);
      }
    }
  }
}


void BlackboxWindow::enterNotifyEvent(const XCrossingEvent * const event) {
  if (event->window != frame.window || event->mode != NotifyNormal)
    return;

  if (!_screen->resource().isSloppyFocus() || !isVisible())
    return;

  switch (windowType()) {
  case WindowTypeDesktop:
  case WindowTypeDock:
    // these types cannot be focused w/ sloppy focus
    return;

  default:
    break;
  }

  XEvent next;
  bool leave = False, inferior = False;

  while (XCheckTypedWindowEvent(blackbox->XDisplay(), event->window,
                                LeaveNotify, &next)) {
    if (next.type == LeaveNotify && next.xcrossing.mode == NotifyNormal) {
      leave = True;
      inferior = (next.xcrossing.detail == NotifyInferior);
    }
  }

  if ((! leave || inferior) && ! isFocused())
    (void) setInputFocus();

  if (_screen->resource().doAutoRaise())
    timer->start();
}


void
BlackboxWindow::leaveNotifyEvent(const XCrossingEvent * const /*unused*/) {
  if (! (_screen->resource().isSloppyFocus() &&
         _screen->resource().doAutoRaise()))
    return;

  if (timer->isTiming())
    timer->stop();
}


#ifdef    SHAPE
void BlackboxWindow::shapeEvent(const XEvent * const /*unused*/)
{ if (client.state.shaped) configureShape(); }
#endif // SHAPE


/*
 *
 */
void BlackboxWindow::restore(void) {
  XChangeSaveSet(blackbox->XDisplay(), client.window, SetModeDelete);
  XSelectInput(blackbox->XDisplay(), client.window, NoEventMask);
  XSelectInput(blackbox->XDisplay(), frame.plate, NoEventMask);

  client.state.visible = false;

  /*
    remove WM_STATE unless the we are shutting down (in which case we
    want to make sure we preserve the state across restarts).
  */
  if (!blackbox->shuttingDown()) {
    clearState();
  } else if (isShaded() && !isIconic()) {
    // do not leave a shaded window as an icon unless it was an icon
    setState(NormalState);
  }

  restoreGravity(client.rect);

  blackbox->XGrabServer();

  XUnmapWindow(blackbox->XDisplay(), frame.window);
  XUnmapWindow(blackbox->XDisplay(), client.window);

  XSetWindowBorderWidth(blackbox->XDisplay(), client.window, client.old_bw);
  if (isMaximized()) {
    // preserve the original size
    XMoveResizeWindow(blackbox->XDisplay(), client.window,
                      client.rect.x() - frame.rect.x(),
                      client.rect.y() - frame.rect.y(),
                      client.premax.width() - (frame.margin.left
                                               + frame.margin.right),
                      client.premax.height() - (frame.margin.top
                                                + frame.margin.bottom));
  } else {
    XMoveWindow(blackbox->XDisplay(), client.window,
                client.rect.x() - frame.rect.x(),
                client.rect.y() - frame.rect.y());
  }

  blackbox->XUngrabServer();

  XEvent unused;
  if (!XCheckTypedWindowEvent(blackbox->XDisplay(), client.window,
                              ReparentNotify, &unused)) {
    /*
      according to the ICCCM, the window manager is responsible for
      reparenting the window back to root... however, we don't want to
      do this if the window has been reparented by someone else
      (i.e. not us).
    */
    XReparentWindow(blackbox->XDisplay(), client.window,
                    _screen->screenInfo().rootWindow(),
                    client.rect.x(), client.rect.y());
  }

  if (blackbox->shuttingDown())
    XMapWindow(blackbox->XDisplay(), client.window);
}


// timer for autoraise
void BlackboxWindow::timeout(bt::Timer *)
{ _screen->raiseWindow(this); }


/*
 * Set the sizes of all components of the window frame
 * (the window decorations).
 * These values are based upon the current style settings and the client
 * window's dimensions.
 */
void BlackboxWindow::upsize(void) {
  const unsigned int bw = (hasWindowDecoration(WindowDecorationBorder)
                           ? frame.style->frame_border_width
                           : 0);

  frame.margin.top = frame.margin.bottom =
    frame.margin.left = frame.margin.right = bw;

  if (client.decorations & WindowDecorationTitlebar)
    frame.margin.top += frame.style->title_height - bw;

  if (client.decorations & WindowDecorationHandle)
    frame.margin.bottom += frame.style->handle_height - bw;

  /*
    We first get the normal dimensions and use this to define the
    width/height then we modify the height if shading is in effect.
    If the shade state is not considered then frame.rect gets reset to
    the normal window size on a reconfigure() call resulting in
    improper dimensions appearing in move/resize and other events.
  */
  unsigned int
    height = client.rect.height() + frame.margin.top + frame.margin.bottom,
    width = client.rect.width() + frame.margin.left + frame.margin.right;

  if (isShaded())
    height = frame.style->title_height;
  frame.rect.setSize(width, height);
}

/*
 * show the geometry of the window based on rectangle r.
 * The logical width and height are used here.  This refers to the user's
 * perception of the window size (for example an xterm resizes in cells,
 * not in pixels).  No extra work is needed if there is no difference between
 * the logical and actual dimensions.
 */
void BlackboxWindow::showGeometry(const bt::Rect &r) const {
  unsigned int w = r.width(), h = r.height();

  // remove the window frame
  w -= frame.margin.left + frame.margin.right;
  h -= frame.margin.top + frame.margin.bottom;

  if (client.wmnormal.flags & PResizeInc) {
    if (client.wmnormal.flags & (PMinSize|PBaseSize)) {
      w -= ((client.wmnormal.base_width)
            ? client.wmnormal.base_width
            : client.wmnormal.min_width);
      h -= ((client.wmnormal.base_height)
            ? client.wmnormal.base_height
            : client.wmnormal.min_height);
    }

    w /= client.wmnormal.width_inc;
    h /= client.wmnormal.height_inc;
  }

  _screen->showGeometry(BScreen::Size, bt::Rect(0, 0, w, h));
}


/*
 * Calculate the size of the client window and constrain it to the
 * size specified by the size hints of the client window.
 *
 * The physical geometry is placed into frame.changing.  Physical
 * geometry refers to the geometry of the window in pixels.
 */
void BlackboxWindow::constrain(Corner anchor) {
  // frame.changing represents the requested frame size, we need to
  // strip the frame margin off and constrain the client size
  frame.changing.
    setCoords(frame.changing.left() + static_cast<signed>(frame.margin.left),
              frame.changing.top() + static_cast<signed>(frame.margin.top),
              frame.changing.right() - static_cast<signed>(frame.margin.right),
              frame.changing.bottom() -
              static_cast<signed>(frame.margin.bottom));

  unsigned int dw = frame.changing.width(), dh = frame.changing.height();
  const unsigned int base_width = ((client.wmnormal.base_width)
                                   ? client.wmnormal.base_width
                                   : client.wmnormal.min_width),
                    base_height = ((client.wmnormal.base_height)
                                   ? client.wmnormal.base_height
                                   : client.wmnormal.min_height);

  // constrain to min and max sizes
  if (dw < client.wmnormal.min_width) dw = client.wmnormal.min_width;
  if (dh < client.wmnormal.min_height) dh = client.wmnormal.min_height;
  if (dw > client.wmnormal.max_width) dw = client.wmnormal.max_width;
  if (dh > client.wmnormal.max_height) dh = client.wmnormal.max_height;

  assert(dw >= base_width && dh >= base_height);

  // fit to size increments
  if (client.wmnormal.flags & PResizeInc) {
    dw = (((dw - base_width) / client.wmnormal.width_inc)
          * client.wmnormal.width_inc) + base_width;
    dh = (((dh - base_height) / client.wmnormal.height_inc)
          * client.wmnormal.height_inc) + base_height;
  }

  /*
   * honor aspect ratios (based on twm which is based on uwm)
   *
   * The math looks like this:
   *
   * minAspectX    dwidth     maxAspectX
   * ---------- <= ------- <= ----------
   * minAspectY    dheight    maxAspectY
   *
   * If that is multiplied out, then the width and height are
   * invalid in the following situations:
   *
   * minAspectX * dheight > minAspectY * dwidth
   * maxAspectX * dheight < maxAspectY * dwidth
   *
   */
  if (client.wmnormal.flags & PAspect) {
    unsigned int delta;
    const unsigned int min_asp_x = client.wmnormal.min_aspect_x,
                       min_asp_y = client.wmnormal.min_aspect_y,
                       max_asp_x = client.wmnormal.max_aspect_x,
                       max_asp_y = client.wmnormal.max_aspect_y,
                       w_inc = client.wmnormal.width_inc,
                       h_inc = client.wmnormal.height_inc;
    if (min_asp_x * dh > min_asp_y * dw) {
      delta = ((min_asp_x * dh / min_asp_y - dw) * w_inc) / w_inc;
      if (dw + delta <= client.wmnormal.max_width) {
        dw += delta;
      } else {
        delta = ((dh - (dw * min_asp_y) / min_asp_x) * h_inc) / h_inc;
        if (dh - delta >= client.wmnormal.min_height) dh -= delta;
      }
    }
    if (max_asp_x * dh < max_asp_y * dw) {
      delta = ((max_asp_y * dw / max_asp_x - dh) * h_inc) / h_inc;
      if (dh + delta <= client.wmnormal.max_height) {
        dh += delta;
      } else {
        delta = ((dw - (dh * max_asp_x) / max_asp_y) * w_inc) / w_inc;
        if (dw - delta >= client.wmnormal.min_width) dw -= delta;
      }
    }
  }

  frame.changing.setSize(dw, dh);

  // add the frame margin back onto frame.changing
  frame.changing.setCoords(frame.changing.left() - frame.margin.left,
                           frame.changing.top() - frame.margin.top,
                           frame.changing.right() + frame.margin.right,
                           frame.changing.bottom() + frame.margin.bottom);

  // move frame.changing to the specified anchor
  int dx = frame.rect.right() - frame.changing.right();
  int dy = frame.rect.bottom() - frame.changing.bottom();

  switch (anchor) {
  case TopLeft:
    // nothing to do
    break;

  case TopRight:
    frame.changing.setPos(frame.changing.x() + dx, frame.changing.y());
    break;

  case BottomLeft:
    frame.changing.setPos(frame.changing.x(), frame.changing.y() + dy);
    break;

  case BottomRight:
    frame.changing.setPos(frame.changing.x() + dx, frame.changing.y() + dy);
    break;
  }
}

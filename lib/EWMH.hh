// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// EWMH.hh for Blackbox - an X11 Window manager
// Copyright (c) 2001 - 2005 Sean 'Shaleh' Perry <shaleh@debian.org>
// Copyright (c) 1997 - 2000, 2002 - 2005
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

#ifndef __EWMH_hh
#define __EWMH_hh

#include "Display.hh"
#include "Unicode.hh"
#include "Util.hh"

#include <string>
#include <vector>

namespace bt {

  class EWMH: public NoCopy {
  public:
    explicit EWMH(const Display &_display);

    typedef std::vector<Atom> AtomList;
    typedef std::vector<Window> WindowList;

    struct Strut {
      unsigned int left, right, top, bottom;
      inline Strut(void)
        : left(0), right(0), top(0), bottom(0)
      { }
    };

    struct StrutPartial {
      unsigned int left, right, top, bottom;
      unsigned int   left_start,   left_end;
      unsigned int  right_start,  right_end;
      unsigned int    top_start,    top_end;
      unsigned int bottom_start, bottom_end;
      inline StrutPartial(void)
        : left(0u), right(0u), top(0u), bottom(0u),
          left_start(0u),   left_end(0u),
          right_start(0u),  right_end(0u),
          top_start(0u),    top_end(0u),
          bottom_start(0u), bottom_end(0u)
      { }
    };

    inline Atom utf8String(void) const
    { return utf8_string; }

    // root window properties
    inline Atom supported(void) const
    { return net_supported; }
    inline Atom clientList(void) const
    { return net_client_list; }
    inline Atom clientListStacking(void) const
    { return net_client_list_stacking; }
    inline Atom numberOfDesktops(void) const
    { return net_number_of_desktops; }
    inline Atom desktopGeometry(void) const
    { return net_desktop_geometry; }
    inline Atom desktopViewport(void) const
    { return net_desktop_viewport; }
    inline Atom currentDesktop(void) const
    { return net_current_desktop; }
    inline Atom desktopNames(void) const
    { return net_desktop_names; }
    inline Atom activeWindow(void) const
    { return net_active_window; }
    inline Atom workarea(void) const
    { return net_workarea; }
    inline Atom supportingWMCheck(void) const
    { return net_supporting_wm_check; }
    inline Atom virtualRoots(void) const
    { return net_virtual_roots; }
    inline Atom desktopLayout(void) const
    { return net_desktop_layout; }
    inline Atom showingDesktop(void) const
    { return net_showing_desktop; }
    inline Atom wmFullPlacement(void) const
    { return net_wm_full_placement; }

    void setSupported(Window target, Atom atoms[], unsigned int count) const;
    bool readSupported(Window target, AtomList& atoms) const;
    void setClientList(Window target, WindowList& windows) const;
    bool readClientList(Window target, WindowList& windows) const;
    void setClientListStacking(Window target, WindowList& windows) const;
    bool readClientListStacking(Window target, WindowList& windows) const;
    void setNumberOfDesktops(Window target, unsigned int count) const;
    bool readNumberOfDesktops(Window target, unsigned int* number) const;
    void setDesktopGeometry(Window target,
                            unsigned int width, unsigned int height) const;
    bool readDesktopGeometry(Window target,
                             unsigned int* width, unsigned int* height) const;
    void setDesktopViewport(Window target, int x, int y) const;
    bool readDesktopViewport(Window target, int *x, int *y) const;
    void setCurrentDesktop(Window target, unsigned int number) const;
    bool readCurrentDesktop(Window target, unsigned int* number) const;
    void setDesktopNames(Window target,
                         const std::vector<bt::ustring> &names) const;
    bool readDesktopNames(Window target,
                          std::vector<bt::ustring> &names) const;
    void setShowingDesktop(Window target, bool flag) const;
    void setActiveWindow(Window target, Window data) const;
    void setWorkarea(Window target, unsigned long workareas[],
                     unsigned int count) const;
    void setSupportingWMCheck(Window target, Window data) const;
    bool readSupportingWMCheck(Window target, Window* window) const;
    void setVirtualRoots(Window target, WindowList &windows) const;
    bool readVirtualRoots(Window target, WindowList &windows) const;
    // void setDesktopLayout(Window target, ...) const;
    // void readDesktopLayout(Window target, ...) const;

    // other root messages
    inline Atom closeWindow(void) const
    { return net_close_window; }
    inline Atom moveresizeWindow(void) const
    { return net_moveresize_window; }
    inline Atom wmMoveResize(void) const
    { return net_wm_moveresize; }
    inline Atom restackWindow(void) const
    { return net_restack_window; }
    inline Atom requestFrameExtents(void) const
    { return net_request_frame_extents; }
    inline Atom startupID(void) const
    { return net_startup_id; }
    inline Atom startupInfo(void) const
    { return net_startup_info; }
    inline Atom startupInfoBegin(void) const
    { return net_startup_info_begin; }
    inline Atom systemTrayMessageData(void) const
    { return net_system_tray_message_data; }
    inline Atom systemTrayOpcode(void) const
    { return net_system_tray_opcode; }
    inline Atom systemTrayOrientation(void) const
    { return net_system_tray_orientation; }
    inline Atom systemtrayVisual(void) const
    { return net_system_tray_visual; }

    // application properties
    inline Atom wmName(void) const
    { return net_wm_name; }
    inline Atom wmVisibleName(void) const
    { return net_wm_visible_name; }
    inline Atom wmIconName(void) const
    { return net_wm_icon_name; }
    inline Atom wmVisibleIconName(void) const
    { return net_wm_visible_icon_name; }
    inline Atom wmDesktop(void) const
    { return net_wm_desktop; }

    inline Atom wmWindowType(void) const
    { return net_wm_window_type; }
    inline Atom wmWindowTypeCombo(void) const
    { return net_wm_window_type_combo; }
    inline Atom wmWindowTypeDesktop(void) const
    { return net_wm_window_type_desktop; }
    inline Atom wmWindowTypeDialog(void) const
    { return net_wm_window_type_dialog; }
    inline Atom wmWindowTypeDnd(void) const
    { return net_wm_window_type_dnd; }
    inline Atom wmWindowTypeDock(void) const
    { return net_wm_window_type_dock; }
    inline Atom wmWindowTypeDropdownMenu(void) const
    { return net_wm_window_type_dropdown_menu; }
    inline Atom wmWindowTypeMenu(void) const
    { return net_wm_window_type_menu; }
    inline Atom wmWindowTypeNormal(void) const
    { return net_wm_window_type_normal; }
    inline Atom wmWindowTypeNotification(void) const
    { return net_wm_window_type_notification; }
    inline Atom wmWindowTypePopupMenu(void) const
    { return net_wm_window_type_popup_menu; }
    inline Atom wmWindowTypeSplash(void) const
    { return net_wm_window_type_splash; }
    inline Atom wmWindowTypeToolbar(void) const
    { return net_wm_window_type_toolbar; }
    inline Atom wmWindowTypeTooltip(void) const
    { return net_wm_window_type_tooltip; }
    inline Atom wmWindowTypeUtility(void) const
    { return net_wm_window_type_utility; }

    inline Atom wmState(void) const
    { return net_wm_state; }
    inline Atom wmStateModal(void) const
    { return net_wm_state_modal; }
    inline Atom wmStateSticky(void) const
    { return net_wm_state_sticky; }
    inline Atom wmStateMaximizedVert(void) const
    { return net_wm_state_maximized_vert; }
    inline Atom wmStateMaximizedHorz(void) const
    { return net_wm_state_maximized_horz; }
    inline Atom wmStateShaded(void) const
    { return net_wm_state_shaded; }
    inline Atom wmStateSkipTaskbar(void) const
    { return net_wm_state_skip_taskbar; }
    inline Atom wmStateSkipPager(void) const
    { return net_wm_state_skip_pager; }
    inline Atom wmStateHidden(void) const
    { return net_wm_state_hidden; }
    inline Atom wmStateFullscreen(void) const
    { return net_wm_state_fullscreen; }
    inline Atom wmStateAbove(void) const
    { return net_wm_state_above; }
    inline Atom wmStateBelow(void) const
    { return net_wm_state_below; }
    inline Atom wmStateDemandsAttention(void) const
    { return net_wm_state_demands_attention; }
    inline Atom wmStateFocused(void) const
    { return net_wm_state_focused; }
    inline Atom wmStateRemove(void) const
    { return 0; }
    inline Atom wmStateAdd(void) const
    { return 1; }
    inline Atom wmStateToggle(void) const
    { return 2; }

    inline Atom wmAllowedActions(void) const
    { return net_wm_allowed_actions; }
    inline Atom wmActionAbove(void) const
    { return net_wm_action_above; }
    inline Atom wmActionBelow(void) const
    { return net_wm_action_below; }
    inline Atom wmActionChangeDesktop(void) const
    { return net_wm_action_change_desktop; }
    inline Atom wmActionClose(void) const
    { return net_wm_action_close; }
    inline Atom wmActionFullscreen(void) const
    { return net_wm_action_fullscreen; }
    inline Atom wmActionMaximizeHorz(void) const
    { return net_wm_action_maximize_horz; }
    inline Atom wmActionMaximizeVert(void) const
    { return net_wm_action_maximize_vert; }
    inline Atom wmActionMinimize(void) const
    { return net_wm_action_minimize; }
    inline Atom wmActionMove(void) const
    { return net_wm_action_move; }
    inline Atom wmActionResize(void) const
    { return net_wm_action_resize; }
    inline Atom wmActionShade(void) const
    { return net_wm_action_shade; }
    inline Atom wmActionStick(void) const
    { return net_wm_action_stick; }

    inline Atom wmStrut(void) const
    { return net_wm_strut; }
    inline Atom wmStrutPartial(void) const
    { return net_wm_strut_partial; }
    inline Atom wmIconGeometry(void) const
    { return net_wm_icon_geometry; }
    inline Atom wmIcon(void) const
    { return net_wm_icon; }
    inline Atom wmPid(void) const
    { return net_wm_pid; }
    inline Atom wmHandledIcons(void) const
    { return net_wm_handled_icons; }
    inline Atom wmUserTime(void) const
    { return net_wm_user_time; }
    inline Atom wmUserTimeWindow(void) const
    { return net_wm_user_time_window; }
    inline Atom wmFrameExtents(void) const
    { return net_frame_extents; }
    inline Atom wmOpaqueRegion(void) const
    { return net_wm_opaque_region; }
    inline Atom wmBypassCompositor(void) const
    { return net_wm_bypass_compositor; }
    inline Atom wmWindowOpacity(void) const
    { return net_wm_window_opacity; }

    void setWMName(Window target, const bt::ustring &name) const;
    bool readWMName(Window target, bt::ustring &name) const;
    void setWMVisibleName(Window target, const bt::ustring &name) const;
    bool readWMIconName(Window target, bt::ustring &name) const;
    void setWMVisibleIconName(Window target, const bt::ustring &name) const;
    void setWMDesktop(Window target, unsigned int desktop) const;
    bool readWMDesktop(Window target, unsigned int& desktop) const;
    bool readWMWindowType(Window target, AtomList& types) const;
    void setWMState(Window target, AtomList& atoms) const;
    bool readWMState(Window target, AtomList& states) const;
    void setWMAllowedActions(Window target, AtomList& atoms) const;
    bool readWMStrut(Window target, Strut* strut) const;
    bool readWMStrutPartial(Window target, StrutPartial* strut) const;
    bool readWMIconGeometry(Window target, int &x, int &y,
                            unsigned int &width, unsigned int &height) const;
    // bool readWMIcon(Window target, ...) const;
    void setWMPid(Window target, unsigned long pid) const;
    bool readWMPid(Window target, unsigned int &pid) const;
    // bool readWMHandledIcons(Window target, ...) const;
    bool readWMUserTime(Window target, Time &user_time) const;
    bool readWMUserTimeWindow(Window target, Window &window) const;
    // void readFrameExtents(Window target, ...) const;
    void setFrameExtents(Window target, Strut& borders) const;
    bool readStartupID(Window target, bt::ustring &id) const;

    // Window Manager Protocols
    inline Atom wmPing(void) const
    { return net_wm_ping; }
    inline Atom wmSyncRequest(void) const
    { return net_wm_sync_request; }
    inline Atom wmSyncRequestCounter(void) const
    { return net_wm_sync_request_counter; }
    inline Atom wmFullscreenMonitors(void) const
    { return net_wm_fullscreen_monitors; }

    // utility
    void removeProperty(Window target, Atom atom) const;
    bool isSupportedWMWindowType(Atom atom) const;

    void setProperty(Window target, Atom type, Atom property,
                     const unsigned char *data, unsigned long count) const;

    bool getProperty(Window target, Atom type, Atom property,
                     unsigned char** data) const;
    bool getListProperty(Window target, Atom type, Atom property,
                         unsigned char** data, unsigned long* count) const;

  private:
    const Display &display;
    Atom utf8_string,
      net_active_window,
      net_client_list,
      net_client_list_stacking,
      net_close_window,
      net_current_desktop,
      net_desktop_geometry,
      net_desktop_layout,
      net_desktop_names,
      net_desktop_viewport,
      net_frame_extents,
      net_moveresize_window,
      net_number_of_desktops,
      net_request_frame_extents,
      net_restack_window,
      net_showing_desktop,
      net_startup_id,
      net_startup_info,
      net_startup_info_begin,
      net_supported,
      net_supporting_wm_check,
      net_system_tray_message_data,
      net_system_tray_opcode,
      net_system_tray_orientation,
      net_system_tray_visual,
      net_virtual_roots,
      net_wm_action_above,
      net_wm_action_below,
      net_wm_action_change_desktop,
      net_wm_action_close,
      net_wm_action_fullscreen,
      net_wm_action_maximize_horz,
      net_wm_action_maximize_vert,
      net_wm_action_minimize,
      net_wm_action_move,
      net_wm_action_resize,
      net_wm_action_shade,
      net_wm_action_stick,
      net_wm_allowed_actions,
      net_wm_bypass_compositor,
      net_wm_desktop,
      net_wm_full_placement,
      net_wm_fullscreen_monitors,
      net_wm_handled_icons,
      net_wm_icon,
      net_wm_icon_geometry,
      net_wm_icon_name,
      net_wm_moveresize,
      net_wm_name,
      net_wm_opaque_region,
      net_wm_pid,
      net_wm_ping,
      net_wm_state,
      net_wm_state_above,
      net_wm_state_below,
      net_wm_state_demands_attention,
      net_wm_state_focused,
      net_wm_state_fullscreen,
      net_wm_state_hidden,
      net_wm_state_maximized_horz,
      net_wm_state_maximized_vert,
      net_wm_state_modal,
      net_wm_state_shaded,
      net_wm_state_skip_pager,
      net_wm_state_skip_taskbar,
      net_wm_state_sticky,
      net_wm_strut,
      net_wm_strut_partial,
      net_wm_sync_request,
      net_wm_sync_request_counter,
      net_wm_user_time,
      net_wm_user_time_window,
      net_wm_visible_icon_name,
      net_wm_visible_name,
      net_wm_window_opacity,
      net_wm_window_type,
      net_wm_window_type_combo,
      net_wm_window_type_desktop,
      net_wm_window_type_dialog,
      net_wm_window_type_dnd,
      net_wm_window_type_dock,
      net_wm_window_type_dropdown_menu,
      net_wm_window_type_menu,
      net_wm_window_type_normal,
      net_wm_window_type_notification,
      net_wm_window_type_popup_menu,
      net_wm_window_type_splash,
      net_wm_window_type_toolbar,
      net_wm_window_type_tooltip,
      net_wm_window_type_utility,
      net_workarea;
  };

} // namespace bt

#endif // __EWMH_hh

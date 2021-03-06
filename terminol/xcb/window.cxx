// vi:noai:sw=4

#include "terminol/xcb/window.hxx"
#include "terminol/support/conv.hxx"
#include "terminol/support/pattern.hxx"

#include <xcb/xcb_icccm.h>
#include <xcb/xcb_aux.h>
#include <pango/pangocairo.h>

#include <limits>

#include <unistd.h>

// TODO consolidate this function
#define xcb_request_failed(connection, cookie, err_msg) _xcb_request_failed(connection, cookie, err_msg, __LINE__)
namespace {

bool _xcb_request_failed(xcb_connection_t * connection, xcb_void_cookie_t cookie,
                         const char * err_msg, int line) {
    auto error = xcb_request_check(connection, cookie);
    if (error) {
        std::cerr
            << __FILE__ << ':' << line << ' ' << err_msg
            << " (X Error Code: " << static_cast<int>(error->error_code) << ')'
            << std::endl;
        std::free(error);
        return true;
    }
    else {
        return false;
    }
}

} // namespace {anonymous}

Window::Window(I_Observer         & observer,
               const Config       & config,
               I_Selector         & selector,
               I_Deduper          & deduper,
               Basics             & basics,
               const ColorSet     & colorSet,
               FontManager        & fontManager,
               const Tty::Command & command) throw (Error) :
    _observer(observer),
    _config(config),
    _basics(basics),
    _colorSet(colorSet),
    _fontManager(fontManager),
    _fontSet(nullptr),
    _window(0),
    _destroyed(false),
    _gc(0),
    _width(0),
    _height(0),
    _terminal(nullptr),
    _open(false),
    _pointerPos(HPos::invalid()),
    _mapped(false),
    _pixmapCurrent(false),
    _pixmap(0),
    _surface(nullptr),
    _cr(nullptr),
    _title(_config.title),
    _icon(_config.icon),
    _primarySelection(),
    _clipboardSelection(),
    _pressed(false),
    _pressCount(0),
    _lastPressTime(0),
    _button(XCB_BUTTON_INDEX_ANY),
    _cursorVisible(true),
    _deferralsAllowed(true),
    _deferred(false),
    _transientTitle(false),
    _hadDeleteRequest(false)
{
    _fontSet = _fontManager.addClient(this);
    ASSERT(_fontSet, "");
    auto fontGuard = scopeGuard([&] { _fontManager.removeClient(this); });

    auto rows = _config.initialRows;
    auto cols = _config.initialCols;

    const auto BORDER_THICKNESS = _config.borderThickness;
    const auto SCROLLBAR_WIDTH  = _config.scrollbarWidth;

    _width  = 2 * BORDER_THICKNESS + cols * _fontSet->getWidth() + SCROLLBAR_WIDTH;
    _height = 2 * BORDER_THICKNESS + rows * _fontSet->getHeight();

    xcb_void_cookie_t cookie;

    //
    // Create the window.
    //

    uint32_t winValues[] = {
        // XCB_CW_BACK_PIXEL
        // Note, it is important to set XCB_CW_BACK_PIXEL to the actual
        // background colour used by the terminal in order to prevent
        // flicker when the window is exposed.
        _colorSet.getBackgroundPixel(),
        // XCB_CW_BIT_GRAVITY
        XCB_GRAVITY_NORTH_WEST,         // What to do if window is resized.
        // XCB_CW_WIN_GRAVITY
        XCB_GRAVITY_NORTH_WEST,         // What to do if parent is resized.
        // XCB_CW_BACKING_STORE
        XCB_BACKING_STORE_NOT_USEFUL,   // XCB_BACKING_STORE_WHEN_MAPPED, XCB_BACKING_STORE_ALWAYS
        // XCB_CW_SAVE_UNDER
        0,                              // 1 -> useful
        // XCB_CW_EVENT_MASK
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
        XCB_EVENT_MASK_POINTER_MOTION_HINT | XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_EXPOSURE |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_FOCUS_CHANGE,
        // XCB_CW_CURSOR
        _basics.normalCursor()
    };

    _window = xcb_generate_id(_basics.connection());
    cookie = xcb_create_window_checked(_basics.connection(),
                                       _basics.screen()->root_depth,
                                       _window,
                                       _basics.screen()->root,
                                       _config.initialX, config.initialY,
                                       _width, _height,
                                       0,            // border width
                                       XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                       _basics.screen()->root_visual,
                                       XCB_CW_BACK_PIXEL |
                                       XCB_CW_BIT_GRAVITY |
                                       XCB_CW_WIN_GRAVITY |
                                       XCB_CW_BACKING_STORE |
                                       XCB_CW_SAVE_UNDER |
                                       XCB_CW_EVENT_MASK |
                                       XCB_CW_CURSOR,
                                       winValues);
    if (xcb_request_failed(_basics.connection(), cookie, "Failed to create window")) {
        throw Error("Failed to create window.");
    }

    auto windowGuard = scopeGuard([&] {xcb_destroy_window(_basics.connection(), _window);});

    //
    // Do the ICCC jive.
    //

    icccmConfigure();

    //
    // Create the GC.
    //

    uint32_t gcValues[] = {
        0 // no exposures
    };

    _gc = xcb_generate_id(_basics.connection());
    cookie = xcb_create_gc_checked(_basics.connection(),
                                   _gc,
                                   _window,
                                   XCB_GC_GRAPHICS_EXPOSURES,
                                   gcValues);
    if (xcb_request_failed(_basics.connection(), cookie, "Failed to allocate GC")) {
        throw Error("Failed to create GC.");
    }

    auto gcGuard = scopeGuard([&] { xcb_free_gc(_basics.connection(), _window); });

    //
    // Create the TTY and terminal.
    //

    try {
        _terminal = new Terminal(*this, _config, selector, deduper, rows, cols, stringify(_window), command);
    }
    catch (const Tty::Error & ex) {
        throw Error("Failed to create tty: " + ex.message);
    }

    _open = true;

    //
    // Update the window title and map the window.
    //

    updateTitle();

    cookie = xcb_map_window_checked(_basics.connection(), _window);
    if (xcb_request_failed(_basics.connection(), cookie, "Failed to map window")) {
        throw Error("Failed to map window.");
    }

    xcb_flush(_basics.connection());

    gcGuard.dismiss();
    windowGuard.dismiss();
    fontGuard.dismiss();
}

Window::~Window() {
    if (_mapped) {
        ASSERT(_pixmap, "");
        ASSERT(_surface, "");

        cairo_surface_finish(_surface);
        cairo_surface_destroy(_surface);

        auto cookie = xcb_free_pixmap_checked(_basics.connection(), _pixmap);
        xcb_request_failed(_basics.connection(), cookie, "Failed to free pixmap");
    }
    else {
        ASSERT(!_surface, "");
        ASSERT(!_pixmap, "");
    }

    // Unwind constructor.

    delete _terminal;

    xcb_free_gc(_basics.connection(), _gc);

    // The window may have been destroyed exogenously.
    if (!_destroyed) {
        auto cookie = xcb_destroy_window_checked(_basics.connection(), _window);
        xcb_request_failed(_basics.connection(), cookie, "Failed to destroy window");
    }

    xcb_flush(_basics.connection());

    _fontManager.removeClient(this);
}

// Events:

void Window::keyPress(xcb_key_press_event_t * event) {
    cursorVisibility(false);

    if (!_open) { return; }

    xcb_keysym_t keySym;
    ModifierSet  modifiers;

    if (_basics.getKeySym(event->detail, event->state, keySym, modifiers)) {
        if (_terminal->keyPress(keySym, modifiers)) {
            if (_hadDeleteRequest) {
                _hadDeleteRequest = false;
            }

            if (_transientTitle) {
                _transientTitle = false;
                updateTitle();
            }
        }
    }
}

void Window::keyRelease(xcb_key_release_event_t * UNUSED(event)) {
    if (!_open) { return; }
}

void Window::buttonPress(xcb_button_press_event_t * event) {
    ASSERT(event->event == _window, "Which window?");

    cursorVisibility(true);

    //PRINT("Button-press: " << event->event_x << " " << event->event_y);
    if (!_open) { return; }
    if (event->detail < XCB_BUTTON_INDEX_1 ||
        event->detail > XCB_BUTTON_INDEX_5) { return; }

    auto modifiers = _basics.convertState(event->state);

    HPos hpos;
    auto within = xy2Pos(event->event_x, event->event_y, hpos);

    switch (event->detail) {
        case XCB_BUTTON_INDEX_4:
            _terminal->scrollWheel(Terminal::ScrollDir::UP, modifiers, within, hpos.pos);
            return;
        case XCB_BUTTON_INDEX_5:
            _terminal->scrollWheel(Terminal::ScrollDir::DOWN, modifiers, within, hpos.pos);
            return;
    }

    if (_pressed) {
        ASSERT(event->detail != _button, "Already pressed!");
        return;
    }

    _pressed = true;

    if (_button != event->detail ||
        event->time - _lastPressTime > _config.doubleClickTimeout)
    {
        _pressCount = 1;
    }
    else {
        ++_pressCount;
    }

    _button        = event->detail;
    _lastPressTime = event->time;

    switch (event->detail) {
        case XCB_BUTTON_INDEX_1:
            _terminal->buttonPress(Terminal::Button::LEFT, _pressCount,
                                   modifiers, within, hpos);
            return;
        case XCB_BUTTON_INDEX_2:
            _terminal->buttonPress(Terminal::Button::MIDDLE, _pressCount,
                                   modifiers, within, hpos);
            return;
        case XCB_BUTTON_INDEX_3:
            _terminal->buttonPress(Terminal::Button::RIGHT, _pressCount,
                                   modifiers, within, hpos);
            return;
    }
}

void Window::buttonRelease(xcb_button_release_event_t * event) {
    ASSERT(event->event == _window, "Which window?");

    cursorVisibility(true);

    //PRINT("Button-release: " << event->event_x << " " << event->event_y);
    if (!_open) { return; }
    if (event->detail < XCB_BUTTON_INDEX_1 ||
        event->detail > XCB_BUTTON_INDEX_5) { return; }

    auto modifiers = _basics.convertState(event->state);

    if (_pressed && _button == event->detail) {
        _terminal->buttonRelease(false, modifiers);
        _pressed = false;
    }
}

void Window::motionNotify(xcb_motion_notify_event_t * event) {
    ASSERT(event->event == _window, "Which window?");

    cursorVisibility(true);

    //PRINT("Motion-notify: " << event->event_x << " " << event->event_y);
    if (!_open) { return; }

    int16_t x, y;
    uint16_t mask;

    if (event->detail == XCB_MOTION_HINT) {
        auto cookie = xcb_query_pointer(_basics.connection(), _window);
        auto reply  = xcb_query_pointer_reply(_basics.connection(), cookie, nullptr);
        if (!reply) {
            WARNING("Failed to query pointer.");
            return;
        }
        x    = reply->win_x;
        y    = reply->win_y;
        mask = reply->mask;
        std::free(reply);
    }
    else {
        x    = event->event_x;
        y    = event->event_y;
        mask = event->state;
    }

    HPos hpos;
    auto within = xy2Pos(x, y, hpos);

    if (_pointerPos != hpos) {
        auto modifiers = _basics.convertState(mask);

        _pointerPos = hpos;
        _terminal->pointerMotion(modifiers, within, hpos);
    }

}

void Window::mapNotify(xcb_map_notify_event_t * UNUSED(event)) {
    //PRINT("Map");
    ASSERT(!_mapped, "");

    _pixmap = xcb_generate_id(_basics.connection());
    // Note, we create the pixmap against the root window rather than
    // _window to avoid dealing with the case where _window may have been
    // asynchronously destroyed.
    auto cookie = xcb_create_pixmap_checked(_basics.connection(),
                                            _basics.screen()->root_depth,
                                            _pixmap,
                                            _basics.screen()->root,
                                            _width,
                                            _height);
    xcb_request_failed(_basics.connection(), cookie, "Failed to create pixmap");

    _surface = cairo_xcb_surface_create(_basics.connection(),
                                        _pixmap,
                                        _basics.visual(),
                                        _width,
                                        _height);
    ENFORCE(_surface, "Failed to create surface");
    ENFORCE(cairo_surface_status(_surface) == CAIRO_STATUS_SUCCESS, "");

    _mapped = true;
}

void Window::unmapNotify(xcb_unmap_notify_event_t * UNUSED(event)) {
    //PRINT("UnMap");
    ASSERT(_mapped, "");

    ASSERT(_surface, "");
    ENFORCE(cairo_surface_status(_surface) == CAIRO_STATUS_SUCCESS, "");
    cairo_surface_finish(_surface);
    cairo_surface_destroy(_surface);
    _surface = nullptr;

    ASSERT(_pixmap, "");
    auto cookie = xcb_free_pixmap(_basics.connection(), _pixmap);
    xcb_request_failed(_basics.connection(), cookie, "Failed to free pixmap");
    _pixmap = 0;
    _pixmapCurrent = false;

    _mapped = false;
}

void Window::reparentNotify(xcb_reparent_notify_event_t * UNUSED(event)) {
    //PRINT("Reparent");
}

void Window::expose(xcb_expose_event_t * event) {
    if (_deferred) { return; }

    ASSERT(event->window == _window, "Which window?");
    /*
    PRINT("Expose: " <<
          event->x << " " << event->y << " " <<
          event->width << " " << event->height);
          */

    ASSERT(_mapped, "");

    if (_mapped) {
        // Once we've had our first expose the pixmap is always valid.   XXX what about on resize?
        if (!_pixmapCurrent) {
            ASSERT(_surface, "");
            // Make the entire pixmap valid.
            draw();
            _pixmapCurrent = true;
        }
        copy(event->x, event->y, event->width, event->height);
    }
}

void Window::configureNotify(xcb_configure_notify_event_t * event) {
    ASSERT(event->window == _window, "Which window?");

    // We are only interested in size changes (not moves).
    if (_width == event->width && _height == event->height) {
        return;
    }

    /*
    PRINT("Configure notify: " <<
          event->x << " " << event->y << " " <<
          event->width << " " << event->height);
          */

    _width  = event->width;
    _height = event->height;

    if (_deferralsAllowed) {
        if (!_deferred) {
            _observer.windowDefer(this);
            _deferred = true;
        }
    }
    else {
        handleResize();
    }
}

void Window::focusIn(xcb_focus_in_event_t * UNUSED(event)) {
    _terminal->focusChange(true);
}

void Window::focusOut(xcb_focus_out_event_t * UNUSED(event)) {
    _terminal->focusChange(false);

}

void Window::enterNotify(xcb_enter_notify_event_t * UNUSED(event)) {
    //PRINT("enter");
}

void Window::leaveNotify(xcb_leave_notify_event_t * event) {
    //PRINT("leave: " << int(event->mode));

    // XXX total guess that this is how we ensure we release
    // the button...
    if (event->mode == 2) {
        if (_pressed) {
            _terminal->buttonRelease(true, ModifierSet());
            _pressed = false;
        }
    }
}

void Window::visibilityNotify(xcb_visibility_notify_event_t * UNUSED(event)) {
}

void Window::destroyNotify(xcb_destroy_notify_event_t * event) {
    ASSERT(event->window == _window, "Which window?");
    //PRINT("Destroy notify");

    _terminal->close();
    _open      = false;
    _destroyed = true;      // XXX why not just zero _window
}

void Window::selectionClear(xcb_selection_clear_event_t * UNUSED(event)) {
    //PRINT("Selection clear");

    _terminal->clearSelection();
}

void Window::selectionNotify(xcb_selection_notify_event_t * UNUSED(event)) {
    //PRINT("Selection notify");
    if (_open) {
        uint32_t offset = 0;        // 32-bit quantities

        for (;;) {
            auto cookie = xcb_get_property(_basics.connection(),
                                           false,     // delete
                                           _window,
                                           XCB_ATOM_PRIMARY,
                                           XCB_GET_PROPERTY_TYPE_ANY,
                                           offset,
                                           8192 / 4);

            auto reply = xcb_get_property_reply(_basics.connection(), cookie, nullptr);
            if (!reply) { break; }
            auto guard = scopeGuard([reply] { std::free(reply); });

            void * value  = xcb_get_property_value(reply);
            int    length = xcb_get_property_value_length(reply);
            if (length == 0) { break; }

            _terminal->paste(reinterpret_cast<const uint8_t *>(value), length);
            offset += (length + 3) / 4;
        }
    }
}

void Window::selectionRequest(xcb_selection_request_event_t * event) {
    ASSERT(event->owner == _window, "Which window?");

    xcb_selection_notify_event_t response;
    response.response_type = XCB_SELECTION_NOTIFY;
    response.time          = event->time;
    response.requestor     = event->requestor;
    response.selection     = event->selection;
    response.target        = event->target;
    response.property      = XCB_ATOM_NONE;        // reject by default

    if (event->target == _basics.atomTargets()) {
        xcb_atom_t atomUtf8String = _basics.atomUtf8String();
        auto cookie = xcb_change_property_checked(_basics.connection(),
                                                  XCB_PROP_MODE_REPLACE,
                                                  event->requestor,
                                                  event->property,
                                                  XCB_ATOM_ATOM,
                                                  32,
                                                  1,
                                                  &atomUtf8String);
        xcb_request_failed(_basics.connection(), cookie, "Failed to change property");
        response.property = event->property;
    }
    else if (event->target == _basics.atomUtf8String()) {
        std::string text;

        if (event->selection == _basics.atomPrimary()) {
            text = _primarySelection;
        }
        else if (event->selection == _basics.atomClipboard()) {
            text = _clipboardSelection;
        }
        else {
            ERROR("Unexpected selection");
        }

        auto cookie = xcb_change_property_checked(_basics.connection(),
                                                  XCB_PROP_MODE_REPLACE,
                                                  event->requestor,
                                                  event->property,
                                                  event->target,
                                                  8,
                                                  text.length(),
                                                  text.data());
        xcb_request_failed(_basics.connection(), cookie, "Failed to change property");
        response.property = event->property;
    }

    auto cookie = xcb_send_event_checked(_basics.connection(),
                                         true,
                                         event->requestor,
                                         0,
                                         reinterpret_cast<const char *>(&response));
    xcb_request_failed(_basics.connection(), cookie, "Failed to send event");

    xcb_flush(_basics.connection());        // Required?
}

void Window::clientMessage(xcb_client_message_event_t * event) {
    if (event->type == _basics.atomWmProtocols()) {
        if (event->data.data32[0] == _basics.atomWmDeleteWindow()) {
            handleDelete();
        }
    }
}

void Window::deferral() {
    ASSERT(_deferred, "");
    handleResize();
    _deferred = false;
}

void Window::icccmConfigure() {
    //
    // machine
    //

    const auto & hostname = _basics.hostname();
    if (!hostname.empty()) {
        xcb_icccm_set_wm_client_machine(_basics.connection(),
                                        _window,
                                        XCB_ATOM_STRING,
                                        8,
                                        hostname.size(),
                                        hostname.data());
    }

    //
    // class
    //

    std::string wm_class =
        std::string("terminol") + '\0' +
        std::string("Terminol") + '\0';
    xcb_icccm_set_wm_class(_basics.connection(), _window,
                           wm_class.size(), wm_class.data());

    //
    // size
    //

    const auto BORDER_THICKNESS = _config.borderThickness;
    const auto SCROLLBAR_WIDTH  = _config.scrollbarWidth;

    const auto BASE_WIDTH  = 2 * BORDER_THICKNESS + SCROLLBAR_WIDTH;
    const auto BASE_HEIGHT = 2 * BORDER_THICKNESS;

    const auto MIN_COLS = 8;
    const auto MIN_ROWS = 2;

    xcb_size_hints_t sizeHints;
    sizeHints.flags = 0;
    xcb_icccm_size_hints_set_min_size(&sizeHints,
                                      BASE_WIDTH  + MIN_COLS * _fontSet->getWidth(),
                                      BASE_HEIGHT + MIN_ROWS * _fontSet->getHeight());
    xcb_icccm_size_hints_set_base_size(&sizeHints,
                                       BASE_WIDTH,
                                       BASE_HEIGHT);
    xcb_icccm_size_hints_set_resize_inc(&sizeHints,
                                        _fontSet->getWidth(),
                                        _fontSet->getHeight());
    xcb_icccm_size_hints_set_win_gravity(&sizeHints, XCB_GRAVITY_NORTH_WEST);
#if 0
    xcb_icccm_set_wm_size_hints(_basics.connection(),
                                _window,
                                XCB_ATOM_WM_NORMAL_HINTS,
                                &sizeHints);
#else
    xcb_icccm_set_wm_normal_hints(_basics.connection(),
                                  _window,
                                  &sizeHints);
#endif

    //
    // wm?
    //

    xcb_icccm_wm_hints_t wmHints;
    wmHints.flags = 0;
    xcb_icccm_wm_hints_set_input(&wmHints, 1 /* What value? */);
    //xcb_icccm_wm_hints_set_icon_pixmap
    xcb_icccm_set_wm_hints(_basics.connection(), _window, &wmHints);

    //
    // xcb_icccm_set_wm_protocols
    //

    xcb_atom_t wmDeleteWindow = _basics.atomWmDeleteWindow();
    xcb_icccm_set_wm_protocols(_basics.connection(), _window,
                               _basics.atomWmProtocols(),
                               1, &wmDeleteWindow);
}

void Window::pos2XY(Pos pos, int & x, int & y) const {
    ASSERT(pos.row <= _terminal->getRows(), "pos.row=" << pos.row << ", getRows()=" << _terminal->getRows());
    ASSERT(pos.col <= _terminal->getCols(), "pos.col=" << pos.col << ", getCols()=" << _terminal->getCols());

    const auto BORDER_THICKNESS = _config.borderThickness;

    x = BORDER_THICKNESS + pos.col * _fontSet->getWidth();
    y = BORDER_THICKNESS + pos.row * _fontSet->getHeight();
}

bool Window::xy2Pos(int x, int y, HPos & hpos) const {
    auto within = true;

    const int BORDER_THICKNESS = _config.borderThickness;

    // x / cols:

    if (x < BORDER_THICKNESS) {
        hpos.pos.col = 0;
        hpos.hand = Hand::LEFT;
        within = false;
    }
    else if (x < BORDER_THICKNESS + _fontSet->getWidth() * _terminal->getCols()) {
        auto xx = x - BORDER_THICKNESS;
        hpos.pos.col = xx / _fontSet->getWidth();
        auto remainder = xx - hpos.pos.col * _fontSet->getWidth();
        hpos.hand = remainder < _fontSet->getWidth() / 2 ? Hand::LEFT : Hand::RIGHT;
        ASSERT(hpos.pos.col < _terminal->getCols(),
               "col is: " << hpos.pos.col << ", getCols() is: " <<
               _terminal->getCols());
    }
    else {
        hpos.pos.col = _terminal->getCols();
        hpos.hand = Hand::LEFT;
        within = false;
    }

    // y / rows:

    if (y < BORDER_THICKNESS) {
        hpos.pos.row = 0;
        within = false;
    }
    else if (y < BORDER_THICKNESS + _fontSet->getHeight() * _terminal->getRows()) {
        auto yy = y - BORDER_THICKNESS;
        hpos.pos.row = yy / _fontSet->getHeight();
        ASSERT(hpos.pos.row < _terminal->getRows(),
               "row is: " << hpos.pos.row << ", getRows() is: " <<
               _terminal->getRows());
    }
    else {
        hpos.pos.row = _terminal->getRows() - 1;
        within = false;
    }

    return within;
}

void Window::updateTitle() {
    ASSERT(_terminal, "");

    std::ostringstream ost;

#if 0
    ost << VERSION " ";
#endif

    ost << "[" << _terminal->getCols() << 'x' << _terminal->getRows() << "] ";
    ost << _title;

    setTitle(ost.str());
}

void Window::updateIcon() {
    ASSERT(_terminal, "");

    std::ostringstream ost;

#if 0
    ost << VERSION " ";
#endif

    ost << "[" << _terminal->getCols() << 'x' << _terminal->getRows() << "] ";
    ost << _icon;

    const auto & fullIcon = ost.str();

#if 1
    xcb_icccm_set_wm_icon_name(_basics.connection(),
                               _window,
                               XCB_ATOM_STRING,
                               8,
                               fullIcon.size(),
                               fullIcon.data());
#else
    xcb_ewmh_set_wm_icon_name(_basics.ewmhConnection(),
                              _window,
                              fullIcon.size(),
                              fullIcon.data());
#endif
}

void Window::setTitle(const std::string & title) {

#if 1
    xcb_icccm_set_wm_name(_basics.connection(),
                          _window,
                          XCB_ATOM_STRING,
                          8,
                          title.size(),
                          title.data());
#else
    xcb_ewmh_set_wm_name(_basics.ewmhConnection(),
                         _window,
                         title.size(),
                         title.data());
#endif

    xcb_flush(_basics.connection());
}

void Window::draw() {
    ASSERT(_mapped, "");        // XXX is this valid?
    ASSERT(_pixmap, "");
    ASSERT(_surface, "");
    _cr = cairo_create(_surface);
    cairo_set_line_width(_cr, 1.0);

    cairo_save(_cr); {
        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));

        drawBorder();
        _terminal->redraw();

        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));

    } cairo_restore(_cr);
    cairo_destroy(_cr);
    _cr = nullptr;

    cairo_surface_flush(_surface);      // Useful?
    ENFORCE(cairo_surface_status(_surface) == CAIRO_STATUS_SUCCESS, "");
}

void Window::drawBorder() {
    const auto BORDER_THICKNESS = _config.borderThickness;
    const auto SCROLLBAR_WIDTH  = _config.scrollbarWidth;

    cairo_save(_cr); {
        const auto & bg = _colorSet.getBorderColor();
        cairo_set_source_rgb(_cr, bg.r, bg.g, bg.b);

        double x1 = BORDER_THICKNESS + _fontSet->getWidth() * _terminal->getCols();
        double x2 = _width - SCROLLBAR_WIDTH;

        double y1 = BORDER_THICKNESS + _fontSet->getHeight() * _terminal->getRows();
        double y2 = _height;

        // Left edge.
        cairo_rectangle(_cr,
                        0.0,
                        0.0,
                        static_cast<double>(BORDER_THICKNESS),
                        static_cast<double>(_height));
        cairo_fill(_cr);

        // Top edge.
        cairo_rectangle(_cr,
                        0.0,
                        0.0,
                        x2,
                        static_cast<double>(BORDER_THICKNESS));
        cairo_fill(_cr);

        // Right edge.
        cairo_rectangle(_cr,
                        x1,
                        0.0,
                        x2 - x1,
                        y2);
        cairo_fill(_cr);

        // Bottom edge.
        cairo_rectangle(_cr,
                        0.0,
                        y1,
                        x2,
                        y2 - y1);
        cairo_fill(_cr);
    } cairo_restore(_cr);
}

void Window::copy(int x, int y, int w, int h) {
    ASSERT(_mapped, "");
    ASSERT(_pixmap, "");
    ASSERT(_pixmapCurrent, "");
    // Copy the buffer region
    auto cookie = xcb_copy_area_checked(_basics.connection(),
                                        _pixmap,
                                        _window,
                                        _gc,
                                        x, y,   // src
                                        x, y,   // dst
                                        w, h);
    xcb_request_failed(_basics.connection(), cookie, "Failed to copy area");
    //xcb_flush(_basics.connection());
    xcb_aux_sync(_basics.connection());
}

void Window::handleResize() {
    if (_mapped) {
        ASSERT(_pixmap, "");
        ASSERT(_surface, "");

        cairo_surface_finish(_surface);
        cairo_surface_destroy(_surface);
        _surface = nullptr;

        auto cookie = xcb_free_pixmap_checked(_basics.connection(), _pixmap);
        xcb_request_failed(_basics.connection(), cookie, "Failed to free pixmap");
        _pixmap = 0;

        //
        //
        //

        _pixmap = xcb_generate_id(_basics.connection());
        // Note, we create the pixmap against the root window rather than
        // _window to avoid dealing with the case where _window may have been
        // asynchronously destroyed.
        cookie = xcb_create_pixmap_checked(_basics.connection(),
                                           _basics.screen()->root_depth,
                                           _pixmap,
                                           //_window,
                                           _basics.screen()->root,
                                           _width,
                                           _height);
        xcb_request_failed(_basics.connection(), cookie, "Failed to create pixmap");

        cairo_surface_finish(_surface);
        _surface = cairo_xcb_surface_create(_basics.connection(),
                                            _pixmap,
                                            _basics.visual(),
                                            _width,
                                            _height);
        ENFORCE(_surface, "Failed to create surface");
        ENFORCE(cairo_surface_status(_surface) == CAIRO_STATUS_SUCCESS, "");
    }

    int16_t rows, cols;
    sizeToRowsCols(rows, cols);

    _terminal->resize(rows, cols);      // Ok to resize if not open?

    if (!_transientTitle) {
        updateTitle();
    }

    if (_mapped) {
        ASSERT(_pixmap, "");
        ASSERT(_surface, "");
        draw();
        _pixmapCurrent = true;
        copy(0, 0, _width, _height);
    }
    else {
        _pixmapCurrent = false;
    }
}

void Window::resizeToAccommodate(int16_t rows, int16_t cols) {
    const auto BORDER_THICKNESS = _config.borderThickness;
    const auto SCROLLBAR_WIDTH  = _config.scrollbarWidth;

    uint32_t width  = 2 * BORDER_THICKNESS + cols * _fontSet->getWidth() + SCROLLBAR_WIDTH;
    uint32_t height = 2 * BORDER_THICKNESS + rows * _fontSet->getHeight();

    if (_width != width || _height != height) {
        uint32_t values[] = { width, height };
        auto cookie = xcb_configure_window(_basics.connection(),
                                           _window,
                                           XCB_CONFIG_WINDOW_WIDTH |
                                           XCB_CONFIG_WINDOW_HEIGHT,
                                           values);
        if (!xcb_request_failed(_basics.connection(), cookie,
                               "Failed to configure window")) {
            xcb_flush(_basics.connection());
            _deferralsAllowed = false;
            _observer.windowSync();
            _deferralsAllowed = true;
        }
    }
}

void Window::sizeToRowsCols(int16_t & rows, int16_t & cols) const {
    const auto BORDER_THICKNESS = _config.borderThickness;
    const auto SCROLLBAR_WIDTH  = _config.scrollbarWidth;

    const auto BASE_WIDTH  = 2 * BORDER_THICKNESS + SCROLLBAR_WIDTH;
    const auto BASE_HEIGHT = 2 * BORDER_THICKNESS;

    if (_width  > static_cast<uint32_t>(BASE_WIDTH  + _fontSet->getWidth()) &&
        _height > static_cast<uint32_t>(BASE_HEIGHT + _fontSet->getHeight()))
    {
        int16_t w = _width  - BASE_WIDTH;
        int16_t h = _height - BASE_HEIGHT;

        rows = h / _fontSet->getHeight();
        cols = w / _fontSet->getWidth();
    }
    else {
        rows = cols = 1;
    }

    ASSERT(rows > 0 && cols > 0, "");
}

void Window::handleDelete() {
    if (_terminal->hasSubprocess()) {
        if (_hadDeleteRequest) {
            xcb_destroy_window(_basics.connection(), _window);
        }
        else {
            _hadDeleteRequest = true;
            _transientTitle   = true;
            setTitle("Process is running, once more to verify...");
        }
    }
    else {
        xcb_destroy_window(_basics.connection(), _window);
    }
}

void Window::cursorVisibility(bool visible) {
    if (_cursorVisible != visible) {
        auto mask   = XCB_CW_CURSOR;
        auto values = visible ? _basics.normalCursor() : _basics.invisibleCursor();
        auto cookie = xcb_change_window_attributes_checked(_basics.connection(),
                                                           _window,
                                                           mask,
                                                           &values);
        xcb_request_failed(_basics.connection(), cookie, "couldn't change window attributes");

        _cursorVisible = visible;
    }
}

// Terminal::I_Observer implementation:

void Window::terminalGetDisplay(std::string & display) throw () {
    display = _basics.display();
}

void Window::terminalCopy(const std::string & text, bool clipboard) throw () {
    //PRINT("Copy: '" << text << "', clipboard: " << clipboard);

    xcb_atom_t atom;

    if (clipboard) {
        atom = _basics.atomClipboard();
        _clipboardSelection = text;
    }
    else {
        atom = _basics.atomPrimary();
        _primarySelection = text;
    }

    xcb_set_selection_owner(_basics.connection(), _window, atom, XCB_CURRENT_TIME);
    xcb_flush(_basics.connection());
}

void Window::terminalPaste(bool clipboard) throw () {
    //PRINT("Copy clipboard: " << clipboard);

    auto atom = clipboard ? _basics.atomClipboard() : _basics.atomPrimary();

    xcb_convert_selection(_basics.connection(),
                          _window,
                          atom,
                          _basics.atomUtf8String(),
                          XCB_ATOM_PRIMARY, // property
                          XCB_CURRENT_TIME);

    xcb_flush(_basics.connection());
}

void Window::terminalResizeLocalFont(int delta) throw () {
    _fontManager.localDelta(this, delta);
}

void Window::terminalResizeGlobalFont(int delta) throw () {
    _fontManager.globalDelta(delta);
}

void Window::terminalResetTitleAndIcon() throw () {
    _title = _config.title;
    _icon  = _config.icon;
    updateTitle();
    updateIcon();
}

void Window::terminalSetWindowTitle(const std::string & str) throw () {
    //PRINT("Set title: " << title);
    _title = str;
    updateTitle();
}

void Window::terminalSetIconName(const std::string & str) throw () {
    //PRINT("Set title: " << title);
    _icon = str;
    updateIcon();
}

void Window::terminalBeep() throw () {
    xcb_icccm_wm_hints_t wmHints;
    wmHints.flags = 0;
    xcb_icccm_wm_hints_set_urgency(&wmHints);
    xcb_icccm_set_wm_hints(_basics.connection(), _window, &wmHints);
}

void Window::terminalResizeBuffer(int16_t rows, int16_t cols) throw () {
    resizeToAccommodate(rows, cols);

    const auto BORDER_THICKNESS = _config.borderThickness;
    const auto SCROLLBAR_WIDTH  = _config.scrollbarWidth;

    uint32_t width  = 2 * BORDER_THICKNESS + cols * _fontSet->getWidth() + SCROLLBAR_WIDTH;
    uint32_t height = 2 * BORDER_THICKNESS + rows * _fontSet->getHeight();

    if (_width != width || _height != height) {
        uint32_t values[] = { width, height };
        auto cookie = xcb_configure_window(_basics.connection(),
                                           _window,
                                           XCB_CONFIG_WINDOW_WIDTH |
                                           XCB_CONFIG_WINDOW_HEIGHT,
                                           values);
        if (!xcb_request_failed(_basics.connection(), cookie,
                               "Failed to configure window")) {
            xcb_flush(_basics.connection());
            _deferralsAllowed = false;
            _observer.windowSync();
            _deferralsAllowed = true;
        }
    }
}

bool Window::terminalFixDamageBegin() throw () {
    if (!_deferred && _mapped) {
        ASSERT(_surface, "");
        _cr = cairo_create(_surface);
        cairo_set_line_width(_cr, 1.0);
        return true;
    }
    else {
        return false;
    }
}

void Window::terminalDrawBg(Pos    pos,
                            UColor color,
                            size_t count) throw () {
    ASSERT(_cr, "");

    cairo_save(_cr); {
        int x, y;
        pos2XY(pos, x, y);

        double w = count * _fontSet->getWidth();
        double h = _fontSet->getHeight();

        auto bg = getColor(color);
        cairo_set_source_rgb(_cr, bg.r, bg.g, bg.b);

        cairo_rectangle(_cr, x, y, w, h);
        cairo_fill(_cr);

        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));
    } cairo_restore(_cr);
}

void Window::terminalDrawFg(Pos             pos,
                            UColor          color,
                            AttrSet         attrs,
                            const uint8_t * str,
                            size_t          size,
                            size_t          count) throw () {
    ASSERT(_cr, "");

    cairo_save(_cr); {
        auto layout = pango_cairo_create_layout(_cr);
        auto layoutGuard = scopeGuard([&] { g_object_unref(layout); });

        auto font = _fontSet->get(attrs.get(Attr::ITALIC), attrs.get(Attr::BOLD));
        pango_layout_set_font_description(layout, font);
        pango_layout_set_width(layout, -1);

        int x, y;
        pos2XY(pos, x, y);

        double w = count * _fontSet->getWidth();
        double h = _fontSet->getHeight();
        cairo_rectangle(_cr, x, y, w, h);
        cairo_clip(_cr);

        auto alpha = attrs.get(Attr::CONCEAL) ? 0.1 : attrs.get(Attr::FAINT) ? 0.5 : 1.0;
        auto fg    = getColor(color);
        cairo_set_source_rgba(_cr, fg.r, fg.g, fg.b, alpha);

        if (attrs.get(Attr::UNDERLINE)) {
            cairo_move_to(_cr, x, y + h - 0.5);
            cairo_rel_line_to(_cr, w, 0.0);
            cairo_stroke(_cr);
        }

        cairo_move_to(_cr, x, y);
        pango_layout_set_text(layout, reinterpret_cast<const char *>(str), size);
        pango_cairo_update_layout(_cr, layout);
        pango_cairo_show_layout(_cr, layout);

        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));
    } cairo_restore(_cr);
}

void Window::terminalDrawCursor(Pos             pos,
                                UColor          fg_,
                                UColor          bg_,
                                AttrSet         attrs,
                                const uint8_t * str,
                                size_t          size,
                                bool            wrapNext,
                                bool            focused) throw () {
    ASSERT(_cr, "");

    cairo_save(_cr); {
        auto layout = pango_cairo_create_layout(_cr);
        auto layoutGuard = scopeGuard([&] { g_object_unref(layout); });

        auto font = _fontSet->get(attrs.get(Attr::ITALIC), attrs.get(Attr::BOLD));
        pango_layout_set_font_description(layout, font);

        pango_layout_set_width(layout, -1);
        pango_layout_set_wrap(layout, PANGO_WRAP_CHAR);

        auto fg = getColor(bg_);
        auto bg = getColor(fg_);

        int x, y;
        pos2XY(pos, x, y);

        if (focused) {
            cairo_set_source_rgb(_cr, bg.r, bg.g, bg.b);
        }
        else {
            cairo_set_source_rgb(_cr, fg.r, fg.g, fg.b);
        }

        cairo_rectangle(_cr, x, y, _fontSet->getWidth(), _fontSet->getHeight());
        cairo_fill(_cr);

        auto alpha = wrapNext ? 0.4 : 0.8;
        cairo_set_source_rgba(_cr, bg.r, bg.g, bg.b, alpha);

        if (focused) {
            cairo_rectangle(_cr, x, y, _fontSet->getWidth(), _fontSet->getHeight());
            cairo_fill(_cr);
            cairo_set_source_rgb(_cr, fg.r, fg.g, fg.b);
        }
        else {
            cairo_rectangle(_cr,
                            x + 0.5, y + 0.5,
                            _fontSet->getWidth() - 1.0, _fontSet->getHeight() - 1.0);
            cairo_stroke(_cr);
        }

        cairo_move_to(_cr, x, y);
        pango_layout_set_text(layout, reinterpret_cast<const char *>(str), size);
        pango_cairo_update_layout(_cr, layout);
        pango_cairo_show_layout(_cr, layout);

        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));
    } cairo_restore(_cr);
}

void Window::terminalDrawScrollbar(size_t  totalRows,
                                   size_t  historyOffset,
                                   int16_t visibleRows) throw () {
    ASSERT(_cr, "");

    const int SCROLLBAR_WIDTH  = _config.scrollbarWidth;

    double x = static_cast<double>(_width - SCROLLBAR_WIDTH);
    double y = 0.0;
    double h = static_cast<double>(_height);
    double w = static_cast<double>(SCROLLBAR_WIDTH);

    // Draw the gutter.

    const auto & bg = _colorSet.getScrollBarBgColor();
    cairo_set_source_rgb(_cr, bg.r, bg.g, bg.b);

    cairo_rectangle(_cr,
                    x,
                    y,
                    w,
                    h);
    cairo_fill(_cr);

    // Draw the bar.

    auto min  = 2.0;
    auto yBar = static_cast<double>(historyOffset) / static_cast<double>(totalRows) * (h - min);
    auto hBar = static_cast<double>(visibleRows)   / static_cast<double>(totalRows) * (h - min);

    const auto & fg = _colorSet.getScrollBarFgColor();
    cairo_set_source_rgb(_cr, fg.r, fg.g, fg.b);

    cairo_rectangle(_cr,
                    x + 1.0,
                    yBar,
                    w - 2.0,
                    hBar + min);
    cairo_fill(_cr);
}

void Window::terminalFixDamageEnd(const Region & damage,
                                  bool           scrollBar) throw () {
    ASSERT(_cr, "");

    cairo_destroy(_cr);
    _cr = nullptr;

    cairo_surface_flush(_surface);      // Useful?

    int x0, y0;
    pos2XY(damage.begin, x0, y0);
    int x1, y1;
    pos2XY(damage.end, x1, y1);

    if (scrollBar) {
        // Expand the region to include the scroll bar
        y0 = 0;
        x1 = _width;
        y1 = _height;
    }

    copy(x0, y0, x1 - x0, y1 - y0);
}

void Window::terminalChildExited(int exitStatus) throw () {
    //PRINT("Child exited: " << exitStatus);
    _open = false;
    _observer.windowExited(this, exitStatus);       // FIXME code vs status
}

// FontManager::I_Client implementation:

void Window::useFontSet(FontSet * fontSet, int delta) throw () {
    _fontSet = fontSet;

    xcb_size_hints_t sizeHints;
    sizeHints.flags = 0;
    xcb_icccm_size_hints_set_resize_inc(&sizeHints,
                                        _fontSet->getWidth(),
                                        _fontSet->getHeight());
    xcb_icccm_set_wm_normal_hints(_basics.connection(),
                                  _window,
                                  &sizeHints);

    resizeToAccommodate(_terminal->getRows(), _terminal->getCols());

    int16_t rows, cols;
    sizeToRowsCols(rows, cols);

    if (rows != _terminal->getRows() || cols != _terminal->getCols()) {
        _terminal->resize(rows, cols);      // Ok to resize if not open?
    }

    if (_mapped) {
        draw();
        copy(0, 0, _width, _height);
    }

    std::ostringstream ost;
    ost << "[" << _terminal->getCols() << 'x' << _terminal->getRows() << "] ";
    ost << "font: " << explicitSign(delta);
    _transientTitle = true;
    setTitle(ost.str());
}

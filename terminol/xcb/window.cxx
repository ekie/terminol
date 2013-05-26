// vi:noai:sw=4

#include "terminol/xcb/window.hxx"
#include "terminol/support/conv.hxx"
#include "terminol/support/pattern.hxx"

#include <xcb/xcb_icccm.h>
#include <xcb/xcb_aux.h>

#include <limits>

#include <unistd.h>

#define xcb_request_failed(connection, cookie, err_msg) _xcb_request_failed(connection, cookie, err_msg, __LINE__)
namespace {
int _xcb_request_failed(xcb_connection_t * connection, xcb_void_cookie_t cookie,
                        const char * err_msg, int line) {
    xcb_generic_error_t * err;
    if ((err = xcb_request_check(connection, cookie)) != nullptr) {
        fprintf(stderr, "[%s:%d] ERROR: %s. X Error Code: %d\n", __FILE__, line, err_msg, err->error_code);
        return err->error_code;
    }
    return 0;
}
} // namespace {anonymous}

Window::Window(const Config       & config,
               Deduper            & deduper,
               Basics             & basics,
               const ColorSet     & colorSet,
               FontSet            & fontSet,
               const KeyMap       & keyMap,
               const Tty::Command & command) throw (Error) :
    _config(config),
    _basics(basics),
    _colorSet(colorSet),
    _fontSet(fontSet),
    _window(0),
    _destroyed(false),
    _gc(0),
    _width(0),
    _height(0),
    _tty(nullptr),
    _terminal(nullptr),
    _isOpen(false),
    _pointerRow(std::numeric_limits<uint16_t>::max()),
    _pointerCol(std::numeric_limits<uint16_t>::max()),
    _mapped(false),
    _focussed(false),
    _hadExpose(false),
    _pixmap(0),
    _surface(nullptr),
    _cr(nullptr),
    _title(_config.getTitle()),
    _primarySelection(),
    _clipboardSelection(),
    _pressed(false),
    _pressCount(0),
    _lastPressTime(0),
    _button(XCB_BUTTON_INDEX_ANY)
{
    uint16_t rows = _config.getInitialRows();
    uint16_t cols = _config.getInitialCols();

    const int BORDER_THICKNESS = _config.getBorderThickness();
    const int SCROLLBAR_WIDTH  = _config.getScrollbarWidth();

    _width  = 2 * BORDER_THICKNESS + cols * _fontSet.getWidth() + SCROLLBAR_WIDTH;
    _height = 2 * BORDER_THICKNESS + rows * _fontSet.getHeight();

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
        // XCB_CW_CURSOR
        //                                 TODO
        // XCB_CW_EVENT_MASK
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
        XCB_EVENT_MASK_POINTER_MOTION_HINT | XCB_EVENT_MASK_BUTTON_MOTION |
        XCB_EVENT_MASK_EXPOSURE |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_FOCUS_CHANGE |
        0
    };

    _window = xcb_generate_id(_basics.connection());
    cookie = xcb_create_window_checked(_basics.connection(),
                                       _basics.screen()->root_depth,
                                       _window,
                                       _basics.screen()->root,
                                       _config.getInitialX(), config.getInitialY(),
                                       _width, _height,
                                       0,            // border width
                                       XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                       _basics.screen()->root_visual,
                                       XCB_CW_BACK_PIXEL |
                                       XCB_CW_BIT_GRAVITY |
                                       XCB_CW_WIN_GRAVITY |
                                       XCB_CW_BACKING_STORE |
                                       XCB_CW_SAVE_UNDER |
                                       //XCB_CW_CURSOR |
                                       XCB_CW_EVENT_MASK,
                                       winValues);
    if (xcb_request_failed(_basics.connection(), cookie, "Failed to create window")) {
        throw Error("Failed to create window.");
    }

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
    if (xcb_request_failed(_basics.connection(), cookie, "Failed to allocate gc")) {
        xcb_destroy_window(_basics.connection(), _window);
        FATAL("");
    }

    //
    // Create the TTY and terminal.
    //

    _tty = new Tty(_config, rows, cols, stringify(_window), command);
    _terminal = new Terminal(*this, _config, deduper, rows, cols, keyMap, *_tty);
    _isOpen = true;

    //
    // Update the window title the map the window.
    //

    updateTitle();

    xcb_map_window(_basics.connection(), _window);
    xcb_flush(_basics.connection());
}

Window::~Window() {
    if (_mapped) {
        if (_config.getDoubleBuffer()) {
            ASSERT(_pixmap, "");
        }
        ASSERT(_surface, "");

        cairo_surface_finish(_surface);
        cairo_surface_destroy(_surface);

        if (_config.getDoubleBuffer()) {
            xcb_void_cookie_t cookie = xcb_free_pixmap(_basics.connection(), _pixmap);
            if (xcb_request_failed(_basics.connection(), cookie,
                                   "Failed to free pixmap"))
            {
                FATAL("");
            }
        }
    }
    else {
        ASSERT(!_surface, "");
        if (_config.getDoubleBuffer()) {
            ASSERT(!_pixmap, "");
        }
    }

    // Unwind constructor.

    delete _tty;
    delete _terminal;

    xcb_free_gc(_basics.connection(), _gc);

    // The window may have been destroyed exogenously.
    if (!_destroyed) {
        xcb_destroy_window(_basics.connection(), _window);
    }

    xcb_flush(_basics.connection());
}

void Window::read() {
    ASSERT(_isOpen, "");
    _terminal->read();
}

bool Window::needsFlush() const {
    ASSERT(_isOpen, "");
    return _terminal->needsFlush();
}

void Window::flush() {
    ASSERT(_isOpen, "");
    _terminal->flush();
}

// Events:

void Window::keyPress(xcb_key_press_event_t * event) {
    if (!_isOpen) { return; }

    xcb_keysym_t keySym = _basics.getKeySym(event->detail, event->state);
    _terminal->keyPress(keySym, event->state);
}

void Window::keyRelease(xcb_key_release_event_t * UNUSED(event)) {
    if (!_isOpen) { return; }
}

void Window::buttonPress(xcb_button_press_event_t * event) {
    ASSERT(event->event == _window, "Which window?");
    //PRINT("Button-press: " << event->event_x << " " << event->event_y);
    if (!_isOpen) { return; }

    if (_pressed) {
        ASSERT(event->detail != _button, "Already pressed!");
        return;
    }

    _pressed = true;

    if (_button != event->detail ||
        event->time - _lastPressTime > _config.getDoubleClickTimeout())
    {
        _pressCount = 1;
    }
    else {
        ++_pressCount;
    }

    _button        = event->detail;
    _lastPressTime = event->time;

    uint16_t row, col;
    bool within = xy2RowCol(event->event_x, event->event_y, row, col);

    switch (event->detail) {
        case XCB_BUTTON_INDEX_1:
            _terminal->buttonPress(Terminal::Button::LEFT, _pressCount,
                                   event->state, within, row, col);
            break;
        case XCB_BUTTON_INDEX_2:
            _terminal->buttonPress(Terminal::Button::MIDDLE, _pressCount,
                                   event->state, within, row, col);
            break;
        case XCB_BUTTON_INDEX_3:
            _terminal->buttonPress(Terminal::Button::RIGHT, _pressCount,
                                   event->state, within, row, col);
            break;
        case XCB_BUTTON_INDEX_4:
            _terminal->scrollWheel(Terminal::ScrollDir::UP, event->state);
            break;
        case XCB_BUTTON_INDEX_5:
            _terminal->scrollWheel(Terminal::ScrollDir::DOWN, event->state);
            // Scroll wheel
            break;
    }
}

void Window::buttonRelease(xcb_button_release_event_t * event) {
    ASSERT(event->event == _window, "Which window?");
    //PRINT("Button-release: " << event->event_x << " " << event->event_y);
    if (!_isOpen) { return; }

    if (_pressed && _button == event->detail) {
        _terminal->buttonRelease(false, event->state);
        _pressed = false;
    }
}

void Window::motionNotify(xcb_motion_notify_event_t * event) {
    ASSERT(event->event == _window, "Which window?");
    //PRINT("Motion-notify: " << event->event_x << " " << event->event_y);
    if (!_isOpen) { return; }
    if (!_pressed) { return; }

    int16_t x, y;

    if (event->detail == XCB_MOTION_HINT) {
        xcb_query_pointer_reply_t * pointer =
            xcb_query_pointer_reply(_basics.connection(),
                                    xcb_query_pointer(_basics.connection(), _window),
                                    nullptr);
        x = pointer->win_x;
        y = pointer->win_y;
        std::free(pointer);
    }
    else {
        x = event->event_x;
        y = event->event_y;
    }

    uint16_t row, col;
    bool within = xy2RowCol(x, y, row, col);

    if (_pointerRow != row || _pointerCol != col) {
        _pointerRow = row;
        _pointerCol = col;
        _terminal->buttonMotion(event->state, within, row, col);
    }

}

void Window::mapNotify(xcb_map_notify_event_t * UNUSED(event)) {
    //PRINT("Map");
    ASSERT(!_mapped, "");

    if (_config.getDoubleBuffer()) {
        _pixmap = xcb_generate_id(_basics.connection());
        xcb_void_cookie_t cookie = xcb_create_pixmap_checked(_basics.connection(),
                                                             _basics.screen()->root_depth,
                                                             _pixmap,
                                                             _window,
                                                             _width,
                                                             _height);
        if (xcb_request_failed(_basics.connection(), cookie, "Failed to create pixmap")) {
            FATAL("");
        }
    }

    _surface = cairo_xcb_surface_create(_basics.connection(),
                                        _config.getDoubleBuffer() ? _pixmap : _window,
                                        _basics.visual(),
                                        _width,
                                        _height);

    _mapped = true;
}

void Window::unmapNotify(xcb_unmap_notify_event_t * UNUSED(event)) {
    //PRINT("UnMap");
    ASSERT(_mapped, "");

    ASSERT(_surface, "");
    cairo_surface_finish(_surface);
    cairo_surface_destroy(_surface);
    _surface = nullptr;

    if (_config.getDoubleBuffer()) {
        ASSERT(_pixmap, "");
        xcb_void_cookie_t cookie = xcb_free_pixmap(_basics.connection(), _pixmap);
        if (xcb_request_failed(_basics.connection(), cookie, "Failed to free pixmap")) {
            FATAL("");
        }
        _pixmap = 0;
    }

    _mapped = false;
}

void Window::reparentNotify(xcb_reparent_notify_event_t * UNUSED(event)) {
    //PRINT("Reparent");
}

void Window::expose(xcb_expose_event_t * event) {
    ASSERT(event->window == _window, "Which window?");
    /*
    PRINT("Expose: " <<
          event->x << " " << event->y << " " <<
          event->width << " " << event->height);
          */

    if (_mapped) {
        if (_config.getDoubleBuffer() && _hadExpose) {
            ASSERT(_pixmap, "");
            xcb_copy_area(_basics.connection(),
                          _pixmap,
                          _window,
                          _gc,
                          event->x, event->y,
                          event->x, event->y,
                          event->width, event->height);
            xcb_flush(_basics.connection());
        }
        else {
            ASSERT(_surface, "");
            draw(event->x, event->y, event->width, event->height);
        }
    }

    _hadExpose = true;
}

void Window::configureNotify(xcb_configure_notify_event_t * event) {
    ASSERT(event->window == _window, "Which window?");

    // We are only interested in size changes.
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

    if (_mapped) {
        if (_config.getDoubleBuffer()) {
            ASSERT(_pixmap, "");
        }

        ASSERT(_surface, "");

        if (_config.getDoubleBuffer()) {
            cairo_surface_finish(_surface);
            cairo_surface_destroy(_surface);
            _surface = nullptr;

            xcb_void_cookie_t cookie;
            cookie = xcb_free_pixmap_checked(_basics.connection(), _pixmap);
            if (xcb_request_failed(_basics.connection(), cookie,
                                   "Failed to free pixmap")) {
                FATAL("");
            }
            _pixmap = 0;

            //

            _pixmap = xcb_generate_id(_basics.connection());
            cookie = xcb_create_pixmap_checked(_basics.connection(),
                                               _basics.screen()->root_depth,
                                               _pixmap,
                                               _window,
                                               _width,
                                               _height);
            if (xcb_request_failed(_basics.connection(), cookie,
                                   "Failed to create pixmap")) {
                FATAL("");
            }

            cairo_surface_finish(_surface);
            _surface = cairo_xcb_surface_create(_basics.connection(),
                                                _config.getDoubleBuffer() ? _pixmap : _window,
                                                _basics.visual(),
                                                _width,
                                                _height);
        }
        else {
            cairo_xcb_surface_set_size(_surface, _width, _height);
        }
    }

    uint16_t rows, cols;

    const int BORDER_THICKNESS = _config.getBorderThickness();
    const int SCROLLBAR_WIDTH  = _config.getScrollbarWidth();

    if (_width  > 2 * BORDER_THICKNESS + _fontSet.getWidth() + SCROLLBAR_WIDTH &&
        _height > 2 * BORDER_THICKNESS + _fontSet.getHeight())
    {
        uint16_t w = _width  - (2 * BORDER_THICKNESS + SCROLLBAR_WIDTH);
        uint16_t h = _height - (2 * BORDER_THICKNESS);

        rows = h / _fontSet.getHeight();
        cols = w / _fontSet.getWidth();
    }
    else {
        rows = cols = 1;
    }

    ASSERT(rows > 0 && cols > 0, "");

    if (_isOpen) {
        _tty->resize(rows, cols);
    }

    _terminal->resize(rows, cols);      // Ok to resize if not open?

    updateTitle();

    if (_mapped) {
        if (_config.getDoubleBuffer()) {
            ASSERT(_pixmap, "");
        }
        ASSERT(_surface, "");
        draw(0, 0, _width, _height);
    }
}

void Window::focusIn(xcb_focus_in_event_t * UNUSED(event)) {
    _focussed = true;
    // TODO damage cursor? Or tell temrinal?

    //PRINT("Focus in");
}

void Window::focusOut(xcb_focus_out_event_t * event) {
    _focussed = false;
    // TODO damage cursor? Or tell temrinal?

    PRINT("Focus out: " << int(event->mode));

}

void Window::enterNotify(xcb_enter_notify_event_t * UNUSED(event)) {
    //PRINT("enter");
}

void Window::leaveNotify(xcb_leave_notify_event_t * event) {
    PRINT("leave: " << int(event->mode));

    // XXX total guess that this is how we ensure we release
    // the button...
    if (event->mode == 2) {
        if (_pressed) {
            _terminal->buttonRelease(true, 0);
            _pressed = false;
        }
    }
}

void Window::visibilityNotify(xcb_visibility_notify_event_t * UNUSED(event)) {
}

void Window::destroyNotify(xcb_destroy_notify_event_t * event) {
    ASSERT(event->window == _window, "Which window?");
    //PRINT("Destroy notify");

    _tty->close();
    _isOpen    = false;
    _destroyed = true;
}

void Window::selectionClear(xcb_selection_clear_event_t * UNUSED(event)) {
    //PRINT("Selection clear");

    // TODO clear the selected region AND clear the selection copy buffer
}

void Window::selectionNotify(xcb_selection_notify_event_t * UNUSED(event)) {
    //PRINT("Selection notify");
    if (_isOpen) {
        uint32_t offset = 0;        // 32-bit quantities

        for (;;) {
            xcb_get_property_cookie_t cookie =
                xcb_get_property(_basics.connection(),
                                 false,     // delete
                                 _window,
                                 XCB_ATOM_PRIMARY,
                                 XCB_GET_PROPERTY_TYPE_ANY,
                                 offset,
                                 8192 / 4);

            xcb_get_property_reply_t * reply =
                xcb_get_property_reply(_basics.connection(), cookie, nullptr);
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
        xcb_change_property(_basics.connection(),
                            XCB_PROP_MODE_REPLACE,
                            event->requestor,
                            event->property,
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            &atomUtf8String);
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

        xcb_change_property(_basics.connection(),
                            XCB_PROP_MODE_REPLACE,
                            event->requestor,
                            event->property,
                            event->target,
                            8,
                            text.length(),
                            text.data());
        response.property = event->property;
    }

    xcb_send_event(_basics.connection(),
                   true,
                   event->requestor,
                   0,
                   reinterpret_cast<const char *>(&response));

    xcb_flush(_basics.connection());        // Required?
}

void Window::icccmConfigure() {
    //
    // machine
    //

    const std::string & hostname = _basics.hostname();
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

    std::string wm_class = "terminol";
    xcb_icccm_set_wm_class(_basics.connection(), _window,
                           wm_class.size(), wm_class.data());

    //
    // size
    //

    const int BORDER_THICKNESS = _config.getBorderThickness();
    const int SCROLLBAR_WIDTH  = _config.getScrollbarWidth();

    xcb_size_hints_t sizeHints;
    sizeHints.flags = 0;
    xcb_icccm_size_hints_set_min_size(&sizeHints,
                                      2 * BORDER_THICKNESS + _fontSet.getWidth() + SCROLLBAR_WIDTH,
                                      2 * BORDER_THICKNESS + _fontSet.getHeight());
    xcb_icccm_size_hints_set_resize_inc(&sizeHints,
                                        _fontSet.getWidth(),
                                        _fontSet.getHeight());
    xcb_icccm_size_hints_set_base_size(&sizeHints,
                                       2 * BORDER_THICKNESS,
                                       2 * BORDER_THICKNESS);
    xcb_icccm_size_hints_set_win_gravity(&sizeHints, XCB_GRAVITY_NORTH_WEST);
    // XXX or call xcb_icccm_set_wm_normal_hints() ?
    xcb_icccm_set_wm_size_hints(_basics.connection(),
                                _window,
                                XCB_ATOM_WM_NORMAL_HINTS,
                                &sizeHints);

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

    // TODO
}

void Window::rowCol2XY(uint16_t row, uint16_t col, int & x, int & y) const {
    ASSERT(row <= _terminal->getRows(), "");
    ASSERT(col <= _terminal->getCols(), "");

    const int BORDER_THICKNESS = _config.getBorderThickness();

    x = BORDER_THICKNESS + col * _fontSet.getWidth();
    y = BORDER_THICKNESS + row * _fontSet.getHeight();
}

bool Window::xy2RowCol(int x, int y, uint16_t & row, uint16_t & col) const {
    bool within = true;

    const int BORDER_THICKNESS = _config.getBorderThickness();

    // x / cols:

    if (x < BORDER_THICKNESS) {
        col = 0;
        within = false;
    }
    else if (x < BORDER_THICKNESS + _fontSet.getWidth() * _terminal->getCols()) {
        col = (x - BORDER_THICKNESS) / _fontSet.getWidth();
        ASSERT(col < _terminal->getCols(),
               "col is: " << col << ", getCols() is: " << _terminal->getCols());
    }
    else {
        col = _terminal->getCols();
        within = false;
    }

    // y / rows:

    if (y < BORDER_THICKNESS) {
        row = 0;
        within = false;
    }
    else if (y < BORDER_THICKNESS + _fontSet.getHeight() * _terminal->getRows()) {
        row = (y - BORDER_THICKNESS) / _fontSet.getHeight();
        ASSERT(row < _terminal->getRows(),
               "row is: " << row << ", getRows() is: " << _terminal->getRows());
    }
    else {
        row = _terminal->getRows();
        within = false;
    }

    return within;
}

void Window::updateTitle() {
    ASSERT(_terminal, "");

    std::ostringstream ost;

#if DEBUG
    ost << "<DEBUG> ";
#endif

    ost << "[" << _terminal->getCols() << 'x' << _terminal->getRows() << "] ";
    ost << _title;

    const std::string & fullTitle = ost.str();

#if 1
    xcb_icccm_set_wm_name(_basics.connection(),
                          _window,
                          XCB_ATOM_STRING,
                          8,
                          fullTitle.size(),
                          fullTitle.data());

    xcb_icccm_set_wm_icon_name(_basics.connection(),
                               _window,
                               XCB_ATOM_STRING,
                               8,
                               fullTitle.size(),
                               fullTitle.data());
#else
    xcb_ewmh_set_wm_name(_basics.ewmhConnection(),
                         _window,
                         fullTitle.size(),
                         fullTitle.data());
#endif
}

void Window::draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    ASSERT(_mapped, "");
    if (_config.getDoubleBuffer()) {
        ASSERT(_pixmap, "");
    }
    ASSERT(_surface, "");
    _cr = cairo_create(_surface);
    cairo_set_line_width(_cr, 1.0);

#if DEBUG
    // Clear the damaged area so that we know we are completely drawing to it.

    if (_config.getDoubleBuffer()) {
        xcb_rectangle_t rect = {
            static_cast<int16_t>(x), static_cast<int16_t>(y), w, h
        };

        xcb_poly_rectangle(_basics.connection(),
                           _pixmap,
                           _gc,
                           1,
                           &rect);
    }
    else {
        xcb_clear_area(_basics.connection(),
                       0,       // don't generate exposure event
                       _window,
                       x, y, w, h);
    }
#endif

    cairo_save(_cr); {
        cairo_rectangle(_cr, x, y, w, h);       // implicit cast to double
        cairo_clip(_cr);

        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));

        drawBorder();

        uint16_t rowBegin, colBegin;
        xy2RowCol(x, y, rowBegin, colBegin);
        uint16_t rowEnd, colEnd;
        xy2RowCol(x + w, y + h, rowEnd, colEnd);
        if (colEnd != _terminal->getCols()) { ++colEnd; }
        if (rowEnd != _terminal->getRows()) { ++rowEnd; }
        _terminal->redraw(rowBegin, rowEnd, colBegin, colEnd);

        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));

    } cairo_restore(_cr);
    cairo_destroy(_cr);
    _cr = nullptr;

    cairo_surface_flush(_surface);      // Useful?

    if (_config.getDoubleBuffer()) {
        xcb_copy_area(_basics.connection(),
                      _pixmap,
                      _window,
                      _gc,
                      x, y, // src
                      x, y, // dst
                      w, h);
    }

    xcb_flush(_basics.connection());
}

void Window::drawBorder() {
    const int BORDER_THICKNESS = _config.getBorderThickness();
    const int SCROLLBAR_WIDTH  = _config.getScrollbarWidth();

    cairo_save(_cr); {
        const auto & bgValues = _colorSet.getBorderColor();
        cairo_set_source_rgb(_cr, bgValues.r, bgValues.g, bgValues.b);

        double x1 = BORDER_THICKNESS + _fontSet.getWidth() * _terminal->getCols();
        double x2 = _width - SCROLLBAR_WIDTH;

        double y1 = BORDER_THICKNESS + _fontSet.getHeight() * _terminal->getRows();
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

// Terminal::I_Observer implementation:

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

    xcb_set_selection_owner(_basics.connection(), _window,
                            atom, XCB_CURRENT_TIME);
    xcb_flush(_basics.connection());
}

void Window::terminalPaste(bool clipboard) throw () {
    //PRINT("Copy clipboard: " << clipboard);

    xcb_atom_t atom = clipboard ? _basics.atomClipboard() : _basics.atomPrimary();

    xcb_convert_selection(_basics.connection(),
                          _window,
                          atom,
                          _basics.atomUtf8String(),
                          XCB_ATOM_PRIMARY, // property
                          XCB_CURRENT_TIME);

    xcb_flush(_basics.connection());
}

void Window::terminalResizeFont(int delta) throw () {
    PRINT("Resize font: " << delta);
}

void Window::terminalResetTitle() throw () {
    _title = _config.getTitle();
    updateTitle();
}

void Window::terminalSetTitle(const std::string & title) throw () {
    //PRINT("Set title: " << title);
    _title = title;
    updateTitle();
}

void Window::terminalResizeBuffer(uint16_t rows, uint16_t cols) throw () {
    NYI("Terminal resize: rows=" << rows << ", cols=" << cols);
    // Thoughts: this is tricky because we are at the mercy of the window
    // manager and our resize request requires a round trip to the X server.
    // Do we need to try to complete the resize before we return? i.e. handle
    // the configure event, etc...

    xcb_ewmh_request_moveresize_window(_basics.ewmhConnection(),
                                       _basics.screenNum(),
                                       _window,
                                       XCB_GRAVITY_NORTH_WEST,
                                       XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL,
                                       static_cast<xcb_ewmh_moveresize_window_opt_flags_t>
                                       (XCB_EWMH_MOVERESIZE_WINDOW_WIDTH |
                                        XCB_EWMH_MOVERESIZE_WINDOW_HEIGHT),
                                       0, 0,        // x,y
                                       100, 100);
}

bool Window::terminalFixDamageBegin(bool internal) throw () {
    //PRINT("Damage begin, internal: " << std::boolalpha << internal);

    if (internal) {
        if (_mapped) {
            ASSERT(_mapped, "");
            ASSERT(_surface, "");
#if 0
            xcb_clear_area(_basics.connection(),
                           0,       // don't generate exposure event
                           _window,
                           0, 0, _width, _height);
#endif
            _cr = cairo_create(_surface);
            cairo_set_line_width(_cr, 1.0);
            return true;
        }
        else {
            return false;
        }
    }
    else {
        ASSERT(_mapped, "");
        if (_config.getDoubleBuffer()) {
            ASSERT(_pixmap, "");
        }
        ASSERT(_surface, "");
        ASSERT(_cr, "");
        return true;
    }
}

void Window::terminalDrawRun(uint16_t        row,
                             uint16_t        col,
                             Style           style,
                             const uint8_t * str,
                             size_t          count) throw () {
    ASSERT(_cr, "");

    cairo_save(_cr); {
        if (style.attrs.get(Attr::INVERSE)) { std::swap(style.fg, style.bg); }

        cairo_set_scaled_font(_cr, _fontSet.get(style.attrs.get(Attr::ITALIC),
                                                style.attrs.get(Attr::BOLD)));

        int x, y;
        rowCol2XY(row, col, x, y);

        const auto & bgValues = _colorSet.getIndexedColor(style.bg);
        cairo_set_source_rgb(_cr, bgValues.r, bgValues.g, bgValues.b);
        cairo_rectangle(_cr,
                        x,
                        y,
                        count * _fontSet.getWidth(),
                        _fontSet.getHeight());
        cairo_fill(_cr);

        const auto & fgValues = _colorSet.getIndexedColor(style.fg);
        cairo_set_source_rgba(_cr, fgValues.r, fgValues.g, fgValues.b,
                              style.attrs.get(Attr::CONCEAL) ? 0.2 : 1.0);

        if (style.attrs.get(Attr::UNDERLINE)) {
            cairo_move_to(_cr,
                          x,
                          y + _fontSet.getHeight() - 0.5);
            cairo_line_to(_cr,
                          x + count * _fontSet.getWidth(),
                          y + _fontSet.getHeight() - 0.5);
            cairo_stroke(_cr);
        }

        cairo_move_to(_cr, x, y + _fontSet.getAscent());
        cairo_show_text(_cr, reinterpret_cast<const char *>(str));

        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));
    } cairo_restore(_cr);
}

void Window::terminalDrawCursor(uint16_t        row,
                                uint16_t        col,
                                Style           style,
                                const uint8_t * str,
                                bool            wrapNext) throw () {
    ASSERT(_cr, "");

    if (style.attrs.get(Attr::INVERSE)) { std::swap(style.fg, style.bg); }

    const auto & fgValues =
        _config.getCustomCursorTextColor() ?
        _colorSet.getCursorTextColor() :
        _colorSet.getIndexedColor(style.bg);

    const auto & bgValues =
        _config.getCustomCursorFillColor() ?
        _colorSet.getCursorFillColor() :
        _colorSet.getIndexedColor(style.fg);

    cairo_save(_cr); {
        cairo_set_scaled_font(_cr, _fontSet.get(style.attrs.get(Attr::ITALIC),
                                                style.attrs.get(Attr::BOLD)));

        int x, y;
        rowCol2XY(row, col, x, y);

        cairo_set_source_rgba(_cr, bgValues.r, bgValues.g, bgValues.b,
                              wrapNext ? 0.4 : 1.0);
        cairo_rectangle(_cr, x, y, _fontSet.getWidth(), _fontSet.getHeight());
        cairo_fill(_cr);

        cairo_set_source_rgb(_cr, fgValues.r, fgValues.g, fgValues.b);
        cairo_move_to(_cr, x, y + _fontSet.getAscent());
        cairo_show_text(_cr, reinterpret_cast<const char *>(str));

        ASSERT(cairo_status(_cr) == 0,
               "Cairo error: " << cairo_status_to_string(cairo_status(_cr)));
    } cairo_restore(_cr);
}

void drawLineSelection1(cairo_t * cr, double x, double y, double w, double h, double c) {
#if 0
    cairo_move_to   (cr, x + c    , y);
    cairo_line_to   (cr, x + w - c, y);
    cairo_curve_to  (cr, x + w    , y    , x + w, y, x + w, y + c);
    cairo_line_to   (cr, x + w    , y + h - c);
    cairo_curve_to  (cr, x + w    , y + h, x + w, y + h, x + w - c, y + h);
    cairo_line_to   (cr, x + c    , y + h);
    cairo_curve_to  (cr, x        , y + h, x,     y + h, x,         y + h - c);
    cairo_line_to   (cr, x        , y + c);
    cairo_curve_to  (cr, x        , y    , x,     y    , x + c,       y);
    cairo_close_path(cr);
#else
    cairo_move_to   (cr, x + c    , y);
    cairo_line_to   (cr, x + w, y);
    cairo_line_to   (cr, x + w    , y + h - c);
    cairo_curve_to  (cr, x + w    , y + h, x + w, y + h, x + w - c, y + h);
    cairo_line_to   (cr, x    , y + h);
    cairo_line_to   (cr, x        , y + c);
    cairo_curve_to  (cr, x        , y    , x,     y    , x + c,       y);
    cairo_close_path(cr);
#endif
}

void drawLineSelection2(cairo_t * cr, double x, double y, double w, double h, double c) {
    c = std::min(c, std::min(w, h) / 3.0);

    double d = M_PI / 180.0;

#if 0
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + c, y + c, c, 180 * d, 270 * d);
    cairo_arc(cr, x + w - c, y + c, c, -90 * d, 0 * d);
    cairo_arc(cr, x + w - c, y + h - c, c, 0 * d, 90 * d);
    cairo_arc(cr, x + c, y + h - c, c, 90 * d, 180 * d);
    cairo_close_path(cr);
#else
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + c, y + c, c, 180 * d, 270 * d);
    cairo_line_to(cr, x + w, y);
    cairo_arc(cr, x + w - c, y + h - c, c, 0 * d, 90 * d);
    cairo_line_to(cr, x, y + h);
    cairo_close_path(cr);
#endif
}

void Window::terminalDrawSelection(uint16_t rowBegin,
                                   uint16_t colBegin,
                                   uint16_t rowEnd,
                                   uint16_t colEnd,
                                   bool     topless,
                                   bool     bottomless) throw () {
    ASSERT(!topless    || rowBegin == 0, "");
    ASSERT(!bottomless || rowEnd   == _terminal->getRows(), "");
    ASSERT(rowBegin < rowEnd, "");

    ASSERT(_cr, "");

    double lineWidth = 1.0;
    double halfWidth = lineWidth / 2.0;
    double curve = 4.0;

    if (rowBegin + 1 == rowEnd) {
        ASSERT(colBegin < colEnd, "");

        int x0, y0;
        rowCol2XY(rowBegin, colBegin, x0, y0);
        int x1, y1;
        rowCol2XY(rowEnd, colEnd, x1, y1);

        drawLineSelection2(_cr,
                           x0 + halfWidth, y0 + halfWidth,
                           x1 - x0 - lineWidth, y1 - y0 - lineWidth,
                           curve);

        cairo_set_source_rgba(_cr, 0.3, 0.1, 0.8, 0.2);
        cairo_fill_preserve(_cr);

        cairo_set_source_rgba(_cr, 0.7, 0.5, 0.8, 1.0);
        cairo_stroke(_cr);
    }
    else {
        // There are 8 distinct points, but they are defined by
        // 8 coordinates (not 16).
        // The general shape is:
        //
        //      0       1      2           3
        //
        //   0          0------------------1
        //   1  6-------7                  |
        //      |                          |
        //   2  |              3-----------2
        //   3  5--------------4
        //
        // Points are:
        //
        //   #: x, y
        //   -------
        //   0: 1, 0
        //   1: 3, 0
        //   2: 3, 2
        //   3: 2, 2
        //   4: 2, 3
        //   5: 0, 3
        //   6: 0, 1
        //   7: 1, 1

        int x0, x1, x2, x3;
        int y0, y1, y2, y3;

        uint16_t numCols = _terminal->getCols();

        rowCol2XY(rowBegin,     0,        x0, y0);  // top left
        rowCol2XY(rowBegin + 1, colBegin, x1, y1);  // #7
        rowCol2XY(rowEnd - 1,   colEnd,   x2, y2);  // #3
        rowCol2XY(rowEnd,       numCols,  x3, y3);  // bottom right

        cairo_move_to(_cr, x1, y0);
        cairo_line_to(_cr, x3, y0);
        cairo_line_to(_cr, x3, y2);
        cairo_line_to(_cr, x2, y2);
        cairo_line_to(_cr, x2, y3);
        cairo_line_to(_cr, x0, y3);
        cairo_line_to(_cr, x0, y1);
        cairo_line_to(_cr, x1, y1);
        cairo_close_path(_cr);

        cairo_set_source_rgba(_cr, 0.3, 0.1, 0.8, 0.2);
        cairo_fill(_cr);
    }
}

void Window::terminalDrawScrollbar(size_t   totalRows,
                                   size_t   historyOffset,
                                   uint16_t visibleRows) throw () {
    ASSERT(_cr, "");

    const int SCROLLBAR_WIDTH  = _config.getScrollbarWidth();

    double x = static_cast<double>(_width - SCROLLBAR_WIDTH);
    double y = 0.0;
    double h = static_cast<double>(_height);
    double w = static_cast<double>(SCROLLBAR_WIDTH);

    // Draw the gutter.

    const auto & bgValues = _colorSet.getScrollBarBgColor();
    cairo_set_source_rgb(_cr, bgValues.r, bgValues.g, bgValues.b);

    cairo_rectangle(_cr,
                    x,
                    y,
                    w,
                    h);
    cairo_fill(_cr);

    // Draw the bar.

    double yBar = static_cast<double>(historyOffset) / static_cast<double>(totalRows) * h;
    double hBar = static_cast<double>(visibleRows)   / static_cast<double>(totalRows) * h;

    const auto & fgValues = _colorSet.getScrollBarFgColor();
    cairo_set_source_rgb(_cr, fgValues.r, fgValues.g, fgValues.b);

    cairo_rectangle(_cr,
                    x + 1.0,
                    yBar,
                    w - 2.0,
                    hBar);
    cairo_fill(_cr);
}

void Window::terminalFixDamageEnd(bool     internal,
                                  uint16_t rowBegin,
                                  uint16_t rowEnd,
                                  uint16_t colBegin,
                                  uint16_t colEnd,
                                  bool     scrollBar) throw () {
    ASSERT(_cr, "");

    if (internal) {
        cairo_destroy(_cr);
        _cr = nullptr;

        cairo_surface_flush(_surface);      // Useful?

        if (_config.getDoubleBuffer()) {
            int x0, y0;
            rowCol2XY(rowBegin, colBegin, x0, y0);
            int x1, y1;
            rowCol2XY(rowEnd, colEnd, x1, y1);

            if (scrollBar) {
                // Expand the region to include the scroll bar
                y0 = 0;
                x1 = _width;
                y1 = _height;
            }

            // Copy the buffer region
            xcb_copy_area(_basics.connection(),
                          _pixmap,
                          _window,
                          _gc,
                          x0, y0,   // src
                          x0, y0,   // dst
                          x1 - x0, y1 - y0);
        }

        //xcb_flush(_basics.connection());
        xcb_aux_sync(_basics.connection());
    }
}

void Window::terminalChildExited(int exitStatus) throw () {
    PRINT("Child exited: " << exitStatus);
    _isOpen = false;
}

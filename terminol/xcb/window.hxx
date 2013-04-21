// vi:noai:sw=4

#ifndef WINDOW__HXX
#define WINDOW__HXX

#include "terminol/xcb/color_set.hxx"
#include "terminol/xcb/key_map.hxx"
#include "terminol/xcb/font_set.hxx"
#include "terminol/common/tty.hxx"
#include "terminol/common/terminal.hxx"
#include "terminol/common/support.hxx"

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <cairo-xcb.h>
#include <cairo-ft.h>

class X_Window :
    protected Terminal::I_Observer,
    protected Uncopyable
{
    static const int           BORDER_THICKNESS;
    static const int           SCROLLBAR_WIDTH;
    static const std::string   DEFAULT_TITLE;

    xcb_connection_t  * _connection;
    xcb_screen_t      * _screen;
    xcb_key_symbols_t * _keySymbols;
    xcb_visualtype_t  * _visual;
    const X_ColorSet  & _colorSet;
    const X_KeyMap    & _keyMap;
    X_FontSet         & _fontSet;
    xcb_window_t        _window;
    xcb_gcontext_t      _gc;
    uint16_t            _width;
    uint16_t            _height;
    Tty               * _tty;
    Terminal          * _terminal;
    bool                _isOpen;
    uint16_t            _pointerRow;
    uint16_t            _pointerCol;
    bool                _damage;
    xcb_pixmap_t        _pixmap;
    cairo_surface_t   * _surface;

public:
    struct Error {      // FIXME constructor never throws ATM
        explicit Error(const std::string & message_) : message(message_) {}
        std::string message;
    };

    X_Window(xcb_connection_t   * connection,
             xcb_screen_t       * screen,
             xcb_key_symbols_t  * keySymbols,
             xcb_visualtype_t   * visual,
             const X_ColorSet   & colorSet,
             const X_KeyMap     & keyMap,
             X_FontSet          & fontSet,
             const std::string  & term,
             const Tty::Command & command) throw (Error);

    virtual ~X_Window();

    // We handle these:

    bool isOpen() const { return _isOpen; }
    int  getFd() { return _tty->getFd(); }

    // The following calls are forwarded to the Terminal.

    void read()                  { _terminal->read(); }
    bool areWritesQueued() const { return _terminal->areWritesQueued(); }
    void flush()                 { _terminal->flush(); }

    // Events:

    void keyPress(xcb_key_press_event_t * event);
    void keyRelease(xcb_key_release_event_t * event);
    void buttonPress(xcb_button_press_event_t * event);
    void buttonRelease(xcb_button_release_event_t * event);
    void motionNotify(xcb_motion_notify_event_t * event);
    void mapNotify(xcb_map_notify_event_t * event);
    void unmapNotify(xcb_unmap_notify_event_t * event);
    void reparentNotify(xcb_reparent_notify_event_t * event);
    void expose(xcb_expose_event_t * event);
    void configureNotify(xcb_configure_notify_event_t * event);
    void focusIn(xcb_focus_in_event_t * event);
    void focusOut(xcb_focus_out_event_t * event);
    void enterNotify(xcb_enter_notify_event_t * event);
    void leaveNotify(xcb_leave_notify_event_t * event);
    void visibilityNotify(xcb_visibility_notify_event_t & event);
    void destroyNotify(xcb_destroy_notify_event_t & event);

protected:
    void rowCol2XY(uint16_t row, uint16_t col, int & x, int & y) const;
    bool xy2RowCol(int x, int y, uint16_t & row, uint16_t & col) const;

    void draw(uint16_t ix, uint16_t iy, uint16_t iw, uint16_t ih);
    void setTitle(const std::string & title);

    void drawBuffer(cairo_t * cr);
    void drawSelection(cairo_t * cr);
    void drawUtf8(cairo_t    * cr,
                  uint16_t     row,
                  uint16_t     col,
                  uint8_t      fg,
                  uint8_t      bg,
                  AttributeSet attr,
                  const char * str,
                  size_t       count,
                  size_t       size);
    void drawCursor(cairo_t * cr);

    // Terminal::I_Observer implementation:

    void terminalBegin() throw ();
    void terminalDamageCells(uint16_t row, uint16_t col0, uint16_t col1) throw ();
    void terminalDamageAll() throw ();
    void terminalResetTitle() throw ();
    void terminalSetTitle(const std::string & title) throw ();
    void terminalChildExited(int exitStatus) throw ();
    void terminalEnd() throw ();
};

#endif // WINDOW__HXX
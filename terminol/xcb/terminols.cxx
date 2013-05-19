// vi:noai:sw=4

#include "terminol/xcb/window.hxx"
#include "terminol/xcb/color_set.hxx"
#include "terminol/xcb/font_set.hxx"
#include "terminol/xcb/basics.hxx"
#include "terminol/common/config.hxx"
#include "terminol/common/key_map.hxx"
#include "terminol/support/debug.hxx"
#include "terminol/support/pattern.hxx"

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>

#include <unistd.h>
#include <sys/select.h>

class I_Creator {
public:
    virtual void create() throw () = 0;

protected:
    I_Creator() {}
    ~I_Creator() {}
};

class EventLoop :
    public I_Creator,
    protected Uncopyable
{
    typedef std::map<xcb_window_t, Window *> Windows;

    const Config & _config;
    Basics         _basics;
    ColorSet       _colorSet;
    FontSet        _fontSet;
    KeyMap         _keyMap;
    Windows        _windows;

public:
    struct Error {
        explicit Error(const std::string & message_) : message(message_) {}
        std::string message;
    };

    explicit EventLoop(const Config & config)
        throw (Basics::Error, FontSet::Error, Error) :
        _config(config),
        _basics(),
        _colorSet(config, _basics),
        _fontSet(config),
        _keyMap(_basics.maskShift(),
                _basics.maskAlt(),
                _basics.maskControl())
    {
        create();
        create();
        create();
        loop();
    }

    virtual ~EventLoop() {
    }

protected:
    void loop() throw (Error) {
        for (;;) {
            int fdMax = 0;
            fd_set readFds, writeFds;
            FD_ZERO(&readFds); FD_ZERO(&writeFds);

            int xFd = xcb_get_file_descriptor(_basics.connection());

            // Select for read on X11
            FD_SET(xFd, &readFds);
            fdMax = std::max(fdMax, xFd);

            // Select for read (and possibly write) on TTYs
            for (auto pair : _windows) {
                auto window = pair.second;
                int wFd = window->getFd();

                FD_SET(wFd, &readFds);
                if (window->needsFlush()) { FD_SET(wFd, &writeFds); }
                fdMax = std::max(fdMax, wFd);
            }

            ENFORCE_SYS(TEMP_FAILURE_RETRY(
                ::select(fdMax + 1, &readFds, &writeFds, nullptr, nullptr)) != -1, "");

            // Handle all.

            for (auto pair : _windows) {
                auto window = pair.second;
                int wFd = window->getFd();

                if (FD_ISSET(wFd, &writeFds)) { window->flush(); }
                if (FD_ISSET(wFd, &readFds) && window->isOpen()) { window->read(); }
            }

            if (FD_ISSET(xFd, &readFds)) { xevent(); }
            else { /* XXX */ xevent(); }

            // Purge the closed windows.

            std::vector<xcb_window_t> closedIds;

            for (auto pair : _windows) {
                auto window = pair.second;
                if (!window->isOpen()) { closedIds.push_back(window->getWindowId()); }
            }

            for (auto id : closedIds) {
                auto iter = _windows.find(id);
                ASSERT(iter != _windows.end(), "");
                Window * window = iter->second;
                delete window;
                _windows.erase(iter);
            }
        }
    }

    void xevent() throw (Error) {
        xcb_generic_event_t * event;

        while ((event = ::xcb_poll_for_event(_basics.connection()))) {
            auto    guard         = scopeGuard([event] { std::free(event); });
            uint8_t response_type = XCB_EVENT_RESPONSE_TYPE(event);
            if (response_type == 0) { throw Error("Lost connection (2)?"); }
            dispatch(response_type & ~0x80, event);
        }

        if (xcb_connection_has_error(_basics.connection())) {
            throw Error("Lost connection (1)?");
        }
    }

    void dispatch(uint8_t response_type, xcb_generic_event_t * event) {
        switch (response_type & ~0x80) {
            case XCB_KEY_PRESS: {
                auto e = reinterpret_cast<xcb_key_press_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->keyPress(e); }
                break;
            }
            case XCB_KEY_RELEASE: {
                auto e = reinterpret_cast<xcb_key_release_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->keyRelease(e); }
                break;
            }
            case XCB_BUTTON_PRESS: {
                auto e = reinterpret_cast<xcb_button_press_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->buttonPress(e); }
                break;
            }
            case XCB_BUTTON_RELEASE: {
                auto e = reinterpret_cast<xcb_button_release_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->buttonRelease(e); }
                break;
            }
            case XCB_MOTION_NOTIFY: {
                auto e = reinterpret_cast<xcb_motion_notify_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->motionNotify(e); }
                break;
            }
            case XCB_EXPOSE: {
                auto e = reinterpret_cast<xcb_expose_event_t *>(event);
                auto i = _windows.find(e->window);
                if (i != _windows.end()) { i->second->expose(e); }
                break;
            }
            /*
               case XCB_GRAPHICS_EXPOSURE:
               PRINT("Got graphics exposure");
               break;
               case XCB_NO_EXPOSURE:
               PRINT("Got no exposure");
               break;
               */
            case XCB_ENTER_NOTIFY: {
                auto e = reinterpret_cast<xcb_enter_notify_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->enterNotify(e); }
                break;
            }
            case XCB_LEAVE_NOTIFY: {
                auto e = reinterpret_cast<xcb_leave_notify_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->leaveNotify(e); }
                break;
            }
            case XCB_FOCUS_IN: {
                auto e = reinterpret_cast<xcb_focus_in_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->focusIn(e); }
                break;
            }
            case XCB_FOCUS_OUT: {
                auto e = reinterpret_cast<xcb_focus_in_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->focusOut(e); }
                break;
            }
            case XCB_MAP_NOTIFY: {
                auto e = reinterpret_cast<xcb_map_notify_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->mapNotify(e); }
                break;
            }
            case XCB_UNMAP_NOTIFY: {
                auto e = reinterpret_cast<xcb_unmap_notify_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->unmapNotify(e); }
                break;
            }
            case XCB_REPARENT_NOTIFY: {
                auto e = reinterpret_cast<xcb_reparent_notify_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->reparentNotify(e); }
                break;
            }
            case XCB_CONFIGURE_NOTIFY: {
                auto e = reinterpret_cast<xcb_configure_notify_event_t *>(event);
                auto i = _windows.find(e->event);
                if (i != _windows.end()) { i->second->configureNotify(e); }
                break;
            }
            case XCB_VISIBILITY_NOTIFY: {
                auto e = reinterpret_cast<xcb_visibility_notify_event_t *>(event);
                auto i = _windows.find(e->window);
                if (i != _windows.end()) { i->second->visibilityNotify(e); }
                break;
            }
            case XCB_DESTROY_NOTIFY: {
                auto e = reinterpret_cast<xcb_destroy_notify_event_t *>(event);
                auto i = _windows.find(e->window);
                if (i != _windows.end()) { i->second->destroyNotify(e); }
                break;
            }
            case XCB_SELECTION_CLEAR: {
                auto e = reinterpret_cast<xcb_selection_clear_event_t *>(event);
                auto i = _windows.find(e->owner);
                if (i != _windows.end()) { i->second->selectionClear(e); } // XXX
                break;
            }
            case XCB_SELECTION_NOTIFY: {
                auto e = reinterpret_cast<xcb_selection_notify_event_t *>(event);
                auto i = _windows.find(e->requestor);
                if (i != _windows.end()) { i->second->selectionNotify(e); } // XXX
                break;
            }
            default:
                PRINT("Unrecognised event: " << static_cast<int>(response_type));
                break;
        }
    }

    // I_Creator implementatin:

    void create() throw () {
        try {
            Window * window = new Window(_config, _basics, _colorSet, _fontSet, _keyMap);
            _windows.insert(std::make_pair(window->getWindowId(), window));
        }
        catch (const Window::Error & ex) {
            PRINT("Failed to create window: " << ex.message);
        }
    }
};

//
//
//

namespace {

bool argMatch(const std::string & arg, const std::string & opt, std::string & val) {
    std::string optComposed = "--" + opt + "=";
    if (arg.substr(0, optComposed.size()) ==  optComposed) {
        val = arg.substr(optComposed.size());
        return true;
    }
    else {
        return false;
    }
}

void showHelp(const std::string & progName, std::ostream & ost) {
    ost << "Usage:" << std::endl
        << "  " << progName << " \\" << std::endl
        << "    " << "--font=FONT --term=TERM --geometry=GEOMETRY \\" << std::endl
        << "    " << "--double-buffer --trace --sync --execute ARG0 ARG1..."
        << std::endl;
}

} // namespace {anonymous}

int main(int argc, char * argv[]) {
    // Command line

    Config       config;

    for (int i = 1; i != argc; ++i) {
        std::string arg = argv[i];
        std::string val;

        if (arg == "--double-buffer") {
            config.setDoubleBuffer(true);
        }
        else if (arg == "--trace") {
            config.setTraceTty(true);
        }
        else if (arg == "--sync") {
            config.setSyncTty(true);
        }
        else if (argMatch(arg, "font", val)) {
            config.setFontName(val);
        }
        else if (argMatch(arg, "term", val)) {
            config.setTermName(val);
        }
        else if (argMatch(arg, "geometry", val)) {
            // WidthxHeight+XPos+YPos
            config.setGeometryString(val);
        }
        else if (arg == "--help") {
            showHelp(argv[0], std::cout);
            return 0;
        }
        else {
            std::cerr << "Unrecognised argument '" << arg << "'" << std::endl;
            showHelp(argv[0], std::cerr);
            return 2;
        }
    }

    FcInit();

    try {
        EventLoop eventLoop(config);
    }
    catch (const EventLoop::Error & ex) {
        FATAL(ex.message);
    }
    catch (const FontSet::Error & ex) {
        FATAL(ex.message);
    }
    catch (const Basics::Error & ex) {
        FATAL(ex.message);
    }

    FcFini();

    return 0;
}


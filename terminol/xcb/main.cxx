// vi:noai:sw=4

#include "terminol/xcb/window.hxx"
#include "terminol/xcb/color_set.hxx"
#include "terminol/xcb/key_map.hxx"
#include "terminol/xcb/font_set.hxx"
#include "terminol/xcb/basics.hxx"
#include "terminol/common/support.hxx"

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <cairo-ft.h>

#include <fontconfig/fontconfig.h>

#include <unistd.h>
#include <sys/select.h>

class X_EventLoop : protected Uncopyable {
    X_Basics   _basics;
    X_ColorSet _colorSet;
    X_KeyMap   _keyMap;
    X_FontSet  _fontSet;
    X_Window   _window;
public:
    X_EventLoop(const std::string  & fontName,
                const std::string  & term,
                const Tty::Command & command)
        throw (X_Basics::Error, X_FontSet::Error, X_Window::Error) :
        _basics(),
        _colorSet(),
        _keyMap(),
        _fontSet(fontName),
        _window(_basics.connection(),
                _basics.screen(),
                _basics.keySymbols(),
                _basics.visual(),
                _colorSet,
                _keyMap,
                _fontSet,
                term,
                command)
    {
        loop();
    }

protected:
    void loop() {
        while(_window.isOpen()) {
            int fdMax = 0;
            fd_set readFds, writeFds;
            FD_ZERO(&readFds); FD_ZERO(&writeFds);

            FD_SET(xcb_get_file_descriptor(_basics.connection()), &readFds);
            fdMax = std::max(fdMax, xcb_get_file_descriptor(_basics.connection()));

            FD_SET(_window.getFd(), &readFds);
            fdMax = std::max(fdMax, _window.getFd());

            bool selectOnWrite = _window.areWritesQueued();
            if (selectOnWrite) {
                FD_SET(_window.getFd(), &writeFds);
                fdMax = std::max(fdMax, _window.getFd());
            }

            //PRINT("Calling select");
            ENFORCE_SYS(TEMP_FAILURE_RETRY(
                ::select(fdMax + 1, &readFds, &writeFds, nullptr, nullptr)) != -1, "");
            //PRINT("SELECT returned");

            // Handle _one_ I/O. XXX this is sub-optimal

            if (selectOnWrite && FD_ISSET(_window.getFd(), &writeFds)) {
                //PRINT("window write event");
                _window.flush();
            }
            else if (FD_ISSET(xcb_get_file_descriptor(_basics.connection()), &readFds)) {
                //PRINT("xevent");
                xevent();
            }
            else if (FD_ISSET(_window.getFd(), &readFds)) {
                //PRINT("window read event");
                _window.read();
            }
            else {
                FATAL("Unreachable");
            }
        }
    }

protected:
    void xevent() {
        xcb_generic_event_t * event;
        while ((event = ::xcb_poll_for_event(_basics.connection()))) {

        //bool send_event = XCB_EVENT_SENT(event);
        uint8_t response_type = XCB_EVENT_RESPONSE_TYPE(event);

        ASSERT(response_type != 0, "Error (according to awesome).");

        switch (response_type) {
            case XCB_KEY_PRESS:
                _window.keyPress(reinterpret_cast<xcb_key_press_event_t *>(event));
                break;
            case XCB_KEY_RELEASE:
                _window.keyRelease(reinterpret_cast<xcb_key_release_event_t *>(event));
                break;
            case XCB_BUTTON_PRESS:
                _window.buttonPress(reinterpret_cast<xcb_button_press_event_t *>(event));
                break;
            case XCB_BUTTON_RELEASE:
                _window.buttonRelease(reinterpret_cast<xcb_button_release_event_t *>(event));
                break;
            case XCB_MOTION_NOTIFY:
                _window.motionNotify(reinterpret_cast<xcb_motion_notify_event_t *>(event));
                break;
            case XCB_EXPOSE:
                _window.expose(reinterpret_cast<xcb_expose_event_t *>(event));
                break;
            case XCB_GRAPHICS_EXPOSURE:
                PRINT("Got graphics exposure");
                break;
            case XCB_NO_EXPOSURE:
                PRINT("Got no exposure");
                break;
            case XCB_ENTER_NOTIFY:
                _window.enterNotify(reinterpret_cast<xcb_enter_notify_event_t *>(event));
                break;
            case XCB_LEAVE_NOTIFY:
                _window.leaveNotify(reinterpret_cast<xcb_leave_notify_event_t *>(event));
                break;
            case XCB_FOCUS_IN:
                _window.focusIn(reinterpret_cast<xcb_focus_in_event_t *>(event));
                break;
            case XCB_FOCUS_OUT:
                _window.focusOut(reinterpret_cast<xcb_focus_out_event_t *>(event));
                break;
            case XCB_MAP_NOTIFY:
                _window.mapNotify(reinterpret_cast<xcb_map_notify_event_t *>(event));
                break;
            case XCB_UNMAP_NOTIFY:
                _window.unmapNotify(reinterpret_cast<xcb_unmap_notify_event_t *>(event));
                break;
            case XCB_REPARENT_NOTIFY:
                _window.reparentNotify(reinterpret_cast<xcb_reparent_notify_event_t *>(event));
                break;
            case XCB_CONFIGURE_NOTIFY:
                _window.configureNotify(reinterpret_cast<xcb_configure_notify_event_t *>(event));
                break;
            default:
                PRINT("Unrecognised event: " << static_cast<int>(response_type));
                break;
        }
        std::free(event);
    }
    }
};

//
//
//

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

int main(int argc, char * argv[]) {
    // Command line

    std::string  fontName          = "inconsolata:pixelsize=20";
    //std::string  fontName          = "mono:pixelsize=20";
    std::string  geometryStr;
    std::string  term              = "ansi";
    Tty::Command command;
    bool         accumulateCommand = false;

    for (int i = 1; i != argc; ++i) {
        std::string arg = argv[i];
        if      (accumulateCommand)                      { command.push_back(arg);   }
        else if (arg == "--execute")                     { accumulateCommand = true; }
        else if (argMatch(arg, "font", fontName))        {}
        else if (argMatch(arg, "term", term))            {}
        else if (argMatch(arg, "geometry", geometryStr)) {}
        else {
            std::cerr
                << "Unrecognised argument '" << arg << "'" << std::endl
                << "Try: --font=FONT --term=TERM --geometry=GEOMETRY --execute ARG0 ARG1..."
                << std::endl;
            return 2;
        }
    }

    FcInit();

    try {
        X_EventLoop eventLoop(fontName, term, command);
    }
    catch (X_Window::Error & ex) {
        FATAL(ex.message);
    }
    catch (X_FontSet::Error & ex) {
        FATAL(ex.message);
    }
    catch (X_Basics::Error & ex) {
        FATAL(ex.message);
    }

    FcFini();

    return 0;
}
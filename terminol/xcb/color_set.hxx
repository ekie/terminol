// vi:noai:sw=4

#ifndef XCB__COLOR_SET__H
#define XCB__COLOR_SET__H

#include "terminol/xcb/basics.hxx"
#include "terminol/common/config.hxx"

#include <stdint.h>

struct XColor {
    XColor() : r(0.0), g(0.0), b(0.0) {}
    XColor(double r_, double g_, double b_) : r(r_), g(g_), b(b_) {}

    double r, g, b;
};

//
//
//

class ColorSet {
    const Config & _config;
    Basics       & _basics;
    XColor         _cursorFgColor;
    XColor         _cursorBgColor;
    XColor         _borderColor;
    XColor         _scrollBarFgColor;
    XColor         _scrollBarBgColor;
    XColor         _indexedColors[256];
    XColor         _foregroundColor;
    XColor         _backgroundColor;
    uint32_t       _backgroundPixel;
    XColor         _cursorFillColor;
    XColor         _cursorTextColor;

public:
    ColorSet(const Config & config, Basics & basics);
    ~ColorSet();

    const XColor & getCursorFillColor()  const { return _cursorFillColor; }
    const XColor & getCursorTextColor()  const { return _cursorTextColor; }
    const XColor & getBorderColor()      const { return _borderColor; }
    const XColor & getScrollBarFgColor() const { return _scrollBarFgColor; }
    const XColor & getScrollBarBgColor() const { return _scrollBarBgColor; }
    const XColor & getIndexedColor(uint8_t index) const { return _indexedColors[index]; }
    const XColor & getForegroundColor()  const { return _foregroundColor; }
    const XColor & getBackgroundColor()  const { return _backgroundColor; }
    uint32_t       getBackgroundPixel()  const { return _backgroundPixel; }

    static XColor convert(const Color & color);
};

#endif // XCB__COLOR_SET__H

// vi:noai:sw=4

#include "terminol/x_key_map.hxx"
#include "terminol/common.hxx"

#include <cstring>

#include <X11/Xutil.h>

namespace {

const uint8_t NoMask  = 0;
const uint8_t AnyMask = ~NoMask;

bool match(uint8_t mask, uint8_t state) {
    if (mask == NoMask) {
        return state == 0;
    }
    else if (mask == AnyMask) {
        return true;
    }
    else {
        return (state & mask) == mask;
    }
}

enum {
    OFF,
    ON,
    EITHER
};

struct Key {
    KeySym       keySym;
    uint8_t      mask;
    const char * str;
    // three valued logic variables: 0 indifferent, 1 on, -1 off
    int8_t       appKey;     // application keypad
    int8_t       appCursor;  // application cursor
    int8_t       crlf;       // crlf mode
};

const Key keys[] = {                           //    appKey  appCursor
    // keySym              mask          string      keypad   cursor   crlf
    { XK_KP_Home,       ShiftMask,     "\033[1;2H",     0,      0,      0 },
    { XK_KP_Home,       AnyMask,       "\033[H",        0,     -1,      0 },
    { XK_KP_Home,       AnyMask,       "\033[1~",       0,     +1,      0 },
    { XK_KP_Up,         AnyMask,       "\033Ox",       +1,      0,      0 },
    { XK_KP_Up,         AnyMask,       "\033[A",        0,     -1,      0 },
    { XK_KP_Up,         AnyMask,       "\033OA",        0,     +1,      0 },
    { XK_KP_Down,       AnyMask,       "\033Or",       +1,      0,      0 },
    { XK_KP_Down,       AnyMask,       "\033[B",        0,     -1,      0 },
    { XK_KP_Down,       AnyMask,       "\033OB",        0,     +1,      0 },
    { XK_KP_Left,       AnyMask,       "\033Ot",       +1,      0,      0 },
    { XK_KP_Left,       AnyMask,       "\033[D",        0,     -1,      0 },
    { XK_KP_Left,       AnyMask,       "\033OD",        0,     +1,      0 },
    { XK_KP_Right,      AnyMask,       "\033Ov",       +1,      0,      0 },
    { XK_KP_Right,      AnyMask,       "\033[C",        0,     -1,      0 },
    { XK_KP_Right,      AnyMask,       "\033OC",        0,     +1,      0 },
    { XK_KP_Prior,      ShiftMask,     "\033[5;2~",     0,      0,      0 },
    { XK_KP_Prior,      AnyMask,       "\033[5~",       0,      0,      0 },
    { XK_KP_Begin,      AnyMask,       "\033[E",        0,      0,      0 },
    { XK_KP_End,        ControlMask,   "\033[J",       -1,      0,      0 },
    { XK_KP_End,        ControlMask,   "\033[1;5F",    +1,      0,      0 },
    { XK_KP_End,        ShiftMask,     "\033[K",       -1,      0,      0 },
    { XK_KP_End,        ShiftMask,     "\033[1;2F",    +1,      0,      0 },
    { XK_KP_End,        AnyMask,       "\033[4~",       0,      0,      0 },
    { XK_KP_Next,       ShiftMask,     "\033[6;2~",     0,      0,      0 },
    { XK_KP_Next,       AnyMask,       "\033[6~",       0,      0,      0 },
    { XK_KP_Insert,     ShiftMask,     "\033[2;2~",    +1,      0,      0 },
    { XK_KP_Insert,     ShiftMask,     "\033[4l",      -1,      0,      0 },
    { XK_KP_Insert,     ControlMask,   "\033[L",       -1,      0,      0 },
    { XK_KP_Insert,     ControlMask,   "\033[2;5~",    +1,      0,      0 },
    { XK_KP_Insert,     AnyMask,       "\033[4h",      -1,      0,      0 },
    { XK_KP_Insert,     AnyMask,       "\033[2~",      +1,      0,      0 },
    { XK_KP_Delete,     ControlMask,   "\033[2J",      -1,      0,      0 },
    { XK_KP_Delete,     ControlMask,   "\033[3;5~",    +1,      0,      0 },
    { XK_KP_Delete,     ShiftMask,     "\033[2K",      +1,      0,      0 },
    { XK_KP_Delete,     ShiftMask,     "\033[3;2~",    -1,      0,      0 },
    { XK_KP_Delete,     AnyMask,       "\033[P",       -1,      0,      0 },
    { XK_KP_Delete,     AnyMask,       "\033[3~",      +1,      0,      0 },
    { XK_KP_Multiply,   AnyMask,       "\033Oj",       +2,      0,      0 },
    { XK_KP_Add,        AnyMask,       "\033Ok",       +2,      0,      0 },
    { XK_KP_Enter,      AnyMask,       "\033OM",       +2,      0,      0 },
    { XK_KP_Enter,      AnyMask,       "\r",           -1,      0,     -1 },
    { XK_KP_Enter,      AnyMask,       "\r\n",         -1,      0,     +1 },
    { XK_KP_Subtract,   AnyMask,       "\033Om",       +2,      0,      0 },
    { XK_KP_Decimal,    AnyMask,       "\033On",       +2,      0,      0 },
    { XK_KP_Divide,     AnyMask,       "\033Oo",       +2,      0,      0 },
    { XK_KP_0,          AnyMask,       "\033Op",       +2,      0,      0 },
    { XK_KP_1,          AnyMask,       "\033Oq",       +2,      0,      0 },
    { XK_KP_2,          AnyMask,       "\033Or",       +2,      0,      0 },
    { XK_KP_3,          AnyMask,       "\033Os",       +2,      0,      0 },
    { XK_KP_4,          AnyMask,       "\033Ot",       +2,      0,      0 },
    { XK_KP_5,          AnyMask,       "\033Ou",       +2,      0,      0 },
    { XK_KP_6,          AnyMask,       "\033Ov",       +2,      0,      0 },
    { XK_KP_7,          AnyMask,       "\033Ow",       +2,      0,      0 },
    { XK_KP_8,          AnyMask,       "\033Ox",       +2,      0,      0 },
    { XK_KP_9,          AnyMask,       "\033Oy",       +2,      0,      0 },
    { XK_BackSpace,     NoMask,        "\177",          0,      0,      0 },
    { XK_Up,            ShiftMask,     "\033[1;2A",     0,      0,      0 },
    { XK_Up,            ControlMask,   "\033[1;5A",     0,      0,      0 },
    { XK_Up,            Mod1Mask,      "\033[1;3A",     0,      0,      0 },
    { XK_Up,            AnyMask,       "\033[A",        0,     -1,      0 },
    { XK_Up,            AnyMask,       "\033OA",        0,     +1,      0 },
    { XK_Down,          ShiftMask,     "\033[1;2B",     0,      0,      0 },
    { XK_Down,          ControlMask,   "\033[1;5B",     0,      0,      0 },
    { XK_Down,          Mod1Mask,      "\033[1;3B",     0,      0,      0 },
    { XK_Down,          AnyMask,       "\033[B",        0,     -1,      0 },
    { XK_Down,          AnyMask,       "\033OB",        0,     +1,      0 },
    { XK_Left,          ShiftMask,     "\033[1;2D",     0,      0,      0 },
    { XK_Left,          ControlMask,   "\033[1;5D",     0,      0,      0 },
    { XK_Left,          Mod1Mask,      "\033[1;3D",     0,      0,      0 },
    { XK_Left,          AnyMask,       "\033[D",        0,     -1,      0 },
    { XK_Left,          AnyMask,       "\033OD",        0,     +1,      0 },
    { XK_Right,         ShiftMask,     "\033[1;2C",     0,      0,      0 },
    { XK_Right,         ControlMask,   "\033[1;5C",     0,      0,      0 },
    { XK_Right,         Mod1Mask,      "\033[1;3C",     0,      0,      0 },
    { XK_Right,         AnyMask,       "\033[C",        0,     -1,      0 },
    { XK_Right,         AnyMask,       "\033OC",        0,     +1,      0 },
    { XK_ISO_Left_Tab,  ShiftMask,     "\033[Z",        0,      0,      0 },
    { XK_Return,        Mod1Mask,      "\033\r",        0,      0,     -1 },
    { XK_Return,        Mod1Mask,      "\033\r\n",      0,      0,     +1 },
    { XK_Return,        AnyMask,       "\r",            0,      0,     -1 },
    { XK_Return,        AnyMask,       "\r\n",          0,      0,     +1 },
    { XK_Insert,        ShiftMask,     "\033[4l",      -1,      0,      0 },
    { XK_Insert,        ShiftMask,     "\033[2;2~",    +1,      0,      0 },
    { XK_Insert,        ControlMask,   "\033[L",       -1,      0,      0 },
    { XK_Insert,        ControlMask,   "\033[2;5~",    +1,      0,      0 },
    { XK_Insert,        AnyMask,       "\033[4h",      -1,      0,      0 },
    { XK_Insert,        AnyMask,       "\033[2~",      +1,      0,      0 },
    { XK_Delete,        ControlMask,   "\033[2J",      -1,      0,      0 },
    { XK_Delete,        ControlMask,   "\033[3;5~",    +1,      0,      0 },
    { XK_Delete,        ShiftMask,     "\033[2K",      +1,      0,      0 },
    { XK_Delete,        ShiftMask,     "\033[3;2~",    -1,      0,      0 },
    { XK_Delete,        AnyMask,       "\033[P",       -1,      0,      0 },
    { XK_Delete,        AnyMask,       "\033[3~",      +1,      0,      0 },
    { XK_Home,          ShiftMask,     "\033[1;2H",     0,      0,      0 },
    { XK_Home,          AnyMask,       "\033[H",        0,     -1,      0 },
    { XK_Home,          AnyMask,       "\033[1~",       0,     +1,      0 },
    { XK_End,           ControlMask,   "\033[J",       -1,      0,      0 },
    { XK_End,           ControlMask,   "\033[1;5F",    +1,      0,      0 },
    { XK_End,           ShiftMask,     "\033[K",       -1,      0,      0 },
    { XK_End,           ShiftMask,     "\033[1;2F",    +1,      0,      0 },
    { XK_End,           AnyMask,       "\033[4~",       0,      0,      0 },
    { XK_Prior,         ControlMask,   "\033[5;5~",     0,      0,      0 },
    { XK_Prior,         ShiftMask,     "\033[5;2~",     0,      0,      0 },
    { XK_Prior,         NoMask,        "\033[5~",       0,      0,      0 },
    { XK_Next,          ControlMask,   "\033[6;5~",     0,      0,      0 },
    { XK_Next,          ShiftMask,     "\033[6;2~",     0,      0,      0 },
    { XK_Next,          AnyMask,       "\033[6~",       0,      0,      0 },
    { XK_F1,            NoMask,        "\033OP" ,       0,      0,      0 },
    { XK_F1,  /* F13 */ ShiftMask,     "\033[1;2P",     0,      0,      0 },
    { XK_F1,  /* F25 */ ControlMask,   "\033[1;5P",     0,      0,      0 },
    { XK_F1,  /* F37 */ Mod4Mask,      "\033[1;6P",     0,      0,      0 },
    { XK_F1,  /* F49 */ Mod1Mask,      "\033[1;3P",     0,      0,      0 },
    { XK_F1,  /* F61 */ Mod3Mask,      "\033[1;4P",     0,      0,      0 },
    { XK_F2,            NoMask,        "\033OQ" ,       0,      0,      0 },
    { XK_F2,  /* F14 */ ShiftMask,     "\033[1;2Q",     0,      0,      0 },
    { XK_F2,  /* F26 */ ControlMask,   "\033[1;5Q",     0,      0,      0 },
    { XK_F2,  /* F38 */ Mod4Mask,      "\033[1;6Q",     0,      0,      0 },
    { XK_F2,  /* F50 */ Mod1Mask,      "\033[1;3Q",     0,      0,      0 },
    { XK_F2,  /* F62 */ Mod3Mask,      "\033[1;4Q",     0,      0,      0 },
    { XK_F3,            NoMask,        "\033OR" ,       0,      0,      0 },
    { XK_F3,  /* F15 */ ShiftMask,     "\033[1;2R",     0,      0,      0 },
    { XK_F3,  /* F27 */ ControlMask,   "\033[1;5R",     0,      0,      0 },
    { XK_F3,  /* F39 */ Mod4Mask,      "\033[1;6R",     0,      0,      0 },
    { XK_F3,  /* F51 */ Mod1Mask,      "\033[1;3R",     0,      0,      0 },
    { XK_F3,  /* F63 */ Mod3Mask,      "\033[1;4R",     0,      0,      0 },
    { XK_F4,            NoMask,        "\033OS" ,       0,      0,      0 },
    { XK_F4,  /* F16 */ ShiftMask,     "\033[1;2S",     0,      0,      0 },
    { XK_F4,  /* F28 */ ShiftMask,     "\033[1;5S",     0,      0,      0 },
    { XK_F4,  /* F40 */ Mod4Mask,      "\033[1;6S",     0,      0,      0 },
    { XK_F4,  /* F52 */ Mod1Mask,      "\033[1;3S",     0,      0,      0 },
    { XK_F5,            NoMask,        "\033[15~",      0,      0,      0 },
    { XK_F5,  /* F17 */ ShiftMask,     "\033[15;2~",    0,      0,      0 },
    { XK_F5,  /* F29 */ ControlMask,   "\033[15;5~",    0,      0,      0 },
    { XK_F5,  /* F41 */ Mod4Mask,      "\033[15;6~",    0,      0,      0 },
    { XK_F5,  /* F53 */ Mod1Mask,      "\033[15;3~",    0,      0,      0 },
    { XK_F6,            NoMask,        "\033[17~",      0,      0,      0 },
    { XK_F6,  /* F18 */ ShiftMask,     "\033[17;2~",    0,      0,      0 },
    { XK_F6,  /* F30 */ ControlMask,   "\033[17;5~",    0,      0,      0 },
    { XK_F6,  /* F42 */ Mod4Mask,      "\033[17;6~",    0,      0,      0 },
    { XK_F6,  /* F54 */ Mod1Mask,      "\033[17;3~",    0,      0,      0 },
    { XK_F7,            NoMask,        "\033[18~",      0,      0,      0 },
    { XK_F7,  /* F19 */ ShiftMask,     "\033[18;2~",    0,      0,      0 },
    { XK_F7,  /* F31 */ ControlMask,   "\033[18;5~",    0,      0,      0 },
    { XK_F7,  /* F43 */ Mod4Mask,      "\033[18;6~",    0,      0,      0 },
    { XK_F7,  /* F55 */ Mod1Mask,      "\033[18;3~",    0,      0,      0 },
    { XK_F8,            NoMask,        "\033[19~",      0,      0,      0 },
    { XK_F8,  /* F20 */ ShiftMask,     "\033[19;2~",    0,      0,      0 },
    { XK_F8,  /* F32 */ ControlMask,   "\033[19;5~",    0,      0,      0 },
    { XK_F8,  /* F44 */ Mod4Mask,      "\033[19;6~",    0,      0,      0 },
    { XK_F8,  /* F56 */ Mod1Mask,      "\033[19;3~",    0,      0,      0 },
    { XK_F9,            NoMask,        "\033[20~",      0,      0,      0 },
    { XK_F9,  /* F21 */ ShiftMask,     "\033[20;2~",    0,      0,      0 },
    { XK_F9,  /* F33 */ ControlMask,   "\033[20;5~",    0,      0,      0 },
    { XK_F9,  /* F45 */ Mod4Mask,      "\033[20;6~",    0,      0,      0 },
    { XK_F9,  /* F57 */ Mod1Mask,      "\033[20;3~",    0,      0,      0 },
    { XK_F10,           NoMask,        "\033[21~",      0,      0,      0 },
    { XK_F10, /* F22 */ ShiftMask,     "\033[21;2~",    0,      0,      0 },
    { XK_F10, /* F34 */ ControlMask,   "\033[21;5~",    0,      0,      0 },
    { XK_F10, /* F46 */ Mod4Mask,      "\033[21;6~",    0,      0,      0 },
    { XK_F10, /* F58 */ Mod1Mask,      "\033[21;3~",    0,      0,      0 },
    { XK_F11,           NoMask,        "\033[23~",      0,      0,      0 },
    { XK_F11, /* F23 */ ShiftMask,     "\033[23;2~",    0,      0,      0 },
    { XK_F11, /* F35 */ ControlMask,   "\033[23;5~",    0,      0,      0 },
    { XK_F11, /* F47 */ Mod4Mask,      "\033[23;6~",    0,      0,      0 },
    { XK_F11, /* F59 */ Mod1Mask,      "\033[23;3~",    0,      0,      0 },
    { XK_F12,           NoMask,        "\033[24~",      0,      0,      0 },
    { XK_F12, /* F24 */ ShiftMask,     "\033[24;2~",    0,      0,      0 },
    { XK_F12, /* F36 */ ControlMask,   "\033[24;5~",    0,      0,      0 },
    { XK_F12, /* F48 */ Mod4Mask,      "\033[24;6~",    0,      0,      0 },
    { XK_F12, /* F60 */ Mod1Mask,      "\033[24;3~",    0,      0,      0 },
    { XK_F13,           NoMask,        "\033[1;2P",     0,      0,      0 },
    { XK_F14,           NoMask,        "\033[1;2Q",     0,      0,      0 },
    { XK_F15,           NoMask,        "\033[1;2R",     0,      0,      0 },
    { XK_F16,           NoMask,        "\033[1;2S",     0,      0,      0 },
    { XK_F17,           NoMask,        "\033[15;2~",    0,      0,      0 },
    { XK_F18,           NoMask,        "\033[17;2~",    0,      0,      0 },
    { XK_F19,           NoMask,        "\033[18;2~",    0,      0,      0 },
    { XK_F20,           NoMask,        "\033[19;2~",    0,      0,      0 },
    { XK_F21,           NoMask,        "\033[20;2~",    0,      0,      0 },
    { XK_F22,           NoMask,        "\033[21;2~",    0,      0,      0 },
    { XK_F23,           NoMask,        "\033[23;2~",    0,      0,      0 },
    { XK_F24,           NoMask,        "\033[24;2~",    0,      0,      0 },
    { XK_F25,           NoMask,        "\033[1;5P",     0,      0,      0 },
    { XK_F26,           NoMask,        "\033[1;5Q",     0,      0,      0 },
    { XK_F27,           NoMask,        "\033[1;5R",     0,      0,      0 },
    { XK_F28,           NoMask,        "\033[1;5S",     0,      0,      0 },
    { XK_F29,           NoMask,        "\033[15;5~",    0,      0,      0 },
    { XK_F30,           NoMask,        "\033[17;5~",    0,      0,      0 },
    { XK_F31,           NoMask,        "\033[18;5~",    0,      0,      0 },
    { XK_F32,           NoMask,        "\033[19;5~",    0,      0,      0 },
    { XK_F33,           NoMask,        "\033[20;5~",    0,      0,      0 },
    { XK_F34,           NoMask,        "\033[21;5~",    0,      0,      0 },
    { XK_F35,           NoMask,        "\033[23;5~",    0,      0,      0 }
};

const size_t numKeys = sizeof(keys) / sizeof(keys[0]);

} // namespace {anonymous}

X_KeyMap::X_KeyMap() {}

X_KeyMap::~X_KeyMap() {}

bool X_KeyMap::lookup(KeySym keySym, uint8_t state,
                      bool appKey, bool appCursor, bool crlf, bool numLock,
                      std::string & str) const {
    std::cerr
        << "keySym=" << keySym << ", state=" << int(state) << ", appKey=" << appKey
        << ", appCursor=" << appCursor << ", crlf=" << crlf << ", numLock=" << numLock
        << ", str=" << str
        << std::endl;

    for (size_t i = 0; i != numKeys; ++i) {
        const Key & key = keys[i];

        if (keySym != key.keySym) continue;
        if (!match(key.mask, state)) continue;

        if (key.appKey < 0 && appKey) continue;
        if (key.appKey > 0 && !appKey) continue;
        if (key.appKey == 2 && numLock) continue;

        if (key.appCursor < 0 &&  appCursor) continue;
        if (key.appCursor > 0 && !appCursor) continue;

        if (key.crlf < 0 &&  crlf) continue;
        if (key.crlf > 0 && !crlf) continue;

        PRINT("Replacing: '" << str << "' with: '" << key.str << "'");
        str = key.str;
        return true;
    }

    return false;
}

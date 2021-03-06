// vi:noai:sw=4

#ifndef XCB__KEY_MAP__HXX
#define XCB__KEY_MAP__HXX

#include "terminol/support/pattern.hxx"
#include "terminol/support/conv.hxx"
#include "terminol/common/bit_sets.hxx"

#include <vector>

#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

namespace xkb {

std::string symToName(xkb_keysym_t keySym);

xkb_keysym_t nameToSym(const std::string & name) throw (ParseError);

std::string modifierToName(Modifier modifier);

Modifier nameToModifier(const std::string & name) throw (ParseError);

bool isPotent(xkb_keysym_t keySym);

bool composeInput(xkb_keysym_t keySym,
                  ModifierSet modifiers,
                  bool appKeypad,
                  bool appCursor,
                  bool crOnLf,
                  bool deleteSendsDel,
                  bool altSendsEsc,
                  std::vector<uint8_t> & input);

} // namespace xkb

#endif // XCB__KEY_MAP__HXX

// vi:noai:sw=4

#include "terminol/utf8.hxx"
#include "terminol/common.hxx"

const uint8_t B0 = 1 << 0;
const uint8_t B1 = 1 << 1;
const uint8_t B2 = 1 << 2;
const uint8_t B3 = 1 << 3;
const uint8_t B4 = 1 << 4;
const uint8_t B5 = 1 << 5;
const uint8_t B6 = 1 << 6;
const uint8_t B7 = 1 << 7;

using namespace utf8;

char nibbleToHex(uint8_t nibble) {
    ASSERT(nibble < 0x10,);
    if (nibble < 0xA) return '0' +  nibble;
    else              return 'A' + (nibble - 10);
}

std::ostream & showBits(std::ostream & ost, uint8_t byte) {
    for (int i = 0; i != 8; ++i) {
        ost << (((byte >> (7 - i)) & 0x01) ? '1' : '0');
    }
    return ost;
}

std::ostream & showCodePointBytes(std::ostream & ost, CodePoint cp) {
    ost << "U+";

    size_t s = sizeof cp;
    bool active = false;

    for (size_t i = 0; i != s; ++i) {
        uint8_t byte = static_cast<uint8_t>(cp >> (8 * (3 - i)));
        active = active || (byte != 0) || i == s - 1;
        if (active) {
            ost << nibbleToHex(byte >> s) << nibbleToHex(byte & 0x0F);
        }
    }

    return ost;
}

std::ostream & showCodePointBits(std::ostream & ost, CodePoint cp) {
    size_t s     = sizeof cp;
    bool  active = false;
    bool  space  = false;

    for (size_t i = 0; i != s; ++i) {
        if (space) { ost << " "; }
        uint8_t byte = static_cast<uint8_t>(cp >> (8 * (3 - i)));
        active = active || (byte != 0) || i == s - 1;
        if (active) {
            showBits(ost, byte);
        }
        space = active;
    }

    return ost;
}

std::ostream & showSeqBytes(std::ostream & ost, const char * seq) {
    Length l = leadLength(seq[0]);
    bool space = false;

    for (size_t i = 0; i != l; ++i) {
        if (space) { ost << " "; }
        unsigned char byte = seq[i];
        ost << nibbleToHex(byte >> 4) << nibbleToHex(byte & 0x0F);
        space = true;
    }

    return ost;
}

std::ostream & showSeqBits(std::ostream & ost, const char * seq) {
    Length l = leadLength(seq[0]);
    bool space = false;

    for (size_t i = 0; i != l; ++i) {
        if (space) { ost << " "; }
        unsigned char byte = seq[i];
        showBits(ost, byte);
        space = true;
    }

    return ost;
}

void forwardReverse(CodePoint cp) {
    std::cout << "Original code point: ";
    showCodePointBytes(std::cout, cp) << std::endl;
    showCodePointBits(std::cout, cp) << std::endl;

    char seq[LMAX];
    encode(cp, seq);

    std::cout << "Converted to sequence: ";
    showSeqBytes(std::cout, seq) << std::endl;
    showSeqBits(std::cout, seq) << std::endl;
    std::cout
        << "Sequence is: '" << std::string(seq, seq + leadLength(seq[0])) << "'"
        << std::endl;

    CodePoint cp2 = decode(seq);

    std::cout << "Back to code point: ";
    showCodePointBytes(std::cout, cp2) << std::endl;
    showCodePointBits(std::cout, cp2) << std::endl << std::endl;

    ENFORCE(cp == cp2, << cp << " = " << cp2);
}

int main() {
    ENFORCE(leadLength(B1) == L1,);
    ENFORCE(leadLength(B1 | B2) == L1,);
    ENFORCE(leadLength(~B7) == L1,);
    ENFORCE(leadLength('a') == L1,);
    ENFORCE(leadLength('z') == L1,);
    ENFORCE(leadLength('\x7F') == L1,);

    ENFORCE(leadLength(B7 | B6) == L2,);
    ENFORCE(leadLength(B7 | B6 | B5) == L3,);
    ENFORCE(leadLength(B7 | B6 | B5 | B4) == L4,);

    forwardReverse(0x50);
    forwardReverse(0x7F);
    forwardReverse(0x80);

    forwardReverse(0x250);
    forwardReverse(0x8250);
    forwardReverse(0x38250);

    return 0;
}

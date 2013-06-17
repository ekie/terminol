// vi:noai:sw=4

#ifndef SUPPORT__CONV__HXX
#define SUPPORT__CONV__HXX

#include "terminol/support/debug.hxx"

#include <string>
#include <sstream>

struct ParseError {
    explicit ParseError(const std::string & message_) : message(message_) {}
    std::string message;
};

template <typename T> T clamp(T t, T min, T max) {
    ASSERT(min <= max, "");
    if      (t < min) { return min; }
    else if (t > max) { return max; }
    else              { return t;   }
}

template <typename T> std::string stringify(const T & t) {
    std::ostringstream ost;
    ost << t;
    return ost.str();
}

template <typename T> T unstringify(const std::string & str) throw (ParseError) {
    std::istringstream ist(str + '\n');
    T t;
    ist >> t;
    if (ist.good()) { return t; }
    else { throw ParseError("Failed to unstringify: " + str); }
}

template <> inline bool unstringify<>(const std::string & str) throw (ParseError) {
    if (str == "0" || str == "false" || str == "False") {
        return false;
    }
    else if (str == "1" || str == "true" || str == "True") {
        return true;
    }
    else { throw ParseError("Failed to unstringify: " + str); }
}

template <typename T> std::string nthStr(T t) {
    std::ostringstream ost;
    ost << t;
    switch (t) {
        case 0:
            ost << "th";
            break;
        case 1:
            ost << "st";
            break;
        case 2:
            ost << "nd";
            break;
        case 3:
            ost << "rd";
            break;
        default:
            ost << "th";
            break;
    }
    return ost.str();
}

inline char nibbleToHex(uint8_t nibble) {
    ASSERT(nibble < 0x10, "");
    if (nibble < 0xA) { return '0' +  nibble;       }
    else              { return 'A' + (nibble - 10); }
}

inline uint8_t hexToNibble(char hex) throw (ParseError) {
    if      (hex >= '0' && hex <= '9') { return hex - '0'; }
    else if (hex >= 'A' && hex <= 'F') { return 10 + hex - 'A'; }
    else if (hex >= 'a' && hex <= 'f') { return 10 + hex - 'a'; }
    else {
        std::ostringstream ost;
        ost << "Illegal hex char: " << hex;
        throw ParseError(ost.str());
    }
}

inline void byteToHex(uint8_t byte, char & hex0, char & hex1) {
    hex0 = nibbleToHex((byte >> 4) & 0x0F);
    hex1 = nibbleToHex(byte & 0x0F);
}

inline uint8_t hexToByte(char hex0, char hex1) throw (ParseError) {
    return (hexToNibble(hex0) << 4) + hexToNibble(hex1);
}

template <typename T> std::string toHexString(T t) {
    std::string str;
    for (size_t i = 0; i < sizeof(T); ++i) {
        str.push_back(nibbleToHex(t >> (8 * (sizeof(T) - i - 1) + 4) & 0x0F));
        str.push_back(nibbleToHex(t >> (8 * (sizeof(T) - i - 1))     & 0x0F));
    }
    return str;
}

template <typename T> std::string toBinaryString(T t) {
    std::string str;
    for (size_t i = 0; i < 8 * sizeof(T); ++i) {
        str.push_back(((t >> (8 * sizeof(T) - i - 1)) & 0x1) ? '1' : '0');
    }
    return str;
}

inline bool XOR(bool a, bool b) {
    return a != b;
}

#endif // SUPPORT__CONV__HXX

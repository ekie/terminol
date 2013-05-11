// vi:noai:sw=4

#include "terminol/common/vt_state_machine.hxx"
#include "terminol/common/ascii.hxx"

VtStateMachine::VtStateMachine(I_Observer & observer) :
    _observer(observer),
    _state(State::NORMAL),
    _outerState(State::NORMAL),
    _escSeq() {}

void VtStateMachine::consume(utf8::Seq seq, utf8::Length len) {
    uint8_t lead = seq.bytes[0];

    switch (_state) {
        case State::NORMAL:
            if (lead == ESC) {
                ASSERT(len == utf8::Length::L1, "");
                _state = State::ESCAPE;
                _outerState = State::NORMAL;
                ASSERT(_escSeq.empty(), "");
            }
            else if (lead < SPACE) {
                ASSERT(len == utf8::Length::L1, "");
                _observer.machineControl(lead);
            }
            else {
                _observer.machineNormal(seq, len);
            }
            break;
        case State::ESCAPE:
            ASSERT(_escSeq.empty(), "");
            std::copy(seq.bytes, seq.bytes + len, std::back_inserter(_escSeq));
            switch (lead) {
                case 'P':
                    _state = State::DCS;
                    break;
                case '[':
                    _state = State::CSI;
                    break;
                case ']':
                    _state = State::OSC;
                    break;
                case '#':   // Test?
                case '(':   // Set primary charset G0
                case ')':   // Set primary charset G1
                case '*':   // Set secondary charset G2
                case '+':   // Set secondary charset G3
                    _state = State::SPECIAL;
                    break;
                case 'X':   // SOS
                case '^':   // PM
                case '_':   // APC
                    _state = State::IGNORE;
                    break;
                default:
                    _observer.machineEscape(lead);
                    _escSeq.clear();
                    _state = State::NORMAL;
                    break;
            }
            break;
        case State::CSI:
            ASSERT(!_escSeq.empty(), "");
            if (lead < SPACE) {
                ASSERT(len == utf8::Length::L1, "");
                _observer.machineControl(lead);
            }
            else if (lead == '?') {
                // XXX For now put the '?' into _escSeq because
                // processCsi is expecting it.
                std::copy(seq.bytes, seq.bytes + len, std::back_inserter(_escSeq));
                //terminal->escape_flags |= ESC_FLAG_WHAT;
            }
            else if (lead == '>') {
                //terminal->escape_flags |= ESC_FLAG_GT;
            }
            else if (lead == '!') {
                //terminal->escape_flags |= ESC_FLAG_BANG;
            }
            else if (lead == '$') {
                //terminal->escape_flags |= ESC_FLAG_CASH;
            }
            else if (lead == '\'') {
                //terminal->escape_flags |= ESC_FLAG_SQUOTE;
            }
            else if (lead == '"') {
                //terminal->escape_flags |= ESC_FLAG_DQUOTE;
            }
            else if (lead == ' ') {
                //terminal->escape_flags |= ESC_FLAG_SPACE;
            }
            else {
                std::copy(seq.bytes, seq.bytes + len, std::back_inserter(_escSeq));
            }

            if (isalpha(lead) || lead == '@' || lead == '`') {
                processCsi(_escSeq);
                _escSeq.clear();
                _state = State::NORMAL;
            }
            break;
        case State::INNER:
            // XXX check the logic for INNER
            if (lead == '\\') {
                if (_outerState == State::DCS) {
                    _observer.machineDcs(_escSeq);
                }
                else if (_outerState == State::OSC) {
                    processOsc(_escSeq);
                }
                else {
                    // ??
                }
                _escSeq.clear();
                _state = State::NORMAL;
                // XXX reset _outerState?
            }
            else if (lead == ESC) {
                _state = _outerState;
                // XXX reset _outerState?
                std::copy(seq.bytes, seq.bytes + len, std::back_inserter(_escSeq));
            }
            else {
                _state = _outerState;
                // XXX reset _outerState?
                _escSeq.push_back(ESC);
                std::copy(seq.bytes, seq.bytes + len, std::back_inserter(_escSeq));
            }
            break;
        case State::DCS:
        case State::OSC:
        case State::IGNORE:
            ASSERT(!_escSeq.empty(), "");
            if (lead == ESC) {
                _outerState = _state;
                _state = State::INNER;
            }
            else if (lead == BEL && _state == State::OSC) {
                processOsc(_escSeq);
                _escSeq.clear();
                _state = State::NORMAL;
            }
            else {
                std::copy(seq.bytes, seq.bytes + len, std::back_inserter(_escSeq));
            }
            break;
        case State::SPECIAL:
            ASSERT(!_escSeq.empty(), "");
            std::copy(seq.bytes, seq.bytes + len, std::back_inserter(_escSeq));
            if (isdigit(lead) || isalpha(lead)) {
                processSpecial(_escSeq);
            }
            _escSeq.clear();
            _state = State::NORMAL;
            break;
    }
}

void VtStateMachine::processCsi(const std::vector<uint8_t> & seq) {
    ASSERT(seq.size() >= 2, "");

    size_t i = 0;
    bool priv = false;
    std::vector<int32_t> args;

    //
    // Parse the arguments.
    //

    ASSERT(seq[i] == '[', "");
    ++i;

    if (seq[i] == '?') {
        ++i;
        priv = true;
    }

    bool inArg = false;

    while (i != seq.size()) {
        uint8_t c = seq[i];

        if (c >= '0' && c <= '9') {
            if (!inArg) {
                args.push_back(0);
                inArg = true;
            }
            args.back() = 10 * args.back() + c - '0';
        }
        else {
            if (inArg) {
                inArg = false;
            }

            if (c != ';') {
                break;
            }
        }

        ++i;
    }

    ASSERT(i == seq.size() - 1, "i=" << i << ", seq.size=" << seq.size() << ", Seq: " << Str(seq));

    _observer.machineCsi(priv, args, seq[i]);
}

void VtStateMachine::processOsc(const std::vector<uint8_t> & seq) {
    ASSERT(!seq.empty(), "");

    size_t i = 0;
    std::vector<std::string> args;

    //
    // Parse the arguments.
    //

    ASSERT(seq[i] == ']', "");
    ++i;

    bool next = true;
    while (i != seq.size()) {
        uint8_t c = seq[i];

        if (next) { args.push_back(std::string()); next = false; }

        if (c == ';') { next = true; }
        else          { args.back().push_back(c); }

        ++i;
    }

    _observer.machineOsc(args);
}

void VtStateMachine::processSpecial(const std::vector<uint8_t> & seq) {
    ASSERT(seq.size() == 2, "");
    uint8_t special = seq.front();
    uint8_t code    = seq.back();

    _observer.machineSpecial(special, code);
}

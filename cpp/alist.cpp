#include "alist.hpp"
#include <vector>
#include <iostream>
#include <deque>
#include <iomanip>
#include <cstring>

using namespace alist;
using namespace std;

ParseException::ParseException(const char * w) : _what(w) { }
const char * ParseException::what() const noexcept { return _what.c_str(); }

struct AListValue {
    bool hasTmp;
    void * tmp;
    void * o;
    bool isString;
    bool isLiteral;
};

template<typename I>
struct CharMap {
    I m[256];

    CharMap(const char * v, I defaultValue, I setValue) {
        for (int i = 0; i < 256; ++i) m[i] = defaultValue;
        while (*v) {
            m[*v] = setValue;
            ++v;
        }
    }

    CharMap(const char * v, I defaultValue, const initializer_list<I> & l) {
        for (int i = 0; i < 256; ++i) m[i] = defaultValue;
        auto it = begin(l);
        while (*v && it != end(l)) {
            m[*v] = *it;
            ++v; ++it;
        }
    }

    void Set(const char * v, I value) {
        while (*v) {
            m[*v] = value;
            ++v;
        }
    }
};

static const CharMap<int> HEX_TRANSLATE("0123456789abcdefABCDEF", -1,
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 10, 11, 12, 13, 14, 15}
);

#define BUF_CLEAN_THRESHOLD 4096

class AListParser : public IParser {
private:

    enum State {
        STATE_ELEMENT_START,
        STATE_ELEMENT_END,
        STATE_ALIST,
        STATE_ALIST_WITH_KEY,
        STATE_QUOTED_STRING,
        STATE_MULTILINE_STRING
    };

    bool            _multi;
    bool            _sealed;
    string          _buf;
    size_t          _readPos;
    vector<AListValue> _valueStack;
    vector<int>     _auxStack;
    vector<State>   _stateStack;
    deque<void *>   _results;
    IOperator *     _op;
    const char *    _c_whitespace;
    const char *    _c_line_comment;
    const char *    _c_item_sep;
    const char *    _c_kv_sep;
    const char *    _c_quote;
    const char *    _c_open;
    const char *    _c_close;
    CharMap<bool>   _c_special;

public:

    AListParser(bool multi, IOperator * op,
                const char * c_whitespace, const char * c_line_comment,
                const char * c_item_sep, const char * c_kv_sep,
                const char * c_quote, const char * c_open, const char * c_close)
        : _c_special("", false, true)
        {
        _multi = multi;
        _sealed = false;
        _readPos = 0;
        _stateStack.push_back(STATE_ELEMENT_START);
        _op = op;
        _c_whitespace = c_whitespace;
        _c_line_comment = c_line_comment;
        _c_item_sep = c_item_sep;
        _c_kv_sep = c_kv_sep;
        _c_quote = c_quote;
        _c_open = c_open;
        _c_close = c_close;
        _c_special.Set(c_whitespace, true);
        _c_special.Set(c_line_comment, true);
        _c_special.Set(c_item_sep, true);
        _c_special.Set(c_kv_sep, true);
        _c_special.Set(c_quote, true);
        _c_special.Set(c_open, true);
        _c_special.Set(c_close, true);
    }

    void ParseLine(const string & line) override {
        if (_sealed) return;

        _buf.append(line);
        ParseBuf();
    }

    void Seal() override {
        if (_sealed) return;

        _sealed = true;
        ParseBuf();

        _readPos = 0;
        _buf.clear();
        _stateStack.clear();
        for (auto && d : _valueStack) {
            if (d.hasTmp) {
                _op->Free(d.tmp);
            }
            _op->Free(d.o);
        }
        _valueStack.clear();
    }

    ~AListParser() {
        Seal();
        for (auto d : _results) {
            _op->Free(d);
        }
    }

    void HandleEscape() {
        if (_readPos >= _buf.size()) return;
        auto && v = _valueStack.back();
        switch (_buf[_readPos]) {
        case 'n':
            v.o = _op->StringAppendByte(v.o, '\n');
            ++_readPos;
            break;
        case 't':
            v.o = _op->StringAppendByte(v.o, '\t');
            ++_readPos;
            break;
        case 'r':
            v.o = _op->StringAppendByte(v.o, '\r');
            ++_readPos;
            break;
        case 'f':
            v.o = _op->StringAppendByte(v.o, '\f');
            ++_readPos;
            break;
        case 'x':
        {
            unsigned char c;
            int digit;

            if (_readPos + 2 >= _buf.size()) goto InputError;
            ++_readPos;
            digit = HEX_TRANSLATE.m[_buf[_readPos]]; if (digit < 0) goto InputError;
            c = digit; ++_readPos;
            digit = HEX_TRANSLATE.m[_buf[_readPos]]; if (digit < 0) goto InputError;
            c = (c << 4) + digit; ++_readPos;
            v.o = _op->StringAppendByte(v.o, c);

            break;

        InputError:
            throw ParseException("Expect 2 hex chars for utf-8 escape");
        }
        default:
            v.o = _op->StringAppendByte(v.o, _buf[_readPos]);
            ++_readPos;
            break;
        }
    }

    void CleanBuf() {
        if (_readPos > _buf.size()) _readPos = _buf.size();
        if (_readPos > BUF_CLEAN_THRESHOLD) {
            _buf = _buf.substr(_readPos, _buf.size() - _readPos);
            _readPos = 0;
        }
    }

    void ParseBuf() {
        size_t limit = _buf.size();
        while (_readPos < limit || (_stateStack.size() > 0 && _stateStack.back() == STATE_ELEMENT_END)) {
            if (_stateStack.size() == 0) {
                Seal();
                return;
            }

            auto state = _stateStack.back();

            switch (state) {
            case STATE_ELEMENT_END: {
                auto value = _valueStack.back();

                _stateStack.pop_back();
                _auxStack.pop_back();
                _valueStack.pop_back();

                if (_stateStack.size() == 0) {
                    _results.push_back(value.o);

                    if (_multi) {
                        _stateStack.push_back(STATE_ELEMENT_START);
                    }

                    break;
                }

                switch (_stateStack.back()) {
                case STATE_ALIST:
                {
                    auto && c = _valueStack.back();
                    if (c.hasTmp) {
                        c.o = _op->AListAppendItem(c.o, c.tmp);
                    }
                    c.hasTmp = true;
                    c.tmp = value.o;
                    c.isString = value.isString;
                    c.isLiteral = value.isLiteral;
                    break;
                }

                case STATE_ALIST_WITH_KEY:
                {
                    auto && c = _valueStack.back();
                    _op->AListAppendKV(c.o, c.tmp, c.isLiteral, value.o);
                    c.hasTmp = false;
                    c.tmp = nullptr;
                    c.isString = false;
                    c.isLiteral = false;
                    _stateStack.back() = STATE_ALIST;
                    break;
                }

                default:
                    throw ParseException("invalid state to insert element");
                }
                break;
            }
            case STATE_QUOTED_STRING: {
                auto && v = _valueStack.back();
                size_t s = _readPos;
                char delim = _c_quote[_auxStack.back()];
                while (s < limit && _buf[s] != '\'' && _buf[s] != delim) ++s;
                v.o = _op->StringAppendByteArray(
                    v.o, (const unsigned char *)_buf.data() + _readPos, s - _readPos);

                if (s >= limit) {
                    _readPos = limit;
                    v.o = _op->StringFinalize(v.o);
                    _stateStack.back() = STATE_ELEMENT_END;
                }
                else {
                    if (_buf[s] == '\\') {
                        _readPos = s + 1;
                        HandleEscape();
                    }
                    else {
                        _readPos = s + 1;
                        v.o = _op->StringFinalize(v.o);
                        _stateStack.back() = STATE_ELEMENT_END;
                    }
                }
                break;
            }
            case STATE_MULTILINE_STRING: {
                auto && v = _valueStack.back();
                size_t s = _readPos;
                char delim = _c_quote[_auxStack.back()];
                while (s < limit && _buf[s] != '\'' && _buf[s] != delim) ++s;
                v.o = _op->StringAppendByteArray(
                    v.o, (const unsigned char *)_buf.data() + _readPos, s - _readPos);

                if (s >= limit) {
                    v.o = _op->StringAppendByte(v.o, '\n');
                    _readPos = limit;
                }
                else {
                    if (_buf[s] == '\\') {
                        _readPos = s + 1;
                        HandleEscape();
                    }
                    else if (s + 2 < limit && _buf[s] == delim && _buf[s + 1] == delim && _buf[s + 2] == delim) {
                        v.o = _op->StringFinalize(v.o);
                        _readPos = s + 3;
                        _stateStack.back() = STATE_ELEMENT_END;
                    }
                    else {
                        v.o = _op->StringAppendByte(v.o, _buf[s]);
                        _readPos = s + 1;
                    }
                }
                break;
            }
            case STATE_ALIST: {
                auto && v = _valueStack.back();
                size_t s = _readPos;
                while (s < limit && strchr(_c_whitespace, _buf[s])) ++s;

                if (s >= limit) {
                    _readPos = limit;
                }
                else if (_buf[s] == _c_close[_auxStack.back()]) {
                    if (v.hasTmp) {
                        v.o = _op->AListAppendItem(v.o, v.tmp);
                        v.hasTmp = false;
                        v.tmp = nullptr;
                        v.isString = false;
                        v.isLiteral = false;
                    }
                    v.o = _op->AListFinalize(v.o);
                    _stateStack.back() = STATE_ELEMENT_END;
                    _readPos = s + 1;
                }
                else if (strchr(_c_item_sep, _buf[s])) {
                    _readPos = s + 1;
                    _stateStack.push_back(STATE_ELEMENT_START);
                }
                else if (strchr(_c_kv_sep, _buf[s])) {
                    if (!v.hasTmp) {
                        throw ParseException("missing key element before '='");
                    }
                    else if (!v.isString && !v.isLiteral) {
                        throw ParseException("key element must be literal or string");
                    }
                    _readPos = s + 1;
                    _stateStack.back() = STATE_ALIST_WITH_KEY;
                    _stateStack.push_back(STATE_ELEMENT_START);
                }
                else if (strchr(_c_line_comment, _buf[s])) {
                    _readPos = limit;
                }
                else {
                    _readPos = s;
                    _stateStack.push_back(STATE_ELEMENT_START);
                }

                break;

            }
            case STATE_ELEMENT_START: {
                size_t s = _readPos;
                while (s < limit && strchr(_c_whitespace, _buf[s])) ++s;

                if (s >= limit) {
                    _readPos = limit;
                }
                else if (strchr(_c_open, _buf[s])) {
                    _valueStack.push_back(AListValue());
                    auto && v = _valueStack.back();
                    v.hasTmp = false;
                    v.tmp = nullptr;
                    v.o = _op->AListNew();
                    _stateStack.back() = STATE_ALIST;
                    _auxStack.push_back(strchr(_c_open, _buf[s]) - _c_open);
                    _readPos = s + 1;
                }
                else if (strchr(_c_quote, _buf[s])) {
                    _valueStack.push_back(AListValue());
                    auto && v = _valueStack.back();
                    v.hasTmp = false;
                    v.tmp = nullptr;
                    v.o = _op->StringNew();
                    _auxStack.push_back(strchr(_c_open, _buf[s]) - _c_open);

                    if (s + 2 < limit &&
                        _buf[s + 1] == _buf[s] && _buf[s + 2] == _buf[s]) {
                        _stateStack.back() = STATE_MULTILINE_STRING;
                        _readPos = s + 3;
                    }
                    else {
                        _stateStack.back() = STATE_QUOTED_STRING;
                        _readPos = s + 1;
                    }
                }
                else if (strchr(_c_line_comment, _buf[s])) {
                    _readPos = limit;
                }
                else {
                    size_t e = s;
                    while (e < limit && !_c_special.m[_buf[e]]) ++e;

                    if (e == s) {
                        throw ParseException("unexpected char at element start");
                    }

                    _valueStack.push_back(AListValue());
                    auto && v = _valueStack.back();
                    v.hasTmp = false;
                    v.tmp = nullptr;
                    v.o = _op->LiteralNew(_buf.data() + s, e - s);
                    v.isString = false;
                    v.isLiteral = true;

                    _auxStack.push_back(0);
                    _stateStack.back() = STATE_ELEMENT_END;
                    _readPos = e;
                }

                break;
            }
            }
        }

        CleanBuf();
    }

    void * Extract() override {
        if (_results.size() > 0) {
            auto v = _results.front();
            _results.pop_front();
            return v;
        }
        else {
            return nullptr;
        }
    }
};

IParser * alist::CreateAListParser(IOperator * op, bool multi,
                                   const char * c_whitespace,
                                   const char * c_line_comment,
                                   const char * c_item_sep,
                                   const char * c_kv_sep,
                                   const char * c_quote,
                                   const char * c_open,
                                   const char * c_close) {
    return new AListParser(multi, op, c_whitespace, c_line_comment,
                           c_item_sep, c_kv_sep, c_quote, c_open, c_close);
}

// #if __cplusplus < 201402L
// #include <sstream>
// string quoted(const string & s) {
//     ostringstream os;
//     os << '"';
//     for (int i = 0; i < s.size(); ++i) {
//         if (32 <= s[i] && s[i] <= 126) {
//             if (s[i] == '"' || s[i] == '\\')
//                 os << '\\';
//             os << s[i];
//         }
//         else os << '\\' << 'x' << "0123456789abcdef"[(unsigned char)s[i] >> 4] << "0123456789abcdef"[s[i] & 0xf];
//     }
//     os << '"';
//     return os.str();
// }
// #endif

// void alist::Dump(ostream & o, const IData * d) {
//     if (d == nullptr) {
//         o << "(NULL)";
//         return;
//     }

//     switch (d->GetType()) {
//     case IData::T_UNKNOWN:
//         o << "(UNKNOWN)";
//         break;
//     case IData::T_LITERAL:
//         o << d->GetString();
//         break;
//     case IData::T_STRING:
//         o << quoted(d->GetString());
//         break;
//     case IData::T_ALIST: {
//         o << '[';
//         bool first = true;
//         for (const IData * ele : d->GetList()) {
//             if (first) first = false;
//             else o << ',';
//             Dump(o, ele);
//         }
//         for (auto && kv : d->GetKVList()) {
//             if (first) first = false;
//             else o << ',';
//             o << get<0>(kv) << "=";
//             Dump(o, get<1>(kv));
//         }
//         o << ']';
//         break;
//     }
//     }
// }

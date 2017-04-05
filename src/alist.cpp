#include "alist.hpp"
#include <vector>
#include <iostream>
#include <deque>
#include <iomanip>

using namespace alist;
using namespace std;

const string _emptyString;
const list<const IData *> _emptyList;
const list<pair<string, const IData *>> _emptyKVList;

class DataImpl : public IData {
    Type _type;
    string * _str;
    list<const IData *> * _list;
    list<pair<string, const IData *>> * _kvList;

    friend class AListParser;

public:
    DataImpl()
        : _type(T_UNKNOWN)
        , _str(nullptr)
        , _list(nullptr)
        , _kvList(nullptr)
        { }

    Type GetType() const override {
        return _type;
    }

    const string & GetString() const override {
        if (_str) return *_str;
        else return _emptyString;
    }

    const list<const IData *> & GetList() const override {
        if (_list) return *_list;
        else return _emptyList;
    }

    const list<pair<string, const IData *>> & GetKVList() const override {
        if (_kvList) return *_kvList;
        else return _emptyKVList;
    }

    ~DataImpl() {
        delete _str;
        if (_list) {
            for (auto item : *_list) {
                delete item;
            }
        }

        if (_kvList) {
            for (auto && kv : *_kvList) {
                delete get<1>(kv);
            }
        }
    }
};

ParseException::ParseException(const char * w) : _what(w) { }
const char * ParseException::what() const noexcept { return _what.c_str(); }

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
};

static const CharMap<bool> SPECIAL_CHARS(" \t[]='\",#", false, true);
static const CharMap<bool> SEPARATOR_CHARS(" \t", false, true);
static const CharMap<int> HEX_TRANSLATE("0123456789abcdefABCDEF", -1,
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 10, 11, 12, 13, 14, 15}
);

#define IS_SPECIAL_CHAR(c) (SPECIAL_CHARS.m[c])
#define IS_SEPARATOR_CHAR(c) (SEPARATOR_CHARS.m[c])
#define BUF_CLEAN_THRESHOLD 4096

class AListParser : public IParser {
private:

    enum State {
        STATE_ELEMENT_START,
        STATE_ELEMENT_END,
        STATE_ALIST,
        STATE_ALIST_WITH_KEY,
        STATE_SQ_STRING,
        STATE_DQ_STRING,
        STATE_SML_STRING,
        STATE_DML_STRING
    };

    bool            _multi;
    bool            _sealed;
    string          _buf;
    size_t          _readPos;
    vector<string>  _keyStack;
    vector<IData *> _valueStack;
    vector<State>   _stateStack;
    deque<IData *>  _results;

public:

    AListParser(bool multi) {
        _multi = multi;
        _sealed = false;
        _readPos = 0;
        _stateStack.push_back(STATE_ELEMENT_START);
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
        for (auto d : _valueStack) {
            delete d;
        }
        _valueStack.clear();
    }

    ~AListParser() {
        Seal();
        for (auto d : _results) {
            delete d;
        }
    }

    void HandleEscape() {
        if (_readPos >= _buf.size()) return;
        auto && value = *(*(DataImpl *)_valueStack.back())._str;
        switch (_buf[_readPos]) {
        case 'n':
            value += '\n';
            ++_readPos;
            break;
        case 't':
            value += '\t';
            ++_readPos;
            break;
        case 'r':
            value += '\r';
            ++_readPos;
            break;
        case 'f':
            value += '\f';
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
            value += c;

            break;

        InputError:
            throw ParseException("Expect 2 hex chars for utf-8 escape");
        }
        case '\\':
        case '"':
        case '\'':
            value += _buf[_readPos];
            ++_readPos;
            break;
        default:
            value += '\\';
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
                _valueStack.pop_back();

                if (_stateStack.size() == 0) {
                    _results.push_back(value);

                    if (_multi) {
                        _stateStack.push_back(STATE_ELEMENT_START);
                    }

                    break;
                }

                switch (_stateStack.back()) {
                case STATE_ALIST:
                {
                    auto alist = (DataImpl *)_valueStack.back();
                    if (!alist->_list) {
                        alist->_list = new list<const IData *>();
                    }

                    ((DataImpl *)_valueStack.back())->_list->push_back(value);
                    break;
                }

                case STATE_ALIST_WITH_KEY:
                {
                    auto alist = (DataImpl *)_valueStack.back();
                    auto key = (DataImpl *)alist->_list->back();
                    alist->_list->pop_back();

                    if (!alist->_kvList) {
                        alist->_kvList = new list<pair<string, const IData *>>();
                    }

                    alist->_kvList->push_back(make_pair(key->GetString(), value));
                    _stateStack.back() = STATE_ALIST;
                    break;
                }

                default:
                    throw ParseException("invalid state to insert element");
                }
                break;
            }
            case STATE_SQ_STRING:
            case STATE_DQ_STRING: {
                size_t s = _readPos;
                if (state == STATE_SQ_STRING) {
                    while (s < limit && _buf[s] != '\'' && _buf[s] != '\\') ++s;
                }
                else {
                    while (s < limit && _buf[s] != '"' && _buf[s] != '\\') ++s;
                }

                ((DataImpl *)_valueStack.back())->_str->append(_buf, _readPos, s - _readPos);

                if (s >= limit) {
                    _readPos = limit;
                    _stateStack.back() = STATE_ELEMENT_END;
                }
                else {
                    if (_buf[s] == '\\') {
                        _readPos = s + 1;
                        HandleEscape();
                    }
                    else {
                        _readPos = s + 1;
                        _stateStack.back() = STATE_ELEMENT_END;
                    }
                }
                break;
            }
            case STATE_SML_STRING:
            case STATE_DML_STRING: {
                size_t s = _readPos;
                if (state == STATE_SML_STRING) {
                    while (s < limit && _buf[s] != '\'' && _buf[s] != '\\') ++s;
                }
                else {
                    while (s < limit && _buf[s] != '"' && _buf[s] != '\\') ++s;
                }

                ((DataImpl *)_valueStack.back())->_str->append(_buf, _readPos, s - _readPos);

                if (s >= limit) {
                    ((DataImpl *)_valueStack.back())->_str->push_back('\n');
                    _readPos = limit;
                }
                else {
                    if (_buf[s] == '\\') {
                        _readPos = s + 1;
                        HandleEscape();
                    }
                    else if (s + 2 < limit && _buf[s + 1] == _buf[s] && _buf[s + 2] == _buf[s]) {
                        _readPos = s + 3;
                        _stateStack.back() = STATE_ELEMENT_END;
                    }
                    else {
                        ((DataImpl *)_valueStack.back())->_str->push_back(_buf[s]);
                        _readPos = s + 1;
                    }
                }
                break;
            }
            case STATE_ALIST:
            case STATE_ALIST_WITH_KEY: {
                size_t s = _readPos;
                while (s < limit && IS_SEPARATOR_CHAR(_buf[s])) ++s;

                if (s >= limit) {
                    _readPos = limit;
                }
                else if (_buf[s] == ']') {
                    _stateStack.back() = STATE_ELEMENT_END;
                    _readPos = s + 1;
                }
                else if (_buf[s] == ',') {
                    _readPos = s + 1;
                    _stateStack.push_back(STATE_ELEMENT_START);
                }
                else if (_buf[s] == '#') {
                    _readPos = limit;
                }
                else if (_buf[s] == '=') {
                    if (state == STATE_ALIST) {
                        auto alist = (DataImpl*)_valueStack.back();
                        if (!alist->_list || alist->_list->size() == 0) {
                            throw ParseException("missing key element before '='");
                        }
                        else if (alist->_list->front()->GetType() != IData::T_LITERAL
                                 && alist->_list->front()->GetType() != IData::T_STRING) {
                            throw ParseException("key element must be literal or string");
                        }
                        _readPos = s + 1;
                        _stateStack.back() = STATE_ALIST_WITH_KEY;
                    }
                    else {
                        throw ParseException("unexpected '='");
                    }
                }
                else {
                    _readPos = s;
                    _stateStack.push_back(STATE_ELEMENT_START);
                }

                break;
            }
            case STATE_ELEMENT_START: {
                size_t s = _readPos;
                while (s < limit && IS_SEPARATOR_CHAR(_buf[s])) ++s;

                if (s >= limit) {
                    _readPos = limit;
                }
                else if (_buf[s] == '[') {
                    DataImpl * data = new DataImpl();
                    data->_type = IData::T_ALIST;

                    _valueStack.push_back(data);
                    _stateStack.back() = STATE_ALIST;
                    _readPos = s + 1;
                }
                else if (_buf[s] == '"' || _buf[s] == '\'') {
                    DataImpl * data = new DataImpl();
                    data->_type = IData::T_STRING;
                    data->_str = new string();

                    _valueStack.push_back(data);
                    if (s + 2 < limit &&
                        _buf[s + 1] == _buf[s] && _buf[s + 2] == _buf[s]) {
                        _stateStack.back() = _buf[s] == '"' ? STATE_DML_STRING : STATE_SML_STRING;
                        _readPos = s + 3;
                    }
                    else {
                        _stateStack.back() = _buf[s] == '"' ? STATE_DQ_STRING : STATE_SQ_STRING;
                        _readPos = s + 1;
                    }
                }
                else if (_buf[s] == '#') {
                    _readPos = limit;
                }
                else {
                    size_t e = s;
                    while (e < limit && !IS_SPECIAL_CHAR(_buf[e])) ++e;

                    if (e == s) {
                        throw ParseException("unexpected char at element start");
                    }

                    DataImpl * data = new DataImpl();
                    data->_type = IData::T_LITERAL;
                    data->_str = new string(_buf, s, e - s);

                    _valueStack.push_back(data);

                    _stateStack.back() = STATE_ELEMENT_END;
                    _readPos = e;
                }

                break;
            }
            }
        }

        CleanBuf();
    }

    IData * Extract() override {
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

IParser * alist::CreateAListParser(bool multi) {
    return new AListParser(multi);
}

#if __cplusplus < 201402L
#include <sstream>
string quoted(const string & s) {
    ostringstream os;
    os << '"';
    for (int i = 0; i < s.size(); ++i) {
        if (32 <= s[i] && s[i] <= 126) {
            if (s[i] == '"' || s[i] == '\\')
                os << '\\';
            os << s[i];
        }
        else os << '\\' << 'x' << "0123456789abcdef"[(unsigned char)s[i] >> 4] << "0123456789abcdef"[s[i] & 0xf];
    }
    os << '"';
    return os.str();
}
#endif

void alist::Dump(ostream & o, const IData * d) {
    if (d == nullptr) {
        o << "(NULL)";
        return;
    }

    switch (d->GetType()) {
    case IData::T_UNKNOWN:
        o << "(UNKNOWN)";
        break;
    case IData::T_LITERAL:
        o << d->GetString();
        break;
    case IData::T_STRING:
        o << quoted(d->GetString());
        break;
    case IData::T_ALIST: {
        o << '[';
        bool first = true;
        for (const IData * ele : d->GetList()) {
            if (first) first = false;
            else o << ',';
            Dump(o, ele);
        }
        for (auto && kv : d->GetKVList()) {
            if (first) first = false;
            else o << ',';
            o << get<0>(kv) << "=";
            Dump(o, get<1>(kv));
        }
        o << ']';
        break;
    }
    }
}

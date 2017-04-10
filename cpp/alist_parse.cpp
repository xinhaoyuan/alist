#include "alist.hpp"
#include <list>
#include <string>
#include <iostream>
#include <utility>
#include <stdexcept>
#include <iomanip>

using namespace alist;
using namespace std;

class Data;

const string _emptyString;
const list<const Data *> _emptyList;
const list<pair<string, const Data *>> _emptyKVList;

class Data {
public:
    enum Type {
        T_UNKNOWN = 0,
        T_LITERAL,
        T_STRING,
        T_ALIST
    };

private:
    Type _type;
    string * _str;
    list<const Data *> * _list;
    list<pair<string, const Data *>> * _kvList;

    friend class ParseOperator;

public:
    Data()
        : _type(T_UNKNOWN)
        , _str(nullptr)
        , _list(nullptr)
        , _kvList(nullptr)
        { }

    Type GetType() const {
        return _type;
    }

    const string & GetString() const {
        if (_str) return *_str;
        else return _emptyString;
    }

    const list<const Data *> & GetList() const {
        if (_list) return *_list;
        else return _emptyList;
    }

    const list<pair<string, const Data *>> & GetKVList() const {
        if (_kvList) return *_kvList;
        else return _emptyKVList;
    }

    ~Data() {
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

class ParseOperator : public IOperator {
public:
    void * AListNew() {
        auto ret = new Data();
        ret->_type = Data::T_ALIST;
        return ret;
    }

    void * AListAppendItem(void * _d, void * i) {
        auto d = (Data *)_d;
        if (d->_list == nullptr) {
            d->_list = new list<const Data *>();
        }
        d->_list->push_back((Data *)i);
        return d;
    }

    void * AListAppendKV(void * _d, void * _k, bool isLiteral, void * _v) {
        auto d = (Data *)_d;
        auto k = (Data *)_k;
        auto v = (Data *)_v;
        if (d->_kvList == nullptr) {
            d->_kvList = new list<pair<string, const Data *>>();
        }
        d->_kvList->push_back(make_pair(k->GetString(), v));
        return d;
    }

    void * AListFinalize(void * d) {
        return d;
    }

    void * StringNew() {
        auto ret = new Data();
        ret->_type = Data::T_STRING;
        return ret;
    }

    void * StringAppendByte(void * _d, unsigned char b) {
        auto d = (Data *)_d;
        if (d->_str == nullptr) {
            d->_str = new string();
        }

        d->_str->push_back(b);
        return d;
    }

    void * StringAppendByteArray(void * _d, const unsigned char * ba, int len) {
        auto d = (Data *)_d;
        if (d->_str == nullptr) {
            d->_str = new string();
        }

        d->_str->append((const char *)ba, len);
        return d;
    }

    void * StringFinalize(void * d) {
        return d;
    }

    void * LiteralNew(const char * s, int len) {
        auto ret = new Data();
        ret->_type = Data::T_LITERAL;
        ret->_str = new string(s, len);
        return ret;
    }

    void * Free(void * _d) {
        auto d = (Data *)_d;
        delete d;
    }
};

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

void Dump(ostream & o, const Data * d) {
    if (d == nullptr) {
        o << "(NULL)";
        return;
    }

    switch (d->GetType()) {
    case Data::T_UNKNOWN:
        o << "(UNKNOWN)";
        break;
    case Data::T_LITERAL:
        o << d->GetString();
        break;
    case Data::T_STRING:
        o << quoted(d->GetString());
        break;
    case Data::T_ALIST: {
        o << '[';
        bool first = true;
        for (const Data * ele : d->GetList()) {
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

int main() {
    auto op = new ParseOperator();
    IParser * parser = CreateAListParser(op);

    string line;
    int line_num = 1;
    bool succ;
    while (true) {
        succ = !!getline(cin, line);
        try {
            if (succ) {
                parser->ParseLine(line);
            }
            else {
                parser->Seal();
            }
        }
        catch (const ParseException & e) {
            if (succ) {
                cerr << "Parsing error at line " << line_num << ": " << e.what() << endl;
            }
            else {
                cerr << "Parsing error when sealing: " << e.what() << endl;
            }
        }

        if (!succ) break;
        ++line_num;
    }

    while (true) {
        auto v = parser->Extract();
        if (v == nullptr) break;
        Dump(cout, (Data *)v); cout << endl;
    }

    return 0;
}

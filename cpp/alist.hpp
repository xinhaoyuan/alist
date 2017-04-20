#ifndef __ALIST__
#define __ALIST__

#include <iostream>
#include <stdexcept>
#include <list>
#include <utility>

namespace alist {
    class IOperator {
    public:
        virtual void * AListNew() = 0;
        virtual void * AListAppendItem(void * d, void * i) = 0;
        virtual void * AListAppendKV(void * d, void * key, bool isLiternal, void * value) = 0;
        virtual void * AListFinalize(void * d) = 0;
        virtual void * StringNew() = 0;
        virtual void * StringAppendByte(void * d, unsigned char b) = 0;
        virtual void * StringAppendByteArray(void * d, const unsigned char * b, int l) = 0;
        virtual void * StringFinalize(void * d) = 0;
        virtual void * LiteralNew(const char * str, int len) = 0;
        virtual void * Free(void * d) = 0;

        virtual ~IOperator() = default;
    };

    class IParser {
    public:
        virtual void ParseLine(const std::string & line) = 0;
        virtual void Seal() = 0;
        virtual void * Extract() = 0;
        virtual ~IParser() = default;
    };

    class ParseException : public std::exception {
    private:
        std::string _what;
    public:
        ParseException(const char * w);
        const char * what() const noexcept override;
    };

    IParser * CreateParser(IOperator * op = NULL, bool multi = true,
                           const char * c_whitespace = " \t",
                           const char * c_line_comment = "#",
                           const char * c_item_sep = ",",
                           const char * c_kv_sep = ":=",
                           const char * c_quote = "'\"",
                           const char * c_open = "[{",
                           const char * c_close = "]}");

    class IData {
    public:
        enum Type {
            T_UNKNOWN = 0,
            T_LITERAL,
            T_STRING,
            T_ALIST
        };

        virtual Type GetType() const = 0;
        virtual const std::string & GetString() const = 0;
        virtual const std::list<const IData *> & GetList() const = 0;
        virtual const std::list<std::pair<std::string, const IData *>> & GetKVList() const = 0;
        virtual ~IData() = default;
    };

    void Dump(std::ostream & o, const IData * d);
}

#endif

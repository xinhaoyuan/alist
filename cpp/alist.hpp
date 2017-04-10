#ifndef __ALIST__
#define __ALIST__

#include <iostream>
#include <stdexcept>

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

    IParser * CreateAListParser(IOperator * op, bool multi = true,
                                const char * c_whitespace = " \t",
                                const char * c_line_comment = "#",
                                const char * c_item_sep = ",",
                                const char * c_kv_sep = ":=",
                                const char * c_quote = "'\"",
                                const char * c_open = "[{",
                                const char * c_close = "]}");
}

#endif

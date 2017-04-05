#ifndef __ALIST__
#define __ALIST__

#include <string>
#include <list>
#include <map>
#include <iostream>
#include <stdexcept>

namespace alist {
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
        virtual const std::map<std::string, const IData *> & GetDict() const = 0;
        virtual ~IData() = default;
    };

    class IParser {
    public:
        virtual void ParseLine(const std::string & line) = 0;
        virtual void Seal() = 0;
        virtual IData * Extract() = 0;
        virtual ~IParser() = default;
    };

    class ParseException : public std::exception {
    private:
        std::string _what;
    public:
        ParseException(const char * w);
        const char * what() const noexcept override;
    };

    IParser * CreateAListParser(bool multi = true);
    void Dump(std::ostream & o, const IData * d);
}

#endif

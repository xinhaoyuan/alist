#include "alist.hpp"
#include <list>
#include <string>
#include <iostream>
#include <utility>
#include <stdexcept>
#include <iomanip>

using namespace alist;
using namespace std;

int main() {
    IParser * parser = CreateParser();

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
        Dump(cout, (IData *)v); cout << endl;
    }

    return 0;
}

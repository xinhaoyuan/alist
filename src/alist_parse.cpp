#include "alist.hpp"
#include <iostream>
#include <stdexcept>

using namespace alist;
using namespace std;

int main() {
    IParser * parser = CreateAListParser(true);

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
        Dump(cout, v); cout << endl;
    }
    
    return 0;
}

#include "emuscale/core/Diagnostics.h"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: emuscale_analyze <session-directory>\n"; return 2; }
    try { std::cout << emuscale::analyze_session(argv[1]); return 0; }
    catch (const std::exception& e) { std::cerr << "Analysis failed: " << e.what() << '\n'; return 1; }
}

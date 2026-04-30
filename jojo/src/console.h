#pragma once
#include <string>

namespace Console {
    void init();
    void clear();
    void print(const std::string& text);
    void println(const std::string& text);
    void errormsg(const std::string& text);
    void titlebar(const std::string& process);
    void colortxt(std::string& text, const std::string& color);
}
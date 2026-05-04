#pragma once
#include <string>

namespace Console {
    void init();
    void clear();
    void print(const std::string& text);
    void println(const std::string& text);
    void errormsg(const std::string& type, const std::string& text);
    void titlebar(const std::string& title);
    void colortxt(const std::string& text, const std::string& color);
}
#include "console.h"
#include "kernel.h"
#include <windows.h>
#include <iostream>

namespace {
    void enableANSI() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, mode);
    }
}

namespace Console {
    constexpr const char* reset = "\033[0m";
    constexpr const char* bold = "\033[1m";
    constexpr const char* red = "\033[31m";
    constexpr const char* f = "\033[48;2;R;G;Bm";

    void init() {
        enableANSI();
    }

    void clear() {
        std::cout << "\033[2J\033[H";
    }

    void print(const std::string& text) {
        std::cout << text;
    }

    void println(const std::string& text) {
        std::cout << text << "\n";
    }
    
    void errormsg(const std::string& type, const std::string& text) {
        std::cout<< bold << type + " " << reset;
        std::cout << "\033[31mERROR: " << text << "\033[0m\n";
    }
    
    void titlebar() {
        Console::clear();
        Console::println("\033[36mJOJO OS v0.2.0\033[0m");
        Console::println("Type 'help' to begin\n");
    }
    void colortxt(const std::string& text, const std::string& color) {
        std::string colorCode;
        if (color == "red") colorCode = "\033[31m";
        else if (color == "green") colorCode = "\033[32m";
        else if (color == "yellow") colorCode = "\033[33m";
        else if (color == "blue") colorCode = "\033[34m";
        else if (color == "magenta") colorCode = "\033[35m";
        else if (color == "cyan") colorCode = "\033[36m";
        else colorCode = "\033[0m"; // default

        std::cout << colorCode << text << "\033[0m";
    }
}

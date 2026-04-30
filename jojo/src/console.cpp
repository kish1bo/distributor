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
    
    void errormsg(const std::string& text) {
        std::cout << "\033[31mError: " << text << "\033[0m\n";
    }
    
    void titlebar(const std::string& process) {
        Console::clear();
        Console::println("\033[36mJOJO OS v0.2\033[0m");
        Console::println("\033[32m" + process + "\033[0m\n");
        Console::println("Type 'help' to begin\n");
    }
    void colortxt(std::string text, std::string color) {
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

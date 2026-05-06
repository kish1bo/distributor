#include "console.h"
#include "kernel.h"
#include <windows.h>
#include <iostream>

#define VERSION = "0.3.1";

namespace {
    void enableANSI() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, mode);
    }
}

SystemState systemState;
User* currentUser;
namespace Console {
    constexpr const char* reset = "\033[0m";
    constexpr const char* bold = "\033[1m";
    constexpr const char* red = "\033[31m";
    constexpr const char* f = "\033[48;2;R;G;Bm";

    std::string buildPrompt(const std::string activeApp) {
    if(systemState == SystemState::LOGGED_OUT && activeApp.empty()) {         //......................................................................................................
        return "\033[35m@jojOS:\033[0m\033[33m/\033[31m$\033[0m ";            //
    }                                                                         //
    else if(systemState == SystemState::LOGGED_OUT && !activeApp.empty()) {   //                    activeApp state for not logged user
        return "\033[32m:" + activeApp + "\033[0m\033[33m/\033[31m$\033[0m "; // 
    }                                                                         //
    else if(systemState == SystemState::USER && activeApp.empty()) {          //.......................................................................................................
        return "\033[36m" + currentUser->username + "\033[35m@jojOS:\033[0m\033[33m/\033[31m$\033[0m ";         //
    }                                                                                                           //
    else if(systemState == SystemState::USER && !activeApp.empty()) {                                           //        activeApp state for logged user
        return "\033[36m" + currentUser->username + "\033[32m" + activeApp +"\033[0m\033[33m/\033[31m$\033[0m ";//
    }                                                                                                           //.....................................................................
    else if(systemState == SystemState::GUEST){                                                                 //
        return "\033[36m" + currentUser->username + "\033[35m@jojOS:\033[0m\033[33m/\033[31m$\033[0m ";         //        Guest do not open the apps
    }                                                                                                           //.....................................................................
    else if(systemState == SystemState::ADMIN && activeApp.empty()){                                            //
       return "\033[31m" + currentUser->username + "\033[35m@jojOS:\033[0m\033[33m/\033[31m$\033[0m ";          //
    }                                                                                                           //        activeApp state for admins
    else if (systemState == SystemState::ADMIN && !activeApp.empty()) {                                         //
        return "\033[36m" + currentUser->username + "\033[32m" + activeApp +"\033[0m\033[33m/\033[31m$\033[0m"; //
    }                                                                                                           //.....................................................................
    else {                                                                                                      //
        return "\033[35m@jojOS__unresolvedprompt:\033[0m\033[33m/\033[31m$\033[0m ";                            //        <unresolved prompt>
    }                                                                                                           //.....................................................................
}

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
        Console::println("\033[36mJOJO OS" + VERSION + "\033[0m");
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

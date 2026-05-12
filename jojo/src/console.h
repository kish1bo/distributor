#pragma once
#include <string>
#include "kernel.h"

namespace Console {
    void init();
    void clear();
    std::string buildPrompt(const std::string& activeApp = "", const SystemState& systemState = SystemState::LOGGED_OUT, User* currentUser = nullptr);
    void print(const std::string& text);
    void println(const std::string& text);
    void errormsg(const std::string& type, const std::string& text);
    void titlebar(const std::string& title = "");
    void colortxt(const std::string& text, const std::string& color);
}
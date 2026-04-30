#pragma once
#include <string>

class InstalatorTool {
public:
    void run();
    void install(const std::string& package);
    void uninstall(const std::string& package);
    void listInstalled();
}; 
#pragma once
#include <string>

class InstalatorTool {
public:
    void run();
    void install(const std::string& directory, const std::string& package);
    void uninstall(const std::string& package);
    void listInstalled(const std::string& parameter);
}; 
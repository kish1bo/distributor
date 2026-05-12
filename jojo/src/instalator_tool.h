#pragma once
#include <string>
#include <vector>

class InstalatorTool {
public:
    void run();
    void install(const std::vector<std::string>& args);
    void uninstall(const std::string& package);
    void listInstalled() const;
    void listAvailable() const;
    void search(const std::string& filter) const;
    void showInfo(const std::string& package) const;
}; 
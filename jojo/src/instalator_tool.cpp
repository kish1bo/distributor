#include "instalator_tool.h"
#include "console.h"
#include "kernel.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <algorithm>

std::vector<std::string> installedPackages = {"git", "curl", "vim"};

void InstalatorTool::listInstalled(const std::string& parameter) {
    Console::println("Installed packages:");
    std::ifstream infile("downloads/packages.txt");
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            Console::println("- " + line);
        }
        infile.close();
    } else {
        Console::errormsg("FILE_NOT_FOUND", "Failed to read" + parameter);
    }
}

void InstalatorTool::install(const std::string& dir, const std::string& package) {
    installedPackages.push_back(package);
    Console::println("Installing package: " + package);
    std::this_thread::sleep_for(std::chrono::seconds(2)); // Simulate installation time
    std::ofstream outfile(dir, std::ios::app);
    if (outfile.is_open()) {
        outfile << package << std::endl;
        outfile.close();
    } else {
        Console::errormsg("DIR_NOT_FOUND", "Failed to write to " + dir);
    }
    Console::println("Package '" + package + "' installed.");
}

void InstalatorTool::uninstall(const std::string& package) {
    installedPackages.erase(std::remove(installedPackages.begin(), installedPackages.end(), package), installedPackages.end());
    Console::println("Uninstalling package: " + package);
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Simulate uninstallation time
    Console::println("Package '" + package + "' uninstalled.");
}

void InstalatorTool::run() {
    Console::titlebar("Instalator");
    Console::println("Welcome to the Instalator Tool. Type 'help -client' for commands.");
    std::string input;
    while (true) {
        Console::print("> ");
        std::getline(std::cin, input);
        if (input == "exit") {
            break;
        } else if (input == "help -client") {
            Console::println("Available commands:");
            Console::println("- install <directory> <package>: Install a package");
            Console::println("- uninstall <package>: Uninstall a package");
            Console::println("- list: List all installed packages");
            Console::println("- exit: Exit the tool");
        } else if (input.rfind("install ", 0) == 0) {
            std::string dir = input.substr(8);
            std::string package = input.substr(dir.length() + 9);
            install(dir, package);
        } else if (input.rfind("uninstall ", 0) == 0) {
            std::string package = input.substr(10);
            uninstall(package);
        } else if (input == "list") {
            listInstalled();
        } else {
            Console::errormsg("UNKNOWN_COMMAND", "Type 'help -client' for available commands.");
        }
    }
}
